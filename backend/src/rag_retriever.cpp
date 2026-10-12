#include "rag_retriever.h"
#include "evidence_validator.h"
#include "knowledge_validity.h"
#include "evidence_conflicts.h"
#include "sha256.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace oral_training::rag {
namespace {

struct Rune {
  char32_t value = 0;
  std::string bytes;
};

std::vector<Rune> decodeUtf8(const std::string& input) {
  std::vector<Rune> output;
  for (std::size_t i = 0; i < input.size();) {
    const auto first = static_cast<unsigned char>(input[i]);
    std::size_t count = 1;
    char32_t value = first;
    if ((first & 0xe0) == 0xc0 && i + 1 < input.size()) {
      count = 2;
      value = first & 0x1f;
    } else if ((first & 0xf0) == 0xe0 && i + 2 < input.size()) {
      count = 3;
      value = first & 0x0f;
    } else if ((first & 0xf8) == 0xf0 && i + 3 < input.size()) {
      count = 4;
      value = first & 0x07;
    }
    bool valid = true;
    for (std::size_t j = 1; j < count; ++j) {
      const auto next = static_cast<unsigned char>(input[i + j]);
      if ((next & 0xc0) != 0x80) {
        valid = false;
        break;
      }
      value = (value << 6) | (next & 0x3f);
    }
    if (!valid) {
      count = 1;
      value = first;
    }
    output.push_back({value, input.substr(i, count)});
    i += count;
  }
  return output;
}

bool isCjk(char32_t value) {
  return (value >= 0x3400 && value <= 0x4dbf) ||
      (value >= 0x4e00 && value <= 0x9fff) ||
      (value >= 0xf900 && value <= 0xfaff);
}

bool isAsciiWord(char32_t value) {
  return value < 128 && std::isalnum(static_cast<unsigned char>(value));
}

std::string hexBytes(const std::string& value) {
  std::ostringstream output;
  for (const auto character : value) {
    output << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(static_cast<unsigned char>(character));
  }
  return output.str();
}

std::string join(const std::vector<std::string>& values, const char* separator = " ") {
  std::ostringstream output;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) output << separator;
    output << values[i];
  }
  return output.str();
}

std::string normalizeForAlias(const std::string& input) {
  std::string output;
  for (const auto& rune : decodeUtf8(input)) {
    auto value = rune.value;
    if (value >= 0xff01 && value <= 0xff5e) value -= 0xfee0;
    if (value < 128) output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
    else output += rune.bytes;
  }
  return output;
}

std::vector<std::string> uniqueTerms(std::vector<std::string> values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

std::string utf8Slice(const std::vector<Rune>& runes, std::size_t first, std::size_t last) {
  std::string output;
  for (std::size_t i = first; i < last; ++i) output += runes[i].bytes;
  return output;
}

std::string metadataString(const json& metadata, const char* key) {
  return metadata.contains(key) && metadata[key].is_string()
      ? metadata[key].get<std::string>() : "";
}

bool containsAny(const std::string& text, const std::vector<std::string>& needles) {
  for (const auto& needle : needles) if (text.find(needle) != std::string::npos) return true;
  return false;
}

std::set<std::string> requestedFields(const std::string& question,
                                      const std::optional<std::string>& explicit_field) {
  std::set<std::string> fields;
  if (explicit_field && !explicit_field->empty()) fields.insert(*explicit_field);
  const auto normalized = normalizeForAlias(question);
  if (containsAny(normalized, {"价格", "费用", "多少钱", "报价", "收费", "价钱", "预算", "贵", "便宜", "花多少钱"})) fields.insert("price");
  if (containsAny(normalized, {"包括", "包含", "另收费", "项目", "拍片"})) fields.insert("includedItems");
  if (containsAny(normalized, {"多久", "多长时间", "时长", "疗程", "当天", "一次做完", "一次弄完"})) {
    fields.insert("visitDuration");
    fields.insert("treatmentDuration");
  }
  if (containsAny(normalized, {"初诊", "到诊", "就诊时长", "来一趟"})) fields.insert("visitDuration");
  if (containsAny(normalized, {"复诊", "复查", "间隔"})) fields.insert("followupInterval");
  if (containsAny(normalized, {"预约", "挂号", "号源", "营业时间"})) fields.insert("appointment");
  return fields;
}

std::string embeddingText(const std::string& title, const std::string& body, const json& metadata) {
  return title + "\n" + body + "\n" + metadataString(metadata, "applicability");
}

}  // namespace

