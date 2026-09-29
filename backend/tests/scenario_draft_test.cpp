// scenario_draft_test.cpp — AI 骨架生成（P1-3 / 迁移 030）的端到端验证。
//
// 为什么需要它：这条链路要调真实模型，离线跑不动；而「只跑通 SQL」证明不了
// 校验、注入的落库回调、乐观并发与强制下线这些真正会出错的环节。
// 所以这里注入一个假模型网关，其余全部是真的——真实数据库、真实迁移后的表、
// 真实的 KnowledgeAdminQueue、真实的 worker 线程、真实的 validateScenarioPayload。
//
// 覆盖：
//   1. 未知 kind 被拒绝（显式分发，不再有「不是 A 就是 B」的兜底）
//   2. scenario_draft 的 create 会在事务里预建占位场景（is_active=FALSE + generation_id）
//   3. 幂等键对 scenario_draft 同样成立（服务端生成的 draftId 不该让合法重放变 409）
//   4. worker 跑完一整轮：假模型产出 → 形状校验 → 落库校验 → 写回 scenarios
//   5. 落库后：ai_draft=TRUE、is_active=FALSE、generation_id 清空、warnings 存档
//   6. 服务要点非空被拒（AI 不得编造机构事实）
//   7. 内容不合规（名称过短）→ 抛错且**一行都不写**（事务回滚）
//   8. 已上架的骨架拒绝重试（重试成功会强制下线，等于把已发布场景撤下来）
//   9. 落库回调对已上架的行拒绝覆盖（stale 保护）
//
// 需要 ORAL_TRAINING_TEST_DATABASE_URL；未配置时返回 77（Skipped），
// 与 database_feature 的约定一致。

#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

using json = nlohmann::json;
using oral_training::knowledge::KnowledgeAdminQueue;
using oral_training::knowledge::KnowledgeStoreError;
using oral_training::knowledge::validateGeneratedDraft;

constexpr char kAdminId[] = "scenario-draft-admin";
constexpr int kSkipExitCode = 77;

int failures = 0;

/* 进度标记：这个测试要跑真库、起真 worker，崩溃或卡住时若没有痕迹就只能靠二分排查。
   每步打一行到 stderr（不进 stdout，避免污染通过时的输出）。 */
void trace(const char* label) {
  std::cerr << "[scenario_draft] " << label << std::endl;
}

void require(bool condition, const std::string& message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << std::endl;
  ++failures;
}

/* 假模型网关：只实现骨架生成。其余方法沿用接口的默认空实现——
   本测试不触碰对练/评分路径，实现它们只会制造无关的失败面。 */
class FakeScenarioGateway final : public oral_training::IModelGateway {
 public:
  FakeScenarioGateway(json candidate, bool record_kind)
      : candidate_(std::move(candidate)), record_kind_(record_kind) {}

  bool configured() const override { return true; }
  std::string modelVersion() const override { return "fake:scenario-draft-v1"; }
  void setRuntimeKey(const std::string&) override {}
  json patientReply(const json&, const json&, const json&) const override { return json::object(); }
  json evaluate(const json&, const json&) const override { return json::object(); }
  json standardServiceReply(const json&, const json&) const override { return json::object(); }
  json roleplaySummary(const json&, const json&) const override { return json::object(); }

  json generateKnowledgeDraft(const std::string& kind, const json& input) const override {
    if (record_kind_) {
      ++calls;
      last_kind = kind;
      last_input = input;
    }
    return candidate_;
  }

  mutable int calls = 0;
  mutable std::string last_kind;
  mutable json last_input;

 private:
  json candidate_;
  bool record_kind_;
};

