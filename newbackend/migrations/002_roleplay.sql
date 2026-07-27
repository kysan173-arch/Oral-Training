-- Independent "patient simulation" data. These tables must not feed the
-- existing customer-service training scores or dashboard aggregates.

ALTER TABLE scenarios
  ADD COLUMN IF NOT EXISTS roleplay_config JSONB NOT NULL
  DEFAULT '{"suggestedQuestions":[],"serviceGuidance":[]}'::jsonb;

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":["种植牙一般是怎样的流程？","做种植牙会不会很疼？","大概需要多久才能完成？","我现在缺一颗牙，先要做哪些检查？"],"serviceGuidance":["先确认患者主要顾虑并说明可安排面诊检查。","不得承诺疼痛程度、成功率、具体疗程或价格。","涉及方案、是否适合种植和恢复情况时，明确由医生结合检查评估。"]}'::jsonb
WHERE id = 'implant-basic';

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":["隐形矫正通常需要先做什么检查？","矫正会影响日常说话或工作吗？","我这种情况一定要拔牙吗？","大概多久能看到变化？"],"serviceGuidance":["先了解患者对外观、时间和舒适度的关注。","不能通过文字判断是否拔牙、是否适合隐形矫正或确定疗程。","说明医生需结合口内检查和影像资料制定方案，价格以检查后的正式方案为准。"]}'::jsonb
WHERE id = 'orthodontic-basic';

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":["为什么你们的报价和别家不一样？","费用里通常包含哪些服务？","材料和医生经验会有什么差别？","如果我想比较，应该重点问哪些内容？"],"serviceGuidance":["承认患者比较价格的需求，不贬低其他机构。","说明可从检查、材料、医生资质、复诊与服务范围等维度了解，但不编造本机构价格或套餐。","以透明沟通和安排面诊、正式报价为下一步。"]}'::jsonb
WHERE id = 'price-comparison';

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":["治疗后一直疼还有点肿，正常吗？","我现在需要立刻回来检查吗？","出现哪些情况需要尽快联系医生？","我很担心治疗是不是失败了。"],"serviceGuidance":["先安抚并收集症状出现时间、程度和是否加重等信息。","不得诊断、开药、保证症状正常或断言治疗结果。","提示出现明显加重或紧急不适时及时联系医生或按医疗机构指引就医，并协助安排复诊。"]}'::jsonb
WHERE id = 'post-treatment-discomfort';

CREATE TABLE IF NOT EXISTS roleplay_sessions (
  id TEXT PRIMARY KEY,
  user_id TEXT NOT NULL DEFAULT 'demo-user-001',
  scenario_id TEXT NOT NULL REFERENCES scenarios(id),
  scenario_name TEXT NOT NULL,
  status TEXT NOT NULL CHECK (status IN ('in_progress', 'completed', 'abandoned')),
  current_round SMALLINT NOT NULL DEFAULT 0 CHECK (current_round BETWEEN 0 AND 10),
  max_rounds SMALLINT NOT NULL DEFAULT 10 CHECK (max_rounds BETWEEN 1 AND 10),
  started_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  finished_at TIMESTAMPTZ
);

CREATE UNIQUE INDEX IF NOT EXISTS one_active_roleplay_session_per_scenario
  ON roleplay_sessions(user_id, scenario_id) WHERE status = 'in_progress';
CREATE INDEX IF NOT EXISTS roleplay_sessions_recent_idx
  ON roleplay_sessions(user_id, updated_at DESC);

CREATE TABLE IF NOT EXISTS roleplay_messages (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL REFERENCES roleplay_sessions(id) ON DELETE CASCADE,
  role TEXT NOT NULL CHECK (role IN ('learner_patient', 'standard_customer')),
  content TEXT NOT NULL CHECK (char_length(content) BETWEEN 1 AND 1000),
  learning_points JSONB NOT NULL DEFAULT '[]'::jsonb,
  compliance_boundary TEXT,
  round SMALLINT NOT NULL CHECK (round BETWEEN 1 AND 10),
  client_message_id TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(session_id, client_message_id)
);
CREATE INDEX IF NOT EXISTS roleplay_messages_session_round_idx
  ON roleplay_messages(session_id, round, created_at);

CREATE TABLE IF NOT EXISTS roleplay_summaries (
  session_id TEXT PRIMARY KEY REFERENCES roleplay_sessions(id) ON DELETE CASCADE,
  status TEXT NOT NULL CHECK (status IN ('generating', 'ready', 'failed')),
  summary JSONB,
  model_version TEXT,
  prompt_version TEXT,
  error_type TEXT,
  generated_at TIMESTAMPTZ,
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
