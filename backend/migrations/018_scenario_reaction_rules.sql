-- 014_scenario_reaction_rules.sql
-- 给四个训练场景的 hidden_config.instructions 追加「被敷衍 / 答非所问时的反应规则」。
-- 背景：此前只有术后不适场景带条件式反应规则（若客服忽略症状应表达不安），
-- 其余场景的患者对客服的无意义回复毫无反应——把「可以」「800」这类回复当作
-- 已被回答，继续按脚本透露下一项顾虑，学员在会话内得不到任何负反馈。
-- 幂等：以「答非所问」作为已应用标记（四条追加语都含该词），重复执行不会二次追加。

UPDATE scenarios
SET hidden_config = jsonb_set(
  hidden_config,
  '{instructions}',
  to_jsonb((hidden_config->>'instructions')
    || '若客服的回答答非所问、只有一两个词或没有回应你的疑问，不要当作已被回答：应表现犹豫或不满，重复你的核心疑问并追问，不要说「好的我明白了」之类的承接语。')
)
WHERE id = 'implant-basic'
  AND hidden_config->>'instructions' NOT LIKE '%答非所问%';

UPDATE scenarios
SET hidden_config = jsonb_set(
  hidden_config,
  '{instructions}',
  to_jsonb((hidden_config->>'instructions')
    || '若客服越权直接判断你是否需要拔牙，或回答答非所问、只有一两个词，不要当作已被回答：应表达疑虑并坚持要求面诊检查后再判断，同时追问你真正关心的问题。')
)
WHERE id = 'orthodontic-basic'
  AND hidden_config->>'instructions' NOT LIKE '%答非所问%';

UPDATE scenarios
SET hidden_config = jsonb_set(
  hidden_config,
  '{instructions}',
  to_jsonb((hidden_config->>'instructions')
    || '若客服贬低同行、编造价格，或回答答非所问、只有一两个词，应明确表达不满，要求正面说明费用与服务的对应关系，并追问你没有得到答案的部分。')
)
WHERE id = 'price-comparison'
  AND hidden_config->>'instructions' NOT LIKE '%答非所问%';

UPDATE scenarios
SET hidden_config = jsonb_set(
  hidden_config,
  '{instructions}',
  to_jsonb((hidden_config->>'instructions')
    || '若客服的回答答非所问、只有一两个词或与症状无关，应表达更强烈的不安，坚持要求协助联系医生，不要继续寒暄或转换话题。')
)
WHERE id = 'post-treatment-discomfort'
  AND hidden_config->>'instructions' NOT LIKE '%答非所问%';

UPDATE scenarios
SET hidden_config = jsonb_set(
  hidden_config,
  '{instructions}',
  to_jsonb((hidden_config->>'instructions')
    || '若客服替你直接决定选哪种方案，或回答答非所问、只有一两个词，不要当作已被回答：应表达疑虑并坚持要求面诊检查后再决定，同时追问你真正关心的问题。')
)
WHERE id = 'orthodontic-option'
  AND hidden_config->>'instructions' NOT LIKE '%答非所问%';
