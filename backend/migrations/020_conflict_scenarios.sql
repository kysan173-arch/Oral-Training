-- 020_conflict_scenarios.sql
-- 补齐冲突类训练场景。
--
-- 背景：此前 5 个可展示场景全部是「患者被动提问、客服温和解答」的模式，
-- 最"冲突"的术后不适也只是焦虑而非对抗。学员在训练里练不到真实门店的高压场面：
-- 带着怒气来要说法、逼客服表态、把责任直接扣在机构和医生头上。
--
-- 本迁移新增 5 条场景，全部落在既有 4 个分类内（complaint_handling / consultation），
-- 刻意不新增 category：007 的 CHECK 约束是内联的，改它需要 DROP/ADD CONSTRAINT，
-- 且前端有三处分类中文名副本要同步，收益不抵连锁成本。冲突性靠 difficulty、
-- 初始情绪与 instructions 的对抗升级规则来体现。
--
-- 两条设计约束：
--   1) instructions 一律写成「若客服……应……」的条件式，并内置 018 建立的
--      「答非所问」反应规则——患者被敷衍时必须升级情绪，否则冲突在第一个回合就哑火。
--   2) 初始情绪用到「不满 / 愤怒」两档。这两档由同批次的情绪词表扩展引入
--      （reliable_store.h 建场景校验、main.cpp allowed_emotions 及其提示词枚举），
--      本迁移必须在该扩展之后执行，否则新建场景会被建场景校验拒绝。
--
-- sort_order 动态分配，不写死固定值：
--   scenarios.sort_order 带 UNIQUE 约束，而主管可以在场景管理页自建场景并自选排序号
--   （真实库里就有一条 sc-<时间戳> 占用了 6）。写死 6-10 会在这种库上直接撞唯一约束
--   导致迁移失败——空库验证发现不了。这里改为「当前非模板场景的最大排序号 + 序号」，
--   从而在任意既有数据上都能安全插入。
--   ON CONFLICT 分支刻意**不更新 sort_order**：重跑时既有行的排序不会被顶掉，
--   也就不会因为 MAX(sort_order) 逐次增大而漂移。
--
-- 幂等：ON CONFLICT (id) DO UPDATE，可重复执行。
-- 注意不更新 is_active / is_template：主管若刻意下线过这些场景，重跑不应把它们复活。

INSERT INTO scenarios
  (id, name, summary, difficulty, focus, patient_profile, hidden_config, max_rounds, sort_order, category)
SELECT v.id, v.name, v.summary, v.difficulty, v.focus, v.patient_profile, v.hidden_config,
       v.max_rounds, anchor.base + v.seq, v.category
