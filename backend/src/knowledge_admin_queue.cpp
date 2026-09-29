#include "knowledge_admin_queue.h"

#include "knowledge_store.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace oral_training::knowledge {
namespace {

constexpr int kLeaseSeconds = 180;

std::string randomId(const std::string& prefix) {
  std::array<unsigned char, 12> bytes{};
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    throw std::runtime_error("secure random generation failed");
  }
  std::ostringstream output;
  output << prefix << '-';
  for (const auto value : bytes) {
    output << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(value);
  }
  return output.str();
}

void requireAdmin(pqxx::transaction_base& tx, const std::string& actor_id) {
  const auto rows = tx.exec_params(
      "SELECT 1 FROM users WHERE id = $1 AND role = 'admin' AND status = 'active'", actor_id);
  if (rows.empty()) throw KnowledgeStoreError(403, "ROLE_FORBIDDEN", "仅管理员可管理生成任务");
}

/* ── 场景骨架草稿（迁移 030）的请求校验 ────────────────────────────────────
   主管只提供「分类 + 想覆盖的顾虑 + 难度」，教学骨架的其余部分交给模型。
   这里卡住的是**输入**；模型**产出**的字段约束在落库时由调用方注入的
   ScenarioDraftWriter 统一执行（最终走 ReliableDatabase 与手工建场景同一套校验），
   所以这里不必、也不应该复制长度/条数细则。
   长度上限按字节给宽松上界，精确口径仍在入库校验。 */
const std::vector<std::string>& sceneCategories() {
  static const std::vector<std::string> categories = {
      "consultation", "price_negotiation", "complaint_handling", "recommendation"};
  return categories;
}

void validateScenarioDraftRequest(const json& request) {
  const auto category = request.value("category", std::string());
  const auto& categories = sceneCategories();
  if (std::find(categories.begin(), categories.end(), category) == categories.end()) {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "场景分类无效");
  }
  const auto difficulty = request.value("difficulty", std::string());
  if (difficulty != "basic" && difficulty != "advanced") {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "难度只能是 basic 或 advanced");
  }
  if (!request.contains("concerns") || !request["concerns"].is_array() ||
      request["concerns"].empty()) {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "请至少填写 1 条想覆盖的顾虑");
  }
  if (request["concerns"].size() > 5) {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "想覆盖的顾虑最多 5 条");
  }
  for (const auto& item : request["concerns"]) {
    if (!item.is_string() || item.get<std::string>().empty() ||
        item.get<std::string>().size() > 180) {
      throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "想覆盖的顾虑每条需 1-60 个字");
    }
  }
  if (request.value("name", std::string()).size() > 90) {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "场景名称需 30 个字以内");
  }
}

/* 占位场景的 id。必须满足场景 id 白名单（小写字母/数字/连字符、2-60 位），
   用随机十六进制而不是毫秒时间戳：骨架任务可能被连点两次，时间戳会撞 id。
   randomId("ai") 形如 `ai-<24 位十六进制>`，整体拼成 `sc-ai-<hex>`（30 位）。
   `sc-ai-` 这个前缀同时是清理脚本/测试识别「AI 生成」的锚点，别改成别的形状。 */
std::string makeScenarioDraftId() {
  return "sc-" + randomId("ai");
}

/* 排序号动态分配：排除骨架模板（模板占 9xx 保留区，写死或直接 MAX+1 都可能
   撞上模板的保留号）。与迁移 025 里新增场景用的是同一条规则。 */
int nextScenarioSortOrder(pqxx::transaction_base& tx) {
  const auto row = tx.exec(R"(
    SELECT COALESCE(MAX(sort_order) FILTER (WHERE NOT is_template), 0) + 1 AS next_order
    FROM scenarios
  )")[0];
  return row["next_order"].as<int>();
}

json parseJsonField(const pqxx::field& field) {
  return field.is_null() ? json(nullptr) : json::parse(field.c_str());
}