namespace {
std::vector<std::string> customerQuestionContext(const RetrievalRequest& request) {
  const auto& current = request.current_question;
  if (request.purpose != RetrievalPurpose::CustomerReply ||
      !request.recent_question_answers.is_array()) return {};
  const auto follows = [](const std::string& question) {
    if (containsAny(question, {"换个话题", "不说价格", "我改问", "我是问", "改成问"})) return false;
    return containsAny(question, {"太长", "简单点", "简短", "说短", "一句话", "说清楚", "说人话", "什么意思",
        "那", "这笔", "这回", "这趟", "这个", "刚才", "里面", "其中", "还要", "再收费", "再收钱", "如果"});
  };
  if (!follows(current)) return {};
  // Resolve short follow-ups through a bounded chain of patient questions. Stop
  // at the most recent independent subject; never copy assistant claims into
  // retrieval or carry a price discussion across an explicit topic correction.
  std::vector<std::string> context;
  int scanned = 0;
  bool skipped_current = false;
  for (auto i = request.recent_question_answers.rbegin();
       i != request.recent_question_answers.rend() && scanned++ < 10 && context.size() < 3; ++i) {
    if (!i->is_object() || i->value("role", "") != "learner_patient") continue;
    const auto previous = i->value("content", "");
    if (!skipped_current && previous == current) { skipped_current = true; continue; }
    if (previous.empty() || previous.size() > 1800 || isSimpleThanks(previous)) break;
    context.push_back(previous);
    if (!follows(previous)) break;
  }
  std::reverse(context.begin(), context.end());
  return context;
}


bool implicitFeeFollowup(const std::string& current, const std::string& anchor) {
  if (anchor.empty() || !containsAny(current, {"收钱", "收费", "付钱", "花钱", "交钱", "另算钱"})) return false;
  std::string remainder = current;
  const auto remove = [&](const std::string& word) {
    size_t pos;
    while ((pos = remainder.find(word)) != std::string::npos) remainder.erase(pos, word.size());
  };
  // Explicit new subjects (e.g. 植骨) must remain independent. A repeated noun
  // can refer to the same prior procedure, never to an assistant's claim.
  for (const auto* noun : {"复查", "拆线", "拍片", "检查", "手术"})
    if (anchor.find(noun) != std::string::npos) remove(noun);
  for (const auto* word : {"要不要", "另外", "额外", "这个", "这次", "那次", "刚才", "需要",
       "收钱", "收费", "付钱", "花钱", "交钱", "另算钱", "单独", "那", "这", "还", "要", "再", "会", "也", "吗", "么", "呢", "？", "?", "。", "，", " "}) remove(word);
  return remainder.empty();
}
}  // namespace

std::string customerRetrievalQuestion(const RetrievalRequest& request) {
  auto context = customerQuestionContext(request);
  context.push_back(request.current_question);
  // Extra discourse labels measurably changed cosine ranking toward generic
  // follow-up policies. Embed only the actual patient questions.
  return join(context, " ");
}

std::vector<std::string> tokenizeChinese(const std::string& text) {
  const auto normalized = normalizeForAlias(text);
  const auto runes = decodeUtf8(normalized);
  std::vector<std::string> terms;
  std::vector<Rune> cjk;
  std::string ascii;
  const auto flushCjk = [&] {
    if (cjk.size() == 1) terms.push_back("c_" + hexBytes(cjk[0].bytes));
    for (std::size_t i = 1; i < cjk.size(); ++i) {
      terms.push_back("c_" + hexBytes(cjk[i - 1].bytes + cjk[i].bytes));
    }
    cjk.clear();
  };
  const auto flushAscii = [&] {
    if (!ascii.empty()) terms.push_back("w_" + ascii);
    ascii.clear();
  };
  for (const auto& rune : runes) {
    if (isCjk(rune.value)) {
      flushAscii();
      cjk.push_back(rune);
    } else if (isAsciiWord(rune.value)) {
      flushCjk();
      ascii.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(rune.value))));
    } else {
      flushCjk();
      flushAscii();
    }
  }
  flushCjk();
  flushAscii();

  const std::vector<std::pair<std::string, std::vector<std::string>>> aliases = {
      {"price", {"价格", "费用", "多少钱", "报价", "收费", "价钱", "预算", "贵", "便宜"}},
      {"duration", {"多久", "多长时间", "时长", "疗程", "当天", "一次做完", "一次弄完"}},
      {"included", {"包括", "包含", "另收费", "拍片"}},
      {"orthodontic_appliance", {"牙套", "矫治器"}},
      {"dental_implant", {"种牙", "种植牙"}},
      {"followup", {"复诊", "复查"}},
      {"appointment", {"预约", "挂号", "号源"}},
  };
  for (const auto& [canonical, variants] : aliases) {
    if (containsAny(normalized, variants)) terms.push_back("a_" + canonical);
  }
  return uniqueTerms(std::move(terms));
}

