#pragma once

struct AiJob {
  std::string id;
  std::string type;
  std::string target_id;
  int generation = 1;
  int attempt = 0;
};

inline int aiJobRetryDelaySeconds(int completed_attempts) {
  return completed_attempts <= 1 ? 5 : 30;
}

inline void enqueueAiJob(pqxx::transaction_base& tx, const std::string& type,
                         const std::string& target_id, bool reset_dead_job = false) {
  const auto dedupe_key = type == "evaluation"
      ? "evaluation:" + target_id : "roleplay-summary:" + target_id;
  if (reset_dead_job) {
    tx.exec_params(R"(
      INSERT INTO ai_jobs
        (id, job_type, target_id, dedupe_key, status, attempts, available_at, updated_at)
      VALUES ($1, $2, $3, $4, 'pending', 0, NOW(), NOW())
      ON CONFLICT (dedupe_key) DO UPDATE SET
        status = 'pending', generation = ai_jobs.generation + 1, attempts = 0,
        available_at = NOW(), lease_until = NULL,
        worker_id = NULL, last_error = NULL, finished_at = NULL, updated_at = NOW()
    )", makeId("job"), type, target_id, dedupe_key);
    return;
  }
  tx.exec_params(R"(
    INSERT INTO ai_jobs(id, job_type, target_id, dedupe_key, status, available_at)
    VALUES ($1, $2, $3, $4, 'pending', NOW())
    ON CONFLICT (dedupe_key) DO NOTHING
  )", makeId("job"), type, target_id, dedupe_key);
}

class ReliableDatabase {
 public:
  explicit ReliableDatabase(std::string database_url) : database_url_(std::move(database_url)) {}

  bool healthy() const {
    try {
      pqxx::connection connection(database_url_);
      pqxx::read_transaction tx(connection);
      const auto row = tx.exec(R"(
        SELECT to_regclass('ai_jobs') IS NOT NULL AS jobs_ready,
          to_regclass('users') IS NOT NULL AS users_ready,
          to_regclass('messages') IS NOT NULL AS messages_ready,
          to_regclass('learner_mistake_progress') IS NOT NULL AS learner_insights_ready,
          to_regclass('session_hints') IS NOT NULL AS training_experience_ready,
          to_regclass('learner_checkins') IS NOT NULL AS learner_growth_ready,
          to_regclass('learner_phrase_favorites') IS NOT NULL AS phrase_favorites_ready
      )")[0];
      return row["jobs_ready"].as<bool>() && row["users_ready"].as<bool>() &&
             row["messages_ready"].as<bool>() && row["learner_insights_ready"].as<bool>() &&
             row["training_experience_ready"].as<bool>() && row["learner_growth_ready"].as<bool>() &&
             row["phrase_favorites_ready"].as<bool>();
    } catch (...) {
      return false;
    }
  }

