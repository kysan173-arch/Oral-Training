#pragma once

#include "rag_manifest.h"
#include <map>
#include <regex>
#include <set>

namespace oral_training::rag {
using json = nlohmann::json;

inline std::string evidenceString(const json& value, const char* key) {
  return value.is_object() && value.contains(key) && value[key].is_string()
      ? value[key].get<std::string>() : "";
}

inline std::size_t textLength(const std::string& text) {
  return std::count_if(text.begin(), text.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
}

// A malformed field is not renderable. Never default a missing amount/unit to zero.
inline std::string renderFact(const std::string& field, const json& value) {
  try {
    if (field == "includedItems") {
      if (!value.is_array() || value.empty()) return "";
      std::string result = "包含项目：";
      for (const auto& item : value) {
        if (!item.is_string() || item.get<std::string>().empty()) return "";
        if (result != "包含项目：") result += "、";
        result += item.get<std::string>();
      }
      return result;
    }
    if (!value.is_object() || evidenceString(value, "status") != "known") return "";
    const auto positive = [&](const char* key) -> long long {
      if (!value.contains(key) || !value[key].is_number_integer() ||
          value[key] <= 0 || value[key] > 9000000000000000LL) return 0;
      return value[key].get<long long>();
    };
    if (field == "price") {
      const std::map<std::string, std::string> units = {
          {"per_tooth", "颗"}, {"per_case", "例"}, {"per_visit", "次"},
          {"per_arch", "牙弓"}, {"per_item", "项"}};
      const auto unit = units.find(evidenceString(value, "unit"));
      if (unit == units.end() || evidenceString(value, "currency") != "CNY" ||
          evidenceString(value, "conditions").empty()) return "";
      const auto yuan = [](long long minor) {
        std::ostringstream out;
        out << minor / 100;
        if (minor % 100) out << '.' << std::setw(2) << std::setfill('0') << minor % 100;
        return out.str();
      };
      const auto type = evidenceString(value, "type");
      std::string result;
      if ((type == "fixed" || type == "starting_from") && positive("amountMinor"))
        result = yuan(positive("amountMinor")) + (type == "fixed" ? " 元" : " 元起");
      else if (type == "range" && positive("minimumMinor") &&
               positive("maximumMinor") >= positive("minimumMinor"))
        result = yuan(positive("minimumMinor")) + "—" + yuan(positive("maximumMinor")) + " 元";
      else if (type == "quote_after_assessment") result = "需评估后报价";
      else return "";
      result += "/" + unit->second + "；" + evidenceString(value, "conditions");
      for (const auto* key : {"validFrom", "validUntil"}) {
        if (value.contains(key) && !value[key].is_null() && !value[key].is_string()) return "";
        const auto date = evidenceString(value, key);
        if (!date.empty()) result += std::string(key == std::string("validFrom") ? "；有效期自 " : "；有效期至 ") + date;
      }
      return result;
    }
    const std::map<std::string, std::string> labels = {
        {"visitDuration", "单次就诊时长"}, {"treatmentDuration", "完整疗程"},
        {"followupInterval", "复诊间隔"}};
    if (labels.count(field)) {
      const std::map<std::string, std::string> units = {{"minute", "分钟"}, {"hour", "小时"},
          {"day", "天"}, {"week", "周"}, {"month", "个月"}, {"year", "年"}};
      const auto unit = units.find(evidenceString(value, "unit"));
      const auto minimum = positive("minimum"), maximum = positive("maximum");
      if (unit == units.end() || !minimum || maximum < minimum ||
          (value.contains("estimated") && !value["estimated"].is_boolean())) return "";
      std::string result = labels.at(field) + "：" + (value.value("estimated", false) ? "约 " : "") +
          std::to_string(minimum) + (minimum == maximum ? "" : "—" + std::to_string(maximum)) + unit->second;
      for (const auto* key : {"phase", "conditions"}) {
        if (value.contains(key) && !value[key].is_null() && !value[key].is_string()) return "";
        if (!evidenceString(value, key).empty()) result += "；" + evidenceString(value, key);
      }
      return result;
    }
    if (field == "appointment") {
      if (evidenceString(value, "text").empty() || evidenceString(value, "timezone").empty() ||
          !value.contains("isLiveAvailability") || !value["isLiveAvailability"].is_boolean()) return "";
      // Published revisions can never reserve a live slot.
      return evidenceString(value, "text") + "（时区：" + evidenceString(value, "timezone") +
          "；仅为已发布资料，非实时号源，请以预约确认为准）";
    }
  } catch (const json::exception&) { return ""; }
  return "";
}

class EvidenceValidator {
 public:
  // Both context and bundle come from the backend, never from model output.
  EvidenceValidator(const json& context, const json& bundle, std::string trace_id)
      : trace_id_(std::move(trace_id)), hash_(evidenceString(context, "manifestHash")) {
    const auto revision = evidenceString(context, "serviceRevisionId");
    const auto scope = evidenceString(context, "trainingScope");
    const auto service = evidenceString(context, "serviceId");
    if (trace_id_.empty() || service.empty() || !context.contains("manifest") ||
        hash_ != manifestHash(revision, context["manifest"], scope) ||
        evidenceString(bundle, "contextId") != evidenceString(context, "contextId") ||
        evidenceString(bundle, "serviceRevisionId") != revision ||
        evidenceString(bundle, "serviceId") != service ||
        evidenceString(bundle, "trainingScope") != scope ||
        evidenceString(bundle, "manifestHash") != hash_) return;
    valid_ = true;
    std::set<std::string> manifest;
    for (const auto& id : context["manifest"]) manifest.insert(id.get<std::string>());
    std::set<std::string> seen;
    for (const auto* kind : {"facts", "passages"}) {
      if (!bundle.contains(kind) || !bundle[kind].is_array()) { valid_ = false; break; }
      for (const auto& item : bundle[kind]) {
        const auto id = evidenceString(item, "evidenceId");
        if (id.empty() || !seen.insert(id).second) { valid_ = false; break; }
        std::string text;
        if (std::string(kind) == "facts") {
          if (evidenceString(item, "revisionId") != revision || !item.contains("value")) continue;
          text = renderFact(evidenceString(item, "field"), item["value"]);
          // Display text is never authority; regenerate from the typed value.
        } else {
          if (!manifest.count(evidenceString(item, "revisionId")) ||
              evidenceString(item, "chunkId").empty() ||
              evidenceString(item, "trainingScope") != scope) continue;
          const auto item_scope = evidenceString(item, "scope");
          if (!((item_scope == "general" && evidenceString(item, "serviceId").empty()) ||
                (item_scope == "service" && evidenceString(item, "serviceId") == service))) continue;
          text = evidenceString(item, "body");
          if (!text.empty()) {
            text = "资料原文：" + text;
            const auto applicability = evidenceString(item, "applicability");
            if (!applicability.empty()) text += "（适用范围：" + applicability + "）";
          }
        }
        if (!text.empty()) entries_[id] = {{"text", text}, {"revisionId", evidenceString(item, "revisionId")}};
      }
    }
    if (!valid_) entries_.clear();
  }

  bool valid() const { return valid_; }
  json resolve(const json& citation, bool require_public = false, bool is_public = false) const {
    if (!valid_ || (require_public && !is_public) ||
        evidenceString(citation, "traceId") != trace_id_) return nullptr;
    const auto id = evidenceString(citation, "evidenceId");
    const auto entry = entries_.find(id);
    if (entry == entries_.end()) return nullptr;
    if (citation.contains("manifestHash") && evidenceString(citation, "manifestHash") != hash_) return nullptr;
    if (citation.contains("revisionId") && citation["revisionId"] != entry->second["revisionId"]) return nullptr;
    return {{"text", entry->second["text"]}, {"citation", {
        {"traceId", trace_id_}, {"evidenceId", id}, {"manifestHash", hash_},
        {"revisionId", entry->second["revisionId"]}}}};
  }

 private:
  bool valid_ = false;
  std::string trace_id_, hash_;
  std::map<std::string, json> entries_;
};

// Canonicalize presentation only, never calculate or loosen a numeric range.
inline std::string comparableReplyText(std::string text) {
  for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
      {"—", "-"}, {"–", "-"}, {"～", "-"}, {"~", "-"}, {"至", "-"}, {"到", "-"},
      {"个月", "月"}, {"％", "%"}}) {
    std::size_t pos = 0;
    while ((pos = text.find(pair.first, pos)) != std::string::npos) {
      text.replace(pos, pair.first.size(), pair.second); pos += pair.second.size();
    }
  }
  text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
  }), text.end());
  return text;
}

