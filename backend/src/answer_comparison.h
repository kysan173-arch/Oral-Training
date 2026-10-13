#pragma once
#include "answer_rubric.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

/* 「参考答案对比评分」的确定性结算模块。
 *
 * 为什么模型不给分：
 *   同一份对话跑两次，模型自报的分数会变（它是概率生成的），学员质疑
 *   「为什么 78 分」时无从回答；而且模型的措辞与分数之间没有可审计的路径。
 *   因此模型只被允许输出「引用 + 编号 + 等级 + 差异类型」这类可证伪的东西，
 *   分值与聚合一律由本模块按 answer_rubric.h 的固定公式算出。
 *   与 knowledge_evaluator.h 的既定原则一致：
 *     Model candidates only identify an existing source span and a field.
 *     Scores, verdicts, numeric values and offsets supplied by the model are ignored.
 *
 * 为什么引用必须逐字校验：
 *   模型可以"记得"学员说过某句话而实际上并没有说。凡是引用不能在对应
 *   原文里逐字定位的条目，一律按未覆盖处理并留痕（不是静默丢弃——
 *   判定质量本身是结题报告要统计的指标）。
 *
 * 为什么数值类必须服务器重判：
 *   模型对数字不稳（「一万二」与「一万多」都可能被判成一致）。涉及价格、
 *   时长、次数、周期的评分项，等级由服务端从两段引用中抽数值与单位比对，
 *   不采信模型给出的等级。
 *
 * 对齐锚点的含义：
 *   第 r 轮的判定只比对「该轮学员原话」与「该轮标准答案」。标准答案本身
 *   是按患者第 r 轮的实际提问、用本轮之前的真实对话生成的，因此两者
 *   面对的是同一个问题——这是对比成立的前提。
 */
namespace oral_training::compare {

using json = nlohmann::json;

inline constexpr std::string_view kAnswerComparisonBlockKey = "answerComparison";

// ── 安全读取 ────────────────────────────────────────────────────────────
inline std::string jsonText(const json& value, const char* key, std::string fallback = "") {
  return value.is_object() && value.contains(key) && value[key].is_string()
      ? value[key].get<std::string>() : std::move(fallback);
}

inline int jsonInt(const json& value, const char* key, int fallback = 0) {
  return value.is_object() && value.contains(key) && value[key].is_number()
      ? value[key].get<int>() : fallback;
}

inline double jsonReal(const json& value, const char* key, double fallback = 0.0) {
  return value.is_object() && value.contains(key) && value[key].is_number()
      ? value[key].get<double>() : fallback;
}

inline bool jsonFlag(const json& value, const char* key, bool fallback = false) {
  return value.is_object() && value.contains(key) && value[key].is_boolean()
      ? value[key].get<bool>() : fallback;
}

// ── 文本与引用的基础工具 ────────────────────────────────────────────────
inline bool textContains(const std::string& haystack, const std::string& needle) {
  return !needle.empty() && haystack.find(needle) != std::string::npos;
}

/* 从「不适用理由」里提取判定者引用的患者原话。
 * 约定：理由中须用「」或 "" 括起患者原话。取不到即视为无依据。 */
inline std::string extractQuotedPatientSpeech(const std::string& reason) {
  static const std::vector<std::pair<std::string, std::string>> marks = {
      {"「", "」"}, {"“", "”"}, {"\"", "\""}};
  for (const auto& [open, close] : marks) {
    const auto begin = reason.find(open);
    if (begin == std::string::npos) continue;
    const auto end = reason.find(close, begin + open.size());
    if (end == std::string::npos || end <= begin + open.size()) continue;
    return reason.substr(begin + open.size(), end - begin - open.size());
  }
  return "";
}

// ── 数值抽取与一致性（服务器重判）──────────────────────────────────────
struct Measurement {
  double value = 0;
  std::string unit;
};

/* 抽取带单位的数值。区间（3800—8800 元）会展开成两个同单位度量。
 * 中文数字刻意不解析：与 knowledge_evaluator.h 的既有口径一致，
 * 宁可判为「无法核验」，也不猜测语义。 */
inline std::vector<Measurement> extractMeasurements(const std::string& text) {
  static const std::regex pattern(
      "([0-9]+(?:\\.[0-9]{1,2})?)(?:\\s*(?:—|-|至|到|~|～)\\s*([0-9]+(?:\\.[0-9]{1,2})?))?\\s*"
      "(分钟|小时|个月|周|天|年|元|万|颗|例|次|牙弓|项|%)");
  std::vector<Measurement> out;
  for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
    if ((*it)[1].length() > 12) return {};
    double factor = 1;
    std::string unit = (*it)[3];
    if (unit == "万") { factor = 10000; unit = "元"; }
    double first = std::stod((*it)[1]) * factor;
    out.push_back({first, unit});
    if ((*it)[2].matched) {
      if ((*it)[2].length() > 12) return {};
      out.push_back({std::stod((*it)[2]) * factor, unit});
    }
  }
  return out;
}

