#pragma once

struct AiJob {
  std::string id;
  std::string type;
  std::string target_id;
  int generation = 1;
  int attempt = 0;
};

// Every transaction that touches both a session and its AI state must lock
// the session first. The session serializes report/job writes for that target;
// queue claim and heartbeat transactions touch jobs only and never wait on it.
inline bool lockAiJobTarget(pqxx::transaction_base& tx, const std::string& type,
                            const std::string& target_id, bool skip_locked = false) {
  const std::string table = type == "evaluation" ? "sessions" : "roleplay_sessions";
  return !tx.exec_params("SELECT id FROM " + table + " WHERE id = $1 FOR UPDATE" +
                        (skip_locked ? " SKIP LOCKED" : ""), target_id).empty();
}

inline int aiJobRetryDelaySeconds(int completed_attempts) {
  return completed_attempts <= 1 ? 5 : 30;
}

inline void enqueueAiJob(pqxx::transaction_base& tx, const std::string& type,
                         const std::string& target_id, bool reset_dead_job = false) {
  const auto dedupe_key = type == "evaluation"
      ? "evaluation:" + target_id : "roleplay-summary:" + target_id;
  if (reset_dead_job) {
    const auto reset = tx.exec_params(R"(
      INSERT INTO ai_jobs
        (id, job_type, target_id, dedupe_key, status, attempts, available_at, updated_at)
      VALUES ($1, $2, $3, $4, 'pending', 0, NOW(), NOW())
      ON CONFLICT (dedupe_key) DO UPDATE SET
        status = 'pending', generation = ai_jobs.generation + 1, attempts = 0,
        available_at = NOW(), lease_until = NULL,
        worker_id = NULL, last_error = NULL, finished_at = NULL, updated_at = NOW()
      WHERE ai_jobs.generation < 100
    )", makeId("job"), type, target_id, dedupe_key);
    if (reset.affected_rows() == 0) {
      throw ApiError(409, "AI_JOB_GENERATION_EXHAUSTED", "AI 任务重试次数已达到上限");
    }
    return;
  }
  tx.exec_params(R"(
    INSERT INTO ai_jobs(id, job_type, target_id, dedupe_key, status, available_at)
    VALUES ($1, $2, $3, $4, 'pending', NOW())
    ON CONFLICT (dedupe_key) DO NOTHING
  )", makeId("job"), type, target_id, dedupe_key);
}

inline bool ensureAiJob(pqxx::transaction_base& tx, const std::string& type,
                        const std::string& target_id) {
  const auto dedupe_key = type == "evaluation"
      ? "evaluation:" + target_id : "roleplay-summary:" + target_id;
  auto jobs = tx.exec_params(
      "SELECT status, generation FROM ai_jobs WHERE dedupe_key = $1", dedupe_key);
  if (jobs.empty()) {
    tx.exec_params(R"(
      INSERT INTO ai_jobs
        (id, job_type, target_id, dedupe_key, status, attempts, available_at, updated_at)
      VALUES ($1, $2, $3, $4, 'pending', 0, NOW(), NOW())
      ON CONFLICT (dedupe_key) DO NOTHING
    )", makeId("job"), type, target_id, dedupe_key);
    jobs = tx.exec_params(
        "SELECT status, generation FROM ai_jobs WHERE dedupe_key = $1", dedupe_key);
  }
  if (jobs.empty()) return false;
  const auto status = std::string(jobs[0]["status"].c_str());
  if (status == "pending" || status == "running" || status == "retry_wait") return true;
  if (jobs[0]["generation"].as<int>() >= 100) return false;
  const auto reset = tx.exec_params(R"(
    UPDATE ai_jobs SET status = 'pending', generation = generation + 1, attempts = 0,
      available_at = NOW(), lease_until = NULL, worker_id = NULL,
      last_error = NULL, finished_at = NULL, updated_at = NOW()
    WHERE dedupe_key = $1 AND status IN ('succeeded', 'dead') AND generation < 100
  )", dedupe_key);
  return reset.affected_rows() == 1;
}

class ReliableDatabase {
 public:
  explicit ReliableDatabase(std::shared_ptr<DatabasePool> database_pool)
      : database_pool_(std::move(database_pool)) {}

  bool healthy() const {
    try {
      auto connection = database_pool_->acquire();
      pqxx::read_transaction tx(connection.get());
      const auto row = tx.exec(R"(
        SELECT to_regclass('ai_jobs') IS NOT NULL AS jobs_ready,
          to_regclass('users') IS NOT NULL AS users_ready,
          to_regclass('messages') IS NOT NULL AS messages_ready,
          to_regclass('learner_mistake_progress') IS NOT NULL AS learner_insights_ready,
          to_regclass('session_hints') IS NOT NULL AS training_experience_ready,
          to_regclass('learner_checkins') IS NOT NULL AS learner_growth_ready,
          to_regclass('learner_phrase_favorites') IS NOT NULL AS phrase_favorites_ready,
          to_regclass('supervisor_team_members') IS NOT NULL AS supervisor_team_ready
      )")[0];
      return row["jobs_ready"].as<bool>() && row["users_ready"].as<bool>() &&
             row["messages_ready"].as<bool>() && row["learner_insights_ready"].as<bool>() &&
             row["training_experience_ready"].as<bool>() && row["learner_growth_ready"].as<bool>() &&
             row["phrase_favorites_ready"].as<bool>() && row["supervisor_team_ready"].as<bool>();
    } catch (...) {
      return false;
    }
  }

  json listScenarios(const std::string& user_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
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
      WHERE s.is_active AND NOT s.is_template
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
          {"maxRounds", row["max_rounds"].as<int>()},
          {"bestScore", row["best_score"].is_null()
              ? json(nullptr) : json(row["best_score"].as<int>())},
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

  // 主管端场景目录：发布培训计划时选择适用场景用。
  // 刻意只返回场景本身的目录字段，不 JOIN sessions，因此不会带出任何
  // 学员侧数据（bestScore / activeSession），与「管理员不读学员明细」的边界一致。
  json listScenarioCatalog() const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec(R"(
      SELECT id, name, category, difficulty
      FROM scenarios
      WHERE is_active AND NOT is_template
      ORDER BY sort_order
    )");
    json items = json::array();
    for (const auto& row : rows) {
      items.push_back({{"id", row["id"].c_str()}, {"name", row["name"].c_str()},
                       {"category", row["category"].c_str()},
                       {"difficulty", row["difficulty"].c_str()}});
    }
    return {{"items", items}};
  }

  // ── 场景管理（主管端内容运营） ────────────────────────────────────────
  // JSONB 列宽容解析：NULL / 坏 JSON 一律落回空容器，绝不让历史脏数据炸掉管理页。
  static json parseColumn(const pqxx::row& row, const char* name, bool as_array = false) {
    if (row[name].is_null()) return as_array ? json::array() : json::object();
    const auto parsed = json::parse(row[name].c_str(), nullptr, false);
    if (as_array) return parsed.is_array() ? parsed : json::array();
    return parsed.is_object() ? parsed : json::object();
  }

  // 字符串数组白名单校验：空串/超长/超条数都显式 400，而不是静默丢弃让主管以为保存成功。
  static json stringArrayOf(const json& object, const char* key, size_t max_items,
                            size_t max_chars, const char* label) {
    json items = json::array();
    if (!object.contains(key) || !object[key].is_array()) return items;
    for (const auto& item : object[key]) {
      if (!item.is_string()) continue;
      const auto text = trim(item.get<std::string>());
      if (text.empty() || utf8Length(text) > max_chars) {
        throw ApiError(400, "INVALID_ARGUMENT",
                       std::string(label) + "每条需 1-" + std::to_string(max_chars) + " 个字");
      }
      if (items.size() >= max_items) {
        throw ApiError(400, "INVALID_ARGUMENT",
                       std::string(label) + "最多 " + std::to_string(max_items) + " 条");
      }
      items.push_back(text);
    }
    return items;
  }

  /* 校验 + 归一化场景字段：只接受白名单键，任何未知输入直接丢弃而不是透传数据库。
     传入的 merged 必须是「已存在的行 + 请求体覆盖」后的完整字段集——校验永远面对
     全量字段，这样 PUT {"isActive":false} 这种轻量请求也能走同一条校验路径。 */
  static json validateScenarioPayload(const json& merged) {
    json out = json::object();
    const auto id = jsonString(merged, "id");
    if (id.size() < 2 || id.size() > 60 ||
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos) {
      throw ApiError(400, "INVALID_ARGUMENT", "场景 id 只能是小写字母、数字和连字符（2-60 位）");
    }
    out["id"] = id;
    const auto name = trim(jsonString(merged, "name"));
    if (utf8Length(name) < 2 || utf8Length(name) > 30) {
      throw ApiError(400, "INVALID_ARGUMENT", "场景名称需 2-30 个字");
    }
    out["name"] = name;
    const auto category = jsonString(merged, "category");
    if (!isSceneCategory(category)) throw ApiError(400, "INVALID_ARGUMENT", "场景分类无效");
    out["category"] = category;
    const auto summary = trim(jsonString(merged, "summary"));
    if (utf8Length(summary) < 2 || utf8Length(summary) > 60) {
      throw ApiError(400, "INVALID_ARGUMENT", "场景简介需 2-60 个字");
    }
    out["summary"] = summary;
    const auto difficulty = jsonString(merged, "difficulty");
    if (difficulty != "basic" && difficulty != "advanced") {
      throw ApiError(400, "INVALID_ARGUMENT", "难度只能是 basic 或 advanced");
    }
    out["difficulty"] = difficulty;
    json focus = json::array();
    if (merged.contains("focus") && merged["focus"].is_array()) {
      for (const auto& item : merged["focus"]) {
        if (!item.is_string()) continue;
        const auto text = trim(item.get<std::string>());
        if (text.empty() || utf8Length(text) > 20) {
          throw ApiError(400, "INVALID_ARGUMENT", "训练重点每项需 1-20 个字");
        }
        if (focus.size() >= 6) throw ApiError(400, "INVALID_ARGUMENT", "训练重点最多 6 项");
        focus.push_back(text);
      }
    }
    if (focus.empty()) throw ApiError(400, "INVALID_ARGUMENT", "请至少填写 1 项训练重点");
    out["focus"] = focus;
    // 患者画像：结构对齐 001 种子数据（age/gender/description），gender 用 unknown 表示未指定。
    const auto& raw_profile = merged.contains("patientProfile") && merged["patientProfile"].is_object()
        ? merged["patientProfile"] : json::object();
    const auto age = jsonInt(raw_profile, "age", 0);
    if (age < 1 || age > 120) throw ApiError(400, "INVALID_ARGUMENT", "患者年龄需 1-120");
    const auto gender = jsonString(raw_profile, "gender", "unknown");
    if (gender != "unknown" && gender != "男" && gender != "女") {
      throw ApiError(400, "INVALID_ARGUMENT", "患者性别只能是 男 / 女 / unknown");
    }
    const auto description = trim(jsonString(raw_profile, "description"));
    if (utf8Length(description) < 2 || utf8Length(description) > 60) {
      throw ApiError(400, "INVALID_ARGUMENT", "患者描述需 2-60 个字");
    }
    out["patientProfile"] = {{"age", age}, {"gender", gender}, {"description", description}};
    // hidden_config：开场白 / 隐藏顾虑 / 初始状态 / 行为规则。instructions 是患者剧本的
    // 核心，占位文案引导写成「若客服……应……」的条件式（本次对练改进的教训）。
    const auto& raw_hidden = merged.contains("hiddenConfig") && merged["hiddenConfig"].is_object()
        ? merged["hiddenConfig"] : json::object();
    const auto opening = trim(jsonString(raw_hidden, "opening"));
    if (utf8Length(opening) < 5 || utf8Length(opening) > 200) {
      throw ApiError(400, "INVALID_ARGUMENT", "患者开场白需 5-200 个字");
    }
    json hidden_items = stringArrayOf(raw_hidden, "hidden", 5, 60, "隐藏顾虑");
    if (hidden_items.empty()) throw ApiError(400, "INVALID_ARGUMENT", "请至少填写 1 条隐藏顾虑");
    const auto& raw_state = raw_hidden.contains("initialState") && raw_hidden["initialState"].is_object()
        ? raw_hidden["initialState"] : json::object();
    const auto emotion = jsonString(raw_state, "emotion", "平静");
    if (emotion != "平静" && emotion != "犹豫" && emotion != "焦虑" && emotion != "缓和") {
      throw ApiError(400, "INVALID_ARGUMENT", "初始情绪只能是 平静/犹豫/焦虑/缓和");
    }
    const auto emotion_level = jsonInt(raw_state, "emotionLevel", 0);
    if (emotion_level < -2 || emotion_level > 2) {
      throw ApiError(400, "INVALID_ARGUMENT", "情绪强度需在 -2 到 2 之间");
    }
    const auto trust_level = jsonInt(raw_state, "trustLevel", 50);
    if (trust_level < 0 || trust_level > 100) {
      throw ApiError(400, "INVALID_ARGUMENT", "初始信任度需 0-100");
    }
    const auto instructions = trim(jsonString(raw_hidden, "instructions"));
    if (utf8Length(instructions) < 5 || utf8Length(instructions) > 400) {
      throw ApiError(400, "INVALID_ARGUMENT",
                     "患者行为规则需 5-400 个字，建议写成「若客服……应……」的条件式");
    }
    out["hiddenConfig"] = {{"opening", opening}, {"hidden", hidden_items},
                           {"initialState", {{"emotion", emotion},
                                             {"emotionLevel", emotion_level},
                                             {"trustLevel", trust_level}}},
                           {"instructions", instructions}};
    const auto& raw_roleplay = merged.contains("roleplayConfig") && merged["roleplayConfig"].is_object()
        ? merged["roleplayConfig"] : json::object();
    out["roleplayConfig"] = {
        {"suggestedQuestions", stringArrayOf(raw_roleplay, "suggestedQuestions", 6, 60, "快捷提问")},
        {"serviceGuidance", stringArrayOf(raw_roleplay, "serviceGuidance", 6, 100, "服务要点")}};
    const auto max_rounds = jsonInt(merged, "maxRounds", 10);
    if (max_rounds < 1 || max_rounds > 10) throw ApiError(400, "INVALID_ARGUMENT", "轮数需 1-10");
    out["maxRounds"] = max_rounds;
    const auto sort_order = jsonInt(merged, "sortOrder", 0);
    if (sort_order < 1 || sort_order > 999) throw ApiError(400, "INVALID_ARGUMENT", "排序号需 1-999");
    out["sortOrder"] = sort_order;
    out["isActive"] = merged.value("isActive", true);
    return out;
  }

