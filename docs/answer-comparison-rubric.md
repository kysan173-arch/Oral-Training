# 对练舱参考答案对比评分：行为锚定量表与取数规程

> 对应实现：`backend/src/answer_rubric.h`（量表常量）、`backend/src/answer_comparison.h`（结算模块）
> 量表版本：`answer-rubric-v1`　公式版本：`answer-comparison-v1`

---

## 一、这份文档解决什么

对比评分跑出来的分数**能不能用、怎么用、怎么验证它准不准**，都取决于三件事：

1. **量表**是否合理——它是"什么算做得好"的唯一依据，模型不能临时创造扣分要求；
2. **判定质量**能不能被度量——引用是否可核验、有没有被模型自造的规则污染；
3. **取数口径**是否统一——结题报告里的数字必须能被复现。

本文给出这三件事的完整口径。**量表部分请交专家评审**（尤其是权重 `a_i` 与 0—4 级锚点的措辞）。

---

## 二、量表（`answer-rubric-v1`）

### 2.1 五个维度与权重

沿用既有五维口径，便于与提示词评分对照：

| 维度 | 权重 | 对比的核心内容 |
|---|---|---|
| `knowledgeAccuracy` | 25% | 是否准确回应问题，是否出现错误、遗漏或无依据的断言 |
| `medicalCompliance` | 25% | 是否符合客服职责，是否存在越权判断、不当承诺 |
| `empathy` | 20% | 是否识别并回应患者的**具体**担忧，表达是否合适 |
| `needsDiscovery` | 20% | 是否利用已有信息，提出当前必要且有效的问题 |
| `serviceEtiquette` | 10% | 是否清晰、尊重、自然，并给出适当的下一步 |

> ⚠️ **权重是初始版本，尚未经专家标注验证**——它们来自既有评分口径的沿袭，不是实证最优解。

### 2.2 逐项锚点

等级含义（对所有项一致）：

| 等级 | 判定含义 |
|---|---|
| **0** | 出现明显错误或相反行为，妨碍服务目标 |
| **1** | 有回应，但关键问题未处理，存在明显缺陷 |
| **2** | 部分完成目标，仍有实质遗漏 |
| **3** | 完成主要目标，只有轻微不足 |
| **4** | 准确、完整、适度地完成目标 |

`作用域`：`逐轮` = 每轮计一次；`整场` = 整场只计一次（round 填 0）。
`数值` 列为「是」的项，**等级不采信模型判定**，由服务端抽取数值与单位后硬比对。

#### knowledgeAccuracy（知识准确性，25%）

**`knowledge.answer_core_question`** 正面回应核心问题｜逐轮｜权重 3｜适用：患者提出可回答的问题时

| 级 | 锚点 |
|---|---|
| 0 | 答非所问或回避，完全未触及患者问题 |
| 1 | 提及相关话题，但未回答患者所问 |
| 2 | 回答了问题的一部分，核心内容仍缺失 |
| 3 | 回答了核心问题，细节略简 |
| 4 | 直接、完整地回答核心问题 |

**`knowledge.factual_accuracy`** 事实数据准确｜逐轮｜**数值**｜权重 3｜适用：本轮涉及价格、时长、包含项、次数等可核验事实

| 级 | 锚点 |
|---|---|
| 0 | 给出错误数值，或编造资料中没有的事实 |
| 1 | 数值存在明显偏差 |
| 2 | 数值方向正确但不完整（如只给单值而非区间） |
| 3 | 数值基本准确，遗漏次要条件 |
| 4 | 数值与区间准确，单位与适用条件完整 |

**`knowledge.no_unfounded_claim`** 不做无依据断言｜逐轮｜权重 2｜适用：任何一轮

| 级 | 锚点 |
|---|---|
| 0 | 明确断言资料不支持的内容 |
| 1 | 暗示未获证实的信息 |
| 2 | 表述模糊，可能被理解为断言 |
| 3 | 基本谨慎，个别措辞偏绝对 |
| 4 | 全程只陈述有依据的内容 |

**`knowledge.disclose_scope_conditions`** 说明适用条件与范围｜逐轮｜权重 1｜适用：本轮答案带前提条件时

| 级 | 锚点 |
|---|---|
| 0 | 未提任何条件即下结论 |
| 1 | 笼统带过条件 |
| 2 | 提到部分适用条件 |
| 3 | 说明了主要适用条件 |
| 4 | 完整说明条件与个体差异 |

