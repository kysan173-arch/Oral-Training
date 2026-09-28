-- 023_plan_scenario_cap.sql
-- 计划防刷分：限制「同一场景最多计入几次完成数」。
--
-- 问题：021/022 之后主管可以不指定场景，只给「目标维度 + 分数 + 次数」。学员因此可以
-- 反复练最容易的那一个场景，把该维度的完成次数与均分一起刷上去，计划看起来已闭环，
-- 训练量却没有真正铺开——弱项被「用最舒服的方式」满足了。
--
-- 堵法有两条，本迁移选后者：
--   甲）强制主管指定场景 —— 主管要逐个挑场景，且剥夺学员自主路径，代价过大；
--   乙）给「同一场景计入的完成次数」设上限（本迁移）—— 约束轻，不改变
--       「学员自己决定练什么」这一前提；上限一到，剩余次数只能靠别的场景补齐。
--
-- max_per_scenario = 0 表示不限，与迁移前语义完全一致（存量计划全部落 0）。
-- 上限取 10，与 required_count 的最大值同档；真正生效的仍是 required_count
-- （上限 ≥ 要求次数时该列不起作用，属无意义配置，但不报错也不拦）。
--
-- 计入「哪几次」由 reliable_store.h 的 planProgressJoin 决定：**最早**的 N 次。
-- 选「最早」而不是「最近」，是因为本设计有一条硬原则——学员额外训练不该反噬他。
-- 取最近 N 次时，学员在已达标的场景上多练一次（且这次分数更低）会把原来的好成绩
-- 顶出统计，计划可能因此由达标翻回未达标；取最早 N 次则多练只是不计入，永不倒扣。
-- 代价是计划期内早期的敷衍成绩会被锁定，学员需靠其他场景补足次数——这是可接受的。
-- 若将来要改成「最近 N 次」，只改 planProgressJoin 的 ORDER BY 方向即可。
--
-- 幂等：ADD COLUMN IF NOT EXISTS + DROP/ADD 具名约束，可重复执行。
-- CHECK 具名，将来放宽上限只需 DROP/ADD 具名约束。

BEGIN;

ALTER TABLE training_plans ADD COLUMN IF NOT EXISTS max_per_scenario SMALLINT NOT NULL DEFAULT 0;

ALTER TABLE training_plans DROP CONSTRAINT IF EXISTS training_plans_max_per_scenario_check;
ALTER TABLE training_plans ADD CONSTRAINT training_plans_max_per_scenario_check
  CHECK (max_per_scenario BETWEEN 0 AND 10);

COMMIT;
