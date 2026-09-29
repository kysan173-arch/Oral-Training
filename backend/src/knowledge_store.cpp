#include "knowledge_store.h"
#include "rag_retriever.h"
#include "sha256.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace oral_training::knowledge {

std::optional<GenerationTarget> parseGenerationTarget(const std::string& kind) {
  if (kind == "service_draft") return GenerationTarget::ServiceDraft;
  if (kind == "knowledge_draft") return GenerationTarget::KnowledgeDraft;
  if (kind == "scenario_draft") return GenerationTarget::ScenarioDraft;
  return std::nullopt;
}

std::string generationTargetTable(GenerationTarget target) {
  switch (target) {
    case GenerationTarget::ServiceDraft: return "service_drafts";
    case GenerationTarget::KnowledgeDraft: return "knowledge_drafts";
    case GenerationTarget::ScenarioDraft: return "scenarios";
  }
  return std::string();
}

std::string generationTargetLabel(GenerationTarget target) {
  switch (target) {
    case GenerationTarget::ServiceDraft: return "服务草稿";
    case GenerationTarget::KnowledgeDraft: return "知识草稿";
    case GenerationTarget::ScenarioDraft: return "训练场景骨架";
  }
  return std::string();
}

namespace {

[[noreturn]] void invalid(const std::string& message) {
  throw KnowledgeStoreError(400, "INVALID_ARGUMENT", message);
}

std::string requiredString(const json& object, const char* key, std::size_t maximum) {
  if (!object.is_object() || !object.contains(key) || !object[key].is_string()) {
    invalid(std::string(key) + " 必须是字符串");
  }
  const auto value = object[key].get<std::string>();
  if (value.empty() || value.size() > maximum) invalid(std::string(key) + " 长度无效");
  return value;
}

std::optional<std::string> optionalString(const json& object, const char* key,
                                          std::size_t maximum) {
  if (!object.contains(key) || object[key].is_null()) return std::nullopt;
  if (!object[key].is_string() || object[key].get<std::string>().size() > maximum) {
    invalid(std::string(key) + " 格式无效");
  }
  return object[key].get<std::string>();
}

void requireEnum(const std::string& value, const std::set<std::string>& allowed,
                 const std::string& field) {
  if (allowed.find(value) == allowed.end()) invalid(field + " 枚举值无效");
}

void validateStringArray(const json& object, const char* key, std::size_t maximum_items,
                         std::size_t maximum_item_length) {
  if (!object.contains(key)) return;
  if (!object[key].is_array() || object[key].size() > maximum_items) {
    invalid(std::string(key) + " 必须是受限数组");
  }
  for (const auto& item : object[key]) {
    if (!item.is_string() || item.get<std::string>().empty() ||
        item.get<std::string>().size() > maximum_item_length) {
      invalid(std::string(key) + " 包含无效条目");
    }
  }
}

bool isoDate(const std::string& value) {
  if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index == 4 || index == 7) continue;
    if (!std::isdigit(static_cast<unsigned char>(value[index]))) return false;
  }
  return true;
}

void validateEffectiveDates(const json& object, const char* from_key, const char* until_key) {
  const auto from = optionalString(object, from_key, 10);
  const auto until = optionalString(object, until_key, 10);
  if (from && !from->empty() && !isoDate(*from)) invalid(std::string(from_key) + " 日期格式无效");
  if (until && !until->empty() && !isoDate(*until)) invalid(std::string(until_key) + " 日期格式无效");
  if (from && until && !from->empty() && !until->empty() && *from > *until) {
    invalid("有效期起始日期不能晚于结束日期");
  }
}

void validatePrice(const json& price) {
  if (!price.is_object()) invalid("price 必须是对象");
  const auto status = requiredString(price, "status", 20);
  requireEnum(status, {"known", "unknown"}, "price.status");
  if (status == "unknown") {
    requiredString(price, "reason", 300);
    return;
  }
  const auto type = requiredString(price, "type", 40);
  requireEnum(type, {"fixed", "starting_from", "range", "quote_after_assessment"},
              "price.type");
  const auto currency = requiredString(price, "currency", 3);
  if (currency != "CNY") invalid("第一版仅支持 CNY");
  const auto unit = requiredString(price, "unit", 60);
  requireEnum(unit, {"per_tooth", "per_case", "per_visit", "per_arch", "per_item"},
              "price.unit");
  requiredString(price, "conditions", 500);
  const auto positive_integer = [&price](const char* key) {
    return price.contains(key) && price[key].is_number_integer() && price[key].get<long long>() > 0;
  };
  if ((type == "fixed" || type == "starting_from") && !positive_integer("amountMinor")) {
    invalid("固定价或起价必须提供正整数 amountMinor；未知价格请使用 unknown");
  }
  if (type == "range") {
    if (!positive_integer("minimumMinor") || !positive_integer("maximumMinor") ||
        price["minimumMinor"].get<long long>() > price["maximumMinor"].get<long long>()) {
      invalid("价格范围必须是有序的正整数；未知价格请使用 unknown");
    }
  }
  validateEffectiveDates(price, "validFrom", "validUntil");
}

