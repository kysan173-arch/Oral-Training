-- 015_roleplay_free_template.sql
-- 自由模拟（患者模拟模式）此前借用 sort_order 第一的场景当模板创建会话：
-- AI 标准客服的服务要点、复盘口径全部来自被借用场景，与学员描述的场景脱节。
-- 本迁移提供载体与开关：
--   1) scenarios.is_template：标记「自由模拟专用模板」，不进任何场景列表；
--   2) scenarios.is_active：预留下线开关（场景管理改进项），默认 true；
--   3) free-roleplay-template：通用模板场景，serviceGuidance 只写合规口径；
--   4) roleplay_sessions.free_description：学员的场景描述入库，由后端注入
--      standardServiceReply / roleplaySummary 的 prompt。
-- 各列表查询同步过滤（见 reliable_store.h）。

ALTER TABLE scenarios ADD COLUMN IF NOT EXISTS is_active BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE scenarios ADD COLUMN IF NOT EXISTS is_template BOOLEAN NOT NULL DEFAULT false;

INSERT INTO scenarios
  (id, name, summary, difficulty, focus, patient_profile, hidden_config, max_rounds, sort_order, category, is_active, is_template)
VALUES
(
  'free-roleplay-template', '自由模拟', '学员自由描述的患者场景（通用模板，不展示在场景列表中）。', 'basic',
  '["以学员描述的场景为准", "先了解患者具体情况", "说明服务边界", "引导面诊检查"]'::jsonb,
  '{}'::jsonb,
  '{"opening":"您好，我想咨询一些问题。","hidden":[],"initialState":{"emotion":"平静","emotionLevel":0,"trustLevel":50},"instructions":"以学员在会话描述中定义的场景为准自然回应。若客服的回答答非所问、只有一两个词或没有回应你的疑问，不要当作已被回答：应表达困惑或不满，并追问你真正想了解的问题。"}'::jsonb,
  10, 99, 'consultation', true, true
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  summary = EXCLUDED.summary,
  difficulty = EXCLUDED.difficulty,
  focus = EXCLUDED.focus,
  patient_profile = EXCLUDED.patient_profile,
  hidden_config = EXCLUDED.hidden_config,
  max_rounds = EXCLUDED.max_rounds,
  sort_order = EXCLUDED.sort_order,
  category = EXCLUDED.category,
  is_active = EXCLUDED.is_active,
  is_template = EXCLUDED.is_template;

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":[],"serviceGuidance":["答复必须围绕学员在会话中描述的具体场景展开，先回应患者当下最关心的问题。","不得诊断、开药、承诺疗效或疼痛安全性，不得编造价格、疗程、优惠或机构政策；涉及治疗判断时说明需由医生结合检查评估。","答复结构清晰：先安抚或回应情绪，再给出可执行的下一步（如协助预约面诊检查）。"]}'::jsonb
WHERE id = 'free-roleplay-template';

ALTER TABLE roleplay_sessions ADD COLUMN IF NOT EXISTS free_description TEXT;