/* 学员数值是否被标准答案支持，返回三态：
 *   1  一致（学员值落在标准答案的同单位区间内）
 *   0  不一致（学员给了数值但超出区间，或单位对不上）
 *  -1  无从比对——不覆盖模型等级
 *
 * 两种 -1 各有理由：
 *   基准侧无数值      → 标准答案本来就没说数字，无从比对，不该判学员错；
 *   学员侧无数值      → 服务器只负责「数字对不对」，「有没有给数字」属于遗漏，
 *                      交给模型的 differenceType 判断，不该由数值模块归零。
 *                      注意中文数字（「一万二」）刻意不解析，因此也会落到这一支——
 *                      这与 knowledge_evaluator.h 的既有口径一致：宁可判为无法核验，
 *                      也不猜测语义。
 * 单位不一致按不一致处理：宁可保守，也不猜测换算。 */
inline int numericAgreement(const std::string& user_quote, const std::string& reference_quote) {
  const auto reference = extractMeasurements(reference_quote);
  if (reference.empty()) return -1;
  const auto user = extractMeasurements(user_quote);
  if (user.empty()) return -1;
  std::map<std::string, std::pair<double, double>> spans;
  for (const auto& item : reference) {
    auto it = spans.find(item.unit);
    if (it == spans.end()) spans[item.unit] = {item.value, item.value};
    else {
      it->second.first = std::min(it->second.first, item.value);
      it->second.second = std::max(it->second.second, item.value);
    }
  }
  for (const auto& item : user) {
    const auto it = spans.find(item.unit);
    if (it == spans.end()) return 0;
    if (item.value < it->second.first - 0.0001 || item.value > it->second.second + 0.0001) return 0;
  }
  return 1;
}

// ── 等级自洽钳制 ────────────────────────────────────────────────────────
/* 差异类型与等级必须自洽，否则模型可以「说等效、给 0 分」。
 * 不自洽时钳制到合法档并标记，供审计——而不是整单失败。 */
inline int clampLevelForDifference(const std::string& difference_type, int level, bool* adjusted) {
  if (adjusted) *adjusted = false;
  const int bounded = std::max(0, std::min(4, level));
  const auto fix = [&](int value) {
    if (adjusted && value != level) *adjusted = true;
    return value;
  };
  if (difference_type == "equivalent") return fix(bounded < 3 ? 3 : bounded);
  if (difference_type == "substantive_omission") return fix(bounded > 2 ? 2 : bounded);
  if (difference_type == "factual_or_boundary_error") return fix(bounded > 1 ? 1 : bounded);
  if (difference_type == "learner_better") return fix(4);
  if (difference_type == "reference_defect") return fix(bounded);
  return fix(bounded);
}

// ── 归一化：校验、重判、钳制、反滥用、去重 ──────────────────────────────
/* candidates 只含模型给出的字段；本函数补齐 weight/scope/source 等服务器字段，
 * 并把不合规的条目按既定规则修正或排除。返回的 checks 是算分与报告的唯一输入。 */