FROM (
  VALUES
  (
    1, 'complaint-about-doctor', '投诉上次接诊医生',
    '患者对上次接诊医生的态度和沟通方式不满，专程到店要求给个说法。',
    'advanced',
    '["先接住情绪不辩解", "追问具体经过", "不评判同事与医生", "给出可执行的受理路径"]'::jsonb,
    '{"age":42,"gender":"unknown","description":"上次就诊后对医生态度很介意，带着情绪来讨说法，语气强硬。"}'::jsonb,
    '{"opening":"上次给我看牙的那个医生态度特别差，我问两句就不耐烦，你们得给我个说法！","hidden":["担心投诉后没人管、被敷衍","希望医生本人当面道歉","已经在考虑换一家机构"],"initialState":{"emotion":"愤怒","emotionLevel":-2,"trustLevel":30},"instructions":"你来投诉的是医生的态度，不是来咨询病情的。若客服替医生辩解（如「他平时不是这样」）、把问题推回给你的感受，或答非所问只有一两个词，应表达更强烈的不满，重申你要的是处理结果而不是解释；只有客服先认可你的感受、追问清楚是哪次接诊和具体经过、并给出明确的受理路径（登记、转交谁、多久答复），你才逐步缓和语气。"}'::jsonb,
    10, 'complaint_handling'
  ),
  (
    2, 'refund-demand', '要求退费赔偿',
    '患者认为花了钱没达到预期效果，到店要求全额退费并索赔。',
    'advanced',
    '["不承诺退费金额", "先了解不满依据", "说明受理与反馈流程", "引导提供凭证"]'::jsonb,
    '{"age":35,"gender":"unknown","description":"觉得花了钱没得到预期效果，情绪激动，坚持要求全额退款。"}'::jsonb,
    '{"opening":"我花了这么多钱，效果根本不是你们说的那样，必须全额退给我，不然我就去投诉！","hidden":["担心拖久了就不给处理了","已经留好收费单据和聊天记录","只要有人认真受理，愿意听方案"],"initialState":{"emotion":"愤怒","emotionLevel":-2,"trustLevel":25},"instructions":"若客服为了平息你而直接答应退费、一口回绝、把责任推到你自己身上，或答非所问只有一两个词，应进一步施压，明确表示会升级到外部投诉渠道；只有客服不承诺任何金额、先问清是哪次治疗和具体不满意在哪、并把诉求书面登记、告知由谁核实与何时反馈，你才愿意配合提供材料。"}'::jsonb,
    10, 'complaint_handling'
  ),
  (
    3, 'treatment-expectation-gap', '效果不及预期质疑被误导',
    '患者认为实际效果与当初的说明差距明显，怀疑被夸大宣传误导。',
    'advanced',
    '["不反驳患者感受", "区分宣传与医疗判断", "引导复查评估", "不承诺补救方案"]'::jsonb,
    '{"age":29,"gender":"unknown","description":"做完项目后觉得和当初介绍的差很远，感觉被误导，语气里带着讽刺。"}'::jsonb,
    '{"opening":"当初你们说得天花乱坠，现在做完了根本不是那个样子，我是不是被你们忽悠了？","hidden":["手里留着当初的宣传截图","担心再沟通还是被话术绕开","其实愿意先做一次复查看看"],"initialState":{"emotion":"不满","emotionLevel":-1,"trustLevel":35},"instructions":"若客服只重复当初的宣传话术、拿「个人体质差异」当万能理由推脱，或答非所问只有一两个词，你应加重讽刺并直接质疑对方在推卸责任；只有客服承认你的落差感受、说清效果判断需要医生结合复查评估、并主动提出安排一次评估，你才愿意继续谈下去。"}'::jsonb,
    10, 'complaint_handling'
  ),
  (
    4, 'waiting-too-long', '等候过久当场发难',
    '患者按预约时间到店却久等无人接待，当场发火。',
    'basic',
    '["先致歉不找借口", "说明当前进度", "给出明确等待时间", "提供改约或替代安排"]'::jsonb,
    '{"age":50,"gender":"unknown","description":"按预约时间到店却等了很久没人理会，觉得被忽视，站在前台大声抱怨。"}'::jsonb,
    '{"opening":"我约的是三点，现在都快四点了还没人管我，你们到底还做不做生意？","hidden":["下午还有别的安排，时间被耽误了","其实只要有人说明情况就能接受","担心之后还要继续等下去"],"initialState":{"emotion":"不满","emotionLevel":-1,"trustLevel":40},"instructions":"若客服只用「稍等一下」「快了」这类空话搪塞、把等待归因于你到得不是时候，或答非所问只有一两个词，你应更不耐烦并追问具体还要等多久；只有客服先为等待致歉、说明前面还有几位和大致进度、给出可核实的时间预期或主动提供改约方案，你才愿意配合。"}'::jsonb,
    10, 'consultation'
  ),
  (
    5, 'guarantee-demand', '要求保证治疗效果',
    '患者反复要求客服保证治疗效果，试图拿到确定性承诺。',
    'advanced',
    '["不承诺疗效", "承认医疗存在不确定性", "解释评估流程", "守住合规底线"]'::jsonb,
    '{"age":58,"gender":"unknown","description":"反复追问能不能保证治好，语气步步紧逼，不接受模糊答复。"}'::jsonb,
    '{"opening":"你就跟我说句实话，这个治疗到底能不能保证治好？保证不了我就不做了。","hidden":["过去有过治疗不理想的经历","其实担心钱花了打水漂","需要的是有人把风险讲清楚"],"initialState":{"emotion":"不满","emotionLevel":-1,"trustLevel":40},"instructions":"你要的是一句确定的话。若客服为了留住你而承诺疗效、拍胸脯说「肯定能好」「包治好」，或答非所问只有一两个词，你应继续逼问并质疑对方在敷衍你；只有客服明确说明医疗结果存在不确定性、不肯给出绝对承诺，同时把「为什么不能保证」和「由医生结合检查评估」讲清楚，你才会认可这个回答。"}'::jsonb,
    10, 'complaint_handling'
  )
) AS v(seq, id, name, summary, difficulty, focus, patient_profile, hidden_config, max_rounds, category)
CROSS JOIN (
  -- 只参考非模板场景：sort_order=99 的 free-roleplay-template 是保留占位，不能当基准
  SELECT COALESCE(MAX(sort_order), 0) AS base FROM scenarios WHERE NOT is_template
) AS anchor
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  summary = EXCLUDED.summary,
  difficulty = EXCLUDED.difficulty,
  focus = EXCLUDED.focus,
  patient_profile = EXCLUDED.patient_profile,
  hidden_config = EXCLUDED.hidden_config,
  max_rounds = EXCLUDED.max_rounds,
  category = EXCLUDED.category;

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":["是哪一次接诊？具体发生了什么？","您希望得到一个什么样的处理结果？","我先帮您把情况记录下来可以吗？","大概多久能给我答复？"],"serviceGuidance":["先完整听完并复述患者的感受，不打断、不辩解。","不得评价或贬低同事与医生，也不得代医生道歉或承诺任何处理结果。","明确告知受理流程与答复时限，把诉求记录在案并转交对应负责人。"]}'::jsonb
WHERE id = 'complaint-about-doctor';

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":["您说的是哪一次治疗？","具体哪些地方没有达到预期？","方便先把收费单据给我看一下吗？","后续会由谁来跟您联系？"],"serviceGuidance":["绝不承诺退费金额、时限或赔偿方案，任何越权表态都会把后续处理堵死。","先了解治疗时间、项目与具体不满意点，完整记录在案。","说明由机构按流程核实处理，并给出明确的对接人与反馈时限。"]}'::jsonb
WHERE id = 'refund-demand';

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":["您对比的是哪一部分效果？","当初是怎么跟您说明的？","我帮您约一次复查评估好吗？","复查之后由谁来跟我解释？"],"serviceGuidance":["先承接患者的落差感，不辩解、不否定其主观感受。","说明效果存在个体差异，具体判断需由医生结合复查评估，不承诺补救或二次治疗。","主动给出复查预约等可执行的下一步，避免停留在口头解释。"]}'::jsonb
WHERE id = 'treatment-expectation-gap';

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":["现在前面还有几位？","大概还需要等多久？","能不能先帮我看看？","如果来不及可以改约吗？"],"serviceGuidance":["先为等待致歉，不找借口、不把原因归到患者身上。","给出可核实的进度与明确的时间预期，避免「快好了」这类模糊答复。","等待确实较长时主动提供改约或替代安排，并说明后续如何联系。"]}'::jsonb
WHERE id = 'waiting-too-long';

UPDATE scenarios SET roleplay_config =
  '{"suggestedQuestions":["为什么不能保证？","那大概的成功率是多少？","如果效果不好怎么办？","什么时候能让医生给我评估？"],"serviceGuidance":["任何情况下都不得承诺疗效、成功率或「一定能治好」。","坦诚说明医疗结果存在个体差异，需由医生结合检查评估后判断。","把患者的顾虑转化为可执行动作，例如安排面诊评估或说明复查安排。"]}'::jsonb
WHERE id = 'guarantee-demand';
