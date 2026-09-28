-- 030_scenario_ai_draft.sql
-- AI 生成训练场景骨架：把「主管从零写一条场景」变成「挑一条草稿改」。
--
-- 为什么值得做：一条高质量场景的成本集中在两处——教学骨架（表面诉求与真实顾虑的落差、
-- 三条性质错开的隐藏顾虑、可判定的缓和条件、升级条件、施压式开场白），以及机构事实
-- （能不能退费、转交谁、多久答复）。前者是可复用的方法论，后者只属于本机构。
-- 让模型写前者、主管写后者，是唯一不会编造机构事实的分工。
--
-- ⚠️ 本迁移只加两列 + 放开一个 CHECK。**真正的分发在代码里**
--    （knowledge_store.h 的 GenerationTarget + parseGenerationTarget）：
--    只加 CHECK 挡不住「未知类型落到错误目标表」——那是 R09 风险表里明确要防的一条，
--    队列里每一处「不是 A 就是 B」的分支都必须改成显式枚举，未知类型拒绝执行。
--
-- 场景行的状态机（两个字段各管一件事，不要合并）：
--   generation_id  当前正在生成它的任务 id。非空 = 生成中（主管端显示「生成中」、
--                  禁止编辑）；成功写入后清空。同时是乐观并发闸门：只有持有该 id 的
--                  任务才能写回这条行，重试会重新武装它。
--   ai_draft       教学骨架由 AI 生成。这是**来源标记，永久保留**——它不表示
--                  「尚未审阅」：「未审阅」由 is_active = FALSE 表达，主管补齐机构红线
--                  并上架之后，ai_draft 仍然为 TRUE（这是这条场景的来历，不该被抹掉）。
--
-- 幂等：ADD COLUMN IF NOT EXISTS + DROP/ADD 具名约束，可重复执行。

BEGIN;

-- 1) 队列放开第三种任务类型。
ALTER TABLE knowledge_admin_jobs DROP CONSTRAINT IF EXISTS knowledge_admin_jobs_kind_check;
ALTER TABLE knowledge_admin_jobs ADD CONSTRAINT knowledge_admin_jobs_kind_check
  CHECK (kind IN ('service_draft', 'knowledge_draft', 'scenario_draft'));

-- 2) 场景行的 AI 草稿状态。
ALTER TABLE scenarios ADD COLUMN IF NOT EXISTS generation_id TEXT;
ALTER TABLE scenarios ADD COLUMN IF NOT EXISTS ai_draft BOOLEAN NOT NULL DEFAULT FALSE;

-- 生成中的行必须对学员不可见：is_active 在创建占位行时已写死 FALSE，
-- 这里补一条数据库级保险——存在 generation_id 的行不允许被上架，
-- 避免人工 SQL 或将来某条新代码路径把半成品直接推给学员。
ALTER TABLE scenarios DROP CONSTRAINT IF EXISTS scenarios_generating_not_active_check;
ALTER TABLE scenarios ADD CONSTRAINT scenarios_generating_not_active_check
  CHECK (generation_id IS NULL OR is_active = FALSE);

-- 占位行是极少数的少数派，用部分索引：不给「按 generation_id 找任务」引入全表开销。
CREATE INDEX IF NOT EXISTS scenarios_generation_idx
  ON scenarios(generation_id) WHERE generation_id IS NOT NULL;

COMMIT;
