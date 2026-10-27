#include "test_environment.h"
#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"
#include <fstream>

// Component replay: production evaluator and gateway; no worker/session publication.
int main(int argc, char** argv) {
  if (argc != 3 || (std::string(argv[1]) != "--offline" && std::string(argv[1]) != "--live")) return 2;
  try {
    const bool live = std::string(argv[1]) == "--live";
    std::ifstream input(argv[2]); const auto suite = json::parse(input);
    const auto& cases = suite.at("cases");
    if (cases.empty() || cases.size() > 64) throw std::runtime_error("case limit");
    Config config{};
    config.model_call_limit = live ? getEnvInt("SCORING_REPLAY_MAX_CALLS", 0) : 1;
    if (config.model_call_limit < 1 || config.model_call_limit > 96) throw std::runtime_error("call limit");
    std::shared_ptr<DatabasePool> pool;
#ifndef _WIN32
    std::unique_ptr<TestKeyFile> key_file;
#endif
    if (live) {
      const auto name = getEnv("SCORING_REPLAY_DATABASE"), token = getEnv("SCORING_REPLAY_TOKEN");
      if (!std::regex_match(name, std::regex("scoring_replay_[a-f0-9]{32}")) || token.size() != 64)
        throw std::runtime_error("isolation configuration");
      pool = std::make_shared<DatabasePool>(getEnv("SCORING_REPLAY_DSN"), 2, std::chrono::milliseconds(1000));
      { auto conn = pool->acquire(); pqxx::read_transaction tx(conn.get());
        const auto rows = tx.exec("SELECT current_database(), token, database_oid::text, "
          "(SELECT oid::text FROM pg_database WHERE datname=current_database()) FROM scoring_replay_guard");
        if (rows.size() != 1 || rows[0][0].as<std::string>() != name || rows[0][1].as<std::string>() != token ||
            rows[0][2].as<std::string>() != rows[0][3].as<std::string>()) throw std::runtime_error("isolation mismatch");
      }
#ifndef _WIN32
      key_file = std::make_unique<TestKeyFile>();
#endif
      const auto key = getEnv("SCORING_REPLAY_API_KEY");
      if (key.empty()) throw std::runtime_error("missing key");
      GatewaySettingsStore(pool).save({{"revision", 0}, {"baseUrl", "https://api.minimax.cn/v1"},
          {"model", "MiniMax-M3.1-Flash"}, {"apiKey", key}}, "demo-user-001");
    }
    ModelGateway gateway(config, pool, "demo-user-001");
    json context = {{"contextId", "synthetic-context"}, {"serviceId", "synthetic-service"},
      {"serviceRevisionId", "synthetic-service-r1"}, {"trainingScope", "demo"}, {"manifest", json::array()}};
    int index = 0;
    for (const auto& entry : suite.at("knowledge")) { (void)entry; context["manifest"].push_back("synthetic-knowledge-" + std::to_string(++index)); }
    context["manifestHash"] = oral_training::rag::manifestHash(context["serviceRevisionId"], context["manifest"], "demo");
    json bundle = context;
    bundle["facts"] = json::array(); bundle["passages"] = json::array();
    bundle["conflicts"] = json::array(); bundle["missingFields"] = json::array();
    const auto& payload = suite.at("service").at("payload"); index = 0;
    for (const auto& field : oral_training::rag::knowledgeFields()) {
      if (!payload.contains(field) || oral_training::rag::renderFact(field, payload[field]).empty()) continue;
      bundle["facts"].push_back({{"evidenceId", "E" + std::to_string(++index)}, {"field", field},
        {"value", payload[field]}, {"revisionId", context["serviceRevisionId"]}});
    }
    int passage = 0;
    for (const auto& entry : suite.at("knowledge")) {
      const auto revision = context["manifest"].at(passage++);
      bundle["passages"].push_back({{"evidenceId", "E" + std::to_string(++index)}, {"revisionId", revision},
        {"chunkId", revision.get<std::string>() + "-0"}, {"body", entry.at("body")}, {"scope", "service"},
        {"serviceId", context["serviceId"]}, {"trainingScope", "demo"}});
    }
    bool failed = false;
    std::string stop_code;
    for (const auto& item : cases) {
      const int repetitions = item.value("repetitions", 1);
      if (repetitions != 1 && repetitions != 3) throw std::runtime_error("repetitions");
      for (int repetition = 1; repetition <= repetitions; ++repetition) {
        const auto start = std::chrono::steady_clock::now(); const auto before = gateway.modelCallCount();
        json result = {{"caseId", item.at("id")}, {"group", item.at("group")}, {"repetition", repetition},
          {"mode", item.at("mode")}, {"status", live ? "ok" : "offline_only"}, {"model", live ? "MiniMax-M3.1-Flash" : "none"},
          {"rawReport", nullptr}, {"report", nullptr}, {"knowledgeAssessment", nullptr}, {"rawSummary", nullptr}, {"summary", nullptr}};
        if (!stop_code.empty()) {
          result["status"] = "not_run"; result["error"] = {{"code", stop_code}};
          result["httpAttempts"] = 0; result["elapsedMs"] = 0;
          std::cout << result.dump() << std::endl; continue;
        }
        try {
          const auto& history = item.at("history"); auto evidence = bundle;
          if (item.value("evidence", "normal") == "missing") { evidence["facts"] = json::array(); evidence["passages"] = json::array(); }
          if (item.value("evidence", "normal") == "conflict") evidence["conflicts"] = json::array({"synthetic conflicting source"});
          if (item.at("mode") == "training") {
            const auto candidates = live ? gateway.extractKnowledgeClaims(history) : json::array();
            auto assessment = oral_training::rag::evaluateKnowledge(context, history, candidates,
                [&](const std::string&, const std::string&) { return evidence; });
            assessment["claimCandidates"] = candidates; result["knowledgeAssessment"] = assessment;
            if (live) {
              const auto raw = gateway.evaluateCommunication(history, assessment); result["rawReport"] = raw;
              auto communication = raw;
              for (const auto* key : {"medicalCompliance", "empathy", "needsDiscovery", "serviceEtiquette"}) {
                const auto score = communication.at("dimensionScores").at(key);
                if (!score.is_number_integer() || score < 0 || score > 100) throw ApiError(503, "MODEL_SCORE_INVALID", "score");
              }
              communication["strengths"] = verifiedCommunicationStrengths(communication.at("strengths"), history, item.at("id"));
              communication["dimensionScores"]["knowledgeAccuracy"] = 80;
              auto report = normalizeReport(communication, history);
              std::size_t rounds = 0; for (const auto& m : history) if (m.at("role") == "user") ++rounds;
              if (report.at("roundComments").size() != rounds) throw ApiError(503, "MODEL_INVALID_RESPONSE", "coverage");
              report["schemaVersion"] = 2;
              result["report"] = oral_training::rag::prepareKnowledgeReport(report, assessment, context);
            }
          } else if (item.at("mode") == "roleplay") {
            // These traces are explicitly reviewed fixture annotations, not inferred
            // from all service facts. The same production public-citation filter is used.
            json public_traces = json::array();
            for (const auto& annotation : item.at("publicTraces")) {
              const int round = annotation.at("round");
              const auto quote = annotation.at("quote").get<std::string>();
              bool source_found = false;
              for (const auto& message : history)
                if (message.at("role") == "standard_customer" && message.at("round") == round &&
                    !quote.empty() && message.at("content").get<std::string>().find(quote) != std::string::npos)
                  source_found = true;
              if (!source_found) throw std::runtime_error("public trace source mismatch");
              const auto trace_id = item.at("id").get<std::string>() + "-public-" + std::to_string(public_traces.size());
              auto public_evidence = context;
              public_evidence["facts"] = json::array(); public_evidence["passages"] = json::array();
              public_evidence["conflicts"] = evidence.at("conflicts");
              public_evidence["missingFields"] = json::array();
              json citations = json::array();
              for (const auto& field : annotation.at("fields")) {
                bool found = false;
                for (const auto& fact : evidence.at("facts")) {
                  if (fact.at("field") != field) continue;
                  found = true;
                  public_evidence["facts"].push_back(fact);
                  citations.push_back({{"traceId", trace_id}, {"evidenceId", fact.at("evidenceId")},
                    {"manifestHash", context.at("manifestHash")}, {"revisionId", fact.at("revisionId")}});
                }
                if (!found) throw std::runtime_error("public trace refers to unavailable fact");
              }
              public_traces.push_back({{"traceId", trace_id}, {"isPublic", true}, {"round", round},
                {"evidence", public_evidence}, {"citations", citations}});
            }
            auto grounded = oral_training::rag::groundedSummary(context, public_traces);
            const json summary_evidence = {{"groundedFacts", grounded.at("groundedFacts")},
              {"citations", grounded.at("citations")}};
            result["publicTraces"] = public_traces;
            result["summaryEvidence"] = summary_evidence;
            result["deterministicSummary"] = grounded;
            if (live) {
              const json scenario = {{"public", {{"name", suite.at("service").at("name")}}}, {"roleplay", json::object()}};
              try {
                result["rawSummary"] = gateway.groundedRoleplaySummary(scenario, history, summary_evidence);
                auto generated = normalizeRoleplaySummary(result["rawSummary"], history);
                generated["schemaVersion"] = 2;
                generated["knowledgeManifestHash"] = grounded.at("knowledgeManifestHash");
                generated["groundedFacts"] = grounded.at("groundedFacts");
                generated["citations"] = grounded.at("citations");
                generated["generationMode"] = "model-with-evidence";
                result["summary"] = generated;
              } catch (...) {
                grounded["generationMode"] = "deterministic-fallback";
                result["summary"] = grounded;
                result["modelGenerationFailed"] = true;
                throw; // Keep the failed model status; fallback is not model success.
              }
            }
          } else throw std::runtime_error("invalid mode");
        } catch (const ApiError& e) { failed = true;
          if (e.code == "MODEL_AUTH_FAILED" || e.code == "MODEL_RATE_LIMITED" ||
              e.code == "MODEL_CALL_BUDGET_EXHAUSTED" || e.code == "MODEL_NOT_CONFIGURED") stop_code = e.code;
          result["status"] = "error"; result["error"] = {{"code", e.code}};
        } catch (const std::exception&) { failed = true; result["status"] = "error"; result["error"] = {{"code", "REPLAY_VALIDATION_FAILED"}}; }
        result["httpAttempts"] = gateway.modelCallCount() - before;
        result["elapsedMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        std::cout << result.dump() << std::endl;
      }
    }
    return failed ? 1 : 0;
  } catch (const std::exception&) { std::cerr << "Scoring replay configuration failed (details suppressed to protect credentials).\n"; return 1; }
}
