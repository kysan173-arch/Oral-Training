#pragma once

// Included in main.cpp after its HTTP/JSON helpers. Secrets are bound to the
// backend Windows service account; no application encryption environment key.
struct GatewayEndpoint {
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port;
  bool secure;
};

GatewayEndpoint gatewayEndpoint(const std::string& base_url) {
  if (base_url.empty() || base_url.size() > 2048 ||
      base_url.find_first_of("\r\n\t ?#\\") != std::string::npos)
    throw ApiError(400, "INVALID_ARGUMENT", "请输入有效的 LiteLLM Base URL，不含查询参数");
  const auto wide = toWide(base_url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwUserNameLength =
      parts.dwPasswordLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.dwHostNameLength == 0 ||
      parts.dwUserNameLength || parts.dwPasswordLength || parts.dwExtraInfoLength ||
      (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS))
    throw ApiError(400, "INVALID_ARGUMENT", "LiteLLM 地址须使用 HTTP/HTTPS，且不能包含用户名或密码");
  std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::transform(host.begin(), host.end(), host.begin(), ::towlower);
  const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  if (!secure && host != L"127.0.0.1" && host != L"localhost" && host != L"[::1]" && host != L"::1")
    throw ApiError(400, "INVALID_ARGUMENT", "远程 LiteLLM 网关必须使用 HTTPS；本机可使用 HTTP");
  std::wstring path(parts.lpszUrlPath ? parts.lpszUrlPath : L"", parts.dwUrlPathLength);
  while (!path.empty() && path.back() == L'/') path.pop_back();
  if (path.empty()) path = L"/v1";
  if (path.find(L"/chat/completions") != std::wstring::npos)
    throw ApiError(400, "INVALID_ARGUMENT", "请输入 Base URL（例如 /v1），不要包含 /chat/completions");
  return {host, path + L"/chat/completions", parts.nPort, secure};
}

std::string protectGatewayKey(const std::string& key) {
  DATA_BLOB input{static_cast<DWORD>(key.size()), reinterpret_cast<BYTE*>(const_cast<char*>(key.data()))};
  DATA_BLOB output{};
  if (!CryptProtectData(&input, L"Oral Training LiteLLM", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &output))
    throw ApiError(503, "MODEL_CONFIG_ENCRYPTION_FAILED", "模型密钥加密失败，配置未保存");
  const char* digits = "0123456789abcdef";
  std::string encrypted;
  for (DWORD i = 0; i < output.cbData; ++i) {
    encrypted += digits[output.pbData[i] >> 4];
    encrypted += digits[output.pbData[i] & 15];
  }
  LocalFree(output.pbData);
  return encrypted;
}

std::string unprotectGatewayKey(const std::string& encrypted) {
  auto fail = [] { throw ApiError(503, "MODEL_CONFIG_UNREADABLE", "模型密钥无法解密，请在个人配置中重新保存 API Key"); };
  if (encrypted.empty() || encrypted.size() % 2) fail();
  std::vector<BYTE> bytes;
  auto digit = [&](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    fail(); return 0;
  };
  for (size_t i = 0; i < encrypted.size(); i += 2)
    bytes.push_back(static_cast<BYTE>((digit(encrypted[i]) << 4) | digit(encrypted[i + 1])));
  DATA_BLOB input{static_cast<DWORD>(bytes.size()), bytes.data()}, output{};
  if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) fail();
  std::string key(reinterpret_cast<char*>(output.pbData), output.cbData);
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return key;
}

struct GatewaySettings {
  std::string base_url;
  std::string model;
  std::string encrypted_key;
  long long revision = 0;
  bool configured() const { return !base_url.empty() && !model.empty() && !encrypted_key.empty(); }
  json status() const {
    return {{"provider", "litellm"}, {"scope", "personal"}, {"configured", configured()}, {"canEdit", true},
            {"baseUrl", base_url}, {"model", model},
            {"hasApiKey", !encrypted_key.empty()}, {"revision", revision}};
  }
};

