-- 022_plan_focus_dimension.sql
-- 让训练计划能声明「本计划针对哪个维度」，并让达标判定真的按该维度算。
--
-- 问题：此前计划的达标判定是 AVG(sessions.total_score) >= required_pass_rate，
-- 而 total_score 是五维加权和（知识 .25 / 合规 .25 / 同理心 .20 / 需求挖掘 .20 /
-- 礼仪 .10，见 009_legacy_report_totals.sql:51）。于是出现度量错位：
-- 一个「需求挖掘 = 40、其余维度 85」的学员，加权总分 76，照样判定「达标」——
-- 弱项分毫未动，计划却已闭环。目标（补弱项）与度量（综合分）不在一条线上。
--
-- 本迁移只负责让「目标」可见可存；判定口径的切换在 reliable_store.h：
--   有 focus_dimension 且该维度存在有效评分 → 用该维度的平均分判定
--   focus_dimension 为空、或该维度无任何有效评分 → 回退综合分
--   （缺失绝不当作 0 分，否则会凭空制造「不达标」）
--
-- 存量计划一律落回 ''（不针对特定维度），语义与迁移前完全一致。
-- CHECK 具名，将来若要扩充维度只需 DROP/ADD 具名约束。

BEGIN;

ALTER TABLE training_plans ADD COLUMN IF NOT EXISTS focus_dimension TEXT NOT NULL DEFAULT '';

ALTER TABLE training_plans DROP CONSTRAINT IF EXISTS training_plans_focus_dimension_check;
ALTER TABLE training_plans ADD CONSTRAINT training_plans_focus_dimension_check
  CHECK (focus_dimension IN ('', 'knowledgeAccuracy', 'medicalCompliance', 'empathy',
                             'needsDiscovery', 'serviceEtiquette'));

COMMIT;