inline json normalizeComparisonChecks(const json& candidates, const json& history, const json& references) {
  std::map<int, std::string> learner_text;  // round -> 该轮学员原话
  std::map<int, std::string> patient_text;  // round -> 该轮患者原话
  if (history.is_array()) {
    for (const auto& message : history) {
      if (!message.is_object()) continue;
      const int round = jsonInt(message, "round", 0);
      const auto role = jsonText(message, "role");
      const auto content = jsonText(message, "content");
      if (role == "user") learner_text[round] += content + "\n";
      else if (role == "patient") patient_text[round] += content + "\n";
    }
  }
  std::map<int, std::string> reference_text;  // round -> 该轮标准答案
  if (references.is_array()) {
    for (const auto& reference : references) {
      if (!reference.is_object()) continue;
      reference_text[jsonInt(reference, "round", 0)] = jsonText(reference, "referenceAnswer");
    }
  }

  json checks = json::array();
  json dropped = json::array();
  std::set<std::string> whole_seen;               // 整场项只计一次
  std::map<std::string, int> error_first_round;   // 事实/边界错误首次扣分轮次

  if (candidates.is_array()) {
    for (const auto& candidate : candidates) {
      if (!candidate.is_object()) continue;
      const auto criterion_id = jsonText(candidate, "criterionId");
      const auto* criterion = findCriterion(criterion_id);
      if (!criterion) {
        // 模型自造的编号一律丢弃：量表是硬契约，不允许临时增加扣分要求。
        dropped.push_back({{"criterionId", criterion_id}, {"reason", "criterion_not_in_rubric"}});
        continue;
      }
      const std::string scope(criterion->scope);
      const std::string dimension(criterion->dimension);

      int round = jsonInt(candidate, "round", 0);
      if (scope == "whole") {
        if (!whole_seen.insert(criterion_id).second) {
          dropped.push_back({{"criterionId", criterion_id}, {"reason", "whole_item_duplicated"}});
          continue;
        }
        round = 0;
      } else if (learner_text.find(round) == learner_text.end()) {
        // 逐轮项必须落在真实存在的客服轮次上。
        dropped.push_back({{"criterionId", criterion_id}, {"reason", "unknown_round"},
                           {"round", jsonInt(candidate, "round", 0)}});
        continue;
      }

      const auto user_quote = jsonText(candidate, "userQuote");
      const auto reference_quote = jsonText(candidate, "referenceQuote");
      const auto difference_type = jsonText(candidate, "differenceType");
      const auto reason = jsonText(candidate, "reason");
      auto applicable = jsonFlag(candidate, "applicable", true);
      auto not_applicable_reason = jsonText(candidate, "notApplicableReason");
      auto level = jsonInt(candidate, "level", 0);
      auto source = std::string("model");
      auto repeat_of_round = 0;
      bool level_adjusted = false;
      bool quote_verified = true;

      /* 引用语料的取法：
       *   userQuote —— 逐轮项只看该轮学员原话：对齐必须严格，否则「学员说过这句」
       *                就失去意义。整场项（round=0）不属于任何一轮，改为在全部
       *                学员原话中查找，否则它永远验不过、永远拿 0 分。
       *   referenceQuote —— 一律在全部标准答案中查找：它只是判定的佐证材料，
       *                不决定分数归到哪一轮，模型跨轮引用不应被判成编造。 */
      std::string learner_corpus, reference_corpus;
      if (scope == "whole") {
        for (const auto& entry : learner_text) learner_corpus += entry.second;
      } else if (learner_text.count(round)) {
        learner_corpus = learner_text.at(round);
      }
      for (const auto& entry : reference_text) reference_corpus += entry.second;

      /* 两处引用的承重不同，核验后果必须分开：
       *   userQuote 是判定成立的前提——「学员说过这句」站不住，整条判定就不可信，
       *             因此归零，且不再参与「差异类型—等级」自洽钳制（否则 equivalent
       *             的保底规则会把一条根本验证不了的判定抬到 3 分）。
       *   referenceQuote 只是佐证材料，抄错它不代表判定本身错了，仅标记留痕供审计。
       * 早期实现把两者塞进同一个标志后统一归零，导致模型引用能力波动时分数被
       * 系统性压低——实测引用核验通过率一度只有 0.60。 */
      const bool user_quote_verified =
          user_quote.empty() || textContains(learner_corpus, user_quote);
      const bool reference_quote_verified =
          reference_quote.empty() || textContains(reference_corpus, reference_quote);
      quote_verified = user_quote_verified;
      if (!user_quote_verified) level = 0;

      // 不适用必须给出指名患者原话的依据，且该原话须能在患者消息中逐字找到
      // （整场项在全部患者消息中查找）。这条同时实现「标准答案缺失某项要求
      // 不能让学员免检」与「不许因学员没做就判不适用」。
      // reference_defect 走独立通道：它排除的是标准答案自身的问题，不要求患者原话依据。
      if (!applicable && difference_type != "reference_defect") {
        const auto cited = extractQuotedPatientSpeech(not_applicable_reason);
        std::string patient_corpus;
        if (scope == "whole") {
          for (const auto& entry : patient_text) patient_corpus += entry.second;
        } else if (patient_text.count(round)) {
          patient_corpus = patient_text.at(round);
        }
        if (cited.empty() || !textContains(patient_corpus, cited)) {
          applicable = true;
          not_applicable_reason.clear();
        }
      }

      /* 数值类不采信模型等级：不一致即覆盖为错误档；无从比对（-1）则保留模型等级，
       * 只把 source 标为 server_numeric 表示该项已过服务器校验。
       * 比对基准必须取「服务端持有的标准答案原文」——它由系统自己生成、不经模型
       * 转手；若沿用模型抄写的 referenceQuote，抄错则基准一起错，「服务器重判」
       * 这条立论就落空了。逐轮项取该轮（缩小范围、避免跨轮数字干扰），整场项取全部拼接。 */
      if (criterion->numeric && applicable) {
        source = "server_numeric";
        const std::string numeric_reference =
            scope == "whole" ? reference_corpus
                             : (reference_text.count(round) ? reference_text.at(round) : reference_corpus);
        if (numericAgreement(user_quote, numeric_reference) == 0) level = 0;
      }

      if (!user_quote_verified) {
        level = 0;
        level_adjusted = true;
      } else {
        level = clampLevelForDifference(difference_type, level, &level_adjusted);
      }

      // 同一问题不重复扣分：事实/边界错误按评分项去重，后续出现仅保留明细。
      if (difference_type == "factual_or_boundary_error") {
        auto it = error_first_round.find(criterion_id);
        if (it == error_first_round.end()) error_first_round[criterion_id] = round;
        else repeat_of_round = it->second;
      }

      checks.push_back({{"criterionId", criterion_id},
                        {"name", std::string(criterion->name)},
                        {"dimension", dimension},
                        {"scope", scope},
                        {"round", round},
                        {"applicable", applicable},
                        {"notApplicableReason", not_applicable_reason},
                        {"level", level},
                        {"levelAdjusted", level_adjusted},
                        {"userQuote", user_quote},
                        {"referenceQuote", reference_quote},
                        {"patientQuote", patient_text.count(round) ? patient_text[round] : std::string()},
                        {"learnerQuote", learner_corpus},
                        {"referenceText", reference_corpus},
                        {"differenceType", difference_type},
                        {"reason", reason},
                        {"quoteVerified", quote_verified},
                        {"referenceQuoteVerified", reference_quote_verified},
                        {"source", source},
                        {"repeatOfRound", repeat_of_round},
                        {"numeric", criterion->numeric},
                        {"importance", criterion->importance},
                        {"applicableHint", std::string(criterion->applicable)}});
    }
  }

  return {{"checks", checks}, {"dropped", dropped}};
}

