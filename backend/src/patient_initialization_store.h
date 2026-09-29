#pragma once

// Included after the AI job primitives. Model work is always outside these transactions.
inline void requireTrainingInitialized(pqxx::transaction_base& tx, const std::string& id) {
  const auto rows = tx.exec_params(R"(
    SELECT s.context_version, c.initialization_status FROM sessions s
    LEFT JOIN training_contexts c ON c.session_type = 'training' AND c.session_id = s.id
    WHERE s.id = $1
  )", id);
  if (rows.empty() || rows[0]["context_version"].as<int>() < 2) return;
  if (rows[0]["initialization_status"].is_null())
    throw ApiError(503, "RAG_UNAVAILABLE", "会话初始化上下文缺失");
  const auto status = std::string(rows[0]["initialization_status"].c_str());
  if (status != "ready")
    throw ApiError(409, status == "failed" ? "PATIENT_INITIALIZATION_FAILED"
                                         : "PATIENT_INITIALIZATION_PENDING",
                   "患者初始化尚未完成，请查询初始化状态");
}

inline json readPatientProfiles(pqxx::transaction_base& tx,const std::string& id) {
  const auto rows=tx.exec_params(
      "SELECT c.*, s.patient_state AS live_state FROM training_contexts c JOIN sessions s ON s.id=c.session_id"
      " WHERE c.session_type='training' AND c.session_id=$1",id);
  if(rows.empty()) throw ApiError(503,"RAG_UNAVAILABLE","患者快照不存在");
  const auto& r=rows[0];
  const auto manifest=json::parse(r["manifest"].c_str());
  if(oral_training::rag::manifestHash(r["service_revision_id"].c_str(),manifest,r["training_scope"].c_str())
      !=r["manifest_hash"].c_str()) throw ApiError(503,"RAG_UNAVAILABLE","患者快照摘要不匹配");
  return {{"context",{{"contextId",r["id"].c_str()},{"serviceId",r["service_id"].c_str()},
      {"serviceRevisionId",r["service_revision_id"].c_str()},{"manifest",manifest},
      {"manifestHash",r["manifest_hash"].c_str()},{"knowledgeAsOf",r["knowledge_as_of"].c_str()},
      {"trainingScope",r["training_scope"].c_str()}}},
      {"publicProfile",r["public_profile"].is_null()?json::object():json::parse(r["public_profile"].c_str())},
      {"privateProfile",r["private_profile"].is_null()?json::object():json::parse(r["private_profile"].c_str())},
      {"state",json::parse(r["live_state"].c_str())}};
}

class PatientInitializationStore {
 public:
  explicit PatientInitializationStore(std::shared_ptr<DatabasePool> pool, bool allow_new = true)
      : pool_(std::move(pool)), allow_new_(allow_new) {}