#### medicalCompliance（医疗合规，25%）

**`compliance.no_diagnosis`** 不越权诊断或制定方案｜逐轮｜权重 3｜适用：本轮涉及病因、症状判断或治疗方式

| 级 | 锚点 |
|---|---|
| 0 | 给出明确诊断或治疗方案 |
| 1 | 暗示病因或治疗方式 |
| 2 | 部分表述接近诊断结论 |
| 3 | 基本守住边界，个别措辞模糊 |
| 4 | 完全不做诊疗判断 |

**`compliance.no_effect_promise`** 不承诺疗效、止痛或绝对安全｜逐轮｜权重 3｜适用：本轮涉及治疗效果、疼痛或风险

| 级 | 锚点 |
|---|---|
| 0 | 明确承诺疗效、无痛或绝对安全 |
| 1 | 用「基本不会」「应该没事」等软化承诺 |
| 2 | 表述可能被患者理解为承诺 |
| 3 | 基本合规，偶有不当措辞 |
| 4 | 明确不作承诺并给出依据 |

**`compliance.defer_to_doctor`** 涉诊疗判断时转交医生｜逐轮｜权重 2｜适用：本轮出现须由医生判断的问题

| 级 | 锚点 |
|---|---|
| 0 | 自行给出诊疗结论 |
| 1 | 以自行判断为主，附带建议就医 |
| 2 | 转交医生但表述含糊 |
| 3 | 明确说明需医生评估 |
| 4 | 明确转交并说明需要哪些检查或信息 |

**`compliance.no_fabricated_policy`** 不编造机构政策或优惠｜逐轮｜权重 2｜适用：本轮涉及价格、优惠、机构政策、预约安排

| 级 | 锚点 |
|---|---|
| 0 | 编造价格、优惠或机构政策 |
| 1 | 说出台账之外的承诺 |
| 2 | 表述含糊，可能被当作承诺 |
| 3 | 基本不编造，个别表述不严谨 |
| 4 | 只陈述资料内内容，未知项明确说明待确认 |

#### empathy（共情能力，20%）

**`empathy.acknowledge_concern`** 识别并回应具体担忧｜逐轮｜权重 3｜适用：患者表达了具体担忧、疑虑或情绪时（仅作事实性提问时不适用）

| 级 | 锚点 |
|---|---|
| 0 | 完全忽略患者的情绪或担忧 |
| 1 | 泛泛安慰，未针对具体担忧 |
| 2 | 提及担忧但未实质回应 |
| 3 | 回应了主要担忧 |
| 4 | 准确识别并针对性回应，表述贴切 |

**`empathy.appropriate_tone`** 表达方式得体有温度｜逐轮｜权重 2｜适用：任何一轮（考察语气而非内容）

| 级 | 锚点 |
|---|---|
| 0 | 语气生硬、冷淡或说教 |
| 1 | 语气偏冷，缺少温度 |
| 2 | 中性，无明显温度 |
| 3 | 语气自然得体 |
| 4 | 自然亲切且专业，分寸恰当 |

**`empathy.no_dismissal`** 不敷衍、不打发｜逐轮｜权重 2｜适用：患者提出诉求或疑虑的轮次

| 级 | 锚点 |
|---|---|
| 0 | 明显敷衍（如「放心就行」） |
| 1 | 简短带过患者的诉求 |
| 2 | 有回应但不够重视 |
| 3 | 认真对待，无敷衍感 |
| 4 | 认真对待并跟进患者关切 |

#### needsDiscovery（需求挖掘，20%）

**`needs.identify_current_demand`** 识别当前诉求｜逐轮｜权重 2｜适用：任何一轮

| 级 | 锚点 |
|---|---|
| 0 | 未理解患者当前诉求 |
| 1 | 理解偏差，回应了别的需求 |
| 2 | 粗略理解诉求 |
| 3 | 准确识别当前诉求 |
| 4 | 准确识别并复述确认 |

**`needs.clarify_required_information`** 澄清必要信息｜逐轮｜权重 2｜适用：本轮存在影响下一步的关键信息缺口

