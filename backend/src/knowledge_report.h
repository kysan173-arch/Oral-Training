#pragma once
#include "knowledge_evaluator.h"

namespace oral_training::rag {
// Replay the server's evidence reads against the locked conversation before publication.
inline json replayKnowledgeAssessment(const json& context, const json& history, const json& assessment) {
  const auto& traces = assessment.at("knowledgeTraces");
  std::size_t index = 0;
  auto replay = evaluateKnowledge(context, history, assessment.at("claimCandidates"),
      [&](const std::string&, const std::string&) -> json {
        if (index >= traces.size()) throw std::runtime_error("missing report evidence");
        return traces.at(index++).at("evidence");
      });
  if (index != traces.size()) throw std::runtime_error("extra report evidence");
  for (const auto* key : {"knowledgeChecks", "knowledgeAssessment", "knowledgeManifestHash"})
    if (replay.at(key) != assessment.at(key)) throw std::runtime_error("report assessment mismatch");
  return replay;
}

inline json prepareKnowledgeReport(json report, const json& assessment, const json& context) {
  report = applyKnowledgeScores(report, assessment);
  report["serviceRevisionId"] = context.at("serviceRevisionId");
  report["knowledgeAssessment"]["nullReason"] = report["totalScore"].is_null()
      ? "没有可核验的知识计分项；未知或冲突不按错误扣分，也不重分配权重。" : "";
  report["learningMistakes"] = json::array();
  report["recommendedPhrases"] = json::array();
  for (auto& check : report["knowledgeChecks"]) {
    std::string expression;
    check["evidenceTexts"] = json::array();
    for (const auto& ref : check.at("evidenceRefs")) {
      json resolved = nullptr;
      for (const auto& trace : assessment.at("knowledgeTraces")) {
        if (trace.at("traceId") != ref.at("traceId")) continue;
        resolved = EvidenceValidator(context, trace.at("evidence"), trace.at("traceId").get<std::string>()).resolve(ref);
      }
      if (resolved.is_null()) throw std::runtime_error("unresolved report citation");
      check["evidenceTexts"].push_back(resolved.at("text"));
      if (!expression.empty()) expression += "；";
      expression += resolved.at("text").get<std::string>();
    }
    check["recommendedRewrite"] = expression.empty()
        ? "现有资料不足以确认，请核实后再答复；涉及治疗判断需由医生评估。"
        : "您好，已发布资料说明：" + expression + "。具体适用情况需结合检查确认。";
    if (check.value("scoringUnit", false) && !check.value("correctedInLaterRound", false) &&
        check.at("verdict") == "contradicted" && !check.at("evidenceRefs").empty()) {
      report["learningMistakes"].push_back({{"mistakeKey", check.at("checkId")}, {"kind", "knowledge"},
          {"priority", "high"}, {"round", check.at("round")}, {"topic", check.at("topic")},
          {"originalQuote", check.at("originalQuote")}, {"reason", check.at("reason")},
          {"recommendedRewrite", check.at("recommendedRewrite")}, {"evidenceRefs", check.at("evidenceRefs")}});
    }
  }
  return report;
}
} // namespace oral_training::rag