void validateDuration(const json& duration, const std::string& field) {
  if (!duration.is_object()) invalid(field + " 必须是对象");
  const auto status = requiredString(duration, "status", 20);
  requireEnum(status, {"known", "unknown"}, field + ".status");
  if (status == "unknown") {
    requiredString(duration, "reason", 300);
    return;
  }
  if (!duration.contains("minimum") || !duration["minimum"].is_number_integer() ||
      duration["minimum"].get<int>() <= 0 || !duration.contains("maximum") ||
      !duration["maximum"].is_number_integer() || duration["maximum"].get<int>() <= 0 ||
      duration["minimum"].get<int>() > duration["maximum"].get<int>()) {
    invalid(field + " 的时长范围无效");
  }
  const auto unit = requiredString(duration, "unit", 20);
  requireEnum(unit, {"minute", "hour", "day", "week", "month", "year"}, field + ".unit");
  if (duration.contains("estimated") && !duration["estimated"].is_boolean()) {
    invalid(field + ".estimated 必须是布尔值");
  }
  optionalString(duration, "phase", 100);
  optionalString(duration, "conditions", 500);
}

void validateAppointment(const json& appointment) {
  if (!appointment.is_object()) invalid("appointment 必须是对象");
  const auto status = requiredString(appointment, "status", 20);
  requireEnum(status, {"known", "unknown"}, "appointment.status");
  if (status == "unknown") {
    requiredString(appointment, "reason", 300);
    return;
  }
  const auto type = requiredString(appointment, "type", 40);
  requireEnum(type, {"consultation_hours", "appointment_slots"}, "appointment.type");
  requiredString(appointment, "timezone", 80);
  requiredString(appointment, "text", 1000);
  if (!appointment.contains("isLiveAvailability") || !appointment["isLiveAvailability"].is_boolean()) {
    invalid("appointment.isLiveAvailability 必须是布尔值");
  }
  optionalString(appointment, "updatedAt", 40);
}

std::string randomId(const std::string& prefix) {
  std::array<unsigned char, 12> bytes{};
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    throw std::runtime_error("secure random generation failed");
  }
  std::ostringstream output;
  output << prefix << '-';
  for (const auto value : bytes) output << std::hex << std::setw(2) << std::setfill('0')
                                        << static_cast<int>(value);
  return output.str();
}

void requireAdmin(pqxx::transaction_base& tx, const std::string& actor_id) {
  const auto rows = tx.exec_params(
      "SELECT 1 FROM users WHERE id = $1 AND role = 'admin' AND status = 'active'", actor_id);
  if (rows.empty()) throw KnowledgeStoreError(403, "ROLE_FORBIDDEN", "仅管理员可管理知识与服务");
}

json parseJsonField(const pqxx::field& field) {
  if (field.is_null()) return nullptr;
  return json::parse(field.c_str());
}

void writeAudit(pqxx::transaction_base& tx, const std::string& actor_id,
                const std::string& action, const std::string& entity_type,
                const std::string& entity_id, const std::optional<std::string>& old_revision,
                const std::optional<std::string>& new_revision, const std::string& request_id) {
  tx.exec_params(R"(
    INSERT INTO knowledge_audit_events
      (id, actor_id, action, entity_type, entity_id, old_revision_id, new_revision_id, request_id)
    VALUES ($1, $2, $3, $4, $5, $6, $7, NULLIF($8, ''))
  )", randomId("audit"), actor_id, action, entity_type, entity_id,
      old_revision, new_revision, request_id);
}

json serviceRevisionJson(const pqxx::row& row) {
  return {{"revisionId", row["id"].c_str()}, {"version", row["version"].as<int>()},
          {"payload", parseJsonField(row["payload"])}, {"contentHash", row["content_hash"].c_str()},
          {"origin", row["origin"].c_str()}, {"publishedBy", row["published_by"].c_str()},
          {"publishedAt", row["published_at"].c_str()}};
}

json knowledgeRevisionJson(const pqxx::row& row) {
  return {{"revisionId", row["id"].c_str()}, {"version", row["version"].as<int>()},
          {"title", row["title"].c_str()}, {"body", row["body"].c_str()},
          {"metadata", parseJsonField(row["metadata"])}, {"contentHash", row["content_hash"].c_str()},
          {"publishedBy", row["published_by"].c_str()},
          {"publishedAt", row["published_at"].c_str()}};
}

}  // namespace

void validateServiceDraft(const json& payload) {
  if (!payload.is_object()) invalid("服务草稿必须是对象");
  requiredString(payload, "name", 120);
  requiredString(payload, "category", 80);
  const auto origin = requiredString(payload, "dataOrigin", 20);
  requireEnum(origin, {"synthetic", "manual", "reference"}, "dataOrigin");
  if (!payload.contains("price")) invalid("缺少 price");
  validatePrice(payload["price"]);
  validateStringArray(payload, "includedItems", 100, 300);
  validateStringArray(payload, "excludedItems", 100, 300);
  validateStringArray(payload, "professionalTopics", 100, 120);
  validateStringArray(payload, "scenarioIds", 20, 120);
  /* scenarioIds 的每一条都会被 publishService 逐条 INSERT 进 service_scenarios，
     而该表两列都是 NOT NULL —— 元素若以 SQL NULL 绑定，会报成
     `null value in column "scenario_id" ... violates not-null constraint`，
     现场看像数据库故障。
     上面 validateStringArray 已经拦住了非字符串/空串/非数组；
     这里再显式验一遍，是为了让契约在「草稿校验」这层就写明，
     并把报错换成可读中文（否则无法从约束错误定位到输入的那一条）。 */
  for (const auto& scenario_id : payload.value("scenarioIds", json::array())) {
    if (!scenario_id.is_string() || scenario_id.get<std::string>().empty()) {
      invalid("scenarioIds 每一项都必须是非空场景 id");
    }
  }
  for (const auto* field : {"visitDuration", "treatmentDuration", "followupInterval"}) {
    if (!payload.contains(field)) invalid(std::string("缺少 ") + field);
    validateDuration(payload[field], field);
  }
  if (!payload.contains("appointment")) invalid("缺少 appointment");
  validateAppointment(payload["appointment"]);
}

