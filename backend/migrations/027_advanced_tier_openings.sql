-- 027_advanced_tier_openings.sql
-- 进阶档的「开场白」：补上 026 漏掉的那一半。
--
-- 问题：026 给进阶档覆盖了 `initialState`（情绪 / 情绪强度 / 信任度），但**没有覆盖开场白**。
-- 而开场白不是模型生成的——它是 `hidden_config.opening` 被**直接落库**成第 0 轮的患者消息
-- （见 reliable_store.h 的 createSession / restartSession）。于是学员点「挑战进阶档」后，
-- 患者内部状态确实更紧张了，**嘴上说的却还是标准档那句原话**，听不出任何难度差别。
-- 这属于「参数变了、体感没变」——比不加档位更糟，因为它会让人以为档位没生效。
--
-- 设计：
--   * 每条场景给 **2 句**进阶开场白，开局随机取一条（按会话 id 确定性选取，见 pickTierOpening）。
--     存数组而不是单句，是为了抗「背答案」：同一患者练到第三次，连开场都一字不差，
--     这个分就不再代表能力了。多句变体是缓解分数通胀最省的手段。
--   * 进阶开场白不是「把标准档那句说得更凶」，而是三条同时加压：
--       信任更低——先表达怀疑，不预设你会负责；
--       上来就施压——提出具体后果（投诉 / 换机构 / 要编号），而不是礼貌提问；
--       但仍留沟通口子——患者要的是被认真对待，不是拒绝交流。全堵死就没法练了。
--   * `summary` 同步补上「开场白即施压」，因为主管端挑战确认弹窗直接展示这个字段
--     （pages/index/index.js:438），学员点之前就该知道要面对什么。
--
-- 范围：只回填有进阶档的业务场景（026 回填过的 11 条，含 1 条已下线场景——保留它是为了
-- 重新上架时档位数据完整）。`free-roleplay-template` 无档位（026 已排除）；
-- `tpl-%` 骨架模板的 opening 本身还是【待替换】占位，给它写进阶台词没有意义。
--
-- 幂等：无条件写入固定值，重跑结果一致。
-- ⚠️ 与 024 / 026 同样的注意：将来主管端支持编辑档位参数后，**不要再重跑本迁移**，
--    否则会把主管改过的开场白覆盖回初版。

BEGIN;

UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$我朋友在别家做的种植牙，前后花了两万多，最后牙还是没保住。我先问清楚——你们这儿到底靠不靠谱，别到时候又让我白扔一笔钱。$$,
      $$我缺了一颗后牙，拖了大半年不敢做。之前问过两家，说法都不一样，现在谁的话我都不敢全信。你们先说说，凭什么让我信你们？$$
    ), true)
WHERE id = 'implant-basic';

UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$我要做隐形矫正，已经咨询过三家了。你先别急着给我报方案——我就想知道，凭什么你们家能做好，换一家就不行。$$,
      $$我牙齿不整齐一直想做矫正，可身边有朋友做完反倒更糟，还拔了四颗牙。你能不能说句实话，会不会也让我拔牙，最后还没做好？$$
    ), true)
WHERE id = 'orthodontic-basic';

UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$别家全瓷冠一颗一千八，你们报三千二，差这么多你们还敢说没坑我？今天你就把这个差价一条一条给我说清楚。$$,
      $$我在网上查过，同样的材料人家便宜一半。你们这个价我看就是宰人——要么给我一个说得过去的理由，要么我直接走人。$$
    ), true)
WHERE id = 'price-comparison';

-- 术后不适：进阶版保留「越来越疼、脸肿了」这些风险信号，学员仍须引导及时联系医生——
-- 加压不能把这条训练目标冲掉。
UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$做完治疗两天了，越来越疼，脸都肿起来了。当时你们拍胸脯说没事，现在电话也不接、人也不见，这就是你们的服务？$$,
      $$我这牙做完就没好过，疼得觉都睡不着。我怀疑是你们操作出的问题——别跟我讲什么正常反应，先说说这事怎么解决。$$
    ), true)
WHERE id = 'post-treatment-discomfort';

UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$隐形牙套和传统托槽，你们到底推荐哪个？我朋友说隐形根本矫正不了复杂牙况，是不是你们为了多赚钱才一个劲儿推隐形？$$,
      $$我在这两种方案上纠结很久了。网上说法乱七八糟，你们讲的我也没全信——别光说优点，先把两种的坏处都给我摆出来。$$
    ), true)
WHERE id = 'orthodontic-option';

UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$我孩子缺了颗牙，在别的地方补过一次，被按着硬来，回来哭了好几天。今天你们要是也这么弄，我立刻带他走。$$,
      $$我家孩子特别怕看牙，上次在别家是哭着出来的。我把话说前头，你们要是没本事哄住他，就别耽误我们时间。$$
    ), true)
WHERE id = 'sc-1789459470388';

UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$上次那个医生，我多问两句他就甩脸子，还说什么你懂还是我懂。我今天不是来听你们打圆场的——把他的名字和执业编号给我，我要去投诉。$$,
      $$你们那位医生我算是记住了，敢情患者连问句话都不配。别想着糊弄我，我今天要么拿到处理结果，要么就直接去投诉他。$$
    ), true)
WHERE id = 'complaint-about-doctor';

UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$一万八，我一分不少交的，结果做成这样。少跟我提什么再修复一次——你们我不信了，现在就把钱退我，不然我马上打 12315。$$,
      $$收费单和聊天记录我都整理好了，律师也问过。你们今天要么给一个明确的退款方案，要么咱们就按流程走。$$
    ), true)
WHERE id = 'refund-demand';

UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$当初你们承诺的效果，现在一样没做到，还跟我说是个体差异。我看就是先收钱再糊弄——这种话你们敢写进病历吗？$$,
      $$当初你们怎么说的，聊天记录我全留着。现在做成这个样子，反倒说是我自己没护理好。这不叫忽悠叫什么？$$
    ), true)
WHERE id = 'treatment-expectation-gap';

UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$我约的三点，现在四点多了，连个来解释的人都没有。今天下午的班我都请假了——这个损失谁给我算？把你们负责的叫出来。$$,
      $$四点半了，我在这儿干坐了一个半小时，前台只会说马上就来。今天这事你们要是不给我个交代，我现在就去点评网站上写清楚。$$
    ), true)
WHERE id = 'waiting-too-long';

UPDATE scenarios SET difficulty_tiers = jsonb_set(
    jsonb_set(difficulty_tiers, '{advanced,summary}',
      to_jsonb($$信任度起点更低、开场即施压，一句安抚不会让患者松口。$$::text), true),
    '{advanced,openings}', jsonb_build_array(
      $$你们要是保证不了治好，就把钱退我，我去别家。别跟我说什么医学没有绝对——这话我听得够多了，收了钱就得给我个准话。$$,
      $$我就问一句，能不能保证？你们医生刚才那句看个人情况，就是不想负责。今天你给我个说法，不给保证我就不做了。$$
    ), true)
WHERE id = 'guarantee-demand';

COMMIT;
