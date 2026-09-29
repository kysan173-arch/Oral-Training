// variant_test.cpp — 同场景变体池（P1-2）的纯函数行为验证。
// applyVariantsToHidden 是 session_id 的确定性纯函数：这里不依赖数据库与模型，
// 只锁住「同 seed 同结果、不同 seed 分流、变体覆盖 hidden/instructions、其余字段保留」。
#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool cond, const char* name) {
  if (!cond) {
    std::cerr << "FAIL: " << name << "\n";
    ++failures;
  }
}
}  // namespace

int main() {
  using json = nlohmann::json;
  const json plain = {
      {"opening", "标准档开场白原句"},
      {"hidden", json::array({"主顾虑A", "主顾虑B"})},
      {"instructions", "主披露节奏原句"},
      {"initialState", {{"emotion", "愤怒"}, {"emotionLevel", -2}, {"trustLevel", 25}}}};

  // 1) 无 variants：原样返回（与迁移前行为完全一致）
  check(ReliableDatabase::applyVariantsToHidden(plain, "sess-1") == plain,
        "no-variants returns unchanged");

  // 2) 非 object / null 输入：返回空 object，不崩
  check(ReliableDatabase::applyVariantsToHidden(json(nullptr), "s") == json::object(),
        "null hidden -> empty object");

  // 3) 空 variants 数组：原样返回
  json empty_pool = plain;
  empty_pool["variants"] = json::array();
  check(ReliableDatabase::applyVariantsToHidden(empty_pool, "s") == plain,
        "empty variants array returns unchanged");

  // 4) 单变体：恒选它（覆盖 hidden + instructions，其余字段保留）
  json single = plain;
  single["variants"] = json::array({json{{"hidden", json::array({"单一顾虑"})},
                                         {"instructions", "单一节奏"}}});
  auto r_single = ReliableDatabase::applyVariantsToHidden(single, "sess-a");
  check(r_single["hidden"] == json::array({"单一顾虑"}), "single variant hidden applied");
  check(r_single["instructions"] == "单一节奏", "single variant instructions applied");
  check(r_single["opening"] == "标准档开场白原句", "single variant keeps opening");
  check(r_single["initialState"]["trustLevel"] == 25, "single variant keeps initialState");

  // 5) 两组变体：同 seed 确定性
  json pool = plain;
  pool["variants"] = json::array({
      json{{"hidden", json::array({"顾虑组0a", "顾虑组0b"})}, {"instructions", "节奏0"}},
      json{{"hidden", json::array({"顾虑组1"})}, {"instructions", "节奏1"}}});
  auto r1 = ReliableDatabase::applyVariantsToHidden(pool, "sess-fixed");
  auto r2 = ReliableDatabase::applyVariantsToHidden(pool, "sess-fixed");
  check(r1 == r2, "same seed -> same variant");
  // 返回值绝不能泄露 variants 元数据——否则患者提示词能看到「其他变体的顾虑」（等于看答案）
  check(!r1.contains("variants"), "variant metadata stripped from result");

  // 6) 不同 seed 分流：50 个 seed 必须命中两个变体
  bool saw0 = false, saw1 = false;
  for (int i = 0; i < 50; ++i) {
    auto r = ReliableDatabase::applyVariantsToHidden(pool, "sess-" + std::to_string(i));
    if (r["hidden"] == json::array({"顾虑组0a", "顾虑组0b"})) saw0 = true;
    if (r["hidden"] == json::array({"顾虑组1"})) saw1 = true;
  }
  check(saw0 && saw1, "different seeds reach both variants");

  // 7) 部分覆盖：变体只给 hidden、不给 instructions → instructions 沿用主值
  json partial = plain;
  partial["variants"] = json::array({json{{"hidden", json::array({"部分顾虑"})}}});
  auto r_partial = ReliableDatabase::applyVariantsToHidden(partial, "sess-p");
  check(r_partial["hidden"] == json::array({"部分顾虑"}), "partial variant hidden applied");
  check(r_partial["instructions"] == "主披露节奏原句", "partial variant keeps main instructions");

  // ── 结构校验（normalizedVariants）：脏数据挡在外面 ──
  check(ReliableDatabase::normalizedVariants(json::object()).empty(), "non-array variants -> empty");
  const auto cleaned = ReliableDatabase::normalizedVariants(json::array({
      json{{"hidden", json::array({"有效顾虑"})}, {"instructions", "有效的披露节奏原句"}},
      json{{"hidden", json::array()} },                                   // 空 hidden → 丢弃
      json{{"hidden", json::array({"另一组"})}, {"instructions", "太短"}}}  // instructions 过短 → 该键丢弃，组保留
  ));
  check(cleaned.size() == 2, "invalid variant group dropped");
  check(cleaned[1].contains("hidden") && !cleaned[1].contains("instructions"),
        "too-short instructions dropped but group kept");

  // ── 最关键：白名单重建必须保留 variants ──
  // 否则主管编辑任何其他字段（哪怕只改简介）都会静默清空变体池。
  const json full_payload = {
      {"id", "variant-guard-scenario"},
      {"name", "变体保留测试场景"},
      {"category", "consultation"},
      {"summary", "用于验证编辑场景不会清空变体池。"},
      {"difficulty", "basic"},
      {"focus", json::array({"需求挖掘"})},
      {"patientProfile", {{"age", 35}, {"gender", "unknown"}, {"description", "测试用患者描述文案"}}},
      {"hiddenConfig",
       {{"opening", "我这个问题你们到底能不能给个准话？"},
        {"hidden", json::array({"顾虑一", "顾虑二", "顾虑三"})},
        {"initialState", {{"emotion", "不满"}, {"emotionLevel", -1}, {"trustLevel", 40}}},
        {"instructions", "若客服答非所问只有一两个词，你应继续追问；只有客服说清处理路径，你才愿意配合。"},
        {"variants",
         json::array({json{{"hidden", json::array({"变体顾虑A1", "变体顾虑A2"})},
                           {"instructions", "若客服推诿，你应更强硬；只有明确受理人，你才配合。"}}})}}},
      {"roleplayConfig", {{"suggestedQuestions", json::array()}, {"serviceGuidance", json::array()}}},
      {"maxRounds", 10},
      {"sortOrder", 500}};
  try {
    const auto validated = ReliableDatabase::validateScenarioPayload(full_payload);
    const auto& kept = validated["hiddenConfig"];
    check(kept.contains("variants") && kept["variants"].size() == 1,
          "validateScenarioPayload keeps variants (edit must not wipe them)");
    check(kept["variants"][0]["hidden"].size() == 2, "kept variant keeps its hidden entries");
  } catch (const ApiError& error) {
    std::cerr << "FAIL: validateScenarioPayload threw unexpectedly: " << error.what() << "\n";
    ++failures;
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "variant_test: all assertions passed\n";
  return 0;
}