void validateKnowledgeDraft(const std::string& title, const std::string& body,
                            const json& metadata) {
  if (title.empty() || title.size() > 200) invalid("知识标题长度无效");
  if (body.empty() || body.size() > 20000) invalid("知识正文长度无效");
  if (!metadata.is_object()) invalid("知识 metadata 必须是对象");
  const auto origin = requiredString(metadata, "origin", 20);
  requireEnum(origin, {"synthetic", "manual", "reference"}, "metadata.origin");
  const auto verification = requiredString(metadata, "verification", 20);
  requireEnum(verification, {"unverified", "reviewed"}, "metadata.verification");
  const auto scope = requiredString(metadata, "trainingScope", 20);
  requireEnum(scope, {"demo", "verified"}, "metadata.trainingScope");
  requiredString(metadata, "applicability", 1000);
  optionalString(metadata, "sourceTitle", 300);
  const auto source_url = optionalString(metadata, "sourceUrl", 1000);
  optionalString(metadata, "sourceLocator", 300);
  validateStringArray(metadata, "aliases", 100, 120);
  validateEffectiveDates(metadata, "effectiveFrom", "effectiveUntil");
  if (origin == "synthetic" && verification != "unverified") {
    invalid("模拟生成知识不能自动标记为 reviewed");
  }
  if (origin == "synthetic" && source_url && !source_url->empty()) {
    invalid("模拟生成知识不能填写伪造来源 URL");
  }
  if (scope == "verified" && verification != "reviewed") {
    invalid("verified 范围只接受 reviewed 知识");
  }
}

void validateGeneratedDraft(const std::string& kind, const json& candidate) {
  /* 显式分发：未知类型直接拒绝，不再有「不是 service_draft 就当 knowledge_draft」
     的兜底（R09）。新增类型时编译器会在这里报出「未覆盖的枚举值」，
     提醒你补分支——这正是把隐式 if 换成 switch 的收益。 */
  const auto target = parseGenerationTarget(kind);
  if (!target.has_value()) invalid("生成任务类型无效");
  if (target == GenerationTarget::ServiceDraft) {
    validateServiceDraft(candidate);
    if (candidate.value("dataOrigin", "") != "synthetic") {
      invalid("生成服务草稿只能标记为 synthetic");
    }
    return;
  }
  if (target == GenerationTarget::KnowledgeDraft) {
    if (!candidate.is_object() || !candidate.contains("title") || !candidate["title"].is_string() ||
        !candidate.contains("body") || !candidate["body"].is_string() ||
        !candidate.contains("metadata") || !candidate["metadata"].is_object()) {
      invalid("生成知识草稿结构无效");
    }
    validateKnowledgeDraft(candidate["title"].get<std::string>(),
                           candidate["body"].get<std::string>(), candidate["metadata"]);
    const auto& metadata = candidate["metadata"];
    if (metadata.value("origin", "") != "synthetic" ||
        metadata.value("verification", "") != "unverified" ||
        metadata.value("trainingScope", "") != "demo" ||
        (metadata.contains("sourceTitle") && metadata["sourceTitle"].is_string() &&
         !metadata["sourceTitle"].get<std::string>().empty()) ||
        (metadata.contains("sourceLocator") && metadata["sourceLocator"].is_string() &&
         !metadata["sourceLocator"].get<std::string>().empty()) ||
        (metadata.contains("sourceUrl") && !metadata["sourceUrl"].is_null())) {
      invalid("生成知识草稿不得伪造来源或审核标记");
    }
    return;
  }
  /* 场景骨架（迁移 030）：这里只做**形状**检查，让 worker 能快速失败并给出
     可读的失败原因；字段级约束（长度、条数、情绪词表、红线条数）由落库那一步
     统一执行——它由 store 层注入（KnowledgeAdminQueue::ScenarioDraftWriter），
     最终走 ReliableDatabase::validateScenarioPayload，与主管手工保存完全同一条路径。 */
  if (!candidate.is_object()) invalid("生成场景骨架结构无效");
  const std::vector<std::pair<const char*, const char*>> required = {
      {"name", "场景名称"},       {"summary", "场景简介"},
      {"category", "场景分类"},   {"difficulty", "难度"},
      {"focus", "训练重点"},      {"patientProfile", "患者画像"},
      {"hiddenConfig", "隐藏剧本"}};
  for (const auto& field : required) {
    if (!candidate.contains(field.first)) {
      invalid(std::string("生成场景骨架缺少") + field.second);
    }
  }
  if (!candidate["focus"].is_array() || !candidate["patientProfile"].is_object() ||
      !candidate["hiddenConfig"].is_object()) {
    invalid("生成场景骨架字段类型无效");
  }
  /* 机构事实（能否退费、转交谁、答复时限）**只能由主管填**：模型不知道本机构
     的真实流程，编出来的红线会让学员把错的做法练成肌肉记忆。
     所以 AI 骨架的服务要点必须为空，哪怕它「很贴心地」填了也要拒绝。 */
  if (candidate.contains("roleplayConfig") && !candidate["roleplayConfig"].is_null()) {
    const auto& roleplay = candidate["roleplayConfig"];
    if (!roleplay.is_object()) invalid("生成场景骨架的 roleplayConfig 结构无效");
    if (roleplay.contains("serviceGuidance") &&
        roleplay["serviceGuidance"].is_array() && !roleplay["serviceGuidance"].empty()) {
      invalid("AI 骨架不得填写服务要点，机构红线必须由主管填写");
    }
  }
}

