#pragma once

#include "database_pool.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace oral_training::knowledge {

using json = nlohmann::json;

struct KnowledgeAdminJob {
  std::string id;
  std::string kind;
  std::string draft_id;
  int generation = 0;
  int base_draft_version = 0;
  int attempt = 0;
  int max_attempts = 0;
  std::string attempt_token;
  json request;
};

class KnowledgeAdminQueue {
 public:
  /* 场景骨架（scenario_draft）的落库策略由**调用方注入**。
     为什么是回调而不是直接调用：knowledge 层不认识 scenarios 表，也不该认识——
     它只负责排队、租约与任务状态。而场景的内容校验（长度、条数、情绪词表、
     「服务要点必须为空」）只有 store 层那一份是权威的，在 knowledge 层复制一份
     必然漂移成「AI 能存、主管存不进去」。
     回调在队列的**事务内**执行，所以「写场景」与「任务置成功」仍然原子。
     返回 {"applied": bool, "warnings": [...]}。 */
  using ScenarioDraftWriter = std::function<json(pqxx::transaction_base&, const std::string&,
                                                 const std::string&, const json&)>;

  explicit KnowledgeAdminQueue(std::shared_ptr<DatabasePool> database_pool)
      : database_pool_(std::move(database_pool)) {}

  void setScenarioDraftWriter(ScenarioDraftWriter writer) {
    scenario_draft_writer_ = std::move(writer);
  }

  json create(const std::string& actor_id, const std::string& kind,
              const std::string& draft_id, const json& request,
              const std::string& idempotency_key,
              const std::string& request_digest,
              const std::string& request_id) const;
  json get(const std::string& actor_id, const std::string& job_id) const;
  json retry(const std::string& actor_id, const std::string& job_id,
             const std::string& request_id) const;
  json stats() const;

  std::optional<KnowledgeAdminJob> claim(const std::string& worker_id) const;
  bool renewLease(const KnowledgeAdminJob& job) const;
  bool succeed(const KnowledgeAdminJob& job, const json& candidate,
               const std::string& model_version) const;
  void fail(const KnowledgeAdminJob& job, const std::string& error_type,
            const std::string& message, bool retryable) const;

 private:
  std::shared_ptr<DatabasePool> database_pool_;
  ScenarioDraftWriter scenario_draft_writer_;
};

}  // namespace oral_training::knowledge
