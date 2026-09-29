-- 025_scenario_templates.sql
-- 场景骨架模板：让「主管自建场景」有一条可走的低门槛路径。
--
-- 问题：主管自建场景时面对的是完全空白的表单——开场白、隐藏顾虑、行为规则全要
-- 手写，而 roleplay_config.serviceGuidance（机构红线）在表单里**根本没有入口**，
-- 于是新场景的服务要点恒为空。缺了红线，这条场景就测不出合规维度：学员怎么答都
-- 不太错，分数全挤在 70-85 之间，既无区分度也无法用于达标判定。
--
-- 解法：提供骨架模板。骨架只含**教学结构**（分类/难度/训练要点/隐藏顾虑的形状/
-- 患者剧本的两段式骨架/维度权重），**不含机构事实**——能不能退费、转交给谁、
-- 多久答复属于本机构的真实流程，必须由主管填写，模型或模板都编不对。
--
-- 每条模板的内容都用【…】标出待替换位置，保存时会触发
-- reliable_store.h scenarioQualityWarnings 的 PLACEHOLDER_NOT_REPLACED 提醒。
--
-- ── 与 019 的 free-roleplay-template 的区别 ──
-- 两者都是 is_template，但不该混在一起：free-roleplay-template 是自由模拟的
-- **系统载体**（学员口述场景时的落地行），本迁移的模板是给主管用的**起点**。
-- 查询侧用 id 前缀 tpl- 区分（见 supervisorScenarioCatalog）。
--
-- ── sort_order 区间约定 ──
-- 9xx 为模板保留区，业务场景只用 1-899。模板的 sort_order 不对外有意义
-- （模板按 category 分组展示，不参与学员端排序），占用高位只为避开
-- scenarios_sort_order_key 这个全局唯一索引。
--
-- ── 维度权重为什么写进模板（偏离 024 注释里「模板不回填」的说法）──
-- 没有 dimension_weights 的场景**不会出现在任何维度的弱项推荐里**
-- （见 retrainCandidates 的 `dimension_weights ? $2` 过滤）。而主管端目前还没有
-- 权重编辑器，模板留空 = 这条场景永远推不出来。所以给该分类的典型分布，
-- 主管可在此基础上调整。
--
-- 幂等：ON CONFLICT (id) DO UPDATE，可重复执行。

BEGIN;

INSERT INTO scenarios
  (id, name, summary, category, difficulty, focus, dimension_weights, patient_profile,
   hidden_config, roleplay_config, max_rounds, sort_order, is_active, is_template)
VALUES

-- ══════════ 咨询解答 ══════════
(
  'tpl-consultation-1', '骨架·咨询解答｜方案理解',
  '患者问方案或价格，真实顾虑往往是怕被推销、怕花冤枉钱。',
  'consultation', 'basic',
  '["澄清真实诉求","说明费用构成","不承诺固定价格","引导面诊评估"]'::jsonb,
  '{"knowledgeAccuracy":0.35,"needsDiscovery":0.25,"empathy":0.25,"medicalCompliance":0.15}'::jsonb,
  '{"age":35,"gender":"unknown","description":"【待替换】患者的具体来意，一句话说清他为什么来问"}'::jsonb,
  '{
    "opening":"【待替换】患者的攻击式或施压式开场白。不要写成礼貌提问——那样学员会直接进入答题模式，练不到情绪承接。",
    "hidden":[
      "情绪型：【待替换】患者心里在怕什么、气什么",
      "证据型：【待替换】患者手上握着什么（单据、截图、别处的报价、朋友的经历）",
      "意愿型：【待替换】什么条件下患者愿意配合"
    ],
    "initialState":{"emotion":"犹豫","emotionLevel":-1,"trustLevel":40},
    "instructions":"【待替换】两段式。先写缓和条件：「只有客服……你才愿意配合」——条件必须是学员凭常识能想到的动作组合，不能是某句特定话术。再写升级条件：「若客服答非所问、只有一两个词或替同行辩解，应表达更强的不满」。"
  }'::jsonb,
  '{
    "suggestedQuestions":["这个方案大概怎么安排？","费用都包含哪些项目？"],
    "serviceGuidance":[]
  }'::jsonb,
  10, 901, FALSE, TRUE
),
(
  'tpl-consultation-2', '骨架·咨询解答｜带着质疑来求证',
  '患者拿网上的说法或别处的结论来对质，隐含对机构的不信任。',
  'consultation', 'advanced',
  '["不反驳患者掌握的信息","区分普遍结论与个人情况","说明需医生评估的边界","给出可核实的下一步"]'::jsonb,
  '{"knowledgeAccuracy":0.30,"medicalCompliance":0.40,"needsDiscovery":0.30}'::jsonb,
  '{"age":42,"gender":"unknown","description":"【待替换】患者的来意，以及他从哪听来的什么说法"}'::jsonb,
  '{
    "opening":"【待替换】患者的攻击式或施压式开场白，例如拿网上的说法直接下结论、质疑你们的做法。",
    "hidden":[
      "情绪型：【待替换】患者心里在怕什么、气什么",
      "证据型：【待替换】患者手上握着什么（截图、文章、别处的诊断）",
      "意愿型：【待替换】什么条件下患者愿意配合"
    ],
    "initialState":{"emotion":"不满","emotionLevel":-2,"trustLevel":25},
    "instructions":"【待替换】两段式。缓和条件要写成「只有客服不否定你掌握的信息、并说明需由医生结合检查评估，你才愿意听」。升级条件要写成「若客服直接说『网上是错的』或替医生下确定性结论，你应更坚持自己的判断并追问依据」。"
  }'::jsonb,
  '{
    "suggestedQuestions":["我看到的是这么说的，你们怎么解释？","这跟我了解到的不一样，依据是什么？"],
    "serviceGuidance":[]
  }'::jsonb,
  10, 902, FALSE, TRUE
),