class GatewaySettingsStore {
 public:
  explicit GatewaySettingsStore(std::shared_ptr<DatabasePool> pool) : pool_(std::move(pool)) {}
  GatewaySettings load(const std::string& user_id) const {
    if (!pool_ || user_id.empty()) return {};
    auto connection = pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    return fromRows(tx.exec_params("SELECT * FROM user_model_gateway_settings WHERE user_id=$1", user_id));
  }
  GatewaySettings save(const json& body, const std::string& actor, bool clear = false) {
    if (actor.empty()) throw ApiError(401, "AUTH_REQUIRED", "请先登录");
    if (!body.contains("revision") || !body["revision"].is_number_integer())
      throw ApiError(400, "INVALID_ARGUMENT", "缺少配置版本，请刷新后重试");
    if (!clear && (!body.contains("baseUrl") || !body["baseUrl"].is_string() ||
        !body.contains("model") || !body["model"].is_string() ||
        (body.contains("apiKey") && !body["apiKey"].is_string())))
      throw ApiError(400, "INVALID_ARGUMENT", "网关地址、模型名称和 API Key 必须为文本");
    auto connection = pool_->acquire();
    pqxx::work tx(connection.get());
    tx.exec_params("INSERT INTO user_model_gateway_settings(user_id) VALUES($1) ON CONFLICT DO NOTHING", actor);
    auto settings = fromRows(tx.exec_params("SELECT * FROM user_model_gateway_settings WHERE user_id=$1 FOR UPDATE", actor));
    if (settings.revision != body["revision"].get<long long>())
      throw ApiError(409, "MODEL_CONFIG_CONFLICT", "配置已被更新，请重新加载后保存");
    if (clear) {
      settings.base_url.clear(); settings.model.clear(); settings.encrypted_key.clear();
    } else {
      auto url = trim(jsonString(body, "baseUrl"));
      while (!url.empty() && url.back() == '/') url.pop_back();
      gatewayEndpoint(url);
      const auto model = trim(jsonString(body, "model"));
      if (model.empty() || model.size() > 200 || model.find_first_of("\r\n\t ") != std::string::npos)
        throw ApiError(400, "INVALID_ARGUMENT", "请输入 LiteLLM 中已配置的模型名称");
      const auto key = jsonString(body, "apiKey");
      if (!key.empty()) {
        if (key.size() > 2048 || std::any_of(key.begin(), key.end(), [](unsigned char c) { return c < 33 || c > 126; }))
          throw ApiError(400, "INVALID_ARGUMENT", "API Key 含空格或无效字符");
        settings.encrypted_key = protectGatewayKey(key);
      } else if (settings.encrypted_key.empty() || url != settings.base_url) {
        throw ApiError(400, "INVALID_ARGUMENT", "首次配置或更换网关地址时必须输入 API Key");
      }
      settings.base_url = url; settings.model = model;
    }
    ++settings.revision;
    tx.exec_params("UPDATE user_model_gateway_settings SET base_url=$1, model=$2, api_key_encrypted=$3, "
                   "revision=$4, updated_at=NOW() WHERE user_id=$5",
                   settings.base_url, settings.model, settings.encrypted_key, settings.revision, actor);
    tx.exec_params("INSERT INTO user_model_gateway_settings_audit(revision,base_url,model,action,user_id) "
                   "VALUES($1,$2,$3,$4,$5)", settings.revision, settings.base_url, settings.model,
                   clear ? "clear" : "save", actor);
    tx.commit();
    return settings;
  }
 private:
  static GatewaySettings fromRows(const pqxx::result& rows) {
    if (rows.empty()) return {};
    const auto row = rows[0];
    return {row["base_url"].c_str(), row["model"].c_str(), row["api_key_encrypted"].c_str(),
            row["revision"].as<long long>()};
  }
  std::shared_ptr<DatabasePool> pool_;
};