std::vector<KnowledgeChunkDraft> chunkKnowledge(const std::string& title,
                                                const std::string& body,
                                                const json& metadata) {
  const auto runes = decodeUtf8(body);
  std::vector<KnowledgeChunkDraft> chunks;
  constexpr std::size_t kTarget = 450;
  constexpr std::size_t kOverlap = 60;
  for (std::size_t start = 0; start < runes.size();) {
    std::size_t end = std::min(start + kTarget, runes.size());
    if (end < runes.size()) {
      const auto floor = start + 200;
      for (std::size_t cursor = end; cursor > floor; --cursor) {
        const auto value = runes[cursor - 1].value;
        if (value == U'\n' || value == U'。' || value == U'！' || value == U'？') {
          end = cursor;
          break;
        }
      }
    }
    auto chunk_body = utf8Slice(runes, start, end);
    while (!chunk_body.empty() && (chunk_body.back() == '\n' || chunk_body.back() == '\r')) {
      chunk_body.pop_back();
    }
    if (!chunk_body.empty()) {
      std::string aliases;
      if (metadata.contains("aliases") && metadata["aliases"].is_array()) {
        for (const auto& alias : metadata["aliases"]) if (alias.is_string()) aliases += " " + alias.get<std::string>();
      }
      const auto title_terms = join(tokenizeChinese(title + " " + aliases));
      const auto body_terms = join(tokenizeChinese(chunk_body + " " + metadataString(metadata, "applicability")));
      chunks.push_back({static_cast<int>(chunks.size()), chunk_body, title,
                        title_terms + (title_terms.empty() || body_terms.empty() ? "" : " ") + body_terms,
                        title_terms, body_terms, static_cast<int>(start), static_cast<int>(end)});
    }
    if (end == runes.size()) break;
    start = end > kOverlap ? end - kOverlap : end;
  }
  return chunks;
}

void insertKnowledgeChunks(pqxx::transaction_base& tx, const std::string& revision_id,
                           const std::string& title, const std::string& body,
                           const json& metadata) {
  const auto chunks = chunkKnowledge(title, body, metadata);
  for (const auto& chunk : chunks) {
    tx.exec_params(R"(
      INSERT INTO knowledge_chunks
        (id, revision_id, ordinal, body, section, terms, search_vector,
         tokenizer_version, source_start, source_end)
      VALUES ($1, $2, $3, $4, $5, $6,
        setweight(to_tsvector('simple', $7), 'A') || setweight(to_tsvector('simple', $8), 'D'),
        $9, $10, $11)
    )", revision_id + "-c" + std::to_string(chunk.ordinal), revision_id, chunk.ordinal,
        chunk.body, chunk.section, chunk.terms, chunk.title_terms, chunk.body_terms,
        std::string(kTokenizerVersion), chunk.source_start, chunk.source_end);
  }
}