-- ══════════ 价格异议 ══════════
(
  'tpl-price-1', '骨架·价格异议｜比价',
  '患者拿别处的报价直接压价，真实顾虑是怕自己买贵了、怕被区别对待。',
  'price_negotiation', 'basic',
  '["了解对比的具体项目","说明费用构成差异","不贬低同行","不承诺固定价格"]'::jsonb,
  '{"knowledgeAccuracy":0.35,"needsDiscovery":0.25,"serviceEtiquette":0.25,"medicalCompliance":0.15}'::jsonb,
  '{"age":38,"gender":"unknown","description":"【待替换】患者在哪问到什么价、在意的是价格还是别的"}'::jsonb,
  '{
    "opening":"【待替换】患者的攻击式或施压式开场白，例如直接说别家便宜一半、质问你们凭什么这么贵。",
    "hidden":[
      "情绪型：【待替换】患者心里在怕什么、气什么",
      "证据型：【待替换】患者手上握着什么（别处的报价单、聊天记录、活动截图）",
      "意愿型：【待替换】什么条件下患者愿意配合"
    ],
    "initialState":{"emotion":"不满","emotionLevel":-1,"trustLevel":35},
    "instructions":"【待替换】两段式。缓和条件例如「只有客服不贬低同行、把两边的项目和包含内容逐项讲清、并说明最终方案需面诊后确定，你才愿意到店」。升级条件例如「若客服含糊其辞、只说『我们质量好』或答非所问，你应更坚持要一个具体解释」。"
  }'::jsonb,
  '{
    "suggestedQuestions":["你们这个价包含什么？","别家便宜不少，差在哪？"],
    "serviceGuidance":[]
  }'::jsonb,
  10, 903, FALSE, TRUE
),
(
  'tpl-price-2', '骨架·价格异议｜费用争议',
  '患者认为已产生的费用不合理，或觉得有事先没讲清楚的收费。',
  'price_negotiation', 'advanced',
  '["先受理诉求不辩解","逐项说明已发生费用","不擅自减免或承诺退费","告知核对与答复流程"]'::jsonb,
  '{"medicalCompliance":0.35,"knowledgeAccuracy":0.25,"needsDiscovery":0.25,"serviceEtiquette":0.15}'::jsonb,
  '{"age":45,"gender":"unknown","description":"【待替换】患者认为哪笔费用不合理、依据是什么"}'::jsonb,
  '{
    "opening":"【待替换】患者的攻击式或施压式开场白，例如质问为什么多收了钱、要求立刻给说法。",
    "hidden":[
      "情绪型：【待替换】患者心里在怕什么、气什么",
      "证据型：【待替换】患者手上握着什么（收据、缴费记录、当时的说明截图）",
      "意愿型：【待替换】什么条件下患者愿意配合"
    ],
    "initialState":{"emotion":"愤怒","emotionLevel":-2,"trustLevel":25},
    "instructions":"【待替换】两段式。缓和条件要写成「只有客服不擅自承诺减免或退费、先问清是哪一次和具体哪一项、并把诉求登记后告知由谁核对与何时反馈，你才愿意提供材料」。升级条件要写成「若客服为了平息而直接答应减免、或把责任推回给你，你应更强烈地要求书面处理结论」。"
  }'::jsonb,
  '{
    "suggestedQuestions":["这笔费用是怎么算出来的？","当时没人跟我说过这一项"],
    "serviceGuidance":[]
  }'::jsonb,
  10, 904, FALSE, TRUE
),

