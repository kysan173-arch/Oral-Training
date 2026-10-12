#pragma once
#include "../src/embedding_client.h"
#include "../src/model_completion.h"
#include <cstdlib>
#include <stdexcept>

struct LiveModelFixture {
  std::string model, base_url;
  const char* key;
  int calls = 0;
  explicit LiveModelFixture() {
    key = std::getenv("LIVE_MODEL_API_KEY");
    if (!key) key = std::getenv("MINIMAX_API_KEY");
    if (!key || !*key) throw std::runtime_error("Missing live model credential");
    const auto* name = std::getenv("LIVE_MODEL_NAME");
    if (!name) name = std::getenv("MINIMAX_MODEL");
    model = name ? name : "MiniMax-M2.7";
    const auto* endpoint = std::getenv("LIVE_MODEL_BASE_URL");
    base_url = endpoint ? endpoint : "https://api.minimax.cn/v1";
  }
  nlohmann::json complete(const nlohmann::json& messages, int budget, double temperature,
                          bool json_mode, nlohmann::json& record) {
    using namespace oral_training;
    if (calls >= 120) throw std::runtime_error("Live call budget exceeded");
    const auto tokens = models::initialCompletionBudget(model, budget);
    const auto request = models::buildCompletionRequest(model, messages, tokens, temperature, json_mode);
    ++calls;
    record["requestedModel"] = model; record["baseUrl"] = base_url;
    record["maxTokens"] = tokens; record["jsonMode"] = json_mode;
    const auto response = rag::postJson(base_url,"/chat/completions",request,90,key);
    record["httpStatus"] = response.status;
    if (response.status != 200) throw std::runtime_error("Live model HTTP status " + std::to_string(response.status));
    const auto payload = nlohmann::json::parse(response.body);
    record["usage"] = payload.value("usage",nlohmann::json::object());
    record["model"] = payload.value("model",model);
    const auto& choice = payload.at("choices").at(0);
    record["finishReason"] = choice.value("finish_reason","");
    record["modelContent"] = choice.at("message").value("content","");
    if (record["finishReason"] != "stop") throw std::runtime_error("Incomplete model output");
    const auto output = models::parseModelJsonContent(record["modelContent"]);
    record["rawOutput"] = output;
    return output;
  }
};