std::string contentSha256(const json& value) {
  return oral_training::sha256Hex(value.dump());
}

json servicePublicProjection(const json& payload) {
  validateServiceDraft(payload);
  json result = payload;
  result.erase("adminNotes");
  result.erase("internalTags");
  const auto& price = result["price"];
  if (price["status"] == "unknown") {
    result["priceDisplay"] = "暂无报价资料";
    return result;
  }
  const auto yuan = [](long long minor) {
    std::ostringstream output;
    output << minor / 100;
    if (minor % 100 != 0) output << '.' << std::setw(2) << std::setfill('0') << minor % 100;
    return output.str();
  };
  const auto type = price["type"].get<std::string>();
  std::string amount;
  if (type == "fixed") amount = yuan(price["amountMinor"].get<long long>()) + " 元";
  else if (type == "starting_from") amount = yuan(price["amountMinor"].get<long long>()) + " 元起";
  else if (type == "range") amount = yuan(price["minimumMinor"].get<long long>()) + "—" +
      yuan(price["maximumMinor"].get<long long>()) + " 元";
  else amount = "需评估后报价";
  const std::map<std::string, std::string> unit_labels = {
      {"per_tooth", "颗"}, {"per_case", "例"}, {"per_visit", "次"},
      {"per_arch", "牙弓"}, {"per_item", "项"},
  };
  result["priceDisplay"] = amount + "/" + unit_labels.at(price["unit"].get<std::string>()) +
      "；" + price["conditions"].get<std::string>();
  return result;
}

