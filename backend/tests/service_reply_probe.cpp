#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"
#include <fstream>

// Opt-in live evaluation: read a user's existing snapshot, but never create,
// modify or finish their conversation. Credentials stay inside ModelGateway.
int main(int argc, char** argv) {
  const bool cases_mode = argc == 3 && std::string(argv[1]) == "--cases";
  if (!cases_mode && (argc != 2 || (std::string(argv[1]) != "--live" && std::string(argv[1]) != "--live-current"))) {
    std::cerr << "Run offline checks first; use --live with DATABASE_URL, REPLY_PROBE_USER_ID and REPLY_PROBE_SESSION_ID. Maximum 14 HTTP attempts (7 questions, existing 2-attempt limit).\n";
    return 2;
  }
  try {
    const auto user = getEnv("REPLY_PROBE_USER_ID"), session = getEnv("REPLY_PROBE_SESSION_ID");
    if (user.empty() || session.empty() || getEnv("DATABASE_URL").empty())
      throw std::runtime_error("Missing live probe configuration");
    auto pool = std::make_shared<DatabasePool>(getEnv("DATABASE_URL"), 4, std::chrono::milliseconds(1000));
    ReliableRoleplayDatabase database(pool);
    const auto detail = database.getSession(user, session); // Server-side ownership check.
    if (detail["session"].value("contextVersion", 1) != 2) throw std::runtime_error("Use a grounded session");
    const auto context = database.getRagContext(session);
    const auto scenario = database.getScenarioInternal(detail["session"]["scenarioId"].get<std::string>());
    oral_training::rag::RagRetriever retriever(pool);
    const bool current = std::string(argv[1]) == "--live-current";
    Config config{}; config.model_call_limit = current ? 2 : 14;
    ModelGateway gateway(config, pool, user);
    if (cases_mode) {
      std::ifstream input(argv[2]);
      const auto cases = json::parse(input);
      if (!cases.is_array() || cases.empty() || cases.size() > 12) throw std::runtime_error("Use 1-12 reviewed cases per batch");
      config.model_call_limit = static_cast<int>(cases.size()) * 2;
      ModelGateway batch_gateway(config, pool, user);
      for (const auto& item : cases) {
        const auto question = item.at("question").get<std::string>();
        auto history = item.value("history", json::array());
        oral_training::rag::RetrievalRequest request;
        request.context_id = context["contextId"].get<std::string>();
        request.current_question = question;
        request.purpose = oral_training::rag::RetrievalPurpose::CustomerReply;
        request.recent_question_answers = history;
        const json evidence = retriever.retrieve(context["serviceRevisionId"].get<std::string>(),
            context["manifest"].get<std::vector<std::string>>(), context["knowledgeAsOf"].get<std::string>(),
            context["manifestHash"].get<std::string>(), request, context["trainingScope"].get<std::string>());
        history.push_back({{"role", "learner_patient"}, {"content", question}});
        const auto raw = batch_gateway.groundedServiceReply(scenario, history, evidence);
        const auto answer = normalizeGroundedRoleplayReply(raw, evidence, "probe-" + item.at("id").get<std::string>(), context, question);
        const auto reply = answer["reply"].get<std::string>();
        std::cout << json{{"id", item["id"]}, {"question", question}, {"reply", reply},
            {"characters", utf8Length(reply)}, {"withinLength", utf8Length(reply) <= item.value("maxChars", 80)},
            {"rawReply", raw.value("reply", "")}, {"answerStatus", answer["answerStatus"]},
            {"rawKind", raw.value("replyKind", "")}, {"facts", evidence["facts"]},
            {"citations", answer["citations"].size()}, {"httpAttempts", batch_gateway.modelCallCount()}}.dump() << std::endl;
      }
      return 0;
    }
    json history = current ? database.getHistory(session) : json::array();
    int index = 0;
    for (const auto* question : {"大概需要多久才能完成？", "初诊检查通常要多久？", "单颗大概要多少钱？",
         "能保证3个月内完成吗？", "明天上午十点肯定能约上吗？", "有买一送一的优惠吗？"}) {
      ++index;
      oral_training::rag::RetrievalRequest request;
      request.context_id = context["contextId"].get<std::string>();
      request.current_question = question;
      request.purpose = oral_training::rag::RetrievalPurpose::CustomerReply;
      request.recent_question_answers = history;
      const json evidence = retriever.retrieve(context["serviceRevisionId"].get<std::string>(),
          context["manifest"].get<std::vector<std::string>>(), context["knowledgeAsOf"].get<std::string>(),
          context["manifestHash"].get<std::string>(), request, context["trainingScope"].get<std::string>());
      history.push_back({{"role", "learner_patient"}, {"content", question}});
      const auto raw = gateway.groundedServiceReply(scenario, history, evidence);
      const auto answer = normalizeGroundedRoleplayReply(raw, evidence, "probe-" + std::to_string(index), context);
      const auto text = answer["reply"].get<std::string>();
      std::cout << json{{"question", question}, {"reply", text}, {"characters", utf8Length(text)},
          {"answerStatus", answer["answerStatus"]}, {"citations", answer["citations"].size()},
          {"rawReply", raw.value("reply", "")}, {"modelVersion", gateway.modelVersion()}}.dump() << '\n';
      if (index <= 3 && answer["answerStatus"] == "unknown")
        throw std::runtime_error("Known-answer probe fell back; inspect the evidence and validation");
      history.push_back({{"role", "standard_customer"}, {"content", text}});
      if (current) {
        if (utf8Length(text) > 80) throw std::runtime_error("Simple duration answer is still too verbose");
        return 0;
      }
    }
    const auto legacy = normalizeRoleplayReply(gateway.standardServiceReply(scenario,
        json::array({{{"role", "learner_patient"}, {"content", "你好，我想先了解预约流程。"}}})));
    std::cout << json{{"mode", "legacy"}, {"reply", legacy["reply"]},
        {"characters", utf8Length(legacy["reply"].get<std::string>())}, {"httpAttempts", gateway.modelCallCount()}}.dump() << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Reply probe failed: " << e.what() << '\n';
    return 1;
  }
}