  json listScenarios(const std::string& user_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT s.id, s.name, s.category, s.summary, s.difficulty, s.focus, s.patient_profile, s.max_rounds,
        COALESCE(best.best_score, 0) AS best_score,
        active.id AS active_id, active.current_round AS active_current_round,
        active.max_rounds AS active_max_rounds, active.updated_at AS active_updated_at
      FROM scenarios s
      LEFT JOIN LATERAL (
        SELECT MAX(total_score) AS best_score FROM sessions
        WHERE user_id = $1 AND scenario_id = s.id AND evaluation_status = 'ready'
      ) best ON TRUE
      LEFT JOIN LATERAL (
        SELECT id, current_round, max_rounds, updated_at FROM sessions
        WHERE user_id = $1 AND scenario_id = s.id AND status = 'in_progress'
        ORDER BY updated_at DESC LIMIT 1
      ) active ON TRUE
      ORDER BY s.sort_order
    )", user_id);
    json items = json::array();
    for (const auto& row : rows) {
      json item = {
          {"id", row["id"].c_str()}, {"name", row["name"].c_str()},
          {"category", row["category"].c_str()},
          {"summary", row["summary"].c_str()}, {"difficulty", row["difficulty"].c_str()},
          {"focus", json::parse(row["focus"].c_str())},
          {"patientProfile", json::parse(row["patient_profile"].c_str())},
          {"maxRounds", row["max_rounds"].as<int>()}, {"bestScore", row["best_score"].as<int>()},
          {"activeSession", nullptr},
      };
      if (!row["active_id"].is_null()) {
        item["activeSession"] = {{"id", row["active_id"].c_str()},
                                 {"currentRound", row["active_current_round"].as<int>()},
                                 {"maxRounds", row["active_max_rounds"].as<int>()},
                                 {"updatedAt", row["active_updated_at"].c_str()}};
      }
      items.push_back(item);
    }
    return {{"items", items}};
  }

  static bool profileField(const json& custom_profile, const char* key, std::string& out) {
    if (!custom_profile.is_object() || !custom_profile.contains(key)) return false;
    const auto& value = custom_profile[key];
    if (value.is_string()) {
      const std::string text = value.get<std::string>();
      if (!text.empty()) { out = text; return true; }
      return false;
    }
    if (value.is_number()) {
      out = value.dump();
      return true;
    }
    return false;
  }

  static std::string customPatientOpening(const json& custom_profile,
                                          const std::string& fallback) {
    if (!custom_profile.is_object()) return fallback;
    std::string age, description, emotion;
    const bool has_age = profileField(custom_profile, "age", age);
    const bool has_description = profileField(custom_profile, "description", description);
    const bool has_emotion = profileField(custom_profile, "emotion", emotion);
    if (!has_age && !has_description && !has_emotion) return fallback;
    // 组装一个更像真人求助的自然开场，避免把年龄/情绪/描述机械罗列成一串。
    // 优先把"描述"作为核心诉求；年龄、情绪作为背景自然带出。
    std::string opening;
    std::string concern = has_description ? description : "我有些口腔方面的疑问";
    if (has_emotion && (emotion == "焦虑" || emotion == "担心" || emotion == "害怕" ||
                        emotion == "紧张" || emotion == "犹豫" || emotion == "不安")) {
      opening = "您好，我最近" + concern + "，心里挺" + emotion + "的";
      if (has_age) opening += "。我今年" + age + "岁";
      opening += "，能麻烦您帮我看看是怎么回事吗？";
    } else {
      opening = "您好，我最近" + concern;
      if (has_age) opening += "，我今年" + age + "岁";
      if (has_emotion) opening += "，现在有点" + emotion;
      opening += "，想请您帮我了解一下。";
    }
    return opening;
  }

  json createSession(const std::string& user_id, const std::string& scenario_id,
                     const json& custom_profile = nullptr) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto scenario = tx.exec_params("SELECT * FROM scenarios WHERE id = $1", scenario_id);
    if (scenario.empty()) throw ApiError(404, "SCENARIO_NOT_FOUND", "训练场景不存在");
    const auto active = tx.exec_params(
        "SELECT id FROM sessions WHERE user_id = $1 AND scenario_id = $2 AND status = 'in_progress'",
        user_id, scenario_id);
    if (!active.empty()) throw ApiError(409, "SESSION_IN_PROGRESS", "该场景已有进行中的训练");
    const auto& row = scenario[0];
    const auto hidden = json::parse(row["hidden_config"].c_str());

    // Merge custom patient profile into hidden initialState if provided
    json merged_hidden = hidden;
    if (custom_profile.is_object()) {
      if (!merged_hidden.contains("initialState")) merged_hidden["initialState"] = json::object();
      auto& initial = merged_hidden["initialState"];
      if (custom_profile.contains("emotion") && custom_profile["emotion"].is_string()) {
        initial["emotion"] = custom_profile["emotion"].get<std::string>();
      }
      if (custom_profile.contains("emotionLevel") && custom_profile["emotionLevel"].is_number()) {
        initial["emotionLevel"] = custom_profile["emotionLevel"].get<int>();
      }
    }

    const json state = {
        {"emotion", merged_hidden["initialState"].value("emotion", "平静")},
        {"emotionLevel", merged_hidden["initialState"].value("emotionLevel", 0)},
        {"trustLevel", merged_hidden["initialState"].value("trustLevel", 50)},
        {"revealedInformation", json::array()}, {"riskTriggered", false},
    };
    const auto session_id = makeId("sess");
    const auto opening_id = makeId("msg");
    const std::string custom_profile_str = custom_profile.is_object() ? custom_profile.dump() : "{}";
    // 开场白优先基于自定义画像生成，未提供画像时回退到场景模板
    const std::string opening = customPatientOpening(custom_profile,
                                                     hidden["opening"].get<std::string>());
    tx.exec_params(R"(
      INSERT INTO sessions
        (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds, patient_state, custom_patient_profile)
      VALUES ($1, $2, $3, $4, 'in_progress', 0, $5, $6::jsonb, $7::jsonb)
    )", session_id, user_id, scenario_id, row["name"].c_str(), row["max_rounds"].as<int>(), state.dump(), custom_profile_str);
    tx.exec_params(R"(
      INSERT INTO messages(id, session_id, role, content, round)
      VALUES ($1, $2, 'patient', $3, 0)
    )", opening_id, session_id, opening);
    const auto saved = getSessionRow(tx, session_id, user_id);
    tx.commit();
    return {{"session", saved},
            {"messages", json::array({messageJson(opening_id, "patient", opening, 0)})}};
  }

  json restartSession(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto previous = tx.exec_params(
        "SELECT scenario_id, status, custom_patient_profile FROM sessions WHERE id = $1 AND user_id = $2 FOR UPDATE",
        session_id, user_id);
    if (previous.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    if (std::string(previous[0]["status"].c_str()) != "in_progress") {
      throw ApiError(409, "SESSION_NOT_RESTARTABLE", "只有进行中的训练可以重新开始");
    }
    const auto scenario_id = std::string(previous[0]["scenario_id"].c_str());
    // 保留旧会话的自定义画像，重新开始时继续沿用
    json custom_profile = nullptr;
    if (!previous[0]["custom_patient_profile"].is_null()) {
      custom_profile = json::parse(previous[0]["custom_patient_profile"].c_str());
    }
    tx.exec_params("UPDATE sessions SET status = 'abandoned', updated_at = NOW() WHERE id = $1", session_id);
    const auto scenario = tx.exec_params("SELECT * FROM scenarios WHERE id = $1", scenario_id)[0];
    const auto hidden = json::parse(scenario["hidden_config"].c_str());
    json merged_hidden = hidden;
    if (custom_profile.is_object() && custom_profile.contains("emotion")
        && custom_profile["emotion"].is_string()) {
      if (!merged_hidden.contains("initialState")) merged_hidden["initialState"] = json::object();
      merged_hidden["initialState"]["emotion"] = custom_profile["emotion"].get<std::string>();
    }
    const json state = {
        {"emotion", merged_hidden["initialState"].value("emotion", "平静")},
        {"emotionLevel", merged_hidden["initialState"].value("emotionLevel", 0)},
        {"trustLevel", merged_hidden["initialState"].value("trustLevel", 50)},
        {"revealedInformation", json::array()}, {"riskTriggered", false},
    };
    const auto new_id = makeId("sess");
    const auto opening_id = makeId("msg");
    const std::string custom_profile_str = custom_profile.is_object() ? custom_profile.dump() : "{}";
    const std::string opening = customPatientOpening(custom_profile,
                                                     hidden["opening"].get<std::string>());
    tx.exec_params(R"(
      INSERT INTO sessions
        (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds, patient_state, custom_patient_profile)
      VALUES ($1, $2, $3, $4, 'in_progress', 0, $5, $6::jsonb, $7::jsonb)
    )", new_id, user_id, scenario_id, scenario["name"].c_str(),
        scenario["max_rounds"].as<int>(), state.dump(), custom_profile_str);
    tx.exec_params("INSERT INTO messages(id, session_id, role, content, round) VALUES ($1, $2, 'patient', $3, 0)",
                   opening_id, new_id, opening);
    const auto saved = getSessionRow(tx, new_id, user_id);
    tx.commit();
    return {{"session", saved},
            {"messages", json::array({messageJson(opening_id, "patient", opening, 0)})}};
  }

  // 强制结束训练：标记为 abandoned，不生成报告，也不计入训练统计
  json abandonTrainingSession(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT status FROM sessions WHERE id = $1 AND user_id = $2 FOR UPDATE
    )", session_id, user_id);
    if (rows.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    const auto status = std::string(rows[0]["status"].c_str());
    if (status == "abandoned") {
      tx.commit();
      return {{"sessionId", session_id}, {"status", "abandoned"}};
    }
    if (status != "in_progress") {
      throw ApiError(409, "SESSION_FINISHED", "只有进行中的训练可以强制结束");
    }
    tx.exec_params("UPDATE sessions SET status = 'abandoned', updated_at = NOW() WHERE id = $1", session_id);
    tx.commit();
    return {{"sessionId", session_id}, {"status", "abandoned"}};
  }

  json getSession(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto session = getSessionRow(tx, session_id, user_id);
    const auto rows = tx.exec_params(R"(
      SELECT id, role, content, round,
        to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS created_at
      FROM messages WHERE session_id = $1 ORDER BY round, created_at
    )", session_id);
    json messages = json::array();
    for (const auto& row : rows) messages.push_back(messageJson(row));
    const auto pending_rows = tx.exec_params(R"(
      SELECT client_message_id, content, round, reply_status
      FROM messages
      WHERE session_id = $1 AND role = 'user' AND client_message_id IS NOT NULL
        AND reply_status <> 'ready'
      ORDER BY created_at DESC LIMIT 1
    )", session_id);
    json pending_message = nullptr;
    if (!pending_rows.empty()) {
      pending_message = {{"clientMessageId", pending_rows[0]["client_message_id"].c_str()},
                         {"content", pending_rows[0]["content"].c_str()},
                         {"round", pending_rows[0]["round"].as<int>()},
                         {"replyStatus", pending_rows[0]["reply_status"].c_str()}};
    }
    const auto hint_rows = tx.exec_params(R"(
      SELECT id, hint_number, content,
        to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS created_at
      FROM session_hints WHERE session_id = $1 ORDER BY hint_number
    )", session_id);
    json hints = json::array();
    for (const auto& row : hint_rows) {
      hints.push_back({{"id", row["id"].c_str()}, {"number", row["hint_number"].as<int>()},
                       {"content", row["content"].c_str()}, {"createdAt", row["created_at"].c_str()}});
    }
    return {{"session", session}, {"messages", messages}, {"pendingMessage", pending_message},
            {"hints", hints}, {"hintLimit", 3},
            {"hintRemaining", std::max(0, 3 - static_cast<int>(hints.size()))}};
  }

  json requestTrainingHint(const std::string& user_id, const std::string& session_id) const {
    if (session_id.empty() || session_id.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "训练会话标识无效");
    }
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto session_rows = tx.exec_params(R"(
      SELECT s.status, s.current_round, s.scenario_id
      FROM sessions s WHERE s.id = $1 AND s.user_id = $2 FOR UPDATE
    )", session_id, user_id);
    if (session_rows.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    if (std::string(session_rows[0]["status"].c_str()) != "in_progress") {
      throw ApiError(409, "SESSION_FINISHED", "已结束的训练不能继续获取提示");
    }
    const auto used_rows = tx.exec_params(
        "SELECT COUNT(*) AS used FROM session_hints WHERE session_id = $1", session_id);
    const auto used = used_rows[0]["used"].as<int>();
    if (used >= 3) throw ApiError(409, "HINT_LIMIT_REACHED", "本次训练的提示已用完");
    const auto hint_number = used + 1;
    const auto content = trainingHintFor(
        session_rows[0]["scenario_id"].c_str(), session_rows[0]["current_round"].as<int>(), hint_number);
    const auto inserted = tx.exec_params(R"(
      INSERT INTO session_hints(id, session_id, hint_number, content)
      VALUES ($1, $2, $3, $4)
      RETURNING to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS created_at
    )", makeId("hint"), session_id, hint_number, content);
    tx.commit();
    return {{"hint", {{"number", hint_number}, {"content", content},
                      {"createdAt", inserted[0]["created_at"].c_str()}}},
            {"hintLimit", 3}, {"hintRemaining", 3 - hint_number}};
  }

  json getSessionInternal(const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    return {{"session", getSessionRow(tx, session_id, "")}};
  }

  json listSessions(const std::string& user_id, const std::string& status,
                    const std::string& scenario_id, int limit) const {
    const std::vector<std::string> allowed = {"all", "in_progress", "completed", "abandoned"};
    if (std::find(allowed.begin(), allowed.end(), status) == allowed.end()) {
      throw ApiError(400, "INVALID_ARGUMENT", "status 参数无效");
    }
    limit = clampInt(limit, 1, 50);
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    std::string query = "SELECT id, scenario_id, scenario_name, status, current_round, max_rounds, " +
        std::string(kSessionTimes) + ", total_score, evaluation_status, custom_patient_profile FROM sessions WHERE user_id = " +
        tx.quote(user_id);
    if (status != "all") query += " AND status = " + tx.quote(status);
    if (!scenario_id.empty()) query += " AND scenario_id = " + tx.quote(scenario_id);
    query += " ORDER BY updated_at DESC LIMIT " + std::to_string(limit);
    const auto rows = tx.exec(query);
    json items = json::array();
    for (const auto& row : rows) items.push_back(sessionJson(row));
    return {{"items", items}, {"total", static_cast<int>(items.size())}};
  }

  json getScenarioInternal(const std::string& scenario_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto rows = tx.exec_params(
        "SELECT id, name, category, summary, difficulty, focus, patient_profile, hidden_config, max_rounds "
        "FROM scenarios WHERE id = $1", scenario_id);
    if (rows.empty()) throw ApiError(404, "SCENARIO_NOT_FOUND", "训练场景不存在");
    const auto& row = rows[0];
    return {{"public", {{"id", row["id"].c_str()}, {"name", row["name"].c_str()},
                         {"category", row["category"].c_str()},
                         {"summary", row["summary"].c_str()}, {"difficulty", row["difficulty"].c_str()},
                         {"focus", json::parse(row["focus"].c_str())},
                         {"patientProfile", json::parse(row["patient_profile"].c_str())},
                         {"maxRounds", row["max_rounds"].as<int>()}}},
            {"hidden", json::parse(row["hidden_config"].c_str())}};
  }

  json getHistory(const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto rows = tx.exec_params(
        "SELECT role, content, round FROM messages WHERE session_id = $1 ORDER BY round, created_at",
        session_id);
    json messages = json::array();
    for (const auto& row : rows) {
      messages.push_back({{"role", row["role"].c_str()}, {"content", row["content"].c_str()},
                          {"round", row["round"].as<int>()}});
    }
    return messages;
  }

  json getPatientState(const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto rows = tx.exec_params("SELECT patient_state FROM sessions WHERE id = $1", session_id);
    if (rows.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    return json::parse(rows[0]["patient_state"].c_str());
  }

  json claimUserMessage(const std::string& user_id, const std::string& session_id,
                        const std::string& client_message_id, const std::string& content) const {
    const auto cleaned_content = trim(content);
    if (client_message_id.empty() || client_message_id.size() > 100) {
      throw ApiError(400, "INVALID_ARGUMENT", "clientMessageId 格式无效");
    }
    const auto content_length = utf8Length(cleaned_content);
    if (content_length < 1 || content_length > 1000) {
      throw ApiError(400, "INVALID_ARGUMENT", "消息长度应为 1 到 1000 个字符");
    }
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto session = tx.exec_params(
        "SELECT status, current_round, max_rounds FROM sessions "
        "WHERE id = $1 AND user_id = $2 FOR UPDATE", session_id, user_id);
    if (session.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    const auto existing = tx.exec_params(R"(
      SELECT input.id AS input_id, input.content AS input_content, input.round AS input_round,
        input.reply_status, input.reply_lease_until > NOW() AS lease_active,
        reply.id AS reply_id, reply.content AS reply_content
      FROM messages input
      LEFT JOIN messages reply ON reply.session_id = input.session_id
        AND reply.role = 'patient' AND reply.round = input.round
      WHERE input.session_id = $1 AND input.client_message_id = $2 AND input.role = 'user'
      LIMIT 1
    )", session_id, client_message_id);
    if (!existing.empty()) {
      const auto& row = existing[0];
      if (std::string(row["input_content"].c_str()) != cleaned_content) {
        throw ApiError(409, "IDEMPOTENCY_CONFLICT", "同一 clientMessageId 不能用于不同内容");
      }
      json result = {{"userMessage", messageJson(row["input_id"].c_str(), "user",
                                                   row["input_content"].c_str(), row["input_round"].as<int>())},
                     {"patientMessage", nullptr}, {"isComplete", !row["reply_id"].is_null()},
                     {"round", row["input_round"].as<int>()}};
      if (!row["reply_id"].is_null()) {
        result["patientMessage"] = messageJson(row["reply_id"].c_str(), "patient",
                                                row["reply_content"].c_str(), row["input_round"].as<int>());
        tx.commit();
        return result;
      }
      const auto status = std::string(session[0]["status"].c_str());
      if (status == "abandoned") throw ApiError(409, "SESSION_ABANDONED", "已放弃的训练不能恢复");
      if (status != "in_progress") throw ApiError(409, "SESSION_FINISHED", "训练已结束");
      const bool lease_active = !row["lease_active"].is_null() && row["lease_active"].as<bool>();
      if (std::string(row["reply_status"].c_str()) == "generating" && lease_active) {
        throw ApiError(409, "SESSION_RESPONSE_PENDING", "患者回复正在生成，请稍后查询会话");
      }
      const auto token = randomToken(16);
      tx.exec_params(R"(
        UPDATE messages SET reply_status = 'generating',
          reply_lease_until = NOW() + ($2 * INTERVAL '1 second'),
          reply_attempt_token = $3, reply_error_type = NULL
        WHERE id = $1
      )", row["input_id"].c_str(), kReplyLeaseSeconds, token);
      result["attemptToken"] = token;
      tx.commit();
      return result;
    }
    const auto status = std::string(session[0]["status"].c_str());
    if (status == "abandoned") throw ApiError(409, "SESSION_ABANDONED", "已放弃的训练不能恢复");
    if (status != "in_progress") {
      throw ApiError(409, "SESSION_FINISHED", "训练已结束，不能继续发送消息");
    }
    const auto current_round = session[0]["current_round"].as<int>();
    const auto max_rounds = session[0]["max_rounds"].as<int>();
    if (current_round >= max_rounds) throw ApiError(409, "MAX_ROUNDS_REACHED", "已达到最大训练轮数");
    const auto pending = tx.exec_params(
        "SELECT id FROM messages WHERE session_id = $1 AND role = 'user' AND round = $2",
        session_id, current_round + 1);
    if (!pending.empty()) {
      throw ApiError(409, "SESSION_RESPONSE_PENDING", "上一条消息正在等待患者回复，请使用原请求重试");
    }
    const auto message_id = makeId("msg");
    const auto token = randomToken(16);
    const auto round = current_round + 1;
    tx.exec_params(R"(
      INSERT INTO messages
        (id, session_id, role, content, round, client_message_id, reply_status,
         reply_lease_until, reply_attempt_token)
      VALUES ($1, $2, 'user', $3, $4, $5, 'generating',
              NOW() + ($6 * INTERVAL '1 second'), $7)
    )", message_id, session_id, cleaned_content, round, client_message_id, kReplyLeaseSeconds, token);
    tx.exec_params("UPDATE sessions SET updated_at = NOW() WHERE id = $1", session_id);
    tx.commit();
    return {{"userMessage", messageJson(message_id, "user", cleaned_content, round)},
            {"patientMessage", nullptr}, {"isComplete", false}, {"round", round},
            {"attemptToken", token}};
  }

  void markReplyFailed(const std::string& session_id, int round, const std::string& token,
                       const std::string& error_type) const noexcept {
    try {
      pqxx::connection connection(database_url_);
      pqxx::work tx(connection);
      tx.exec_params(R"(
        UPDATE messages SET reply_status = 'failed', reply_lease_until = NULL,
          reply_attempt_token = NULL, reply_error_type = $4
        WHERE session_id = $1 AND role = 'user' AND round = $2 AND reply_attempt_token = $3
      )", session_id, round, token, error_type);
      tx.commit();
    } catch (const std::exception& error) {
      std::cerr << json({{"event", "reply_failure_persist_error"}, {"sessionId", session_id},
                        {"error", error.what()}}).dump() << '\n';
    }
  }

  json savePatientReply(const std::string& user_id, const std::string& session_id, int round,
                        const std::string& token, const json& model_reply) const {
    const auto reply = jsonString(model_reply, "reply");
    const auto reply_length = utf8Length(reply);
    if (reply_length < 1 || reply_length > 1000) {
      throw ApiError(503, "MODEL_INVALID_RESPONSE", "模型未返回有效患者回复");
    }
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto session_rows = tx.exec_params(
        "SELECT * FROM sessions WHERE id = $1 AND user_id = $2 FOR UPDATE", session_id, user_id);
    if (session_rows.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    const auto existing = tx.exec_params(
        "SELECT id, content FROM messages WHERE session_id = $1 AND role = 'patient' AND round = $2",
        session_id, round);
    if (!existing.empty()) {
      const auto session = getSessionRow(tx, session_id, user_id);
      tx.commit();
      return {{"patientMessage", messageJson(existing[0]["id"].c_str(), "patient",
                                              existing[0]["content"].c_str(), round)},
              {"session", session}, {"shouldFinish", session["status"] == "completed"}};
    }
    const auto& session = session_rows[0];
    const auto status = std::string(session["status"].c_str());
    if (status == "abandoned") throw ApiError(409, "SESSION_ABANDONED", "已放弃的训练不能恢复");
    if (status != "in_progress") throw ApiError(409, "SESSION_FINISHED", "训练已结束");
    const auto input = tx.exec_params(R"(
      SELECT id, reply_attempt_token FROM messages
      WHERE session_id = $1 AND role = 'user' AND round = $2 FOR UPDATE
    )", session_id, round);
    if (input.empty() || input[0]["reply_attempt_token"].is_null() ||
        std::string(input[0]["reply_attempt_token"].c_str()) != token) {
      throw ApiError(409, "SESSION_RESPONSE_PENDING", "该回复生成租约已失效，请查询会话后重试");
    }
    json state = json::parse(session["patient_state"].c_str());
    state["emotion"] = jsonString(model_reply, "emotion", state.value("emotion", "平静"));
    state["emotionLevel"] = clampInt(
        jsonInt(model_reply, "emotionLevel", state.value("emotionLevel", 0)), -2, 2);
    state["trustLevel"] = clampInt(
        jsonInt(model_reply, "trustLevel", state.value("trustLevel", 50)), 0, 100);
    state["riskTriggered"] = model_reply.value(
        "riskTriggered", state.value("riskTriggered", false));
    if (!state.contains("revealedInformation") || !state["revealedInformation"].is_array()) {
      state["revealedInformation"] = json::array();
    }
    if (model_reply.contains("newlyRevealedInformation") &&
        model_reply["newlyRevealedInformation"].is_array()) {
      for (const auto& value : model_reply["newlyRevealedInformation"]) {
        if (value.is_string() && std::find(state["revealedInformation"].begin(),
            state["revealedInformation"].end(), value) == state["revealedInformation"].end()) {
          state["revealedInformation"].push_back(value);
        }
      }
    }
    const auto message_id = makeId("msg");
    tx.exec_params(
        "INSERT INTO messages(id, session_id, role, content, round) VALUES ($1, $2, 'patient', $3, $4)",
        message_id, session_id, reply, round);
    tx.exec_params(R"(
      UPDATE messages SET reply_status = 'ready', reply_lease_until = NULL,
        reply_attempt_token = NULL, reply_error_type = NULL WHERE id = $1
    )", input[0]["id"].c_str());
    const bool should_finish = round >= session["max_rounds"].as<int>();
    if (should_finish) {
      tx.exec_params(R"(
        UPDATE sessions SET current_round = $2, patient_state = $3::jsonb, status = 'completed',
          finished_at = NOW(), updated_at = NOW(), evaluation_status = 'generating'
        WHERE id = $1
      )", session_id, round, state.dump());
      tx.exec_params(R"(
        INSERT INTO evaluations(session_id, status, updated_at) VALUES ($1, 'generating', NOW())
        ON CONFLICT (session_id) DO UPDATE SET status = 'generating', report = NULL,
          error_type = NULL, updated_at = NOW()
      )", session_id);
      enqueueAiJob(tx, "evaluation", session_id);
    } else {
      tx.exec_params(R"(
        UPDATE sessions SET current_round = $2, patient_state = $3::jsonb, updated_at = NOW()
        WHERE id = $1
      )", session_id, round, state.dump());
    }
    const auto saved = getSessionRow(tx, session_id, user_id);
    tx.commit();
    return {{"patientMessage", messageJson(message_id, "patient", reply, round)},
            {"session", saved}, {"shouldFinish", should_finish}};
  }

  json finish(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(
        "SELECT status, current_round FROM sessions WHERE id = $1 AND user_id = $2 FOR UPDATE",
        session_id, user_id);
    if (rows.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    const auto status = std::string(rows[0]["status"].c_str());
    if (status == "abandoned") throw ApiError(409, "SESSION_ABANDONED", "已放弃的训练不能结束或恢复");
    if (status == "completed") {
      const auto saved = getSessionRow(tx, session_id, user_id);
      tx.commit();
      return saved;
    }
    if (rows[0]["current_round"].as<int>() == 0) {
      throw ApiError(422, "MIN_ROUNDS_NOT_REACHED", "至少完成 1 轮对话后才能评分");
    }
    const auto pending = tx.exec_params(R"(
      SELECT 1 FROM messages WHERE session_id = $1 AND role = 'user' AND reply_status <> 'ready'
      LIMIT 1
    )", session_id);
    if (!pending.empty()) throw ApiError(409, "SESSION_RESPONSE_PENDING", "患者回复正在生成，暂不能结束训练");
    tx.exec_params(R"(
      UPDATE sessions SET status = 'completed', finished_at = NOW(), updated_at = NOW(),
        evaluation_status = 'generating' WHERE id = $1
    )", session_id);
    tx.exec_params(R"(
      INSERT INTO evaluations(session_id, status, updated_at) VALUES ($1, 'generating', NOW())
      ON CONFLICT (session_id) DO UPDATE SET status = 'generating', report = NULL,
        error_type = NULL, updated_at = NOW()
    )", session_id);
    enqueueAiJob(tx, "evaluation", session_id);
    const auto saved = getSessionRow(tx, session_id, user_id);
    tx.commit();
    return saved;
  }

  void retryEvaluation(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(
        "SELECT status, evaluation_status FROM sessions WHERE id = $1 AND user_id = $2 FOR UPDATE",
        session_id, user_id);
    if (rows.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    if (std::string(rows[0]["status"].c_str()) != "completed" ||
        std::string(rows[0]["evaluation_status"].c_str()) != "failed") {
      throw ApiError(409, "EVALUATION_NOT_RETRYABLE", "当前评分不可重试");
    }
    tx.exec_params("UPDATE sessions SET evaluation_status = 'generating', updated_at = NOW() WHERE id = $1",
                   session_id);
    tx.exec_params(R"(
      UPDATE evaluations SET status = 'generating', report = NULL, error_type = NULL, updated_at = NOW()
      WHERE session_id = $1
    )", session_id);
    enqueueAiJob(tx, "evaluation", session_id, true);
    tx.commit();
  }

  json getEvaluation(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto session = tx.exec_params(
        "SELECT evaluation_status FROM sessions WHERE id = $1 AND user_id = $2", session_id, user_id);
    if (session.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    const auto status = std::string(session[0]["evaluation_status"].c_str());
    if (status != "ready") {
      return {{"sessionId", session_id}, {"status", status}, {"retryable", status == "failed"},
              {"evaluation", nullptr}};
    }
    const auto evaluation = tx.exec_params(
        "SELECT report FROM evaluations WHERE session_id = $1 AND status = 'ready'", session_id);
    if (evaluation.empty() || evaluation[0]["report"].is_null()) {
      return {{"sessionId", session_id}, {"status", "generating"}, {"retryable", false},
              {"evaluation", nullptr}};
    }
    auto report = json::parse(evaluation[0]["report"].c_str(), nullptr, false);
    if (!report.is_object()) throw ApiError(503, "REPORT_INVALID", "评分报告存储格式无效");
    if (!report.contains("recommendedPhrases")) {
      report["recommendedPhrases"] = learningPhrasesFromReport(report);
    }
    if (!report.contains("learningMistakes")) {
      report["learningMistakes"] = learningMistakesFromReport(report);
    }
    return {{"sessionId", session_id}, {"status", "ready"}, {"retryable", false},
            {"evaluation", report}};
  }

  void saveEvaluation(const AiJob& job, json report, const std::string& model_version) const {
    const auto& dimensions = report["dimensionScores"];
    const auto total = static_cast<int>(std::round(
        dimensions["knowledgeAccuracy"].get<int>() * 0.25 +
        dimensions["medicalCompliance"].get<int>() * 0.25 +
        dimensions["empathy"].get<int>() * 0.20 +
        dimensions["needsDiscovery"].get<int>() * 0.20 +
        dimensions["serviceEtiquette"].get<int>() * 0.10));
    report["totalScore"] = clampInt(total, 0, 100);
    report["modelVersion"] = model_version;
    report["promptVersion"] = "score-prompt-v1";
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto owned = tx.exec_params(R"(
      SELECT 1 FROM ai_jobs WHERE id = $1 AND status = 'running' AND target_id = $2
        AND generation = $3 AND attempts = $4 AND lease_until > NOW() FOR UPDATE
    )", job.id, job.target_id, job.generation, job.attempt);
    if (owned.empty()) throw ApiError(409, "JOB_LEASE_LOST", "评分任务租约已失效");
    tx.exec_params(R"(
      UPDATE evaluations SET status = 'ready', report = $2::jsonb, model_version = $3,
        prompt_version = 'score-prompt-v1', generated_at = NOW(), updated_at = NOW(), error_type = NULL
      WHERE session_id = $1
    )", job.target_id, report.dump(), model_version);
    tx.exec_params(R"(
      UPDATE sessions SET evaluation_status = 'ready', total_score = $2, updated_at = NOW() WHERE id = $1
    )", job.target_id, report["totalScore"].get<int>());
    completeJob(tx, job);
    tx.commit();
  }

  json dashboard(const std::string& user_id, bool institution_aggregate) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const std::string filter = institution_aggregate ? "" : " WHERE user_id = " + tx.quote(user_id);
    const auto totals = tx.exec(R"(
      SELECT COUNT(*) FILTER (WHERE status <> 'abandoned') AS total_sessions,
        COUNT(*) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready') AS completed_sessions,
        AVG(total_score) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready') AS average_score
      FROM sessions
    )" + filter)[0];
    const auto user_condition = institution_aggregate ? "" : " AND x.user_id = " + tx.quote(user_id);
    const auto scenarios = tx.exec(R"(
      SELECT s.id, s.name, COUNT(x.id) AS training_count FROM scenarios s
      LEFT JOIN sessions x ON x.scenario_id = s.id AND x.status <> 'abandoned'
    )" + user_condition + " GROUP BY s.id, s.name, s.sort_order ORDER BY s.sort_order");
    const auto report_filter = institution_aggregate ? "" : " AND s.user_id = " + tx.quote(user_id);
    const auto reports = tx.exec(R"(
      SELECT e.report FROM evaluations e JOIN sessions s ON s.id = e.session_id
      WHERE s.status = 'completed' AND e.status = 'ready'
    )" + report_filter);
    const std::vector<std::string> keys = {
        "knowledgeAccuracy", "medicalCompliance", "empathy", "needsDiscovery", "serviceEtiquette"};
    json dimensions = json::object();
    for (const auto& key : keys) dimensions[key] = 0.0;
    for (const auto& row : reports) accumulateDimensionScores(
        dimensions, json::parse(row["report"].c_str()), keys);
    if (!reports.empty()) {
      for (const auto& key : keys) {
        dimensions[key] = std::round(dimensions[key].get<double>() / reports.size() * 10.0) / 10.0;
      }
    }
    json scenario_stats = json::array();
    for (const auto& row : scenarios) {
      scenario_stats.push_back({{"scenarioId", row["id"].c_str()},
                                {"scenarioName", row["name"].c_str()},
                                {"trainingCount", row["training_count"].as<int>()}});
    }
    json recent = json::array();
    if (!institution_aggregate) {
      const auto recent_rows = tx.exec(
          "SELECT id, scenario_id, scenario_name, status, current_round, max_rounds, " +
          std::string(kSessionTimes) +
          ", total_score, evaluation_status, custom_patient_profile FROM sessions WHERE user_id = " + tx.quote(user_id) +
          " AND status <> 'abandoned' ORDER BY updated_at DESC LIMIT 5");
      for (const auto& row : recent_rows) recent.push_back(sessionJson(row));
    }
    return {{"scope", institution_aggregate ? "institution" : "personal"},
            {"totalSessions", totals["total_sessions"].as<int>()},
            {"completedSessions", totals["completed_sessions"].as<int>()},
            {"averageScore", totals["average_score"].is_null()
                ? 0.0 : totals["average_score"].as<double>()},
            {"scenarioStats", scenario_stats}, {"dimensionAverages", dimensions},
            {"recentSessions", recent}};
  }

  json listLearningPhrases(const std::string& user_id, const std::string& search,
                           const std::string& scenario_id, bool favorites_only, int limit) const {
    if (utf8Length(search) > 120 || scenario_id.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "学习筛选参数过长");
    }
    limit = clampInt(limit, 1, 50);
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto favorite_rows = tx.exec_params(
        "SELECT session_id, phrase_key FROM learner_phrase_favorites WHERE user_id = $1", user_id);
    std::set<std::string> favorites;
    for (const auto& row : favorite_rows) {
      favorites.insert(favoriteKey(row["session_id"].c_str(), row["phrase_key"].c_str()));
    }
    std::string query = R"(
      SELECT s.id, s.scenario_id, s.scenario_name,
        to_char(s.finished_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS finished_date,
        e.report
      FROM sessions s JOIN evaluations e ON e.session_id = s.id
      WHERE s.user_id = $1 AND s.status = 'completed' AND e.status = 'ready'
    )";
    if (!scenario_id.empty()) query += " AND s.scenario_id = $2";
    query += " ORDER BY s.finished_at DESC NULLS LAST LIMIT 200";
    const auto rows = scenario_id.empty() ? tx.exec_params(query, user_id)
                                          : tx.exec_params(query, user_id, scenario_id);
    json items = json::array();
    for (const auto& row : rows) {
      const auto report = storedReport(row);
      for (const auto& phrase : learningPhrasesFromReport(report)) {
        if (items.size() >= static_cast<size_t>(limit)) break;
        if (!phrase.is_object()) continue;
        const auto phrase_key = jsonString(phrase, "phraseKey");
        if (phrase_key.empty()) continue;
        const auto patient_says = jsonString(phrase, "patientSays");
        const auto cs_reply = jsonString(phrase, "csReply");
        const auto reason = jsonString(phrase, "reason");
        const auto scenario_name = std::string(row["scenario_name"].c_str());
        const auto session_id = std::string(row["id"].c_str());
        const bool favorited = favorites.find(favoriteKey(session_id, phrase_key)) != favorites.end();
        if (favorites_only && !favorited) continue;
        if (!search.empty() && patient_says.find(search) == std::string::npos &&
            cs_reply.find(search) == std::string::npos && reason.find(search) == std::string::npos &&
            scenario_name.find(search) == std::string::npos) {
          continue;
        }
        items.push_back({
            {"id", session_id + ':' + phrase_key}, {"sessionId", session_id}, {"phraseKey", phrase_key},
            {"scenarioId", row["scenario_id"].c_str()},
            {"scenarioName", scenario_name}, {"finishedDate", row["finished_date"].c_str()},
            {"round", jsonInt(phrase, "round", 0)}, {"patientSays", patient_says},
            {"csReply", cs_reply}, {"reason", reason}, {"favorited", favorited},
        });
      }
      if (items.size() >= static_cast<size_t>(limit)) break;
    }
    return {{"items", items}, {"total", static_cast<int>(items.size())},
            {"favoritesOnly", favorites_only}};
  }

  json setLearningPhraseFavorite(const std::string& user_id, const std::string& session_id,
                                 const std::string& phrase_key, bool favorite) const {
    if (session_id.empty() || session_id.size() > 120 || phrase_key.empty() || phrase_key.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "话术标识参数无效");
    }
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT e.report FROM sessions s JOIN evaluations e ON e.session_id = s.id
      WHERE s.id = $1 AND s.user_id = $2 AND s.status = 'completed' AND e.status = 'ready'
      FOR UPDATE OF s
    )", session_id, user_id);
    if (rows.empty()) throw ApiError(404, "LEARNING_PHRASE_NOT_FOUND", "话术不存在或无权访问");
    bool known_phrase = false;
    for (const auto& item : learningPhrasesFromReport(storedReport(rows[0]))) {
      if (item.is_object() && jsonString(item, "phraseKey") == phrase_key) {
        known_phrase = true;
        break;
      }
    }
    if (!known_phrase) throw ApiError(404, "LEARNING_PHRASE_NOT_FOUND", "话术不存在或无权访问");
    if (favorite) {
      tx.exec_params(R"(
        INSERT INTO learner_phrase_favorites(user_id, session_id, phrase_key, updated_at)
        VALUES ($1, $2, $3, NOW())
        ON CONFLICT (user_id, session_id, phrase_key) DO UPDATE SET updated_at = NOW()
      )", user_id, session_id, phrase_key);
    } else {
      tx.exec_params(R"(
        DELETE FROM learner_phrase_favorites
        WHERE user_id = $1 AND session_id = $2 AND phrase_key = $3
      )", user_id, session_id, phrase_key);
    }
    tx.commit();
    return {{"sessionId", session_id}, {"phraseKey", phrase_key}, {"favorited", favorite}};
  }

  json listLearningMistakes(const std::string& user_id, const std::string& scenario_id,
                            bool include_mastered, int limit) const {
    if (scenario_id.size() > 120) throw ApiError(400, "INVALID_ARGUMENT", "scenarioId 参数过长");
    limit = clampInt(limit, 1, 50);
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto mastered_rows = tx.exec_params(R"(
      SELECT session_id, mistake_key FROM learner_mistake_progress
      WHERE user_id = $1 AND mastered_at IS NOT NULL
    )", user_id);
    std::set<std::string> mastered_keys;
    for (const auto& row : mastered_rows) {
      mastered_keys.insert(masteryKey(row["session_id"].c_str(), row["mistake_key"].c_str()));
    }
    std::string query = R"(
      SELECT s.id, s.scenario_id, s.scenario_name,
        to_char(s.finished_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS finished_date,
        e.report
      FROM sessions s JOIN evaluations e ON e.session_id = s.id
      WHERE s.user_id = $1 AND s.status = 'completed' AND e.status = 'ready'
    )";
    if (!scenario_id.empty()) query += " AND s.scenario_id = $2";
    query += " ORDER BY s.finished_at DESC NULLS LAST LIMIT 200";
    const auto rows = scenario_id.empty() ? tx.exec_params(query, user_id)
                                          : tx.exec_params(query, user_id, scenario_id);
    json items = json::array();
    for (const auto& row : rows) {
      const auto session_id = std::string(row["id"].c_str());
      const auto report = storedReport(row);
      for (const auto& mistake : learningMistakesFromReport(report)) {
        if (items.size() >= static_cast<size_t>(limit)) break;
        if (!mistake.is_object()) continue;
        const auto mistake_key = jsonString(mistake, "mistakeKey");
        if (mistake_key.empty()) continue;
        const bool mastered = mastered_keys.find(masteryKey(session_id, mistake_key)) != mastered_keys.end();
        if (mastered && !include_mastered) continue;
        items.push_back({
            {"id", session_id + ':' + mistake_key}, {"sessionId", session_id},
            {"mistakeKey", mistake_key}, {"scenarioId", row["scenario_id"].c_str()},
            {"scenarioName", row["scenario_name"].c_str()}, {"finishedDate", row["finished_date"].c_str()},
            {"kind", jsonString(mistake, "kind")}, {"priority", jsonString(mistake, "priority")},
            {"round", jsonInt(mistake, "round", 0)}, {"originalQuote", jsonString(mistake, "originalQuote")},
            {"reason", jsonString(mistake, "reason")},
            {"recommendedRewrite", jsonString(mistake, "recommendedRewrite")}, {"mastered", mastered},
        });
      }
      if (items.size() >= static_cast<size_t>(limit)) break;
    }
    return {{"items", items}, {"total", static_cast<int>(items.size())},
            {"includeMastered", include_mastered}};
  }

  json setLearningMistakeMastery(const std::string& user_id, const std::string& session_id,
                                 const std::string& mistake_key, bool mastered) const {
    if (session_id.empty() || session_id.size() > 120 || mistake_key.empty() || mistake_key.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "错题标识参数无效");
    }
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT e.report FROM sessions s JOIN evaluations e ON e.session_id = s.id
      WHERE s.id = $1 AND s.user_id = $2 AND s.status = 'completed' AND e.status = 'ready'
      FOR UPDATE OF s
    )", session_id, user_id);
    if (rows.empty()) throw ApiError(404, "LEARNING_MISTAKE_NOT_FOUND", "错题不存在或无权访问");
    bool known_mistake = false;
    for (const auto& item : learningMistakesFromReport(storedReport(rows[0]))) {
      if (item.is_object() && jsonString(item, "mistakeKey") == mistake_key) {
        known_mistake = true;
        break;
      }
    }
    if (!known_mistake) throw ApiError(404, "LEARNING_MISTAKE_NOT_FOUND", "错题不存在或无权访问");
    tx.exec_params(R"(
      INSERT INTO learner_mistake_progress(user_id, session_id, mistake_key, mastered_at, updated_at)
      VALUES ($1, $2, $3, CASE WHEN $4 THEN NOW() ELSE NULL END, NOW())
      ON CONFLICT (user_id, session_id, mistake_key) DO UPDATE SET
        mastered_at = CASE WHEN $4 THEN NOW() ELSE NULL END, updated_at = NOW()
    )", user_id, session_id, mistake_key, mastered);
    tx.commit();
    return {{"sessionId", session_id}, {"mistakeKey", mistake_key}, {"mastered", mastered}};
  }

  json learningProfile(const std::string& user_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT s.id, s.scenario_id, s.scenario_name, s.total_score,
        to_char(s.finished_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS finished_date,
        e.report
      FROM sessions s JOIN evaluations e ON e.session_id = s.id
      WHERE s.user_id = $1 AND s.status = 'completed' AND e.status = 'ready'
      ORDER BY s.finished_at ASC NULLS LAST LIMIT 200
    )", user_id);
    const std::vector<std::string> keys = {
        "knowledgeAccuracy", "medicalCompliance", "empathy", "needsDiscovery", "serviceEtiquette"};
    json totals = json::object();
    for (const auto& key : keys) totals[key] = 0.0;
    int total_score = 0;
    std::vector<json> all_trend;
    std::set<std::string> mistake_keys;
    for (const auto& row : rows) {
      const auto report = storedReport(row);
      if (!report.is_object()) continue;
      accumulateDimensionScores(totals, report, keys);
      total_score += row["total_score"].as<int>();
      all_trend.push_back({
          {"sessionId", row["id"].c_str()}, {"scenarioId", row["scenario_id"].c_str()},
          {"scenarioName", row["scenario_name"].c_str()}, {"date", row["finished_date"].c_str()},
          {"totalScore", row["total_score"].as<int>()},
          {"scores", report.value("dimensionScores", json::object())},
      });
      for (const auto& mistake : learningMistakesFromReport(report)) {
        if (mistake.is_object() && !jsonString(mistake, "mistakeKey").empty()) {
          mistake_keys.insert(masteryKey(row["id"].c_str(), jsonString(mistake, "mistakeKey")));
        }
      }
    }
    const auto count = static_cast<int>(all_trend.size());
    json averages = json::object();
    for (const auto& key : keys) {
      averages[key] = count == 0 ? 0.0 : std::round(totals[key].get<double>() / count * 10.0) / 10.0;
    }
    const auto mastered_rows = tx.exec_params(R"(
      SELECT session_id, mistake_key FROM learner_mistake_progress
      WHERE user_id = $1 AND mastered_at IS NOT NULL
    )", user_id);
    int mastered_count = 0;
    for (const auto& row : mastered_rows) {
      if (mistake_keys.find(masteryKey(row["session_id"].c_str(), row["mistake_key"].c_str())) != mistake_keys.end()) {
        ++mastered_count;
      }
    }
    const std::map<std::string, std::pair<std::string, std::string>> dimension_copy = {
        {"knowledgeAccuracy", {"知识准确性", "先确认患者关切，再说明需要由医生结合检查评估的边界。"}},
        {"medicalCompliance", {"医疗合规", "避免确定性承诺或越权判断，明确由医生结合检查评估。"}},
        {"empathy", {"同理心", "先回应患者的担忧和情绪，再说明可协助的下一步。"}},
        {"needsDiscovery", {"需求挖掘", "用开放问题确认患者最在意的重点，再提供服务协助。"}},
        {"serviceEtiquette", {"服务礼仪", "使用清晰、尊重的表达，并给出可执行的服务安排。"}},
    };
    std::vector<std::string> ordered_keys = keys;
    std::sort(ordered_keys.begin(), ordered_keys.end(), [&](const auto& left, const auto& right) {
      return averages[left].get<double>() < averages[right].get<double>();
    });
    json weaknesses = json::array();
    for (size_t index = 0; index < ordered_keys.size() && index < 2; ++index) {
      const auto& key = ordered_keys[index];
      const auto& copy = dimension_copy.at(key);
      weaknesses.push_back({{"key", key}, {"name", copy.first}, {"score", averages[key]}, {"suggestion", copy.second}});
    }
    json trend = json::array();
    const size_t first = all_trend.size() > 12 ? all_trend.size() - 12 : 0;
    for (size_t index = first; index < all_trend.size(); ++index) trend.push_back(all_trend[index]);
    const int score_delta = all_trend.size() < 2 ? 0
        : all_trend.back()["totalScore"].get<int>() - all_trend.front()["totalScore"].get<int>();
    return {{"overall", {{"totalCompleted", count},
                            {"averageScore", count == 0 ? 0.0 : std::round(static_cast<double>(total_score) / count * 10.0) / 10.0},
                            {"scoreDelta", score_delta}}},
            {"dimensionAverages", averages}, {"trend", trend}, {"weaknesses", weaknesses},
            {"mistakes", {{"total", static_cast<int>(mistake_keys.size())}, {"mastered", mastered_count}}}};
  }

  json learningMine(const std::string& user_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto user_rows = tx.exec_params(R"(
      SELECT COALESCE(NULLIF(display_name, ''), '学员') AS display_name
      FROM users WHERE id = $1 AND status = 'active'
    )", user_id);
    if (user_rows.empty()) throw ApiError(404, "USER_NOT_FOUND", "用户不存在");
    const auto stats = tx.exec_params(R"(
      SELECT COUNT(*) FILTER (WHERE status <> 'abandoned') AS total_sessions,
        COUNT(*) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready') AS completed_sessions,
        COUNT(*) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready' AND total_score >= 60) AS passed_sessions,
        AVG(total_score) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready') AS average_score
      FROM sessions WHERE user_id = $1
    )", user_id)[0];
    const auto today = tx.exec(R"(
      SELECT (NOW() AT TIME ZONE 'Asia/Shanghai')::date AS checkin_date,
        to_char(NOW() AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS today,
        EXTRACT(YEAR FROM NOW() AT TIME ZONE 'Asia/Shanghai')::int AS year,
        EXTRACT(MONTH FROM NOW() AT TIME ZONE 'Asia/Shanghai')::int AS month
    )")[0];
    const auto checkin_summary = tx.exec_params(R"(
      SELECT COALESCE(SUM(points), 0) AS points, COUNT(*) AS checkin_days,
        COALESCE(BOOL_OR(checkin_date = $2::date), false) AS checked_today
      FROM learner_checkins WHERE user_id = $1
    )", user_id, today["checkin_date"].c_str())[0];
    const auto checked_rows = tx.exec_params(R"(
      SELECT to_char(checkin_date, 'YYYY-MM-DD') AS checkin_date
      FROM learner_checkins
      WHERE user_id = $1
        AND checkin_date >= date_trunc('month', $2::date)::date
        AND checkin_date < (date_trunc('month', $2::date) + INTERVAL '1 month')::date
      ORDER BY checkin_date
    )", user_id, today["checkin_date"].c_str());
    json checked_dates = json::array();
    for (const auto& row : checked_rows) checked_dates.push_back(row["checkin_date"].c_str());
    const auto streak = tx.exec_params(R"(
      WITH numbered AS (
        SELECT checkin_date + ((ROW_NUMBER() OVER (ORDER BY checkin_date DESC) - 1)::int) AS anchor
        FROM learner_checkins WHERE user_id = $1 AND checkin_date <= $2::date
      )
      SELECT COUNT(*) AS streak FROM numbered WHERE anchor = $2::date
    )", user_id, today["checkin_date"].c_str())[0]["streak"].as<int>();
    const auto favorites = tx.exec_params(
        "SELECT COUNT(*) AS count FROM learner_phrase_favorites WHERE user_id = $1", user_id)[0]["count"].as<int>();
    const auto completed = stats["completed_sessions"].as<int>();
    const auto passed = stats["passed_sessions"].as<int>();
    const auto average = stats["average_score"].is_null() ? 0.0 : stats["average_score"].as<double>();
    return {{"user", {{"displayName", user_rows[0]["display_name"].c_str()}}},
            {"points", checkin_summary["points"].as<int>()},
            {"checkin", {{"today", today["today"].c_str()}, {"year", today["year"].as<int>()},
                         {"month", today["month"].as<int>()},
                         {"checkedToday", checkin_summary["checked_today"].as<bool>()},
                         {"checkinDays", checkin_summary["checkin_days"].as<int>()},
                         {"streakDays", streak}, {"checkedDates", checked_dates}}},
            {"stats", {{"totalCompleted", completed}, {"passRate", completed == 0 ? 0.0
                : std::round(static_cast<double>(passed) / completed * 1000.0) / 10.0},
                       {"averageScore", std::round(average * 10.0) / 10.0}}},
            {"favoritesCount", favorites},
            {"rules", json::array({{{"action", "每日签到"}, {"points", "+10"},
                                     {"description", "每个自然日限一次，按中国时区计算。"}}})}};
  }

  json checkIn(const std::string& user_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto today = tx.exec(R"(
      SELECT (NOW() AT TIME ZONE 'Asia/Shanghai')::date AS checkin_date,
        to_char(NOW() AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS today
    )")[0];
    const auto inserted = tx.exec_params(R"(
      INSERT INTO learner_checkins(user_id, checkin_date, points)
      VALUES ($1, $2::date, 10)
      ON CONFLICT (user_id, checkin_date) DO NOTHING
      RETURNING points
    )", user_id, today["checkin_date"].c_str());
    const auto total = tx.exec_params(
        "SELECT COALESCE(SUM(points), 0) AS points FROM learner_checkins WHERE user_id = $1", user_id)[0];
    tx.commit();
    return {{"checkedIn", !inserted.empty()}, {"alreadyCheckedIn", inserted.empty()},
            {"pointsAwarded", inserted.empty() ? 0 : inserted[0]["points"].as<int>()},
            {"pointsTotal", total["points"].as<int>()}, {"today", today["today"].c_str()}};
  }

  json supervisorDashboard(const std::string& time_range) const {
    const auto time_filter = supervisorTimeFilter(time_range, "s.updated_at");
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto student_count = tx.exec(R"(
      SELECT COUNT(*) AS count FROM users WHERE role = 'learner' AND status = 'active'
    )")[0]["count"].as<int>();
    const auto totals = tx.exec(R"(
      SELECT COUNT(*) FILTER (WHERE s.status <> 'abandoned') AS total_sessions,
        COUNT(*) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready') AS completed_sessions,
        COUNT(*) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready' AND s.total_score >= 60) AS passed_sessions,
        AVG(s.total_score) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready') AS average_score
      FROM sessions s WHERE TRUE
    )" + time_filter)[0];
    const auto scenario_rows = tx.exec(R"(
      SELECT sc.id, sc.name, COUNT(s.id) AS completed_count,
        AVG(s.total_score) AS average_score,
        COALESCE(ROUND(100.0 * COUNT(s.id) FILTER (WHERE s.total_score >= 60) /
          NULLIF(COUNT(s.id), 0), 1), 0) AS pass_rate
      FROM scenarios sc
      LEFT JOIN sessions s ON s.scenario_id = sc.id
        AND s.status = 'completed' AND s.evaluation_status = 'ready'
    )" + supervisorTimeFilter(time_range, "s.updated_at") + R"(
      GROUP BY sc.id, sc.name, sc.sort_order ORDER BY sc.sort_order
    )");
    const auto report_rows = tx.exec(R"(
      SELECT e.report FROM evaluations e JOIN sessions s ON s.id = e.session_id
      WHERE s.status = 'completed' AND s.evaluation_status = 'ready' AND e.status = 'ready'
    )" + time_filter);
    const std::vector<std::string> keys = {
        "knowledgeAccuracy", "medicalCompliance", "empathy", "needsDiscovery", "serviceEtiquette"};
    json dimensions = json::object();
    for (const auto& key : keys) dimensions[key] = 0.0;
    for (const auto& row : report_rows) {
      const auto report = storedReport(row);
      if (report.is_object()) accumulateDimensionScores(dimensions, report, keys);
    }
    if (!report_rows.empty()) {
      for (const auto& key : keys) {
        dimensions[key] = std::round(dimensions[key].get<double>() / report_rows.size() * 10.0) / 10.0;
      }
    }
    json scenario_stats = json::array();
    for (const auto& row : scenario_rows) {
      scenario_stats.push_back({{"scenarioId", row["id"].c_str()}, {"scenarioName", row["name"].c_str()},
                                {"total", row["completed_count"].as<int>()},
                                {"averageScore", row["average_score"].is_null() ? 0.0 : row["average_score"].as<double>()},
                                {"passRate", row["pass_rate"].as<double>()}});
    }
    const auto trend_rows = tx.exec(R"(
      SELECT to_char(s.finished_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS date,
        COUNT(*) AS count, ROUND(AVG(s.total_score)::numeric, 1) AS average_score
      FROM sessions s
      WHERE s.status = 'completed' AND s.evaluation_status = 'ready'
    )" + time_filter + R"(
      GROUP BY 1 ORDER BY date DESC LIMIT 12
    )");
    json trend = json::array();
    for (auto iterator = trend_rows.rbegin(); iterator != trend_rows.rend(); ++iterator) {
      trend.push_back({{"date", (*iterator)["date"].c_str()}, {"count", (*iterator)["count"].as<int>()},
                       {"averageScore", (*iterator)["average_score"].as<double>()}});
    }
    const auto completed = totals["completed_sessions"].as<int>();
    const auto passed = totals["passed_sessions"].as<int>();
    return {{"range", time_range}, {"studentCount", student_count},
            {"totalSessions", totals["total_sessions"].as<int>()}, {"completedSessions", completed},
            {"averageScore", totals["average_score"].is_null() ? 0.0 : totals["average_score"].as<double>()},
            {"passRate", completed == 0 ? 0.0 : std::round(static_cast<double>(passed) / completed * 1000.0) / 10.0},
            {"dimensionAverages", dimensions}, {"scenarioStats", scenario_stats}, {"trend", trend}};
  }

  json listSupervisorMembers(int limit) const {
    limit = clampInt(limit, 1, 100);
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto rows = tx.exec(R"(
      SELECT u.id, COALESCE(NULLIF(u.display_name, ''), '未命名学员') AS display_name,
        to_char(u.created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS joined_at,
        COUNT(s.id) FILTER (WHERE s.status <> 'abandoned') AS total_sessions,
        COUNT(s.id) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready') AS completed_sessions,
        COUNT(s.id) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready' AND s.total_score >= 60) AS passed_sessions,
        AVG(s.total_score) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready') AS average_score,
        to_char(MAX(s.updated_at) AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS last_training_date
      FROM users u LEFT JOIN sessions s ON s.user_id = u.id
      WHERE u.role = 'learner' AND u.status = 'active'
      GROUP BY u.id, u.display_name, u.created_at
      ORDER BY lower(COALESCE(NULLIF(u.display_name, ''), u.id)), u.created_at
      LIMIT )" + std::to_string(limit));
    json members = json::array();
    for (const auto& row : rows) {
      const auto completed = row["completed_sessions"].as<int>();
      const auto passed = row["passed_sessions"].as<int>();
      members.push_back({{"id", row["id"].c_str()}, {"displayName", row["display_name"].c_str()},
                         {"joinedAt", row["joined_at"].c_str()}, {"totalSessions", row["total_sessions"].as<int>()},
                         {"completedSessions", completed},
                         {"averageScore", row["average_score"].is_null() ? 0.0 : row["average_score"].as<double>()},
                         {"passRate", completed == 0 ? 0.0
                            : std::round(static_cast<double>(passed) / completed * 1000.0) / 10.0},
                         {"lastTrainingDate", row["last_training_date"].is_null()
                            ? json(nullptr) : json(row["last_training_date"].c_str())}});
    }
    return {{"members", members}, {"total", static_cast<int>(members.size())}};
  }

  json supervisorMemberDetail(const std::string& member_id) const {
    if (member_id.empty() || member_id.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "成员标识无效");
    }
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto user_rows = tx.exec_params(R"(
      SELECT id, COALESCE(NULLIF(display_name, ''), '未命名学员') AS display_name,
        to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS joined_at
      FROM users WHERE id = $1 AND role = 'learner' AND status = 'active'
    )", member_id);
    if (user_rows.empty()) throw ApiError(404, "MEMBER_NOT_FOUND", "成员不存在");
    const auto stats = tx.exec_params(R"(
      SELECT COUNT(*) FILTER (WHERE status <> 'abandoned') AS total_sessions,
        COUNT(*) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready') AS completed_sessions,
        COUNT(*) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready' AND total_score >= 60) AS passed_sessions,
        AVG(total_score) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready') AS average_score
      FROM sessions WHERE user_id = $1
    )", member_id)[0];
    const auto report_rows = tx.exec_params(R"(
      SELECT s.id, s.scenario_id, s.scenario_name, s.total_score,
        to_char(s.finished_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS finished_date, e.report
      FROM sessions s JOIN evaluations e ON e.session_id = s.id
      WHERE s.user_id = $1 AND s.status = 'completed' AND s.evaluation_status = 'ready' AND e.status = 'ready'
      ORDER BY s.finished_at ASC NULLS LAST LIMIT 200
    )", member_id);
    const std::vector<std::string> keys = {
        "knowledgeAccuracy", "medicalCompliance", "empathy", "needsDiscovery", "serviceEtiquette"};
    json totals = json::object();
    for (const auto& key : keys) totals[key] = 0.0;
    json all_trend = json::array();
    for (const auto& row : report_rows) {
      const auto report = storedReport(row);
      if (!report.is_object()) continue;
      accumulateDimensionScores(totals, report, keys);
      all_trend.push_back({{"sessionId", row["id"].c_str()}, {"scenarioId", row["scenario_id"].c_str()},
                           {"scenarioName", row["scenario_name"].c_str()}, {"date", row["finished_date"].c_str()},
                           {"totalScore", row["total_score"].as<int>()},
                           {"scores", report.value("dimensionScores", json::object())}});
    }
    const auto completed = static_cast<int>(all_trend.size());
    json averages = json::object();
    for (const auto& key : keys) {
      averages[key] = completed == 0 ? 0.0
          : std::round(totals[key].get<double>() / completed * 10.0) / 10.0;
    }
    json trend = json::array();
    const size_t first = all_trend.size() > 12 ? all_trend.size() - 12 : 0;
    for (size_t index = first; index < all_trend.size(); ++index) trend.push_back(all_trend[index]);
    const auto recent_rows = tx.exec_params(R"(
      SELECT scenario_name, total_score,
        to_char(finished_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS finished_date
      FROM sessions WHERE user_id = $1 AND status = 'completed' AND evaluation_status = 'ready'
      ORDER BY finished_at DESC NULLS LAST LIMIT 10
    )", member_id);
    json recent = json::array();
    for (const auto& row : recent_rows) {
      recent.push_back({{"scenarioName", row["scenario_name"].c_str()}, {"totalScore", row["total_score"].as<int>()},
                        {"date", row["finished_date"].c_str()}});
    }
    const auto passed = stats["passed_sessions"].as<int>();
    const auto reported_completed = stats["completed_sessions"].as<int>();
    return {{"member", {{"id", user_rows[0]["id"].c_str()},
                           {"displayName", user_rows[0]["display_name"].c_str()},
                           {"joinedAt", user_rows[0]["joined_at"].c_str()}}},
            {"totalSessions", stats["total_sessions"].as<int>()}, {"completedSessions", reported_completed},
            {"averageScore", stats["average_score"].is_null() ? 0.0 : stats["average_score"].as<double>()},
            {"passRate", reported_completed == 0 ? 0.0
                : std::round(static_cast<double>(passed) / reported_completed * 1000.0) / 10.0},
            {"dimensionAverages", averages}, {"weaknesses", dimensionWeaknesses(averages)},
            {"trend", trend}, {"recentSessions", recent}};
  }

 private:
  static std::string trainingHintFor(const std::string& scenario_id, int current_round, int hint_number) {
    (void)current_round;
    const std::vector<std::string> generic = {
        "先复述患者最在意的点，再提出一个开放式问题，避免急于给出结论。",
        "涉及是否适合治疗、具体疗程或疼痛等判断时，明确需要由医生结合检查评估。",
        "最后给出可执行的服务下一步，例如协助预约咨询、复诊或联系医生。",
    };
    const std::vector<std::string> price = {
        "先确认患者比较报价时最在意的是材料、医生经验、服务安排还是费用透明度。",
        "客观说明费用需要结合检查后的方案确认，不贬低其他机构，也不承诺固定价格。",
        "可邀请患者了解咨询和报价流程，并说明可以协助安排合适的沟通时间。",
    };
    const std::vector<std::string> discomfort = {
        "先回应患者的不安，再了解不适出现的时间、程度和变化，不要直接判断是否正常。",
        "不要给出诊断、用药或结果保证；具体情况应由医生结合检查评估。",
        "建议协助及时联系医生或安排复诊，并提醒患者按医疗机构的正式指引处理。",
    };
    const auto index = static_cast<size_t>(clampInt(hint_number, 1, 3) - 1);
    if (scenario_id == "price-comparison") return price[index];
    if (scenario_id == "post-treatment-discomfort") return discomfort[index];
    return generic[index];
  }

  static std::string supervisorTimeFilter(const std::string& requested_range, const std::string& column) {
    const auto range = requested_range.empty() ? "month" : requested_range;
    if (range == "all") return "";
    std::string unit;
    if (range == "week") unit = "week";
    else if (range == "month") unit = "month";
    else if (range == "quarter") unit = "quarter";
    else throw ApiError(400, "INVALID_ARGUMENT", "range 参数无效");
    return " AND " + column + " >= (date_trunc('" + unit +
        "', NOW() AT TIME ZONE 'Asia/Shanghai') AT TIME ZONE 'Asia/Shanghai')";
  }

  static json dimensionWeaknesses(const json& averages) {
    const std::vector<std::string> keys = {
        "knowledgeAccuracy", "medicalCompliance", "empathy", "needsDiscovery", "serviceEtiquette"};
    const std::map<std::string, std::pair<std::string, std::string>> copy = {
        {"knowledgeAccuracy", {"知识准确性", "先确认患者关切，再说明需由医生结合检查评估的边界。"}},
        {"medicalCompliance", {"医疗合规", "避免确定性承诺或越权判断，清楚说明医生评估边界。"}},
        {"empathy", {"同理心", "先回应患者的担忧和情绪，再说明可协助的下一步。"}},
        {"needsDiscovery", {"需求挖掘", "多用开放式问题确认患者最在意的重点。"}},
        {"serviceEtiquette", {"服务礼仪", "使用清晰、尊重的表达，并给出可执行的服务安排。"}},
    };
    auto ordered = keys;
    std::sort(ordered.begin(), ordered.end(), [&](const auto& left, const auto& right) {
      return averages.value(left, 0.0) < averages.value(right, 0.0);
    });
    json weaknesses = json::array();
    for (size_t index = 0; index < ordered.size() && index < 2; ++index) {
      const auto& key = ordered[index];
      const auto score = averages.value(key, 0.0);
      const auto& item = copy.at(key);
      weaknesses.push_back({{"key", key}, {"name", item.first}, {"score", score},
                            {"severity", score < 60 ? "high" : "medium"},
                            {"suggestion", item.second}});
    }
    return weaknesses;
  }

  static std::string masteryKey(const std::string& session_id, const std::string& mistake_key) {
    return session_id + '\x1f' + mistake_key;
  }

  static std::string favoriteKey(const std::string& session_id, const std::string& phrase_key) {
    return session_id + '\x1f' + phrase_key;
  }

  static json storedReport(const pqxx::row& row) {
    if (row["report"].is_null()) return json();
    return json::parse(row["report"].c_str(), nullptr, false);
  }

  static json learningPhrasesFromReport(const json& report) {
    if (!report.is_object()) return json::array();
    if (report.contains("recommendedPhrases") && report["recommendedPhrases"].is_array()) {
      return report["recommendedPhrases"];
    }
    json phrases = json::array();
    const auto append = [&](const json& item, const std::string& reason) {
      if (!item.is_object() || phrases.size() >= 8) return;
      const auto reply = jsonString(item, "recommendedRewrite");
      if (reply.empty()) return;
      phrases.push_back({{"phraseKey", "legacy-phrase-" + std::to_string(phrases.size() + 1)},
                         {"round", jsonInt(item, "round", 0)}, {"patientSays", ""},
                         {"csReply", reply}, {"reason", reason}});
    };
    if (report.contains("roundComments") && report["roundComments"].is_array()) {
      for (const auto& item : report["roundComments"]) {
        if (item.is_object()) append(item, jsonString(item, "comment"));
      }
    }
    if (report.contains("violations") && report["violations"].is_array()) {
      for (const auto& item : report["violations"]) {
        if (item.is_object()) append(item, jsonString(item, "reason"));
      }
    }
    return phrases;
  }

  static json learningMistakesFromReport(const json& report) {
    if (!report.is_object()) return json::array();
    if (report.contains("learningMistakes") && report["learningMistakes"].is_array()) {
      return report["learningMistakes"];
    }
    json mistakes = json::array();
    if (!report.contains("violations") || !report["violations"].is_array()) return mistakes;
    for (size_t index = 0; index < report["violations"].size() && mistakes.size() < 12; ++index) {
      const auto& item = report["violations"][index];
      if (!item.is_object()) continue;
      const auto round = jsonInt(item, "round", 0);
      mistakes.push_back({
          {"mistakeKey", "legacy-violation-" + std::to_string(round) + "-" + std::to_string(index + 1)},
          {"kind", "violation"}, {"priority", jsonInt(item, "deduction", 0) >= 30 ? "high" : "medium"},
          {"round", round}, {"originalQuote", jsonString(item, "originalQuote")},
          {"reason", jsonString(item, "reason")}, {"recommendedRewrite", jsonString(item, "recommendedRewrite")},
      });
    }
    return mistakes;
  }

  static json messageJson(const std::string& id, const std::string& role,
                          const std::string& content, int round) {
    return {{"id", id}, {"role", role}, {"content", content}, {"round", round}};
  }

  static json messageJson(const pqxx::row& row) {
    return {{"id", row["id"].c_str()}, {"role", row["role"].c_str()},
            {"content", row["content"].c_str()}, {"round", row["round"].as<int>()},
            {"createdAt", row["created_at"].c_str()}};
  }

  static json getSessionRow(pqxx::transaction_base& tx, const std::string& session_id,
                            const std::string& user_id) {
    auto rows = tx.exec_params(
        "SELECT id, user_id, scenario_id, scenario_name, status, current_round, max_rounds, " +
        std::string(kSessionTimes) +
        ", total_score, evaluation_status, custom_patient_profile FROM sessions WHERE id = $1", session_id);
    if (rows.empty() || (!user_id.empty() && std::string(rows[0]["user_id"].c_str()) != user_id)) {
      throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    }
    return sessionJson(rows[0]);
  }

  static void completeJob(pqxx::transaction_base& tx, const AiJob& job) {
    tx.exec_params(R"(
      UPDATE ai_job_attempts SET status = 'succeeded', finished_at = NOW()
      WHERE job_id = $1 AND generation = $2 AND attempt_number = $3
    )", job.id, job.generation, job.attempt);
    tx.exec_params(R"(
      UPDATE ai_jobs SET status = 'succeeded', lease_until = NULL, worker_id = NULL,
        last_error = NULL, finished_at = NOW(), updated_at = NOW() WHERE id = $1
    )", job.id);
  }

  std::string database_url_;
};

class ReliableRoleplayDatabase {
 public:
  explicit ReliableRoleplayDatabase(std::string database_url) : database_url_(std::move(database_url)) {}

  json listScenarios(const std::string& user_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT s.id, s.name, s.category, s.summary, s.difficulty, s.focus, s.patient_profile,
        s.max_rounds, s.roleplay_config,
        active.id AS active_id, active.current_round AS active_current_round,
        active.max_rounds AS active_max_rounds, active.updated_at AS active_updated_at
      FROM scenarios s
      LEFT JOIN LATERAL (
        SELECT id, current_round, max_rounds, updated_at FROM roleplay_sessions
        WHERE user_id = $1 AND scenario_id = s.id AND status = 'in_progress'
        ORDER BY updated_at DESC LIMIT 1
      ) active ON TRUE
      ORDER BY s.sort_order
    )", user_id);
    json items = json::array();
    for (const auto& row : rows) {
      const auto config = json::parse(row["roleplay_config"].c_str());
      json suggested_questions = json::array();
      if (config.contains("suggestedQuestions") && config["suggestedQuestions"].is_array()) {
        for (const auto& question : config["suggestedQuestions"]) {
          if (!question.is_string() || suggested_questions.size() >= 5) continue;
          const auto cleaned = trim(question.get<std::string>());
          if (!cleaned.empty() && utf8Length(cleaned) <= 200) suggested_questions.push_back(cleaned);
        }
      }
      json item = {{"id", row["id"].c_str()}, {"name", row["name"].c_str()},
                   {"category", row["category"].c_str()},
                   {"summary", row["summary"].c_str()}, {"difficulty", row["difficulty"].c_str()},
                   {"focus", json::parse(row["focus"].c_str())},
                   {"patientProfile", json::parse(row["patient_profile"].c_str())},
                   {"maxRounds", row["max_rounds"].as<int>()},
                   {"suggestedQuestions", suggested_questions}, {"activeSession", nullptr}};
      if (!row["active_id"].is_null()) {
        item["activeSession"] = {{"id", row["active_id"].c_str()},
                                 {"currentRound", row["active_current_round"].as<int>()},
                                 {"maxRounds", row["active_max_rounds"].as<int>()},
                                 {"updatedAt", row["active_updated_at"].c_str()}};
      }
      items.push_back(item);
    }
    return {{"items", items}};
  }

  json createSession(const std::string& user_id, const std::string& scenario_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto scenario = tx.exec_params(
        "SELECT id, name, max_rounds FROM scenarios WHERE id = $1", scenario_id);
    if (scenario.empty()) throw ApiError(404, "SCENARIO_NOT_FOUND", "训练场景不存在");
    const auto active = tx.exec_params(R"(
      SELECT id FROM roleplay_sessions
      WHERE user_id = $1 AND scenario_id = $2 AND status = 'in_progress'
    )", user_id, scenario_id);
    if (!active.empty()) {
      throw ApiError(409, "ROLEPLAY_SESSION_IN_PROGRESS", "该场景已有进行中的患者模拟");
    }
    const auto session_id = makeId("rpsess");
    const auto max_rounds = clampInt(scenario[0]["max_rounds"].as<int>(), 1, 10);
    tx.exec_params(R"(
      INSERT INTO roleplay_sessions
        (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds)
      VALUES ($1, $2, $3, $4, 'in_progress', 0, $5)
    )", session_id, user_id, scenario_id, scenario[0]["name"].c_str(), max_rounds);
    const auto saved = getSessionRow(tx, session_id, user_id);
    tx.commit();
    return {{"session", saved}, {"messages", json::array()}};
  }

  json restartSession(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto previous = tx.exec_params(R"(
      SELECT scenario_id, status FROM roleplay_sessions
      WHERE id = $1 AND user_id = $2 FOR UPDATE
    )", session_id, user_id);
    if (previous.empty()) {
      throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    }
    if (std::string(previous[0]["status"].c_str()) != "in_progress") {
      throw ApiError(409, "ROLEPLAY_SESSION_NOT_RESTARTABLE", "只有进行中的患者模拟可以重新开始");
    }
    const auto scenario_id = std::string(previous[0]["scenario_id"].c_str());
    const auto scenario = tx.exec_params(
        "SELECT name, max_rounds FROM scenarios WHERE id = $1", scenario_id);
    tx.exec_params(
        "UPDATE roleplay_sessions SET status = 'abandoned', updated_at = NOW() WHERE id = $1",
        session_id);
    const auto new_id = makeId("rpsess");
    tx.exec_params(R"(
      INSERT INTO roleplay_sessions
        (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds)
      VALUES ($1, $2, $3, $4, 'in_progress', 0, $5)
    )", new_id, user_id, scenario_id, scenario[0]["name"].c_str(),
        clampInt(scenario[0]["max_rounds"].as<int>(), 1, 10));
    const auto saved = getSessionRow(tx, new_id, user_id);
    tx.commit();
    return {{"session", saved}, {"messages", json::array()}};
  }

  json getSession(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto session = getSessionRow(tx, session_id, user_id);
    const auto rows = tx.exec_params(R"(
      SELECT id, role, content, learning_points, compliance_boundary, round,
        to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS created_at
      FROM roleplay_messages WHERE session_id = $1 ORDER BY round, created_at
    )", session_id);
    json messages = json::array();
    for (const auto& row : rows) messages.push_back(messageJson(row));
    const auto pending = tx.exec_params(R"(
      SELECT client_message_id, content, round, reply_status
      FROM roleplay_messages
      WHERE session_id = $1 AND role = 'learner_patient' AND client_message_id IS NOT NULL
        AND reply_status <> 'ready'
      ORDER BY created_at DESC LIMIT 1
    )", session_id);
    json pending_message = nullptr;
    if (!pending.empty()) {
      pending_message = {{"clientMessageId", pending[0]["client_message_id"].c_str()},
                         {"content", pending[0]["content"].c_str()},
                         {"round", pending[0]["round"].as<int>()},
                         {"replyStatus", pending[0]["reply_status"].c_str()}};
    }
    return {{"session", session}, {"messages", messages}, {"pendingMessage", pending_message}};
  }

  json getSessionInternal(const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    return {{"session", getSessionRow(tx, session_id, "")}};
  }

  json listSessions(const std::string& user_id, const std::string& status,
                    const std::string& scenario_id, int limit) const {
    const std::vector<std::string> allowed = {"all", "active", "in_progress", "completed", "abandoned"};
    if (std::find(allowed.begin(), allowed.end(), status) == allowed.end()) {
      throw ApiError(400, "INVALID_ARGUMENT", "status 参数无效");
    }
    limit = clampInt(limit, 1, 50);
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const std::string db_status = status == "active" ? "in_progress" : status;
    std::string query =
        "SELECT r.id, r.scenario_id, r.scenario_name, r.status, r.current_round, r.max_rounds, " +
        std::string(kRoleplaySessionTimes) +
        ", COALESCE(summary.status, 'not_started') AS summary_status "
        "FROM roleplay_sessions r LEFT JOIN roleplay_summaries summary ON summary.session_id = r.id "
        "WHERE r.user_id = " + tx.quote(user_id);
    if (status != "all") query += " AND r.status = " + tx.quote(db_status);
    if (!scenario_id.empty()) query += " AND r.scenario_id = " + tx.quote(scenario_id);
    query += " ORDER BY r.updated_at DESC LIMIT " + std::to_string(limit);
    const auto rows = tx.exec(query);
    json items = json::array();
    for (const auto& row : rows) items.push_back(roleplaySessionJson(row));
    return {{"items", items}, {"total", static_cast<int>(items.size())}};
  }

  json abandonSession(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT status FROM roleplay_sessions WHERE id = $1 AND user_id = $2 FOR UPDATE
    )", session_id, user_id);
    if (rows.empty()) throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    const auto status = std::string(rows[0]["status"].c_str());
    if (status == "abandoned") {
      return {{"sessionId", session_id}, {"status", "abandoned"}};
    }
    if (status != "in_progress") {
      throw ApiError(409, "ROLEPLAY_SESSION_FINISHED", "只有进行中的患者模拟可以放弃");
    }
    tx.exec_params(
        "UPDATE roleplay_sessions SET status = 'abandoned', updated_at = NOW() WHERE id = $1",
        session_id);
    tx.commit();
    return {{"sessionId", session_id}, {"status", "abandoned"}};
  }

  json getScenarioInternal(const std::string& scenario_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT id, name, category, summary, difficulty, focus, patient_profile, max_rounds, roleplay_config
      FROM scenarios WHERE id = $1
    )", scenario_id);
    if (rows.empty()) throw ApiError(404, "SCENARIO_NOT_FOUND", "训练场景不存在");
    const auto& row = rows[0];
    const auto config = json::parse(row["roleplay_config"].c_str());
    const auto guidance = config.contains("serviceGuidance") && config["serviceGuidance"].is_array()
        ? config["serviceGuidance"] : json::array();
    return {{"public", {{"id", row["id"].c_str()}, {"name", row["name"].c_str()},
                         {"category", row["category"].c_str()},
                         {"summary", row["summary"].c_str()}, {"difficulty", row["difficulty"].c_str()},
                         {"focus", json::parse(row["focus"].c_str())},
                         {"patientProfile", json::parse(row["patient_profile"].c_str())},
                         {"maxRounds", row["max_rounds"].as<int>()}}},
            {"roleplay", guidance}};
  }

  json getHistory(const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT role, content, round FROM roleplay_messages
      WHERE session_id = $1 ORDER BY round, created_at
    )", session_id);
    json messages = json::array();
    for (const auto& row : rows) {
      messages.push_back({{"role", row["role"].c_str()}, {"content", row["content"].c_str()},
                          {"round", row["round"].as<int>()}});
    }
    return messages;
  }

  json claimLearnerMessage(const std::string& user_id, const std::string& session_id,
                           const std::string& client_message_id, const std::string& content) const {
    const auto cleaned_content = trim(content);
    if (client_message_id.empty() || client_message_id.size() > 100) {
      throw ApiError(400, "INVALID_ARGUMENT", "clientMessageId 格式无效");
    }
    const auto content_length = utf8Length(cleaned_content);
    if (content_length < 1 || content_length > 1000) {
      throw ApiError(400, "INVALID_ARGUMENT", "消息长度应为 1 到 1000 个字符");
    }
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto session = tx.exec_params(R"(
      SELECT status, current_round, max_rounds FROM roleplay_sessions
      WHERE id = $1 AND user_id = $2 FOR UPDATE
    )", session_id, user_id);
    if (session.empty()) {
      throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    }
    const auto existing = tx.exec_params(R"(
      SELECT learner.id AS learner_id, learner.content AS learner_content,
        learner.round AS learner_round, learner.reply_status,
        learner.reply_lease_until > NOW() AS lease_active,
        customer.id AS customer_id, customer.content AS customer_content,
        customer.learning_points AS customer_learning_points,
        customer.compliance_boundary AS customer_compliance_boundary
      FROM roleplay_messages learner
      LEFT JOIN roleplay_messages customer ON customer.session_id = learner.session_id
        AND customer.role = 'standard_customer' AND customer.round = learner.round
      WHERE learner.session_id = $1 AND learner.client_message_id = $2
        AND learner.role = 'learner_patient' LIMIT 1
    )", session_id, client_message_id);
    if (!existing.empty()) {
      const auto& row = existing[0];
      if (std::string(row["learner_content"].c_str()) != cleaned_content) {
        throw ApiError(409, "IDEMPOTENCY_CONFLICT", "同一 clientMessageId 不能用于不同内容");
      }
      json result = {{"learnerMessage", messageJson(
                          row["learner_id"].c_str(), "learner_patient",
                          row["learner_content"].c_str(), row["learner_round"].as<int>())},
                     {"standardCustomerMessage", nullptr}, {"isComplete", !row["customer_id"].is_null()},
                     {"round", row["learner_round"].as<int>()}};
      if (!row["customer_id"].is_null()) {
        result["standardCustomerMessage"] = messageJson(
            row["customer_id"].c_str(), "standard_customer", row["customer_content"].c_str(),
            row["learner_round"].as<int>(), json::parse(row["customer_learning_points"].c_str()),
            row["customer_compliance_boundary"].is_null()
                ? "" : row["customer_compliance_boundary"].c_str());
        tx.commit();
        return result;
      }
      const auto status = std::string(session[0]["status"].c_str());
      if (status == "abandoned") {
        throw ApiError(409, "ROLEPLAY_SESSION_ABANDONED", "已放弃的患者模拟不能恢复");
      }
      if (status != "in_progress") {
        throw ApiError(409, "ROLEPLAY_SESSION_FINISHED", "患者模拟已结束");
      }
      const bool lease_active = !row["lease_active"].is_null() && row["lease_active"].as<bool>();
      if (std::string(row["reply_status"].c_str()) == "generating" && lease_active) {
        throw ApiError(409, "ROLEPLAY_RESPONSE_PENDING", "标准客服回复正在生成，请稍后查询会话");
      }
      const auto token = randomToken(16);
      tx.exec_params(R"(
        UPDATE roleplay_messages SET reply_status = 'generating',
          reply_lease_until = NOW() + ($2 * INTERVAL '1 second'),
          reply_attempt_token = $3, reply_error_type = NULL WHERE id = $1
      )", row["learner_id"].c_str(), kReplyLeaseSeconds, token);
      result["attemptToken"] = token;
      tx.commit();
      return result;
    }
    const auto status = std::string(session[0]["status"].c_str());
    if (status == "abandoned") {
      throw ApiError(409, "ROLEPLAY_SESSION_ABANDONED", "已放弃的患者模拟不能恢复");
    }
    if (status != "in_progress") {
      throw ApiError(409, "ROLEPLAY_SESSION_FINISHED", "患者模拟已结束，不能继续发送消息");
    }
    const auto current_round = session[0]["current_round"].as<int>();
    if (current_round >= session[0]["max_rounds"].as<int>()) {
      throw ApiError(409, "MAX_ROUNDS_REACHED", "已达到最大患者模拟轮数");
    }
    const auto pending = tx.exec_params(R"(
      SELECT id FROM roleplay_messages
      WHERE session_id = $1 AND role = 'learner_patient' AND round = $2
    )", session_id, current_round + 1);
    if (!pending.empty()) {
      throw ApiError(409, "ROLEPLAY_RESPONSE_PENDING", "上一条提问正在等待标准客服回复，请使用原请求重试");
    }
    const auto message_id = makeId("rpmsg");
    const auto token = randomToken(16);
    const auto round = current_round + 1;
    tx.exec_params(R"(
      INSERT INTO roleplay_messages
        (id, session_id, role, content, round, client_message_id, reply_status,
         reply_lease_until, reply_attempt_token)
      VALUES ($1, $2, 'learner_patient', $3, $4, $5, 'generating',
              NOW() + ($6 * INTERVAL '1 second'), $7)
    )", message_id, session_id, cleaned_content, round, client_message_id,
        kReplyLeaseSeconds, token);
    tx.exec_params("UPDATE roleplay_sessions SET updated_at = NOW() WHERE id = $1", session_id);
    tx.commit();
    return {{"learnerMessage", messageJson(message_id, "learner_patient", cleaned_content, round)},
            {"standardCustomerMessage", nullptr}, {"isComplete", false}, {"round", round},
            {"attemptToken", token}};
  }

  void markReplyFailed(const std::string& session_id, int round, const std::string& token,
                       const std::string& error_type) const noexcept {
    try {
      pqxx::connection connection(database_url_);
      pqxx::work tx(connection);
      tx.exec_params(R"(
        UPDATE roleplay_messages SET reply_status = 'failed', reply_lease_until = NULL,
          reply_attempt_token = NULL, reply_error_type = $4
        WHERE session_id = $1 AND role = 'learner_patient' AND round = $2
          AND reply_attempt_token = $3
      )", session_id, round, token, error_type);
      tx.commit();
    } catch (const std::exception& error) {
      std::cerr << json({{"event", "roleplay_reply_failure_persist_error"},
                        {"sessionId", session_id}, {"error", error.what()}}).dump() << '\n';
    }
  }

  json saveStandardCustomerReply(const std::string& user_id, const std::string& session_id,
                                 int round, const std::string& token, const json& model_reply) const {
    const auto reply = trim(jsonString(model_reply, "reply"));
    const auto learning_points = model_reply.value("learningPoints", json::array());
    const auto boundary = trim(jsonString(model_reply, "complianceBoundary"));
    if (utf8Length(reply) < 1 || utf8Length(reply) > 1000 || !learning_points.is_array() ||
        learning_points.size() < 2 || learning_points.size() > 4 || boundary.empty() ||
        utf8Length(boundary) > 300) {
      throw ApiError(503, "MODEL_INVALID_RESPONSE", "模型未返回有效标准客服回复");
    }
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto session_rows = tx.exec_params(R"(
      SELECT * FROM roleplay_sessions WHERE id = $1 AND user_id = $2 FOR UPDATE
    )", session_id, user_id);
    if (session_rows.empty()) {
      throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    }
    const auto existing = tx.exec_params(R"(
      SELECT id, content, learning_points, compliance_boundary FROM roleplay_messages
      WHERE session_id = $1 AND role = 'standard_customer' AND round = $2
    )", session_id, round);
    if (!existing.empty()) {
      const auto session = getSessionRow(tx, session_id, user_id);
      const auto& row = existing[0];
      tx.commit();
      return {{"standardCustomerMessage", messageJson(
                  row["id"].c_str(), "standard_customer", row["content"].c_str(), round,
                  json::parse(row["learning_points"].c_str()),
                  row["compliance_boundary"].is_null() ? "" : row["compliance_boundary"].c_str())},
              {"session", session}, {"shouldFinish", session["status"] == "completed"}};
    }
    const auto status = std::string(session_rows[0]["status"].c_str());
    if (status == "abandoned") {
      throw ApiError(409, "ROLEPLAY_SESSION_ABANDONED", "已放弃的患者模拟不能恢复");
    }
    if (status != "in_progress") throw ApiError(409, "ROLEPLAY_SESSION_FINISHED", "患者模拟已结束");
    const auto input = tx.exec_params(R"(
      SELECT id, reply_attempt_token FROM roleplay_messages
      WHERE session_id = $1 AND role = 'learner_patient' AND round = $2 FOR UPDATE
    )", session_id, round);
    if (input.empty() || input[0]["reply_attempt_token"].is_null() ||
        std::string(input[0]["reply_attempt_token"].c_str()) != token) {
      throw ApiError(409, "ROLEPLAY_RESPONSE_PENDING", "该回复生成租约已失效，请查询会话后重试");
    }
    const auto message_id = makeId("rpmsg");
    tx.exec_params(R"(
      INSERT INTO roleplay_messages
        (id, session_id, role, content, learning_points, compliance_boundary, round)
      VALUES ($1, $2, 'standard_customer', $3, $4::jsonb, $5, $6)
    )", message_id, session_id, reply, learning_points.dump(), boundary, round);
    tx.exec_params(R"(
      UPDATE roleplay_messages SET reply_status = 'ready', reply_lease_until = NULL,
        reply_attempt_token = NULL, reply_error_type = NULL WHERE id = $1
    )", input[0]["id"].c_str());
    const bool should_finish = round >= session_rows[0]["max_rounds"].as<int>();
    if (should_finish) {
      tx.exec_params(R"(
        UPDATE roleplay_sessions SET current_round = $2, status = 'completed',
          finished_at = NOW(), updated_at = NOW() WHERE id = $1
      )", session_id, round);
      tx.exec_params(R"(
        INSERT INTO roleplay_summaries(session_id, status, updated_at)
        VALUES ($1, 'generating', NOW())
        ON CONFLICT (session_id) DO UPDATE SET status = 'generating', summary = NULL,
          error_type = NULL, updated_at = NOW()
      )", session_id);
      enqueueAiJob(tx, "roleplay_summary", session_id);
    } else {
      tx.exec_params(
          "UPDATE roleplay_sessions SET current_round = $2, updated_at = NOW() WHERE id = $1",
          session_id, round);
    }
    const auto saved = getSessionRow(tx, session_id, user_id);
    tx.commit();
    return {{"standardCustomerMessage", messageJson(
                message_id, "standard_customer", reply, round, learning_points, boundary)},
            {"session", saved}, {"shouldFinish", should_finish}};
  }

  json finish(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT r.status, r.current_round, summary.status AS summary_status
      FROM roleplay_sessions r
      LEFT JOIN roleplay_summaries summary ON summary.session_id = r.id
      WHERE r.id = $1 AND r.user_id = $2 FOR UPDATE OF r
    )", session_id, user_id);
    if (rows.empty()) throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    const auto status = std::string(rows[0]["status"].c_str());
    if (status == "abandoned") {
      throw ApiError(409, "ROLEPLAY_SESSION_ABANDONED", "已放弃的患者模拟不能结束或恢复");
    }
    if (status == "completed") {
      const auto saved = getSessionRow(tx, session_id, user_id);
      tx.commit();
      return saved;
    }
    if (rows[0]["current_round"].as<int>() == 0) {
      throw ApiError(422, "MIN_ROUNDS_NOT_REACHED", "至少完成 1 轮患者模拟后才能生成复盘");
    }
    const auto pending = tx.exec_params(R"(
      SELECT 1 FROM roleplay_messages
      WHERE session_id = $1 AND role = 'learner_patient' AND reply_status <> 'ready' LIMIT 1
    )", session_id);
    if (!pending.empty()) {
      throw ApiError(409, "ROLEPLAY_RESPONSE_PENDING", "标准客服回复正在生成，暂不能结束患者模拟");
    }
    tx.exec_params(R"(
      UPDATE roleplay_sessions SET status = 'completed', finished_at = NOW(), updated_at = NOW()
      WHERE id = $1
    )", session_id);
    tx.exec_params(R"(
      INSERT INTO roleplay_summaries(session_id, status, updated_at) VALUES ($1, 'generating', NOW())
      ON CONFLICT (session_id) DO UPDATE SET status = 'generating', summary = NULL,
        error_type = NULL, updated_at = NOW()
    )", session_id);
    enqueueAiJob(tx, "roleplay_summary", session_id);
    const auto saved = getSessionRow(tx, session_id, user_id);
    tx.commit();
    return saved;
  }

  void retrySummary(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(R"(
      SELECT r.status, summary.status AS summary_status
      FROM roleplay_sessions r LEFT JOIN roleplay_summaries summary ON summary.session_id = r.id
      WHERE r.id = $1 AND r.user_id = $2 FOR UPDATE OF r
    )", session_id, user_id);
    if (rows.empty()) throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    if (std::string(rows[0]["status"].c_str()) != "completed" ||
        rows[0]["summary_status"].is_null() ||
        std::string(rows[0]["summary_status"].c_str()) != "failed") {
      throw ApiError(409, "ROLEPLAY_SUMMARY_NOT_RETRYABLE", "当前复盘不可重试");
    }
    tx.exec_params(R"(
      UPDATE roleplay_summaries SET status = 'generating', summary = NULL,
        error_type = NULL, updated_at = NOW() WHERE session_id = $1
    )", session_id);
    tx.exec_params("UPDATE roleplay_sessions SET updated_at = NOW() WHERE id = $1", session_id);
    enqueueAiJob(tx, "roleplay_summary", session_id, true);
    tx.commit();
  }

  json getSummary(const std::string& user_id, const std::string& session_id) const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto session = tx.exec_params(
        "SELECT status FROM roleplay_sessions WHERE id = $1 AND user_id = $2", session_id, user_id);
    if (session.empty()) throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    const auto summary = tx.exec_params(
        "SELECT status, summary FROM roleplay_summaries WHERE session_id = $1", session_id);
    if (summary.empty()) {
      return {{"sessionId", session_id}, {"status", "not_started"},
              {"retryable", false}, {"summary", nullptr}};
    }
    const auto status = std::string(summary[0]["status"].c_str());
    if (status != "ready" || summary[0]["summary"].is_null()) {
      return {{"sessionId", session_id}, {"status", status},
              {"retryable", status == "failed"}, {"summary", nullptr}};
    }
    return {{"sessionId", session_id}, {"status", "ready"}, {"retryable", false},
            {"summary", json::parse(summary[0]["summary"].c_str())}};
  }

  void saveSummary(const AiJob& job, json summary, const std::string& model_version) const {
    summary["modelVersion"] = model_version;
    summary["promptVersion"] = "roleplay-summary-prompt-v1";
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto owned = tx.exec_params(R"(
      SELECT 1 FROM ai_jobs WHERE id = $1 AND status = 'running' AND target_id = $2
        AND generation = $3 AND attempts = $4 AND lease_until > NOW() FOR UPDATE
    )", job.id, job.target_id, job.generation, job.attempt);
    if (owned.empty()) throw ApiError(409, "JOB_LEASE_LOST", "复盘任务租约已失效");
    tx.exec_params(R"(
      INSERT INTO roleplay_summaries
        (session_id, status, summary, model_version, prompt_version, error_type, generated_at, updated_at)
      VALUES ($1, 'ready', $2::jsonb, $3, 'roleplay-summary-prompt-v1', NULL, NOW(), NOW())
      ON CONFLICT (session_id) DO UPDATE SET status = 'ready', summary = EXCLUDED.summary,
        model_version = EXCLUDED.model_version, prompt_version = EXCLUDED.prompt_version,
        error_type = NULL, generated_at = NOW(), updated_at = NOW()
    )", job.target_id, summary.dump(), model_version);
    tx.exec_params("UPDATE roleplay_sessions SET updated_at = NOW() WHERE id = $1", job.target_id);
    tx.exec_params(R"(
      UPDATE ai_job_attempts SET status = 'succeeded', finished_at = NOW()
      WHERE job_id = $1 AND generation = $2 AND attempt_number = $3
    )", job.id, job.generation, job.attempt);
    tx.exec_params(R"(
      UPDATE ai_jobs SET status = 'succeeded', lease_until = NULL, worker_id = NULL,
        last_error = NULL, finished_at = NOW(), updated_at = NOW() WHERE id = $1
    )", job.id);
    tx.commit();
  }

 private:
  static json messageJson(const std::string& id, const std::string& role,
                          const std::string& content, int round,
                          const json& learning_points = json::array(),
                          const std::string& compliance_boundary = "") {
    return {{"id", id}, {"role", role}, {"content", content}, {"round", round},
            {"learningPoints", learning_points},
            {"complianceBoundary", compliance_boundary.empty()
                ? json(nullptr) : json(compliance_boundary)}};
  }

  static json messageJson(const pqxx::row& row) {
    const auto learning_points = row["learning_points"].is_null()
        ? json::array() : json::parse(row["learning_points"].c_str());
    const auto boundary = row["compliance_boundary"].is_null()
        ? "" : std::string(row["compliance_boundary"].c_str());
    auto message = messageJson(row["id"].c_str(), row["role"].c_str(),
                               row["content"].c_str(), row["round"].as<int>(),
                               learning_points, boundary);
    message["createdAt"] = row["created_at"].c_str();
    return message;
  }

  static json getSessionRow(pqxx::transaction_base& tx, const std::string& session_id,
                            const std::string& user_id) {
    const auto rows = tx.exec_params(
        "SELECT r.id, r.user_id, r.scenario_id, r.scenario_name, r.status, r.current_round, "
        "r.max_rounds, " + std::string(kRoleplaySessionTimes) +
        ", COALESCE(summary.status, 'not_started') AS summary_status "
        "FROM roleplay_sessions r LEFT JOIN roleplay_summaries summary ON summary.session_id = r.id "
        "WHERE r.id = $1", session_id);
    if (rows.empty() || (!user_id.empty() && std::string(rows[0]["user_id"].c_str()) != user_id)) {
      throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    }
    return roleplaySessionJson(rows[0]);
  }

  std::string database_url_;
};