EvidenceBundle RagRetriever::retrieve(const std::string& service_revision_id,
                                      const std::vector<std::string>& knowledge_revision_ids,
                                      const std::string& knowledge_as_of,
                                      const std::string& locked_manifest_hash,
                                      const RetrievalRequest& request,
                                      const std::string& training_scope) const {
  EvidenceBundle bundle;
  bundle.context_id = request.context_id;
  bundle.service_revision_id = service_revision_id;
  bundle.knowledge_as_of = knowledge_as_of;
  bundle.purpose = request.purpose;
  if (locked_manifest_hash != manifestHash(service_revision_id, knowledge_revision_ids, training_scope))
    throw std::runtime_error("RAG manifest hash mismatch");
  bundle.manifest_hash = locked_manifest_hash;
  bundle.training_scope = training_scope;
  bundle.retriever_version = "embeddinggemma-context-cosine-v4";
  bundle.embedding_model = embeddings_->config().model;
  bundle.embedding_dimensions = embeddings_->config().dimensions;
  const auto cache_key = embeddings_->config().cacheKey();
  bundle.embedding_cache_key = cache_key;
  struct Candidate { PassageEvidence passage; std::string topic; std::string input; std::string hash;
                     Embedding vector; double score = 0; bool needs_cache = false; };
  std::vector<Candidate> candidates;
  const auto retrieval_question = customerRetrievalQuestion(request);
  int evidence = 1;
  {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());

    const auto as_of_date = std::string(tx.exec_params(
        "SELECT to_char($1::timestamptz AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') AS date",
        knowledge_as_of)[0]["date"].c_str());
    std::string service_id;
    json service_payload;
    if (!service_revision_id.empty()) {
      const auto service = tx.exec_params(
          "SELECT service_id, payload FROM service_revisions WHERE id = $1", service_revision_id);
      if (service.empty()) throw std::runtime_error("service revision not found");
      service_id = service[0]["service_id"].c_str();
      service_payload = json::parse(service[0]["payload"].c_str());
    }

    // Validate the complete locked set before retrieval; missing or foreign revisions
    // are a snapshot failure, not a legitimate no-hit response.
    const auto canonical = canonicalManifest(service_revision_id, knowledge_revision_ids, training_scope);
    const auto revisions = tx.exec_params(R"(
      SELECT r.id FROM knowledge_revisions r
      JOIN knowledge_entries e ON e.id = r.entry_id
      WHERE r.id IN (SELECT jsonb_array_elements_text($1::jsonb))
        AND r.metadata->>'trainingScope' = $2
        AND (e.scope = 'general' OR e.service_id = $3)
    )", canonical["knowledgeRevisionIds"].dump(), training_scope, service_id);
    if (revisions.size() != canonical["knowledgeRevisionIds"].size())
      throw std::runtime_error("RAG locked revisions unavailable or outside service scope");
    bundle.service_id = service_id;
    for (const auto& field : requestedFields(retrieval_question, request.field)) {
      if (!service_payload.contains(field)) {
        bundle.missing_fields.push_back(field);
        continue;
      }
      const auto& value = service_payload[field];
      if (!effectiveOnDate(value, "validFrom", "validUntil", as_of_date)) {
        bundle.missing_fields.push_back(field);
        continue;
      }
      if (value.is_object() && value.value("status", "known") == "unknown") {
        bundle.missing_fields.push_back(field);
        continue;
      }
      const auto display = renderFact(field, value);
      if (display.empty()) {
        bundle.missing_fields.push_back(field);
        continue;
      }
      bundle.facts.push_back({"E" + std::to_string(evidence++), field, value, display,
                              service_revision_id, service_payload.value("dataOrigin", "manual")});
    }

    if (!knowledge_revision_ids.empty() && !retrieval_question.empty()) {
      const json manifest = knowledge_revision_ids;
      // Semantic retrieval considers every eligible chunk, including those with no lexical overlap.
      const auto rows = tx.exec_params(R"(
        SELECT c.id, c.revision_id, c.body, c.section, r.title, r.metadata, e.scope, e.service_id, e.topic,
               v.input_sha256, v.embedding
        FROM knowledge_chunks c
        JOIN knowledge_revisions r ON r.id = c.revision_id
        JOIN knowledge_entries e ON e.id = r.entry_id
        LEFT JOIN knowledge_chunk_embeddings v ON v.chunk_id = c.id AND v.cache_key = $6
        WHERE c.revision_id IN (SELECT jsonb_array_elements_text($1::jsonb))
          AND r.metadata->>'trainingScope' = $2
          AND (e.scope = 'general' OR e.service_id = $3)
          AND ($4 = '' OR e.topic = $4)
          AND COALESCE(NULLIF(r.metadata->>'effectiveFrom', ''), '0001-01-01') <= to_char($5::timestamptz AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD')
          AND COALESCE(NULLIF(r.metadata->>'effectiveUntil', ''), '9999-12-31') >= to_char($5::timestamptz AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD')
        ORDER BY c.revision_id, c.ordinal
      )", manifest.dump(), training_scope, service_id, request.topic.value_or(""), knowledge_as_of, cache_key);
      for (const auto& row : rows) {
        const auto metadata = json::parse(row["metadata"].c_str());
        Candidate candidate;
        candidate.passage = {"", row["id"].c_str(), row["revision_id"].c_str(),
            row["title"].c_str(), row["body"].c_str(), metadataString(metadata, "applicability"),
            metadataString(metadata, "sourceTitle"),
            metadataString(metadata, "sourceUrl").empty() ? std::nullopt
                : std::optional<std::string>(metadataString(metadata, "sourceUrl")),
            metadataString(metadata, "sourceLocator").empty() ? std::nullopt
                : std::optional<std::string>(metadataString(metadata, "sourceLocator")),
            row["scope"].c_str(), row["service_id"].is_null() ? "" : row["service_id"].c_str(), training_scope};
        candidate.topic = row["topic"].c_str();
        candidate.input = embeddingText(candidate.passage.title, candidate.passage.body, metadata);
        candidate.hash = oral_training::sha256Hex(candidate.input);
        if (!row["embedding"].is_null() && !row["input_sha256"].is_null() &&
            candidate.hash == row["input_sha256"].c_str()) {
          try { candidate.vector = EmbeddingClient::validatedVector(json::parse(row["embedding"].c_str()),
                                                                    embeddings_->config().dimensions); }
          catch (const std::exception&) { candidate.vector.clear(); }
        }
        candidate.needs_cache = candidate.vector.empty();
        candidates.push_back(std::move(candidate));
      }
    }
  } // Release both transaction and pooled connection BEFORE calling embedding services.

  if (!candidates.empty()) {
    const auto context_questions = customerQuestionContext(request);
    const auto anchor = context_questions.empty() ? "" : context_questions.front();
    const bool focus_previous_topic = implicitFeeFollowup(request.current_question, anchor);
    std::vector<std::string> queries = {retrieval_question};
    if (focus_previous_topic) queries.push_back(anchor);
    const auto query_vectors = embeddings_->embed(queries, "query");
    const auto& query_vector = query_vectors.front();
    std::string anchor_topic;
    double anchor_score = embeddings_->config().min_similarity;
    std::vector<std::string> missing_inputs;
    for (const auto& candidate : candidates) if (candidate.needs_cache) missing_inputs.push_back(candidate.input);
    const auto computed = embeddings_->embed(missing_inputs, "document");
    size_t next = 0;
    for (auto& candidate : candidates) {
      if (candidate.needs_cache) candidate.vector = computed.at(next++);
      candidate.score = EmbeddingClient::cosine(query_vector, candidate.vector);
      if (focus_previous_topic) {
        const auto score = EmbeddingClient::cosine(query_vectors.back(), candidate.vector);
        if (score > anchor_score) { anchor_score = score; anchor_topic = candidate.topic; }
      }
    }
    if (!computed.empty()) {
      auto connection = database_pool_->acquire();
      pqxx::work tx(connection.get());
      for (const auto& candidate : candidates) if (candidate.needs_cache) {
        tx.exec_params(R"(
          INSERT INTO knowledge_chunk_embeddings(chunk_id,cache_key,input_sha256,model,dimensions,embedding)
          VALUES($1,$2,$3,$4,$5,$6::jsonb)
          ON CONFLICT(chunk_id,cache_key) DO UPDATE SET input_sha256=EXCLUDED.input_sha256,
            model=EXCLUDED.model, dimensions=EXCLUDED.dimensions, embedding=EXCLUDED.embedding, created_at=NOW()
        )", candidate.passage.chunk_id, cache_key, candidate.hash, embeddings_->config().model,
            embeddings_->config().dimensions, json(candidate.vector).dump());
      }
      tx.commit();
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
      return a.score > b.score;
    });
    std::map<std::string, std::string> topics;
    for (auto& candidate : candidates) {
      if (candidate.score < embeddings_->config().min_similarity) continue;
      // A subject-free fee follow-up stays within the semantically resolved
      // previous topic. Keep every eligible chunk in that topic for conflict
      // detection; a new explicit subject is never constrained this way.
      if (focus_previous_topic && !anchor_topic.empty() && candidate.topic != anchor_topic) continue;
      candidate.passage.evidence_id = "E" + std::to_string(evidence++);
      topics[candidate.passage.evidence_id] = candidate.topic;
      bundle.passages.push_back(std::move(candidate.passage));
    }
    // Existing conflict-aware selection checks every qualifying candidate before limiting to six.
    detectEvidenceConflicts(bundle, topics);
  }
  bundle.retrieval_status = bundle.facts.empty() && bundle.passages.empty()
      ? RetrievalStatus::NoHit : RetrievalStatus::Ok;
  return bundle;
}