json jobJson(const pqxx::row& row) {
  json result = {
      {"jobId", row["id"].c_str()}, {"kind", row["kind"].c_str()},
      {"draftId", row["draft_id"].c_str()}, {"generation", row["generation"].as<int>()},
      {"baseDraftVersion", row["base_draft_version"].as<int>()},
      {"status", row["status"].c_str()}, {"attempts", row["attempts"].as<int>()},
      {"maxAttempts", row["max_attempts"].as<int>()},
      {"promptVersion", row["prompt_version"].c_str()},
      {"modelVersion", row["model_version"].is_null()
          ? json(nullptr) : json(row["model_version"].c_str())},
      {"result", parseJsonField(row["result"])},
      {"resultApplied", row["result_applied"].is_null()
          ? json(nullptr) : json(row["result_applied"].as<bool>())},
      {"errorType", row["error_type"].is_null()
          ? json(nullptr) : json(row["error_type"].c_str())},
      {"errorMessage", row["error_message"].is_null()
          ? json(nullptr) : json(row["error_message"].c_str())},
      {"createdAt", row["created_at"].c_str()}, {"updatedAt", row["updated_at"].c_str()},
      {"finishedAt", row["finished_at"].is_null()
          ? json(nullptr) : json(row["finished_at"].c_str())},
  };
  return result;
}

pqxx::result selectJob(pqxx::transaction_base& tx, const std::string& job_id) {
  return tx.exec_params(R"(
    SELECT id, kind, draft_id, generation, base_draft_version, status, attempts, max_attempts,
      prompt_version, model_version, result, result_applied, error_type, error_message,
      created_at, updated_at, finished_at
    FROM knowledge_admin_jobs WHERE id = $1
  )", job_id);
}

void writeAudit(pqxx::transaction_base& tx, const std::string& actor_id,
                const std::string& action, const std::string& job_id,
                const std::string& request_id) {
  tx.exec_params(R"(
    INSERT INTO knowledge_audit_events
      (id, actor_id, action, entity_type, entity_id, request_id)
    VALUES ($1, $2, $3, 'generation_job', $4, NULLIF($5, ''))
  )", randomId("audit"), actor_id, action, job_id, request_id);
}

}  // namespace