class AiJobQueue {
 public:
  explicit AiJobQueue(std::string database_url) : database_url_(std::move(database_url)) {}

  std::optional<AiJob> claim(const std::string& worker_id) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    markExhaustedLeases(tx);
    const auto rows = tx.exec_params(R"(
      WITH candidate AS (
        SELECT id FROM ai_jobs
        WHERE attempts < max_attempts AND (
          (status IN ('pending', 'retry_wait') AND available_at <= NOW()) OR
          (status = 'running' AND lease_until <= NOW())
        )
        ORDER BY available_at, created_at
        FOR UPDATE SKIP LOCKED
        LIMIT 1
      )
      UPDATE ai_jobs AS job SET status = 'running', attempts = job.attempts + 1,
        lease_until = NOW() + ($2 * INTERVAL '1 second'), worker_id = $1,
        updated_at = NOW(), finished_at = NULL
      FROM candidate WHERE job.id = candidate.id
      RETURNING job.id, job.job_type, job.target_id, job.generation, job.attempts
    )", worker_id, kJobLeaseSeconds);
    if (rows.empty()) {
      tx.commit();
      return std::nullopt;
    }
    AiJob job{rows[0]["id"].c_str(), rows[0]["job_type"].c_str(),
              rows[0]["target_id"].c_str(), rows[0]["generation"].as<int>(),
              rows[0]["attempts"].as<int>()};
    tx.exec_params(R"(
      UPDATE ai_job_attempts SET status = 'failed', error_type = 'JOB_LEASE_EXPIRED',
        error_message = 'worker lease expired', finished_at = NOW()
      WHERE job_id = $1 AND generation = $2 AND status = 'running'
    )", job.id, job.generation);
    tx.exec_params(R"(
      INSERT INTO ai_job_attempts(job_id, generation, attempt_number, status, worker_id)
      VALUES ($1, $2, $3, 'running', $4)
    )", job.id, job.generation, job.attempt, worker_id);
    tx.commit();
    return job;
  }

  void fail(const AiJob& job, const std::string& error_type,
            const std::string& error_message, bool retryable) const {
    pqxx::connection connection(database_url_);
    pqxx::work tx(connection);
    const auto rows = tx.exec_params(
        "SELECT attempts, max_attempts, job_type, target_id FROM ai_jobs "
        "WHERE id = $1 AND status = 'running' AND generation = $2 AND attempts = $3 FOR UPDATE",
        job.id, job.generation, job.attempt);
    if (rows.empty()) {
      tx.commit();
      return;
    }
    const auto attempts = rows[0]["attempts"].as<int>();
    const auto max_attempts = rows[0]["max_attempts"].as<int>();
    const bool retry = retryable && attempts < max_attempts;
    const int delay_seconds = retry ? aiJobRetryDelaySeconds(attempts) : 0;
    const auto stored_message = utf8Truncate(error_message, 500);
    tx.exec_params(R"(
      UPDATE ai_job_attempts SET status = $4, error_type = $5,
        error_message = $6, finished_at = NOW()
      WHERE job_id = $1 AND generation = $2 AND attempt_number = $3
    )", job.id, job.generation, job.attempt,
        retry ? "retry_wait" : "failed", error_type, stored_message);
    if (retry) {
      tx.exec_params(R"(
        UPDATE ai_jobs SET status = 'retry_wait', available_at = NOW() + ($2 * INTERVAL '1 second'),
          lease_until = NULL, worker_id = NULL, last_error = $3, updated_at = NOW()
        WHERE id = $1
      )", job.id, delay_seconds, error_type);
    } else {
      tx.exec_params(R"(
        UPDATE ai_jobs SET status = 'dead', lease_until = NULL, worker_id = NULL,
          last_error = $2, updated_at = NOW(), finished_at = NOW() WHERE id = $1
      )", job.id, error_type);
      markTargetFailed(tx, rows[0]["job_type"].c_str(), rows[0]["target_id"].c_str(), error_type);
    }
    tx.commit();
    std::cerr << json({{"event", retry ? "ai_job_retry_scheduled" : "ai_job_dead"},
                      {"jobId", job.id}, {"jobType", job.type}, {"attempt", attempts},
                      {"delaySeconds", delay_seconds}, {"errorType", error_type}}).dump() << '\n';
  }

  json stats() const {
    pqxx::connection connection(database_url_);
    pqxx::read_transaction tx(connection);
    const auto row = tx.exec(R"(
      SELECT COUNT(*) FILTER (WHERE status IN ('pending', 'running', 'retry_wait')) AS pending_jobs,
        COUNT(*) FILTER (WHERE status = 'dead') AS dead_jobs FROM ai_jobs
    )")[0];
    return {{"pendingJobs", row["pending_jobs"].as<int>()},
            {"deadJobs", row["dead_jobs"].as<int>()}};
  }

 private:
  static void markTargetFailed(pqxx::transaction_base& tx, const std::string& type,
                               const std::string& target_id, const std::string& error_type) {
    if (type == "evaluation") {
      tx.exec_params(R"(
        UPDATE evaluations SET status = 'failed', error_type = $2, updated_at = NOW()
        WHERE session_id = $1
      )", target_id, error_type);
      tx.exec_params(R"(
        UPDATE sessions SET evaluation_status = 'failed', updated_at = NOW() WHERE id = $1
      )", target_id);
    } else {
      tx.exec_params(R"(
        INSERT INTO roleplay_summaries(session_id, status, error_type, updated_at)
        VALUES ($1, 'failed', $2, NOW())
        ON CONFLICT (session_id) DO UPDATE SET status = 'failed',
          error_type = EXCLUDED.error_type, updated_at = NOW()
      )", target_id, error_type);
      tx.exec_params("UPDATE roleplay_sessions SET updated_at = NOW() WHERE id = $1", target_id);
    }
  }

  static void markExhaustedLeases(pqxx::transaction_base& tx) {
    const auto exhausted = tx.exec(R"(
      SELECT id, job_type, target_id, generation, attempts FROM ai_jobs
      WHERE status = 'running' AND lease_until <= NOW() AND attempts >= max_attempts
      FOR UPDATE SKIP LOCKED
    )");
    for (const auto& row : exhausted) {
      tx.exec_params(R"(
        UPDATE ai_job_attempts SET status = 'failed', error_type = 'JOB_LEASE_EXPIRED',
          error_message = 'worker lease expired after final attempt', finished_at = NOW()
        WHERE job_id = $1 AND generation = $2 AND attempt_number = $3 AND status = 'running'
      )", row["id"].c_str(), row["generation"].as<int>(), row["attempts"].as<int>());
      tx.exec_params(R"(
        UPDATE ai_jobs SET status = 'dead', lease_until = NULL, worker_id = NULL,
          last_error = 'JOB_LEASE_EXPIRED', updated_at = NOW(), finished_at = NOW()
        WHERE id = $1
      )", row["id"].c_str());
      markTargetFailed(tx, row["job_type"].c_str(), row["target_id"].c_str(),
                       "JOB_LEASE_EXPIRED");
    }
  }

  std::string database_url_;
};
