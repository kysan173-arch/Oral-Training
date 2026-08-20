#pragma once

struct UserContext {
  std::string id;
  std::string role;
  std::string display_name;

  bool isAdmin() const { return role == "admin"; }
};

class SlidingWindowRateLimiter {
 public:
  explicit SlidingWindowRateLimiter(int limit) : limit_(limit) {}

  bool allow(const std::string& key) {
    const auto now = std::chrono::steady_clock::now();
    const auto cutoff = now - std::chrono::minutes(1);
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entries = entries_[key];
    while (!entries.empty() && entries.front() < cutoff) entries.pop_front();
    if (static_cast<int>(entries.size()) >= limit_) return false;
    entries.push_back(now);
    if (entries_.size() > 10000) {
      for (auto iterator = entries_.begin(); iterator != entries_.end();) {
        if (iterator->second.empty() || iterator->second.back() < cutoff) {
          iterator = entries_.erase(iterator);
        } else {
          ++iterator;
        }
      }
    }
    return true;
  }

 private:
  int limit_;
  std::mutex mutex_;
  std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> entries_;
};

class IdentityService {
 public:
  explicit IdentityService(Config config)
      : config_(std::move(config)), limiter_(config_.rate_limit_per_minute),
        login_limiter_(std::max(10, config_.rate_limit_per_minute / 4)) {}

  json login(const crow::request& request, const std::string& code) {
    enforceTransportAndOrigin(request);
    if (!login_limiter_.allow("login|" + request.remote_ip_address)) {
      throw ApiError(429, "RATE_LIMITED", "登录请求过于频繁，请稍后重试");
    }
    std::string user_id;
    if (config_.auth_mode == "demo") {
      user_id = kDemoUserId;
    } else {
      const auto cleaned_code = trim(code);
      if (cleaned_code.empty() || cleaned_code.size() > 256) {
        throw ApiError(400, "INVALID_ARGUMENT", "微信登录 code 无效");
      }
      user_id = resolveWechatUser(cleaned_code);
    }
    return createSession(user_id);
  }

  json switchRole(const crow::request& request, const std::string& target_role) {
    if (config_.production) {
      throw ApiError(403, "FORBIDDEN", "生产环境不支持角色切换");
    }
    if (config_.auth_mode != "demo") {
      throw ApiError(403, "FORBIDDEN", "仅 Demo 模式支持角色切换");
    }
    if (target_role != "learner" && target_role != "admin") {
      throw ApiError(400, "INVALID_ARGUMENT", "角色必须为 learner 或 admin");
    }
    const auto user = authorize(request);
    pqxx::connection connection(config_.database_url);
    pqxx::work tx(connection);
    tx.exec_params("UPDATE users SET role = $1, updated_at = NOW() WHERE id = $2", target_role, user.id);
    tx.commit();
    return createSession(user.id);
  }

