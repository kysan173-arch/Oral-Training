#include "../src/knowledge_store.h"

#include <iostream>

namespace {

using json = nlohmann::json;
using oral_training::knowledge::KnowledgeStoreError;

json validService() {
  return {
      {"name", "演示种植服务 A"}, {"category", "implant"}, {"dataOrigin", "synthetic"},
      {"price", {{"status", "known"}, {"type", "starting_from"}, {"currency", "CNY"},
                 {"amountMinor", 398000}, {"unit", "per_tooth"},
                 {"conditions", "演示套餐口径"}, {"validFrom", "2026-09-01"},
                 {"validUntil", "2026-12-31"}}},
      {"includedItems", json::array({"演示项目一"})}, {"excludedItems", json::array()},
      {"visitDuration", {{"status", "unknown"}, {"reason", "未录入"}}},
      {"treatmentDuration", {{"status", "unknown"}, {"reason", "未录入"}}},
      {"followupInterval", {{"status", "unknown"}, {"reason", "未录入"}}},
      {"appointment", {{"status", "known"}, {"type", "consultation_hours"},
                       {"timezone", "Asia/Shanghai"},
                       {"text", "周一至周五 09:00—17:00"}, {"isLiveAvailability", false}}},
      {"professionalTopics", json::array({"implant-components"})},
      {"scenarioIds", json::array({"implant-basic"})},
      {"adminNotes", "不得进入公开投影"},
  };
}

bool rejected(const json& payload) {
  try {
    oral_training::knowledge::validateServiceDraft(payload);
    return false;
  } catch (const KnowledgeStoreError& error) {
    return error.code == "INVALID_ARGUMENT";
  }
}

}  // namespace

int main() {
  try {
    const auto service = validService();
    oral_training::knowledge::validateServiceDraft(service);
    const auto projection = oral_training::knowledge::servicePublicProjection(service);
    if (projection.contains("adminNotes") ||
        projection["priceDisplay"].get<std::string>().find("3980 元起/颗") == std::string::npos) {
      std::cerr << "service public projection lost price scope or leaked admin notes\n";
      return 1;
    }
    auto invalid_range = service;
    invalid_range["price"] = {{"status", "known"}, {"type", "range"}, {"currency", "CNY"},
                              {"minimumMinor", 500000}, {"maximumMinor", 300000},
                              {"unit", "per_case"}, {"conditions", "演示"}};
    auto invalid_date = service;
    invalid_date["price"]["validFrom"] = "2026-12-31";
    invalid_date["price"]["validUntil"] = "2026-01-01";
    auto invalid_duration = service;
    invalid_duration["visitDuration"] = {
        {"status", "known"}, {"minimum", 0}, {"maximum", 30},
        {"unit", "minute"}, {"estimated", true}};
    auto invalid_unit = service;
    invalid_unit["price"]["unit"] = "total_unspecified";
    if (!rejected(invalid_range) || !rejected(invalid_date) ||
        !rejected(invalid_duration) || !rejected(invalid_unit)) {
      std::cerr << "invalid price, duration, unit or effective dates were accepted\n";
      return 1;
    }

    /* scenarioIds 会被逐条 INSERT 进 service_scenarios（两列均 NOT NULL）。
       非字符串或空串必须在入口就被拒，否则会以 SQL NULL 绑定，
       退化成 `null value in column "scenario_id"` 这种看不出根因的约束错误。 */
    auto null_scenario = service;
    null_scenario["scenarioIds"] = json::array({nullptr});
    auto empty_scenario = service;
    empty_scenario["scenarioIds"] = json::array({""});
    auto nested_scenario = service;
    nested_scenario["scenarioIds"] = json::array({json::array({"a", "b"})});
    auto non_array_scenario = service;
    non_array_scenario["scenarioIds"] = "implant-basic";
    if (!rejected(null_scenario) || !rejected(empty_scenario) ||
        !rejected(nested_scenario) || !rejected(non_array_scenario)) {
      std::cerr << "invalid scenarioIds reached the service_scenarios insert\n";
      return 1;
    }
    auto empty_list_service = service;
    empty_list_service["scenarioIds"] = json::array();
    oral_training::knowledge::validateServiceDraft(empty_list_service);

    const json synthetic_metadata = {
        {"origin", "synthetic"}, {"verification", "unverified"},
        {"sourceTitle", ""}, {"sourceUrl", nullptr}, {"sourceLocator", ""},
        {"applicability", "演示训练"}, {"trainingScope", "demo"},
        {"aliases", json::array({"种植体"})},
    };
    oral_training::knowledge::validateKnowledgeDraft(
        "种植体概念", "这是固定测试语料，不代表真实诊疗建议。", synthetic_metadata);
    oral_training::knowledge::validateGeneratedDraft(
        "knowledge_draft", {{"title", "种植体概念"},
                            {"body", "这是固定测试语料，不代表真实诊疗建议。"},
                            {"metadata", synthetic_metadata}});
    auto falsely_reviewed = synthetic_metadata;
    falsely_reviewed["verification"] = "reviewed";
    try {
      oral_training::knowledge::validateKnowledgeDraft("标题", "正文", falsely_reviewed);
      std::cerr << "synthetic knowledge was allowed to mark itself reviewed\n";
      return 1;
    } catch (const KnowledgeStoreError&) {
    }
    auto forged_source = synthetic_metadata;
    forged_source["sourceTitle"] = "不存在的临床指南";
    try {
      oral_training::knowledge::validateGeneratedDraft(
          "knowledge_draft", {{"title", "标题"}, {"body", "正文"},
                              {"metadata", forged_source}});
      std::cerr << "generated knowledge was allowed to forge a source title\n";
      return 1;
    } catch (const KnowledgeStoreError&) {
    }
    auto manual_service = service;
    manual_service["dataOrigin"] = "manual";
    try {
      oral_training::knowledge::validateGeneratedDraft("service_draft", manual_service);
      std::cerr << "generated service was allowed to claim a manual origin\n";
      return 1;
    } catch (const KnowledgeStoreError&) {
    }

    const auto first_hash = oral_training::knowledge::contentSha256(service);
    auto changed = service;
    changed["name"] = "演示种植服务 B";
    const auto second_hash = oral_training::knowledge::contentSha256(changed);
    if (first_hash.size() != 64 || first_hash == second_hash ||
        first_hash != oral_training::knowledge::contentSha256(service)) {
      std::cerr << "content hash is not stable or content-sensitive\n";
      return 1;
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "knowledge store validation failed: " << error.what() << '\n';
    return 1;
  }
}
