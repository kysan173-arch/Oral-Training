#pragma once
#include "../src/rag_retriever.h"
#include "../src/rag_manifest.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
using json = nlohmann::json;
using namespace oral_training::rag;
inline void requireEmbedding(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
struct EmbeddingFixture {
  std::shared_ptr<DatabasePool> pool;
  std::string prefix, actor, service, revision;
  std::vector<std::string> manifest;
  explicit EmbeddingFixture(const char* url, const json& payload = json::object()) {
    requireEmbedding(std::getenv("ORAL_EMBEDDING_TEST_ISOLATED") != nullptr,
                     "Use run_embedding_checks.py to create an isolated test database");
    // A one-connection pool also catches accidental network calls holding a lease.
    pool = std::make_shared<DatabasePool>(url, 1, std::chrono::milliseconds(500));
    prefix = "embedding-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    actor = prefix + "-admin"; service = prefix + "-svc"; revision = prefix + "-sr";
    auto c = pool->acquire(); pqxx::work tx(c.get());
    tx.exec_params("INSERT INTO users(id,role,is_demo) VALUES($1,'admin',true)", actor);
    tx.exec_params("INSERT INTO clinic_services(id,name,category,created_by) VALUES($1,'Embedding test','synthetic',$2)", service, actor);
    tx.exec_params("INSERT INTO service_revisions(id,service_id,version,payload,content_hash,origin,published_by) "
                   "VALUES($1,$2,1,$5::jsonb,$3,'synthetic',$4)", revision, service, std::string(64,'0'), actor, payload.dump());
    tx.commit();
  }
  std::string add(const std::string& title, const std::string& body, json metadata = json::object(),
                  const std::string& topic = "general", bool locked = true, const std::string& other_service = "") {
    static int sequence = 0;
    const auto entry = prefix + "-k" + std::to_string(++sequence), rev = entry + "-r1";
    if (!metadata.contains("trainingScope")) metadata["trainingScope"] = "demo";
    if (!metadata.contains("applicability")) metadata["applicability"] = "模拟训练";
    auto c = pool->acquire(); pqxx::work tx(c.get());
    tx.exec_params("INSERT INTO knowledge_entries(id,topic,scope,service_id,created_by) VALUES($1,$2,'service',$3,$4)",
                   entry, topic, other_service.empty() ? service : other_service, actor);
    tx.exec_params("INSERT INTO knowledge_revisions(id,entry_id,version,title,body,metadata,content_hash,published_by) "
                   "VALUES($1,$2,1,$3,$4,$5::jsonb,$6,$7)", rev, entry, title, body, metadata.dump(), std::string(64,'0'), actor);
    tx.exec_params("INSERT INTO knowledge_drafts(id,entry_id,title,body,metadata,updated_by) VALUES($1,$1,$2,$3,$4::jsonb,$5)",
                   entry, title, body, metadata.dump(), actor);
    insertKnowledgeChunks(tx, rev, title, body, metadata); tx.commit();
    if (locked) manifest.push_back(rev);
    return rev;
  }
  std::string hash() const { return manifestHash(revision, manifest, "demo"); }
  json context() const {
    return {{"contextId", prefix}, {"serviceId", service}, {"serviceRevisionId", revision},
            {"manifest", manifest}, {"manifestHash", hash()}, {"trainingScope", "demo"}};
  }
  EvidenceBundle retrieve(RagRetriever& retriever, const std::string& question,
                          const std::optional<std::string>& topic = std::nullopt) {
    RetrievalRequest request; request.context_id = prefix; request.current_question = question; request.topic = topic;
    return retriever.retrieve(revision, manifest, "2026-10-08T12:00:00+08:00", hash(), request);
  }
};
