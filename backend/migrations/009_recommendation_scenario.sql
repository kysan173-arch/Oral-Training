-- Add a "recommendation" category scenario so the 项目推荐 group is not empty.

INSERT INTO scenarios (id, name, summary, difficulty, focus, patient_profile, hidden_config, max_rounds, sort_order, category)
VALUES
(
  'orthodontic-option', '矫正方案对比选择', '患者纠结隐形牙套与传统托槽，希望了解差异并判断哪个更适合自己。', 'basic',
  '["客观对比选项差异", "澄清矫正诉求", "引导专业检查", "不替代医生判断"]'::jsonb,
  '{"age":25,"gender":"unknown","description":"上班族，考虑矫正但不确定选隐形牙套还是传统托槽，理性但犹豫。"}'::jsonb,
  '{"opening":"我想做牙齿矫正，纠结隐形牙套和传统托槽哪个更适合我，能帮我对比一下吗？","hidden":["更在意美观和平时说话的影响","预算有限","担心矫正期间的饮食和生活不便"],"initialState":{"emotion":"犹豫","emotionLevel":0,"trustLevel":50},"instructions":"重视面诊与影像检查，不接受客服直接判断哪种方案更适合，要求客观说明差异并引导专业评估。"}'::jsonb,
  10, 5, 'recommendation'
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
  category = EXCLUDED.category;

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":["隐形牙套和传统托槽主要差别在哪？","我这种情况适合哪一种？","矫正期间会不会影响说话和工作？","大概多久能看到变化，需要拔牙吗？"],"serviceGuidance":["先了解患者对美观、日常影响和预算的关注，再客观说明两类方案的一般差异。","不能替患者判断或承诺哪种方案更适合，也不得保证疗程、效果或价格。","说明需由医生结合口内检查和影像资料评估，以面诊后的正式方案为准。"]}'::jsonb
WHERE id = 'orthodontic-option';
