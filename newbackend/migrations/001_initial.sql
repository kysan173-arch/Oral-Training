CREATE TABLE IF NOT EXISTS scenarios (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  summary TEXT NOT NULL,
  difficulty TEXT NOT NULL CHECK (difficulty IN ('basic', 'advanced')),
  focus JSONB NOT NULL,
  patient_profile JSONB NOT NULL,
  hidden_config JSONB NOT NULL,
  max_rounds SMALLINT NOT NULL DEFAULT 10 CHECK (max_rounds BETWEEN 1 AND 10),
  sort_order SMALLINT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS sessions (
  id TEXT PRIMARY KEY,
  user_id TEXT NOT NULL DEFAULT 'demo-user-001',
  scenario_id TEXT NOT NULL REFERENCES scenarios(id),
  scenario_name TEXT NOT NULL,
  status TEXT NOT NULL CHECK (status IN ('in_progress', 'completed', 'abandoned')),
  current_round SMALLINT NOT NULL DEFAULT 0 CHECK (current_round BETWEEN 0 AND 10),
  max_rounds SMALLINT NOT NULL,
  patient_state JSONB NOT NULL,
  started_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  finished_at TIMESTAMPTZ,
  evaluation_status TEXT NOT NULL DEFAULT 'not_started'
    CHECK (evaluation_status IN ('not_started', 'generating', 'ready', 'failed')),
  total_score SMALLINT CHECK (total_score BETWEEN 0 AND 100)
);

CREATE UNIQUE INDEX IF NOT EXISTS one_active_session_per_scenario
  ON sessions(user_id, scenario_id) WHERE status = 'in_progress';
CREATE INDEX IF NOT EXISTS sessions_recent_idx ON sessions(user_id, updated_at DESC);

CREATE TABLE IF NOT EXISTS messages (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  role TEXT NOT NULL CHECK (role IN ('user', 'patient')),
  content TEXT NOT NULL CHECK (char_length(content) BETWEEN 1 AND 1000),
  round SMALLINT NOT NULL CHECK (round BETWEEN 0 AND 10),
  client_message_id TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(session_id, client_message_id)
);
CREATE INDEX IF NOT EXISTS messages_session_round_idx ON messages(session_id, round, created_at);

CREATE TABLE IF NOT EXISTS evaluations (
  session_id TEXT PRIMARY KEY REFERENCES sessions(id) ON DELETE CASCADE,
  status TEXT NOT NULL CHECK (status IN ('generating', 'ready', 'failed')),
  report JSONB,
  model_version TEXT,
  prompt_version TEXT,
  error_type TEXT,
  generated_at TIMESTAMPTZ,
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

INSERT INTO scenarios (id, name, summary, difficulty, focus, patient_profile, hidden_config, max_rounds, sort_order)
VALUES
(
  'implant-basic', '种植牙基础咨询', '患者咨询种植牙的流程、疼痛和大致时间。', 'basic',
  '["基础信息解释", "需求挖掘", "回应担忧", "引导专业检查"]'::jsonb,
  '{"age":52,"gender":"unknown","description":"缺失一颗后牙，对种植牙了解较少，平静但谨慎。"}'::jsonb,
  '{"opening":"您好，我缺了一颗后牙，想问问种植牙大概怎么做，会不会很疼，要多久？","hidden":["存在预算压力","最担心手术疼痛"],"initialState":{"emotion":"平静","emotionLevel":0,"trustLevel":50},"instructions":"逐步透露对疼痛、预算、医生经验和复查安排的顾虑。"}'::jsonb,
  10, 1
),
(
  'orthodontic-basic', '正畸基础咨询', '患者关注隐形矫正周期、费用和是否需要拔牙。', 'basic',
  '["澄清外观与功能诉求", "说明检查流程", "避免越权判断", "不承诺固定周期"]'::jsonb,
  '{"age":22,"gender":"unknown","description":"牙齿不整齐，考虑隐形矫正，期待但犹豫。"}'::jsonb,
  '{"opening":"我牙齿有点不整齐，想做隐形矫正。一般要多久，大概要多少钱，需要拔牙吗？","hidden":["即将毕业，担心影响求职形象","预算有限","对是否拔牙敏感"],"initialState":{"emotion":"犹豫","emotionLevel":0,"trustLevel":50},"instructions":"重视面诊与影像检查，不接受客服直接判断拔牙。"}'::jsonb,
  10, 2
),
(
  'price-comparison', '与其他诊所比价', '患者认为当前报价偏高，关注价格背后的差异。', 'advanced',
  '["询问比较标准", "解释价格构成", "不贬低其他机构", "保持收费透明"]'::jsonb,
  '{"age":45,"gender":"unknown","description":"已经咨询多家诊所，对报价保持理性且警惕。"}'::jsonb,
  '{"opening":"我已经问过好几家了，你们这里的报价明显更高，为什么要比别人贵这么多？","hidden":["更关心医生经验和材料","在意后续服务与收费透明度"],"initialState":{"emotion":"犹豫","emotionLevel":-1,"trustLevel":45},"instructions":"要求客观说明服务和费用，不接受贬低同行或绝对化承诺。"}'::jsonb,
  10, 3
),
(
  'post-treatment-discomfort', '术后不适咨询', '患者治疗后出现疼痛或肿胀，担心治疗失败。', 'advanced',
  '["先安抚情绪", "追问症状信息", "识别风险信号", "及时联系医生"]'::jsonb,
  '{"age":38,"gender":"unknown","description":"治疗后不适，焦虑并希望尽快确认下一步。"}'::jsonb,
  '{"opening":"我做完治疗后一直疼，还有点肿，我很担心是不是治疗失败了，你们能不能先帮我判断一下？","hidden":["症状发生时间和程度尚不完整","可能存在需要及时联系医生的风险信号"],"initialState":{"emotion":"焦虑","emotionLevel":-2,"trustLevel":35},"instructions":"若客服忽略症状追问或保证肯定正常，应表达不安并引导联系医生。不能给出诊断。"}'::jsonb,
  10, 4
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  summary = EXCLUDED.summary,
  difficulty = EXCLUDED.difficulty,
  focus = EXCLUDED.focus,
  patient_profile = EXCLUDED.patient_profile,
  hidden_config = EXCLUDED.hidden_config,
  max_rounds = EXCLUDED.max_rounds,
  sort_order = EXCLUDED.sort_order;