  // 管理目录：返回全量字段（含 hidden_config / roleplay_config），含已下架场景
  // 以便重新上架；is_template 场景（自由模拟载体）不对外管理，直接过滤。
  json supervisorScenarioCatalog() const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec(R"(
      SELECT id, name, category, summary, difficulty, focus, patient_profile,
        hidden_config, roleplay_config, max_rounds, sort_order, is_active, is_template
      FROM scenarios
      WHERE NOT is_template
      ORDER BY sort_order
    )");
    json items = json::array();
    for (const auto& row : rows) {
      items.push_back({{"id", row["id"].c_str()}, {"name", row["name"].c_str()},
                       {"category", row["category"].c_str()}, {"summary", row["summary"].c_str()},
                       {"difficulty", row["difficulty"].c_str()},
                       {"focus", parseColumn(row, "focus", true)},
                       {"patientProfile", parseColumn(row, "patient_profile")},
                       {"hiddenConfig", parseColumn(row, "hidden_config")},
                       {"roleplayConfig", parseColumn(row, "roleplay_config")},
                       {"maxRounds", row["max_rounds"].as<int>()},
                       {"sortOrder", row["sort_order"].as<int>()},
                       {"isActive", row["is_active"].as<bool>()}});
    }
    return {{"items", items}};
  }

  json createScenario(const json& payload) const {
    // 场景 id 选填：留空时自动生成（主管不需要理解英文标识的内部含义）。
    // 手工填写仍走白名单校验；生成值 sc-<毫秒时间戳> 必然满足 id 规则。
    json body = payload;
    const auto requested_id = body.contains("id") && body["id"].is_string()
        ? trim(body["id"].get<std::string>()) : std::string();
    if (requested_id.empty()) {
      body["id"] = "sc-" + std::to_string(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count());
    }
    const auto data = validateScenarioPayload(body);
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    if (!tx.exec_params("SELECT 1 FROM scenarios WHERE id = $1",
                        data["id"].get<std::string>()).empty()) {
      throw ApiError(409, "SCENARIO_EXISTS", "场景 id 已存在");
    }
    if (!tx.exec_params("SELECT 1 FROM scenarios WHERE sort_order = $1",
                        data["sortOrder"].get<int>()).empty()) {
      throw ApiError(409, "SORT_ORDER_TAKEN", "排序号已被其他场景占用");
    }
    tx.exec_params(R"(
      INSERT INTO scenarios(id, name, category, summary, difficulty, focus, patient_profile,
        hidden_config, roleplay_config, max_rounds, sort_order, is_active, is_template)
      VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7::jsonb, $8::jsonb, $9::jsonb, $10, $11, $12, FALSE)
    )", data["id"].get<std::string>(), data["name"].get<std::string>(),
        data["category"].get<std::string>(), data["summary"].get<std::string>(),
        data["difficulty"].get<std::string>(), data["focus"].dump(),
        data["patientProfile"].dump(), data["hiddenConfig"].dump(),
        data["roleplayConfig"].dump(), data["maxRounds"].get<int>(),
        data["sortOrder"].get<int>(), data["isActive"].get<bool>());
    tx.commit();
    return {{"id", data["id"].get<std::string>()}, {"created", true}};
  }

  /* 更新场景：请求体里出现的键覆盖既有值，未出现的键保留原值——
     这样「只下架」（PUT {"isActive":false}）与完整编辑走同一个接口。
     id 与 is_template 不可改（前者被 sessions 外键引用，后者由迁移管理）。 */
  json updateScenario(const std::string& scenario_id, const json& payload) const {
    if (scenario_id.empty() || scenario_id.size() > 60) {
      throw ApiError(400, "INVALID_ARGUMENT", "场景标识无效");
    }
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT id, name, category, summary, difficulty, focus, patient_profile, hidden_config,
        roleplay_config, max_rounds, sort_order, is_active
      FROM scenarios WHERE id = $1 FOR UPDATE
    )", scenario_id);
    if (rows.empty()) throw ApiError(404, "SCENARIO_NOT_FOUND", "场景不存在");
    json merged = {{"id", scenario_id},
                   {"name", rows[0]["name"].c_str()},
                   {"category", rows[0]["category"].c_str()},
                   {"summary", rows[0]["summary"].c_str()},
                   {"difficulty", rows[0]["difficulty"].c_str()},
                   {"focus", parseColumn(rows[0], "focus", true)},
                   {"patientProfile", parseColumn(rows[0], "patient_profile")},
                   {"hiddenConfig", parseColumn(rows[0], "hidden_config")},
                   {"roleplayConfig", parseColumn(rows[0], "roleplay_config")},
                   {"maxRounds", rows[0]["max_rounds"].as<int>()},
                   {"sortOrder", rows[0]["sort_order"].as<int>()},
                   {"isActive", rows[0]["is_active"].as<bool>()}};
    for (auto it = payload.begin(); it != payload.end(); ++it) {
      if (it.key() == "id" || it.key() == "isTemplate") continue;
      merged[it.key()] = it.value();
    }
    const auto data = validateScenarioPayload(merged);
    if (data["sortOrder"].get<int>() != rows[0]["sort_order"].as<int>() &&
        !tx.exec_params("SELECT 1 FROM scenarios WHERE sort_order = $1 AND id <> $2",
                        data["sortOrder"].get<int>(), scenario_id).empty()) {
      throw ApiError(409, "SORT_ORDER_TAKEN", "排序号已被其他场景占用");
    }
    tx.exec_params(R"(
      UPDATE scenarios SET name = $2, category = $3, summary = $4, difficulty = $5,
        focus = $6::jsonb, patient_profile = $7::jsonb, hidden_config = $8::jsonb,
        roleplay_config = $9::jsonb, max_rounds = $10, sort_order = $11, is_active = $12
      WHERE id = $1
    )", scenario_id, data["name"].get<std::string>(), data["category"].get<std::string>(),
        data["summary"].get<std::string>(), data["difficulty"].get<std::string>(),
        data["focus"].dump(), data["patientProfile"].dump(), data["hiddenConfig"].dump(),
        data["roleplayConfig"].dump(), data["maxRounds"].get<int>(),
        data["sortOrder"].get<int>(), data["isActive"].get<bool>());
    tx.commit();
    return {{"id", scenario_id}, {"updated", true}, {"isActive", data["isActive"].get<bool>()}};
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

  // 平静/正常这类中性词没有情绪张力，拼成「，现在有点平静」会失真，按未提供处理。
  static bool isNeutralEmotion(const std::string& emotion) {
    return emotion == "平静" || emotion == "正常" || emotion == "无" || emotion == "一般";
  }

  // 描述会被直接拼进「您好，我最近……」句式：学员若手填了结尾标点，会拼出「。，心里挺…」。
  static std::string trimTrailingPunctuation(std::string text) {
    static const std::string marks = "。！？，、；：,.!?;: \t\r\n";
    const auto end = text.find_last_not_of(marks);
    return end == std::string::npos ? std::string() : text.substr(0, end + 1);
  }

  static std::string customPatientOpening(const json& custom_profile,
                                          const std::string& fallback) {
    if (!custom_profile.is_object()) return fallback;
    std::string age, description, emotion;
    bool has_age = profileField(custom_profile, "age", age);
    bool has_description = profileField(custom_profile, "description", description);
    bool has_emotion = profileField(custom_profile, "emotion", emotion);
    if (has_description) {
      // 迁移前落库的旧画像可能带结尾标点或超长，重新开始时同样要按当前契约收敛。
      description = utf8Truncate(trimTrailingPunctuation(description),
                                 kCustomProfileDescriptionLimit);
      has_description = !description.empty();
    }
    if (has_emotion && isNeutralEmotion(emotion)) {
      emotion.clear();
      has_emotion = false;
    }
    if (!has_age && !has_description && !has_emotion) return fallback;
    // 组装一个更像真人求助的自然开场，避免把年龄/情绪/描述机械罗列成一串。
    // 优先把"描述"作为核心诉求；年龄、情绪作为背景自然带出。
    // 注意：concern 会被接在"您好，我最近"之后，兜底串绝不能再以"我"开头（会拼成"我最近我…"）。
    std::string opening;
    std::string concern = has_description ? description : "有些口腔方面的疑问";
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
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto scenario = tx.exec_params("SELECT * FROM scenarios WHERE id = $1", scenario_id);
    if (scenario.empty()) throw ApiError(404, "SCENARIO_NOT_FOUND", "训练场景不存在");
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
      const auto inserted = tx.exec_params(R"(
      INSERT INTO sessions
          (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds, patient_state, custom_patient_profile)
        VALUES ($1, $2, $3, $4, 'in_progress', 0, $5, $6::jsonb, $7::jsonb)
        ON CONFLICT (user_id, scenario_id) WHERE status = 'in_progress' DO NOTHING
        RETURNING id
      )", session_id, user_id, scenario_id, row["name"].c_str(), row["max_rounds"].as<int>(), state.dump(), custom_profile_str);
      if (inserted.empty()) throw ApiError(409, "SESSION_IN_PROGRESS", "该场景已有进行中的训练");
    tx.exec_params(R"(
      INSERT INTO messages(id, session_id, role, content, round, emotion)
      VALUES ($1, $2, 'patient', $3, 0, $4)
    )", opening_id, session_id, opening, jsonString(state, "emotion"));
    const auto saved = getSessionRow(tx, session_id, user_id);
    tx.commit();
    return {{"session", saved},
            {"messages", json::array({messageJson(opening_id, "patient", opening, 0, jsonString(state, "emotion"))})}};
  }

  json restartSession(const std::string& user_id, const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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
    tx.exec_params("INSERT INTO messages(id, session_id, role, content, round, emotion) VALUES ($1, $2, 'patient', $3, 0, $4)",
                   opening_id, new_id, opening, jsonString(state, "emotion"));
    const auto saved = getSessionRow(tx, new_id, user_id);
    tx.commit();
    return {{"session", saved},
            {"messages", json::array({messageJson(opening_id, "patient", opening, 0, jsonString(state, "emotion"))})}};
  }

  // 强制结束训练：标记为 abandoned，不生成报告，也不计入训练统计
  json abandonTrainingSession(const std::string& user_id, const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto session = getSessionRow(tx, session_id, user_id);
    const auto rows = tx.exec_params(R"(
      SELECT id, role, content, round, emotion,
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
      SELECT id, hint_number, round, content,
        to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS created_at
      FROM session_hints WHERE session_id = $1 ORDER BY hint_number
    )", session_id);
    json hints = json::array();
    int hint_used_this_round = 0;
    const auto current_round = session["currentRound"].get<int>();
    for (const auto& row : hint_rows) {
      const auto hint_round = row["round"].as<int>();
      if (hint_round == current_round) ++hint_used_this_round;
      hints.push_back({{"id", row["id"].c_str()}, {"number", row["hint_number"].as<int>()},
                       {"round", hint_round}, {"content", row["content"].c_str()},
                       {"createdAt", row["created_at"].c_str()}});
    }
    const auto hint_total_limit = 3;
    const auto hint_round_limit = 1;
    // 患者当前内部状态透出给前端：对练舱画像条用它显示信任档位。
    // patient_state NOT NULL；读法照旧用 jsonString/jsonInt 兜底，防旧数据缺键。
    const auto state_rows = tx.exec_params(
        "SELECT patient_state FROM sessions WHERE id = $1", session_id);
    const auto patient_state = state_rows.empty()
        ? json::object() : json::parse(state_rows[0]["patient_state"].c_str());
    const json patient_state_out = {
        {"emotion", jsonString(patient_state, "emotion", "平静")},
        {"emotionLevel", jsonInt(patient_state, "emotionLevel", 0)},
        {"trustLevel", jsonInt(patient_state, "trustLevel", 50)},
        {"riskTriggered", patient_state.value("riskTriggered", false)},
    };
    return {{"session", session}, {"messages", messages}, {"pendingMessage", pending_message},
            {"patientState", patient_state_out},
            {"hints", hints}, {"hintLimit", hint_total_limit},
            {"hintRemaining", std::max(0, hint_total_limit - static_cast<int>(hints.size()))},
            {"hintRound", current_round}, {"hintRoundLimit", hint_round_limit},
            {"hintUsedThisRound", hint_used_this_round},
            {"hintRemainingThisRound", std::max(0, hint_round_limit - hint_used_this_round)}};
  }

  /* 提示落库：总 3 条、每轮 1 条，两个上限都在本事务内裁定。
     round 由调用方给出（= 会话当前轮次），不在这里重新读——提示要针对的那一轮
     必须在模型看到它时就被固定下来，中途玩家发新消息不该让提示漂到下一轮。 */
  json requestTrainingHint(const std::string& user_id, const std::string& session_id, int round,
                           const std::string& content, int round_limit, int total_limit) const {
    if (session_id.empty() || session_id.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "训练会话标识无效");
    }
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto session_rows = tx.exec_params(R"(
      SELECT s.status, s.current_round FROM sessions s
      WHERE s.id = $1 AND s.user_id = $2 FOR UPDATE
    )", session_id, user_id);
    if (session_rows.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    if (std::string(session_rows[0]["status"].c_str()) != "in_progress") {
      throw ApiError(409, "SESSION_FINISHED", "已结束的训练不能继续获取提示");
    }
    const auto used_rows = tx.exec_params(
        "SELECT COUNT(*) AS total, COUNT(*) FILTER (WHERE round = $2) AS in_round"
        " FROM session_hints WHERE session_id = $1", session_id, round);
    const auto used_total = used_rows[0]["total"].as<int>();
    const auto used_this_round = used_rows[0]["in_round"].as<int>();
    // 先判「本轮」：同时触顶时更该告诉学员「这一轮已经用过了」，而不是「三张牌打完了」。
    if (used_this_round > 0) {
      throw ApiError(409, "HINT_ROUND_LIMIT_REACHED", "本轮已经获取过提示，回复患者后可在下一轮继续获取");
    }
    if (used_total >= total_limit) {
      throw ApiError(409, "HINT_LIMIT_REACHED", "本次训练的三条提示已经用完");
    }
    const auto hint_number = used_total + 1;
    const auto inserted = tx.exec_params(R"(
      INSERT INTO session_hints(id, session_id, hint_number, round, content)
      VALUES ($1, $2, $3, $4, $5)
      RETURNING to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS created_at
    )", makeId("hint"), session_id, hint_number, round, content);
    tx.commit();
    return {{"hint", {{"number", hint_number}, {"round", round}, {"content", content},
                      {"createdAt", inserted[0]["created_at"].c_str()}}},
            {"hintLimit", total_limit}, {"hintRemaining", total_limit - hint_number},
            {"hintRound", round}, {"hintRoundLimit", round_limit},
            {"hintUsedThisRound", 1}, {"hintRemainingThisRound", 0}};
  }

  json getSessionInternal(const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    return {{"session", getSessionRow(tx, session_id, "")}};
  }

  json listSessions(const std::string& user_id, const std::string& status,
                    const std::string& scenario_id, int limit) const {
    const std::vector<std::string> allowed = {"all", "in_progress", "completed", "abandoned"};
    if (std::find(allowed.begin(), allowed.end(), status) == allowed.end()) {
      throw ApiError(400, "INVALID_ARGUMENT", "status 参数无效");
    }
    limit = clampInt(limit, 1, 50);
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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
                                                row["reply_content"].c_str(), row["input_round"].as<int>(),
                                                row["reply_emotion"].is_null() ? "" : std::string(row["reply_emotion"].c_str()));
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
      auto connection = database_pool_->acquire();
      pqxx::work tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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
        "INSERT INTO messages(id, session_id, role, content, round, emotion) VALUES ($1, $2, 'patient', $3, $4, $5)",
        message_id, session_id, reply, round, jsonString(state, "emotion"));
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
    return {{"patientMessage", messageJson(message_id, "patient", reply, round, jsonString(state, "emotion"))},
            {"session", saved}, {"shouldFinish", should_finish}};
  }

  json finish(const std::string& user_id, const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT status, current_round, evaluation_status
      FROM sessions WHERE id = $1 AND user_id = $2 FOR UPDATE
    )", session_id, user_id);
    if (rows.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    const auto status = std::string(rows[0]["status"].c_str());
    if (status == "abandoned") throw ApiError(409, "SESSION_ABANDONED", "已放弃的训练不能结束或恢复");
    if (status == "completed") {
      repairEvaluationState(tx, session_id, status, rows[0]["evaluation_status"].c_str());
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
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto rows = tx.exec_params(
        "SELECT status, evaluation_status FROM sessions WHERE id = $1 AND user_id = $2 FOR UPDATE",
        session_id, user_id);
    if (rows.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    if (std::string(rows[0]["status"].c_str()) != "completed" ||
        std::string(rows[0]["evaluation_status"].c_str()) != "failed") {
      throw ApiError(409, "EVALUATION_NOT_RETRYABLE", "当前评分不可重试");
    }
    tx.exec_params(R"(
      UPDATE sessions SET evaluation_status = 'generating', total_score = NULL, updated_at = NOW()
      WHERE id = $1
    )", session_id);
    tx.exec_params(R"(
      INSERT INTO evaluations(session_id, status, report, error_type, updated_at)
      VALUES ($1, 'generating', NULL, NULL, NOW())
      ON CONFLICT (session_id) DO UPDATE SET status = 'generating', report = NULL,
        error_type = NULL, updated_at = NOW()
    )", session_id);
    enqueueAiJob(tx, "evaluation", session_id, true);
    tx.commit();
  }

  json getEvaluation(const std::string& user_id, const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto session = tx.exec_params(
        "SELECT status, evaluation_status FROM sessions WHERE id = $1 AND user_id = $2 FOR UPDATE",
        session_id, user_id);
    if (session.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "训练会话不存在");
    const auto status = repairEvaluationState(
        tx, session_id, session[0]["status"].c_str(), session[0]["evaluation_status"].c_str());
    if (status != "ready") {
      tx.commit();
      return {{"sessionId", session_id}, {"status", status}, {"retryable", status == "failed"},
              {"evaluation", nullptr}};
    }
    const auto evaluation = tx.exec_params(
        "SELECT report FROM evaluations WHERE session_id = $1 AND status = 'ready'", session_id);
    if (evaluation.empty() || evaluation[0]["report"].is_null()) {
      tx.commit();
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
    const auto result = json{{"sessionId", session_id}, {"status", "ready"}, {"retryable", false},
                             {"evaluation", report}};
    tx.commit();
    return result;
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
    report["promptVersion"] = "score-prompt-v3";
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    if (!lockAiJobTarget(tx, "evaluation", job.target_id)) {
      throw ApiError(409, "JOB_LEASE_LOST", "AI 任务目标已不存在");
    }
    const auto owned = tx.exec_params(R"(
      SELECT 1 FROM ai_jobs WHERE id = $1 AND status = 'running' AND target_id = $2
        AND generation = $3 AND attempts = $4 AND lease_until > NOW() FOR UPDATE
    )", job.id, job.target_id, job.generation, job.attempt);
    if (owned.empty()) throw ApiError(409, "JOB_LEASE_LOST", "评分任务租约已失效");
    tx.exec_params(R"(
      INSERT INTO evaluations
        (session_id, status, report, model_version, prompt_version, error_type, generated_at, updated_at)
      VALUES ($1, 'ready', $2::jsonb, $3, 'score-prompt-v3', NULL, NOW(), NOW())
      ON CONFLICT (session_id) DO UPDATE SET status = 'ready', report = EXCLUDED.report,
        model_version = EXCLUDED.model_version, prompt_version = EXCLUDED.prompt_version,
        error_type = NULL, generated_at = NOW(), updated_at = NOW()
    )", job.target_id, report.dump(), model_version);
    tx.exec_params(R"(
      UPDATE sessions SET evaluation_status = 'ready', total_score = $2, updated_at = NOW() WHERE id = $1
    )", job.target_id, report["totalScore"].get<int>());
    completeJob(tx, job);
    tx.commit();
  }

  json dashboard(const std::string& user_id, bool institution_aggregate) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const std::string filter = institution_aggregate ? "" : " WHERE user_id = " + tx.quote(user_id);
    const auto totals = tx.exec(R"(
      SELECT COUNT(*) FILTER (WHERE status <> 'abandoned') AS total_sessions,
        COUNT(*) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready') AS completed_sessions,
        COUNT(*) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready'
          AND total_score IS NOT NULL) AS scored_sessions,
        AVG(total_score) FILTER (WHERE status = 'completed' AND evaluation_status = 'ready'
          AND total_score IS NOT NULL) AS average_score
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
    json dimension_counts = json::object();
    for (const auto& key : keys) {
      dimensions[key] = 0.0;
      dimension_counts[key] = 0;
    }
    for (const auto& row : reports) accumulateDimensionScores(
        dimensions, dimension_counts, json::parse(row["report"].c_str()), keys);
    dimensions = dimensionAverages(dimensions, dimension_counts, keys);
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
    const auto completed = totals["completed_sessions"].as<int>();
    const auto scored = totals["scored_sessions"].as<int>();
    return {{"scope", institution_aggregate ? "institution" : "personal"},
            {"totalSessions", totals["total_sessions"].as<int>()},
            {"completedSessions", completed}, {"scoredSessions", scored},
            {"unscoredSessions", completed - scored},
            {"averageScore", totals["average_score"].is_null()
                ? json(nullptr) : json(totals["average_score"].as<double>())},
            {"scenarioStats", scenario_stats}, {"dimensionAverages", dimensions},
            {"recentSessions", recent}};
  }

  json listLearningPhrases(const std::string& user_id, const std::string& search,
                           const std::string& scenario_id, const std::string& scene_category,
                           bool favorites_only, int limit) const {
    if (utf8Length(search) > 120 || scenario_id.size() > 120 || scene_category.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "学习筛选参数过长");
    }
    /* 分类在 SQL 里过滤、在 LIMIT 之前生效。若改到前端按 category 字段筛，
       会变成「先截断再筛选」：训练量大的学员，50 条上限被单一分类占满后，
       其他分类会显示出少于实际的结果。 */
    if (!scene_category.empty() && !isSceneCategory(scene_category)) {
      throw ApiError(400, "INVALID_ARGUMENT", "sceneCategory 参数无效");
    }
    limit = clampInt(limit, 1, 50);
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto favorite_rows = tx.exec_params(
        "SELECT session_id, phrase_key FROM learner_phrase_favorites WHERE user_id = $1", user_id);
    std::set<std::string> favorites;
    for (const auto& row : favorite_rows) {
      favorites.insert(favoriteKey(row["session_id"].c_str(), row["phrase_key"].c_str()));
    }
    /* 两个可选筛选用固定占位符 + 空串短路，避免拼 SQL 时重排参数序号；
       空串比较必须显式 ::text，否则 PostgreSQL 无法推断 $2/$3 的类型。
       scenarios 用 JOIN：sessions.scenario_id 有外键指向 scenarios，场景行必然存在。 */
    const auto rows = tx.exec_params(R"(
      SELECT s.id, s.scenario_id, s.scenario_name, sc.category,
        to_char(s.finished_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS finished_date,
        e.report
      FROM sessions s JOIN evaluations e ON e.session_id = s.id
      JOIN scenarios sc ON sc.id = s.scenario_id
      WHERE s.user_id = $1 AND s.status = 'completed' AND e.status = 'ready'
        AND ($2::text = '' OR s.scenario_id = $2::text)
        AND ($3::text = '' OR sc.category = $3::text)
      ORDER BY s.finished_at DESC NULLS LAST LIMIT 200
    )", user_id, scenario_id, scene_category);
    json items = json::array();
    for (const auto& row : rows) {
      const auto report = storedReport(row);
      const auto category = std::string(row["category"].c_str());
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
            {"scenarioId", row["scenario_id"].c_str()}, {"scenarioName", scenario_name},
            {"category", category}, {"finishedDate", row["finished_date"].c_str()},
            {"round", jsonInt(phrase, "round", 0)}, {"patientSays", patient_says},
            {"csReply", cs_reply}, {"reason", reason}, {"favorited", favorited},
        });
      }
      if (items.size() >= static_cast<size_t>(limit)) break;
    }
    /* 分类中文名只由后端下发（sceneCategories），前端不再各写一份映射 */
    return {{"items", items}, {"total", static_cast<int>(items.size())},
            {"favoritesOnly", favorites_only}, {"sceneCategory", scene_category},
            {"sceneCategories", sceneCategoryCatalog()}};
  }

  json setLearningPhraseFavorite(const std::string& user_id, const std::string& session_id,
                                 const std::string& phrase_key, bool favorite) const {
    if (session_id.empty() || session_id.size() > 120 || phrase_key.empty() || phrase_key.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "话术标识参数无效");
    }
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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

  // 错题「复现原回合」的上下文聚合：错题详情 + 患者当时提问原话 + 学员当时发言 + 场景画像。
  // 患者提问是历史事实，直接从 messages 表取原话复现，不依赖模型重演。
  json getMistakeRetrainContext(const std::string& user_id, const std::string& session_id,
                                const std::string& mistake_key) const {
    if (session_id.empty() || session_id.size() > 120 || mistake_key.empty() || mistake_key.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "错题标识参数无效");
    }
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT s.scenario_id, s.custom_patient_profile, e.report FROM sessions s
      JOIN evaluations e ON e.session_id = s.id
      WHERE s.id = $1 AND s.user_id = $2 AND s.status = 'completed' AND e.status = 'ready'
    )", session_id, user_id);
    if (rows.empty()) throw ApiError(404, "LEARNING_MISTAKE_NOT_FOUND", "错题不存在或无权访问");
    json mistake;
    bool known_mistake = false;
    for (const auto& item : learningMistakesFromReport(storedReport(rows[0]))) {
      if (item.is_object() && jsonString(item, "mistakeKey") == mistake_key) {
        mistake = item;
        known_mistake = true;
        break;
      }
    }
    if (!known_mistake) throw ApiError(404, "LEARNING_MISTAKE_NOT_FOUND", "错题不存在或无权访问");
    const auto round = jsonInt(mistake, "round", 0);
    if (round <= 0) throw ApiError(404, "MISTAKE_ROUND_NOT_FOUND", "该错题缺少可复现的回合信息");
    const auto patient_rows = tx.exec_params(
        "SELECT content, emotion FROM messages WHERE session_id = $1 AND role = 'patient' AND round = $2",
        session_id, round);
    if (patient_rows.empty()) throw ApiError(404, "MISTAKE_ROUND_NOT_FOUND", "该错题对应的回合信息不存在");
    const auto user_rows = tx.exec_params(
        "SELECT content FROM messages WHERE session_id = $1 AND role = 'user' AND round = $2",
        session_id, round);
    const auto mastery_rows = tx.exec_params(
        "SELECT 1 FROM learner_mistake_progress WHERE user_id = $1 AND session_id = $2"
        " AND mistake_key = $3 AND mastered_at IS NOT NULL",
        user_id, session_id, mistake_key);
    const auto scenario_id = std::string(rows[0]["scenario_id"].c_str());
    json custom_profile = json::object();
    if (!rows[0]["custom_patient_profile"].is_null()) {
      custom_profile = json::parse(rows[0]["custom_patient_profile"].c_str());
    }
    tx.commit();
    const auto scenario = getScenarioInternal(scenario_id);
    return {
        {"session", {{"id", session_id}, {"scenarioId", scenario_id}, {"status", "completed"}}},
        {"mistake", {
            {"mistakeKey", mistake_key}, {"kind", jsonString(mistake, "kind")},
            {"priority", jsonString(mistake, "priority")}, {"round", round},
            {"originalQuote", jsonString(mistake, "originalQuote")},
            {"reason", jsonString(mistake, "reason")},
            {"recommendedRewrite", jsonString(mistake, "recommendedRewrite")},
            {"mastered", !mastery_rows.empty()}}},
        {"patientQuestion", patient_rows[0]["content"].c_str()},
        {"patientEmotion", patient_rows[0]["emotion"].is_null()
            ? json(nullptr) : json(std::string(patient_rows[0]["emotion"].c_str()))},
        {"originalAnswer", user_rows.empty() ? std::string() : std::string(user_rows[0]["content"].c_str())},
        {"scenario", scenario["public"]},
        {"customPatientProfile", custom_profile}};
  }

  json learningProfile(const std::string& user_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT recent.id, recent.scenario_id, recent.scenario_name, recent.total_score,
        to_char(recent.finished_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS finished_date,
        recent.report
      FROM (
        SELECT s.id, s.scenario_id, s.scenario_name, s.total_score, s.finished_at, e.report
        FROM sessions s JOIN evaluations e ON e.session_id = s.id
        WHERE s.user_id = $1 AND s.status = 'completed' AND e.status = 'ready'
        ORDER BY s.finished_at DESC NULLS LAST
        LIMIT 200
      ) recent
      ORDER BY recent.finished_at ASC NULLS LAST
    )", user_id);
    const std::vector<std::string> keys = {
        "knowledgeAccuracy", "medicalCompliance", "empathy", "needsDiscovery", "serviceEtiquette"};
    json totals = json::object();
    json dimension_counts = json::object();
    for (const auto& key : keys) {
      totals[key] = 0.0;
      dimension_counts[key] = 0;
    }
    int total_score = 0;
    int completed_count = 0;
    int scored_count = 0;
    std::vector<json> all_trend;
    std::set<std::string> mistake_keys;
    for (const auto& row : rows) {
      const auto report = storedReport(row);
      if (!report.is_object()) continue;
      ++completed_count;
      accumulateDimensionScores(totals, dimension_counts, report, keys);
      if (!row["total_score"].is_null()) {
        ++scored_count;
        total_score += row["total_score"].as<int>();
        all_trend.push_back({
            {"sessionId", row["id"].c_str()}, {"scenarioId", row["scenario_id"].c_str()},
            {"scenarioName", row["scenario_name"].c_str()}, {"date", row["finished_date"].c_str()},
            {"totalScore", row["total_score"].as<int>()},
            {"scores", report.value("dimensionScores", json::object())},
        });
      }
      for (const auto& mistake : learningMistakesFromReport(report)) {
        if (mistake.is_object() && !jsonString(mistake, "mistakeKey").empty()) {
          mistake_keys.insert(masteryKey(row["id"].c_str(), jsonString(mistake, "mistakeKey")));
        }
      }
    }
    const auto averages = dimensionAverages(totals, dimension_counts, keys);
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
    std::vector<std::string> ordered_keys;
    for (const auto& key : keys) {
      if (averages.contains(key) && averages[key].is_number()) ordered_keys.push_back(key);
    }
    std::sort(ordered_keys.begin(), ordered_keys.end(), [&](const auto& left, const auto& right) {
      return averages[left].get<double>() < averages[right].get<double>();
    });
    json weaknesses = json::array();
    if (!ordered_keys.empty()) {
      for (size_t index = 0; index < ordered_keys.size() && index < 2; ++index) {
        const auto& key = ordered_keys[index];
        const auto& copy = dimension_copy.at(key);
        weaknesses.push_back({{"key", key}, {"name", copy.first}, {"score", averages[key]}, {"suggestion", copy.second}});
      }
    }
    json trend = json::array();
    const size_t first = all_trend.size() > 12 ? all_trend.size() - 12 : 0;
    for (size_t index = first; index < all_trend.size(); ++index) trend.push_back(all_trend[index]);
    const json score_delta = all_trend.size() < 2 ? json(nullptr)
        : json(all_trend.back()["totalScore"].get<int>() -
               all_trend.front()["totalScore"].get<int>());
    return {{"overall", {{"totalCompleted", completed_count}, {"scoredCount", scored_count},
                            {"unscoredCount", completed_count - scored_count},
                            {"averageScore", scored_count == 0 ? json(nullptr)
                                : json(std::round(static_cast<double>(total_score) /
                                    scored_count * 10.0) / 10.0)},
                            {"scoreDelta", score_delta}}},
            {"dimensionAverages", averages}, {"trend", trend}, {"weaknesses", weaknesses},
            {"mistakes", {{"total", static_cast<int>(mistake_keys.size())}, {"mastered", mastered_count}}}};
  }

  json learningMine(const std::string& user_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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

  /* 主管端所有聚合都收敛到「我的团队」：supervisorTeamFilter 里的 $1 恒为当前
     登录主管 id，由服务端 authorize() 提供，从不接受客户端传参。
     统计口径与学员端 dashboard 一致：只有 total_score 非空的已完成报告才计入
     均分 / 及格率，分母用 scored 而不是 completed，避免未评分会话把均分摊薄。 */
  json supervisorDashboard(const std::string& supervisor_id, const std::string& time_range) const {
    const auto time_filter = supervisorTimeFilter(time_range, "s.finished_at");
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto student_count = tx.exec_params(R"(
      SELECT COUNT(*) AS count FROM supervisor_team_members tm
      JOIN users u ON u.id = tm.learner_id AND u.role = 'learner' AND u.status = 'active'
      WHERE tm.supervisor_id = $1
    )", supervisor_id)[0]["count"].as<int>();
    const auto totals = tx.exec_params(R"(
      SELECT COUNT(*) FILTER (WHERE s.status <> 'abandoned') AS total_sessions,
        COUNT(*) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready') AS completed_sessions,
        COUNT(*) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready'
          AND s.total_score IS NOT NULL) AS scored_sessions,
        COUNT(*) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready' AND s.total_score >= 60) AS passed_sessions,
        AVG(s.total_score) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready'
          AND s.total_score IS NOT NULL) AS average_score
      FROM sessions s WHERE TRUE
    )" + time_filter + supervisorTeamFilter("s.user_id"), supervisor_id)[0];
    const auto scenario_rows = tx.exec_params(R"(
      SELECT sc.id, sc.name, COUNT(s.id) AS completed_count,
        COUNT(s.id) FILTER (WHERE s.total_score IS NOT NULL) AS scored_count,
        AVG(s.total_score) AS average_score,
        ROUND(100.0 * COUNT(s.id) FILTER (WHERE s.total_score >= 60) /
          NULLIF(COUNT(s.id) FILTER (WHERE s.total_score IS NOT NULL), 0), 1) AS pass_rate
      FROM scenarios sc
      LEFT JOIN sessions s ON s.scenario_id = sc.id
        AND s.status = 'completed' AND s.evaluation_status = 'ready'
    )" + supervisorTimeFilter(time_range, "s.finished_at") + supervisorTeamFilter("s.user_id") + R"(
      GROUP BY sc.id, sc.name, sc.sort_order ORDER BY sc.sort_order
    )", supervisor_id);
    const auto report_rows = tx.exec_params(R"(
      SELECT e.report FROM evaluations e JOIN sessions s ON s.id = e.session_id
      WHERE s.status = 'completed' AND s.evaluation_status = 'ready' AND e.status = 'ready'
    )" + time_filter + supervisorTeamFilter("s.user_id"), supervisor_id);
    const std::vector<std::string> keys = {
        "knowledgeAccuracy", "medicalCompliance", "empathy", "needsDiscovery", "serviceEtiquette"};
    json dimensions = json::object();
    json dimension_counts = json::object();
    for (const auto& key : keys) {
      dimensions[key] = 0.0;
      dimension_counts[key] = 0;
    }
    for (const auto& row : report_rows) {
      const auto report = storedReport(row);
      if (report.is_object()) {
        accumulateDimensionScores(dimensions, dimension_counts, report, keys);
      }
    }
    dimensions = dimensionAverages(dimensions, dimension_counts, keys);
    json scenario_stats = json::array();
    for (const auto& row : scenario_rows) {
      scenario_stats.push_back({{"scenarioId", row["id"].c_str()}, {"scenarioName", row["name"].c_str()},
                                {"total", row["completed_count"].as<int>()},
                                {"scoredCount", row["scored_count"].as<int>()},
                                {"unscoredCount", row["completed_count"].as<int>() -
                                    row["scored_count"].as<int>()},
                                {"averageScore", row["average_score"].is_null()
                                    ? json(nullptr) : json(row["average_score"].as<double>())},
                                {"passRate", row["pass_rate"].is_null()
                                    ? json(nullptr) : json(row["pass_rate"].as<double>())}});
    }
    const auto trend_rows = tx.exec_params(R"(
      SELECT to_char(s.finished_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS date,
        COUNT(*) AS count, ROUND(AVG(s.total_score)::numeric, 1) AS average_score
      FROM sessions s
      WHERE s.status = 'completed' AND s.evaluation_status = 'ready'
        AND s.total_score IS NOT NULL
    )" + time_filter + supervisorTeamFilter("s.user_id") + R"(
      GROUP BY 1 ORDER BY date DESC LIMIT 12
    )", supervisor_id);
    json trend = json::array();
    for (auto iterator = trend_rows.rbegin(); iterator != trend_rows.rend(); ++iterator) {
      trend.push_back({{"date", (*iterator)["date"].c_str()}, {"count", (*iterator)["count"].as<int>()},
                       {"averageScore", (*iterator)["average_score"].as<double>()}});
    }
    const auto completed = totals["completed_sessions"].as<int>();
    const auto scored = totals["scored_sessions"].as<int>();
    const auto passed = totals["passed_sessions"].as<int>();
    return {{"range", time_range}, {"studentCount", student_count},
            {"totalSessions", totals["total_sessions"].as<int>()}, {"completedSessions", completed},
            {"scoredSessions", scored}, {"unscoredSessions", completed - scored},
            {"averageScore", totals["average_score"].is_null()
                ? json(nullptr) : json(totals["average_score"].as<double>())},
            {"passRate", scored == 0 ? json(nullptr)
                : json(std::round(static_cast<double>(passed) / scored * 1000.0) / 10.0)},
            {"dimensionAverages", dimensions}, {"scenarioStats", scenario_stats}, {"trend", trend}};
  }

  json listSupervisorMembers(const std::string& supervisor_id, int limit) const {
    limit = clampInt(limit, 1, 100);
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT u.id, COALESCE(NULLIF(u.display_name, ''), '未命名学员') AS display_name,
        to_char(u.created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS joined_at,
        COUNT(s.id) FILTER (WHERE s.status <> 'abandoned') AS total_sessions,
        COUNT(s.id) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready') AS completed_sessions,
        COUNT(s.id) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready' AND s.total_score >= 60) AS passed_sessions,
        AVG(s.total_score) FILTER (WHERE s.status = 'completed' AND s.evaluation_status = 'ready') AS average_score,
        to_char(MAX(s.updated_at) AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS last_training_date
      FROM users u LEFT JOIN sessions s ON s.user_id = u.id
      WHERE u.role = 'learner' AND u.status = 'active'
        AND EXISTS (SELECT 1 FROM supervisor_team_members tm
                    WHERE tm.learner_id = u.id AND tm.supervisor_id = $1)
      GROUP BY u.id, u.display_name, u.created_at
      ORDER BY lower(COALESCE(NULLIF(u.display_name, ''), u.id)), u.created_at
      LIMIT )" + std::to_string(limit), supervisor_id);
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
    /* totalTeamMembers 是本团队成员总数；totalLearners 是可被拉进团队的在职学员
       总数（含已在别人团队的人）。limit 只有 100，调用方（发布计划选发布对象）
       需要据此判断列表是否被截断。 */
    const auto count_rows = tx.exec_params(R"(
      SELECT COUNT(*) FILTER (WHERE tm.learner_id IS NOT NULL) AS team_members,
             COUNT(*) AS total_learners
      FROM users u
      LEFT JOIN supervisor_team_members tm ON tm.learner_id = u.id AND tm.supervisor_id = $1
      WHERE u.role = 'learner' AND u.status = 'active'
    )", supervisor_id)[0];
    return {{"members", members}, {"total", static_cast<int>(members.size())},
            {"totalLearners", count_rows["total_learners"].as<int>()},
            {"totalTeamMembers", count_rows["team_members"].as<int>()}};
  }

  // ── 我的团队：主管 ↔ 学员的归属关系 ────────────────────────────────────
  // 一名学员最多隶属一个主管（supervisor_team_members.learner_id 是主键）。
  // 移出团队只解除归属，账号与全部训练记录一律保留。

  json listTeamCandidates(const std::string& supervisor_id, int limit) const {
    limit = clampInt(limit, 1, 100);
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    /* 候选人 = 在职学员中「还没有归属」的人。已在别人团队里的学员不会出现，
       因为一人一主管，拉走他会破坏对方团队的数据口径。 */
    const auto rows = tx.exec_params(R"(
      SELECT u.id, COALESCE(NULLIF(u.display_name, ''), '未命名学员') AS display_name,
        to_char(u.created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS joined_at
      FROM users u
      WHERE u.role = 'learner' AND u.status = 'active'
        AND NOT EXISTS (SELECT 1 FROM supervisor_team_members tm WHERE tm.learner_id = u.id)
      ORDER BY lower(COALESCE(NULLIF(u.display_name, ''), u.id)), u.created_at
      LIMIT )" + std::to_string(limit));
    json candidates = json::array();
    for (const auto& row : rows) {
      candidates.push_back({{"id", row["id"].c_str()}, {"displayName", row["display_name"].c_str()},
                            {"joinedAt", row["joined_at"].c_str()}});
    }
    const auto counts = tx.exec_params(R"(
      SELECT COUNT(*) FILTER (WHERE tm.learner_id IS NOT NULL) AS team_members,
             COUNT(*) FILTER (WHERE tm.learner_id IS NULL) AS candidates
      FROM users u
      LEFT JOIN supervisor_team_members tm ON tm.learner_id = u.id
      WHERE u.role = 'learner' AND u.status = 'active'
        AND (tm.learner_id IS NULL OR tm.supervisor_id = $1)
    )", supervisor_id)[0];
    return {{"candidates", candidates}, {"total", static_cast<int>(candidates.size())},
            {"totalTeamMembers", counts["team_members"].as<int>()},
            {"totalCandidates", counts["candidates"].as<int>()}};
  }

  json addTeamMembers(const std::string& supervisor_id,
                      const std::vector<std::string>& learner_ids) const {
    if (learner_ids.empty()) throw ApiError(400, "INVALID_ARGUMENT", "请至少选择一名学员");
    if (learner_ids.size() > 500) throw ApiError(400, "INVALID_ARGUMENT", "单次最多添加 500 名学员");
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    /* 只插入「在职且当前无归属」的学员：ON CONFLICT 拦住已有归属的人（既包括
       别人团队里的成员，也包括本团队里重复提交的人），所以不会覆盖任何既有归属。
       注意这不是「幂等接口」：一名都没插进去时会返回 400，而不是 addedCount=0。 */
    const auto inserted = tx.exec_params(R"(
      INSERT INTO supervisor_team_members(learner_id, supervisor_id)
      SELECT u.id, $2 FROM users u
      WHERE u.role = 'learner' AND u.status = 'active'
        AND u.id IN (SELECT jsonb_array_elements_text($1::jsonb))
        AND NOT EXISTS (SELECT 1 FROM supervisor_team_members tm WHERE tm.learner_id = u.id)
      ON CONFLICT (learner_id) DO NOTHING
      RETURNING learner_id
    )", json(learner_ids).dump(), supervisor_id);
    /* 用 RETURNING 结果的行数而不是 affected_rows()：带 RETURNING 的语句在
       libpqxx 里 affected_rows() 不可靠（可能是 0），本项目统一按返回集判空。 */
    const auto added_count = static_cast<int>(inserted.size());
    if (added_count == 0) {
      /* 一名都没加进去 → 明确失败，而不是回一个 addedCount=0 的成功。
         前端拿到 0 会显示「已添加 0 人」，看起来像成功但其实什么都没发生；
         这里的真实原因只可能是「已停用」或「已归属其他主管」，直接说明更好排查。
         与 createTrainingPlan 的「所选学员均不可用」保持同一种口径。 */
      throw ApiError(400, "TEAM_MEMBER_UNAVAILABLE",
                     "所选学员均不可加入（可能已停用或已属于其他主管）");
    }
    tx.commit();
    return {{"addedCount", added_count},
            {"skippedCount", static_cast<int>(learner_ids.size()) - added_count}};
  }

  json removeTeamMember(const std::string& supervisor_id, const std::string& learner_id) const {
    if (learner_id.empty() || learner_id.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "成员标识无效");
    }
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    /* 归属行与未到期计划的指派行必须同事务删除，否则会出现「人已移出团队，
       却还挂在计划名单里」的不一致。已到期计划保留指派行以便回溯审计。 */
    const auto removed = tx.exec_params(R"(
      DELETE FROM supervisor_team_members
      WHERE learner_id = $1 AND supervisor_id = $2
      RETURNING learner_id
    )", learner_id, supervisor_id);
    if (removed.empty()) {
      /* 返回集为空说明该学员不属于当前主管（或根本不存在），不能移出别人的成员。 */
      throw ApiError(404, "TEAM_MEMBER_NOT_FOUND", "该学员不在你的团队中");
    }
    const auto cleared = tx.exec_params(R"(
      DELETE FROM training_assignments a
      USING training_plans p
      WHERE a.plan_id = p.id AND a.learner_id = $1 AND p.created_by = $2
        AND p.due_at > NOW()
    )", learner_id, supervisor_id);
    tx.commit();
    return {{"removed", true}, {"learnerId", learner_id},
            {"clearedAssignments", static_cast<int>(cleared.affected_rows())}};
  }

  json supervisorMemberDetail(const std::string& supervisor_id, const std::string& member_id) const {
    if (member_id.empty() || member_id.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "成员标识无效");
    }
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    /* 非本团队成员与「成员不存在」返回同一个 404，避免通过错误码差异探测
       其他主管的成员是否存在。 */
    const auto user_rows = tx.exec_params(R"(
      SELECT id, COALESCE(NULLIF(display_name, ''), '未命名学员') AS display_name,
        to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS joined_at
      FROM users WHERE id = $1 AND role = 'learner' AND status = 'active'
        AND EXISTS (SELECT 1 FROM supervisor_team_members tm
                    WHERE tm.learner_id = users.id AND tm.supervisor_id = $2)
    )", member_id, supervisor_id);
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
    json dimension_counts = json::object();
    for (const auto& key : keys) {
      totals[key] = 0.0;
      dimension_counts[key] = 0;
    }
    json all_trend = json::array();
    for (const auto& row : report_rows) {
      const auto report = storedReport(row);
      if (!report.is_object()) continue;
      accumulateDimensionScores(totals, dimension_counts, report, keys);
      all_trend.push_back({{"sessionId", row["id"].c_str()}, {"scenarioId", row["scenario_id"].c_str()},
                           {"scenarioName", row["scenario_name"].c_str()}, {"date", row["finished_date"].c_str()},
                           {"totalScore", row["total_score"].as<int>()},
                           {"scores", report.value("dimensionScores", json::object())}});
    }
    const auto averages = dimensionAverages(totals, dimension_counts, keys);
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
    // 抽查入口需要的最近会话（含进行中）：与 recentSessions（仅已完成已评分）分开，
    // 避免动到 member-detail 已消费的字段结构。
    const auto inspect_rows = tx.exec_params(R"(
      SELECT s.id, s.scenario_name, s.status, s.current_round, s.max_rounds, s.total_score,
        to_char(s.updated_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI') AS updated_text
      FROM sessions s
      WHERE s.user_id = $1 AND s.status <> 'abandoned'
      ORDER BY s.updated_at DESC LIMIT 10
    )", member_id);
    json inspect_sessions = json::array();
    for (const auto& row : inspect_rows) {
      inspect_sessions.push_back({{"id", row["id"].c_str()},
                                  {"scenarioName", row["scenario_name"].c_str()},
                                  {"status", row["status"].c_str()},
                                  {"currentRound", row["current_round"].as<int>()},
                                  {"maxRounds", row["max_rounds"].as<int>()},
                                  {"totalScore", row["total_score"].is_null()
                                      ? json(nullptr) : json(row["total_score"].as<int>())},
                                  {"updatedAtText", row["updated_text"].c_str()}});
    }
    return {{"member", {{"id", user_rows[0]["id"].c_str()},
                           {"displayName", user_rows[0]["display_name"].c_str()},
                           {"joinedAt", user_rows[0]["joined_at"].c_str()}}},
            {"totalSessions", stats["total_sessions"].as<int>()}, {"completedSessions", reported_completed},
            {"averageScore", stats["average_score"].is_null() ? 0.0 : stats["average_score"].as<double>()},
            {"passRate", reported_completed == 0 ? 0.0
                : std::round(static_cast<double>(passed) / reported_completed * 1000.0) / 10.0},
            {"dimensionAverages", averages}, {"weaknesses", dimensionWeaknesses(averages)},
            {"trend", trend}, {"recentSessions", recent},
            {"inspectSessions", inspect_sessions}};
  }

  // ── 训练抽查（主管只读） ─────────────────────────────────────────────
  // 返回完整对话 + 评分 + 轻规则敷衍分析（不引入模型，SQL/C++ 聚合即可）。
  // 阈值刻意保守：轮数不足 3 不判定，避免一两轮的正常短回复被误标。
  json supervisorMemberSession(const std::string& supervisor_id, const std::string& member_id,
                               const std::string& session_id) const {
    if (member_id.empty() || member_id.size() > 120 ||
        session_id.empty() || session_id.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "成员或会话标识无效");
    }
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    // 双重 404 防探测：非本团队成员 / 非该成员的会话，与「不存在」同一错误。
    const auto member_rows = tx.exec_params(R"(
      SELECT 1 FROM users u
      WHERE u.id = $1 AND u.role = 'learner' AND u.status = 'active'
        AND EXISTS (SELECT 1 FROM supervisor_team_members tm
                    WHERE tm.learner_id = u.id AND tm.supervisor_id = $2)
    )", member_id, supervisor_id);
    if (member_rows.empty()) throw ApiError(404, "MEMBER_NOT_FOUND", "成员不存在");
    const auto session = getSessionRow(tx, session_id, member_id);
    const auto rows = tx.exec_params(R"(
      SELECT id, role, content, round, emotion,
        to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS created_at
      FROM messages WHERE session_id = $1 ORDER BY round, created_at
    )", session_id);
    json messages = json::array();
    for (const auto& row : rows) messages.push_back(messageJson(row));
    // 疑似敷衍分析：只看学员消息（role='user'）的长度、重复率、回复间隔。
    const auto stat_rows = tx.exec_params(R"(
      WITH user_msgs AS (
        SELECT content, created_at,
          LAG(created_at) OVER (ORDER BY created_at) AS prev_created_at
        FROM messages
        WHERE session_id = $1 AND role = 'user'
      )
      SELECT COUNT(*) AS rounds,
        COALESCE(ROUND(AVG(char_length(content))::numeric, 1), 0) AS avg_length,
        COUNT(DISTINCT content) AS distinct_count,
        COALESCE(AVG(EXTRACT(EPOCH FROM (created_at - prev_created_at))), 0) AS avg_gap_seconds
      FROM user_msgs
    )", session_id);
    const auto& stats = stat_rows[0];
    const auto rounds = stats["rounds"].as<int>();
    const auto avg_length = stats["avg_length"].as<double>();
    const auto distinct_count = stats["distinct_count"].as<int>();
    const auto avg_gap = stats["avg_gap_seconds"].as<double>();
    const auto repeat_ratio = rounds == 0 ? 0.0
        : std::round((1.0 - static_cast<double>(distinct_count) / rounds) * 100.0) / 100.0;
    json flags = json::array();
    if (rounds >= 3 && avg_length < 8.0) flags.push_back("平均每条回复不足 8 个字");
    if (rounds >= 3 && repeat_ratio >= 0.34) flags.push_back("重复内容占比过高");
    if (rounds >= 3 && avg_gap > 0 && avg_gap < 3.0) flags.push_back("回复间隔过短（秒级连发）");
    json report = json::object();
    const auto report_rows = tx.exec_params(
        "SELECT status, report FROM evaluations WHERE session_id = $1", session_id);
    if (!report_rows.empty() && !report_rows[0]["report"].is_null()) {
      report = json::parse(report_rows[0]["report"].c_str(), nullptr, false);
      if (!report.is_object()) report = json::object();
      report["evaluationStatus"] = report_rows[0]["status"].c_str();
    }
    return {{"session", session}, {"messages", messages}, {"report", report},
            {"suspicion", {{"suspected", !flags.empty()}, {"flags", flags},
                           {"rounds", rounds}, {"avgLength", avg_length},
                           {"repeatRatio", repeat_ratio},
                           {"avgGapSeconds", rounds >= 2 ? json(avg_gap) : json(nullptr)}}}};
  }

  // ── 培训运营（文档 3.2 第三模块） ─────────────────────────────────────
  // 进度刻意不落库：全部在读时按 [created_at, due_at] 窗口实时聚合，因此
  // 不存在计数漂移。指派行是发布那一刻的快照，之后新加入团队的学员不会自动
  // 进入既有计划；移出团队时同步清掉未到期计划的指派行（见 removeTeamMember）。

  json createTrainingPlan(const json& payload, const std::string& supervisor_id) const {
    const auto title = trim(jsonString(payload, "title"));
    if (title.empty() || utf8Truncate(title, 100).size() != title.size()) {
      throw ApiError(400, "INVALID_ARGUMENT", "计划标题需为 1-100 个字符");
    }
    const auto period = jsonString(payload, "period");
    if (period != "week" && period != "month") {
      throw ApiError(400, "INVALID_ARGUMENT", "计划周期只能是 week 或 month");
    }
    const auto due_at = trim(jsonString(payload, "dueAt"));
    if (due_at.empty()) throw ApiError(400, "INVALID_ARGUMENT", "截止时间不能为空");
    const auto required_count = clampInt(jsonInt(payload, "requiredCount", 1), 1, 20);
    const auto required_pass_rate = clampInt(jsonInt(payload, "requiredPassRate", 60), 0, 100);
    const auto description = utf8Truncate(trim(jsonString(payload, "description")), 500);
    json scenario_ids = json::array();
    if (payload.contains("scenarioIds") && payload["scenarioIds"].is_array()) {
      for (const auto& item : payload["scenarioIds"]) {
        if (item.is_string() && !item.get<std::string>().empty()) scenario_ids.push_back(item);
      }
    }
    /* 发布对象：targetUserIds 为空数组 = 全团队成员；非空 = 只指派给这些学员
       （且必须仍在本人团队内）。去重后逐个校验长度，非法、已停用或不属于本团队
       的 id 会被过滤并回报 skippedCount。 */
    std::vector<std::string> target_user_ids;
    if (payload.contains("targetUserIds") && payload["targetUserIds"].is_array()) {
      for (const auto& item : payload["targetUserIds"]) {
        if (!item.is_string()) continue;
        const auto candidate = trim(item.get<std::string>());
        if (candidate.empty()) continue;
        if (candidate.size() > 120) throw ApiError(400, "INVALID_ARGUMENT", "学员标识无效");
        bool duplicated = false;
        for (const auto& existing : target_user_ids) {
          if (existing == candidate) { duplicated = true; break; }
        }
        if (!duplicated) target_user_ids.push_back(candidate);
      }
      if (target_user_ids.size() > 500) throw ApiError(400, "INVALID_ARGUMENT", "单次最多指派 500 名学员");
    }
    const bool targeted = !target_user_ids.empty();
    const auto plan_id = makeId("plan");
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    /* 空团队 + 空 targetUserIds 会「发布成功但零指派」，属于静默失效，直接拒绝。 */
    if (!targeted) {
      const auto team_size = tx.exec_params(R"(
        SELECT COUNT(*) AS count FROM supervisor_team_members tm
        JOIN users u ON u.id = tm.learner_id AND u.role = 'learner' AND u.status = 'active'
        WHERE tm.supervisor_id = $1
      )", supervisor_id)[0]["count"].as<int>();
      if (team_size == 0) {
        throw ApiError(400, "TEAM_EMPTY", "团队暂无成员，请先到「我的团队」添加成员");
      }
    }
    tx.exec_params(R"(
      INSERT INTO training_plans
        (id, title, period, scenario_ids, required_count, required_pass_rate, description, due_at, created_by)
      VALUES ($1, $2, $3, $4::jsonb, $5, $6, $7, $8::timestamptz, $9)
    )", plan_id, title, period, scenario_ids.dump(), required_count, required_pass_rate,
        description, due_at, supervisor_id);
    /* 指定学员时先确认至少有一人可用，否则直接拒绝——
       避免「计划发布成功但零指派」这种静默失效（前端会提示覆盖 0 人）。
       可用 = 在职且当前属于本主管团队。 */
    int skipped_count = 0;
    if (targeted) {
      const auto valid_rows = tx.exec_params(R"(
        SELECT u.id FROM users u
        WHERE u.role = 'learner' AND u.status = 'active'
          AND u.id IN (SELECT jsonb_array_elements_text($1::jsonb))
          AND EXISTS (SELECT 1 FROM supervisor_team_members tm
                      WHERE tm.learner_id = u.id AND tm.supervisor_id = $2)
      )", json(target_user_ids).dump(), supervisor_id);
      if (valid_rows.empty()) {
        throw ApiError(400, "INVALID_ARGUMENT", "所选学员均不在你的团队中，请重新选择发布对象");
      }
      skipped_count = static_cast<int>(target_user_ids.size() - valid_rows.size());
    }
    /* 空数组短路为全团队成员，与场景过滤保持同一种写法 */
    const auto assigned = tx.exec_params(R"(
      INSERT INTO training_assignments(plan_id, learner_id)
      SELECT $1, tm.learner_id FROM supervisor_team_members tm
      JOIN users u ON u.id = tm.learner_id AND u.role = 'learner' AND u.status = 'active'
      WHERE tm.supervisor_id = $2
        AND ($3::jsonb = '[]'::jsonb OR tm.learner_id IN (SELECT jsonb_array_elements_text($3::jsonb)))
      ON CONFLICT (plan_id, learner_id) DO NOTHING
    )", plan_id, supervisor_id, json(target_user_ids).dump());
    tx.commit();
    return {{"plan", {{"id", plan_id}, {"title", title}, {"period", period}, {"dueAt", due_at},
                      {"requiredCount", required_count}, {"requiredPassRate", required_pass_rate},
                      {"scenarioIds", scenario_ids}, {"description", description}}},
            {"assignmentCount", static_cast<int>(assigned.affected_rows())},
            {"targeted", targeted},
            {"requestedCount", targeted ? static_cast<int>(target_user_ids.size()) : 0},
            {"skippedCount", skipped_count}};
  }

  json listTrainingPlans(const std::string& supervisor_id, const std::string& requested_status) const {
    const auto status = requested_status.empty() ? std::string("all") : requested_status;
    std::string status_filter;
    if (status == "active") status_filter = " AND p.due_at > NOW()";
    else if (status == "expired") status_filter = " AND p.due_at <= NOW()";
    else if (status != "all") throw ApiError(400, "INVALID_ARGUMENT", "status 参数无效");
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    /* 只列本人发布的计划。指派行里可能残留已移出成员的记录（到期计划刻意保留），
       因此统计时按当前团队过滤一次，保证数字与成员列表口径一致。 */
    const auto rows = tx.exec_params(R"(
      SELECT p.id, p.title, p.period, p.scenario_ids, p.required_count, p.required_pass_rate,
        p.description,
        to_char(p.due_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS due_at,
        (p.due_at <= NOW()) AS expired,
        COUNT(a.learner_id) AS assignment_count,
        COUNT(a.learner_id) FILTER (
          WHERE COALESCE(prog.completed_count, 0) >= p.required_count
            AND COALESCE(prog.avg_score, 0) >= p.required_pass_rate
        ) AS done_count,
        COALESCE(ROUND(AVG(COALESCE(prog.avg_score, 0))::numeric, 1), 0) AS avg_score
      FROM training_plans p
      LEFT JOIN training_assignments a ON a.plan_id = p.id
        AND EXISTS (SELECT 1 FROM supervisor_team_members tm
                    WHERE tm.learner_id = a.learner_id AND tm.supervisor_id = $1)
      LEFT JOIN LATERAL (
        SELECT COUNT(*) AS completed_count, AVG(s.total_score) AS avg_score
        FROM sessions s
        WHERE s.user_id = a.learner_id
          AND s.status = 'completed' AND s.evaluation_status = 'ready'
          AND s.finished_at >= p.created_at AND s.finished_at <= p.due_at
          AND (p.scenario_ids = '[]'::jsonb OR p.scenario_ids ? s.scenario_id)
      ) prog ON TRUE
      WHERE p.created_by = $1
    )" + status_filter + R"(
      GROUP BY p.id
      ORDER BY p.created_at DESC
    )", supervisor_id);
    json plans = json::array();
    for (const auto& row : rows) {
      plans.push_back({{"id", row["id"].c_str()}, {"title", row["title"].c_str()},
                       {"period", row["period"].c_str()},
                       {"scenarioIds", jsonbColumn(row, "scenario_ids")},
                       {"requiredCount", row["required_count"].as<int>()},
                       {"requiredPassRate", row["required_pass_rate"].as<int>()},
                       {"description", row["description"].c_str()},
                       {"dueAt", row["due_at"].c_str()},
                       {"expired", row["expired"].as<bool>()},
                       {"assignmentCount", row["assignment_count"].as<int>()},
                       {"doneCount", row["done_count"].as<int>()},
                       {"avgScore", row["avg_score"].as<double>()}});
    }
    return {{"status", status}, {"plans", plans}, {"total", static_cast<int>(plans.size())}};
  }

  json trainingPlanDetail(const std::string& supervisor_id, const std::string& plan_id) const {
    if (plan_id.empty() || plan_id.size() > 120) {
      throw ApiError(400, "INVALID_ARGUMENT", "计划标识无效");
    }
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    /* 只允许读本人发布的计划；别人的计划与不存在返回同一个 404。 */
    const auto plan_rows = tx.exec_params(R"(
      SELECT id, title, period, scenario_ids, required_count, required_pass_rate, description,
        to_char(due_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS due_at,
        (due_at <= NOW()) AS expired
      FROM training_plans WHERE id = $1 AND created_by = $2
    )", plan_id, supervisor_id);
    if (plan_rows.empty()) throw ApiError(404, "TRAINING_PLAN_NOT_FOUND", "培训计划不存在");
    const auto& plan_row = plan_rows[0];
    const auto required_count = plan_row["required_count"].as<int>();
    const auto required_pass_rate = plan_row["required_pass_rate"].as<int>();
    /* 名单按当前团队成员过滤：已移出成员在未到期计划里的指派行会被同步清掉，
       但已到期计划刻意保留历史行，读取时不再展示。 */
    const auto rows = tx.exec_params(R"(
      WITH plan AS (SELECT * FROM training_plans WHERE id = $1 AND created_by = $2)
      SELECT u.id AS learner_id, COALESCE(NULLIF(u.display_name, ''), '未命名学员') AS display_name,
        COALESCE(prog.completed_count, 0) AS completed_count,
        COALESCE(ROUND(prog.avg_score::numeric, 1), 0) AS avg_score,
        to_char(prog.last_training_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS last_training_date
      FROM training_assignments a
      JOIN plan p ON p.id = a.plan_id
      JOIN users u ON u.id = a.learner_id
        AND EXISTS (SELECT 1 FROM supervisor_team_members tm
                    WHERE tm.learner_id = u.id AND tm.supervisor_id = $2)
      LEFT JOIN LATERAL (
        SELECT COUNT(*) AS completed_count, AVG(s.total_score) AS avg_score,
          MAX(s.updated_at) AS last_training_at
        FROM sessions s
        WHERE s.user_id = a.learner_id
          AND s.status = 'completed' AND s.evaluation_status = 'ready'
          AND s.finished_at >= p.created_at AND s.finished_at <= p.due_at
          AND (p.scenario_ids = '[]'::jsonb OR p.scenario_ids ? s.scenario_id)
      ) prog ON TRUE
      ORDER BY lower(COALESCE(NULLIF(u.display_name, ''), u.id))
    )", plan_id, supervisor_id);
    json assignments = json::array();
    int done_count = 0;
    double score_sum = 0.0;
    for (const auto& row : rows) {
      const auto completed_count = row["completed_count"].as<int>();
      const auto avg_score = row["avg_score"].as<double>();
      const bool done = completed_count >= required_count && avg_score >= required_pass_rate;
      if (done) done_count += 1;
      score_sum += avg_score;
      assignments.push_back({{"learnerId", row["learner_id"].c_str()},
                             {"displayName", row["display_name"].c_str()},
                             {"completedCount", completed_count}, {"avgScore", avg_score},
                             {"lastTrainingDate", row["last_training_date"].is_null()
                                 ? json(nullptr) : json(row["last_training_date"].c_str())},
                             {"done", done}});
    }
    const auto total = static_cast<int>(assignments.size());
    return {{"plan", {{"id", plan_row["id"].c_str()}, {"title", plan_row["title"].c_str()},
                      {"period", plan_row["period"].c_str()},
                      {"scenarioIds", jsonbColumn(plan_row, "scenario_ids")},
                      {"requiredCount", required_count}, {"requiredPassRate", required_pass_rate},
                      {"description", plan_row["description"].c_str()},
                      {"dueAt", plan_row["due_at"].c_str()},
                      {"expired", plan_row["expired"].as<bool>()}}},
            {"assignments", assignments}, {"assignmentCount", total}, {"doneCount", done_count},
            {"avgScore", total == 0 ? 0.0 : std::round(score_sum / total * 10.0) / 10.0}};
  }

  json markPlanNotified(const std::string& supervisor_id, const std::string& plan_id) const {
    const auto detail = trainingPlanDetail(supervisor_id, plan_id);
    json pending = json::array();
    for (const auto& item : detail["assignments"]) {
      if (!item.value("done", false)) pending.push_back(item);
    }
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    tx.exec_params("UPDATE training_assignments SET is_notified = TRUE WHERE plan_id = $1", plan_id);
    tx.commit();
    return {{"planId", plan_id}, {"pendingLearners", pending},
            {"pendingCount", static_cast<int>(pending.size())}};
  }

  /* 报表导出数据源：主管名下全部计划 × 团队成员一行一条。
     进度口径与 trainingPlanDetail 完全一致（同一 LATERAL 聚合），
     包含已到期计划——导出本就用于离线归档，历史行有价值。 */
  json exportPlanMemberRows(const std::string& supervisor_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT p.title AS plan_title,
        to_char(p.due_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS due_date,
        (p.due_at <= NOW()) AS expired,
        COALESCE(NULLIF(u.display_name, ''), '未命名学员') AS display_name,
        COALESCE(prog.completed_count, 0) AS completed_count,
        p.required_count, p.required_pass_rate,
        COALESCE(ROUND(prog.avg_score::numeric, 1), 0) AS avg_score
      FROM training_plans p
      JOIN training_assignments a ON a.plan_id = p.id
      JOIN users u ON u.id = a.learner_id
        AND u.status = 'active'
        AND EXISTS (SELECT 1 FROM supervisor_team_members tm
                    WHERE tm.learner_id = u.id AND tm.supervisor_id = $1)
      LEFT JOIN LATERAL (
        SELECT COUNT(*) AS completed_count, AVG(s.total_score) AS avg_score
        FROM sessions s
        WHERE s.user_id = a.learner_id
          AND s.status = 'completed' AND s.evaluation_status = 'ready'
          AND s.finished_at >= p.created_at AND s.finished_at <= p.due_at
          AND (p.scenario_ids = '[]'::jsonb OR p.scenario_ids ? s.scenario_id)
      ) prog ON TRUE
      ORDER BY p.created_at DESC, lower(COALESCE(NULLIF(u.display_name, ''), u.id))
    )", supervisor_id);
    json items = json::array();
    for (const auto& row : rows) {
      const auto completed_count = row["completed_count"].as<int>();
      const auto avg_score = row["avg_score"].as<double>();
      items.push_back({{"planTitle", row["plan_title"].c_str()},
                       {"dueDate", row["due_date"].c_str()},
                       {"expired", row["expired"].as<bool>()},
                       {"displayName", row["display_name"].c_str()},
                       {"completedCount", completed_count},
                       {"requiredCount", row["required_count"].as<int>()},
                       {"avgScore", avg_score},
                       {"requiredPassRate", row["required_pass_rate"].as<int>()},
                       {"done", completed_count >= row["required_count"].as<int>() &&
                                avg_score >= row["required_pass_rate"].as<int>()}});
    }
    return {{"items", items}};
  }

  json listLearnerTrainingPlans(const std::string& user_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT p.id, p.title, p.period, p.scenario_ids, p.required_count, p.required_pass_rate,
        p.description,
        to_char(p.due_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS due_at,
        (p.due_at <= NOW()) AS expired,
        COALESCE(prog.completed_count, 0) AS completed_count,
        COALESCE(ROUND(prog.avg_score::numeric, 1), 0) AS avg_score
      FROM training_assignments a
      JOIN training_plans p ON p.id = a.plan_id
      LEFT JOIN LATERAL (
        SELECT COUNT(*) AS completed_count, AVG(s.total_score) AS avg_score
        FROM sessions s
        WHERE s.user_id = a.learner_id
          AND s.status = 'completed' AND s.evaluation_status = 'ready'
          AND s.finished_at >= p.created_at AND s.finished_at <= p.due_at
          AND (p.scenario_ids = '[]'::jsonb OR p.scenario_ids ? s.scenario_id)
      ) prog ON TRUE
      WHERE a.learner_id = $1
      ORDER BY p.created_at DESC
    )", user_id);
    json plans = json::array();
    int pending_count = 0;
    for (const auto& row : rows) {
      const auto completed_count = row["completed_count"].as<int>();
      const auto avg_score = row["avg_score"].as<double>();
      const auto required_count = row["required_count"].as<int>();
      const auto required_pass_rate = row["required_pass_rate"].as<int>();
      const bool expired = row["expired"].as<bool>();
      const bool done = completed_count >= required_count && avg_score >= required_pass_rate;
      if (!done && !expired) pending_count += 1;
      plans.push_back({{"id", row["id"].c_str()}, {"title", row["title"].c_str()},
                       {"period", row["period"].c_str()},
                       {"scenarioIds", jsonbColumn(row, "scenario_ids")},
                       {"requiredCount", required_count}, {"requiredPassRate", required_pass_rate},
                       {"description", row["description"].c_str()}, {"dueAt", row["due_at"].c_str()},
                       {"completedCount", completed_count}, {"avgScore", avg_score},
                       {"expired", expired}, {"done", done},
                       {"status", done ? "done" : (expired ? "expired" : "pending")}});
    }
    return {{"plans", plans}, {"total", static_cast<int>(plans.size())}, {"pendingCount", pending_count}};
  }

  // ── 数据报表（文档 3.2 第四模块） ─────────────────────────────────────
  // 违规词直接聚合 evaluations.report->'violations'：该字段是评分模型真实输出，
  // 并经 main.cpp 校验后写入，因此无需任何额外的检测层或新表。

  json forbiddenPhrases(const std::string& supervisor_id, const std::string& time_range,
                        const std::string& requested_scene_category, int limit) const {
    limit = clampInt(limit, 1, 50);
    const auto scene_category = requested_scene_category.empty()
        ? std::string() : requested_scene_category;
    std::string category_filter;
    if (!scene_category.empty()) {
      if (!isSceneCategory(scene_category)) {
        throw ApiError(400, "INVALID_ARGUMENT", "sceneCategory 参数无效");
      }
      category_filter = " AND sc.category = '" + scene_category + "'";
    }
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT )" + violationCategorySql("v->>'type'") + R"( AS category,
        COUNT(*) AS violation_count,
        COUNT(DISTINCT s.user_id) AS member_count
      FROM evaluations e
      JOIN sessions s ON s.id = e.session_id
      JOIN scenarios sc ON sc.id = s.scenario_id
      CROSS JOIN LATERAL jsonb_array_elements(e.report->'violations') AS v
      WHERE e.status = 'ready'
        AND e.report->'violations' IS NOT NULL
        AND s.status = 'completed' AND s.evaluation_status = 'ready'
    )" + supervisorTimeFilter(time_range, "s.updated_at") + category_filter +
        supervisorTeamFilter("s.user_id") + R"(
      GROUP BY 1
      ORDER BY violation_count DESC
      LIMIT )" + std::to_string(limit), supervisor_id);
    json phrases = json::array();
    int counted = 0;
    for (const auto& row : rows) {
      const auto key = std::string(row["category"].c_str());
      const auto count = row["violation_count"].as<int>();
      counted += count;
      phrases.push_back({{"category", key}, {"categoryLabel", violationCategoryLabel(key)},
                         {"count", count}, {"memberCount", row["member_count"].as<int>()}});
    }
    return {{"range", time_range.empty() ? "month" : time_range},
            {"sceneCategory", scene_category},
            {"phrases", phrases}, {"countedViolations", counted},
            {"violationCategories", violationCategoryCatalog()},
            {"sceneCategories", sceneCategoryCatalog()}};
  }

  json forbiddenPhraseMembers(const std::string& supervisor_id, const std::string& category,
                              const std::string& time_range, int limit) const {
    if (!isViolationCategory(category)) throw ApiError(400, "INVALID_ARGUMENT", "category 参数无效");
    limit = clampInt(limit, 1, 200);
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT u.id AS learner_id, COALESCE(NULLIF(u.display_name, ''), '未命名学员') AS display_name,
        COUNT(*) AS violation_count,
        jsonb_agg(DISTINCT s.scenario_name) AS scenarios
      FROM evaluations e
      JOIN sessions s ON s.id = e.session_id
      JOIN users u ON u.id = s.user_id
      CROSS JOIN LATERAL jsonb_array_elements(e.report->'violations') AS v
      WHERE e.status = 'ready'
        AND e.report->'violations' IS NOT NULL
        AND s.status = 'completed' AND s.evaluation_status = 'ready'
        AND )" + violationCategorySql("v->>'type'") + " = '" + category + "'" +
        supervisorTimeFilter(time_range, "s.updated_at") + supervisorTeamFilter("s.user_id") + R"(
      GROUP BY u.id, u.display_name
      ORDER BY violation_count DESC, lower(COALESCE(NULLIF(u.display_name, ''), u.id))
      LIMIT )" + std::to_string(limit), supervisor_id);
    json members = json::array();
    for (const auto& row : rows) {
      members.push_back({{"learnerId", row["learner_id"].c_str()},
                         {"displayName", row["display_name"].c_str()},
                         {"count", row["violation_count"].as<int>()},
                         {"scenarios", jsonbColumn(row, "scenarios")}});
    }
    return {{"category", category}, {"categoryLabel", violationCategoryLabel(category)},
            {"range", time_range.empty() ? "month" : time_range}, {"members", members},
            {"total", static_cast<int>(members.size())}};
  }

  json leaderboard(const std::string& supervisor_id, const std::string& requested_dimension,
                   int limit) const {
    const auto dimension = requested_dimension.empty()
        ? std::string("weekly_sessions") : requested_dimension;
    limit = clampInt(limit, 1, 100);
    /* 榜单只覆盖本团队成员：$1 为当前登录主管 id。 */
    const auto team_filter = supervisorTeamFilter("u.id");
    std::string sql;
    if (dimension == "weekly_sessions") {
      sql = R"(
        SELECT u.id AS learner_id, COALESCE(NULLIF(u.display_name, ''), '未命名学员') AS display_name,
          COUNT(s.id) AS score
        FROM users u
        LEFT JOIN sessions s ON s.user_id = u.id
          AND s.status = 'completed' AND s.evaluation_status = 'ready')" +
          supervisorTimeFilter("week", "s.updated_at") + R"(
        WHERE u.role = 'learner' AND u.status = 'active')" + team_filter + R"(
        GROUP BY u.id, u.display_name
        ORDER BY score DESC, lower(COALESCE(NULLIF(u.display_name, ''), u.id))
        LIMIT )" + std::to_string(limit);
    } else if (dimension == "monthly_avg") {
      sql = R"(
        SELECT u.id AS learner_id, COALESCE(NULLIF(u.display_name, ''), '未命名学员') AS display_name,
          COALESCE(ROUND(AVG(s.total_score)::numeric, 1), 0) AS score
        FROM users u
        LEFT JOIN sessions s ON s.user_id = u.id
          AND s.status = 'completed' AND s.evaluation_status = 'ready')" +
          supervisorTimeFilter("month", "s.updated_at") + R"(
        WHERE u.role = 'learner' AND u.status = 'active')" + team_filter + R"(
        GROUP BY u.id, u.display_name
        ORDER BY score DESC, lower(COALESCE(NULLIF(u.display_name, ''), u.id))
        LIMIT )" + std::to_string(limit);
    } else if (dimension == "completed_scenarios") {
      sql = R"(
        SELECT u.id AS learner_id, COALESCE(NULLIF(u.display_name, ''), '未命名学员') AS display_name,
          COUNT(s.id) FILTER (WHERE s.total_score >= 60) AS score
        FROM users u
        LEFT JOIN sessions s ON s.user_id = u.id
          AND s.status = 'completed' AND s.evaluation_status = 'ready'
        WHERE u.role = 'learner' AND u.status = 'active')" + team_filter + R"(
        GROUP BY u.id, u.display_name
        ORDER BY score DESC, lower(COALESCE(NULLIF(u.display_name, ''), u.id))
        LIMIT )" + std::to_string(limit);
    } else if (dimension == "streak_days") {
      // 连续打卡：沿用 learner_checkins 的连续段算法，锚点放宽为「今天或昨天」，
      // 否则每天上午尚未打卡时整张榜会集体归零。
      sql = R"(
        WITH today AS (SELECT (NOW() AT TIME ZONE 'Asia/Shanghai')::date AS d),
        anchor_days AS (SELECT DISTINCT user_id, checkin_date FROM learner_checkins),
        numbered AS (
          SELECT user_id,
            checkin_date + ((ROW_NUMBER() OVER (PARTITION BY user_id ORDER BY checkin_date DESC)
              - 1)::int) AS anchor
          FROM anchor_days, today WHERE checkin_date <= today.d
        ),
        streaks AS (
          SELECT user_id, COUNT(*) AS streak FROM numbered, today
          WHERE anchor IN (today.d, today.d - 1) GROUP BY user_id
        )
        SELECT u.id AS learner_id, COALESCE(NULLIF(u.display_name, ''), '未命名学员') AS display_name,
          COALESCE(st.streak, 0) AS score
        FROM users u
        LEFT JOIN streaks st ON st.user_id = u.id
        WHERE u.role = 'learner' AND u.status = 'active')" + team_filter + R"(
        ORDER BY score DESC, lower(COALESCE(NULLIF(u.display_name, ''), u.id))
        LIMIT )" + std::to_string(limit);
    } else {
      throw ApiError(400, "INVALID_ARGUMENT", "dimension 参数无效");
    }
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(sql, supervisor_id);
    json entries = json::array();
    int rank = 0;
    for (const auto& row : rows) {
      rank += 1;
      entries.push_back({{"rank", rank}, {"learnerId", row["learner_id"].c_str()},
                         {"displayName", row["display_name"].c_str()},
                         {"score", std::round(row["score"].as<double>() * 10.0) / 10.0}});
    }
    return {{"dimension", dimension}, {"entries", entries}, {"total", static_cast<int>(entries.size())}};
  }

 private:
  static const std::vector<std::pair<std::string, std::string>>& violationCategories() {
    static const std::vector<std::pair<std::string, std::string>> categories = {
        {"efficacy_guarantee", "疗效/绝对化保证"},
        {"overreach_judgement", "越权判断方案"},
        {"risk_mishandling", "风险处理不当"},
        {"peer_disparagement", "贬低同行"},
        {"other", "其他"},
    };
    return categories;
  }

  static json violationCategoryCatalog() {
    json catalog = json::array();
    for (const auto& item : violationCategories()) {
      catalog.push_back({{"id", item.first}, {"name", item.second}});
    }
    return catalog;
  }

  static bool isViolationCategory(const std::string& category) {
    for (const auto& item : violationCategories()) {
      if (item.first == category) return true;
    }
    return false;
  }

  // 场景分类与 migrations/007 的 CHECK 约束一一对应；报表按此维度筛选，
  // 与违规类型（violationCategories）是两条正交的筛选轴，不要混用。
  // 中文名与学员端训练页（pages/index CATEGORY_CONFIG）保持同一套措辞，
  // 否则主管发布的「价格沟通」在学员端显示为「价格异议」，两边对不上。
  static const std::vector<std::pair<std::string, std::string>>& sceneCategories() {
    static const std::vector<std::pair<std::string, std::string>> categories = {
        {"consultation", "咨询解答"},
        {"price_negotiation", "价格异议"},
        {"complaint_handling", "投诉安抚"},
        {"recommendation", "项目推荐"},
    };
    return categories;
  }

  static json sceneCategoryCatalog() {
    json catalog = json::array();
    for (const auto& item : sceneCategories()) {
      catalog.push_back({{"id", item.first}, {"name", item.second}});
    }
    return catalog;
  }

  static bool isSceneCategory(const std::string& category) {
    for (const auto& item : sceneCategories()) {
      if (item.first == category) return true;
    }
    return false;
  }

  static std::string violationCategoryLabel(const std::string& category) {
    for (const auto& item : violationCategories()) {
      if (item.first == category) return item.second;
    }
    return "其他";
  }

  // 违规类型由评分模型自由生成（措辞不统一），按关键词归并到固定几类，
  // 保证统计图稳定可读；原始措辞仍保留在逐条明细里。
  static std::string violationCategorySql(const std::string& expr) {
    return "(CASE"
           " WHEN " + expr + " ~ '保证|绝对|一定|百分百|100%|包好|包治|承诺' THEN 'efficacy_guarantee'"
           " WHEN " + expr + " ~ '越权|诊断|治疗方案|判断|适合|拔牙' THEN 'overreach_judgement'"
           " WHEN " + expr + " ~ '风险|术后|症状|肿|疼|不适|忽视|延误' THEN 'risk_mishandling'"
           " WHEN " + expr + " ~ '贬低|同行|机构|别家|竞争' THEN 'peer_disparagement'"
           " ELSE 'other' END)";
  }

  static json jsonbColumn(const pqxx::row& row, const std::string& name) {
    if (row[name].is_null()) return json::array();
    const auto parsed = json::parse(row[name].c_str(), nullptr, false);
    return parsed.is_discarded() ? json::array() : parsed;
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

  /* 团队范围过滤。$1 恒为当前登录主管 id，由服务端 authorize() 提供；
     客户端只传计划 / 成员标识，永远无法指定「以谁的身份聚合」。 */
  static std::string supervisorTeamFilter(const std::string& column) {
    return " AND " + column +
        " IN (SELECT learner_id FROM supervisor_team_members WHERE supervisor_id = $1)";
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
    /* 只统计真正有评分的维度：dimensionAverages 对「没有任何有效评分」
       的维度返回 null，直接 value(key, 0.0) 会在 null 上取 double 而抛 type_error。
       口径与 learningProfile 一致：没数据就不编造 0 分弱项。 */
    std::vector<std::string> ordered;
    for (const auto& key : keys) {
      if (averages.contains(key) && averages[key].is_number()) ordered.push_back(key);
    }
    std::sort(ordered.begin(), ordered.end(), [&](const auto& left, const auto& right) {
      return averages[left].get<double>() < averages[right].get<double>();
    });
    json weaknesses = json::array();
    for (size_t index = 0; index < ordered.size() && index < 2; ++index) {
      const auto& key = ordered[index];
      const auto score = averages[key].get<double>();
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

  static std::string repairEvaluationState(pqxx::transaction_base& tx,
                                           const std::string& session_id,
                                           const std::string& session_status,
                                           const std::string& session_evaluation_status) {
    if (session_status != "completed") return session_evaluation_status;
    const auto rows = tx.exec_params(
        "SELECT status, report FROM evaluations WHERE session_id = $1 FOR UPDATE", session_id);
    if (!rows.empty() && std::string(rows[0]["status"].c_str()) == "ready" &&
        !rows[0]["report"].is_null()) {
      auto report = json::parse(rows[0]["report"].c_str(), nullptr, false);
      if (!report.is_object()) throw ApiError(503, "REPORT_INVALID", "评分报告存储格式无效");
      const auto schema_version = reportSchemaVersion(report);
      if (schema_version == 2 && isV2InsufficientEvidenceReport(report)) {
        tx.exec_params(R"(
          UPDATE sessions SET evaluation_status = 'ready', total_score = NULL, updated_at = NOW()
          WHERE id = $1 AND (evaluation_status <> 'ready' OR total_score IS NOT NULL)
        )", session_id);
        return "ready";
      }
      if (schema_version != 1 && schema_version != 2) {
        throw ApiError(503, "REPORT_INVALID", "评分报告版本无效");
      }
      const auto valid_integer_score = [](const json& value) {
        return validReportScore(value) && std::floor(value.get<double>()) == value.get<double>();
      };
      std::optional<int> total;
      if (report.contains("totalScore") && valid_integer_score(report["totalScore"])) {
        total = report["totalScore"].get<int>();
      } else if (schema_version == 1) {
          const auto session = tx.exec_params("SELECT total_score FROM sessions WHERE id = $1", session_id);
          if (!session[0]["total_score"].is_null()) total = session[0]["total_score"].as<int>();
          if (!total && report.contains("dimensionScores") && report["dimensionScores"].is_object()) {
            const auto& dimensions = report["dimensionScores"];
            const std::vector<std::pair<std::string, double>> weights = {
                {"knowledgeAccuracy", .25}, {"medicalCompliance", .25}, {"empathy", .20},
                {"needsDiscovery", .20}, {"serviceEtiquette", .10}};
            double weighted = 0;
            bool complete = true;
            for (const auto& entry : weights) {
              if (!dimensions.contains(entry.first) || !valid_integer_score(dimensions[entry.first])) {
                complete = false;
                break;
              }
              weighted += dimensions[entry.first].get<double>() * entry.second;
            }
            if (complete) total = static_cast<int>(std::round(weighted));
          }
      }
      // A read must never destroy an existing report just because it cannot
      // infer a score. Keep the evidence for diagnosis instead of re-enqueueing.
      if (!total) {
        throw ApiError(503, "REPORT_INVALID",
                       schema_version == 2 ? "v2 报告总分状态无效，原报告已保留"
                                           : "旧报告缺少可恢复的总分，原报告已保留");
      }
      if (schema_version == 1) {
        tx.exec_params(R"(
          UPDATE evaluations SET report = jsonb_set(report, '{totalScore}', to_jsonb($2::int)),
            updated_at = NOW()
          WHERE session_id = $1 AND report->'totalScore' IS DISTINCT FROM to_jsonb($2::int)
        )", session_id, *total);
      }
      tx.exec_params(R"(
        UPDATE sessions SET evaluation_status = 'ready', total_score = $2, updated_at = NOW()
        WHERE id = $1 AND (evaluation_status <> 'ready' OR total_score IS DISTINCT FROM $2)
      )", session_id, *total);
      return "ready";
    }
    const bool failed = session_evaluation_status == "failed" ||
        (!rows.empty() && std::string(rows[0]["status"].c_str()) == "failed");
    if (failed) {
      tx.exec_params(R"(
        INSERT INTO evaluations(session_id, status, report, error_type, updated_at)
        VALUES ($1, 'failed', NULL, 'STATE_RECORD_MISSING', NOW())
        ON CONFLICT (session_id) DO UPDATE SET status = 'failed', report = NULL,
          error_type = COALESCE(evaluations.error_type, 'STATE_RECORD_MISSING'), updated_at = NOW()
      )", session_id);
      tx.exec_params(R"(
        UPDATE sessions SET evaluation_status = 'failed', total_score = NULL, updated_at = NOW()
        WHERE id = $1 AND (evaluation_status <> 'failed' OR total_score IS NOT NULL)
      )", session_id);
      return "failed";
    }
    tx.exec_params(R"(
      INSERT INTO evaluations(session_id, status, report, error_type, updated_at)
      VALUES ($1, 'generating', NULL, NULL, NOW())
      ON CONFLICT (session_id) DO UPDATE SET status = 'generating', report = NULL,
        error_type = NULL, updated_at = NOW()
      WHERE evaluations.status <> 'generating' OR evaluations.report IS NOT NULL
    )", session_id);
    tx.exec_params(R"(
      UPDATE sessions SET evaluation_status = 'generating', total_score = NULL, updated_at = NOW()
      WHERE id = $1 AND (evaluation_status <> 'generating' OR total_score IS NOT NULL)
    )", session_id);
    if (!ensureAiJob(tx, "evaluation", session_id)) {
      tx.exec_params(R"(
        UPDATE evaluations SET status = 'failed', error_type = 'JOB_GENERATION_EXHAUSTED',
          updated_at = NOW() WHERE session_id = $1
      )", session_id);
      tx.exec_params(R"(
        UPDATE sessions SET evaluation_status = 'failed', total_score = NULL, updated_at = NOW()
        WHERE id = $1
      )", session_id);
      return "failed";
    }
    return "generating";
  }

  static json messageJson(const std::string& id, const std::string& role,
                          const std::string& content, int round,
                          const std::string& emotion = "") {
    return {{"id", id}, {"role", role}, {"content", content}, {"round", round},
            {"emotion", emotion.empty() ? json(nullptr) : json(emotion)}};
  }

  static json messageJson(const pqxx::row& row) {
    auto message = messageJson(row["id"].c_str(), row["role"].c_str(),
                               row["content"].c_str(), row["round"].as<int>(),
                               row["emotion"].is_null() ? "" : std::string(row["emotion"].c_str()));
    message["createdAt"] = row["created_at"].c_str();
    return message;
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

  std::shared_ptr<DatabasePool> database_pool_;
};

class ReliableRoleplayDatabase {
 public:
  explicit ReliableRoleplayDatabase(std::shared_ptr<DatabasePool> database_pool)
      : database_pool_(std::move(database_pool)) {}

  json listScenarios(const std::string& user_id, const std::string& service_id = "") const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT s.id, s.name, s.category, s.summary, s.difficulty, s.focus, s.patient_profile,
        s.max_rounds, s.roleplay_config,
        active.id AS active_id, active.current_round AS active_current_round,
        active.max_rounds AS active_max_rounds, active.updated_at AS active_updated_at
      FROM scenarios s
      LEFT JOIN LATERAL (
        SELECT id, current_round, max_rounds, updated_at FROM roleplay_sessions
        WHERE user_id = $1 AND scenario_id = s.id AND status = 'in_progress'
          AND ($2 = '' OR service_id = $2)
        ORDER BY updated_at DESC LIMIT 1
      ) active ON TRUE
      WHERE s.is_active AND NOT s.is_template
      ORDER BY s.sort_order
    )", user_id, service_id);
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

  json createSession(const std::string& user_id, const std::string& scenario_id,
                     const std::string& free_description = std::string(),
                     const std::string& service_id = std::string(),
                     const std::string& client_session_id = std::string()) const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto scenario = tx.exec_params(
        "SELECT id, name, max_rounds FROM scenarios WHERE id = $1", scenario_id);
    if (scenario.empty()) throw ApiError(404, "SCENARIO_NOT_FOUND", "训练场景不存在");
    if (!service_id.empty()) {
      if (client_session_id.empty() || client_session_id.size() > 100) {
        throw ApiError(400, "INVALID_ARGUMENT", "clientSessionId 格式无效");
      }
      const auto replay = tx.exec_params(R"(
        SELECT id, scenario_id, service_id FROM roleplay_sessions
        WHERE user_id = $1 AND client_session_id = $2
      )", user_id, client_session_id);
      if (!replay.empty()) {
        if (std::string(replay[0]["scenario_id"].c_str()) != scenario_id ||
            replay[0]["service_id"].is_null() ||
            std::string(replay[0]["service_id"].c_str()) != service_id) {
          throw ApiError(409, "IDEMPOTENCY_CONFLICT", "clientSessionId 对应不同会话参数");
        }
        return {{"session", getSessionRow(tx, replay[0]["id"].c_str(), user_id)},
                {"messages", json::array()}};
      }
    }
    const auto session_id = makeId("rpsess");
    const auto max_rounds = clampInt(scenario[0]["max_rounds"].as<int>(), 1, 10);
    std::string service_revision_id;
    if (!service_id.empty()) {
      const auto service = tx.exec_params(R"(
        SELECT s.current_revision_id FROM clinic_services s
        JOIN service_scenarios ss ON ss.service_id = s.id AND ss.scenario_id = $2
        WHERE s.id = $1 AND s.status = 'active' AND s.current_revision_id IS NOT NULL
      )", service_id, scenario_id);
      if (service.empty()) {
        throw ApiError(409, "SERVICE_SCENARIO_MISMATCH", "服务不可用或不支持当前场景");
      }
      service_revision_id = service[0]["current_revision_id"].c_str();
    }
    const auto inserted = tx.exec_params(R"(
      INSERT INTO roleplay_sessions
        (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds,
         free_description, service_id, service_revision_id, client_session_id, context_version)
      VALUES ($1, $2, $3, $4, 'in_progress', 0, $5, NULLIF($6, ''), NULLIF($7, ''), NULLIF($8, ''),
              NULLIF($9, ''), CASE WHEN $7 = '' THEN 1 ELSE 2 END)
      ON CONFLICT DO NOTHING
      RETURNING id
    )", session_id, user_id, scenario_id, scenario[0]["name"].c_str(), max_rounds,
        free_description, service_id, service_revision_id, client_session_id);
    if (inserted.empty()) {
      throw ApiError(409, "ROLEPLAY_SESSION_IN_PROGRESS", "该服务和场景已有进行中的患者模拟");
    }
    if (!service_id.empty()) {
      createRagContext(tx, session_id, service_id, service_revision_id);
    }
    const auto saved = getSessionRow(tx, session_id, user_id);
    tx.commit();
    return {{"session", saved}, {"messages", json::array()}};
  }

  json restartSession(const std::string& user_id, const std::string& session_id,
                      const std::string& client_session_id = "") const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto previous = tx.exec_params(R"(
      SELECT scenario_id, status, free_description, service_id FROM roleplay_sessions
      WHERE id = $1 AND user_id = $2 FOR UPDATE
    )", session_id, user_id);
    if (previous.empty()) {
      throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    }
    if (std::string(previous[0]["status"].c_str()) != "in_progress") {
      throw ApiError(409, "ROLEPLAY_SESSION_NOT_RESTARTABLE", "只有进行中的患者模拟可以重新开始");
    }
    const auto scenario_id = std::string(previous[0]["scenario_id"].c_str());
    // 重新开始要沿用原会话的场景描述（自由模拟），否则新会话会丢掉场景设定。
    const auto free_description = previous[0]["free_description"].is_null()
        ? std::string() : std::string(previous[0]["free_description"].c_str());
    const auto service_id = previous[0]["service_id"].is_null()
        ? std::string() : std::string(previous[0]["service_id"].c_str());
    const auto scenario = tx.exec_params(
        "SELECT name, max_rounds FROM scenarios WHERE id = $1", scenario_id);
    tx.exec_params(
        "UPDATE roleplay_sessions SET status = 'abandoned', updated_at = NOW() WHERE id = $1",
        session_id);
    const auto new_id = makeId("rpsess");
    std::string service_revision_id;
    if (!service_id.empty()) {
      if (client_session_id.empty() || client_session_id.size() > 100) {
        throw ApiError(400, "INVALID_ARGUMENT", "clientSessionId 格式无效");
      }
      const auto service = tx.exec_params(R"(
        SELECT current_revision_id FROM clinic_services
        WHERE id = $1 AND status = 'active' AND current_revision_id IS NOT NULL
      )", service_id);
      if (service.empty()) throw ApiError(409, "SERVICE_NOT_AVAILABLE", "服务当前不可用");
      service_revision_id = service[0]["current_revision_id"].c_str();
    }
    tx.exec_params(R"(
      INSERT INTO roleplay_sessions
        (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds,
         free_description, service_id, service_revision_id, client_session_id, context_version)
      VALUES ($1, $2, $3, $4, 'in_progress', 0, $5, NULLIF($6, ''), NULLIF($7, ''), NULLIF($8, ''),
              NULLIF($9, ''), CASE WHEN $7 = '' THEN 1 ELSE 2 END)
    )", new_id, user_id, scenario_id, scenario[0]["name"].c_str(),
        clampInt(scenario[0]["max_rounds"].as<int>(), 1, 10), free_description, service_id,
        service_revision_id, client_session_id);
    if (!service_id.empty()) createRagContext(tx, new_id, service_id, service_revision_id);
    const auto saved = getSessionRow(tx, new_id, user_id);
    tx.commit();
    return {{"session", saved}, {"messages", json::array()}};
  }

  json getSession(const std::string& user_id, const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto session = getSessionRow(tx, session_id, user_id);
    const auto rows = tx.exec_params(R"(
      SELECT id, role, content, learning_points, compliance_boundary, answer_status,
        citations, trace_id, round,
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
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    return {{"session", getSessionRow(tx, session_id, "")}};
  }

  json listSessions(const std::string& user_id, const std::string& status,
                    const std::string& scenario_id, int limit) const {
    const std::vector<std::string> allowed = {"all", "active", "in_progress", "completed", "abandoned"};
    if (std::find(allowed.begin(), allowed.end(), status) == allowed.end()) {
      throw ApiError(400, "INVALID_ARGUMENT", "status 参数无效");
    }
    limit = clampInt(limit, 1, 50);
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const std::string db_status = status == "active" ? "in_progress" : status;
    std::string query =
        "SELECT r.id, r.scenario_id, r.scenario_name, r.status, r.current_round, r.max_rounds, "
        "r.context_version, r.service_id, r.service_revision_id, cs.name AS service_name, "
        "sr.version AS service_version, " +
        std::string(kRoleplaySessionTimes) +
        ", COALESCE(summary.status, 'not_started') AS summary_status "
        "FROM roleplay_sessions r LEFT JOIN roleplay_summaries summary ON summary.session_id = r.id "
        "LEFT JOIN clinic_services cs ON cs.id = r.service_id "
        "LEFT JOIN service_revisions sr ON sr.id = r.service_revision_id "
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
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
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

  /* 自由模拟会话里学员描述的场景原文；普通场景会话返回空串。 */
  std::string getFreeDescription(const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(
        "SELECT free_description FROM roleplay_sessions WHERE id = $1", session_id);
    if (rows.empty() || rows[0]["free_description"].is_null()) return std::string();
    return trim(std::string(rows[0]["free_description"].c_str()));
  }

  json getRagContext(const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT c.id, c.service_id, c.service_revision_id, c.manifest, c.manifest_hash,
        c.training_scope,
        to_char(c.knowledge_as_of AT TIME ZONE 'Asia/Shanghai',
          'YYYY-MM-DD"T"HH24:MI:SS') || '+08:00' AS knowledge_as_of
      FROM training_contexts c
      WHERE c.session_type = 'roleplay' AND c.session_id = $1
    )", session_id);
    if (rows.empty()) return nullptr;
    return {{"contextId", rows[0]["id"].c_str()},
            {"serviceId", rows[0]["service_id"].c_str()},
            {"serviceRevisionId", rows[0]["service_revision_id"].c_str()},
            {"manifest", json::parse(rows[0]["manifest"].c_str())},
            {"manifestHash", rows[0]["manifest_hash"].c_str()},
            {"trainingScope", rows[0]["training_scope"].c_str()},
            {"knowledgeAsOf", rows[0]["knowledge_as_of"].c_str()}};
  }

  json getEvidence(const std::string& user_id, const std::string& session_id,
                   const std::string& trace_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT t.id, t.purpose, t.round, t.query, t.evidence_json, t.model_version, t.created_at
      FROM rag_traces t
      JOIN training_contexts c ON c.id = t.context_id AND c.session_type = 'roleplay'
      JOIN roleplay_sessions r ON r.id = c.session_id
      WHERE r.id = $1 AND r.user_id = $2 AND t.id = $3 AND t.is_public = TRUE
    )", session_id, user_id, trace_id);
    if (rows.empty()) throw ApiError(404, "EVIDENCE_NOT_FOUND", "引用依据不存在");
    return {{"traceId", rows[0]["id"].c_str()}, {"purpose", rows[0]["purpose"].c_str()},
            {"round", rows[0]["round"].is_null() ? json(nullptr) : json(rows[0]["round"].as<int>())},
            {"query", rows[0]["query"].c_str()},
            {"evidence", json::parse(rows[0]["evidence_json"].c_str())},
            {"modelVersion", rows[0]["model_version"].is_null()
                ? json(nullptr) : json(rows[0]["model_version"].c_str())},
            {"createdAt", rows[0]["created_at"].c_str()}};
  }

  json getHistory(const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
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
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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
        customer.compliance_boundary AS customer_compliance_boundary,
        customer.answer_status AS customer_answer_status,
        customer.citations AS customer_citations, customer.trace_id AS customer_trace_id
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
                ? "" : row["customer_compliance_boundary"].c_str(),
            row["customer_answer_status"].is_null() ? "" : row["customer_answer_status"].c_str(),
            row["customer_citations"].is_null() ? json::array()
                : json::parse(row["customer_citations"].c_str()),
            row["customer_trace_id"].is_null() ? "" : row["customer_trace_id"].c_str());
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
      auto connection = database_pool_->acquire();
      pqxx::work tx(connection.get());
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
    const auto answer_status = trim(jsonString(model_reply, "answerStatus"));
    const auto citations = model_reply.value("citations", json::array());
    if (utf8Length(reply) < 1 || utf8Length(reply) > 1000 || !learning_points.is_array() ||
        learning_points.size() < 2 || learning_points.size() > 4 || boundary.empty() ||
        utf8Length(boundary) > 300 || !citations.is_array() ||
        (!answer_status.empty() && answer_status != "answered" && answer_status != "partial" &&
         answer_status != "unknown" && answer_status != "conflicted")) {
      throw ApiError(503, "MODEL_INVALID_RESPONSE", "模型未返回有效标准客服回复");
    }
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto session_rows = tx.exec_params(R"(
      SELECT * FROM roleplay_sessions WHERE id = $1 AND user_id = $2 FOR UPDATE
    )", session_id, user_id);
    if (session_rows.empty()) {
      throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    }
    const auto existing = tx.exec_params(R"(
      SELECT id, content, learning_points, compliance_boundary, answer_status, citations, trace_id
      FROM roleplay_messages
      WHERE session_id = $1 AND role = 'standard_customer' AND round = $2
    )", session_id, round);
    if (!existing.empty()) {
      const auto session = getSessionRow(tx, session_id, user_id);
      const auto& row = existing[0];
      tx.commit();
      return {{"standardCustomerMessage", messageJson(
                  row["id"].c_str(), "standard_customer", row["content"].c_str(), round,
                  json::parse(row["learning_points"].c_str()),
                  row["compliance_boundary"].is_null() ? "" : row["compliance_boundary"].c_str(),
                  row["answer_status"].is_null() ? "" : row["answer_status"].c_str(),
                  json::parse(row["citations"].c_str()),
                  row["trace_id"].is_null() ? "" : row["trace_id"].c_str())},
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
    const auto trace_id = trim(jsonString(model_reply, "traceId"));
    if (!trace_id.empty()) {
      const auto evidence_bundle = model_reply.value("evidenceBundle", json::object());
      const auto inserted_trace = tx.exec_params(R"(
        INSERT INTO rag_traces
          (id, context_id, purpose, round, attempt_token, query, evidence_json,
           model_version, is_public)
        SELECT $1, c.id, 'customer_reply', $3, $4, $5, $6::jsonb, NULLIF($7, ''), FALSE
        FROM training_contexts c
        WHERE c.session_type = 'roleplay' AND c.session_id = $2
        RETURNING id
      )", trace_id, session_id, round, token, jsonString(model_reply, "query"),
          evidence_bundle.dump(), jsonString(model_reply, "modelVersion"));
      if (inserted_trace.empty()) {
        throw ApiError(503, "RAG_UNAVAILABLE", "会话证据上下文不可用");
      }
    }
    tx.exec_params(R"(
      INSERT INTO roleplay_messages
        (id, session_id, role, content, learning_points, compliance_boundary, round,
         answer_status, citations, trace_id)
      VALUES ($1, $2, 'standard_customer', $3, $4::jsonb, $5, $6,
              NULLIF($7, ''), $8::jsonb, NULLIF($9, ''))
    )", message_id, session_id, reply, learning_points.dump(), boundary, round,
        answer_status, citations.dump(), trace_id);
    if (!trace_id.empty()) {
      tx.exec_params("UPDATE rag_traces SET is_public = TRUE WHERE id = $1", trace_id);
    }
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
                message_id, "standard_customer", reply, round, learning_points, boundary,
                answer_status, citations, trace_id)},
            {"session", saved}, {"shouldFinish", should_finish}};
  }

  json finish(const std::string& user_id, const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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
      repairSummaryState(tx, session_id, status);
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
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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
      INSERT INTO roleplay_summaries(session_id, status, summary, error_type, updated_at)
      VALUES ($1, 'generating', NULL, NULL, NOW())
      ON CONFLICT (session_id) DO UPDATE SET status = 'generating', summary = NULL,
        error_type = NULL, updated_at = NOW()
    )", session_id);
    tx.exec_params("UPDATE roleplay_sessions SET updated_at = NOW() WHERE id = $1", session_id);
    enqueueAiJob(tx, "roleplay_summary", session_id, true);
    tx.commit();
  }

  json getSummary(const std::string& user_id, const std::string& session_id) const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto session = tx.exec_params(
        "SELECT status FROM roleplay_sessions WHERE id = $1 AND user_id = $2 FOR UPDATE",
        session_id, user_id);
    if (session.empty()) throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    repairSummaryState(tx, session_id, session[0]["status"].c_str());
    const auto summary = tx.exec_params(
        "SELECT status, summary FROM roleplay_summaries WHERE session_id = $1", session_id);
    if (summary.empty()) {
      tx.commit();
      return {{"sessionId", session_id}, {"status", "not_started"},
              {"retryable", false}, {"summary", nullptr}};
    }
    const auto status = std::string(summary[0]["status"].c_str());
    if (status != "ready" || summary[0]["summary"].is_null()) {
      tx.commit();
      return {{"sessionId", session_id}, {"status", status},
              {"retryable", status == "failed"}, {"summary", nullptr}};
    }
    const auto result = json{{"sessionId", session_id}, {"status", "ready"}, {"retryable", false},
                             {"summary", json::parse(summary[0]["summary"].c_str())}};
    tx.commit();
    return result;
  }

  void saveSummary(const AiJob& job, json summary, const std::string& model_version) const {
    summary["modelVersion"] = model_version;
    summary["promptVersion"] = "roleplay-summary-prompt-v1";
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    if (!lockAiJobTarget(tx, "roleplay_summary", job.target_id)) {
      throw ApiError(409, "JOB_LEASE_LOST", "AI 任务目标已不存在");
    }
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
  static void createRagContext(pqxx::transaction_base& tx, const std::string& session_id,
                               const std::string& service_id,
                               const std::string& service_revision_id) {
    const auto revisions = tx.exec_params(R"(
      SELECT r.id
      FROM knowledge_entries e
      JOIN knowledge_revisions r ON r.id = e.current_revision_id
      WHERE e.status = 'active' AND r.metadata->>'trainingScope' = 'demo'
        AND (e.scope = 'general' OR e.service_id = $1)
      ORDER BY r.id
    )", service_id);
    json manifest = json::array();
    for (const auto& row : revisions) manifest.push_back(row["id"].c_str());
    const auto manifest_text = manifest.dump();
    const auto manifest_hash = std::string("md5:") +
        tx.exec_params("SELECT md5($1) AS hash", manifest_text)[0]["hash"].c_str();
    tx.exec_params(R"(
      INSERT INTO training_contexts
        (id, session_type, session_id, service_id, service_revision_id,
         manifest, manifest_hash, training_scope)
      VALUES ($1, 'roleplay', $2, $3, $4, $5::jsonb, $6, 'demo')
    )", makeId("ctx"), session_id, service_id, service_revision_id,
        manifest_text, manifest_hash);
  }

  static std::string repairSummaryState(pqxx::transaction_base& tx,
                                        const std::string& session_id,
                                        const std::string& session_status) {
    const auto rows = tx.exec_params(
        "SELECT status, summary FROM roleplay_summaries WHERE session_id = $1 FOR UPDATE", session_id);
    if (!rows.empty() && std::string(rows[0]["status"].c_str()) == "ready" &&
        !rows[0]["summary"].is_null()) {
      try {
        if (json::parse(rows[0]["summary"].c_str()).is_object()) return "ready";
      } catch (...) {
      }
    }
    if (!rows.empty() && std::string(rows[0]["status"].c_str()) == "failed") {
      tx.exec_params(R"(
        UPDATE roleplay_summaries SET summary = NULL, updated_at = NOW()
        WHERE session_id = $1 AND summary IS NOT NULL
      )", session_id);
      return "failed";
    }
    if (session_status != "completed") return rows.empty() ? "not_started" : rows[0]["status"].c_str();
    tx.exec_params(R"(
      INSERT INTO roleplay_summaries(session_id, status, summary, error_type, updated_at)
      VALUES ($1, 'generating', NULL, NULL, NOW())
      ON CONFLICT (session_id) DO UPDATE SET status = 'generating', summary = NULL,
        error_type = NULL, updated_at = NOW()
      WHERE roleplay_summaries.status <> 'generating' OR roleplay_summaries.summary IS NOT NULL
    )", session_id);
    if (!ensureAiJob(tx, "roleplay_summary", session_id)) {
      tx.exec_params(R"(
        UPDATE roleplay_summaries SET status = 'failed', error_type = 'JOB_GENERATION_EXHAUSTED',
          updated_at = NOW() WHERE session_id = $1
      )", session_id);
      return "failed";
    }
    return "generating";
  }

  static json messageJson(const std::string& id, const std::string& role,
                          const std::string& content, int round,
                          const json& learning_points = json::array(),
                          const std::string& compliance_boundary = "",
                          const std::string& answer_status = "",
                          const json& citations = json::array(),
                          const std::string& trace_id = "") {
    return {{"id", id}, {"role", role}, {"content", content}, {"round", round},
            {"learningPoints", learning_points},
            {"complianceBoundary", compliance_boundary.empty()
                ? json(nullptr) : json(compliance_boundary)},
            {"answerStatus", answer_status.empty() ? json(nullptr) : json(answer_status)},
            {"citations", citations},
            {"traceId", trace_id.empty() ? json(nullptr) : json(trace_id)}};
  }

  static json messageJson(const pqxx::row& row) {
    const auto learning_points = row["learning_points"].is_null()
        ? json::array() : json::parse(row["learning_points"].c_str());
    const auto boundary = row["compliance_boundary"].is_null()
        ? "" : std::string(row["compliance_boundary"].c_str());
    auto message = messageJson(row["id"].c_str(), row["role"].c_str(),
                               row["content"].c_str(), row["round"].as<int>(),
                               learning_points, boundary,
                               row["answer_status"].is_null() ? "" : row["answer_status"].c_str(),
                               row["citations"].is_null() ? json::array()
                                   : json::parse(row["citations"].c_str()),
                               row["trace_id"].is_null() ? "" : row["trace_id"].c_str());
    message["createdAt"] = row["created_at"].c_str();
    return message;
  }

  static json getSessionRow(pqxx::transaction_base& tx, const std::string& session_id,
                            const std::string& user_id) {
    const auto rows = tx.exec_params(
        "SELECT r.id, r.user_id, r.scenario_id, r.scenario_name, r.status, r.current_round, "
        "r.max_rounds, r.context_version, r.service_id, r.service_revision_id, "
        "cs.name AS service_name, sr.version AS service_version, " + std::string(kRoleplaySessionTimes) +
        ", COALESCE(summary.status, 'not_started') AS summary_status "
        "FROM roleplay_sessions r LEFT JOIN roleplay_summaries summary ON summary.session_id = r.id "
        "LEFT JOIN clinic_services cs ON cs.id = r.service_id "
        "LEFT JOIN service_revisions sr ON sr.id = r.service_revision_id "
        "WHERE r.id = $1", session_id);
    if (rows.empty() || (!user_id.empty() && std::string(rows[0]["user_id"].c_str()) != user_id)) {
      throw ApiError(404, "ROLEPLAY_SESSION_NOT_FOUND", "患者模拟会话不存在");
    }
    return roleplaySessionJson(rows[0]);
  }

  std::shared_ptr<DatabasePool> database_pool_;
};

