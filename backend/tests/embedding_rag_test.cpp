#include "embedding_fixture.h"
#include "../src/api_error.h"
#include "../src/evidence_validator.h"
int main() {
  const auto* url = std::getenv("ORAL_TRAINING_TEST_DATABASE_URL");
  if (!url) return 77;
  try {
    EmbeddingFixture f(url);
    auto relevant = f.add("固定修复", "缺失牙可以咨询人工牙根修复，由医生检查评估。", {}, "repair");
    f.add("天气", "晴空万里适合散步。", {}, "weather");
    f.add("过期", "OUT_OF_SCOPE", {{"effectiveUntil", "2026-10-07"}});
    f.add("尚未生效", "OUT_OF_SCOPE", {{"effectiveFrom", "2026-10-09"}});
    f.add("未锁定的新知识", "OUT_OF_SCOPE", {}, "repair", false);
    int document_calls = 0, query_calls = 0;
    EmbeddingConfig config; config.dimensions = 128;
    auto client = std::make_shared<EmbeddingClient>(config, [&](const json& input) {
      requireEmbedding(f.pool->stats().in_use == 0, "network call holds database lease");
      if (input["task"] == "document") ++document_calls; else ++query_calls;
      json data = json::array();
      for (size_t i=0; i<input["input"].size(); ++i) {
        const auto text = input["input"][i].get<std::string>();
        requireEmbedding(text.find("OUT_OF_SCOPE") == std::string::npos, "excluded source sent to model");
        Embedding v(128, 0);
        v[text.find("天气") == std::string::npos ? 0 : 1] = 1;
        data.push_back({{"index", i}, {"embedding", v}});
      }
      return JsonHttpResponse{200, json{{"model", config.model},{"data", data}}.dump()};
    });
    RagRetriever retriever(f.pool, client);
    auto result = f.retrieve(retriever, "少了一颗，怎样补上？");
    requireEmbedding(result.passages.size() == 1 && result.passages[0].revision_id == relevant,
                     "semantic recall or scope filtering failed");
    requireEmbedding(document_calls == 1 && query_calls == 1, "cold embedding calls wrong");
    auto warm = f.retrieve(retriever, "少了一颗，怎样补上？");
    requireEmbedding(warm.passages[0].chunk_id == result.passages[0].chunk_id && document_calls == 1,
                     "warm cache not reused");
    requireEmbedding(f.retrieve(retriever, "少了一颗", "weather").passages.empty(), "topic filter ignored");
    EvidenceValidator validator(f.context(), json(result), "embedding-test-trace");
    requireEmbedding(validator.valid(), "semantic evidence violates locked manifest");
    // Cache corruption is repaired, never interpreted as evidence or silently ignored.
    {
      auto c = f.pool->acquire(); pqxx::work tx(c.get());
      tx.exec_params("UPDATE knowledge_chunk_embeddings SET embedding=$1::jsonb WHERE chunk_id=$2",
                     json(Embedding(128, 0)).dump(), result.passages[0].chunk_id); tx.commit();
    }
    f.retrieve(retriever, "少了一颗");
    requireEmbedding(document_calls == 2, "corrupt cache not repaired");
    auto changed = config; changed.revision = "new-weights";
    int reembedded = 0;
    auto fresh_client = std::make_shared<EmbeddingClient>(changed, [&](const json& input) {
      requireEmbedding(f.pool->stats().in_use == 0, "network call holds transaction");
      if (input["task"] == "document") reembedded += input["input"].size();
      json data = json::array(); Embedding v(128, 0); v[0] = 1;
      for (size_t i=0; i<input["input"].size(); ++i) data.push_back({{"index",i},{"embedding",v}});
      return JsonHttpResponse{200,json{{"model",changed.model},{"data",data}}.dump()};
    });
    RagRetriever changed_retriever(f.pool, fresh_client); f.retrieve(changed_retriever,"少了一颗");
    requireEmbedding(reembedded == 2, "new weights reused old vectors");
    // Preview must use the same semantic service, outside its read transaction.
    const auto entry = relevant.substr(0, relevant.size()-3);
    const auto preview = retriever.previewKnowledge(f.actor, entry, 1, "少了一颗");
    requireEmbedding(preview["evidence"].size() == 1 && preview["embeddingModel"] == "embeddinggemma-2",
                     "preview did not use embeddings");
    // No keyword fallback when embeddings are unavailable, even with a warm cache.
    auto broken = std::make_shared<EmbeddingClient>(config, [](const json&) { return JsonHttpResponse{503,""}; });
    RagRetriever failing(f.pool, broken); bool failed = false;
    try { f.retrieve(failing,"缺失牙"); } catch (const ApiError& e) { failed = e.code == "RAG_UNAVAILABLE"; }
    requireEmbedding(failed, "embedding outage silently changed retrieval mode");
    // Foreign scope and service must fail before inference, even if manifest hash is valid.
    auto foreign = f.add("真实资料", "OUT_OF_SCOPE", {{"trainingScope","real"}});
    failed = false;
    try { f.retrieve(retriever,"少了一颗"); } catch (const std::runtime_error&) { failed = true; }
    requireEmbedding(failed,"foreign training scope accepted"); f.manifest.pop_back();
    EmbeddingFixture other(url);
    f.add("其他服务", "OUT_OF_SCOPE", {}, "general", true, other.service);
    failed = false;
    try { f.retrieve(retriever,"少了一颗"); } catch (const std::runtime_error&) { failed = true; }
    requireEmbedding(failed,"foreign service accepted"); f.manifest.pop_back();
    // Conflicting eligible documents survive semantic top-K selection.
    f.add("条款甲", "本服务包含拍片。", {}, "contract");
    f.add("条款乙", "本服务不包含拍片。", {}, "contract");
    requireEmbedding(!f.retrieve(retriever,"拍片").conflicts.empty(), "conflicts dropped by semantic ranking");
    // An ambiguous fee follow-up must retain the original postoperative topic,
    // even when fee vocabulary favors long-term maintenance in vector space.
    EmbeddingFixture followup(url);
    followup.add("术后护理", "拆线复查不额外收费。", {}, "aftercare");
    followup.add("长期维护", "戴冠后长期维护收费100元。", {}, "maintenance");
    auto context_client = std::make_shared<EmbeddingClient>(config, [&](const json& input) {
      requireEmbedding(followup.pool->stats().in_use == 0, "context inference holds database lease");
      json data = json::array();
      for (size_t i = 0; i < input["input"].size(); ++i) {
        const auto text = input["input"][i].get<std::string>();
        Embedding v(128, 0);
        if (input["task"] == "query") v[text.find("收钱") == std::string::npos ? 0 : 1] = 1;
        else if (text.find("术后护理") != std::string::npos) { v[0] = 0.71; v[1] = 0.71; }
        else { v[0] = 0.3; v[1] = 0.95; }
        data.push_back({{"index", i}, {"embedding", v}});
      }
      return JsonHttpResponse{200, json{{"model", config.model}, {"data", data}}.dump()};
    });
    RagRetriever contextual(followup.pool, context_client);
    RetrievalRequest q; q.purpose = RetrievalPurpose::CustomerReply; q.current_question = "那要再收钱吗？";
    q.recent_question_answers = json::array({{{"role", "learner_patient"}, {"content", "种植术后什么时候拆线复查？"}},
        {{"role", "standard_customer"}, {"content", "untrusted statement"}},
        {{"role", "learner_patient"}, {"content", q.current_question}}});
    const auto focused = contextual.retrieve(followup.revision, followup.manifest,
        "2026-10-08T12:00:00+08:00", followup.hash(), q);
    requireEmbedding(focused.passages.size() == 1 && focused.passages[0].title == "术后护理",
                     "implicit fee question switched to long-term maintenance");
    q.current_question = "那植骨要再收钱吗？";
    const auto switched = contextual.retrieve(followup.revision, followup.manifest,
        "2026-10-08T12:00:00+08:00", followup.hash(), q);
    requireEmbedding(switched.passages.size() == 2, "explicit new subject constrained to old topic");
    EmbeddingFixture timing(url, {{"visitDuration", {{"status","known"},{"minimum",30},
        {"maximum",45},{"unit","minute"},{"estimated",true},{"conditions","初诊检查和沟通"}}}});
    RagRetriever timing_retriever(timing.pool, client);
    RetrievalRequest timing_query; timing_query.purpose=RetrievalPurpose::PatientReply;
    timing_query.current_question="初诊检查和沟通一般30～45分钟。";
    const auto timing_evidence=timing_retriever.retrieve(timing.revision,timing.manifest,
        "2026-10-08T12:00:00+08:00",timing.hash(),timing_query);
    requireEmbedding(timing_evidence.facts.size()==1 && timing_evidence.facts[0].field=="visitDuration",
                     "patient-side factual statement failed to retrieve visit duration");
    std::cout << "Embedding RAG database checks passed (cache, scope, preview, outage, conflicts, context, transaction release)\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
