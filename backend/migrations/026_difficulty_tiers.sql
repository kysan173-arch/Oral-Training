-- 026_difficulty_tiers.sql
-- 难度档位：把「难度」从场景的静态标签，变成学员可显式选择的档位。
--
-- 问题：`scenarios.difficulty` 是场景的静态标签（basic / advanced），它把两件不同的事
-- 混在一起——「这条场景本身是什么情境」与「这次练到什么强度」。同一个「要求退费赔偿」，
-- 新手做是高压，老手做是日常。写死在场景上，复练时就无法升压。
--
-- 比「难度不够」更该担心的其实是**分数通胀**：同一患者练三次，学员把隐藏顾虑背下来了，
-- 这个分已经不代表能力。升档是让分数重新有信息量的手段之一。
--
-- 设计（与自动升级的差别，这不是风格问题）：
--   * 档位**显式摆在场景卡上**，由学员自己点。系统不偷偷调难度——否则分数会失去可解释性
--     （这轮 70 分，是我退步了还是患者更凶了？），失败也会被学员归因到「我是不是变差了」。
--   * 学员额外挑战进阶档**不该反噬他**：进阶档不计入计划达标判定（见 planProgressJoin），
--     只进成长趋势。否则「勇于挑战」会变成「拖累主管给的计划」。
--
-- 结构：`scenarios.difficulty_tiers` 只写**非默认档**。缺 `advanced` 键 = 这条场景没有进阶档
-- （前端不展示档位选择）。`standard` 就是场景的 `hidden_config.initialState`，不重复存一份，
-- 否则两个地方都能定义「默认难度」，迟早不一致。
--
-- 会话侧：`sessions.difficulty_tier` 记录本次实际所用档位。没有它就无法分档统计，
-- 混档趋势线会把「主动挑战更难档」读成「断崖式下滑」。存量全为 'standard'，语义与迁移前一致。
--
-- 幂等：ADD COLUMN IF NOT EXISTS + DROP/ADD 具名约束；回填是无条件写入（同 007 / 024 风格），
-- 迁移在生产只执行一次。若将来主管端支持编辑档位参数，**不要再重跑本迁移**。

BEGIN;

ALTER TABLE scenarios
  ADD COLUMN IF NOT EXISTS difficulty_tiers JSONB NOT NULL DEFAULT '{}'::jsonb;

ALTER TABLE scenarios DROP CONSTRAINT IF EXISTS scenarios_difficulty_tiers_check;
ALTER TABLE scenarios ADD CONSTRAINT scenarios_difficulty_tiers_check
  CHECK (jsonb_typeof(difficulty_tiers) = 'object');

ALTER TABLE sessions
  ADD COLUMN IF NOT EXISTS difficulty_tier TEXT NOT NULL DEFAULT 'standard';

ALTER TABLE sessions DROP CONSTRAINT IF EXISTS sessions_difficulty_tier_check;
ALTER TABLE sessions ADD CONSTRAINT sessions_difficulty_tier_check
  CHECK (difficulty_tier IN ('standard', 'advanced'));

-- 回填：给每条可训练场景生成一个进阶档。
-- 三条一起加压才算「进阶」——只降信任度的话，学员一句道歉就回到原状，档位形同虚设：
--   情绪档升一级：平静 / 犹豫 / 缓和 → 不满；已是不满 / 愤怒 → 愤怒
--   情绪强度 -1（下限 -2）：开场对抗更强
--   信任度 -15（下限 5）：一句安抚不会让患者松口
--
-- 排除 free-roleplay-template：它是自由模拟的系统载体，没有固定的患者剧本，档位对它无意义。
-- 主管自建的新场景也**不会**被本迁移回填（迁移只跑一次）——它们的进阶档要靠主管端将来
-- 提供的档位编辑入口补，这是已知缺口。
UPDATE scenarios SET difficulty_tiers = jsonb_build_object(
  'advanced', jsonb_build_object(
    'name', '进阶档',
    'summary', '信任度起点更低、开场情绪更强，一句安抚不会让患者松口',
    'initialState', jsonb_build_object(
      'emotion', CASE
        WHEN COALESCE(hidden_config->'initialState'->>'emotion', '平静') IN ('平静', '犹豫', '缓和')
          THEN '不满'
        ELSE '愤怒'
      END,
      'emotionLevel', GREATEST(-2, COALESCE((hidden_config->'initialState'->>'emotionLevel')::int, 0) - 1),
      'trustLevel', GREATEST(5, COALESCE((hidden_config->'initialState'->>'trustLevel')::int, 50) - 15)
    )
  )
)
WHERE id <> 'free-roleplay-template';

COMMIT;
