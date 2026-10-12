#include "../src/evidence_validator.h"
#include "../src/rag_types.h"
#include <iostream>
#include <stdexcept>

using namespace oral_training::rag;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
json context() {
  json c = {{"contextId", "ctx1"}, {"serviceId", "svc1"}, {"serviceRevisionId", "sr1"},
            {"trainingScope", "demo"}, {"manifest", {"kr1", "kr2"}}};
  c["manifestHash"] = manifestHash("sr1", c["manifest"], "demo");
  return c;
}
json bundle() {
  auto b = context();
  b["facts"] = json::array({{{"evidenceId", "E1"}, {"revisionId", "sr1"}, {"field", "price"},
      {"displayText", "3980 元总价"}, {"value", {{"status", "known"}, {"type", "starting_from"},
      {"currency", "CNY"}, {"amountMinor", 398000}, {"unit", "per_tooth"},
      {"conditions", "需检查后确认"}, {"validUntil", "2026-12-31"}}}}});
  b["passages"] = json::array({{{"evidenceId", "E2"}, {"revisionId", "kr1"}, {"chunkId", "kr1-c0"},
      {"body", "疗程依检查结果确定，不能保证固定时间完成。"}, {"scope", "service"},
      {"serviceId", "svc1"}, {"trainingScope", "demo"}}});
  b["conflicts"] = json::array();
  b["missingFields"] = json::array();
  return b;
}
json ref(const char* id = "E1") { return {{"traceId", "t1"}, {"evidenceId", id}}; }
}
int main() {
  try {
    require(oral_training::sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA empty vector");
    require(oral_training::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA abc vector");
    require(oral_training::sha256Hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA multi-block vector");
    auto c = context(), b = bundle();
    const auto thanks = groundedReply({{"reply", "暂时不能确认"}, {"replyKind", "answer"},
        {"evidenceIds", json::array()}, {"shouldEnd", true}}, c, b, "t1", "好，谢谢。");
    require(thanks["reply"] == "不客气。" && thanks["learningPoints"].empty() &&
        thanks["complianceBoundary"] == "" && !thanks["shouldEnd"].get<bool>(), "standalone thanks fallback stays conversational and open");
    require(!isSimpleThanks("谢谢，那多少钱？"), "thanks plus factual question must not bypass grounding");
    require(manifestHash("sr1", {"kr2", "kr1", "kr1"}, "demo") == c["manifestHash"], "manifest order and duplicates");
    require(manifestHash("sr2", c["manifest"], "demo") != c["manifestHash"], "service revision participates in hash");
    require(manifestHash("sr1", c["manifest"], "production") != c["manifestHash"], "scope participates in hash");
    require(manifestHash("sr1", json::array(), "demo").size() == 71, "empty knowledge manifest supported");
    bool rejected = false;
    try { manifestHash("sr1", {1}, "demo"); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "non-string revision rejected");
    EvidenceValidator v(c, b, "t1");
    require(v.valid(), "valid bundle");
    auto resolved = v.resolve(ref());
    require(!resolved.is_null(), "valid evidence resolves");
    require(resolved["text"].get<std::string>().find("3980 元起/颗") != std::string::npos, "price rendered from value");
    require(resolved["text"].get<std::string>().find("2026-12-31") != std::string::npos, "price validity retained");
    require(resolved["citation"]["manifestHash"] == c["manifestHash"], "citation hash matches context");
    require(v.resolve({{"traceId", "other"}, {"evidenceId", "E1"}}).is_null(), "foreign trace E1 rejected");
    require(v.resolve(ref(), true, false).is_null(), "private evidence rejected");
    require(v.resolve(ref(), true, true).is_object(), "public evidence allowed");
    require(v.resolve(ref("missing")).is_null(), "unknown ID rejected");
    for (const auto* key : {"manifestHash", "revisionId"}) {
      auto r = ref(); r[key] = "foreign";
      require(v.resolve(r).is_null(), "forged citation metadata rejected");
    }
    for (const auto* key : {"contextId", "serviceId", "serviceRevisionId", "manifestHash", "trainingScope"}) {
      auto bad = b; bad[key] = "other";
      require(!EvidenceValidator(c, bad, "t1").valid(), "bundle binding checked");
    }
    auto bad = b; bad["facts"][0]["revisionId"] = "sr2";
    require(EvidenceValidator(c, bad, "t1").resolve(ref()).is_null(), "foreign service fact rejected");
    for (const auto* key : {"revisionId", "serviceId", "trainingScope", "scope"}) {
      bad = b; bad["passages"][0][key] = "other";
      require(EvidenceValidator(c, bad, "t1").resolve(ref("E2")).is_null(), "passage scope checked");
    }
    bad = b; bad["passages"][0]["scope"] = "general"; bad["passages"][0]["serviceId"] = "";
    require(!EvidenceValidator(c, bad, "t1").resolve(ref("E2")).is_null(), "general knowledge allowed");
    bad = b; bad["passages"][0]["evidenceId"] = "E1";
    require(!EvidenceValidator(c, bad, "t1").valid(), "duplicate evidence IDs rejected");
    for (const auto* key : {"unit", "amountMinor", "currency", "conditions"}) {
      auto price = b["facts"][0]["value"]; price.erase(key);
      require(renderFact("price", price).empty(), "incomplete typed price rejected");
    }
    auto price = b["facts"][0]["value"]; price["status"] = "unknown";
    require(renderFact("price", price).empty(), "unknown price not rendered");
    price = b["facts"][0]["value"]; price["type"] = "range"; price["minimumMinor"] = 399001; price["maximumMinor"] = 499099;
    require(renderFact("price", price).find("3990.01—4990.99 元/颗") != std::string::npos, "range and fractional yuan exact");
    price["maximumMinor"] = 1;
    require(renderFact("price", price).empty(), "inverted range rejected");
    json duration = {{"status", "known"}, {"minimum", 3}, {"maximum", 6}, {"unit", "month"}, {"estimated", true}, {"conditions", "因人而异"}};
    require(renderFact("treatmentDuration", duration) == "完整疗程：约 3—6个月；因人而异", "duration conditions retained");
    duration["unit"] = "nonsense";
    require(renderFact("treatmentDuration", duration).empty(), "unknown unit rejected");
    auto reply = groundedReply({{"evidenceIds", json::array()}}, c, b, "t1");
    auto social = groundedReply({{"reply", "不客气。"}, {"replyKind", "conversation"}, {"evidenceIds", json::array()}}, c, b, "t1");
    require(social["reply"] == "不客气。" && social["answerStatus"] == "answered", "thanks is not a missing-evidence error");
    require(social["citations"].empty() && social["learningPoints"].empty() && social["complianceBoundary"] == "", "simple chat has no citation or teaching boilerplate");
    require(conversationOnlyText("您更担心打针，还是做完后的那几天？"), "clarifying unknown duration is not a factual assertion");
    require(unavailabilityText("抱歉，刚才没说清楚。周六的实时号源我这里看不到，还得由门诊确认。"), "natural apology retains uncertainty");
    require(unavailabilityText("不能保证完全不疼，这点我得跟您说清楚。"), "honest refusal is not a pain guarantee");
    require(unavailabilityText("没法保证完全不疼。"), "colloquial refusal retains its meaning");
    require(unavailabilityText("不能保证按指定时间完成，具体需医生检查评估。"),
            "negative completion guarantee is an honest capability limit");
    require(!unavailabilityText("不能保证按期完成，但保证治愈。"),
            "a negative clause must not hide a positive clinical guarantee");
    require(conversationOnlyText("可以先核对两家报价包含的项目明细，确认是否在同等条件下比较。"),
            "complete quote-comparison advice is not an inclusion claim");
    require(conversationOnlyText("不同报价可能包含的项目不同，可以先核对两家的项目明细，看看是否在同等条件下比较。"),
            "generic comparison preface is not a specific clinic inclusion claim");
    require(!conversationOnlyText("可以先核对两家报价包含的项目明细，确认是否在同等条件下比较。我们包含拍片。"),
            "quote advice cannot hide an unsupported inclusion claim");
    const json no_slot={{"reply","明天已约好"},{"evidenceIds",json::array()},{"unavailableTopic","appointment"}};
    const auto first_slot=groundedReply(no_slot,c,b,"first-slot","明天能约吗？");
    const auto next_slot=groundedReply(no_slot,c,b,"next-slot","怎么又是确认？",first_slot["reply"]);
    require(first_slot["reply"]!=next_slot["reply"] && next_slot["reply"].get<std::string>().find("看不到实时号源")!=std::string::npos,
        "repeated unavailable answer must explain actual limitation");
    require(next_slot["citations"].empty() && next_slot["answerStatus"]=="unknown","follow-up invented availability");
    const auto third_slot=groundedReply(no_slot,c,b,"third-slot","还是不知道吗？",next_slot["reply"],
        json::array({first_slot["reply"],next_slot["reply"]}));
    require(third_slot["reply"]!=first_slot["reply"] && third_slot["reply"]!=next_slot["reply"],"fallback cycles between old replies");
    const json repeated_slot={{"reply","抱歉，我能解释已有服务资料，但看不到实时号源，也不能代为预约。"},
        {"replyKind","unavailable"},{"evidenceIds",json::array()},{"unavailableTopic","appointment"}};
    const auto slot1=groundedReply(repeated_slot,c,b,"slot1","周六能约吗？");
    const auto slot2=groundedReply(repeated_slot,c,b,"slot2","替我占位。",slot1["reply"]);
    const auto slot3=groundedReply(repeated_slot,c,b,"slot3","怎么又要确认？",slot2["reply"],
        json::array({slot1["reply"],slot2["reply"]}));
    require(slot3["reply"].get<std::string>().find("抱歉")!=std::string::npos &&
            slot3["reply"].get<std::string>().find("我能")!=std::string::npos && slot3["citations"].empty(),
            "repeated booking fallback loses acknowledgement and capability explanation");
    const auto fake_followup=groundedReply({{"reply","我看不到实时号源，需要帮您跟门诊确认一下。"},
        {"replyKind","unavailable"},{"evidenceIds",json::array()},{"unavailableTopic","appointment"}},c,b,"no-followup");
    require(fake_followup["reply"].get<std::string>().find("帮您")==std::string::npos,"unsupported follow-up promise accepted");
    const json honest_limit = {{"reply", "抱歉让您重复确认。我这边只能解释已有的服务资料，看不到实时号源，也无法替您预约。"},
        {"replyKind", "unavailable"}, {"evidenceIds", json::array()}, {"unavailableTopic", "appointment"}};
    require(groundedReply(honest_limit,c,b,"honest-limit")["reply"] == honest_limit["reply"],
            "negated booking capability is not an invented booking action");
    for (const auto* text : {"无法替您预约，但可以安排。", "不能帮您查询，不过我会联系门诊。", "不是不能替您预约。", "并非无法替您预约。"}) {
      auto unsafe_limit = honest_limit;
      unsafe_limit["reply"] = text;
      require(groundedReply(unsafe_limit,c,b,"unsafe-limit")["reply"] != text,
              "negated capability must not hide a later or double-negative action claim");
    }
    require(!unavailabilityText("没法确定，但保证完全不疼。"), "positive guarantee still rejected");
    require(!unavailabilityText("不能确定，但我已经预约成功。"), "uncertainty cannot mask invented action");
    for (const auto* unsafe : {"保证无痛。", "只需3个月。", "支持分期。", "已经预约成功。", "当天完成。"}) {
      auto rejectedSocial = groundedReply({{"reply", unsafe}, {"replyKind", "conversation"}, {"evidenceIds", json::array()}}, c, b, "t1");
      require(rejectedSocial["reply"] != unsafe, "conversation cannot bypass grounding");
    }
    require(reply["answerStatus"] == "unknown" && reply["citations"].empty(), "no fallback evidence selection");
    require(groundedReply({{"evidenceIds", {"invalid"}}}, c, b, "t1")["answerStatus"] == "unknown", "illegal selection unknown");
    reply = groundedReply({{"reply", "明天10点已约上"}, {"evidenceIds", json::array()}, {"unavailableTopic", "appointment"}}, c, b, "t1");
    require(reply["reply"] == "具体号源需要门诊实时确认，目前还不能确定能否约上。", "unknown appointment answered directly without inventing live availability");
    reply = groundedReply({{"reply", "买一送一"}, {"evidenceIds", json::array()}, {"unavailableTopic", "promotion"}}, c, b, "t1");
    require(reply["reply"] == "优惠活动还需向门诊核实，目前不能确认有这项优惠。", "missing promotion does not imply promotion exists or is unavailable");
    for (const std::string attack : {"3980元总价", "明天有号", "三个月完成", "九折优惠", "保证治愈", "１００％有效", "服务包含全部检查"}) {
      reply = groundedReply({{"evidenceIds", {"E1"}}, {"intro", attack}, {"learningPoints", {attack}}, {"complianceBoundary", attack}}, c, b, "t1");
      require(reply.dump().find(attack) == std::string::npos, "model free-text fact injection blocked");
    }
    const json concise = {{"reply", "每颗3980元起，具体费用需检查后确认。"}, {"evidenceIds", {"E1"}}};
    reply = groundedReply(concise, c, b, "t1");
    require(reply["reply"] == concise["reply"] && reply["answerStatus"] == "answered", "use concise model answer, not raw sources");
    require(reply["citations"].size() == 1 && reply["evidenceBundle"] == b, "keep full audit evidence outside bubble");
    auto budget_reply = concise;
    budget_reply["reply"] = "您的3000元预算不够。每颗3980元起，具体费用需检查后确认。";
    const auto budget_result = groundedReply(budget_reply, c, b, "t1", "我只有三千预算够吗？");
    require(budget_result["reply"] == "这个预算不够。每颗3980元起，具体费用需检查后确认。" &&
            budget_result["citations"].size() == 1, "patient budget echo must not invalidate a supported quote");
    require(groundedReply(budget_reply,c,b,"t1","种植多少钱？")["answerStatus"] == "unknown",
            "budget rewriting requires an actual budget question");
    budget_reply["reply"] = "您的3000元预算不足。每颗3000元起，检查后确认。";
    require(groundedReply(budget_reply,c,b,"t1","三千预算够吗？")["answerStatus"] == "unknown",
            "patient amount cannot authorize a fabricated service price");
    require(omitRepeatedBudget("费用3000元。", "三千预算够吗？") == "费用3000元。",
            "ordinary price claims must never be removed by budget normalization");
    require(omitRepeatedBudget("每颗3980元起，3000元的预算不够，需检查确认。", "三千预算够吗？") ==
            "每颗3980元起，这个预算不够，需检查确认。", "mid-sentence budget echo retains published price and condition");
    require(reply["reply"].get<std::string>().find("2026-12-31") == std::string::npos, "do not append unrelated source details");
    for (const auto* attack : {"3980元总价。", "每颗398元起，需检查确认。", "每颗3980元起，保证治愈。",
         "三个月完成。", "资料原文：3980元起。", "明天有号。", "每颗3980元起，无需检查。"}) {
      auto candidate = concise; candidate["reply"] = attack;
      reply = groundedReply(candidate, c, b, "t1");
      require(reply["answerStatus"] == "unknown" && reply["citations"].empty(), "reject unsafe or unsupported paraphrase");
    }
    auto forged = concise; forged["evidenceIds"].push_back("foreign");
    require(groundedReply(forged, c, b, "t1")["answerStatus"] == "unknown", "mixed valid and forged references rejected");
    auto lengthy = concise; lengthy["reply"] = std::string(181, 'x');
    require(groundedReply(lengthy, c, b, "t1")["answerStatus"] == "unknown", "long output rejected without truncating conditions");
    bad = b; bad["missingFields"] = {"appointment"};
    require(groundedReply(concise, c, bad, "t1")["answerStatus"] == "partial", "partial answer");
    bad["conflicts"] = {{{"reason", "conflict"}}};
    reply = groundedReply({{"evidenceIds", {"E1"}}}, c, bad, "t1");
    require(reply["answerStatus"] == "conflicted" && reply["citations"].empty(), "conflict must not choose side");
    bad = b; bad["passages"][0]["body"] = std::string(3000, 'x') + "疗程需检查后确定，不能保证固定时间完成。";
    reply = groundedReply({{"reply", "疗程需检查后确定，不能保证固定时间完成。"}, {"evidenceIds", {"E2"}}}, c, bad, "t1");
    require(reply["citations"].size() == 1 && textLength(reply["reply"]) < 60, "long sources summarized, not copied into chat");
    require(reply["evidenceBundle"] == bad, "full source retained without truncation");
    bad = b;
    bad["facts"] = json::array({{{"evidenceId", "E3"}, {"revisionId", "sr1"}, {"field", "treatmentDuration"},
      {"value", {{"status", "known"}, {"minimum", 3}, {"maximum", 8}, {"unit", "month"}, {"estimated", true}, {"conditions", "受愈合情况影响"}}}}});
    auto durationReply = json{{"reply", "完整疗程约3—8个月，具体取决于愈合情况，需医生检查后确认。"}, {"evidenceIds", {"E3"}}};
    require(groundedReply(durationReply, c, bad, "t1")["reply"] == durationReply["reply"], "screenshot question answered concisely with supported range");
    durationReply["reply"] = "约3—8天完成。";
    require(groundedReply(durationReply, c, bad, "t1")["answerStatus"] == "unknown", "cannot reuse supported numbers with wrong unit");
    durationReply["reply"] = "约3个月完成。";
    require(groundedReply(durationReply, c, bad, "t1")["answerStatus"] == "unknown", "cannot collapse range to its lower endpoint");
    json trace = {{"traceId", "t1"}, {"isPublic", true}, {"evidence", b}, {"citations", {ref()}}};
    auto summary = groundedSummary(c, json::array({trace}));
    require(summary["groundedFacts"].size() == 1, "summary reuses published selected evidence only");
    require(summary["knowledgeManifestHash"] == summary["citations"][0]["manifestHash"], "summary and citation same hash");
    require(summary["groundedFacts"][0]["text"] == resolved["text"], "summary preserves complete fact");
    trace["isPublic"] = false;
    require(groundedSummary(c, json::array({trace}))["groundedFacts"].empty(), "private trace not in summary");
    trace["isPublic"] = true; trace["citations"][0]["traceId"] = "foreign";
    require(groundedSummary(c, json::array({trace}))["groundedFacts"].empty(), "summary rejects foreign E1");
    require(groundedSummary(c, json::array())["groundedFacts"].empty(), "no evidence summary safe");
    std::cout << checks << " evidence checks passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
