#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

/* 「参考答案对比评分」的行为锚定量表（BARS）。
 *
 * 为什么是硬契约而不是提示词里现编：
 *   评分项编号（criterionId）同时被三处使用——生成标准答案时的服务动作标注、
 *   对比阶段的判定输出、服务端的算分与校验。任何一处自己造新编号，
 *   另外两处就对不上：模型可以凭空造一个「扣分项」把学员扣死，
 *   也可以把本该检查的项目漏掉。把量表放代码里与二进制同步版本，
 *   与 rag_types.h 的 kKnowledgeRubricVersion 是同一思路。
 *
 * 为什么锚点是 0—4 五级而不是「对/错」：
 *   客服应答的多数缺陷不是二值的（「答了一部分」很常见）。
 *   五级能把「漏了关键信息」和「只差一点」区分开，
 *   而分数由服务端算，锚点只需可判、不需可累加。
 *
 * 适用范围（applicable）的语义：
 *   不是「学员没做就算不适用」。不适用必须由情境决定，且判定者
 *   须给出指名患者原话的依据，服务端会逐字核对（见 answer_comparison.h）。
 *   本表里的 applicable 只是给模型的提示，不是免检依据。
 */
namespace oral_training::compare {

inline constexpr std::string_view kAnswerRubricVersion = "answer-rubric-v1";
inline constexpr std::string_view kAnswerComparisonFormulaVersion = "answer-comparison-v1";

struct DimensionWeight {
  std::string_view dimension;
  double weight;
};

/* 五维权重沿用既有评分口径，便于新旧成绩对照。 */
inline const std::vector<DimensionWeight>& dimensionWeights() {
  static const std::vector<DimensionWeight> weights = {
      {"knowledgeAccuracy", 0.25},
      {"medicalCompliance", 0.25},
      {"empathy", 0.20},
      {"needsDiscovery", 0.20},
      {"serviceEtiquette", 0.10}};
  return weights;
}

struct RubricCriterion {
  std::string_view id;        // 全局唯一，模型不得自造
  std::string_view name;
  std::string_view dimension; // 必须是 dimensionWeights() 中的一个
  std::string_view scope;     // per_round（逐轮计分）| whole（整场只计一次）
  bool numeric;               // 涉及价格/时长/次数/周期：等级由服务端重判，不采信模型
  double importance;          // a_i，同维内为正
  std::string_view applicable;
  std::array<std::string_view, 5> anchors; // 0..4 级行为锚点
};

inline const std::vector<RubricCriterion>& answerRubric() {
  static const std::vector<RubricCriterion> rubric = {
      // ── 知识准确性 ────────────────────────────────────────────────
      {"knowledge.answer_core_question", "正面回应核心问题", "knowledgeAccuracy", "per_round", false, 3.0,
       "患者提出可回答的问题时适用。",
       {"答非所问或回避，完全未触及患者问题",
        "提及相关话题，但未回答患者所问",
        "回答了问题的一部分，核心内容仍缺失",
        "回答了核心问题，细节略简",
        "直接、完整地回答核心问题"}},
      {"knowledge.factual_accuracy", "事实数据准确", "knowledgeAccuracy", "per_round", true, 3.0,
       "本轮涉及价格、时长、包含项、次数等可核验事实时适用。",
       {"给出错误数值，或编造资料中没有的事实",
        "数值存在明显偏差",
        "数值方向正确但不完整（如只给单值而非区间）",
        "数值基本准确，遗漏次要条件",
        "数值与区间准确，单位与适用条件完整"}},
      {"knowledge.no_unfounded_claim", "不做无依据断言", "knowledgeAccuracy", "per_round", false, 2.0,
       "任何一轮都适用：不得陈述资料未支持的内容。",
       {"明确断言资料不支持的内容",
        "暗示未获证实的信息",
        "表述模糊，可能被理解为断言",
        "基本谨慎，个别措辞偏绝对",
        "全程只陈述有依据的内容"}},
      {"knowledge.disclose_scope_conditions", "说明适用条件与范围", "knowledgeAccuracy", "per_round", false, 1.0,
       "本轮答案带前提条件（个体差异、需检查确认、有效期）时适用。",
       {"未提任何条件即下结论",
        "笼统带过条件",
        "提到部分适用条件",
        "说明了主要适用条件",
        "完整说明条件与个体差异"}},

      // ── 医疗合规 ──────────────────────────────────────────────────
      {"compliance.no_diagnosis", "不越权诊断或制定方案", "medicalCompliance", "per_round", false, 3.0,
       "本轮涉及病因、症状判断或治疗方式时适用。",
       {"给出明确诊断或治疗方案",
        "暗示病因或治疗方式",
        "部分表述接近诊断结论",
        "基本守住边界，个别措辞模糊",
        "完全不做诊疗判断"}},
      {"compliance.no_effect_promise", "不承诺疗效、止痛或绝对安全", "medicalCompliance", "per_round", false, 3.0,
       "本轮涉及治疗效果、疼痛或风险时适用。",
       {"明确承诺疗效、无痛或绝对安全",
        "用「基本不会」「应该没事」等软化承诺",
        "表述可能被患者理解为承诺",
        "基本合规，偶有不当措辞",
        "明确不作承诺并给出依据"}},
      {"compliance.defer_to_doctor", "涉诊疗判断时转交医生", "medicalCompliance", "per_round", false, 2.0,
       "本轮出现须由医生判断的问题（是否适合、是否拔除、症状原因、紧急程度）时适用。",
       {"自行给出诊疗结论",
        "以自行判断为主，附带建议就医",
        "转交医生但表述含糊",
        "明确说明需医生评估",
        "明确转交并说明需要哪些检查或信息"}},
      {"compliance.no_fabricated_policy", "不编造机构政策或优惠", "medicalCompliance", "per_round", false, 2.0,
       "本轮涉及价格、优惠、机构政策、预约安排时适用。",
       {"编造价格、优惠或机构政策",
        "说出台账之外的承诺",
        "表述含糊，可能被当作承诺",
        "基本不编造，个别表述不严谨",
        "只陈述资料内内容，未知项明确说明待确认"}},

      // ── 共情能力 ──────────────────────────────────────────────────
      {"empathy.acknowledge_concern", "识别并回应具体担忧", "empathy", "per_round", false, 3.0,
       "患者表达了具体担忧、疑虑或情绪时适用；患者仅作事实性提问时不适用。",
       {"完全忽略患者的情绪或担忧",
        "泛泛安慰，未针对具体担忧",
        "提及担忧但未实质回应",
        "回应了主要担忧",
        "准确识别并针对性回应，表述贴切"}},
      {"empathy.appropriate_tone", "表达方式得体有温度", "empathy", "per_round", false, 2.0,
       "任何一轮都适用：考察语气而非内容。",
       {"语气生硬、冷淡或说教",
        "语气偏冷，缺少温度",
        "中性，无明显温度",
        "语气自然得体",
        "自然亲切且专业，分寸恰当"}},
      {"empathy.no_dismissal", "不敷衍、不打发", "empathy", "per_round", false, 2.0,
       "患者提出诉求或疑虑的轮次适用。",
       {"明显敷衍（如「放心就行」）",
        "简短带过患者的诉求",
        "有回应但不够重视",
        "认真对待，无敷衍感",
        "认真对待并跟进患者关切"}},

      // ── 需求挖掘 ──────────────────────────────────────────────────
      {"needs.identify_current_demand", "识别当前诉求", "needsDiscovery", "per_round", false, 2.0,
       "任何一轮都适用：考察是否理解患者当下要什么。",
       {"未理解患者当前诉求",
        "理解偏差，回应了别的需求",
        "粗略理解诉求",
        "准确识别当前诉求",
        "准确识别并复述确认"}},
      {"needs.clarify_required_information", "澄清必要信息", "needsDiscovery", "per_round", false, 2.0,
       "本轮存在影响下一步的关键信息缺口时适用。",
       {"未澄清任何必要信息，且未说明需要澄清",
        "提了问题但方向不对",
        "问了部分必要信息，仍有关键缺口",
        "主要信息问全，遗漏次要项",
        "信息完整且问法自然"}},
      {"needs.avoid_repetitive_questioning", "不重复追问", "needsDiscovery", "per_round", false, 1.0,
       "本轮之前已有可用的患者信息时适用。",
       {"重复追问已得到答复的信息",
        "问题与上文高度重叠",
        "有少量重复",
        "基本不重复",
        "完全承接上文，无重复"}},
      {"needs.final_demand_clarified", "核心需求最终澄清", "needsDiscovery", "whole", false, 3.0,
       "整场项：对话结束时评价需求是否被澄清。",
       {"全程未澄清核心需求",
        "只澄清了次要需求",
        "核心需求部分澄清",
        "核心需求基本澄清",
        "核心需求完全澄清并有明确下一步"}},

      // ── 服务礼仪 ──────────────────────────────────────────────────
      {"etiquette.clear_expression", "表达清晰", "serviceEtiquette", "per_round", false, 2.0,
       "任何一轮都适用。",
       {"表述混乱难懂",
        "表述冗长含糊",
        "基本能懂，不够简洁",
        "清晰简洁",
        "清晰、简洁、有条理"}},
      {"etiquette.respectful_language", "尊重与礼貌", "serviceEtiquette", "per_round", false, 2.0,
       "任何一轮都适用。",
       {"失礼、指责或命令口吻",
        "生硬，缺少基本礼貌用语",
        "礼貌但生硬",
        "礼貌得体",
        "礼貌且尊重患者选择"}},
      {"etiquette.next_step", "给出适当下一步", "serviceEtiquette", "per_round", false, 2.0,
       "本轮有可推进的下一步（预约、检查、复诊、等待确认）时适用。",
       {"未给出下一步，患者无从跟进",
        "给出模糊建议",
        "给出下一步但不够具体",
        "给出明确的下一步",
        "给出明确、可执行且尊重意愿的下一步"}},
      {"etiquette.consistency", "整场前后一致", "serviceEtiquette", "whole", false, 1.0,
       "整场项：检查跨轮陈述是否自相矛盾。",
       {"前后说法矛盾",
        "多处不一致",
        "有轻微不一致",
        "基本一致",
        "全程一致无矛盾"}}};
  return rubric;
}

/* 供提示词使用的量表摘要：只发编号与名称，不发 0—4 级锚点正文。
 * 完整锚点留在服务端做校验与报告用，避免提示词无谓膨胀。 */
inline std::string rubricPromptHint() {
  std::string text;
  for (const auto& entry : dimensionWeights()) {
    text += "- " + std::string(entry.dimension) + "：\n";
    for (const auto& criterion : answerRubric()) {
      if (criterion.dimension != entry.dimension) continue;
      text += "    " + std::string(criterion.id) + "（" + std::string(criterion.name) + "）";
      if (criterion.scope == std::string_view("whole")) text += " [整场项，round 填 0]";
      text += "\n";
    }
  }
  return text;
}

/* 按编号查找评分项。
 *
 * 先精确匹配；失败时按「点号之后的项名」做唯一后缀匹配——模型常把前缀写成
 * 完整维度名（medicalCompliance.no_diagnosis），而量表用的是短前缀
 * （compliance.no_diagnosis）。这一步只消除前缀写法差异，不猜测模型想指哪一项：
 * 后缀命中多条（歧义）或没有命中，都返回 nullptr，由调用方丢弃并留痕。 */
inline const RubricCriterion* findCriterion(std::string_view id) {
  for (const auto& criterion : answerRubric())
    if (criterion.id == id) return &criterion;
  const auto dot = id.find('.');
  if (dot == std::string_view::npos) return nullptr;
  const auto suffix = id.substr(dot);
  const RubricCriterion* matched = nullptr;
  for (const auto& criterion : answerRubric()) {
    const auto candidate_dot = criterion.id.find('.');
    if (candidate_dot == std::string_view::npos) continue;
    if (criterion.id.substr(candidate_dot) != suffix) continue;
    if (matched != nullptr) return nullptr;  // 歧义：宁可不接受，也不猜
    matched = &criterion;
  }
  return matched;
}

inline std::vector<std::string_view> rubricCriterionIds() {
  std::vector<std::string_view> ids;
  ids.reserve(answerRubric().size());
  for (const auto& criterion : answerRubric()) ids.push_back(criterion.id);
  return ids;
}

/* 量表自检：编号唯一、维度合法、权重为正、锚点齐全、同一维度同时存在逐轮与整场项。
 * 单测直接调用；服务启动时也可调用一次，避免把坏量表带上线。 */
inline bool validateAnswerRubric(std::string* error = nullptr) {
  const auto fail = [&](const std::string& message) {
    if (error) *error = message;
    return false;
  };
  std::vector<std::string_view> seen;
  for (const auto& criterion : answerRubric()) {
    if (criterion.id.empty()) return fail("criterion id 为空");
    if (std::find(seen.begin(), seen.end(), criterion.id) != seen.end())
      return fail("criterion id 重复：" + std::string(criterion.id));
    seen.push_back(criterion.id);
    if (criterion.scope != "per_round" && criterion.scope != "whole")
      return fail("scope 非法：" + std::string(criterion.id));
    if (!(criterion.importance > 0)) return fail("importance 非正：" + std::string(criterion.id));
    bool dimension_known = false;
    for (const auto& entry : dimensionWeights())
      if (entry.dimension == criterion.dimension) dimension_known = true;
    if (!dimension_known) return fail("dimension 未在权重表中：" + std::string(criterion.id));
    for (std::size_t level = 0; level < criterion.anchors.size(); ++level)
      if (criterion.anchors[level].empty())
        return fail("锚点缺失：" + std::string(criterion.id) + " 第 " + std::to_string(level) + " 级");
  }
  for (const auto& entry : dimensionWeights()) {
    if (!(entry.weight > 0)) return fail("维度权重非正：" + std::string(entry.dimension));
    bool covered = false;
    for (const auto& criterion : answerRubric())
      if (criterion.dimension == entry.dimension) covered = true;
    if (!covered) return fail("维度无评分项：" + std::string(entry.dimension));
  }
  double total = 0;
  for (const auto& entry : dimensionWeights()) total += entry.weight;
  if (std::abs(total - 1.0) > 0.0001) return fail("维度权重合计不为 1");
  return true;
}

}  // namespace oral_training::compare