| 级 | 锚点 |
|---|---|
| 0 | 未澄清任何必要信息，且未说明需要澄清 |
| 1 | 提了问题但方向不对 |
| 2 | 问了部分必要信息，仍有关键缺口 |
| 3 | 主要信息问全，遗漏次要项 |
| 4 | 信息完整且问法自然 |

**`needs.avoid_repetitive_questioning`** 不重复追问｜逐轮｜权重 1｜适用：本轮之前已有可用的患者信息

| 级 | 锚点 |
|---|---|
| 0 | 重复追问已得到答复的信息 |
| 1 | 问题与上文高度重叠 |
| 2 | 有少量重复 |
| 3 | 基本不重复 |
| 4 | 完全承接上文，无重复 |

**`needs.final_demand_clarified`** 核心需求最终澄清｜**整场**｜权重 3

| 级 | 锚点 |
|---|---|
| 0 | 全程未澄清核心需求 |
| 1 | 只澄清了次要需求 |
| 2 | 核心需求部分澄清 |
| 3 | 核心需求基本澄清 |
| 4 | 核心需求完全澄清并有明确下一步 |

#### serviceEtiquette（服务礼仪，10%）

**`etiquette.clear_expression`** 表达清晰｜逐轮｜权重 2｜适用：任何一轮

| 级 | 锚点 |
|---|---|
| 0 | 表述混乱难懂 |
| 1 | 表述冗长含糊 |
| 2 | 基本能懂，不够简洁 |
| 3 | 清晰简洁 |
| 4 | 清晰、简洁、有条理 |

**`etiquette.respectful_language`** 尊重与礼貌｜逐轮｜权重 2｜适用：任何一轮

| 级 | 锚点 |
|---|---|
| 0 | 失礼、指责或命令口吻 |
| 1 | 生硬，缺少基本礼貌用语 |
| 2 | 礼貌但生硬 |
| 3 | 礼貌得体 |
| 4 | 礼貌且尊重患者选择 |

**`etiquette.next_step`** 给出适当下一步｜逐轮｜权重 2｜适用：本轮有可推进的下一步（预约、检查、复诊、等待确认）

| 级 | 锚点 |
|---|---|
| 0 | 未给出下一步，患者无从跟进 |
| 1 | 给出模糊建议 |
| 2 | 给出下一步但不够具体 |
| 3 | 给出明确的下一步 |
| 4 | 给出明确、可执行且尊重意愿的下一步 |

**`etiquette.consistency`** 整场前后一致｜**整场**｜权重 1

| 级 | 锚点 |
|---|---|
| 0 | 前后说法矛盾 |
| 1 | 多处不一致 |
| 2 | 有轻微不一致 |
| 3 | 基本一致 |
| 4 | 全程一致无矛盾 |

---

## 三、评分口径与公式

### 3.1 维度分

```
D_d = 25 × Σ(a_i · l_i) / Σ(a_i)
```

- `i` ∈ 该维度**适用且未重复计分**的评分项集合
- `l_i ∈ 0..4`，`a_i` 为量表声明的重要性权重
- 因 `l_i` 最高为 4，故满分恰好 100

### 3.2 总分

```
totalScore = Σ(w_d × D_d)      w_d ∈ {0.25, 0.25, 0.20, 0.20, 0.10}
```

**若任一维度没有任何可评项，该维度记为 `null`，且不给总分**——没有足够证据时不自动补分，也不默默重分配权重（此时区块带 `lowConfidence: true`）。

### 3.3 三条硬规则

| 规则 | 实现 |
|---|---|
| **同一问题不重复扣分** | 事实/边界类错误按评分项去重，后续出现标记 `repeatOfRound` 并排除出分母 |
| **整场项只计一次** | `scope=whole`，重复判定直接丢弃并记入 `dropped` |
| **标准答案缺陷不扣学员分** | `differenceType=reference_defect` 的项排除出分母，但统计排除占比 |

### 3.4 模型只定位、服务器算分

模型输出的**等级可被服务器覆盖或钳制**：