inline bool conciseGroundedText(const std::string& reply, const std::string& authority) {
  if (reply.empty() || textLength(reply) > 180) return false;
  for (const auto* term : {"资料原文", "适用范围：", "根据资料", "evidenceId", "[E", "```",
       "学习要点：", "合规边界：", "我理解您对这项服务的关注", "保证成功", "保证治愈",
       "绝对安全", "绝对无痛", "肯定没问题", "一定有效", "一定完成", "肯定完成", "包成功", "诊断为", "我判断", "不用检查",
       "无需检查", "不需要医生", "不用联系医生", "服用", "剂量", "处方"})
    if (reply.find(term) != std::string::npos) return false;
  const auto candidate = comparableReplyText(reply), supported = comparableReplyText(authority);
  // Quantities must match cited evidence including unit and range, not merely
  // reuse a digit that occurred elsewhere. Never silently truncate an answer.
  static const std::regex quantities(
      R"([0-9]+(?:\.[0-9]+)?(?:-[0-9]+(?:\.[0-9]+)?)?(?:万元|元|分钟|小时|天|周|月|年|%|折|次|颗)?)");
  std::set<std::string> allowed;
  for (std::sregex_iterator i(supported.begin(), supported.end(), quantities), end; i != end; ++i)
    allowed.insert(i->str());
  for (std::sregex_iterator i(candidate.begin(), candidate.end(), quantities), end; i != end; ++i)
    if (!allowed.count(i->str())) return false;
  // The prompt requires Arabic quantities. Reject spelled-out quantities rather
  // than let them bypass numeric grounding (Chinese regex character classes are
  // byte-based in std::regex, so enumerate complete UTF-8 characters).
  static const std::regex spelled_quantity(
      "(?:一|二|两|三|四|五|六|七|八|九|十|百|千|万|半|几|数)+(?:元|月|天|周|年|折|成|分钟|小时)");
  if (std::regex_search(candidate, spelled_quantity)) return false;
  for (const auto* term : {"免费", "优惠", "折扣", "包含", "赠送", "明天有号", "已预约", "已安排"})
    if (candidate.find(term) != std::string::npos && supported.find(term) == std::string::npos) return false;
  return true;
}

