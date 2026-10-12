#include "embedding_client.h"
#include "api_error.h"
#include "sha256.h"
#ifdef _WIN32
#include <winhttp.h>
#else
#include <httplib.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <regex>

namespace oral_training::rag {
namespace {
std::string env(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return value && *value ? value : fallback;
}
int envInt(const char* name, int fallback) {
  const auto value = env(name, std::to_string(fallback));
  size_t used = 0;
  const int result = std::stoi(value, &used);
  if (used != value.size()) throw std::invalid_argument(std::string("Invalid ") + name);
  return result;
}
struct Endpoint { std::string origin; std::string path; };
Endpoint endpoint(const std::string& url) {
  // Service configuration only: no credentials, query strings, fragments or redirects.
  static const std::regex pattern(R"(^(https?)://(localhost|127\.0\.0\.1|\[::1\]|[A-Za-z0-9][A-Za-z0-9.-]*)(:[0-9]{1,5})?(/[A-Za-z0-9_./-]*)?$)");
  std::smatch match;
  if (!std::regex_match(url, match, pattern)) throw std::invalid_argument("Invalid model Base URL");
  if (match[1] == "http" && match[2] != "localhost" && match[2] != "127.0.0.1" && match[2] != "[::1]")
    throw std::invalid_argument("Remote model endpoints require HTTPS");
  auto path = match[4].str();
  while (!path.empty() && path.back() == '/') path.pop_back();
  return {match[1].str() + "://" + match[2].str() + match[3].str(), path};
}
[[noreturn]] void unavailable() {
  throw ApiError(503, "RAG_UNAVAILABLE", "EmbeddingGemma 向量服务不可用或返回无效向量，请检查服务与配置");
}
}  // namespace

EmbeddingConfig EmbeddingConfig::fromEnvironment() {
  EmbeddingConfig config;
  config.base_url = env("RAG_EMBEDDING_BASE_URL", config.base_url);
  config.model = env("RAG_EMBEDDING_MODEL", config.model);
  config.revision = env("RAG_EMBEDDING_REVISION", config.revision);
  config.dimensions = envInt("RAG_EMBEDDING_DIMENSIONS", config.dimensions);
  config.batch_size = envInt("RAG_EMBEDDING_BATCH_SIZE", config.batch_size);
  config.timeout_seconds = envInt("RAG_EMBEDDING_TIMEOUT_SECONDS", config.timeout_seconds);
  const auto threshold = env("RAG_EMBEDDING_MIN_SIMILARITY", "0.65");
  size_t used = 0;
  config.min_similarity = std::stod(threshold, &used);
  if (used != threshold.size()) throw std::invalid_argument("Invalid RAG_EMBEDDING_MIN_SIMILARITY");
  config.validate();
  return config;
}

void EmbeddingConfig::validate() const {
  endpoint(base_url);
  if (model.empty() || model.size() > 200 || revision.empty() || revision.size() > 100 ||
      (dimensions != 128 && dimensions != 256 && dimensions != 512 && dimensions != 768) ||
      batch_size < 1 || batch_size > 16 || timeout_seconds < 1 || timeout_seconds > 60 ||
      !std::isfinite(min_similarity) || min_similarity < -1 || min_similarity > 1)
    throw std::invalid_argument("Invalid EmbeddingGemma configuration");
}

std::string EmbeddingConfig::cacheKey() const {
  return oral_training::sha256Hex(nlohmann::json::array(
      {base_url, model, revision, dimensions, "document:title-body-applicability-v1"}).dump());
}

JsonHttpResponse postJson(const std::string& base_url, const std::string& path,
                          const nlohmann::json& body, int timeout_seconds,
                          const std::string& bearer) {
  const auto target = endpoint(base_url);
#ifdef _WIN32
  struct Handle {
    HINTERNET value;
    ~Handle() { if (value) WinHttpCloseHandle(value); }
  };
  const std::wstring wide(target.origin.begin(), target.origin.end());
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) throw std::runtime_error("Invalid model URL");
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  Handle session{WinHttpOpen(L"oral-training-embedding/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.value) throw std::runtime_error("Model HTTP initialization failed");
  WinHttpSetTimeouts(session.value, 5000, 5000, timeout_seconds * 1000, timeout_seconds * 1000);
  Handle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
  const auto resource = target.path + path;
  const std::wstring resource_wide(resource.begin(), resource.end());
  Handle request{WinHttpOpenRequest(connection.value, L"POST", resource_wide.c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
  if (!connection.value || !request.value) throw std::runtime_error("Model HTTP connection failed");
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)))
    throw std::runtime_error("Model HTTP redirect policy failed");
  std::wstring headers = L"Content-Type: application/json\r\n";
  if (!bearer.empty()) headers += L"Authorization: Bearer " + std::wstring(bearer.begin(), bearer.end()) + L"\r\n";
  const auto data = body.dump();
  if (!WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(headers.size()),
      const_cast<char*>(data.data()), static_cast<DWORD>(data.size()), static_cast<DWORD>(data.size()), 0) ||
      !WinHttpReceiveResponse(request.value, nullptr)) throw std::runtime_error("Model HTTP transport failed");
  DWORD status = 0, size = sizeof(status);
  if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
    throw std::runtime_error("Model HTTP status failed");
  std::string output;
  for (;;) {
    char buffer[8192];
    DWORD received = 0;
    if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &received))
      throw std::runtime_error("Model HTTP read failed");
    if (!received) break;
    if (output.size() + received > 4 * 1024 * 1024) throw std::runtime_error("Model response too large");
    output.append(buffer, received);
  }
  return {static_cast<int>(status), output};