| 情形 | 服务端处理 | 标记 |
|---|---|---|
| **学员引用**（`userQuote`）无法逐字核验 | 等级归 0，且**不参与**自洽钳制（防止 `equivalent` 保底把不可信判定抬到 3 分） | `quoteVerified=false` |
| **标准答案引用**（`referenceQuote`）无法逐字核验 | **只标记，不影响等级**——它只是佐证材料，抄错不代表判定本身错 | `referenceQuoteVerified=false` |
| 数值类**基准侧**无数值 | 保留模型等级（标准答案本就没说数字，无从比对） | `source=server_numeric` |
| 数值类**学员侧**无数值 | 保留模型等级（「有没有给数字」属于遗漏，交给 `differenceType` 判断） | `source=server_numeric` |
| 数值类不一致 | 等级归 0 | `source=server_numeric` |
| 等级与差异类型不自洽 | 钳制到合法档 | `levelAdjusted=true` |
| 编号不在量表内 | 整条丢弃 | `dropped[].reason=criterion_not_in_rubric` |
| 判「不适用」但给不出可核验的患者原话 | 强制判为适用 | — |

**数据流**：`evaluations.report.answerComparison`

```
status / rubricVersion / formulaVersion
referencePromptVersion / comparisonPromptVersion / referenceModelVersion / comparisonModelVersion
rounds[]   —— round / patientQuestion / learnerAnswer / referenceAnswer / referenceActions / checks[]
checks[]   —— criterionId / dimension / scope / round / applicable / notApplicableReason /
              level / levelAdjusted / userQuote / referenceQuote /
              quoteVerified（学员引用可核验）/ referenceQuoteVerified（佐证引用可核验）/
              source / differenceType / reason / repeatOfRound / importance / numeric
summary    —— checksTotal / checksVerified / quoteVerificationRate /
              referenceVerifiedChecks / referenceQuoteVerificationRate /
              excludedByDimension / excludedTotal / referenceDefectCount /
              duplicateErrorCount / levelHistogram / byDifferenceType / applicableByDimension
dimensionScores / totalScore / complete / missingDimensions / lowConfidence
learnerBetter[]  —— 学员优于标准答案的条目
delta            —— 与既有 dimensionScores / totalScore 的差
```

---

## 四、判定质量指标

结题报告里应当同时汇报 **方法** 与 **方法的质量**。后者用以下指标：

| 指标 | 取自 | 健康区间 | 不健康说明 |
|---|---|---|---|
| **学员引用核验通过率** `quoteVerificationRate` | `summary` | **≥ 0.90** | **判定可信度的主指标**。低于则说明模型定位能力不足，分数被系统性压低——需检查对比阶段 prompt |
| **标准答案引用核验通过率** `referenceQuoteVerificationRate` | `summary` | **≥ 0.90** | 佐证质量指标，**不决定分数**。偏低说明模型抄引用不准，可据此判断对比阶段 prompt 是否还需收紧 |
| **自造编号率** | `dropped` 中 `criterion_not_in_rubric` 占比 | **= 0** | 非零说明量表提示不到位 |
| **缺陷排除占比** | `referenceDefectCount / checksTotal` | **≤ 0.10** | 过高说明模型在滥用「标准答案有缺陷」这个免责出口 |
| **重复计分剔除数** | `duplicateErrorCount` | 任意 | 说明去重规则确实在起作用 |
| **维度覆盖完整率** | `complete == true` 的占比 | **≥ 0.80** | 低于则大量报告给不出总分 |

> **本机实测记录**
>
> - 改进 prompt 明确引用字段来源后，`quoteVerificationRate` 从 **0.60** 提升到 **1.00**。指标本身即可作为 prompt 迭代的评估依据。
> - **标准答案生成走微调槽（sft-v4）实测**：单轮提示词实际 **811 / 874 / 927 tokens**，远低于该实例的 4096 上下文（单轮耗时 1.0～3.8 秒），**4096 足够，无需扩容**。
> - 报告区块里 `referenceModelVersion` 形如 `litellm:sft-v4 (tuned)@N`（`(tuned)` 标记即表示走了微调槽）；`comparisonModelVersion` 是对比判定所用模型（实测为基座）。

---

## 五、人工标注规程

用于验证量表与判定质量（专家评审用）。

**抽样单位**：`会话 × 轮次 × 评分项`。
建议规模：**≥ 30 场 × 每场全部轮次**，覆盖全部五个场景分类（含无 RAG 场景）。

**CSV 字段**：