// ── 确定性算分 ──────────────────────────────────────────────────────────
/* 维度分 D_d = 25 × Σ(a_i·l_i) / Σ(a_i)，i 为该维度适用且非重复计分的评分项。
 * l_i ∈ 0..4，故满分恰好为 100。
 * 若某维度没有任何可评项，该维度记为待评（null），且不给出总分——
 * 「没有足够证据」不得自动补分，也不得默默重分配权重。 */
inline json computeComparisonScores(const json& checks) {
  std::map<std::string, double> earned;
  std::map<std::string, double> weight_sum;
  std::map<std::string, int> applicable_count;
  std::map<std::string, int> excluded_count;
  int total_checks = 0, verified_checks = 0, reference_verified_checks = 0;
  int defect_count = 0, duplicate_count = 0;
  json by_difference_type = json::object();
  json level_histogram = json::object();

  if (checks.is_array()) {
    for (const auto& check : checks) {
      if (!check.is_object()) continue;
      ++total_checks;
      // 学员引用的核验通过率是判定可信度的主指标；
      // 标准答案引用的核验通过率只是佐证质量，不决定分数。
      if (jsonFlag(check, "quoteVerified", true)) ++verified_checks;
      if (jsonFlag(check, "referenceQuoteVerified", true)) ++reference_verified_checks;
      const auto difference_type = jsonText(check, "differenceType");
      if (!difference_type.empty()) by_difference_type[difference_type] = by_difference_type.value(difference_type, 0) + 1;
      const int level = jsonInt(check, "level", 0);
      level_histogram[std::to_string(level)] = level_histogram.value(std::to_string(level), 0) + 1;
      const auto dimension = jsonText(check, "dimension");

      if (difference_type == "reference_defect") { ++defect_count; ++excluded_count[dimension]; continue; }
      if (jsonInt(check, "repeatOfRound", 0) > 0) { ++duplicate_count; continue; }
      if (!jsonFlag(check, "applicable", true)) { ++excluded_count[dimension]; continue; }

      const double importance = jsonReal(check, "importance", 1.0);
      earned[dimension] += importance * level;
      weight_sum[dimension] += importance;
      ++applicable_count[dimension];
    }
  }

  json dimension_scores = json::object();
  json missing_dimensions = json::array();
  double total = 0;
  bool complete = true;
  for (const auto& entry : dimensionWeights()) {
    const std::string dimension(entry.dimension);
    if (weight_sum[dimension] <= 0) {
      dimension_scores[dimension] = nullptr;
      missing_dimensions.push_back(dimension);
      complete = false;
      continue;
    }
    const double score = 25.0 * earned[dimension] / weight_sum[dimension];
    dimension_scores[dimension] = static_cast<int>(std::lround(score));
    total += entry.weight * score;
  }

  return {{"dimensionScores", dimension_scores},
          {"totalScore", complete ? json(static_cast<int>(std::lround(total))) : json(nullptr)},
          {"complete", complete},
          {"missingDimensions", missing_dimensions},
          {"summary", {{"checksTotal", total_checks},
                       {"checksVerified", verified_checks},
                       {"quoteVerificationRate", total_checks ? static_cast<double>(verified_checks) / total_checks : 0.0},
                       {"referenceVerifiedChecks", reference_verified_checks},
                       {"referenceQuoteVerificationRate", total_checks ? static_cast<double>(reference_verified_checks) / total_checks : 0.0},
                       {"excludedByDimension", excluded_count},
                       {"excludedTotal", defect_count + duplicate_count},
                       {"referenceDefectCount", defect_count},
                       {"duplicateErrorCount", duplicate_count},
                       {"applicableByDimension", applicable_count},
                       {"levelHistogram", level_histogram},
                       {"byDifferenceType", by_difference_type}}}};
}

