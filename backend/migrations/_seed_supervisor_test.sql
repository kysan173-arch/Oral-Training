-- Seed test learners + completed sessions so the supervisor view has data.
-- Run ONLY against a local test database; do not use in production.
BEGIN;

-- 1) Learner users
INSERT INTO users(id, wechat_openid, display_name, role, status)
VALUES
  ('learner-test-001', 'seed-openid-001', 'Test 学员小明', 'learner', 'active'),
  ('learner-test-002', 'seed-openid-002', 'Test 学员小红', 'learner', 'active'),
  ('learner-test-003', 'seed-openid-003', 'Test 学员小刚', 'learner', 'active'),
  ('learner-test-004', 'seed-openid-004', 'Test 学员小美', 'learner', 'active')
ON CONFLICT (id) DO NOTHING;

-- 2) Sessions (completed + evaluation ready) for each learner
-- learner-test-001
INSERT INTO sessions(id, user_id, scenario_id, scenario_name, status, current_round, max_rounds,
  patient_state, started_at, updated_at, finished_at, evaluation_status, total_score, custom_patient_profile)
VALUES
  ('seed-sess-101', 'learner-test-001', 'implant-basic', '种植牙基础咨询', 'completed', 6, 10,
   '{"emotion":"平静","emotionLevel":0,"trustLevel":70}', NOW()-INTERVAL '6 days', NOW()-INTERVAL '6 days', NOW()-INTERVAL '6 days', 'ready', 82, '{}'),
  ('seed-sess-102', 'learner-test-001', 'price-comparison', '与其他诊所比价', 'completed', 5, 10,
   '{"emotion":"犹豫","emotionLevel":0,"trustLevel":60}', NOW()-INTERVAL '4 days', NOW()-INTERVAL '4 days', NOW()-INTERVAL '4 days', 'ready', 75, '{}'),
  ('seed-sess-103', 'learner-test-001', 'post-treatment-discomfort', '术后不适咨询', 'completed', 4, 10,
   '{"emotion":"焦虑","emotionLevel":-1,"trustLevel":55}', NOW()-INTERVAL '2 days', NOW()-INTERVAL '2 days', NOW()-INTERVAL '2 days', 'ready', 68, '{}');

-- learner-test-002
INSERT INTO sessions(id, user_id, scenario_id, scenario_name, status, current_round, max_rounds,
  patient_state, started_at, updated_at, finished_at, evaluation_status, total_score, custom_patient_profile)
VALUES
  ('seed-sess-201', 'learner-test-002', 'orthodontic-basic', '正畸基础咨询', 'completed', 7, 10,
   '{"emotion":"犹豫","emotionLevel":0,"trustLevel":65}', NOW()-INTERVAL '5 days', NOW()-INTERVAL '5 days', NOW()-INTERVAL '5 days', 'ready', 88, '{}'),
  ('seed-sess-202', 'learner-test-002', 'implant-basic', '种植牙基础咨询', 'completed', 5, 10,
   '{"emotion":"平静","emotionLevel":0,"trustLevel":72}', NOW()-INTERVAL '3 days', NOW()-INTERVAL '3 days', NOW()-INTERVAL '3 days', 'ready', 91, '{}'),
  ('seed-sess-203', 'learner-test-002', 'orthodontic-option', '矫正方案对比选择', 'completed', 6, 10,
   '{"emotion":"犹豫","emotionLevel":0,"trustLevel":66}', NOW()-INTERVAL '1 days', NOW()-INTERVAL '1 days', NOW()-INTERVAL '1 days', 'ready', 79, '{}');

-- learner-test-003
INSERT INTO sessions(id, user_id, scenario_id, scenario_name, status, current_round, max_rounds,
  patient_state, started_at, updated_at, finished_at, evaluation_status, total_score, custom_patient_profile)
VALUES
  ('seed-sess-301', 'learner-test-003', 'price-comparison', '与其他诊所比价', 'completed', 4, 10,
   '{"emotion":"犹豫","emotionLevel":0,"trustLevel":50}', NOW()-INTERVAL '7 days', NOW()-INTERVAL '7 days', NOW()-INTERVAL '7 days', 'ready', 55, '{}'),
  ('seed-sess-302', 'learner-test-003', 'post-treatment-discomfort', '术后不适咨询', 'completed', 3, 10,
   '{"emotion":"焦虑","emotionLevel":-2,"trustLevel":40}', NOW()-INTERVAL '5 days', NOW()-INTERVAL '5 days', NOW()-INTERVAL '5 days', 'ready', 48, '{}'),
  ('seed-sess-303', 'learner-test-003', 'orthodontic-basic', '正畸基础咨询', 'completed', 5, 10,
   '{"emotion":"犹豫","emotionLevel":0,"trustLevel":58}', NOW()-INTERVAL '2 days', NOW()-INTERVAL '2 days', NOW()-INTERVAL '2 days', 'ready', 62, '{}');

-- learner-test-004
INSERT INTO sessions(id, user_id, scenario_id, scenario_name, status, current_round, max_rounds,
  patient_state, started_at, updated_at, finished_at, evaluation_status, total_score, custom_patient_profile)
VALUES
  ('seed-sess-401', 'learner-test-004', 'implant-basic', '种植牙基础咨询', 'completed', 6, 10,
   '{"emotion":"平静","emotionLevel":0,"trustLevel":70}', NOW()-INTERVAL '4 days', NOW()-INTERVAL '4 days', NOW()-INTERVAL '4 days', 'ready', 85, '{}'),
  ('seed-sess-402', 'learner-test-004', 'orthodontic-basic', '正畸基础咨询', 'completed', 5, 10,
   '{"emotion":"犹豫","emotionLevel":0,"trustLevel":64}', NOW()-INTERVAL '2 days', NOW()-INTERVAL '2 days', NOW()-INTERVAL '2 days', 'ready', 73, '{}');