class AiJobQueue {
 public:
  explicit AiJobQueue(std::shared_ptr<DatabasePool> database_pool)
      : database_pool_(std::move(database_pool)) {}

  std::optional<AiJob> claim(const std::string& worker_id) const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
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

  bool renewLease(const AiJob& job) const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    const auto renewed = tx.exec_params(R"(
      UPDATE ai_jobs SET lease_until = NOW() + ($5 * INTERVAL '1 second'), updated_at = NOW()
      WHERE id = $1 AND status = 'running' AND target_id = $2
        AND generation = $3 AND attempts = $4 AND lease_until > NOW()
      RETURNING id
    )", job.id, job.target_id, job.generation, job.attempt, kJobLeaseSeconds);
    tx.commit();
    return !renewed.empty();
  }

  void fail(const AiJob& job, const std::string& error_type,
            const std::string& error_message, bool retryable) const {
    auto connection = database_pool_->acquire();
    pqxx::work tx(connection.get());
    if (!lockAiJobTarget(tx, job.type, job.target_id)) return;
    const auto rows = tx.exec_params(
        "SELECT attempts, max_attempts, job_type, target_id FROM ai_jobs "
        "WHERE id = $1 AND status = 'running' AND generation = $2 AND attempts = $3 "
        "AND job_type = $4 AND target_id = $5 FOR UPDATE",
        job.id, job.generation, job.attempt, job.type, job.target_id);
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
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
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
        INSERT INTO evaluations(session_id, status, report, error_type, updated_at)
        VALUES ($1, 'failed', NULL, $2, NOW())
        ON CONFLICT (session_id) DO UPDATE SET status = 'failed', report = NULL,
          error_type = EXCLUDED.error_type, updated_at = NOW()
      )", target_id, error_type);
      tx.exec_params(R"(
        UPDATE sessions SET evaluation_status = 'failed', total_score = NULL, updated_at = NOW()
        WHERE id = $1
      )", target_id);
    } else {
      tx.exec_params(R"(
        INSERT INTO roleplay_summaries(session_id, status, summary, error_type, updated_at)
        VALUES ($1, 'failed', NULL, $2, NOW())
        ON CONFLICT (session_id) DO UPDATE SET status = 'failed', summary = NULL,
          error_type = EXCLUDED.error_type, updated_at = NOW()
      )", target_id, error_type);
      tx.exec_params("UPDATE roleplay_sessions SET updated_at = NOW() WHERE id = $1", target_id);
    }
  }

  static void markExhaustedLeases(pqxx::transaction_base& tx) {
    const auto exhausted = tx.exec(R"(
      SELECT id, job_type, target_id, generation, attempts FROM ai_jobs
      WHERE status = 'running' AND lease_until <= NOW() AND attempts >= max_attempts
      ORDER BY id LIMIT 100
    )");
    for (const auto& candidate : exhausted) {
      // Never hold a job lock while waiting for its session. Skip busy targets
      // so one active poll cannot stall reclamation of unrelated expired jobs.
      if (!lockAiJobTarget(tx, candidate["job_type"].c_str(),
                           candidate["target_id"].c_str(), true)) continue;
      const auto locked = tx.exec_params(R"(
        SELECT id, job_type, target_id, generation, attempts FROM ai_jobs
        WHERE id = $1 AND job_type = $2 AND target_id = $3
          AND status = 'running' AND lease_until <= NOW() AND attempts >= max_attempts
        FOR UPDATE SKIP LOCKED
      )", candidate["id"].c_str(), candidate["job_type"].c_str(), candidate["target_id"].c_str());
      if (locked.empty()) continue;
      const auto& row = locked[0];
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

  std::shared_ptr<DatabasePool> database_pool_;
};