inline bool conversationOnlyText(const std::string& reply) {
  auto checked = reply;
  // Asking about an unspecified period is a clarification, not a duration claim.
  if (reply.find("？") != std::string::npos)
    for (const auto* phrase : {"几天", "几周", "几个月"}) {
      std::size_t pos;
      while ((pos = checked.find(phrase)) != std::string::npos) checked.erase(pos, std::string(phrase).size());
    }
  if (textLength(reply) > 70 || !conciseGroundedText(checked, "")) return false;
  // Conversation can acknowledge, empathize or clarify, but must not become a
  // second route for prices, institution policies or clinical assurances.
  for (const auto* term : {"分期", "报销", "退费", "包含", "赠送", "免费", "优惠", "有号", "已安排", "已联系",
       "已预约", "预约成功", "已经联系", "已经安排", "安排好了", "一定能", "肯定能", "一定不", "肯定不", "不会疼", "不疼", "无痛", "疼痛很轻",
       "正常现象", "当天完成", "无需复诊", "不需要复诊", "保证"})
    if (reply.find(term) != std::string::npos) return false;
  return true;
}

inline bool unavailabilityText(const std::string& reply) {
  bool uncertain = false;
  for (const auto* term : {"不能", "无法", "不清楚", "看不到", "查不到", "查不了", "没法", "还需", "暂不"})
    if (reply.find(term) != std::string::npos) uncertain = true;
  auto checked = reply;
  for (const auto* refusal : {"不能保证完全不疼", "不能保证不疼", "无法保证完全不疼", "无法保证不疼", "没法保证完全不疼", "没法保证不疼"}) {
    const auto pos = checked.find(refusal);
    if (pos != std::string::npos) checked.replace(pos, std::string(refusal).size(), "不能承诺");
  }
  return uncertain && conversationOnlyText(checked);
}

