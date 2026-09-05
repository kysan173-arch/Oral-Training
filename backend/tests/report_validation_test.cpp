#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"

#include <iostream>

int main() {
  if (utf8Length("口腔🙂") != 3 || utf8Truncate("口腔🙂训练", 3) != "口腔🙂") {
    std::cerr << "UTF-8 character counting or truncation failed\n";
    return 1;
  }
  if (aiJobRetryDelaySeconds(1) != 5 || aiJobRetryDelaySeconds(2) != 30) {
    std::cerr << "AI job retry policy changed unexpectedly\n";
    return 1;
  }
  const auto expected_reply = std::string("我还想了解一下具体需要检查什么。");
  const auto plain = parseModelJsonContent("{\"reply\":\"" + expected_reply + "\"}");
  const auto fenced = parseModelJsonContent("```json\n{\"reply\":\"" + expected_reply + "\"}\n```");
  const auto explained = parseModelJsonContent("下面是结果：\n{\"reply\":\"" + expected_reply + "\"}\n以上。");
  const auto thought = parseModelJsonContent("<think>内部思考不应参与解析</think>\n{\"reply\":\"" + expected_reply + "\"}");
  const auto encoded = parseModelJsonContent("\"{\\\"reply\\\":\\\"" + expected_reply + "\\\"}\"");
  if (plain["reply"] != expected_reply || fenced["reply"] != expected_reply ||
      explained["reply"] != expected_reply || thought["reply"] != expected_reply || encoded["reply"] != expected_reply) {
    std::cerr << "model JSON recovery did not preserve the reply\n";
    return 1;
  }
  try {
    (void)parseModelJsonContent("这不是 JSON");
    std::cerr << "invalid model content was accepted\n";
    return 1;
  } catch (const ApiError& error) {
    if (error.code != "MODEL_INVALID_RESPONSE") throw;
  }
  const auto plain_patient = plainPatientReply("患者：我还是有点担心疼痛。");
  if (plain_patient["reply"] != "患者：我还是有点担心疼痛。") {
    std::cerr << "plain patient fallback did not preserve the reply\n";
    return 1;
  }

  const json patient_state = {{"emotion", "犹豫"}, {"emotionLevel", -1}, {"trustLevel", 45}, {"riskTriggered", false}};
  const auto normalized_patient = normalizePatientReply(
      {{"reply", "我还是有些担心费用。"}, {"emotion", 123}, {"emotionLevel", "invalid"},
       {"trustLevel", 200}, {"newlyRevealedInformation", json::array({"预算有限", 42})},
       {"riskTriggered", "false"}, {"shouldEnd", "false"}},
      patient_state);
  if (normalized_patient["emotion"] != "犹豫" || normalized_patient["emotionLevel"] != -1 ||
      normalized_patient["trustLevel"] != 100 || normalized_patient["riskTriggered"] != false ||
      normalized_patient["shouldEnd"] != false || normalized_patient["newlyRevealedInformation"].size() != 1) {
    std::cerr << "patient reply fields were not normalized safely\n";
    return 1;
  }

  const auto json_request = buildCompletionRequest("deepseek-v4-flash", json::array(), 500, 0.4, true);
  const auto fallback_request = buildCompletionRequest("deepseek-v4-flash", json::array(), 1000, 0.0, false);
  if (json_request["response_format"]["type"] != "json_object" || fallback_request.contains("response_format") ||
      fallback_request["max_tokens"] != 1000 || fallback_request["thinking"]["type"] != "disabled") {
    std::cerr << "completion fallback request was not configured correctly\n";
    return 1;
  }

  const std::vector<std::string> dimension_keys = {
      "knowledgeAccuracy", "medicalCompliance", "empathy", "needsDiscovery", "serviceEtiquette",
  };
  json dimension_totals = json::object();
  for (const auto& key : dimension_keys) dimension_totals[key] = 0.0;
  accumulateDimensionScores(dimension_totals,
                            {{"dimensionScores", {{"knowledgeAccuracy", 80}, {"medicalCompliance", 90},
                                                   {"empathy", 75}, {"needsDiscovery", 70},
                                                   {"serviceEtiquette", 85}}}},
                            dimension_keys);
  if (dimension_totals["knowledgeAccuracy"].get<double>() != 80.0 ||
      dimension_totals["medicalCompliance"].get<double>() != 90.0) {
    std::cerr << "dimension scores were not accumulated numerically\n";
    return 1;
  }

  const json messages = json::array({
      {{"role", "patient"}, {"content", "需要拔牙吗？"}, {"round", 0}},
      {{"role", "user"}, {"content", "是否需要拔牙，要由医生结合检查结果评估。"}, {"round", 1}},
  });
  const json safe_report = {
      {"dimensionScores", {{"knowledgeAccuracy", 80}, {"medicalCompliance", 90}, {"empathy", 75},
                            {"needsDiscovery", 70}, {"serviceEtiquette", 85}}},
      {"summary", "能够说明医疗判断边界。"},
      {"strengths", json::array({{{"round", 1}, {"evidence", "说明需要医生评估"}, {"content", "合规边界清楚"}}})},
      {"improvements", json::array({{{"round", 1}, {"content", "可以先回应患者的担忧，再说明由医生评估。"}}})},
      {"violations", json::array()},
      {"roundComments", json::array({{{"round", 1}, {"userMessage", "错误的患者原话"},
                                        {"comment", "边界表达清楚。"},
                                        {"recommendedRewrite", "是否需要拔牙，要由医生结合检查结果评估。"}}})},
  };

  const auto normalized = normalizeReport(safe_report, messages);
  if (normalized["roundComments"][0]["userMessage"] != messages[1]["content"]) {
    std::cerr << "round comment was not grounded in the user message\n";
    return 1;
  }
  if (normalized["recommendedPhrases"].size() != 1 ||
      normalized["recommendedPhrases"][0]["patientSays"] != messages[0]["content"] ||
      normalized["recommendedPhrases"][0]["csReply"] !=
          normalized["roundComments"][0]["recommendedRewrite"]) {
    std::cerr << "report-derived phrase insight was not grounded correctly\n";
    return 1;
  }
  if (normalized["learningMistakes"].size() != 1 ||
      normalized["learningMistakes"][0]["kind"] != "improvement" ||
      normalized["learningMistakes"][0]["originalQuote"] != messages[1]["content"]) {
    std::cerr << "report-derived learning mistake was not created correctly\n";
    return 1;
  }

  const auto expect_invalid_report = [](const json& report, const json& history) {
    try {
      (void)normalizeReport(report, history);
      return false;
    } catch (const ApiError& error) {
      return error.code == "MODEL_INVALID_RESPONSE";
    }
  };
  auto missing_dimension_report = safe_report;
  missing_dimension_report["dimensionScores"].erase("empathy");
  auto invalid_dimension_report = safe_report;
  invalid_dimension_report["dimensionScores"]["empathy"] = "75";
  auto missing_array_report = safe_report;
  missing_array_report.erase("improvements");
  auto missing_summary_report = safe_report;
  missing_summary_report.erase("summary");
  auto empty_strengths_report = safe_report;
  empty_strengths_report["strengths"] = json::array();
  auto missing_violations_report = safe_report;
  missing_violations_report.erase("violations");
  auto invalid_strength_round = safe_report;
  invalid_strength_round["strengths"][0]["round"] = 2;
  auto invalid_improvement_round = safe_report;
  invalid_improvement_round["improvements"][0]["round"] = 2;
  const json two_round_messages = json::array({
      messages[0], messages[1],
      {{"role", "patient"}, {"content", "那我下一步怎么做？"}, {"round", 1}},
      {{"role", "user"}, {"content", "我可以先协助安排医生面诊。"}, {"round", 2}},
  });
  if (!expect_invalid_report(missing_dimension_report, messages) ||
      !expect_invalid_report(invalid_dimension_report, messages) ||
      !expect_invalid_report(missing_array_report, messages) ||
      !expect_invalid_report(missing_summary_report, messages) ||
      !expect_invalid_report(empty_strengths_report, messages) ||
      !expect_invalid_report(missing_violations_report, messages) ||
      !expect_invalid_report(invalid_strength_round, messages) ||
      !expect_invalid_report(invalid_improvement_round, messages) ||
      !expect_invalid_report(safe_report, two_round_messages)) {
    std::cerr << "incomplete report schema or invalid round reference was accepted\n";
    return 1;
  }

  std::atomic<int> heartbeat_renewals{0};
  {
    LeaseHeartbeat heartbeat(
        [&heartbeat_renewals] {
          heartbeat_renewals.fetch_add(1);
          return true;
        },
        std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (heartbeat_renewals.load() < 2) {
    std::cerr << "job lease heartbeat did not renew repeatedly\n";
    return 1;
  }
  std::atomic<int> lost_renewals{0};
  LeaseHeartbeat lost_heartbeat(
      [&lost_renewals] {
        lost_renewals.fetch_add(1);
        return false;
      },
      std::chrono::milliseconds(10));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  lost_heartbeat.stop();
  if (!lost_heartbeat.leaseLost() || lost_renewals.load() != 1) {
    std::cerr << "job lease heartbeat did not stop after ownership loss\n";
    return 1;
  }

  auto capped_deduction_report = safe_report;
  capped_deduction_report["dimensionScores"]["medicalCompliance"] = 60;
  capped_deduction_report["violations"] = json::array({
      {{"round", 1}, {"originalQuote", "是否需要拔牙"}, {"type", "越权判断"},
       {"reason", "具体诊疗判断需要医生结合检查评估。"}, {"deduction", 80},
       {"recommendedRewrite", "是否需要拔牙，要由医生结合检查结果评估。"}}});
  const auto normalized_deduction = normalizeReport(capped_deduction_report, messages);
  if (normalized_deduction["violations"][0]["deduction"] != 50) {
    std::cerr << "violation deduction was not capped at 50\n";
    return 1;
  }
  if (normalized_deduction["learningMistakes"].size() != 1 ||
      normalized_deduction["learningMistakes"][0]["kind"] != "violation" ||
      normalized_deduction["learningMistakes"][0]["priority"] != "high") {
    std::cerr << "violation learning mistake was not classified correctly\n";
    return 1;
  }
  capped_deduction_report["dimensionScores"]["medicalCompliance"] = 61;
  try {
    (void)normalizeReport(capped_deduction_report, messages);
    std::cerr << "inconsistent severe violation score was accepted\n";
    return 1;
  } catch (const ApiError& error) {
    if (error.code != "MODEL_SCORE_INCONSISTENT") throw;
  }

  auto cumulative_deduction_report = safe_report;
  cumulative_deduction_report["dimensionScores"]["medicalCompliance"] = 61;
  cumulative_deduction_report["violations"] = json::array({
      {{"round", 1}, {"originalQuote", "是否需要拔牙"}, {"type", "边界表达不充分"},
       {"reason", "需要明确说明诊疗判断边界。"}, {"deduction", 15},
       {"recommendedRewrite", "是否需要拔牙，要由医生结合检查结果评估。"}},
      {{"round", 1}, {"originalQuote", "医生结合检查结果评估"}, {"type", "沟通信息不完整"},
       {"reason", "还应说明可协助安排面诊。"}, {"deduction", 15},
       {"recommendedRewrite", "是否需要拔牙，要由医生结合检查结果评估。"}},
  });
  try {
    (void)normalizeReport(cumulative_deduction_report, messages);
    std::cerr << "inconsistent cumulative violation score was accepted\n";
    return 1;
  } catch (const ApiError& error) {
    if (error.code != "MODEL_SCORE_INCONSISTENT") throw;
  }
  cumulative_deduction_report["dimensionScores"]["medicalCompliance"] = 60;
  (void)normalizeReport(cumulative_deduction_report, messages);

  auto unsafe_report = safe_report;
  unsafe_report["roundComments"][0]["recommendedRewrite"] = "一般需要1-2年，费用2-5万元。";
  const auto normalized_unsafe = normalizeReport(unsafe_report, messages);
  if (normalized_unsafe["roundComments"][0]["recommendedRewrite"] ==
      unsafe_report["roundComments"][0]["recommendedRewrite"] ||
      normalized_unsafe["roundComments"][0]["recommendedRewrite"].get<std::string>().find("医生") == std::string::npos) {
    std::cerr << "unsafe advice was not replaced with a compliant fallback\n";
    return 1;
  }

  const json roleplay_reply = {
      {"reply", "我理解您担心疼痛。是否适合种植以及具体安排，需要由医生结合面诊检查评估；我可以协助您预约咨询。"},
      {"learningPoints", json::array({"先回应患者的疼痛担忧，再说明可提供的预约协助。", "涉及是否适合治疗时，要明确由医生结合检查评估。"})},
      {"complianceBoundary", "客服不判断是否适合治疗，具体情况需由医生结合检查评估。"},
      {"shouldEnd", false},
  };
  const auto normalized_roleplay_reply = normalizeRoleplayReply(roleplay_reply);
  if (normalized_roleplay_reply["learningPoints"].size() != 2 ||
      normalized_roleplay_reply["complianceBoundary"].get<std::string>().find("医生") == std::string::npos) {
    std::cerr << "roleplay reply was not normalized\n";
    return 1;
  }
  auto unsafe_roleplay_reply = roleplay_reply;
  unsafe_roleplay_reply["reply"] = "您的情况肯定没问题，完全正常。";
  const auto normalized_unsafe_roleplay_reply = normalizeRoleplayReply(unsafe_roleplay_reply);
  if (normalized_unsafe_roleplay_reply["reply"] == unsafe_roleplay_reply["reply"] ||
      normalized_unsafe_roleplay_reply["reply"].get<std::string>().find("医生") == std::string::npos) {
    std::cerr << "unsafe roleplay reply was not replaced with a compliant fallback\n";
    return 1;
  }
  auto malformed_roleplay_reply = roleplay_reply;
  malformed_roleplay_reply["learningPoints"] = "not an array";
  try {
    (void)normalizeRoleplayReply(malformed_roleplay_reply);
    std::cerr << "malformed roleplay learning points were accepted\n";
    return 1;
  } catch (const ApiError& error) {
    if (error.code != "MODEL_INVALID_RESPONSE") throw;
  }

  const json roleplay_history = json::array({
      {{"role", "learner_patient"}, {"content", "种植牙一般是怎样的流程？"}, {"round", 1}},
      {{"role", "standard_customer"}, {"content", "需要由医生结合检查评估，我可以协助预约。"}, {"round", 1}},
  });
  const json roleplay_summary = {
      {"summary", "本次接待先回应了患者对流程的关心，并清楚说明了由医生评估的服务边界。"},
      {"coveredTopics", json::array({"种植牙咨询流程", "预约与检查安排"})},
      {"keyPrinciples", json::array({"先确认患者最关心的问题，再说明服务安排。", "涉及诊疗判断时由医生结合检查评估。"})},
      {"nextPracticeSuggestions", json::array({"继续练习用同理回应后引导预约。"})},
  };
  const auto normalized_roleplay_summary = normalizeRoleplaySummary(roleplay_summary, roleplay_history);
  if (normalized_roleplay_summary["coveredTopics"].size() != 2 ||
      normalized_roleplay_summary["keyPrinciples"].size() != 2 ||
      normalized_roleplay_summary["nextPracticeSuggestions"].size() != 1) {
    std::cerr << "roleplay summary structure was not normalized\n";
    return 1;
  }
  auto unsafe_roleplay_summary = roleplay_summary;
  unsafe_roleplay_summary["summary"] = "治疗一定成功，完全不用担心。";
  const auto normalized_unsafe_roleplay_summary = normalizeRoleplaySummary(unsafe_roleplay_summary, roleplay_history);
  if (normalized_unsafe_roleplay_summary["summary"] == unsafe_roleplay_summary["summary"] ||
      normalized_unsafe_roleplay_summary["summary"].get<std::string>().find("医生") == std::string::npos) {
    std::cerr << "unsafe roleplay summary was not replaced with a compliant fallback\n";
    return 1;
  }

  auto fabricated_quote_report = safe_report;
  fabricated_quote_report["violations"] = json::array({
      {{"round", 1}, {"originalQuote", "保证一定成功"}, {"type", "疗效保证"},
       {"reason", "不能作出绝对承诺。"}, {"deduction", 30},
       {"recommendedRewrite", "具体情况需要医生结合检查结果评估。"}},
  });
  try {
    (void)normalizeReport(fabricated_quote_report, messages);
    std::cerr << "fabricated quote was accepted\n";
    return 1;
  } catch (const ApiError& error) {
    if (error.code != "MODEL_INVALID_RESPONSE") throw;
  }

  std::cout << "report validation tests passed\n";
  return 0;
}