json RagRetriever::previewKnowledge(const std::string& actor_id, const std::string& entry_id,
                                    int draft_version, const std::string& question) const {
  std::string title, body;
  json metadata;
  {
    auto connection = database_pool_->acquire();
    pqxx::read_transaction tx(connection.get());
    const auto rows = tx.exec_params(R"(
      SELECT e.topic, e.scope, e.service_id, d.title, d.body, d.metadata, d.draft_version
      FROM users u, knowledge_entries e JOIN knowledge_drafts d ON d.entry_id = e.id
      WHERE u.id = $1 AND u.role = 'admin' AND u.status = 'active' AND e.id = $2
    )", actor_id, entry_id);
    if (rows.empty()) throw std::runtime_error("knowledge draft not found");
    if (rows[0]["draft_version"].as<int>() != draft_version) {
      throw std::invalid_argument("draft version conflict");
    }
    title = rows[0]["title"].c_str();
    body = rows[0]["body"].c_str();
    metadata = json::parse(rows[0]["metadata"].c_str());
  }
  const auto chunks = chunkKnowledge(title, body, metadata);
  const auto effective_query = question.empty() ? title : question;
  struct Hit { double score; const KnowledgeChunkDraft* chunk; };
  std::vector<Hit> hits;
  if (!chunks.empty()) {
    std::vector<std::string> inputs;
    for (const auto& chunk : chunks) inputs.push_back(embeddingText(title, chunk.body, metadata));
    const auto documents = embeddings_->embed(inputs, "document");
    const auto query = embeddings_->embed({effective_query}, "query").at(0);
    for (size_t i = 0; i < chunks.size(); ++i) {
      const auto score = EmbeddingClient::cosine(query, documents[i]);
      if (score >= embeddings_->config().min_similarity) hits.push_back({score, &chunks[i]});
    }
  }
  std::sort(hits.begin(), hits.end(), [](const Hit& left, const Hit& right) {
    if (std::abs(left.score - right.score) > 0.000001) return left.score > right.score;
    return left.chunk->ordinal < right.chunk->ordinal;
  });
  if (hits.size() > 6) hits.resize(6);
  json evidence = json::array();
  for (std::size_t i = 0; i < hits.size(); ++i) {
    evidence.push_back({{"evidenceId", "E" + std::to_string(i + 1)},
                        {"title", title}, {"body", hits[i].chunk->body},
                        {"section", hits[i].chunk->section},
                        {"score", std::round(hits[i].score * 1000.0) / 1000.0},
                        {"applicability", metadataString(metadata, "applicability")}});
  }
  const auto status = evidence.empty() ? "no_hit" : "ok";
  const auto answer = evidence.empty()
      ? "当前草稿没有命中该问题，请补充资料或调整问题表述。"
      : "已命中当前草稿中的可引用内容，训练回答将只使用下方依据。";
  return {{"entityId", entry_id}, {"draftVersion", draft_version},
          {"query", effective_query}, {"tokenizerVersion", kTokenizerVersion},
          {"retrieverVersion", "embeddinggemma-context-cosine-v4"},
          {"embeddingModel", embeddings_->config().model},
          {"embeddingDimensions", embeddings_->config().dimensions},
          {"retrievalStatus", status}, {"answer", answer}, {"evidence", evidence}};
}

}  // namespace oral_training::rag
