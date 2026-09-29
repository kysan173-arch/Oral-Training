#pragma once

#include "database_pool.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace oral_training::knowledge {

using json = nlohmann::json;

class KnowledgeStoreError : public std::runtime_error {
 public:
  KnowledgeStoreError(int status, std::string code, std::string message)
      : std::runtime_error(std::move(message)), status(status), code(std::move(code)) {}

  int status;
  std::string code;
};

/* ── 生成任务类型的显式分发（RAG 执行计划 §7.3 / 风险表 R09） ────────────────
   队列里新增一种任务类型时，最容易犯的错是「忘了改某一处 if」——
   原实现是「不是 service_draft 就是 knowledge_draft」，未知类型会**静默**
   落到 knowledge_drafts 上，锁错行、写错表、还不报错。
   这里把 kind → 目标收敛成唯一一张映射表：所有分支先解析，解析不出就拒绝执行。
   新增类型的正确做法是：这里加一个枚举值 + 三个 switch 分支，然后编译器会把
   所有漏改的 switch 报成 warning（无 default 的 switch 覆盖枚举）。 */
enum class GenerationTarget { ServiceDraft, KnowledgeDraft, ScenarioDraft };

/* 解析 kind。返回 nullopt 表示「不认识」——调用方必须拒绝执行，不得兜底。 */
std::optional<GenerationTarget> parseGenerationTarget(const std::string& kind);
/* 目标表名，仅用于日志与错误信息（真正的 SQL 分支写在各自的用例里）。 */
std::string generationTargetTable(GenerationTarget target);
/* 中文标签，用于给运维/主管看的报错与审计文案。 */
std::string generationTargetLabel(GenerationTarget target);

void validateServiceDraft(const json& payload);
void validateKnowledgeDraft(const std::string& title, const std::string& body,
                            const json& metadata);
void validateGeneratedDraft(const std::string& kind, const json& candidate);
json servicePublicProjection(const json& payload);
std::string contentSha256(const json& value);

class KnowledgeStore {
 public:
  explicit KnowledgeStore(std::shared_ptr<DatabasePool> database_pool)
      : database_pool_(std::move(database_pool)) {}

  json listAvailableServices() const;
  json listServices(const std::string& actor_id) const;
  json createService(const std::string& actor_id, const json& payload,
                     const std::string& request_id) const;
  json getServiceDraft(const std::string& actor_id, const std::string& service_id) const;
  json saveServiceDraft(const std::string& actor_id, const std::string& service_id,
                        int draft_version, const json& payload,
                        const std::string& request_id) const;
  json publishService(const std::string& actor_id, const std::string& service_id,
                      int draft_version, const std::string& idempotency_key,
                      const std::string& request_digest, const std::string& request_id) const;
  json archiveService(const std::string& actor_id, const std::string& service_id,
                      const std::string& request_id) const;
  json serviceRevisions(const std::string& actor_id, const std::string& service_id) const;

  json listKnowledge(const std::string& actor_id) const;
  json createKnowledge(const std::string& actor_id, const std::string& topic,
                       const std::string& scope, const std::string& service_id,
                       const std::string& title, const std::string& body,
                       const json& metadata, const std::string& request_id) const;
  json getKnowledgeDraft(const std::string& actor_id, const std::string& entry_id) const;
  json saveKnowledgeDraft(const std::string& actor_id, const std::string& entry_id,
                          int draft_version, const std::string& title,
                          const std::string& body, const json& metadata,
                          const std::string& request_id) const;
  json publishKnowledge(const std::string& actor_id, const std::string& entry_id,
                        int draft_version, const std::string& idempotency_key,
                        const std::string& request_digest, const std::string& request_id) const;
  json archiveKnowledge(const std::string& actor_id, const std::string& entry_id,
                        const std::string& request_id) const;
  json knowledgeRevisions(const std::string& actor_id, const std::string& entry_id) const;

 private:
  std::shared_ptr<DatabasePool> database_pool_;
};

}  // namespace oral_training::knowledge