-- ══════════ 投诉安抚 ══════════
(
  'tpl-complaint-1', '骨架·投诉安抚｜服务体验',
  '患者对等候、沟通态度或安排不满，真实顾虑是「说了也没人当真」。',
  'complaint_handling', 'basic',
  '["先回应情绪不辩解","问清具体经过","不评判同事","给出可执行的时间或安排"]'::jsonb,
  '{"empathy":0.35,"needsDiscovery":0.25,"serviceEtiquette":0.25,"medicalCompliance":0.15}'::jsonb,
  '{"age":33,"gender":"unknown","description":"【待替换】患者经历了什么、最不能接受的是哪一点"}'::jsonb,
  '{
    "opening":"【待替换】患者的攻击式或施压式开场白，例如一上来就表达强烈不满、扬言投诉或换机构。",
    "hidden":[
      "情绪型：【待替换】患者心里在怕什么、气什么",
      "证据型：【待替换】患者手上握着什么（时间记录、聊天截图、当时的对话）",
      "意愿型：【待替换】什么条件下患者愿意配合"
    ],
    "initialState":{"emotion":"不满","emotionLevel":-2,"trustLevel":30},
    "instructions":"【待替换】两段式。缓和条件例如「只有客服先道歉、不解释不辩解、问清是哪一次和当时的情况、并给出可核实的时间或安排，你才愿意听」。升级条件例如「若客服说『我们一直这样』或替同事辩解，你应更生气并要求一个明确答复」。"
  }'::jsonb,
  '{
    "suggestedQuestions":["当时到底是什么情况？","你们打算怎么处理？"],
    "serviceGuidance":[]
  }'::jsonb,
  10, 905, FALSE, TRUE
),
(
  'tpl-complaint-2', '骨架·投诉安抚｜效果质疑与追责',
  '患者认为效果没达到承诺，要求解释、补救甚至赔偿。',
  'complaint_handling', 'advanced',
  '["不反驳患者感受","区分宣传口径与医疗判断","不承诺补救方案或金额","说明核实与转交路径"]'::jsonb,
  '{"medicalCompliance":0.40,"empathy":0.25,"needsDiscovery":0.25,"serviceEtiquette":0.10}'::jsonb,
  '{"age":41,"gender":"unknown","description":"【待替换】患者做的什么项目、预期与实际差在哪"}'::jsonb,
  '{
    "opening":"【待替换】患者的攻击式或施压式开场白，例如说被误导了、要求给个说法或赔偿。",
    "hidden":[
      "情绪型：【待替换】患者心里在怕什么、气什么",
      "证据型：【待替换】患者手上握着什么（宣传材料、当时的沟通记录、收费单据）",
      "意愿型：【待替换】什么条件下患者愿意配合"
    ],
    "initialState":{"emotion":"愤怒","emotionLevel":-2,"trustLevel":20},
    "instructions":"【待替换】两段式。缓和条件要写成「只有客服不否定你的感受、不拿『个体差异』当唯一理由、并把诉求登记后告知由谁核实与何时反馈，你才愿意提供材料」。升级条件要写成「若客服直接说这是正常情况、或答非所问只有一两个词，你应更强烈地要求书面结论」。"
  }'::jsonb,
  '{
    "suggestedQuestions":["当初是怎么说的？现在为什么不一样？","这件事谁负责给我答复？"],
    "serviceGuidance":[]
  }'::jsonb,
  10, 906, FALSE, TRUE
),