/* 一份「形状正确」的教学骨架。刻意不填 serviceGuidance——那正是模型的义务边界。 */
json validCandidate(const std::string& name) {
  return {
      {"name", name},
      {"summary", "表面要退费，真实顾虑是怕被继续推销"},
      {"category", "complaint_handling"},
      {"difficulty", "basic"},
      {"focus", json::array({"情绪承接", "费用说明边界"})},
      {"patientProfile", {{"age", 41}, {"gender", "女"},
                          {"description", "术后疼痛，觉得被推销过，情绪对抗"}}},
      {"hiddenConfig",
       {{"opening", "一万八我一分不少交的，做成这样你们还想再让我加钱？"},
        {"hidden", json::array({"怕再被推销套餐", "手上有别家的报价单", "只有先承认问题才愿意谈修复"})},
        {"initialState", {{"emotion", "愤怒"}, {"emotionLevel", -2}, {"trustLevel", 22}}},
        {"instructions", "若客服只回一两个字或答非所问，应表达不满并升级；"
                         "只有客服先承认问题、说明不追加费用并给出复诊安排，才愿意继续沟通。"}}},
      {"roleplayConfig", {{"suggestedQuestions", json::array({"您最担心的是哪一点？"})},
                          {"serviceGuidance", json::array()}}},
      {"dimensionWeights", {{"medicalCompliance", 0.6}, {"empathy", 0.4}}},
      {"maxRounds", 10}};
}

json scenarioRow(const std::string& database_url, const std::string& scenario_id) {
  pqxx::connection connection(database_url);
  pqxx::read_transaction tx(connection);
  const auto rows = tx.exec_params(R"(
    SELECT name, summary, is_active, is_template, ai_draft, generation_id,
      difficulty_tiers, dimension_weights, roleplay_config, hidden_config, sort_order
    FROM scenarios WHERE id = $1
  )", scenario_id);
  if (rows.empty()) return json(nullptr);
  const auto& row = rows[0];
  return {{"name", row["name"].c_str()},
          {"summary", row["summary"].c_str()},
          {"is_active", row["is_active"].as<bool>()},
          {"is_template", row["is_template"].as<bool>()},
          {"ai_draft", row["ai_draft"].as<bool>()},
          {"generating", !row["generation_id"].is_null()},
          {"dimension_weights", json::parse(row["dimension_weights"].c_str())},
          {"roleplay_config", json::parse(row["roleplay_config"].c_str())},
          {"hidden_config", json::parse(row["hidden_config"].c_str())},
          {"sort_order", row["sort_order"].as<int>()}};
}

void execSql(const std::string& database_url, const std::string& sql, const std::string& id) {
  pqxx::connection connection(database_url);
  pqxx::work tx(connection);
  tx.exec_params(sql, id);
  tx.commit();
}

/* 等到任务进入**终态或等待重试**：这三种状态下不再有 worker 会动它，
   断言才有意义。（retry_wait 也算终止条件——否则失败用例每次都要空转等满超时。） */
json waitForJob(Service& service, const std::string& job_id, int attempts = 200) {
  for (int index = 0; index < attempts; ++index) {
    const auto job = service.getKnowledgeJob(kAdminId, job_id);
    const auto status = job["status"].get<std::string>();
    if (status == "succeeded" || status == "dead" || status == "retry_wait") return job;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return service.getKnowledgeJob(kAdminId, job_id);
}

void cleanup(const std::string& database_url) {
  pqxx::connection connection(database_url);
  pqxx::work tx(connection);
  /* 只删本测试自造的痕迹，按 created_by / id 前缀定位，不碰真实数据。
     删除顺序必须满足外键：审计事件与任务都引用 users。漏掉审计事件，
     `DELETE FROM users` 会以外键冲突抛异常——而这个异常发生在 main 末尾，
     表现是「断言全过但进程以 127 退出」，比断言失败更难查（已踩）。 */
  tx.exec("DELETE FROM knowledge_audit_events WHERE actor_id = '" + std::string(kAdminId) + "'");
  tx.exec("DELETE FROM knowledge_admin_jobs WHERE created_by = '" + std::string(kAdminId) + "'");
  tx.exec("DELETE FROM scenarios WHERE id LIKE 'sc-ai-%' OR name LIKE 'TEST-%'");
  tx.exec("DELETE FROM users WHERE id = '" + std::string(kAdminId) + "'");
  tx.commit();
}

std::string digestOf(const json& value) {
  return oral_training::knowledge::contentSha256(value);
}

json draftRequest(const std::string& brief) {
  return {{"category", "complaint_handling"},
          {"difficulty", "basic"},
          {"concerns", json::array({"觉得被推销了", "担心再被收费"})},
          {"name", ""},
          {"brief", brief},
          {"count", 1}};
}

}  // namespace

