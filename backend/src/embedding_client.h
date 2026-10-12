#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>

namespace oral_training::rag {

using Embedding = std::vector<double>;

struct EmbeddingConfig {
  std::string base_url = "http://127.0.0.1:8765/v1";
  std::string model = "embeddinggemma-2";
  // Bump when the weights or document prompting at the same endpoint change.
  std::string revision = "1";
  int dimensions = 768;
  int batch_size = 16;
  int timeout_seconds = 30;
  double min_similarity = 0.65;
  static EmbeddingConfig fromEnvironment();
  void validate() const;
  std::string cacheKey() const;
};

struct JsonHttpResponse { int status; std::string body; };
JsonHttpResponse postJson(const std::string& base_url, const std::string& path,
                          const nlohmann::json& body, int timeout_seconds,
                          const std::string& bearer = "");

class EmbeddingClient {
 public:
  using Transport = std::function<JsonHttpResponse(const nlohmann::json&)>;
  explicit EmbeddingClient(EmbeddingConfig config = EmbeddingConfig::fromEnvironment(),
                           Transport transport = {});
  const EmbeddingConfig& config() const { return config_; }
  std::vector<Embedding> embed(const std::vector<std::string>& inputs,
                               const std::string& task) const;
  static Embedding validatedVector(const nlohmann::json& value, int dimensions);
  static double cosine(const Embedding& a, const Embedding& b);
 private:
  EmbeddingConfig config_;
  Transport transport_;
};

}  // namespace oral_training::rag
