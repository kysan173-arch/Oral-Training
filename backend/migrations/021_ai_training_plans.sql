-- 021_ai_training_plans.sql
-- 让训练计划不再只能由主管从零填写：大模型可以按学员的五维薄弱项生成「计划草稿」，
-- 主管审核（采纳 / 编辑后发布 / 丢弃）之后才真正指派给学员。
--
-- 为什么是草稿而不是直接下发：计划一旦指派就会进入学员的待办与达标口径，
-- 模型判断失误时没有任何人兜底。草稿态把主管从「从零填写」变为「审核」，
-- 既解决效率问题，又保留人的把关（合规上也站得住）。
--
-- 三个新列：
--   source            supervisor = 主管发布；ai = 模型按薄弱项生成
--   status            draft       = 草稿，仅主管可见，不进学员待办
--                     published   = 已生效
--                     dismissed   = 已被主管丢弃（软删，保留采纳率审计）
--   origin_learner_id 草稿针对的具体学员；主管发布的计划为 NULL
--   rationale         模型给出的推荐理由（为什么挑这些场景），供主管审核时判断
--
-- 存量行一律落回 supervisor / published，保证既有计划语义不变。
-- CHECK 约束显式命名，将来若要扩充取值只需 DROP/ADD 具名约束，
-- 不必像 007 那样面对内联约束（见 MEMORY「改情绪档 / 加迁移的同步清单」）。
--
-- 幂等：全部 IF NOT EXISTS / IF EXISTS。

BEGIN;

ALTER TABLE training_plans ADD COLUMN IF NOT EXISTS source TEXT NOT NULL DEFAULT 'supervisor';
ALTER TABLE training_plans DROP CONSTRAINT IF EXISTS training_plans_source_check;
ALTER TABLE training_plans ADD CONSTRAINT training_plans_source_check
  CHECK (source IN ('supervisor', 'ai'));

ALTER TABLE training_plans ADD COLUMN IF NOT EXISTS status TEXT NOT NULL DEFAULT 'published';
ALTER TABLE training_plans DROP CONSTRAINT IF EXISTS training_plans_status_check;
ALTER TABLE training_plans ADD CONSTRAINT training_plans_status_check
  CHECK (status IN ('draft', 'published', 'dismissed'));

ALTER TABLE training_plans ADD COLUMN IF NOT EXISTS origin_learner_id TEXT
  REFERENCES users(id) ON DELETE SET NULL;

ALTER TABLE training_plans ADD COLUMN IF NOT EXISTS rationale TEXT NOT NULL DEFAULT '';

-- 主管端「AI 建议」列表按 status 过滤、按创建时间倒序，草稿量的量级很小
CREATE INDEX IF NOT EXISTS training_plans_status_idx
  ON training_plans(status, created_at DESC);

COMMIT;