// ── 报告区块组装 ────────────────────────────────────────────────────────
/* existing_dimension_scores 用于给出 delta：结题报告要回答的
 * 「对比口径分数与既有提示词评分的差异」，正是这块数据。
 * meta 里承载各阶段版本号——必须放在区块内部，因为 saveEvaluation 会
 * 覆写顶层 promptVersion。 */
inline json applyAnswerComparison(const json& normalized, const json& history, const json& references,
                                  const json& existing_dimension_scores, const json& meta) {
  const auto checks = normalized.value("checks", json::array());
  const auto scored = computeComparisonScores(checks);

  std::map<int, json> rounds;
  const auto ensure_round = [&](int round) -> json& {
    if (rounds.find(round) == rounds.end())
      rounds[round] = {{"round", round},
                       {"patientQuestion", ""},
                       {"learnerAnswer", ""},
                       {"referenceAnswer", ""},
                       {"referenceActions", json::array()},
                       {"checks", json::array()}};
    return rounds[round];
  };
  if (history.is_array()) {
    for (const auto& message : history) {
      if (!message.is_object()) continue;
      const int round = jsonInt(message, "round", 0);
      const auto role = jsonText(message, "role");
      if (role != "patient" && role != "user") continue;
      auto& bucket = ensure_round(round);
      const auto content = jsonText(message, "content");
      auto& slot = role == "patient" ? bucket["patientQuestion"] : bucket["learnerAnswer"];
      slot = slot.get<std::string>() + (slot.get<std::string>().empty() ? "" : "\n") + content;
    }
  }
  if (references.is_array()) {
    for (const auto& reference : references) {
      if (!reference.is_object()) continue;
      const int round = jsonInt(reference, "round", 0);
      auto& bucket = ensure_round(round);
      bucket["referenceAnswer"] = jsonText(reference, "referenceAnswer");
      if (reference.contains("referenceActions") && reference["referenceActions"].is_array())
        bucket["referenceActions"] = reference["referenceActions"];
    }
  }
  json learner_better = json::array();
  if (checks.is_array()) {
    for (const auto& check : checks) {
      if (!check.is_object()) continue;
      const int round = jsonInt(check, "round", 0);
      auto& bucket = ensure_round(round);
      bucket["checks"].push_back(check);
      if (jsonText(check, "differenceType") == "learner_better") learner_better.push_back(check);
    }
  }
  json rounds_array = json::array();
  for (auto& [round, bucket] : rounds) {
    (void)round;
    rounds_array.push_back(bucket);
  }

  json delta = json::object();
  if (existing_dimension_scores.is_object()) {
    for (const auto& entry : dimensionWeights()) {
      const std::string dimension(entry.dimension);
      const json mine = scored["dimensionScores"][dimension];
      const json theirs = existing_dimension_scores.contains(dimension) ? existing_dimension_scores[dimension] : json(nullptr);
      if (mine.is_number_integer() && theirs.is_number_integer())
        delta[dimension] = mine.get<int>() - theirs.get<int>();
    }
    const json mine_total = scored["totalScore"];
    const json theirs_total = existing_dimension_scores.value("totalScore", json(nullptr));
    if (mine_total.is_number_integer() && theirs_total.is_number_integer())
      delta["totalScore"] = mine_total.get<int>() - theirs_total.get<int>();
  }

  json block = {
      {"status", "ready"},
      {"rubricVersion", std::string(kAnswerRubricVersion)},
      {"formulaVersion", std::string(kAnswerComparisonFormulaVersion)},
      {"referencePromptVersion", jsonText(meta, "referencePromptVersion")},
      {"comparisonPromptVersion", jsonText(meta, "comparisonPromptVersion")},
      {"referenceModelVersion", jsonText(meta, "referenceModelVersion")},
      {"comparisonModelVersion", jsonText(meta, "comparisonModelVersion")},
      {"rounds", rounds_array},
      {"checks", checks},
      {"dropped", normalized.value("dropped", json::array())},
      {"summary", scored["summary"]},
      {"dimensionScores", scored["dimensionScores"]},
      {"totalScore", scored["totalScore"]},
      {"complete", scored["complete"]},
      {"missingDimensions", scored["missingDimensions"]},
      {"learnerBetter", learner_better},
      {"delta", delta}};
  if (!scored["complete"].get<bool>())
    block["lowConfidence"] = true;
  return block;
}

}  // namespace oral_training::compare