| 字段 | 取值 | 说明 |
|---|---|---|
| `session_id` | 文本 | 会话 ID |
| `round` | 整数 | 轮次；整场项填 0 |
| `criterion_id` | 文本 | 量表编号 |
| `expert_applicable` | `true`/`false` | 该轮该情境下这项是否应当评价 |
| `expert_level` | `0`-`4` | 专家给出的等级 |
| `expert_quote` | 文本 | 学员原话（须逐字摘自学员答复） |
| `machine_level` | `0`-`4` | 系统判定（从报告抄录） |
| `machine_quote_verified` | `true`/`false` | 系统是否核验通过 |
| `disagreement_note` | 文本 | 分歧原因（可选） |

**一致性统计**：
- **等级完全一致率**（`expert_level == machine_level`）
- **相邻档一致率**（差值 ≤ 1）——实际工程中更有意义
- **适用性判定一致率**（避免 N/A 成为逃逸口）
- **加权 Kappa**（剔除随机一致）

---

## 六、结题报告取数模板（PostgreSQL）

以下均可直接从 `evaluations.report` 的 JSON 取，**无需新增表或列**。

```sql
-- ① 开了开关的会话数 / 成功出区块的比例
SELECT
  count(*) FILTER (WHERE jsonb_exists(report::jsonb, 'answerComparison'))                    AS with_block,
  count(*) FILTER (WHERE report::jsonb->'answerComparison'->>'status' = 'ready')             AS ready,
  count(*) FILTER (WHERE report::jsonb->'answerComparison'->>'status' = 'failed')            AS failed,
  count(*)                                                                                   AS total
FROM evaluations;

-- ② 引用核验通过率分布（判定质量核心指标）
SELECT
  report::jsonb->'answerComparison'->'summary'->>'quoteVerificationRate' AS verify_rate,
  count(*)
FROM evaluations
WHERE report::jsonb->'answerComparison'->>'status' = 'ready'
GROUP BY 1 ORDER BY 1;

-- ③ 五个维度的对比口径分数分布（均值 / 标准差 / 样本数）
SELECT
  round(avg((report::jsonb->'answerComparison'->'dimensionScores'->>'knowledgeAccuracy')::numeric), 1) AS knowledge_avg,
  round(avg((report::jsonb->'answerComparison'->'dimensionScores'->>'medicalCompliance')::numeric), 1) AS compliance_avg,
  round(avg((report::jsonb->'answerComparison'->'dimensionScores'->>'empathy')::numeric), 1)           AS empathy_avg,
  round(avg((report::jsonb->'answerComparison'->'dimensionScores'->>'needsDiscovery')::numeric), 1)    AS needs_avg,
  round(avg((report::jsonb->'answerComparison'->'dimensionScores'->>'serviceEtiquette')::numeric), 1)  AS etiquette_avg,
  count(*) AS samples
FROM evaluations
WHERE report::jsonb->'answerComparison'->>'status' = 'ready';

-- ④ 逐项等级直方图（定位「哪条要求学员普遍做不到」——教学价值最高的数据）
SELECT
  c->>'criterionId'                                        AS criterion_id,
  count(*)                                                 AS n,
  round(avg((c->>'level')::numeric), 2)                    AS avg_level,
  count(*) FILTER (WHERE (c->>'level')::int = 0)           AS lv0,
  count(*) FILTER (WHERE (c->>'level')::int = 1)           AS lv1,
  count(*) FILTER (WHERE (c->>'level')::int = 2)           AS lv2,
  count(*) FILTER (WHERE (c->>'level')::int = 3)           AS lv3,
  count(*) FILTER (WHERE (c->>'level')::int = 4)           AS lv4
FROM evaluations e, jsonb_array_elements(e.report::jsonb->'answerComparison'->'checks') c
WHERE e.report::jsonb->'answerComparison'->>'status' = 'ready'
GROUP BY 1 ORDER BY avg_level ASC;

-- ⑤ 差异类型分布（等效 / 实质遗漏 / 事实边界错误 / 学员更优 / 标准答案缺陷）
SELECT
  d.key AS difference_type, sum(d.value::int) AS n
FROM evaluations e,
     jsonb_each_text(e.report::jsonb->'answerComparison'->'summary'->'byDifferenceType') d
GROUP BY 1 ORDER BY n DESC;

-- ⑥ 与既有提示词分数的差异（delta）——一致性/相关性分析的关键数据
SELECT
  round(avg((report::jsonb->'answerComparison'->'delta'->>'knowledgeAccuracy')::numeric), 1) AS d_knowledge,
  round(avg((report::jsonb->'answerComparison'->'delta'->>'medicalCompliance')::numeric), 1) AS d_compliance,
  round(avg((report::jsonb->'answerComparison'->'delta'->>'empathy')::numeric), 1)           AS d_empathy,
  round(avg((report::jsonb->'answerComparison'->'delta'->>'needsDiscovery')::numeric), 1)    AS d_needs,
  round(avg((report::jsonb->'answerComparison'->'delta'->>'serviceEtiquette')::numeric), 1)  AS d_etiquette,
  round(avg((report::jsonb->'answerComparison'->'delta'->>'totalScore')::numeric), 1)        AS d_total,
  count(*) AS samples
FROM evaluations
WHERE report::jsonb->'answerComparison'->>'status' = 'ready';

-- ⑦ 明细导出（供人工标注与相关性分析；自变量 x = 既有分，因变量 y = 对比分）
SELECT
  e.session_id,
  e.model_version,
  (report::jsonb->'dimensionScores'->>'knowledgeAccuracy')::int                         AS x_knowledge,
  (report::jsonb->'answerComparison'->'dimensionScores'->>'knowledgeAccuracy')::int     AS y_knowledge,
  (report::jsonb->>'totalScore')::int                                                   AS x_total,
  (report::jsonb->'answerComparison'->>'totalScore')::int                               AS y_total,
  report::jsonb->'answerComparison'->>'referenceModelVersion'                           AS reference_model,
  report::jsonb->'answerComparison'->>'comparisonModelVersion'                          AS comparison_model,
  report::jsonb->'answerComparison'->'summary'->>'quoteVerificationRate'                AS verify_rate
FROM evaluations e
WHERE report::jsonb->'answerComparison'->>'status' = 'ready';
```