int main() {
  const char* raw_url = std::getenv("ORAL_TRAINING_TEST_DATABASE_URL");
  if (raw_url == nullptr || std::string(raw_url).empty()) {
    std::cerr << "ORAL_TRAINING_TEST_DATABASE_URL is required\n";
    return kSkipExitCode;
  }
  const std::string database_url(raw_url);
  trace("config");

  auto config = Config::fromEnvironment();
  config.database_url = database_url;
  config.worker_concurrency = 1;
  config.knowledge_worker_concurrency = 1;

  trace("cleanup");
  cleanup(database_url);
  trace("seed admin");
  {
    pqxx::connection connection(database_url);
    pqxx::work tx(connection);
    tx.exec_params(R"(
      INSERT INTO users(id, display_name, role, status, is_demo)
      VALUES ($1, 'Scenario Draft Admin', 'admin', 'active', TRUE)
    )", kAdminId);
    tx.commit();
  }

  const auto pool = std::make_shared<DatabasePool>(
      database_url, 6, std::chrono::milliseconds(3000));
  KnowledgeAdminQueue queue(pool);

  trace("case 1: unknown kind");
  /* ── 1. 未知 kind 必须被拒绝（显式分发） ── */
  {
    bool rejected = false;
    try {
      queue.create(kAdminId, "roleplay_draft", "some-id", json::object(),
                   "unknown-kind-key", std::string(64, 'a'), "request-unknown");
    } catch (const KnowledgeStoreError& error) {
      rejected = error.status == 400 && error.code == "INVALID_ARGUMENT";
    }
    require(rejected, "unknown generation kind was accepted instead of rejected");
  }

  trace("case 2: placeholder + idempotency");
  /* ── 2. create 预建占位场景 + 3. 幂等重放 ── */
  std::string staged_scenario_id;
  {
    const auto request = draftRequest("测试：占位行与幂等");
    const auto digest = digestOf({{"kind", "scenario_draft"}, {"draftId", ""}, {"request", request}});
    const auto created = queue.create(kAdminId, "scenario_draft", "", request,
                                      "scenario-draft-idempotency", digest, "request-create");
    staged_scenario_id = created["draftId"].get<std::string>();
    require(created["kind"] == "scenario_draft", "job kind was not scenario_draft");
    require(created["promptVersion"] == "scenario-draft-v1",
            "scenario draft prompt version is wrong");
    const auto row = scenarioRow(database_url, staged_scenario_id);
    require(row.is_object(), "scenario_draft did not pre-create its target row");
    require(row["generating"].get<bool>(), "placeholder row was not marked as generating");
    require(!row["is_active"].get<bool>(), "placeholder row was created as active");
    require(row["ai_draft"].get<bool>(), "placeholder row was not marked as an AI draft");
    require(row["sort_order"].get<int>() > 0, "placeholder row has no sort order");

    /* 幂等重放：draftId 由服务端生成，重放请求里是空的。
       如果拿库里那个生成的 id 去比对，这里会变成 409 冲突。 */
    const auto replay = queue.create(kAdminId, "scenario_draft", "", request,
                                     "scenario-draft-idempotency", digest, "request-create-replay");
    require(replay["jobId"] == created["jobId"],
            "scenario_draft idempotent replay created a second job");
    require(replay["draftId"] == staged_scenario_id,
            "scenario_draft replay returned a different target scenario");
    /* 把这条任务按死：它没有 worker 处理，留着 pending 会被后面构造的 Service
       抢走（claim 按 created_at 排序），于是「假模型被调用了几次」的断言会失真。 */
    execSql(database_url, "UPDATE knowledge_admin_jobs SET status = 'dead' WHERE id = $1",
            created["jobId"].get<std::string>());
  }

  trace("case 4: full generation round");
  /* ── 4/5. 完整一轮：假模型 → 形状校验 → 落库 → 写回 scenarios ── */
  std::string generated_scenario_id;
  bool generation_succeeded = false;
  {
    const auto candidate = validCandidate("TEST-术后索赔安抚");
    auto gateway = std::make_unique<FakeScenarioGateway>(candidate, true);
    auto* gateway_ptr = gateway.get();
    Service service(config, pool, std::move(gateway));

    const auto request = draftRequest("测试：完整生成链路");
    const auto digest = digestOf({{"kind", "scenario_draft"}, {"draftId", ""}, {"request", request}});
    const auto created = service.createKnowledgeJob(kAdminId, "scenario_draft", "", request,
                                                    "scenario-draft-full", digest);
    generated_scenario_id = created["draftId"].get<std::string>();
    const auto job = waitForJob(service, created["jobId"].get<std::string>());
    if (job["status"] != "succeeded") {
      /* 失败时把任务原文与占位行原文都打出来。否则只有一句「没有成功」，
         排查要从零开始猜（模型？校验？落库回调？）——而这三者都会写进 errorType。 */
      std::cerr << "诊断 job: " << job.dump() << '\n';
      std::cerr << "诊断 scenario: " << scenarioRow(database_url, generated_scenario_id).dump() << '\n';
    }
    require(job["status"] == "succeeded", "scenario draft job did not succeed");
    generation_succeeded = job["status"] == "succeeded";
    require(gateway_ptr->calls == 1 && gateway_ptr->last_kind == "scenario_draft",
            "worker did not ask the gateway for a scenario draft");
    require(job["modelVersion"] == "fake:scenario-draft-v1", "model version was not recorded");
    require(job["result"].is_object() && job["result"].value("applied", false),
            "generated skeleton was not applied");
    require(job["result"].value("scenarioId", "") == generated_scenario_id,
            "result did not report the scenario id");
    /* AI 骨架必然缺机构红线，提醒必须随结果存档。 */
    bool has_guidance_warning = false;
    if (job["result"].contains("warnings")) {
      for (const auto& warning : job["result"]["warnings"]) {
        if (warning.value("code", "") == "NO_SERVICE_GUIDANCE") has_guidance_warning = true;
      }
    }
    require(has_guidance_warning, "missing service guidance warning was not persisted");

    const auto row = scenarioRow(database_url, generated_scenario_id);
    /* 行不存在时下面每条 json 下标都会在 null 上取值 —— 那是崩溃而不是失败报告，
       所以先单独断言一次存在性，再进入内容断言。 */
    require(row.is_object(), "generated scenario row is missing");
    if (row.is_object()) {
      require(row["name"] == "TEST-术后索赔安抚", "generated scenario name was not written");
      require(!row["is_active"].get<bool>(), "generated skeleton was published without review");
      require(row["ai_draft"].get<bool>(), "generated skeleton lost its ai_draft marker");
      require(!row["generating"].get<bool>(), "generation_id was not cleared after success");
      require(!row["is_template"].get<bool>(), "generated skeleton was marked as a template");
      require(row["dimension_weights"]["medicalCompliance"] == 0.6,
              "generated dimension weights were not written");
      require(row["roleplay_config"]["serviceGuidance"].empty(),
              "generated skeleton carries institution facts it must not invent");
      require(row["hidden_config"]["hidden"].size() == 3,
              "generated hidden concerns were not written");
      require(row["hidden_config"]["initialState"]["emotion"] == "愤怒",
              "generated initial state was not written");
    }
  }

  trace("case 6: shape validation");
  /* ── 6. 服务要点非空：形状校验就要拒（机构红线只能由主管填） ── */
  {
    auto bad = validCandidate("TEST-越界");
    bad["roleplayConfig"]["serviceGuidance"] = json::array({"不得承诺退费金额"});
    bool rejected = false;
    try {
      validateGeneratedDraft("scenario_draft", bad);
    } catch (const KnowledgeStoreError& error) {
      rejected = error.status == 400;
    }
    require(rejected, "generated skeleton was allowed to invent institution facts");

    auto shape_bad = validCandidate("TEST-缺字段");
    shape_bad.erase("hiddenConfig");
    bool shape_rejected = false;
    try {
      validateGeneratedDraft("scenario_draft", shape_bad);
    } catch (const KnowledgeStoreError& error) {
      shape_rejected = error.status == 400;
    }
    require(shape_rejected, "generated skeleton missing hiddenConfig was accepted");
  }

  trace("case 7: rollback on invalid content");
  /* ── 7. 字段级校验不过 → 抛错且一行都不写（事务回滚） ── */
  {
    const auto request = draftRequest("测试：不合规内容回滚");
    const auto digest = digestOf({{"kind", "scenario_draft"}, {"draftId", ""}, {"request", request}});
    auto bad_candidate = validCandidate("短");  // 名称 2-30 字，1 字必拒
    auto gateway = std::make_unique<FakeScenarioGateway>(bad_candidate, false);
    Service service(config, pool, std::move(gateway));
    const auto created = service.createKnowledgeJob(kAdminId, "scenario_draft", "", request,
                                                   "scenario-draft-invalid", digest);
    const auto target_id = created["draftId"].get<std::string>();
    const auto job = waitForJob(service, created["jobId"].get<std::string>());
    require(job["status"] != "succeeded", "invalid generated content was accepted");
    require(job["resultApplied"].is_null(), "invalid generated content was marked as applied");
    const auto row = scenarioRow(database_url, target_id);
    require(row["name"] != "短", "invalid generated content was written to the scenario");
    /* 占位行仍带着 generation_id：失败后它还是这条任务的目标，
       主管要么看到「生成中」、要么等它进死信——总之不会被误当成已完成的骨架。 */
    require(row["generating"].get<bool>(), "failed generation lost ownership of its placeholder");
  }

  trace("case 8: reject retry on published");
  /* ── 8. 已上架的骨架拒绝重试 ──
     前置条件依赖第 4 步真的生成成功（失败时占位行仍带 generation_id，
     数据库的 scenarios_generating_not_active_check 会拒绝把它置为已上架——
     那本身就是一道正确的保险，但不该让测试在这里崩掉）。 */
  if (generation_succeeded) {
    execSql(database_url,
            "UPDATE scenarios SET is_active = TRUE WHERE id = $1", generated_scenario_id);
    execSql(database_url,
            "UPDATE knowledge_admin_jobs SET status = 'dead' WHERE draft_id = $1",
            generated_scenario_id);
    std::string job_id;
    {
      pqxx::connection connection(database_url);
      pqxx::read_transaction tx(connection);
      job_id = tx.exec_params("SELECT id FROM knowledge_admin_jobs WHERE draft_id = $1",
                              generated_scenario_id)[0]["id"].c_str();
    }
    bool published_rejected = false;
    std::string published_code;
    try {
      queue.retry(kAdminId, job_id, "request-retry-published");
    } catch (const KnowledgeStoreError& error) {
      published_rejected = error.status == 409;
      published_code = error.code;
    }
    require(published_rejected && published_code == "SCENARIO_PUBLISHED",
            "retry on a published scenario was not rejected");
  }

  trace("case 9: stale guard");
  /* ── 9. 落库回调拒绝覆盖已上架的行（stale 保护） ── */
  if (generation_succeeded) {
    pqxx::connection connection(database_url);
    pqxx::work tx(connection);
    const auto outcome = ReliableDatabase::applyScenarioDraft(
        tx, generated_scenario_id, "stale-job-id", validCandidate("TEST-stale"));
    tx.commit();
    require(!outcome.value("applied", true),
            "applyScenarioDraft overwrote a published scenario");
    const auto row = scenarioRow(database_url, generated_scenario_id);
    require(row["name"] == "TEST-术后索赔安抚",
            "applyScenarioDraft rewrote a published scenario's content");
  }

  trace("cleanup + done");
  cleanup(database_url);
  trace("exit");

  if (failures != 0) {
    std::cerr << "scenario_draft_test failures: " << failures << '\n';
    return 1;
  }
  std::cout << "scenario_draft_test passed\n";
  return 0;
}
