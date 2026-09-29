#pragma once

#include "database_pool.h"
#include "rag_types.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace oral_training::rag {

struct KnowledgeChunkDraft {
  int ordinal = 0;
  std::string body;
  std::string section;
  std::string terms;
  std::string title_terms;
  std::string body_terms;
  int source_start = 0;
  int source_end = 0;
};

std::vector<std::string> tokenizeChinese(const std::string& text);
std::string customerRetrievalQuestion(const RetrievalRequest& request);
std::vector<KnowledgeChunkDraft> chunkKnowledge(const std::string& title,
                                                const std::string& body,
                                                const json& metadata);
void insertKnowledgeChunks(pqxx::transaction_base& tx, const std::string& revision_id,
                           const std::string& title, const std::string& body,
                           const json& metadata);

class RagRetriever {
 public:
  explicit RagRetriever(std::shared_ptr<DatabasePool> database_pool)
      : database_pool_(std::move(database_pool)) {}

  EvidenceBundle retrieve(const std::string& service_revision_id,
                          const std::vector<std::string>& knowledge_revision_ids,
                          const std::string& knowledge_as_of,
                          const std::string& locked_manifest_hash,
                          const RetrievalRequest& request,
                          const std::string& training_scope = "demo") const;

  json previewKnowledge(const std::string& actor_id, const std::string& entry_id,
                        int draft_version, const std::string& question) const;

 private:
  std::shared_ptr<DatabasePool> database_pool_;
};

}  // namespace oral_training::rag
