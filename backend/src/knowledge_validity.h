#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace oral_training::rag {

// Store validation guarantees ISO calendar dates. The caller supplies the
// locked timestamp converted to an Asia/Shanghai calendar date by PostgreSQL.
inline bool effectiveOnDate(const nlohmann::json& value, const std::string& from_key,
                            const std::string& until_key, const std::string& date) {
  if (!value.is_object()) return true;
  const auto bound = [&](const std::string& key) {
    const auto it = value.find(key);
    return it != value.end() && it->is_string() ? it->get<std::string>() : std::string();
  };
  const auto from = bound(from_key);
  const auto until = bound(until_key);
  return (from.empty() || from <= date) && (until.empty() || date <= until);
}

}  // namespace oral_training::rag