// Keep full sources in the trace, but never append them to the chat bubble.
// Citation validation and numeric checks are guardrails, not a semantic proof;
// the versioned prompt and controlled examples also enforce faithful paraphrase.
inline bool isSimpleThanks(std::string question) {
  for (const auto* punctuation : {" ", "\t", "\r", "\n", "。", "，", ",", ".", "！", "!"}) {
    size_t pos;
    while ((pos = question.find(punctuation)) != std::string::npos)
      question.erase(pos, std::string(punctuation).size());
  }
  return question == "谢谢" || question == "谢谢你" || question == "谢谢您" ||
      question == "好谢谢" || question == "好的谢谢" || question == "好的谢谢你" ||
      question == "好谢谢你" || question == "谢谢了";
}

inline json groundedReply(const json& source, const json& context,
                           const json& bundle, const std::string& trace_id,
                           const std::string& question = "") {
  if (!source.is_object()) throw std::invalid_argument("grounded reply must be an object");
  EvidenceValidator validator(context, bundle, trace_id);
  if (!validator.valid()) throw std::runtime_error("RAG evidence context mismatch");
  const bool conflicted = bundle.contains("conflicts") && !bundle["conflicts"].empty();
  std::string reply = evidenceString(source, "reply"), authority;
  json citations = json::array();
  std::set<std::string> selected;
  bool invalid_selection = false;
  if (!conflicted && source.contains("evidenceIds") && source["evidenceIds"].is_array()) {
    for (const auto& id : source["evidenceIds"]) {
      if (!id.is_string()) { invalid_selection = true; continue; }
      if (!selected.insert(id.get<std::string>()).second) continue;
      const auto result = validator.resolve({{"traceId", trace_id}, {"evidenceId", id}});
      if (result.is_null()) { invalid_selection = true; continue; }
      authority += "\n" + result["text"].get<std::string>();
      citations.push_back(result["citation"]);
    }
  }
  bool conversation = !invalid_selection && citations.empty() &&
      evidenceString(source, "replyKind") == "conversation" && conversationOnlyText(reply);
  const bool unavailable = !invalid_selection && citations.empty() &&
      evidenceString(source, "replyKind") == "unavailable" && unavailabilityText(reply);
  bool usable = conversation || unavailable || (!invalid_selection && !citations.empty() && citations.size() <= 3 &&
                conciseGroundedText(reply, authority));
  for (const auto* claim : {"可以安排", "可以约上", "肯定能约", "肯定有号"})
    if (reply.find(claim) != std::string::npos) usable = false;
  // An exact, standalone thank-you has no factual question to defer for verification.
  // Never apply this fallback to messages that also contain a substantive question.
  if (!usable && !conflicted && isSimpleThanks(question)) {
    reply = "不客气。";
    citations = json::array();
    conversation = usable = true;
  }
  // A starting price must not become a total quote; preserve the typed qualifier,
  // unit and assessment condition even when the model otherwise writes fluently.
  if (usable && reply.find("元") != std::string::npos) {
    for (const auto& fact : bundle["facts"]) {
      if (!selected.count(evidenceString(fact, "evidenceId")) || evidenceString(fact, "field") != "price") continue;
      const auto& price = fact["value"];
      if (evidenceString(price, "type") == "starting_from" && reply.find("起") == std::string::npos) usable = false;
      const std::map<std::string, std::string> units = {{"per_tooth", "颗"}, {"per_case", "例"},
          {"per_visit", "次"}, {"per_arch", "牙弓"}, {"per_item", "项"}};
      const auto unit = units.find(evidenceString(price, "unit"));
      if (unit == units.end() || reply.find(unit->second) == std::string::npos) usable = false;
      if (reply.find("确认") == std::string::npos && reply.find("评估") == std::string::npos &&
          reply.find("检查") == std::string::npos) usable = false;
    }
  }
  const bool missing = bundle.contains("missingFields") && !bundle["missingFields"].empty();
  if (!usable || conflicted) {
    citations = json::array();
    const std::map<std::string, std::string> unavailable = {
        {"appointment", "具体号源需要门诊实时确认，目前还不能确定能否约上。"},
        {"price", "具体费用还需核实，目前不能给您准确报价。"},
        {"duration", "具体需要多久还不能确认，需医生检查后评估。"},
        {"promotion", "优惠活动还需向门诊核实，目前不能确认有这项优惠。"},
        {"medical", "仅凭描述还不能判断，建议联系医生进一步评估。"}};
    const auto topic = unavailable.find(evidenceString(source, "unavailableTopic"));
    reply = conflicted ? "这项信息还需核对，暂时不能给您准确答复。确认后再向您说明。"
        : topic != unavailable.end() ? topic->second : "这项信息暂时还不能确认，需要向门诊核实后再答复您。";
  }
  return {{"reply", reply}, {"replyKind", conversation && usable && !conflicted ? "conversation" : "answer"},
      {"answerStatus", conflicted ? "conflicted" : conversation && usable ? "answered" : citations.empty() ? "unknown" : missing ? "partial" : "answered"},
      {"citations", citations}, {"learningPoints", conversation && usable && !conflicted ? json::array() : json::array({"先直接回答当前问题，只补充影响答案的必要条件。", "用患者听得懂的话说明，保留范围和条件，不作保证。"})},
      {"complianceBoundary", conversation && usable && !conflicted ? "" : "客服仅说明已发布的服务资料，具体诊疗判断需由医生结合检查评估。"},
      {"shouldEnd", !conversation && source.contains("shouldEnd") && source["shouldEnd"].is_boolean() && source["shouldEnd"].get<bool>()},
      {"traceId", trace_id}, {"evidenceBundle", bundle}};
}