json KnowledgeAdminQueue::create(const std::string& actor_id, const std::string& kind,
                                 const std::string& draft_id, const json& request,
                                 const std::string& idempotency_key,
                                 const std::string& request_digest,
                                 const std::string& request_id) const {
  /* 显式分发：未知 kind 在**入库之前**就被拒绝。原实现是「不是 service_draft
     就当 knowledge_draft」，未知类型会拿 draft_id 去锁 knowledge_drafts，
     然后在成功时写错表——不报错，只是数据落到别处（R09 风险表第一条）。 */
  const auto target = parseGenerationTarget(kind);
  if (!target.has_value()) {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "生成任务 kind 无效");
  }
  /* scenario_draft 的 draft_id 由服务端生成：草稿行（scenarios 占位行）在本事务里
     才创建，调用方不可能预先给出 id。因此「draft_id 非空」这条通用规则不能套它。 */
  const bool server_side_draft = *target == GenerationTarget::ScenarioDraft;
  if ((!server_side_draft && draft_id.empty()) || draft_id.size() > 200 ||
      !request.is_object() || idempotency_key.empty() || idempotency_key.size() > 200 ||
      request_digest.size() != 64) {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "生成任务参数无效");
  }
  if (request.contains("count") && !request["count"].is_number_integer()) {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "生成数量必须是整数");
  }
  const auto count = request.value("count", 1);
  if (count != 1) {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "每个生成任务仅允许生成 1 份候选草稿");
  }
  if (request.contains("brief") && !request["brief"].is_string()) {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "生成说明必须是字符串");
  }
  const auto brief = request.value("brief", std::string());
  if (brief.size() > 2000) {
    throw KnowledgeStoreError(400, "INVALID_ARGUMENT", "生成说明最长 2000 个字符");
  }

  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto replay = tx.exec_params(R"(
    SELECT id, kind, draft_id, request_digest FROM knowledge_admin_jobs
    WHERE created_by = $1 AND idempotency_key = $2
  )", actor_id, idempotency_key);
  if (!replay.empty()) {
    /* 重放校验要按「调用方真的提供了什么」来判断。scenario_draft 的 draft_id 是
       服务端生成的，重放请求里必然是空的——拿库里那个生成的 id 去比，会把
       一个合法的重放判成 409 冲突。kind 与 request_digest 已经足够区分请求。 */
    const bool same_target = server_side_draft ||
        std::string(replay[0]["draft_id"].c_str()) == draft_id;
    if (std::string(replay[0]["kind"].c_str()) != kind || !same_target ||
        std::string(replay[0]["request_digest"].c_str()) != request_digest) {
      throw KnowledgeStoreError(409, "IDEMPOTENCY_CONFLICT", "幂等键对应不同生成请求");
    }
    return jobJson(selectJob(tx, replay[0]["id"].c_str())[0]);
  }

  pqxx::result draft_rows;
  json model_input = request;
  auto resolved_draft_id = draft_id;
  int base_version = 1;
  auto prompt_version = std::string("knowledge-draft-v1");
  // 仅 scenario_draft 用：本事务预建的占位场景。job_id 先算出来，好让占位行
  // 直接带上 generation_id，省掉一次 UPDATE，也让「这个占位行归哪个任务」在
  // 插入那一刻就是原子的。
  auto placeholder_insert = json::object();
  auto job_id = std::string();

  if (*target == GenerationTarget::ServiceDraft) {
    draft_rows = tx.exec_params(
        "SELECT id, draft_version, payload FROM service_drafts WHERE id = $1 FOR UPDATE", draft_id);
    if (draft_rows.empty()) throw KnowledgeStoreError(404, "SERVICE_NOT_FOUND", "服务草稿不存在");
    model_input["currentDraft"] = parseJsonField(draft_rows[0]["payload"]);
    base_version = draft_rows[0]["draft_version"].as<int>();
    prompt_version = "service-draft-v1";
  } else if (*target == GenerationTarget::KnowledgeDraft) {
    draft_rows = tx.exec_params(R"(
      SELECT d.id, d.draft_version, d.title, d.body, d.metadata, e.topic, e.scope
      FROM knowledge_drafts d JOIN knowledge_entries e ON e.id = d.entry_id
      WHERE d.id = $1 FOR UPDATE OF d
    )", draft_id);
    if (draft_rows.empty()) throw KnowledgeStoreError(404, "KNOWLEDGE_NOT_FOUND", "知识草稿不存在");
    model_input["currentDraft"] = {
        {"title", draft_rows[0]["title"].c_str()}, {"body", draft_rows[0]["body"].c_str()},
        {"metadata", parseJsonField(draft_rows[0]["metadata"])},
        {"topic", draft_rows[0]["topic"].c_str()}, {"scope", draft_rows[0]["scope"].c_str()},
    };
    base_version = draft_rows[0]["draft_version"].as<int>();
  } else {
    /* 场景骨架（迁移 030）：与两类草稿相反——目标行还不存在，由本事务创建。
       为什么要预建一条占位场景，而不是等生成成功再 INSERT：
         · 任务表要求 draft_id 非空且稳定，生成期间主管端还要能看到「正在生成」；
         · 生成失败（模型连错 3 次）时留下一条已下线的空壳，主管一眼看得出
           「这条没生成出来」，比什么都不留更容易排查；
         · 与 service_drafts / knowledge_drafts 的形态一致：目标行先存在，
           AI 只负责往里填内容。 */
    validateScenarioDraftRequest(request);
    job_id = randomId("knowledge-job");
    resolved_draft_id = makeScenarioDraftId();
    placeholder_insert = {{"category", request.value("category", std::string())},
                          {"difficulty", request.value("difficulty", std::string())},
                          {"sortOrder", nextScenarioSortOrder(tx)}};
    prompt_version = "scenario-draft-v1";
    /* 占位内容刻意写成「明显不是成品」：万一主管在生成完成前打开它，
       看到的是「AI 生成中」而不是一段半截的假场景。 */
    const auto draft_name = request.value("name", std::string());
    const auto brief = request.value("brief", std::string());
    tx.exec_params(R"(
      INSERT INTO scenarios(id, name, category, summary, difficulty, focus, dimension_weights,
        patient_profile, hidden_config, roleplay_config, max_rounds, sort_order, is_active,
        is_template, difficulty_tiers, ai_draft, generation_id)
      VALUES ($1, $2, $3, $4, $5, '[]'::jsonb, '{}'::jsonb, '{}'::jsonb, '{}'::jsonb,
              '{"suggestedQuestions":[],"serviceGuidance":[]}'::jsonb, 10, $6, FALSE, FALSE,
              '{}'::jsonb, TRUE, $7)
    )", resolved_draft_id,
        draft_name.empty() ? std::string("AI 骨架生成中…") : draft_name,
        placeholder_insert["category"].get<std::string>(),
        brief.empty() ? std::string("AI 正在生成教学骨架，完成后请补充机构红线") : brief,
        placeholder_insert["difficulty"].get<std::string>(),
        placeholder_insert["sortOrder"].get<int>(), job_id);
  }
  model_input["contentPolicy"] = "synthetic_unverified_only";
  if (job_id.empty()) job_id = randomId("knowledge-job");
  tx.exec_params(R"(
    INSERT INTO knowledge_admin_jobs
      (id, kind, draft_id, base_draft_version, idempotency_key, request_digest, request,
       prompt_version, created_by)
    VALUES ($1, $2, $3, $4, $5, $6, $7::jsonb, $8, $9)
  )", job_id, kind, resolved_draft_id, base_version, idempotency_key, request_digest,
      model_input.dump(), prompt_version, actor_id);
  if (*target == GenerationTarget::ServiceDraft) {
    tx.exec_params("UPDATE service_drafts SET generation_id = $2 WHERE id = $1",
                   resolved_draft_id, job_id);
  } else if (*target == GenerationTarget::KnowledgeDraft) {
    tx.exec_params("UPDATE knowledge_drafts SET generation_id = $2 WHERE id = $1",
                   resolved_draft_id, job_id);
  }
  // scenario_draft 不需要这一步：占位行插进去时就带着 generation_id。
  writeAudit(tx, actor_id, "generation_requested", job_id, request_id);
  const auto result = jobJson(selectJob(tx, job_id)[0]);
  tx.commit();
  return result;
}

