#include "../src/patient_positive_floor.h"
#include <iostream>

// v1 正向演进兜底的约束测试。
//
// 为什么要独立成文件：applyPositiveFloor 曾放在 main.cpp 里，无法被单独链接测试——
// 而它恰恰是最需要测试的一段（兜底逻辑最怕悄悄失效：改了没人发现，线上也只是
//「情绪不动」，不报错不告警）。抽成头文件后，这里可以纯逻辑钉死每条约束。
//
// 分工：patient_grounding_test 守「既有契约不许破」，patient_evolution_check 守
// 「v2 演进确实发生」，本文件守「v1 兜底只在该推的时候推、且不越权」。

using namespace oral_training;

namespace {
int checks = 0;
void check(bool ok, const char* why) {
  ++checks;
  if (!ok) { std::cerr << "FAILED: " << why << "\n"; std::exit(1); }
}

// 客服话：够长、含答复信号、无敷衍特征。
const char* kGoodAnswer =
    "整个流程需要3趟：术前拍片检查、手术当天、术后3个月复诊，费用6000到12000。";
}

int main() {
  try {
    /* ── 条件 1：只有模型完全照抄时才介入 ───────────────────────── */
    // 模型自己把信任调高了 → 绝不覆盖它的决策。
    {
      const auto d = decidePositiveFloor(1, 60, "缓和", 0, 50, "平静", kGoodAnswer);
      check(!d.applied, "overrode a model that actually moved state");
      check(std::string(d.reason) == "model_moved_state", "wrong reason for model-moved");
      check(d.trustLevel == 60, "must not touch model decision");
    }
    // 模型只改了情绪词、数值没动 → 同样不介入（数值已不同）。
    {
      const auto d = decidePositiveFloor(0, 50, "不满", 0, 50, "平静", kGoodAnswer);
      check(!d.applied, "overrode model on emotion change only");
      check(d.emotion == "不满", "must keep model's negative emotion");
    }
    // 模型照抄全部三项 + 客服答得好 → 兜底介入。
    {
      const auto d = decidePositiveFloor(0, 50, "平静", 0, 50, "平静", kGoodAnswer);
      check(d.applied, "positive floor did not fire on a good answer");
      check(d.trustLevel == 55, "trust should rise by 5");
      check(d.emotionLevel == 1, "emotion level should rise by 1");
    }

    /* ── 条件 2a：太短不算应答 ─────────────────────────────────── */
    {
      const auto d = decidePositiveFloor(0, 50, "平静", 0, 50, "平静", "好的");
      check(!d.applied, "short reply must not earn trust");
      check(std::string(d.reason) == "reply_too_short", "wrong reason for short reply");
    }
    {
      const auto d = decidePositiveFloor(0, 50, "平静", 0, 50, "平静", "不知道");
      check(!d.applied, "one-word reply must not earn trust");
    }

    /* ── 条件 2b：敷衍特征不得加分 ─────────────────────────────── */
    // 这些词字面礼貌，实际是把话题推回患者。带答复信号词也不能放行。
    for (const char* deflect : {"您先说", "麻烦您说", "我不太懂", "您继续", "您说说"}) {
      const auto d = decidePositiveFloor(0, 50, "平静", 0, 50, "平静", deflect);
      check(!d.applied, "deflecting reply must not earn trust");
    }
    {
      // 混合：既含敷衍词又含信号词，仍然拒绝——敷衍是更重的信号。
      const auto d = decidePositiveFloor(0, 50, "平静", 0, 50, "平静",
                                         "这个我不太懂，您先说需要几天时间流程安排");
      check(!d.applied, "deflecting keyword must veto answer signal");
    }

    /* ── 条件 2c：没有答复信号不得加分 ─────────────────────────── */
    {
      const auto d = decidePositiveFloor(0, 50, "平静", 0, 50, "平静",
                                         "您好的，很高兴为您服务，欢迎来到诊所");
      check(!d.applied, "pleasantries are not an answer");
      check(std::string(d.reason) == "no_answer_signal", "wrong reason for no signal");
    }

    /* ── 情绪词升级的门槛：强度到顶 + 信任良好 ─────────────────── */
    {
      // 强度 1→2，但信任 60→65 未到门槛：情绪不该提前变。
      const auto d = decidePositiveFloor(1, 60, "平静", 1, 60, "平静", kGoodAnswer);
      check(d.applied && d.emotionLevel == 2, "level should reach 2");
      check(d.trustLevel == 65, "trust should rise to 65");
      check(d.emotion == "平静", "emotion must not soften before trust reaches 70");
    }
    {
      // 刚好跨过门槛（65→70）：本轮就应允许转「缓和」——门槛是含 70 的。
      const auto d = decidePositiveFloor(1, 65, "平静", 1, 65, "平静", kGoodAnswer);
      check(d.applied, "floor should apply");
      check(d.trustLevel == 70, "trust should reach 70");
      check(d.emotion == "缓和", "emotion may soften once trust hits 70");
    }
    {
      // 强度还没到 2（1→2 本轮就到 2，故这里测强度不足的存量）：
      // 信任已 85 但强度仅 0→1，情绪不该跳到「缓和」。
      const auto d = decidePositiveFloor(0, 85, "平静", 0, 85, "平静", kGoodAnswer);
      check(d.emotionLevel == 1, "level should only reach 1");
      check(d.emotion == "平静", "emotion must wait for level 2 regardless of high trust");
    }
    {
      // 「犹豫」也可以在同样条件下转「缓和」。
      const auto d = decidePositiveFloor(1, 72, "犹豫", 1, 72, "犹豫", kGoodAnswer);
      check(d.emotion == "缓和", "hesitation should be able to soften");
    }

    /* ── 边界：不得越界 ─────────────────────────────────────────── */
    {
      // 信任已满：再答也不越 100。
      const auto d = decidePositiveFloor(2, 98, "缓和", 2, 98, "缓和", kGoodAnswer);
      check(d.applied, "floor should still apply at ceiling");
      check(d.trustLevel == 100, "trust must not exceed 100");
      check(d.emotionLevel == 2, "level must not exceed 2");
    }
    {
      // 脏存量：负数强度也应被夹回合法域，而不是继续往下加。
      const auto d = decidePositiveFloor(-2, 50, "平静", -2, 50, "平静", kGoodAnswer);
      check(d.applied, "floor should clamp dirty level");
      check(d.emotionLevel == -1, "dirty emotionLevel must not propagate");
      check(d.trustLevel == 55, "trust should still rise from clamped value");
    }

    /* ── 累积演进：连续多轮的形状（这是用户要的「累积式」） ──────── */
    {
      int level = 0, trust = 50;
      std::string emotion = "平静";
      int softenedAt = -1;
      for (int round = 1; round <= 6; ++round) {
        const auto d = decidePositiveFloor(level, trust, emotion, level, trust, emotion, kGoodAnswer);
        check(d.applied, "floor must fire every round while answers stay good");
        level = d.emotionLevel; trust = d.trustLevel; emotion = d.emotion;
        if (emotion == "缓和" && softenedAt < 0) softenedAt = round;
      }
      check(trust == 80, "six good rounds should reach trust 80");
      check(level == 2, "emotion level should saturate at 2");
      check(softenedAt >= 4, "should not soften too early");
    }

    /* ── 词表一致性：与 main.cpp 的白名单保持同源 ───────────────── */
    for (const char* e : {"平静", "犹豫", "焦虑", "缓和", "不满", "愤怒"}) {
      check(knownPatientEmotion(e), "known emotion rejected");
    }
    check(!knownPatientEmotion("烦躁"), "unknown emotion accepted");

    std::cout << "patient positive floor tests passed: " << checks << " checks\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}