-- 3) A patient opening message per session (needed for conversation view)
INSERT INTO messages(id, session_id, role, content, round)
VALUES
  ('seed-msg-101', 'seed-sess-101', 'patient', '您好，我缺了一颗后牙，想问问种植牙怎么做。', 0),
  ('seed-msg-102', 'seed-sess-102', 'patient', '我咨询了多家诊所，想了解价格差异。', 0),
  ('seed-msg-103', 'seed-sess-103', 'patient', '我治疗后一直疼还有点肿，很担心。', 0),
  ('seed-msg-201', 'seed-sess-201', 'patient', '我想做隐形矫正，要多久多少钱？', 0),
  ('seed-msg-202', 'seed-sess-202', 'patient', '缺一颗后牙，种植牙会疼吗？', 0),
  ('seed-msg-203', 'seed-sess-203', 'patient', '隐形牙套和传统托槽哪个更适合我？', 0),
  ('seed-msg-301', 'seed-sess-301', 'patient', '你们报价为什么比别人贵？', 0),
  ('seed-msg-302', 'seed-sess-302', 'patient', '治疗后疼得厉害，是不是失败了？', 0),
  ('seed-msg-303', 'seed-sess-303', 'patient', '矫正需要拔牙吗，我很担心。', 0),
  ('seed-msg-401', 'seed-sess-401', 'patient', '种植牙流程是怎样的？', 0),
  ('seed-msg-402', 'seed-sess-402', 'patient', '隐形矫正周期要多久？', 0);

-- 4) Evaluations with dimensionScores
INSERT INTO evaluations(session_id, status, report, model_version, prompt_version, generated_at, updated_at)
VALUES
  ('seed-sess-101', 'ready',
   '{"summary":"整体稳定，知识讲解清晰。","dimensionScores":{"knowledgeAccuracy":88,"medicalCompliance":85,"empathy":80,"needsDiscovery":78,"serviceEtiquette":82},"totalScore":82,"strengths":[{"content":"流程讲解清楚"}],"improvements":[{"content":"可更多挖掘患者顾虑"}],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW()),
  ('seed-sess-102', 'ready',
   '{"summary":"价格说明较清楚。","dimensionScores":{"knowledgeAccuracy":76,"medicalCompliance":80,"empathy":72,"needsDiscovery":70,"serviceEtiquette":75},"totalScore":75,"strengths":[],"improvements":[],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW()),
  ('seed-sess-103', 'ready',
   '{"summary":"术后安抚可再加强。","dimensionScores":{"knowledgeAccuracy":65,"medicalCompliance":72,"empathy":62,"needsDiscovery":64,"serviceEtiquette":70},"totalScore":68,"strengths":[],"improvements":[],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW()),
  ('seed-sess-201', 'ready',
   '{"summary":"沟通流畅，边界把握好。","dimensionScores":{"knowledgeAccuracy":90,"medicalCompliance":88,"empathy":86,"needsDiscovery":84,"serviceEtiquette":89},"totalScore":88,"strengths":[],"improvements":[],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW()),
  ('seed-sess-202', 'ready',
   '{"summary":"表现优秀。","dimensionScores":{"knowledgeAccuracy":93,"medicalCompliance":92,"empathy":90,"needsDiscovery":88,"serviceEtiquette":91},"totalScore":91,"strengths":[],"improvements":[],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW()),
  ('seed-sess-203', 'ready',
   '{"summary":"方案对比说明清晰。","dimensionScores":{"knowledgeAccuracy":80,"medicalCompliance":82,"empathy":76,"needsDiscovery":75,"serviceEtiquette":78},"totalScore":79,"strengths":[],"improvements":[],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW()),
  ('seed-sess-301', 'ready',
   '{"summary":"价格沟通较生硬。","dimensionScores":{"knowledgeAccuracy":55,"medicalCompliance":60,"empathy":48,"needsDiscovery":52,"serviceEtiquette":58},"totalScore":55,"strengths":[],"improvements":[],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW()),
  ('seed-sess-302', 'ready',
   '{"summary":"术后风险处理需加强。","dimensionScores":{"knowledgeAccuracy":45,"medicalCompliance":50,"empathy":42,"needsDiscovery":46,"serviceEtiquette":52},"totalScore":48,"strengths":[],"improvements":[],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW()),
  ('seed-sess-303', 'ready',
   '{"summary":"整体达标。","dimensionScores":{"knowledgeAccuracy":62,"medicalCompliance":64,"empathy":60,"needsDiscovery":58,"serviceEtiquette":63},"totalScore":62,"strengths":[],"improvements":[],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW()),
  ('seed-sess-401', 'ready',
   '{"summary":"表现良好。","dimensionScores":{"knowledgeAccuracy":86,"medicalCompliance":87,"empathy":82,"needsDiscovery":80,"serviceEtiquette":85},"totalScore":85,"strengths":[],"improvements":[],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW()),
  ('seed-sess-402', 'ready',
   '{"summary":"需要继续练习。","dimensionScores":{"knowledgeAccuracy":74,"medicalCompliance":76,"empathy":70,"needsDiscovery":72,"serviceEtiquette":75},"totalScore":73,"strengths":[],"improvements":[],"violations":[],"roundComments":[]}',
   'seed','seed', NOW(), NOW());

COMMIT;