json KnowledgeAdminQueue::get(const std::string& actor_id, const std::string& job_id) const {
  auto connection = database_pool_->acquire();
  pqxx::read_transaction tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto rows = selectJob(tx, job_id);
  if (rows.empty()) throw KnowledgeStoreError(404, "GENERATION_JOB_NOT_FOUND", "生成任务不存在");
  return jobJson(rows[0]);
}

json KnowledgeAdminQueue::retry(const std::string& actor_id, const std::string& job_id,
                                const std::string& request_id) const {
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto rows = tx.exec_params(R"(
    SELECT id, kind, draft_id, generation, status, request
    FROM knowledge_admin_jobs WHERE id = $1 FOR UPDATE
  )", job_id);
  if (rows.empty()) throw KnowledgeStoreError(404, "GENERATION_JOB_NOT_FOUND", "生成任务不存在");
  /* 类型从库里读出来的那一刻就显式解析：未知类型拒绝重试，不再「猜成
     knowledge_draft」去刷新输入（那会把 A 的输入喂给 B 的草稿）。 */
  const auto target = parseGenerationTarget(std::string(rows[0]["kind"].c_str()));
  if (!target.has_value()) {
    throw KnowledgeStoreError(409, "GENERATION_JOB_STATE_CONFLICT", "生成任务类型无效");
  }
  if (std::string(rows[0]["status"].c_str()) != "dead") {
    throw KnowledgeStoreError(409, "GENERATION_JOB_STATE_CONFLICT", "仅失败任务可以重试");
  }
  if (rows[0]["generation"].as<int>() >= 100) {
    throw KnowledgeStoreError(409, "GENERATION_EXHAUSTED", "生成任务已达到重试上限");
  }
  const auto kind = std::string(rows[0]["kind"].c_str());
  const auto draft_id = std::string(rows[0]["draft_id"].c_str());
  pqxx::result draft;
  auto updated_request = parseJsonField(rows[0]["request"]);
  int base_version = 1;
  if (*target == GenerationTarget::ServiceDraft) {
    draft = tx.exec_params(
        "SELECT draft_version, payload FROM service_drafts WHERE id = $1 FOR UPDATE", draft_id);
    if (!draft.empty()) {
      updated_request["currentDraft"] = parseJsonField(draft[0]["payload"]);
      base_version = draft[0]["draft_version"].as<int>();
    }
  } else if (*target == GenerationTarget::KnowledgeDraft) {
    draft = tx.exec_params(R"(
      SELECT d.draft_version, d.title, d.body, d.metadata, e.topic, e.scope
      FROM knowledge_drafts d JOIN knowledge_entries e ON e.id = d.entry_id
      WHERE d.id = $1 FOR UPDATE OF d
    )", draft_id);
    if (!draft.empty()) {
      updated_request["currentDraft"] = {
          {"title", draft[0]["title"].c_str()}, {"body", draft[0]["body"].c_str()},
          {"metadata", parseJsonField(draft[0]["metadata"])},
          {"topic", draft[0]["topic"].c_str()}, {"scope", draft[0]["scope"].c_str()},
      };
      base_version = draft[0]["draft_version"].as<int>();
    }
  } else {
    /* 场景骨架：重新生成必须以**当前**草稿为输入，否则主管在编辑器里改过的内容
       会被一次重试悄悄丢掉。已上架的骨架则不允许重试——重试成功会强制下线
       （AI 产出未经审阅不能自动上架），那等于把一条已经发布的场景撤下来。 */
    draft = tx.exec_params(R"(
      SELECT sort_order, is_active, name, category, summary, difficulty, focus,
        dimension_weights, patient_profile, hidden_config, roleplay_config, max_rounds
      FROM scenarios WHERE id = $1 FOR UPDATE
    )", draft_id);
    if (!draft.empty()) {
      if (draft[0]["is_active"].as<bool>()) {
        throw KnowledgeStoreError(409, "SCENARIO_PUBLISHED",
                                  "这条场景已经上架，重新生成会把它下线；请先下线再重试");
      }
      updated_request["currentDraft"] = {
          {"name", draft[0]["name"].c_str()}, {"category", draft[0]["category"].c_str()},
          {"summary", draft[0]["summary"].c_str()},
          {"difficulty", draft[0]["difficulty"].c_str()},
          {"focus", parseJsonField(draft[0]["focus"])},
          {"dimensionWeights", parseJsonField(draft[0]["dimension_weights"])},
          {"patientProfile", parseJsonField(draft[0]["patient_profile"])},
          {"hiddenConfig", parseJsonField(draft[0]["hidden_config"])},
          {"roleplayConfig", parseJsonField(draft[0]["roleplay_config"])},
          {"maxRounds", draft[0]["max_rounds"].as<int>()},
      };
      base_version = 1;
    }
  }
  if (draft.empty()) throw KnowledgeStoreError(404, "DRAFT_NOT_FOUND", "目标草稿不存在");
  tx.exec_params(R"(
    UPDATE knowledge_admin_jobs SET generation = generation + 1,
      base_draft_version = $2, status = 'pending', lease_until = NULL, worker_id = NULL,
      attempt_token = NULL, attempts = 0, available_at = NOW(), result = NULL,
      result_applied = NULL, model_version = NULL, error_type = NULL, error_message = NULL,
      request = $3::jsonb, updated_at = NOW(), finished_at = NULL WHERE id = $1
  )", job_id, base_version, updated_request.dump());
  if (*target == GenerationTarget::ServiceDraft) {
    tx.exec_params("UPDATE service_drafts SET generation_id = $2 WHERE id = $1", draft_id, job_id);
  } else if (*target == GenerationTarget::KnowledgeDraft) {
    tx.exec_params("UPDATE knowledge_drafts SET generation_id = $2 WHERE id = $1", draft_id, job_id);
  } else {
    // 重新武装占位行：succeed 用 generation_id 做乐观并发闸门，不清空就写不进去。
    tx.exec_params("UPDATE scenarios SET generation_id = $2 WHERE id = $1", draft_id, job_id);
  }
  writeAudit(tx, actor_id, "generation_retried", job_id, request_id);
  const auto result = jobJson(selectJob(tx, job_id)[0]);
  tx.commit();
  return result;
}

