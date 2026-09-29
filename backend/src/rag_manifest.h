#pragma once

#include "sha256.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <vector>

namespace oral_training::rag {

// Revision IDs are immutable. Array order and duplicate IDs carry no meaning.
inline nlohmann::json canonicalManifest(const std::string& service_revision,
                                         const nlohmann::json& revisions,
                                         const std::string& scope) {
  if (service_revision.empty() || scope.empty() || !revisions.is_array())
    throw std::invalid_argument("invalid RAG manifest");
  std::vector<std::string> ids;
  for (const auto& item : revisions) {
    if (!item.is_string() || item.get<std::string>().empty())
      throw std::invalid_argument("invalid RAG revision ID");
    ids.push_back(item.get<std::string>());
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return {{"version", 1}, {"serviceRevisionId", service_revision},
          {"knowledgeRevisionIds", ids}, {"trainingScope", scope}};
}

inline std::string manifestHash(const std::string& service_revision,
                                const nlohmann::json& revisions,
                                const std::string& scope) {
  return "sha256:" + oral_training::sha256Hex(
      canonicalManifest(service_revision, revisions, scope).dump());
}

}  // namespace oral_training::rag
