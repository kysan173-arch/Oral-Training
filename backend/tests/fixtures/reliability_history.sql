INSERT INTO sessions
  (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds,
   patient_state, evaluation_status)
VALUES
  ('test-duplicate', 'demo-user-001', 'implant-basic', '种植牙基础咨询',
   'completed', 1, 10, '{}'::jsonb, 'generating'),
  ('test-missing-evaluation', 'demo-user-001', 'price-comparison', '与其他诊所比价',
   'completed', 1, 10, '{}'::jsonb, 'generating'),
  ('test-unresolved-pair', 'demo-user-001', 'implant-basic', '种植牙基础咨询',
   'in_progress', 0, 10, '{}'::jsonb, 'not_started'),
  ('test-max-rounds', 'demo-user-001', 'orthodontic-basic', '正畸基础咨询',
   'in_progress', 10, 10, '{}'::jsonb, 'not_started');

INSERT INTO messages(id, session_id, role, content, round, client_message_id, created_at)
VALUES
  ('input-earliest', 'test-duplicate', 'user', '最早输入', 1, 'same-request', '2026-01-01 00:00:00+00'),
  ('input-later', 'test-duplicate', 'user', '稍后输入', 1, 'other-request', '2026-01-01 00:00:01+00'),
  ('reply-earlier', 'test-duplicate', 'patient', '较早回复', 1, NULL, '2026-01-01 00:00:02+00'),
  ('reply-latest', 'test-duplicate', 'patient', '最新回复', 1, NULL, '2026-01-01 00:00:03+00'),
  ('unresolved-input-earliest', 'test-unresolved-pair', 'user', '未回复的最早输入', 1,
   'unresolved-request-1', '2026-01-01 00:01:00+00'),
  ('unresolved-input-later', 'test-unresolved-pair', 'user', '未回复的稍后输入', 1,
   'unresolved-request-2', '2026-01-01 00:01:01+00');

INSERT INTO evaluations(session_id, status) VALUES ('test-duplicate', 'generating');

INSERT INTO roleplay_sessions
  (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds)
VALUES ('test-roleplay-duplicate', 'demo-user-001', 'price-comparison', '与其他诊所比价',
        'completed', 1, 10),
       ('test-roleplay-missing-summary', 'demo-user-001', 'implant-basic', '种植牙基础咨询',
        'completed', 1, 10);

INSERT INTO roleplay_messages
  (id, session_id, role, content, learning_points, compliance_boundary, round,
   client_message_id, created_at)
VALUES
  ('rp-input-earliest', 'test-roleplay-duplicate', 'learner_patient', '最早问题', '[]', NULL,
   1, 'rp-same-request', '2026-01-01 00:00:00+00'),
  ('rp-input-later', 'test-roleplay-duplicate', 'learner_patient', '稍后问题', '[]', NULL,
   1, 'rp-other-request', '2026-01-01 00:00:01+00'),
  ('rp-reply-earlier', 'test-roleplay-duplicate', 'standard_customer', '较早标准回复',
   '["要点一","要点二"]', '由医生评估', 1, NULL, '2026-01-01 00:00:02+00'),
  ('rp-reply-latest', 'test-roleplay-duplicate', 'standard_customer', '最新标准回复',
   '["要点一","要点二"]', '由医生评估', 1, NULL, '2026-01-01 00:00:03+00');

INSERT INTO roleplay_summaries(session_id, status)
VALUES ('test-roleplay-duplicate', 'generating');