json KnowledgeStore::listAvailableServices() const {
  auto connection = database_pool_->acquire();
  pqxx::read_transaction tx(connection.get());
  const auto rows = tx.exec(R"(
    SELECT s.id, r.payload->>'name' AS name, r.payload->>'category' AS category,
      s.current_revision_id, r.version, r.payload,
      COALESCE(jsonb_agg(ss.scenario_id ORDER BY ss.scenario_id)
        FILTER (WHERE ss.scenario_id IS NOT NULL), '[]'::jsonb) AS scenario_ids
    FROM clinic_services s
    JOIN service_revisions r ON r.id = s.current_revision_id
    LEFT JOIN service_scenarios ss ON ss.service_id = s.id
    WHERE s.status = 'active'
        AND COALESCE(NULLIF((r.payload->'price')->>'validFrom', ''), '0001-01-01') <= to_char(statement_timestamp() AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD')
        AND COALESCE(NULLIF((r.payload->'price')->>'validUntil', ''), '9999-12-31') >= to_char(statement_timestamp() AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD')
    GROUP BY s.id, s.name, s.category, s.current_revision_id, r.version, r.payload
    ORDER BY r.payload->>'name', s.id
  )");
  json items = json::array();
  for (const auto& row : rows) {
    const auto payload = servicePublicProjection(parseJsonField(row["payload"]));
    items.push_back({{"id", row["id"].c_str()}, {"name", row["name"].c_str()},
                     {"category", row["category"].c_str()},
                     {"revisionId", row["current_revision_id"].c_str()},
                     {"version", row["version"].as<int>()},
                     {"priceDisplay", payload.value("priceDisplay", "暂无报价资料")},
                     {"scenarioIds", parseJsonField(row["scenario_ids"])} });
  }
  return {{"items", items}};
}

json KnowledgeStore::listServices(const std::string& actor_id) const {
  auto connection = database_pool_->acquire();
  pqxx::read_transaction tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto rows = tx.exec(R"(
    SELECT s.id, COALESCE(d.payload->>'name', s.name) AS name,
      COALESCE(d.payload->>'category', s.category) AS category, s.status, s.current_revision_id,
      d.draft_version, d.updated_at, r.version AS published_version
    FROM clinic_services s
    LEFT JOIN service_drafts d ON d.service_id = s.id
    LEFT JOIN service_revisions r ON r.id = s.current_revision_id
    ORDER BY s.updated_at DESC, s.id
  )");
  json items = json::array();
  for (const auto& row : rows) {
    items.push_back({{"id", row["id"].c_str()}, {"name", row["name"].c_str()},
                     {"category", row["category"].c_str()}, {"status", row["status"].c_str()},
                     {"currentRevisionId", row["current_revision_id"].is_null()
                         ? json(nullptr) : json(row["current_revision_id"].c_str())},
                     {"publishedVersion", row["published_version"].is_null()
                         ? json(nullptr) : json(row["published_version"].as<int>())},
                     {"draftVersion", row["draft_version"].is_null()
                         ? json(nullptr) : json(row["draft_version"].as<int>())},
                     {"draftUpdatedAt", row["updated_at"].is_null()
                         ? json(nullptr) : json(row["updated_at"].c_str())}});
  }
  return {{"items", items}};
}

json KnowledgeStore::createService(const std::string& actor_id, const json& payload,
                                   const std::string& request_id) const {
  validateServiceDraft(payload);
  const auto service_id = randomId("svc");
  const auto draft_id = randomId("service-draft");
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  requireAdmin(tx, actor_id);
  tx.exec_params(R"(
    INSERT INTO clinic_services(id, name, category, created_by)
    VALUES ($1, $2, $3, $4)
  )", service_id, payload["name"].get<std::string>(), payload["category"].get<std::string>(), actor_id);
  tx.exec_params(R"(
    INSERT INTO service_drafts(id, service_id, payload, updated_by)
    VALUES ($1, $2, $3::jsonb, $4)
  )", draft_id, service_id, payload.dump(), actor_id);
  writeAudit(tx, actor_id, "service_created", "service", service_id,
             std::nullopt, std::nullopt, request_id);
  tx.commit();
  return {{"id", service_id}, {"draftId", draft_id}, {"draftVersion", 1},
          {"payload", payload}};
}

json KnowledgeStore::getServiceDraft(const std::string& actor_id,
                                     const std::string& service_id) const {
  auto connection = database_pool_->acquire();
  pqxx::read_transaction tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto rows = tx.exec_params(R"(
    SELECT s.id, s.status, s.current_revision_id, d.id AS draft_id, d.payload,
      d.draft_version, d.generation_id, d.updated_at
    FROM clinic_services s JOIN service_drafts d ON d.service_id = s.id WHERE s.id = $1
  )", service_id);
  if (rows.empty()) throw KnowledgeStoreError(404, "SERVICE_NOT_FOUND", "服务草稿不存在");
  const auto& row = rows[0];
  return {{"id", row["id"].c_str()}, {"status", row["status"].c_str()},
          {"currentRevisionId", row["current_revision_id"].is_null()
              ? json(nullptr) : json(row["current_revision_id"].c_str())},
          {"draftId", row["draft_id"].c_str()}, {"payload", parseJsonField(row["payload"])},
          {"draftVersion", row["draft_version"].as<int>()},
          {"generationId", row["generation_id"].is_null()
              ? json(nullptr) : json(row["generation_id"].c_str())},
          {"updatedAt", row["updated_at"].c_str()}};
}

json KnowledgeStore::saveServiceDraft(const std::string& actor_id,
                                      const std::string& service_id, int draft_version,
                                      const json& payload, const std::string& request_id) const {
  validateServiceDraft(payload);
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto rows = tx.exec_params(R"(
    UPDATE service_drafts SET payload = $4::jsonb, draft_version = draft_version + 1,
      updated_by = $2, generation_id = NULL, updated_at = NOW()
    WHERE service_id = $1 AND draft_version = $3
    RETURNING id, draft_version, updated_at
  )", service_id, actor_id, draft_version, payload.dump());
  if (rows.empty()) {
    const auto exists = tx.exec_params("SELECT draft_version FROM service_drafts WHERE service_id = $1",
                                       service_id);
    if (exists.empty()) throw KnowledgeStoreError(404, "SERVICE_NOT_FOUND", "服务草稿不存在");
    throw KnowledgeStoreError(409, "DRAFT_VERSION_CONFLICT", "服务草稿已被其他编辑更新");
  }
  tx.exec_params("UPDATE clinic_services SET updated_at = NOW() WHERE id = $1", service_id);
  writeAudit(tx, actor_id, "service_draft_saved", "service", service_id,
             std::nullopt, std::nullopt, request_id);
  tx.commit();
  return {{"id", service_id}, {"draftId", rows[0]["id"].c_str()},
          {"draftVersion", rows[0]["draft_version"].as<int>()}, {"payload", payload},
          {"updatedAt", rows[0]["updated_at"].c_str()}};
}

json KnowledgeStore::publishService(const std::string& actor_id,
                                    const std::string& service_id, int draft_version,
                                    const std::string& idempotency_key,
                                    const std::string& request_digest,
                                    const std::string& request_id) const {
  if (idempotency_key.empty() || idempotency_key.size() > 200 || request_digest.size() != 64) {
    invalid("发布幂等参数无效");
  }
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto replay = tx.exec_params(R"(
    SELECT entity_type, entity_id, request_digest, result_revision_id
    FROM knowledge_publish_requests WHERE actor_id = $1 AND idempotency_key = $2
  )", actor_id, idempotency_key);
  if (!replay.empty()) {
    if (std::string(replay[0]["entity_type"].c_str()) != "service" ||
        std::string(replay[0]["entity_id"].c_str()) != service_id ||
        std::string(replay[0]["request_digest"].c_str()) != request_digest) {
      throw KnowledgeStoreError(409, "IDEMPOTENCY_CONFLICT", "幂等键对应不同发布请求");
    }
    const auto revision = tx.exec_params(R"(
      SELECT id, version, payload, content_hash, origin, published_by, published_at
      FROM service_revisions WHERE id = $1
    )", replay[0]["result_revision_id"].c_str());
    return {{"serviceId", service_id}, {"replayed", true},
            {"revision", serviceRevisionJson(revision[0])}};
  }
  const auto rows = tx.exec_params(R"(
    SELECT s.current_revision_id, d.payload, d.draft_version
    FROM clinic_services s JOIN service_drafts d ON d.service_id = s.id
    WHERE s.id = $1 FOR UPDATE OF s, d
  )", service_id);
  if (rows.empty()) throw KnowledgeStoreError(404, "SERVICE_NOT_FOUND", "服务草稿不存在");
  if (rows[0]["draft_version"].as<int>() != draft_version) {
    throw KnowledgeStoreError(409, "DRAFT_VERSION_CONFLICT", "发布的草稿版本已经过期");
  }
  const auto payload = parseJsonField(rows[0]["payload"]);
  validateServiceDraft(payload);
  const auto next_version = tx.exec_params(
      "SELECT COALESCE(MAX(version), 0) + 1 AS version FROM service_revisions WHERE service_id = $1",
      service_id)[0]["version"].as<int>();
  const auto revision_id = randomId("srv-rev");
  const auto content_hash = contentSha256(payload);
  tx.exec_params(R"(
    INSERT INTO service_revisions
      (id, service_id, version, payload, content_hash, origin, published_by)
    VALUES ($1, $2, $3, $4::jsonb, $5, $6, $7)
  )", revision_id, service_id, next_version, payload.dump(), content_hash,
      payload["dataOrigin"].get<std::string>(), actor_id);
  tx.exec_params("UPDATE clinic_services SET current_revision_id = $2, name = $3, category = $4, "
                 "status = 'active', updated_at = NOW() WHERE id = $1",
                 service_id, revision_id, payload["name"].get<std::string>(), payload["category"].get<std::string>());
  tx.exec_params("DELETE FROM service_scenarios WHERE service_id = $1", service_id);
  /* 显式取字符串再绑参：service_scenarios 两列都是 NOT NULL，元素一旦以
     SQL NULL 绑定，报的是 `null value in column "service_id"` —— 看不出是
     输入的第几个场景 id 有问题。validateServiceDraft 已拦过一层，
     这里按「写入前再校验」的惯例兜底，保证报错停在可读的中文上。 */
  for (const auto& scenario_id : payload.value("scenarioIds", json::array())) {
    if (!scenario_id.is_string() || scenario_id.get<std::string>().empty()) {
      invalid("scenarioIds 每一项都必须是非空场景 id");
    }
    tx.exec_params("INSERT INTO service_scenarios(service_id, scenario_id) VALUES ($1, $2)",
                   service_id, scenario_id.get<std::string>());
  }
  tx.exec_params(R"(
    INSERT INTO knowledge_publish_requests
      (actor_id, idempotency_key, entity_type, entity_id, request_digest, result_revision_id)
    VALUES ($1, $2, 'service', $3, $4, $5)
  )", actor_id, idempotency_key, service_id, request_digest, revision_id);
  const std::optional<std::string> old_revision = rows[0]["current_revision_id"].is_null()
      ? std::nullopt : std::optional<std::string>(rows[0]["current_revision_id"].c_str());
  writeAudit(tx, actor_id, "service_published", "service", service_id,
             old_revision, revision_id, request_id);
  tx.commit();
  return {{"serviceId", service_id}, {"replayed", false},
          {"revision", {{"revisionId", revision_id}, {"version", next_version},
                         {"payload", payload}, {"contentHash", content_hash},
                         {"origin", payload["dataOrigin"]}}}};
}

json KnowledgeStore::archiveService(const std::string& actor_id,
                                    const std::string& service_id,
                                    const std::string& request_id) const {
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto rows = tx.exec_params(R"(
    UPDATE clinic_services SET status = 'archived', updated_at = NOW()
    WHERE id = $1 RETURNING current_revision_id
  )", service_id);
  if (rows.empty()) throw KnowledgeStoreError(404, "SERVICE_NOT_FOUND", "服务不存在");
  const std::optional<std::string> revision = rows[0]["current_revision_id"].is_null()
      ? std::nullopt : std::optional<std::string>(rows[0]["current_revision_id"].c_str());
  writeAudit(tx, actor_id, "service_archived", "service", service_id,
             revision, revision, request_id);
  tx.commit();
  return {{"id", service_id}, {"status", "archived"}};
}

json KnowledgeStore::serviceRevisions(const std::string& actor_id,
                                      const std::string& service_id) const {
  auto connection = database_pool_->acquire();
  pqxx::read_transaction tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto exists = tx.exec_params("SELECT 1 FROM clinic_services WHERE id = $1", service_id);
  if (exists.empty()) throw KnowledgeStoreError(404, "SERVICE_NOT_FOUND", "服务不存在");
  const auto rows = tx.exec_params(R"(
    SELECT id, version, payload, content_hash, origin, published_by, published_at
    FROM service_revisions WHERE service_id = $1 ORDER BY version DESC
  )", service_id);
  json items = json::array();
  for (const auto& row : rows) items.push_back(serviceRevisionJson(row));
  return {{"serviceId", service_id}, {"items", items}};
}

json KnowledgeStore::listKnowledge(const std::string& actor_id) const {
  auto connection = database_pool_->acquire();
  pqxx::read_transaction tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto rows = tx.exec(R"(
    SELECT e.id, e.topic, e.scope, e.service_id, e.status, e.current_revision_id,
      d.title, d.draft_version, d.updated_at, r.version AS published_version
    FROM knowledge_entries e
    LEFT JOIN knowledge_drafts d ON d.entry_id = e.id
    LEFT JOIN knowledge_revisions r ON r.id = e.current_revision_id
    ORDER BY e.updated_at DESC, e.id
  )");
  json items = json::array();
  for (const auto& row : rows) {
    items.push_back({{"id", row["id"].c_str()}, {"topic", row["topic"].c_str()},
                     {"scope", row["scope"].c_str()}, {"serviceId", row["service_id"].is_null()
                         ? json(nullptr) : json(row["service_id"].c_str())},
                     {"title", row["title"].is_null() ? "" : row["title"].c_str()},
                     {"status", row["status"].c_str()},
                     {"currentRevisionId", row["current_revision_id"].is_null()
                         ? json(nullptr) : json(row["current_revision_id"].c_str())},
                     {"publishedVersion", row["published_version"].is_null()
                         ? json(nullptr) : json(row["published_version"].as<int>())},
                     {"draftVersion", row["draft_version"].is_null()
                         ? json(nullptr) : json(row["draft_version"].as<int>())},
                     {"draftUpdatedAt", row["updated_at"].is_null()
                         ? json(nullptr) : json(row["updated_at"].c_str())}});
  }
  return {{"items", items}};
}

json KnowledgeStore::createKnowledge(const std::string& actor_id, const std::string& topic,
                                     const std::string& scope, const std::string& service_id,
                                     const std::string& title, const std::string& body,
                                     const json& metadata, const std::string& request_id) const {
  if (topic.empty() || topic.size() > 120) invalid("topic 长度无效");
  requireEnum(scope, {"general", "service"}, "scope");
  if ((scope == "general" && !service_id.empty()) || (scope == "service" && service_id.empty())) {
    invalid("scope 与 serviceId 不一致");
  }
  validateKnowledgeDraft(title, body, metadata);
  const auto entry_id = randomId("knowledge");
  const auto draft_id = randomId("knowledge-draft");
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  requireAdmin(tx, actor_id);
  tx.exec_params(R"(
    INSERT INTO knowledge_entries(id, topic, scope, service_id, created_by)
    VALUES ($1, $2, $3, NULLIF($4, ''), $5)
  )", entry_id, topic, scope, service_id, actor_id);
  tx.exec_params(R"(
    INSERT INTO knowledge_drafts(id, entry_id, title, body, metadata, updated_by)
    VALUES ($1, $2, $3, $4, $5::jsonb, $6)
  )", draft_id, entry_id, title, body, metadata.dump(), actor_id);
  writeAudit(tx, actor_id, "knowledge_created", "knowledge", entry_id,
             std::nullopt, std::nullopt, request_id);
  tx.commit();
  return {{"id", entry_id}, {"draftId", draft_id}, {"draftVersion", 1},
          {"topic", topic}, {"scope", scope}, {"serviceId", service_id.empty()
              ? json(nullptr) : json(service_id)}, {"title", title}, {"body", body},
          {"metadata", metadata}};
}

json KnowledgeStore::getKnowledgeDraft(const std::string& actor_id,
                                       const std::string& entry_id) const {
  auto connection = database_pool_->acquire();
  pqxx::read_transaction tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto rows = tx.exec_params(R"(
    SELECT e.id, e.topic, e.scope, e.service_id, e.status, e.current_revision_id,
      d.id AS draft_id, d.title, d.body, d.metadata, d.draft_version, d.generation_id, d.updated_at
    FROM knowledge_entries e JOIN knowledge_drafts d ON d.entry_id = e.id WHERE e.id = $1
  )", entry_id);
  if (rows.empty()) throw KnowledgeStoreError(404, "KNOWLEDGE_NOT_FOUND", "知识草稿不存在");
  const auto& row = rows[0];
  return {{"id", row["id"].c_str()}, {"topic", row["topic"].c_str()},
          {"scope", row["scope"].c_str()}, {"serviceId", row["service_id"].is_null()
              ? json(nullptr) : json(row["service_id"].c_str())}, {"status", row["status"].c_str()},
          {"currentRevisionId", row["current_revision_id"].is_null()
              ? json(nullptr) : json(row["current_revision_id"].c_str())},
          {"draftId", row["draft_id"].c_str()}, {"title", row["title"].c_str()},
          {"body", row["body"].c_str()}, {"metadata", parseJsonField(row["metadata"])},
          {"draftVersion", row["draft_version"].as<int>()},
          {"generationId", row["generation_id"].is_null()
              ? json(nullptr) : json(row["generation_id"].c_str())},
          {"updatedAt", row["updated_at"].c_str()}};
}

json KnowledgeStore::saveKnowledgeDraft(const std::string& actor_id,
                                        const std::string& entry_id, int draft_version,
                                        const std::string& title, const std::string& body,
                                        const json& metadata, const std::string& request_id) const {
  validateKnowledgeDraft(title, body, metadata);
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto rows = tx.exec_params(R"(
    UPDATE knowledge_drafts SET title = $4, body = $5, metadata = $6::jsonb,
      draft_version = draft_version + 1, updated_by = $2, generation_id = NULL, updated_at = NOW()
    WHERE entry_id = $1 AND draft_version = $3
    RETURNING id, draft_version, updated_at
  )", entry_id, actor_id, draft_version, title, body, metadata.dump());
  if (rows.empty()) {
    const auto exists = tx.exec_params("SELECT draft_version FROM knowledge_drafts WHERE entry_id = $1",
                                       entry_id);
    if (exists.empty()) throw KnowledgeStoreError(404, "KNOWLEDGE_NOT_FOUND", "知识草稿不存在");
    throw KnowledgeStoreError(409, "DRAFT_VERSION_CONFLICT", "知识草稿已被其他编辑更新");
  }
  writeAudit(tx, actor_id, "knowledge_draft_saved", "knowledge", entry_id,
             std::nullopt, std::nullopt, request_id);
  tx.commit();
  return {{"id", entry_id}, {"draftId", rows[0]["id"].c_str()},
          {"draftVersion", rows[0]["draft_version"].as<int>()}, {"title", title},
          {"body", body}, {"metadata", metadata}, {"updatedAt", rows[0]["updated_at"].c_str()}};
}

json KnowledgeStore::publishKnowledge(const std::string& actor_id,
                                      const std::string& entry_id, int draft_version,
                                      const std::string& idempotency_key,
                                      const std::string& request_digest,
                                      const std::string& request_id) const {
  if (idempotency_key.empty() || idempotency_key.size() > 200 || request_digest.size() != 64) {
    invalid("发布幂等参数无效");
  }
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto replay = tx.exec_params(R"(
    SELECT entity_type, entity_id, request_digest, result_revision_id
    FROM knowledge_publish_requests WHERE actor_id = $1 AND idempotency_key = $2
  )", actor_id, idempotency_key);
  if (!replay.empty()) {
    if (std::string(replay[0]["entity_type"].c_str()) != "knowledge" ||
        std::string(replay[0]["entity_id"].c_str()) != entry_id ||
        std::string(replay[0]["request_digest"].c_str()) != request_digest) {
      throw KnowledgeStoreError(409, "IDEMPOTENCY_CONFLICT", "幂等键对应不同发布请求");
    }
    const auto revision = tx.exec_params(R"(
      SELECT id, version, title, body, metadata, content_hash, published_by, published_at
      FROM knowledge_revisions WHERE id = $1
    )", replay[0]["result_revision_id"].c_str());
    return {{"entryId", entry_id}, {"replayed", true},
            {"revision", knowledgeRevisionJson(revision[0])}};
  }
  const auto rows = tx.exec_params(R"(
    SELECT e.current_revision_id, d.title, d.body, d.metadata, d.draft_version
    FROM knowledge_entries e JOIN knowledge_drafts d ON d.entry_id = e.id
    WHERE e.id = $1 FOR UPDATE OF e, d
  )", entry_id);
  if (rows.empty()) throw KnowledgeStoreError(404, "KNOWLEDGE_NOT_FOUND", "知识草稿不存在");
  if (rows[0]["draft_version"].as<int>() != draft_version) {
    throw KnowledgeStoreError(409, "DRAFT_VERSION_CONFLICT", "发布的草稿版本已经过期");
  }
  const auto title = std::string(rows[0]["title"].c_str());
  const auto body = std::string(rows[0]["body"].c_str());
  const auto metadata = parseJsonField(rows[0]["metadata"]);
  validateKnowledgeDraft(title, body, metadata);
  const auto next_version = tx.exec_params(
      "SELECT COALESCE(MAX(version), 0) + 1 AS version FROM knowledge_revisions WHERE entry_id = $1",
      entry_id)[0]["version"].as<int>();
  const auto revision_id = randomId("kn-rev");
  const auto content_hash = contentSha256({{"title", title}, {"body", body}, {"metadata", metadata}});
  tx.exec_params(R"(
    INSERT INTO knowledge_revisions
      (id, entry_id, version, title, body, metadata, content_hash, published_by)
    VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7, $8)
  )", revision_id, entry_id, next_version, title, body, metadata.dump(), content_hash, actor_id);
  oral_training::rag::insertKnowledgeChunks(tx, revision_id, title, body, metadata);
  tx.exec_params("UPDATE knowledge_entries SET current_revision_id = $2, status = 'active', updated_at = NOW() WHERE id = $1",
                 entry_id, revision_id);
  tx.exec_params(R"(
    INSERT INTO knowledge_publish_requests
      (actor_id, idempotency_key, entity_type, entity_id, request_digest, result_revision_id)
    VALUES ($1, $2, 'knowledge', $3, $4, $5)
  )", actor_id, idempotency_key, entry_id, request_digest, revision_id);
  const std::optional<std::string> old_revision = rows[0]["current_revision_id"].is_null()
      ? std::nullopt : std::optional<std::string>(rows[0]["current_revision_id"].c_str());
  writeAudit(tx, actor_id, "knowledge_published", "knowledge", entry_id,
             old_revision, revision_id, request_id);
  tx.commit();
  return {{"entryId", entry_id}, {"replayed", false},
          {"revision", {{"revisionId", revision_id}, {"version", next_version},
                         {"title", title}, {"body", body}, {"metadata", metadata},
                         {"contentHash", content_hash}}}};
}