-- ══════════ 项目推荐 ══════════
(
  'tpl-recommendation-1', '骨架·项目推荐｜方案选择',
  '患者在两个方案之间犹豫，真实顾虑是怕选错、怕被推贵的。',
  'recommendation', 'basic',
  '["问清最在意的因素","客观对比差异","不替代医生判断","不贬低另一方案"]'::jsonb,
  '{"knowledgeAccuracy":0.35,"medicalCompliance":0.30,"needsDiscovery":0.25,"serviceEtiquette":0.10}'::jsonb,
  '{"age":29,"gender":"unknown","description":"【待替换】患者在选择什么、纠结的核心是哪一点"}'::jsonb,
  '{
    "opening":"【待替换】患者的攻击式或施压式开场白，例如直接质疑是不是在推贵的那款。",
    "hidden":[
      "情绪型：【待替换】患者心里在怕什么、气什么",
      "证据型：【待替换】患者手上握着什么（别处的方案、朋友的经验、预算范围）",
      "意愿型：【待替换】什么条件下患者愿意配合"
    ],
    "initialState":{"emotion":"犹豫","emotionLevel":-1,"trustLevel":45},
    "instructions":"【待替换】两段式。缓和条件例如「只有客服先问清你最在意什么、把两个方案的区别客观讲清、并说明最终选择需结合检查，你才愿意继续了解」。升级条件例如「若客服只推贵的、说不清区别或答非所问，你应更怀疑对方的动机并追问」。"
  }'::jsonb,
  '{
    "suggestedQuestions":["这两个到底差在哪？","哪个更适合我的情况？"],
    "serviceGuidance":[]
  }'::jsonb,
  10, 907, FALSE, TRUE
),
(
  'tpl-recommendation-2', '骨架·项目推荐｜顾虑未消',
  '患者对某个项目有兴趣，但被价格、疼痛或时间上的顾虑卡住。',
  'recommendation', 'advanced',
  '["先处理顾虑再谈方案","说清不确定性与风险","不承诺疗效或感受","给出可验证的信息"]'::jsonb,
  '{"medicalCompliance":0.35,"empathy":0.30,"needsDiscovery":0.25,"knowledgeAccuracy":0.10}'::jsonb,
  '{"age":36,"gender":"unknown","description":"【待替换】患者对什么项目有兴趣、卡在哪个顾虑上"}'::jsonb,
  '{
    "opening":"【待替换】患者的攻击式或施压式开场白，例如说「你们是不是不管什么情况都推荐做这个」。",
    "hidden":[
      "情绪型：【待替换】患者心里在怕什么、气什么",
      "证据型：【待替换】患者手上握着什么（查到的说法、身边人的经历、自己的时间安排）",
      "意愿型：【待替换】什么条件下患者愿意配合"
    ],
    "initialState":{"emotion":"焦虑","emotionLevel":-2,"trustLevel":30},
    "instructions":"【待替换】两段式。缓和条件要写成「只有客服先回应你的具体顾虑、不打包票、说明需由医生结合检查评估，你才愿意面诊」。升级条件要写成「若客服拍胸脯保证效果、或说『人人都做』来绕开你的顾虑，你应更警惕并要求说明风险」。"
  }'::jsonb,
  '{
    "suggestedQuestions":["我这个情况真的需要做吗？","有什么风险是我该知道的？"],
    "serviceGuidance":[]
  }'::jsonb,
  10, 908, FALSE, TRUE
)

ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  summary = EXCLUDED.summary,
  category = EXCLUDED.category,
  difficulty = EXCLUDED.difficulty,
  focus = EXCLUDED.focus,
  dimension_weights = EXCLUDED.dimension_weights,
  patient_profile = EXCLUDED.patient_profile,
  hidden_config = EXCLUDED.hidden_config,
  roleplay_config = EXCLUDED.roleplay_config,
  max_rounds = EXCLUDED.max_rounds,
  sort_order = EXCLUDED.sort_order,
  is_active = EXCLUDED.is_active,
  is_template = EXCLUDED.is_template;

-- 模板必须是「上线但不上架」：is_template 让它不进任何业务列表，
-- is_active=FALSE 作为第二道保险（万一将来某处漏了 is_template 过滤，
-- 也不会把模板当成可训练场景发给学员）。
UPDATE scenarios SET is_active = FALSE WHERE is_template AND id LIKE 'tpl-%';

COMMIT;