json KnowledgeAdminQueue::stats() const {
  auto connection = database_pool_->acquire();
  pqxx::read_transaction tx(connection.get());
  const auto row = tx.exec(R"(
    SELECT COUNT(*) FILTER (WHERE status IN ('pending', 'retry_wait')) AS pending_jobs,
      COUNT(*) FILTER (WHERE status = 'dead') AS dead_jobs
    FROM knowledge_admin_jobs
  )")[0];
  return {{"pendingJobs", row["pending_jobs"].as<int>()},
          {"deadJobs", row["dead_jobs"].as<int>()}};
}

std::optional<KnowledgeAdminJob> KnowledgeAdminQueue::claim(
    const std::string& worker_id) const {
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  tx.exec(R"(
    UPDATE knowledge_admin_jobs SET status = 'dead', lease_until = NULL,
      worker_id = NULL, attempt_token = NULL, error_type = 'LEASE_EXHAUSTED',
      error_message = '生成任务租约失效且已达到最大尝试次数',
      updated_at = NOW(), finished_at = NOW()
    WHERE status = 'running' AND lease_until <= NOW() AND attempts >= max_attempts
  )");
  const auto attempt_token = randomId("attempt");
  const auto rows = tx.exec_params(R"(
    WITH candidate AS (
      SELECT id FROM knowledge_admin_jobs
      WHERE ((status IN ('pending', 'retry_wait') AND available_at <= NOW()) OR
             (status = 'running' AND lease_until <= NOW() AND attempts < max_attempts))
      ORDER BY created_at, id FOR UPDATE SKIP LOCKED LIMIT 1
    )
    UPDATE knowledge_admin_jobs j SET status = 'running', worker_id = $1,
      attempt_token = $2, attempts = attempts + 1,
      lease_until = NOW() + make_interval(secs => $3), updated_at = NOW()
    FROM candidate WHERE j.id = candidate.id
    RETURNING j.id, j.kind, j.draft_id, j.generation, j.base_draft_version,
      j.attempts, j.max_attempts, j.attempt_token, j.request
  )", worker_id, attempt_token, kLeaseSeconds);
  tx.commit();
  if (rows.empty()) return std::nullopt;
  return KnowledgeAdminJob{
      rows[0]["id"].c_str(), rows[0]["kind"].c_str(), rows[0]["draft_id"].c_str(),
      rows[0]["generation"].as<int>(), rows[0]["base_draft_version"].as<int>(),
      rows[0]["attempts"].as<int>(), rows[0]["max_attempts"].as<int>(),
      rows[0]["attempt_token"].c_str(), parseJsonField(rows[0]["request"])};
}