  UserContext authorize(const crow::request& request, bool learner_only = false) {
    enforceTransportAndOrigin(request);
    if (!limiter_.allow("ip|" + request.remote_ip_address)) {
      throw ApiError(429, "RATE_LIMITED", "请求过于频繁，请稍后重试");
    }
    const auto authorization = request.get_header_value("Authorization");
    constexpr char prefix[] = "Bearer ";
    if (authorization.rfind(prefix, 0) != 0 || authorization.size() <= sizeof(prefix) - 1) {
      throw ApiError(401, "AUTH_REQUIRED", "请先登录");
    }
    const auto token = authorization.substr(sizeof(prefix) - 1);
    if (token.size() < 32 || token.size() > 256) throw ApiError(401, "AUTH_INVALID", "登录状态无效");
    pqxx::connection connection(config_.database_url);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT users.id, users.role, users.display_name
      FROM auth_sessions JOIN users ON users.id = auth_sessions.user_id
      WHERE auth_sessions.token_hash = $1 AND auth_sessions.expires_at > NOW()
        AND users.status = 'active'
      FOR UPDATE OF auth_sessions
    )", sha256Hex(token));
    if (rows.empty()) throw ApiError(401, "AUTH_EXPIRED", "登录已过期，请重新登录");
    const UserContext user{rows[0]["id"].c_str(), rows[0]["role"].c_str(),
                           rows[0]["display_name"].c_str()};
    if (learner_only && user.role != "learner") {
      throw ApiError(403, "ROLE_FORBIDDEN", "管理员账号不能使用学员训练与个人成长功能");
    }
    if (!limiter_.allow("user|" + user.id)) {
      throw ApiError(429, "RATE_LIMITED", "请求过于频繁，请稍后重试");
    }
    tx.exec_params("UPDATE auth_sessions SET last_seen_at = NOW() WHERE token_hash = $1", sha256Hex(token));
    tx.commit();
    return user;
  }

  bool runtimeKeyAllowed() const { return config_.allow_runtime_api_key; }
  bool production() const { return config_.production; }
  const std::string& authMode() const { return config_.auth_mode; }

 private:
  void enforceTransportAndOrigin(const crow::request& request) const {
    if (config_.require_https && request.get_header_value("X-Forwarded-Proto") != "https") {
      throw ApiError(400, "HTTPS_REQUIRED", "生产环境仅接受 HTTPS 请求");
    }
    const auto origin = request.get_header_value("Origin");
    if (!origin.empty() && config_.allowed_origin != "*" && origin != config_.allowed_origin) {
      throw ApiError(403, "ORIGIN_FORBIDDEN", "请求来源不受信任");
    }
  }

  std::string resolveWechatUser(const std::string& code) const {
    const auto path = std::string("/sns/jscode2session?appid=") + urlEncode(config_.wechat_app_id) +
        "&secret=" + urlEncode(config_.wechat_app_secret) + "&js_code=" + urlEncode(code) +
        "&grant_type=authorization_code";
    const auto response = getHttps(L"api.weixin.qq.com", toWide(path));
    if (response.status < 200 || response.status >= 300) {
      throw ApiError(503, "AUTH_UPSTREAM_ERROR", "微信登录服务暂时不可用");
    }
    const auto payload = json::parse(response.body, nullptr, false);
    if (!payload.is_object()) throw ApiError(503, "AUTH_UPSTREAM_ERROR", "微信登录响应无效");
    if (payload.contains("errcode") && payload["errcode"].is_number_integer() &&
        payload["errcode"].get<int>() != 0) {
      throw ApiError(401, "WECHAT_LOGIN_FAILED", "微信登录凭证无效或已过期");
    }
    const auto openid = jsonString(payload, "openid");
    if (openid.empty()) throw ApiError(401, "WECHAT_LOGIN_FAILED", "微信登录未返回用户标识");
    const auto user_id = "wx_" + sha256Hex(openid).substr(0, 32);
    pqxx::connection connection(config_.database_url);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(R"(
      INSERT INTO users(id, wechat_openid, display_name, role, status)
      VALUES ($1, $2, '微信用户', 'learner', 'active')
      ON CONFLICT (wechat_openid) DO UPDATE SET updated_at = NOW()
      RETURNING id
    )", user_id, openid);
    const auto resolved_id = std::string(rows[0]["id"].c_str());
    tx.commit();
    return resolved_id;
  }

  json createSession(const std::string& user_id) const {
    const auto token = randomToken();
    pqxx::connection connection(config_.database_url);
    pqxx::work tx(connection);
    tx.exec("DELETE FROM auth_sessions WHERE expires_at <= NOW()");
    tx.exec_params(R"(
      INSERT INTO auth_sessions(token_hash, user_id, expires_at)
      VALUES ($1, $2, NOW() + ($3 * INTERVAL '1 second'))
    )", sha256Hex(token), user_id, config_.auth_token_ttl_seconds);
    const auto users = tx.exec_params(
        "SELECT id, role, display_name FROM users WHERE id = $1 AND status = 'active'", user_id);
    if (users.empty()) throw ApiError(403, "USER_DISABLED", "用户不可用");
    const json user = {{"id", users[0]["id"].c_str()}, {"role", users[0]["role"].c_str()},
                       {"displayName", users[0]["display_name"].c_str()}};
    tx.commit();
    return {{"accessToken", token}, {"expiresIn", config_.auth_token_ttl_seconds}, {"user", user}};
  }

  Config config_;
  SlidingWindowRateLimiter limiter_;
  SlidingWindowRateLimiter login_limiter_;
};