json KnowledgeStore::archiveKnowledge(const std::string& actor_id,
                                      const std::string& entry_id,
                                      const std::string& request_id) const {
  auto connection = database_pool_->acquire();
  pqxx::work tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto rows = tx.exec_params(R"(
    UPDATE knowledge_entries SET status = 'archived', updated_at = NOW()
    WHERE id = $1 RETURNING current_revision_id
  )", entry_id);
  if (rows.empty()) throw KnowledgeStoreError(404, "KNOWLEDGE_NOT_FOUND", "知识条目不存在");
  const std::optional<std::string> revision = rows[0]["current_revision_id"].is_null()
      ? std::nullopt : std::optional<std::string>(rows[0]["current_revision_id"].c_str());
  writeAudit(tx, actor_id, "knowledge_archived", "knowledge", entry_id,
             revision, revision, request_id);
  tx.commit();
  return {{"id", entry_id}, {"status", "archived"}};
}

json KnowledgeStore::knowledgeRevisions(const std::string& actor_id,
                                        const std::string& entry_id) const {
  auto connection = database_pool_->acquire();
  pqxx::read_transaction tx(connection.get());
  requireAdmin(tx, actor_id);
  const auto exists = tx.exec_params("SELECT 1 FROM knowledge_entries WHERE id = $1", entry_id);
  if (exists.empty()) throw KnowledgeStoreError(404, "KNOWLEDGE_NOT_FOUND", "知识条目不存在");
  const auto rows = tx.exec_params(R"(
    SELECT id, version, title, body, metadata, content_hash, published_by, published_at
    FROM knowledge_revisions WHERE entry_id = $1 ORDER BY version DESC
  )", entry_id);
  json items = json::array();
  for (const auto& row : rows) items.push_back(knowledgeRevisionJson(row));
  return {{"entryId", entry_id}, {"items", items}};
}

}  // namespace oral_training::knowledge
