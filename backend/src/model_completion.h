#pragma once
#include "api_error.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>
namespace oral_training::models {
using json = nlohmann::json;
inline std::string trim(std::string text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
inline int initialCompletionBudget(const std::string& model, int requested) {
  return std::max(requested, model.rfind("MiniMax-", 0) == 0 ? 4096 : 1000);
}
inline std::string removeThinkBlocks(std::string value) {
  while (true) {
    const auto start = value.find("<think>");
    if (start == std::string::npos) break;
    const auto end = value.find("</think>", start + 7);
    value.erase(start, end == std::string::npos ? std::string::npos : end + 8 - start);
  }
  return value;
}

inline json parseJsonCandidate(const std::string& candidate) {
  auto parsed = json::parse(candidate, nullptr, false);
  if (parsed.is_discarded()) return json();
  if (parsed.is_string()) parsed = json::parse(parsed.get<std::string>(), nullptr, false);
  return parsed.is_object() ? parsed : json();
}

inline json parseModelJsonContent(std::string content) {
  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
      static_cast<unsigned char>(content[1]) == 0xBB && static_cast<unsigned char>(content[2]) == 0xBF) {
    content.erase(0, 3);
  }
  content = trim(removeThinkBlocks(std::move(content)));
  if (content.empty()) throw ApiError(503, "MODEL_INVALID_RESPONSE", "模型返回内容为空");

  if (const auto parsed = parseJsonCandidate(content); parsed.is_object()) return parsed;

  if (content.rfind("```", 0) == 0) {
    const auto first_line = content.find('\n');
    const auto closing_fence = content.rfind("```");
    if (first_line != std::string::npos && closing_fence > first_line) {
      if (const auto parsed = parseJsonCandidate(trim(content.substr(first_line + 1, closing_fence - first_line - 1)));
          parsed.is_object()) return parsed;
    }
  }

  for (size_t start = content.find('{'); start != std::string::npos; start = content.find('{', start + 1)) {
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t index = start; index < content.size(); ++index) {
      const char current = content[index];
      if (in_string) {
        if (escaped) escaped = false;
        else if (current == '\\') escaped = true;
        else if (current == '"') in_string = false;
        continue;
      }
      if (current == '"') in_string = true;
      else if (current == '{') ++depth;
      else if (current == '}' && --depth == 0) {
        if (const auto parsed = parseJsonCandidate(content.substr(start, index - start + 1)); parsed.is_object()) {
          return parsed;
        }
        break;
      }
    }
  }
  throw ApiError(503, "MODEL_INVALID_RESPONSE", "模型未返回可解析 JSON");
}

inline json buildCompletionRequest(const std::string& model, const json& messages, int max_tokens,
                            double temperature, bool json_output) {
  json request = {
      {"model", model},
      {"messages", messages},
      {"stream", false},
      {"temperature", temperature},
      {"max_tokens", max_tokens},
  };
  if (json_output) request["response_format"] = {{"type", "json_object"}};
  if (model.rfind("MiniMax-", 0) == 0) {
    request["reasoning_split"] = true;
    // MiniMax may accept json_object yet answer in prose after long evidence.
    // Reassert the existing structured-output contract after the conversation.
    request["messages"].push_back({{"role", "system"}, {"content",
        "本次是程序接口调用。最终响应必须是一个完整合法 JSON 对象，字段严格遵循前面系统消息指定的结构。"
        "即使最后一条用户消息是自然语言问题、感谢或告别，也必须使用该 JSON 结构。"
        "不要仅输出 reply 字段里的文字；不要输出 Markdown 或任何 JSON 外的说明。"}});
  }
  return request;
}

}  // namespace oral_training::models
