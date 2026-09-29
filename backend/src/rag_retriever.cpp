#include "rag_retriever.h"
#include "evidence_validator.h"
#include "knowledge_validity.h"
#include "evidence_conflicts.h"

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
  if (containsAny(normalized, {"复诊", "复查", "间隔"})) fields.insert("followupInterval");
  if (containsAny(normalized, {"预约", "挂号", "号源", "营业时间"})) fields.insert("appointment");
  return fields;
}

std::string tsQuery(const std::vector<std::string>& terms) {
  return join(terms, " | ");
}

double overlapScore(const std::vector<std::string>& query, const std::string& terms) {
  if (query.empty()) return 0.0;
  std::set<std::string> document;
  std::istringstream input(terms);
  for (std::string value; input >> value;) document.insert(value);
  int matches = 0;
  for (const auto& value : query) if (document.count(value) != 0) ++matches;
  return static_cast<double>(matches) / static_cast<double>(query.size());
}

}  // namespace

std::string customerRetrievalQuestion(const RetrievalRequest& request) {
  std::string query = request.current_question;
  if (request.purpose != RetrievalPurpose::CustomerReply ||
      !containsAny(query, {"太长", "简单点", "简短", "一句话", "说清楚", "说人话", "什么意思"}) ||
      !request.recent_question_answers.is_array()) return query;
  // Resolve requests to rephrase using the latest substantive patient question.
  // Never index a previous assistant's answer as if it were published evidence.
  int scanned = 0;
  for (auto i = request.recent_question_answers.rbegin();
       i != request.recent_question_answers.rend() && scanned++ < 8; ++i) {
    if (!i->is_object() || i->value("role", "") != "learner_patient") continue;
    const auto previous = i->value("content", "");
    if (previous.empty() || previous == query ||
        containsAny(previous, {"太长", "简单点", "简短", "一句话", "说清楚", "说人话", "什么意思"})) continue;
    return previous + " " + query;
  }
  return query;
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
  int evidence = 1;
  const auto retrieval_question = customerRetrievalQuestion(request);
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

  const auto query_terms = tokenizeChinese(retrieval_question);
  if (!knowledge_revision_ids.empty() && !query_terms.empty()) {
    const json manifest = knowledge_revision_ids;
    const auto rows = tx.exec_params(R"(
      SELECT c.id, c.revision_id, c.body, c.section, r.title, r.metadata, e.scope, e.service_id, e.topic,
        ts_rank_cd(c.search_vector, to_tsquery('simple', $2)) AS rank
      FROM knowledge_chunks c
      JOIN knowledge_revisions r ON r.id = c.revision_id
      JOIN knowledge_entries e ON e.id = r.entry_id
      WHERE c.revision_id IN (SELECT jsonb_array_elements_text($1::jsonb))
        AND c.tokenizer_version = $3
        AND r.metadata->>'trainingScope' = $4
        AND (e.scope = 'general' OR e.service_id = $5)
        AND ($6 = '' OR e.topic = $6)
        AND COALESCE(NULLIF(r.metadata->>'effectiveFrom', ''), '0001-01-01') <= to_char($7::timestamptz AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD')
        AND COALESCE(NULLIF(r.metadata->>'effectiveUntil', ''), '9999-12-31') >= to_char($7::timestamptz AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD')
        AND c.search_vector @@ to_tsquery('simple', $2)
      ORDER BY rank DESC, c.revision_id, c.ordinal
    )", manifest.dump(), tsQuery(query_terms), std::string(kTokenizerVersion), training_scope,
        service_id, request.topic.value_or(""), knowledge_as_of);
    std::map<std::string, std::string> topics;
    for (const auto& row : rows) {
      const auto metadata = json::parse(row["metadata"].c_str());
      bundle.passages.push_back({"E" + std::to_string(evidence++), row["id"].c_str(),
          row["revision_id"].c_str(), row["title"].c_str(), row["body"].c_str(),
          metadataString(metadata, "applicability"), metadataString(metadata, "sourceTitle"),
          metadataString(metadata, "sourceUrl").empty() ? std::nullopt
              : std::optional<std::string>(metadataString(metadata, "sourceUrl")),
          metadataString(metadata, "sourceLocator").empty() ? std::nullopt
              : std::optional<std::string>(metadataString(metadata, "sourceLocator")),
          row["scope"].c_str(), row["service_id"].is_null() ? "" : row["service_id"].c_str(),
          training_scope});
      topics[bundle.passages.back().evidence_id] = row["topic"].c_str();
    }
    detectEvidenceConflicts(bundle, topics);
  }
  bundle.retrieval_status = bundle.facts.empty() && bundle.passages.empty()
      ? RetrievalStatus::NoHit : RetrievalStatus::Ok;
  return bundle;
}

json RagRetriever::previewKnowledge(const std::string& actor_id, const std::string& entry_id,
                                    int draft_version, const std::string& question) const {
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
  const auto title = std::string(rows[0]["title"].c_str());
  const auto body = std::string(rows[0]["body"].c_str());
  const auto metadata = json::parse(rows[0]["metadata"].c_str());
  const auto chunks = chunkKnowledge(title, body, metadata);
  const auto effective_query = question.empty() ? title : question;
  const auto query_terms = tokenizeChinese(effective_query);
  struct Hit { double score; const KnowledgeChunkDraft* chunk; };
  std::vector<Hit> hits;
  for (const auto& chunk : chunks) {
    const auto score = overlapScore(query_terms, chunk.terms);
    if (score > 0.0 || question.empty()) hits.push_back({score, &chunk});
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
      ? "当前草稿没有命中该问题，请补充更明确的资料或关键词。"
      : "已命中当前草稿中的可引用内容，训练回答将只使用下方依据。";
  return {{"entityId", entry_id}, {"draftVersion", draft_version},
          {"query", effective_query}, {"tokenizerVersion", kTokenizerVersion},
          {"retrievalStatus", status}, {"answer", answer}, {"evidence", evidence}};
}

}  // namespace oral_training::rag
