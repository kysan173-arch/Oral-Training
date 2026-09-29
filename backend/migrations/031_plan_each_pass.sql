-- 031_plan_each_pass.sql
-- 「逐次达标」闸门：把判定分从「窗口内均分」换成「窗口内最低分」。
--
-- 为什么需要它：均分会掩盖尾部风险。真实数据里已经出现 75 + 52 -> 均分 63.5 的案例，
-- 按 60 分线算「达标」，但该学员第二次只有 52 分。要求「每一次都过线」才拦得住这类蒙混。
--
-- 为什么用「最低分」而不是「最后一次」：planProgressJoin 的截取规则是「取最早 N 次」，
-- 目的在于「多练只是不计入，永不倒扣」。若改看「最后一次」，学员在已达标的场景上
-- 多练一次失手就会翻回未达标 —— 等于因为多练而受罚。最低分沿用同一批行，不会倒扣。
--
-- 默认 FALSE = 与存量计划语义完全一致。这是**计划级**开关：由主管显式选择，
-- 系统不偷偷改判定口径（与 D1「不做自动升级」同一条原则）。
--
-- 幂等：ADD COLUMN IF NOT EXISTS，可重复执行。

BEGIN;

ALTER TABLE training_plans
  ADD COLUMN IF NOT EXISTS require_each_pass BOOLEAN NOT NULL DEFAULT FALSE;

COMMIT;
