/* 参考答案对比评分模块的纯逻辑单测：不依赖数据库与网络。
 * 覆盖量表完整性、引用逐字校验、数值服务器重判、等级自洽钳制、
 * N/A 反滥用、去重、加权汇总与边界。 */
#include "../src/answer_comparison.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace oral_training::compare;

int main() {
  int checks = 0;
  const auto check = [&](bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
  };
  try {
    // ── 1. 量表完整性 ──────────────────────────────────────────────────
    std::string error;
    check(validateAnswerRubric(&error), error.empty() ? "rubric invalid" : error.c_str());
    check(findCriterion("knowledge.answer_core_question") != nullptr, "criterion lookup");
    check(findCriterion("made.up.criterion") == nullptr, "unknown criterion rejected");
    // 前缀容错：模型常把前缀写成完整维度名，量表用的是短前缀。
    check(findCriterion("medicalCompliance.no_diagnosis") != nullptr, "prefix-tolerant lookup");
    check(findCriterion("needsDiscovery.identify_current_demand") != nullptr, "prefix-tolerant lookup 2");
    check(findCriterion("serviceEtiquette.next_step") != nullptr, "prefix-tolerant lookup 3");
    check(rubricCriterionIds().size() == answerRubric().size(), "criterion id enumeration");
    check(!rubricPromptHint().empty(), "prompt hint produced");

    // 基准对话：患者问价，学员答错（编造单一价格并承诺疗效）。
    const json history = json::array({
        {{"round", 0}, {"role", "patient"}, {"content", "种植牙多少钱？"}},
        {{"round", 1}, {"role", "user"}, {"content", "一口价一万二，包好。"}},
        {{"round", 1}, {"role", "patient"}, {"content", "能便宜点吗？"}},
        {{"round", 2}, {"role", "user"}, {"content", "价格统一，另外要拍片检查。"}}});
    const json references = json::array({
        {{"round", 1}, {"referenceAnswer", "单颗种植包含术前检查，区间大致3800—8800元，需医生面诊评估。"}},
        {{"round", 2}, {"referenceAnswer", "价格按项目固定，不含额外项目，需医生面诊确认。"}}});


    // Stored message rounds differ across speakers. Ignore the final unanswered patient message.
    {
      const json stored = json::array({
        {{"round",0},{"role","patient"},{"content","多少钱？"}},
        {{"round",1},{"role","user"},{"content","6000元。"}},
        {{"round",1},{"role","patient"},{"content","需要多久？"}},
        {{"round",2},{"role","user"},{"content","1小时。"}},
        {{"round",2},{"role","patient"},{"content","可以预约吗？"}}});
      const auto turns = comparisonTurns(stored);
      check(turns.size() == 2 && turns[0]["round"] == 1 && turns[1]["round"] == 2,
            "only answered learner rounds should be generated");
      check(turns[0]["patientQuestion"] == "多少钱？" && turns[1]["patientQuestion"] == "需要多久？",
            "learner aligned with preceding question");
      check(turns[0]["historyBeforeAnswer"].size() == 1 && turns[1]["historyBeforeAnswer"].size() == 3,
            "reference context leaked current answer or future question");
      const json refs = json::array({{{"round",1},{"referenceAnswer","3800—8800元。"}},
                                    {{"round",2},{"referenceAnswer","1小时。"}}});
      const json item = {{"round",1},{"criterionId","knowledge.factual_accuracy"},
                         {"level",4},{"differenceType","equivalent"}};
      auto normalized = normalizeComparisonChecks(json::array({item,item,item}), stored, refs);
      check(normalized["checks"].size() == 1 && normalized["dropped"].size() == 2,
            "duplicate round item changed denominator");
      const auto once = computeComparisonScores(normalized["checks"]);
      check(once["dimensionScores"]["knowledgeAccuracy"] == 100, "in-range answer scored against next question");
      auto bad = stored; bad[1]["content"] = "12000元。";
      for (const auto* type : {"equivalent", "learner_better"}) {
        auto candidate = item; candidate["differenceType"] = type;
        const auto checked = normalizeComparisonChecks(json::array({candidate}),bad,refs)["checks"][0];
        check(checked["level"] == 0 && checked["differenceType"] == "factual_or_boundary_error",
              "model classification overrode server numeric mismatch");
      }
      auto alias = item; alias["criterionId"] = "knowledgeAccuracy.factual_accuracy";
      check(normalizeComparisonChecks(json::array({item,alias}),stored,refs)["checks"].size()==1,
            "criterion alias bypassed deduplication");
      const auto block = applyAnswerComparison(normalized,stored,refs,json::object(),json::object());
      check(block["rounds"].size()==2 && block["rounds"][0]["patientQuestion"]=="多少钱？" &&
            block["rounds"][1]["learnerAnswer"]=="1小时。", "report pair differs from scored pair");
    }
    // ── 2. 引用逐字校验：取不到即按未覆盖处理并留痕 ──────────────────────
    {
      const json candidates = json::array({
          {{"criterionId", "knowledge.answer_core_question"}, {"round", 1}, {"applicable", true},
           {"level", 4}, {"userQuote", "这句话学员根本没说过"}, {"referenceQuote", ""},
           {"differenceType", "equivalent"}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, history, references);
      check(normalized["checks"][0]["quoteVerified"] == true &&
          normalized["checks"][0]["modelQuoteVerified"] == false, "answer binding and quote audit separated");
      // The complete real answer is the scoring input; transcription quality is audited separately.
      check(normalized["checks"][0]["level"] == 4, "transcription error must not zero actual answer");
    }

    // ── 2b. 佐证引用（referenceQuote）抄错只标记，不归零 ────────────────
    {
      const json candidates = json::array({
          {{"criterionId", "knowledge.answer_core_question"}, {"round", 1}, {"applicable", true},
           {"level", 3}, {"userQuote", "一口价一万二，包好。"},
           {"referenceQuote", "标准答案里根本没有这一句"},
           {"differenceType", "substantive_omission"}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, history, references);
      check(normalized["checks"][0]["quoteVerified"] == true, "learner quote still verified");
      check(normalized["checks"][0]["referenceQuoteVerified"] == false, "reference quote marked unverified");
      // 学员引用是好的，佐证抄错不该抹掉等级；等级只受差异类型钳制约束。
      check(normalized["checks"][0]["level"] == 2, "bad reference quote must not zero the level");
    }

    // ── 3. 等级与差异类型自洽钳制 ───────────────────────────────────────
    {
      const json candidates = json::array({
          {{"criterionId", "knowledge.answer_core_question"}, {"round", 1}, {"applicable", true},
           {"level", 0}, {"userQuote", "一口价一万二，包好。"}, {"referenceQuote", ""},
           {"differenceType", "equivalent"}, {"reason", ""}},
          {{"criterionId", "knowledge.no_unfounded_claim"}, {"round", 1}, {"applicable", true},
           {"level", 4}, {"userQuote", "一口价一万二，包好。"}, {"referenceQuote", ""},
           {"differenceType", "substantive_omission"}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, history, references);
      check(normalized["checks"][0]["level"] == 3, "equivalent must not score below 3");
      check(normalized["checks"][0]["levelAdjusted"] == true, "adjustment flagged");
      check(normalized["checks"][1]["level"] == 2, "omission must not score above 2");
    }

    // ── 4. 数值类由服务器重判 ───────────────────────────────────────────
    check(numericAgreement("大概6000元。", "区间大致3800—8800元。") == 1, "in-range consistent");
    check(numericAgreement("12000元。", "区间大致3800—8800元。") == 0, "out-of-range inconsistent");
    check(numericAgreement("大概6000元。", "需医生面诊评估。") == -1, "no reference number");
    // 中文数字与「学员没说数字」都落到「无从比对」：不猜语义，也不替模型判遗漏。
    check(numericAgreement("一万二。", "区间大致3800—8800元。") == -1, "chinese numeral not parsed");
    check(numericAgreement("具体价格要面诊确认", "区间大致3800—8800元。") == -1, "no learner number");
    {
      // 学员价格超出标准答案区间 → 覆盖为错误档（用阿拉伯数字以便被抽取）
      const json price_history = json::array({
          {{"round", 0}, {"role", "patient"}, {"content", "多少钱？"}},
          {{"round", 1}, {"role", "user"}, {"content", "一口价12000元。"}}});
      const json price_reference = json::array({
          {{"round", 1}, {"referenceAnswer", "区间大致3800—8800元。"}}});
      const json candidates = json::array({
          {{"criterionId", "knowledge.factual_accuracy"}, {"round", 1}, {"applicable", true},
           {"level", 4}, {"userQuote", "一口价12000元。"},
           {"referenceQuote", "区间大致3800—8800元"}, {"differenceType", ""}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, price_history, price_reference);
      check(normalized["checks"][0]["source"] == "server_numeric", "numeric recheck source");
      check(normalized["checks"][0]["level"] == 0, "out-of-range numeric zeroed");
    }
    {
      // 学员数值落在区间内 → 保留模型等级（服务器只校验，不武断加分）
      const json short_history = json::array({
          {{"round", 0}, {"role", "patient"}, {"content", "多少钱？"}},
          {{"round", 1}, {"role", "user"}, {"content", "大概6000元。"}}});
      const json short_reference = json::array({
          {{"round", 1}, {"referenceAnswer", "区间大致3800—8800元。"}}});
      const json candidates = json::array({
          {{"criterionId", "knowledge.factual_accuracy"}, {"round", 1}, {"applicable", true},
           {"level", 3}, {"userQuote", "大概6000元。"},
           {"referenceQuote", "区间大致3800—8800元"}, {"differenceType", ""}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, short_history, short_reference);
      check(normalized["checks"][0]["level"] == 3, "in-range keeps model level");
    }
    {
      // 数值基准必须取自标准答案原文（系统自产），而不是模型抄写的佐证引用——
      // 下面两条的 referenceQuote 都是错的，判定仍应正确。
      const json short_reference = json::array({
          {{"round", 1}, {"referenceAnswer", "区间大致3800—8800元。"}}});
      const json out_of_range_history = json::array({
          {{"round", 0}, {"role", "patient"}, {"content", "多少钱？"}},
          {{"round", 1}, {"role", "user"}, {"content", "一口价12000元。"}}});
      const json out_of_range = json::array({
          {{"criterionId", "knowledge.factual_accuracy"}, {"round", 1}, {"applicable", true},
           {"level", 4}, {"userQuote", "一口价12000元。"}, {"referenceQuote", "抄错了的佐证"},
           {"differenceType", ""}, {"reason", ""}}});
      const auto zeroed = normalizeComparisonChecks(out_of_range, out_of_range_history, short_reference);
      check(zeroed["checks"][0]["source"] == "server_numeric", "numeric recheck ran");
      check(zeroed["checks"][0]["level"] == 0, "out-of-range zeroed via reference text");
      check(zeroed["checks"][0]["quoteVerified"] == true, "learner quote unaffected");
      check(zeroed["checks"][0]["referenceQuoteVerified"] == false, "bad reference quote marked");

      const json in_range_history = json::array({
          {{"round", 0}, {"role", "patient"}, {"content", "多少钱？"}},
          {{"round", 1}, {"role", "user"}, {"content", "大概6000元。"}}});
      const json in_range = json::array({
          {{"criterionId", "knowledge.factual_accuracy"}, {"round", 1}, {"applicable", true},
           {"level", 3}, {"userQuote", "大概6000元。"}, {"referenceQuote", "同样抄错了"},
           {"differenceType", ""}, {"reason", ""}}});
      const auto kept = normalizeComparisonChecks(in_range, in_range_history, short_reference);
      check(kept["checks"][0]["level"] == 3, "in-range keeps level via reference text");
    }

    // ── 5. N/A 反滥用 ──────────────────────────────────────────────────
    {
      // 无据的不适用 → 强制判为适用
      const json candidates = json::array({
          {{"criterionId", "empathy.acknowledge_concern"}, {"round", 1}, {"applicable", false},
           {"notApplicableReason", "患者没有表达情绪"}, {"level", 4},
           {"userQuote", ""}, {"referenceQuote", ""}, {"differenceType", ""}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, history, references);
      check(normalized["checks"][0]["applicable"] == true, "baseless N/A forced applicable");
    }
    {
      // 理由中逐字引述了患者原话 → 允许不适用
      const json candidates = json::array({
          {{"criterionId", "empathy.acknowledge_concern"}, {"round", 1}, {"applicable", false},
           {"notApplicableReason", "患者本轮只是问价「种植牙多少钱？」，未表达情绪"}, {"level", 0},
           {"userQuote", ""}, {"referenceQuote", ""}, {"differenceType", ""}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, history, references);
      check(normalized["checks"][0]["applicable"] == false, "cited N/A accepted");
    }

    // ── 6. 去重：同一问题不重复扣分 ─────────────────────────────────────
    {
      const json candidates = json::array({
          {{"criterionId", "compliance.no_fabricated_policy"}, {"round", 1}, {"applicable", true},
           {"level", 0}, {"userQuote", "一口价一万二，包好。"}, {"referenceQuote", ""},
           {"differenceType", "factual_or_boundary_error"}, {"reason", ""}},
          {{"criterionId", "compliance.no_fabricated_policy"}, {"round", 2}, {"applicable", true},
           {"level", 0}, {"userQuote", "价格统一，另外要拍片检查。"}, {"referenceQuote", ""},
           {"differenceType", "factual_or_boundary_error"}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, history, references);
      check(normalized["checks"][1]["repeatOfRound"] == 1, "repeated error flagged");
      const auto scored = computeComparisonScores(normalized["checks"]);
      check(scored["summary"]["duplicateErrorCount"] == 1, "duplicate excluded from denominator");
    }

    // ── 7. 整场项只计一次 ──────────────────────────────────────────────
    {
      const json candidates = json::array({
          {{"criterionId", "needs.final_demand_clarified"}, {"round", 1}, {"applicable", true},
           {"level", 3}, {"userQuote", ""}, {"referenceQuote", ""}, {"differenceType", ""}, {"reason", ""}},
          {{"criterionId", "needs.final_demand_clarified"}, {"round", 2}, {"applicable", true},
           {"level", 4}, {"userQuote", ""}, {"referenceQuote", ""}, {"differenceType", ""}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, history, references);
      check(normalized["checks"].size() == 1, "whole item counted once");
      check(normalized["dropped"].size() == 1, "duplicate whole item dropped");
    }

    // ── 8. 模型自造编号与未知轮次一律拒绝 ───────────────────────────────
    {
      const json invented = json::array({
          {{"criterionId", "made.up.criterion"}, {"round", 1}, {"applicable", true}, {"level", 0},
           {"userQuote", ""}, {"referenceQuote", ""}, {"differenceType", ""}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(invented, history, references);
      check(normalized["checks"].empty(), "invented criterion rejected");
      check(normalized["dropped"][0]["reason"] == "criterion_not_in_rubric", "rejection recorded");

      const json unknown_round = json::array({
          {{"criterionId", "knowledge.answer_core_question"}, {"round", 9}, {"applicable", true},
           {"level", 3}, {"userQuote", ""}, {"referenceQuote", ""}, {"differenceType", ""}, {"reason", ""}}});
      check(normalizeComparisonChecks(unknown_round, history, references)["checks"].empty(),
            "unknown round rejected");
    }

    // ── 9. 全满分 → 100；权重聚合正确 ──────────────────────────────────
    {
      json full = json::array();
      for (const auto& criterion : answerRubric()) {
        const int round = criterion.scope == std::string_view("whole") ? 0 : 1;
        full.push_back({{"criterionId", std::string(criterion.id)}, {"round", round},
                        {"applicable", true}, {"level", 4}, {"userQuote", ""},
                        {"referenceQuote", ""}, {"differenceType", ""}, {"reason", ""}});
      }
      const auto normalized = normalizeComparisonChecks(full, history, references);
      const auto scored = computeComparisonScores(normalized["checks"]);
      check(scored["totalScore"] == 100, "all-perfect totals 100");
      check(scored["complete"] == true, "all dimensions assessable");
      check(scored["dimensionScores"]["knowledgeAccuracy"] == 100, "dimension score 100");
    }

    // ── 10. 边界：空输入不产生总分 ─────────────────────────────────────
    check(normalizeComparisonChecks(json::array(), history, references)["checks"].empty(),
          "empty candidates");
    {
      const auto scored = computeComparisonScores(json::array());
      check(scored["totalScore"].is_null(), "no checks means no total");
      check(scored["complete"] == false, "missing dimensions flagged");
    }

    // ── 11. 整场项（round=0）的引用跨轮校验 ────────────────────────────
    {
      // 整场项不属于任何一轮；若只在 round=0 的语料里找，它会永远验不过、永远 0 分。
      const json candidates = json::array({
          {{"criterionId", "needs.final_demand_clarified"}, {"round", 0}, {"applicable", true},
           {"level", 3}, {"userQuote", "价格统一，另外要拍片检查。"}, {"referenceQuote", ""},
           {"differenceType", ""}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, history, references);
      check(normalized["checks"][0]["quoteVerified"] == true, "whole item quote resolved across rounds");
      check(normalized["checks"][0]["level"] == 3, "whole item keeps its level");
      check(normalized["checks"][0]["round"] == 0, "whole item round normalised to zero");
    }

    // ── 12. 报告区块：版本组与 delta ───────────────────────────────────
    {
      const json candidates = json::array({
          {{"criterionId", "knowledge.answer_core_question"}, {"round", 1}, {"applicable", true},
           {"level", 4}, {"userQuote", "一口价一万二，包好。"}, {"referenceQuote", ""},
           {"differenceType", ""}, {"reason", ""}}});
      const auto normalized = normalizeComparisonChecks(candidates, history, references);
      const json existing = {{"knowledgeAccuracy", 90}, {"totalScore", 80}};
      const json meta = {{"referencePromptVersion", "reference-answer-v1"},
                         {"comparisonPromptVersion", "answer-comparison-v1"},
                         {"referenceModelVersion", "litellm:base@1"},
                         {"comparisonModelVersion", "litellm:base@1"}};
      const auto block = applyAnswerComparison(normalized, history, references, existing, meta);
      check(block["status"] == "ready", "block status");
      check(block["rubricVersion"] == std::string(kAnswerRubricVersion), "rubric version recorded");
      check(block["formulaVersion"] == std::string(kAnswerComparisonFormulaVersion), "formula version recorded");
      check(block["rounds"].is_array() && block["rounds"].size() == 2, "rounds assembled");
      check(block["rounds"][0]["patientQuestion"] == "种植牙多少钱？", "patient question attached");
      check(!block["rounds"][0]["referenceAnswer"].get<std::string>().empty(), "reference attached");
      check(block["delta"]["knowledgeAccuracy"] == 100 - 90, "delta versus existing score");
    }
  } catch (const std::exception& failure) {
    std::cerr << "FAILED after " << checks << " checks: " << failure.what() << "\n";
    return 1;
  }
  std::cout << "answer_comparison_test passed (" << checks << " checks)\n";
  return 0;
}