inline json groundedSummary(const json& context, const json& public_traces) {
  json facts = json::array(), citations = json::array();
  std::set<std::string> seen;
  for (const auto& trace : public_traces) {
    if (!trace.value("isPublic", false) || !trace.contains("evidence") ||
        !trace.contains("citations") || !trace["citations"].is_array()) continue;
    if (trace["evidence"].contains("conflicts") && !trace["evidence"]["conflicts"].empty()) continue;
    EvidenceValidator validator(context, trace["evidence"], evidenceString(trace, "traceId"));
    for (const auto& ref : trace["citations"]) {
      const auto result = validator.resolve(ref, true, true);
      if (result.is_null() || !seen.insert(result["text"].get<std::string>()).second) continue;
      facts.push_back({{"text", result["text"]}, {"citation", result["citation"]}});
      citations.push_back(result["citation"]);
      if (facts.size() == 6) break;
    }
    if (facts.size() == 6) break;
  }
  return {{"schemaVersion", 2}, {"knowledgeManifestHash", context.at("manifestHash")},
      {"summary", facts.empty() ? "本次复盘没有可复用的已公开依据，仅整理沟通原则；未核实的信息需要进一步确认。" : "本次复盘复用会话中已公开的依据。请结合原文及适用条件回顾表达，未核实的信息需要进一步确认。"},
      {"coveredTopics", {"患者问题回应与资料核对"}},
      {"keyPrinciples", {"先理解患者关注，再使用可核对的资料说明。", "资料不足时明确说明，具体诊疗判断交由医生评估。"}},
      {"nextPracticeSuggestions", {"练习保留资料中的单位、范围和适用条件。"}},
      {"groundedFacts", facts}, {"citations", citations}};
}

}  // namespace oral_training::rag