**相关性分析**：把 ⑦ 的 `x_*` 与 `y_*` 交给统计工具算 **Pearson 相关系数与组内相关系数（ICC）**。两者高相关 → 新方法结论与既有方法一致（相互印证）；差异集中在某维度 → 该维度的提示词评分可能不够可靠，这本身就是可写入报告的发现。

---

## 七、开启方式与版本升级

### 7.1 开关与槽位

对比评分**默认关闭**（`Config::answer_comparison_enabled = false`）。开启方式：

```powershell
# merge29-local\merge29.env
ANSWER_COMPARISON_ENABLED=true
# 标准答案由哪个槽位生成：tuned（微调实例，默认）/ base（基座）
ANSWER_COMPARISON_REFERENCE_SLOT=tuned
```

`ANSWER_COMPARISON_REFERENCE_SLOT` 有两个用途：① 微调槽上下文不足时**免重编译**回退主槽；② 把「标准答案由微调还是基座生成」当作结题报告里的**对照实验变量**。

**非法取值在启动时直接报错**，不静默降级——槽位写错会让报告里的模型版本与实际不符，而那个字段正是结题报告要引用的。

关闭时该环节完全不执行，报告里不会出现 `answerComparison` 区块，**既有五维评分与总分不受任何影响**（已在第四环境实测确认：关闭时五维正常出分、总分 88，无区块）。

开启后每场评分额外产生 **N + 1 次**模型调用（N = 客服轮数；逐轮生成理想答复 + 一次批量对比），受既有 `MODEL_CALL_LIMIT` 与调用审计约束。

### 7.2 结果不参与既有分数

`answerComparison` 区块**不写入** `dimensionScores`、**不改** `totalScore`；它自带一套按量表算出的维度分与总分，并记录与既有分数的 `delta`。这样新旧口径并存、可比，便于先验证再决定是否提升为主分。

### 7.3 版本升级须知

三处版本号会随报告固化：`rubricVersion`、`formulaVersion`、以及各阶段的 `referencePromptVersion` / `comparisonPromptVersion`。**修改量表或公式时必须同步改版本常量**（`answer_rubric.h` 的 `kAnswerRubricVersion` / `kAnswerComparisonFormulaVersion`），否则跨版本的分数会被误当作同一口径合并统计。

标准答案生成后即固化保存在报告里，**刷新报告不会重新生成**；失败任务的降级结果也会写入区块（`status=failed` + `reason`），不会连带毁掉既有评分报告。
