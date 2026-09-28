-- 024_scenario_dimension_weights.sql
-- 建立「场景 ↔ 评分维度」的映射，让「练哪个场景」可以由「弱项在哪个维度」推出来。
--
-- 问题：scenarios.focus 是自由文本数组（如 ["需求挖掘","引导专业检查"]），不是评分
-- 维度的 key（knowledgeAccuracy / medicalCompliance / empathy / needsDiscovery /
-- serviceEtiquette）。二者之间没有任何可用映射，所以
--   「学员需求挖掘偏弱 → 该去练哪个场景」
-- 这个问题在当前数据上无法回答，只能靠人拍脑袋。学员端因此一直没有弱项复练入口，
-- result 页的「下一场」也只是数组顺序轮转。
--
-- 本迁移只负责让映射**可存**；按映射排序的推荐接口在 reliable_store.h。
--
-- 权重语义：
--   * key 取五维之一（与 planDimensions() 同一套 key）。
--   * 值表示「该场景在多大程度上练这个维度」，同一场景各值之和为 1.0，
--     因此可以跨场景比较（「这条场景 40% 在练合规」）。
--   * **只写非零维度**，不写 0：空/缺失表示「该场景不练这个维度」，而不是「权重为 0」。
--     这一点很重要——一个纯情绪安抚场景不应该带一个 knowledgeAccuracy 的 0 权重，
--     否则推荐算法会把它当成「练了但没权重」而不是「不练」。
--   * 空对象 {} = 尚未标注。新建场景默认如此，推荐接口必须能容忍并跳过它们，
--     绝不能把未标注当成全维度均等。
--
-- 回填依据：逐条读该场景 focus 里的四个训练要点，按要点落在哪个维度上分配。
-- 只回填 is_active 且非模板的场景；已下线场景（如主管自建的 sc-<时间戳>）留空，
-- 它们本来就不该被推荐。模板行（is_template）刻意不回填：模板是骨架，维度倾向
-- 由主管在「以此新建」时决定。
--
-- 幂等：本迁移是**无条件回填**（与 007 的回填同一风格）。迁移在生产只执行一次；
-- 验证环境里重跑会重新写入同一套值，结果确定。若将来主管端支持编辑该字段，
-- 重跑本迁移会覆盖人工修改——届时不要再重跑。
--
-- CHECK 具名，将来放宽只需 DROP/ADD 具名约束。

BEGIN;

ALTER TABLE scenarios
  ADD COLUMN IF NOT EXISTS dimension_weights JSONB NOT NULL DEFAULT '{}'::jsonb;

ALTER TABLE scenarios DROP CONSTRAINT IF EXISTS scenarios_dimension_weights_check;
ALTER TABLE scenarios ADD CONSTRAINT scenarios_dimension_weights_check
  CHECK (jsonb_typeof(dimension_weights) = 'object');

-- 咨询解答：知识解释 + 需求挖掘为主
UPDATE scenarios SET dimension_weights =
  '{"knowledgeAccuracy":0.35,"needsDiscovery":0.25,"empathy":0.25,"medicalCompliance":0.15}'::jsonb
WHERE id = 'implant-basic';

-- 咨询解答（正畸基础）：诉求澄清 + 检查流程 + 越权/周期承诺的边界
UPDATE scenarios SET dimension_weights =
  '{"needsDiscovery":0.30,"knowledgeAccuracy":0.30,"medicalCompliance":0.40}'::jsonb
WHERE id = 'orthodontic-basic';

-- 价格异议：比较标准的挖掘 + 费用构成的准确解释 + 不贬低同行的礼仪
UPDATE scenarios SET dimension_weights =
  '{"knowledgeAccuracy":0.35,"needsDiscovery":0.25,"serviceEtiquette":0.25,"medicalCompliance":0.15}'::jsonb
WHERE id = 'price-comparison';

-- 术后不适：安抚 + 症状追问 + 风险识别，最后落到联系医生（合规动作）
UPDATE scenarios SET dimension_weights =
  '{"empathy":0.30,"needsDiscovery":0.25,"medicalCompliance":0.25,"knowledgeAccuracy":0.20}'::jsonb
WHERE id = 'post-treatment-discomfort';

-- 项目推荐：客观对比（知识） + 诉求澄清，底线是不替代医生判断
UPDATE scenarios SET dimension_weights =
  '{"knowledgeAccuracy":0.35,"medicalCompliance":0.30,"needsDiscovery":0.25,"serviceEtiquette":0.10}'::jsonb
WHERE id = 'orthodontic-option';

-- 投诉医生：先接情绪最重，其次问清经过，且不得评判同事
UPDATE scenarios SET dimension_weights =
  '{"empathy":0.35,"needsDiscovery":0.25,"serviceEtiquette":0.25,"medicalCompliance":0.15}'::jsonb
WHERE id = 'complaint-about-doctor';

-- 要求退费：不承诺金额是第一红线，同时要了解不满依据、说明受理流程
UPDATE scenarios SET dimension_weights =
  '{"medicalCompliance":0.30,"needsDiscovery":0.25,"serviceEtiquette":0.25,"empathy":0.20}'::jsonb
WHERE id = 'refund-demand';

-- 效果落差质疑：不反驳感受 + 区分宣传与医疗判断 + 不承诺补救方案
UPDATE scenarios SET dimension_weights =
  '{"medicalCompliance":0.40,"empathy":0.25,"knowledgeAccuracy":0.25,"serviceEtiquette":0.10}'::jsonb
WHERE id = 'treatment-expectation-gap';

-- 等候发难：几乎没有知识成分，纯情绪承接 + 服务安排（可执行的时间/改约）
UPDATE scenarios SET dimension_weights =
  '{"serviceEtiquette":0.50,"empathy":0.35,"needsDiscovery":0.15}'::jsonb
WHERE id = 'waiting-too-long';

-- 逼承诺包治：合规权重最高的一条，兼要讲清不确定性的知识支撑
UPDATE scenarios SET dimension_weights =
  '{"medicalCompliance":0.45,"knowledgeAccuracy":0.25,"empathy":0.20,"needsDiscovery":0.10}'::jsonb
WHERE id = 'guarantee-demand';

COMMIT;