#else
  httplib::Client client(target.origin);
  client.set_connection_timeout(5);
  client.set_read_timeout(timeout_seconds);
  client.set_write_timeout(timeout_seconds);
  client.set_follow_location(false);
  // Explicit direct connections keep localhost documents out of proxy services.
  httplib::Headers headers;
  if (!bearer.empty()) headers.emplace("Authorization", "Bearer " + bearer);
  const auto response = client.Post(target.path + path, headers, body.dump(), "application/json",
      [](uint64_t current, uint64_t total) { return current <= 4 * 1024 * 1024 && total <= 4 * 1024 * 1024; });
  if (!response) throw std::runtime_error("Model HTTP transport failed");
  return {response->status, response->body};
#endif
}

EmbeddingClient::EmbeddingClient(EmbeddingConfig config, Transport transport)
    : config_(std::move(config)), transport_(std::move(transport)) { config_.validate(); }

Embedding EmbeddingClient::validatedVector(const nlohmann::json& value, int dimensions) {
  if (!value.is_array() || value.size() != static_cast<size_t>(dimensions)) unavailable();
  Embedding vector;
  double norm = 0;
  for (const auto& element : value) {
    if (!element.is_number()) unavailable();
    const auto number = element.get<double>();
    if (!std::isfinite(number)) unavailable();
    vector.push_back(number);
    norm += number * number;
  }
  if (!std::isfinite(norm) || norm <= 1e-12) unavailable();
  for (auto& number : vector) number /= std::sqrt(norm);
  return vector;
}

std::vector<Embedding> EmbeddingClient::embed(const std::vector<std::string>& inputs,
                                             const std::string& task) const {
  if (task != "query" && task != "document") throw std::invalid_argument("Invalid embedding task");
  std::vector<Embedding> vectors;
  try {
    for (size_t start = 0; start < inputs.size(); start += config_.batch_size) {
      const auto end = std::min(inputs.size(), start + config_.batch_size);
      const std::vector<std::string> batch(inputs.begin() + start, inputs.begin() + end);
      const nlohmann::json request = {{"model", config_.model}, {"input", batch},
          {"dimensions", config_.dimensions}, {"task", task}, {"encoding_format", "float"}};
      const auto response = transport_ ? transport_(request)
          : postJson(config_.base_url, "/embeddings", request, config_.timeout_seconds);
      if (response.status != 200) unavailable();
      const auto payload = nlohmann::json::parse(response.body);
      if (payload.value("model", "") != config_.model || !payload.at("data").is_array() ||
          payload["data"].size() != batch.size()) unavailable();
      std::vector<Embedding> ordered(batch.size());
      for (const auto& item : payload["data"]) {
        if (!item.at("index").is_number_integer()) unavailable();
        const auto index = item["index"].get<int>();
        if (index < 0 || static_cast<size_t>(index) >= batch.size() || !ordered[index].empty()) unavailable();
        ordered[index] = validatedVector(item.at("embedding"), config_.dimensions);
      }
      vectors.insert(vectors.end(), ordered.begin(), ordered.end());
    }
  } catch (const ApiError&) { throw; }
    catch (const std::exception&) { unavailable(); }
  return vectors;
}

double EmbeddingClient::cosine(const Embedding& a, const Embedding& b) {
  if (a.empty() || a.size() != b.size()) throw std::invalid_argument("Embedding dimension mismatch");
  return std::clamp(std::inner_product(a.begin(), a.end(), b.begin(), 0.0), -1.0, 1.0);
}
}  // namespace oral_training::rag