bool KnowledgeAdminQueue::renewLease(const KnowledgeAdminJob& job) const {
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  const auto rows = tx.exec_params(R"(
    UPDATE knowledge_admin_jobs SET lease_until = NOW() + make_interval(secs => $4),
      updated_at = NOW() WHERE id = $1 AND generation = $2 AND attempt_token = $3
      AND status = 'running' AND lease_until > NOW() RETURNING 1
  )", job.id, job.generation, job.attempt_token, kLeaseSeconds);
  tx.commit();
  return !rows.empty();
}

bool KnowledgeAdminQueue::succeed(const KnowledgeAdminJob& job, const json& candidate,
                                  const std::string& model_version) const {
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  const auto active = tx.exec_params(R"(
    SELECT created_by FROM knowledge_admin_jobs WHERE id = $1 AND generation = $2
      AND attempt_token = $3 AND status = 'running' AND lease_until > NOW() FOR UPDATE
  )", job.id, job.generation, job.attempt_token);
  if (active.empty()) return false;

  /* 显式分发。job.kind 来自数据库，未知类型**什么都不写**就返回 false：
     猜一个目标表去写，正是 R09 风险表里「队列新增类型落到错误目标表」的成因。 */
  const auto target = parseGenerationTarget(job.kind);
  if (!target.has_value()) {
    tx.commit();
    return false;
  }

  pqxx::result applied;
  json stored_extra = json::object();
  bool applied_ok = false;
  if (*target == GenerationTarget::ServiceDraft) {
    applied = tx.exec_params(R"(
      UPDATE service_drafts SET payload = $4::jsonb, draft_version = draft_version + 1,
        generation_id = NULL, updated_by = $5, updated_at = NOW()
      WHERE id = $1 AND generation_id = $2 AND draft_version = $3
      RETURNING service_id, draft_version
    )", job.draft_id, job.id, job.base_draft_version, candidate.dump(),
        active[0]["created_by"].c_str());
    applied_ok = !applied.empty();
    if (applied_ok) {
      tx.exec_params(R"(
        UPDATE clinic_services SET updated_at = NOW()
        WHERE id = $1
        )", applied[0]["service_id"].c_str());
    }
  } else if (*target == GenerationTarget::KnowledgeDraft) {
    applied = tx.exec_params(R"(
      UPDATE knowledge_drafts SET title = $4, body = $5, metadata = $6::jsonb,
        draft_version = draft_version + 1, generation_id = NULL,
        updated_by = $7, updated_at = NOW()
      WHERE id = $1 AND generation_id = $2 AND draft_version = $3
      RETURNING draft_version
    )", job.draft_id, job.id, job.base_draft_version,
        candidate["title"].get<std::string>(), candidate["body"].get<std::string>(),
        candidate["metadata"].dump(), active[0]["created_by"].c_str());
    applied_ok = !applied.empty();
  } else {
    /* 场景骨架（迁移 030）。三步都在本事务里，缺一不可：
         1. 校验并写入由**注入的回调**完成——它复用主管手工建场景那套
            validateScenarioPayload，是 AI 内容进入 scenarios 表的最后一道闸门；
            校验不过就抛异常 → 事务整体回滚 → worker 记为失败并可重试。
         2. 回调同时也拦住了两种不该写的情况：占位行已被主管上架、
            或 generation_id 已不再属于这个任务（stale）。那时返回
            applied=false，候选结果照样存档，只是不落库。
         3. 生成的骨架一律 is_active = FALSE：AI 产出未经主管审阅绝不能自动上架。 */
    if (!scenario_draft_writer_) {
      throw KnowledgeStoreError(500, "SCENARIO_WRITER_MISSING",
                                "服务未配置场景骨架的落库逻辑");
    }
    const auto outcome = scenario_draft_writer_(tx, job.draft_id, job.id, candidate);
    applied_ok = outcome.value("applied", false);
    if (outcome.contains("warnings")) stored_extra["warnings"] = outcome["warnings"];
  }
  json stored_result = {{"candidate", candidate}, {"applied", applied_ok}};
  if (*target == GenerationTarget::ScenarioDraft) {
    stored_result["scenarioId"] = job.draft_id;
    for (auto it = stored_extra.begin(); it != stored_extra.end(); ++it) {
      stored_result[it.key()] = it.value();
    }
  } else if (applied_ok) {
    /* RETURNING 的列按目标表而不同：草稿类给 draft_version，场景骨架给 sort_order。
       按列名读一个不存在的列在 pqxx 里是抛异常（不是返回空值），所以必须显式分派。 */
    stored_result["draftVersion"] = applied[0]["draft_version"].as<int>();
  }
  tx.exec_params(R"(
    UPDATE knowledge_admin_jobs SET status = 'succeeded', result = $4::jsonb,
      result_applied = $5, model_version = $6, lease_until = NULL, worker_id = NULL,
      attempt_token = NULL,
      error_type = NULL, error_message = NULL, updated_at = NOW(), finished_at = NOW()
    WHERE id = $1 AND generation = $2 AND attempt_token = $3
  )", job.id, job.generation, job.attempt_token, stored_result.dump(), applied_ok,
      model_version);
  tx.commit();
  return true;
}

void KnowledgeAdminQueue::fail(const KnowledgeAdminJob& job,
                               const std::string& error_type,
                               const std::string& message, bool retryable) const {
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  const bool retry_wait = retryable && job.attempt < job.max_attempts;
  tx.exec_params(R"(
    UPDATE knowledge_admin_jobs SET status = $4, lease_until = NULL, worker_id = NULL,
      attempt_token = NULL, available_at = CASE WHEN $5 THEN NOW() +
        make_interval(secs => LEAST(30, attempts * attempts)) ELSE available_at END,
      error_type = $6, error_message = $7, updated_at = NOW(),
      finished_at = CASE WHEN $5 THEN NULL ELSE NOW() END
    WHERE id = $1 AND generation = $2 AND attempt_token = $3 AND status = 'running'
  )", job.id, job.generation, job.attempt_token, retry_wait ? "retry_wait" : "dead",
      retry_wait, error_type, message.substr(0, 1000));
  tx.commit();
}

}  // namespace oral_training::knowledge
