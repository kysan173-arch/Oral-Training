#pragma once
#include "../../src/embedding_client.h"
#include <memory>

// Deterministic transport for lifecycle/authorization tests. This is deliberately
// NOT a semantic quality benchmark; real model quality belongs to the live probe.
inline std::shared_ptr<oral_training::rag::EmbeddingClient> stubEmbeddings() {
  using namespace oral_training::rag;
  EmbeddingConfig config;
  return std::make_shared<EmbeddingClient>(config, [config](const nlohmann::json& request) {
    nlohmann::json data = nlohmann::json::array();
    size_t index = 0;
    for (const auto& item : request.at("input")) {
      auto text = item.get<std::string>();
      for (size_t at = 0; (at = text.find("种牙", at)) != std::string::npos; at += 9)
        text.replace(at, 6, "种植牙");
      Embedding vector(config.dimensions, 0);
      vector[0] = 100;
      // Byte trigrams are sufficient for deterministic fixture ranking.
      for (size_t i = 0; i + 2 < text.size(); ++i) {
        unsigned hash = 2166136261u;
        for (size_t j = 0; j < 3; ++j) hash = (hash ^ static_cast<unsigned char>(text[i+j])) * 16777619u;
        vector[1 + hash % (config.dimensions - 1)] = 1;
      }
      data.push_back({{"index",index++},{"embedding",vector}});
    }
    return JsonHttpResponse{200,nlohmann::json{{"model",config.model},{"data",data}}.dump()};
  });
}