  std::string create(const std::string& user, const std::string& scenario,
                     const std::string& service, const std::string& client) const {
    if (client.empty() || client.size() > 100 || service.empty() || service.size() > 120 ||
        scenario.empty() || scenario.size() > 120)
      throw ApiError(400, "INVALID_ARGUMENT", "服务、场景或 clientSessionId 格式无效");
    auto connection = pool_->acquire();
    pqxx::work tx(connection.get());
    // Serialize creation for this user; duplicate HTTP requests wait and replay the committed row.
    if (tx.exec_params("SELECT id FROM users WHERE id = $1 FOR UPDATE", user).empty())
      throw ApiError(404, "USER_NOT_FOUND", "用户不存在");
    const auto replay = tx.exec_params(
        "SELECT id, scenario_id, service_id FROM sessions WHERE user_id = $1 AND client_session_id = $2",
        user, client);
    if (!replay.empty()) {
      if (std::string(replay[0]["scenario_id"].c_str()) != scenario ||
          replay[0]["service_id"].is_null() || std::string(replay[0]["service_id"].c_str()) != service)
        throw ApiError(409, "IDEMPOTENCY_CONFLICT", "clientSessionId 对应不同会话参数");
      return replay[0]["id"].c_str();
    }
    if (!allow_new_) throw ApiError(503, "RAG_NEW_SESSIONS_PAUSED", "服务训练暂停新建，已有会话仍可继续");
    // Wait for any publisher before taking the statement snapshot / knowledgeAsOf timestamp.
    // Otherwise READ COMMITTED row-lock rechecks could see a newer service pointer.
    tx.exec_params("SELECT id FROM clinic_services WHERE id = $1 FOR SHARE", service);
    // The service pointer and complete knowledge manifest are selected in one MVCC statement.
    const auto snapshot = tx.exec_params(R"(
      SELECT sc.name, sc.max_rounds, s.current_revision_id,
        statement_timestamp()::text AS knowledge_as_of,
        COALESCE((SELECT jsonb_agg(r.id ORDER BY r.id)
          FROM knowledge_entries e JOIN knowledge_revisions r ON r.id = e.current_revision_id
          WHERE e.status = 'active' AND r.metadata->>'trainingScope' = 'demo'
        AND COALESCE(NULLIF(r.metadata->>'effectiveFrom', ''), '0001-01-01') <= to_char(statement_timestamp() AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD')
        AND COALESCE(NULLIF(r.metadata->>'effectiveUntil', ''), '9999-12-31') >= to_char(statement_timestamp() AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD')
            AND (e.scope = 'general' OR e.service_id = s.id)), '[]'::jsonb) AS manifest
      FROM clinic_services s
      JOIN service_revisions sr ON sr.id = s.current_revision_id
      JOIN service_scenarios ss ON ss.service_id = s.id AND ss.scenario_id = $2
      JOIN scenarios sc ON sc.id = ss.scenario_id
      WHERE s.id = $1 AND s.status = 'active' AND sc.is_active AND NOT sc.is_template
        AND COALESCE(NULLIF((sr.payload->'price')->>'validFrom', ''), '0001-01-01') <= to_char(statement_timestamp() AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD')
        AND COALESCE(NULLIF((sr.payload->'price')->>'validUntil', ''), '9999-12-31') >= to_char(statement_timestamp() AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD')
      FOR SHARE OF s, sc
    )", service, scenario);
    if (snapshot.empty())
      throw ApiError(409, "SERVICE_SCENARIO_MISMATCH", "服务不可用或不支持当前场景");
    const auto& row = snapshot[0];
    const auto id = makeId("sess");
    const auto revision = std::string(row["current_revision_id"].c_str());
    const auto manifest = json::parse(row["manifest"].c_str());
    const auto hash = oral_training::rag::manifestHash(revision, manifest, "demo");
    const auto inserted = tx.exec_params(R"(
      INSERT INTO sessions(id, user_id, scenario_id, scenario_name, status, current_round,
        max_rounds, patient_state, service_id, service_revision_id, client_session_id, context_version)
      VALUES ($1,$2,$3,$4,'in_progress',0,$5,'{}'::jsonb,$6,$7,$8,2)
      ON CONFLICT DO NOTHING RETURNING id
    )", id, user, scenario, row["name"].c_str(), clampInt(row["max_rounds"].as<int>(),1,10),
        service, revision, client);
    if (inserted.empty()) throw ApiError(409, "SESSION_IN_PROGRESS", "该服务和场景已有进行中的训练");
    tx.exec_params(R"(
      INSERT INTO training_contexts(id, session_type, session_id, service_id, service_revision_id,
        knowledge_as_of, manifest, manifest_hash, training_scope)
      VALUES ($1,'training',$2,$3,$4,$5::timestamptz,$6::jsonb,$7,'demo')
    )", makeId("ctx"), id, service, revision, row["knowledge_as_of"].c_str(), manifest.dump(), hash);
    enqueueAiJob(tx, "patient_initialization", id);
    tx.commit();
    return id;
  }

  json get(const std::string& user, const std::string& id) const {
    auto connection = pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT c.*, s.status AS session_status FROM sessions s
      JOIN training_contexts c ON c.session_id = s.id AND c.session_type = 'training'
      WHERE s.id = $1 AND s.user_id = $2
    )", id, user);
    if (rows.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "初始化会话不存在或无权访问");
    const auto& row = rows[0];
    std::string status = row["initialization_status"].c_str();
    // Queue-only claim does not acquire a session lock. Project generating from its live state.
    const auto job = tx.exec_params(
        "SELECT status, generation FROM ai_jobs WHERE dedupe_key = $1",
        aiJobDedupeKey(AiJobKind::PatientInitialization, id));
    if (status == "pending" && !job.empty() && std::string(job[0]["status"].c_str()) == "running")
      status = "generating";
    return {{"sessionId", id}, {"status", status},
            {"generation", row["initialization_generation"].as<int>()},
            {"retryable", status == "failed" && std::string(row["session_status"].c_str()) == "in_progress" &&
                row["initialization_generation"].as<int>() < 100},
            {"errorType", row["initialization_error"].is_null() ? json(nullptr) : json(row["initialization_error"].c_str())},
            {"manifestHash", row["manifest_hash"].c_str()},
            {"publicProfile", status == "ready" && !row["public_profile"].is_null()
                ? json::parse(row["public_profile"].c_str()) : json(nullptr)}};
  }

  void retry(const std::string& user, const std::string& id) const {
    auto connection = pool_->acquire();
    pqxx::work tx(connection.get());
    const auto session = tx.exec_params(
        "SELECT status FROM sessions WHERE id = $1 AND user_id = $2 FOR UPDATE", id, user);
    if (session.empty()) throw ApiError(404, "SESSION_NOT_FOUND", "会话不存在或无权访问");
    if (std::string(session[0]["status"].c_str()) != "in_progress")
      throw ApiError(409, "SESSION_FINISHED", "已结束的会话不能重试初始化");
    const auto context = tx.exec_params(
        "SELECT initialization_status FROM training_contexts WHERE session_type = 'training' AND session_id = $1 FOR UPDATE", id);
    if (context.empty()) throw ApiError(404, "INITIALIZATION_NOT_FOUND", "该会话不使用异步初始化");
    const auto status = std::string(context[0]["initialization_status"].c_str());
    if (status != "failed") throw ApiError(409, "INITIALIZATION_NOT_RETRYABLE", "只有失败的初始化可以重试");
    enqueueAiJob(tx, "patient_initialization", id, true);
    tx.exec_params(R"(
      UPDATE training_contexts SET initialization_status = 'pending', initialization_error = NULL,
        initialization_generation = (SELECT generation FROM ai_jobs WHERE dedupe_key = $2)
      WHERE session_type = 'training' AND session_id = $1
    )", id, aiJobDedupeKey(AiJobKind::PatientInitialization, id));
    tx.commit();
  }

  json begin(const AiJob& job) const {
    auto connection = pool_->acquire();
    pqxx::work tx(connection.get());
    own(tx, job);
    const auto rows = tx.exec_params(R"(
      UPDATE training_contexts SET initialization_status = 'generating', initialization_error = NULL
      WHERE session_type = 'training' AND session_id = $1
        AND initialization_generation = $2 AND initialization_status IN ('pending','generating')
      RETURNING *
    )", job.target_id, job.generation);
    if (rows.empty()) throw ApiError(409, "JOB_LEASE_LOST", "初始化版本已失效");
    const auto& row = rows[0];
    const auto manifest = json::parse(row["manifest"].c_str());
    if (oral_training::rag::manifestHash(row["service_revision_id"].c_str(), manifest,
          row["training_scope"].c_str()) != row["manifest_hash"].c_str())
      throw ApiError(503, "RAG_UNAVAILABLE", "初始化快照摘要不匹配");
    json context = {{"contextId", row["id"].c_str()}, {"serviceId", row["service_id"].c_str()},
        {"serviceRevisionId", row["service_revision_id"].c_str()}, {"manifest", manifest},
        {"manifestHash", row["manifest_hash"].c_str()}, {"trainingScope", row["training_scope"].c_str()},
        {"knowledgeAsOf", row["knowledge_as_of"].c_str()}};
    tx.commit();
    return context;
  }

  json profiles(const std::string& id) const {
    auto connection=pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    return readPatientProfiles(tx,id);
  }

  void save(const AiJob& job, json output, const std::string& model_version) const {
    // N03 supplies grounded generation. This boundary only accepts structured, bounded output.
    if (!output.is_object() || !output.contains("publicProfile") || !output["publicProfile"].is_object() ||
        !output.contains("privateProfile") || !output["privateProfile"].is_object() ||
        !output.contains("patientState") || !output["patientState"].is_object() ||
        jsonString(output, "opening").empty() || utf8Length(jsonString(output,"opening")) > 2000 ||
        output.dump().size() > 32000)
      throw ApiError(502, "PATIENT_INITIALIZATION_INVALID", "患者初始化输出格式无效");
    json public_profile = json::object();
    for (const auto* key : {"displayName", "ageRange", "initialEmotion"}) {
      const auto value = jsonString(output["publicProfile"], key);
      if (value.empty() || utf8Length(value) > 80)
        throw ApiError(502, "PATIENT_INITIALIZATION_INVALID", "公开画像格式无效");
      public_profile[key] = value;
    }
    auto connection = pool_->acquire();
    pqxx::work tx(connection.get());
    own(tx, job);
    if(output.contains("selection")) {
      const auto context=readPatientProfiles(tx,job.target_id)["context"];
      const auto evidence=output.at("evidenceBundle");
      output=oral_training::rag::initializeGroundedPatient(output["selection"],context,evidence);
      public_profile=output["publicProfile"];
      tx.exec_params(R"(
        INSERT INTO rag_traces(id,context_id,purpose,round,attempt_token,evidence_json,model_version,is_public)
        VALUES ($1,$2,'patient_initialization',0,$3,$4::jsonb,$5,FALSE)
      )",makeId("trace"),context["contextId"].get<std::string>(),
          std::to_string(job.generation)+":"+std::to_string(job.attempt),evidence.dump(),model_version);
    }
    const auto updated = tx.exec_params(R"(
      UPDATE training_contexts SET initialization_status = 'ready', initialization_error = NULL,
        public_profile = $3::jsonb, private_profile = $4::jsonb, patient_state = $5::jsonb,
        initialized_at = NOW(), initialization_model_version = $6
      WHERE session_type = 'training' AND session_id = $1 AND initialization_generation = $2
        AND initialization_status = 'generating' RETURNING id
    )", job.target_id, job.generation, public_profile.dump(), output["privateProfile"].dump(),
        output["patientState"].dump(), model_version);
    if (updated.empty()) throw ApiError(409,"JOB_LEASE_LOST","初始化状态已失效");
    tx.exec_params("UPDATE sessions SET patient_state = $2::jsonb, updated_at = NOW() WHERE id = $1",
                   job.target_id, output["patientState"].dump());
    tx.exec_params(R"(
      INSERT INTO messages(id, session_id, role, content, round, emotion)
      VALUES ($1,$2,'patient',$3,0,$4)
    )", makeId("msg"), job.target_id, jsonString(output,"opening"), jsonString(public_profile,"initialEmotion"));
    tx.exec_params("UPDATE ai_job_attempts SET status = 'succeeded', finished_at = NOW()"
        " WHERE job_id = $1 AND generation = $2 AND attempt_number = $3",
        job.id, job.generation, job.attempt);
    tx.exec_params("UPDATE ai_jobs SET status = 'succeeded', lease_until = NULL, worker_id = NULL,"
        " last_error = NULL, finished_at = NOW(), updated_at = NOW() WHERE id = $1", job.id);
    tx.commit();
  }

 private:
  static void own(pqxx::transaction_base& tx, const AiJob& job) {
    if (job.type != "patient_initialization" || !lockAiJobTarget(tx, job.type, job.target_id))
      throw ApiError(409, "JOB_LEASE_LOST", "初始化任务目标无效");
    const auto session = tx.exec_params("SELECT status FROM sessions WHERE id = $1", job.target_id);
    if (std::string(session[0]["status"].c_str()) != "in_progress")
      throw ApiError(409, "SESSION_ABANDONED", "会话已结束");
    if (tx.exec_params(R"(
      SELECT id FROM ai_jobs WHERE id = $1 AND job_type = 'patient_initialization'
        AND target_id = $2 AND generation = $3 AND attempts = $4
        AND status = 'running' AND lease_until > clock_timestamp() FOR UPDATE
    )", job.id, job.target_id, job.generation, job.attempt).empty())
      throw ApiError(409, "JOB_LEASE_LOST", "初始化任务租约已失效");
  }
  std::shared_ptr<DatabasePool> pool_;
  bool allow_new_;
};
