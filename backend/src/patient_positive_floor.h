#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace oral_training {

/* ── v1 患者状态：正向演进兜底 ──────────────────────────────────────────
   与 v2 的 patient_grounding.h 同一类问题的不同解法：v2 整条转移都由后端规则
   负责，v1 保留模型决策权（模型比规则更懂语境），所以这里只做**兜底**。

   历史故障：真实 DeepSeek 实测，模型能可靠地执行「变坏」（敷衍→不满、信任下调），
   却对「变好」一律照抄当前值——同一局 7 轮始终「平静 55」，情绪形同虚设。
   提示词把操作写具体（"设为当前值 +5~+10"）仍不足以稳定驱动正向，
   而这一层失效是**静默**的：不报错、不告警、页面照常显示，只能靠翻数据库发现。

   因此后端在模型完全没动时兜底给最小正向增量。判定「本轮表现良好」
   **不看模型输出**（它正是不可信的一方），而看客服本轮实际说了什么——
   与 v2 的 promise/money 判定同源，都是对文本的客观规则。

   放在独立头文件而非 main.cpp：main.cpp 的自由函数无法被测试单独链接，
   而这条逻辑必须能被单测钉住——它是「兜底」，最怕的就是悄悄失效。
   本文件不 include main.cpp 的任何东西，可独立编译。 */

/* 已知词表，与 main.cpp 的 normalizePatientReply 保持一致。 */
inline bool knownPatientEmotion(const std::string& value) {
  static const std::vector<std::string> allowed{"平静","犹豫","焦虑","缓和","不满","愤怒"};
  return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

inline int utf8CharCount(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
  return static_cast<int>(n);
}

inline bool containsAny(const std::string& text, std::initializer_list<const char*> terms) {
  for (const auto* term : terms) if (text.find(term) != std::string::npos) return true;
  return false;
}

/* 客服话里出现这些词，说明它把话题推回给患者、或明确表示没在听——不算实质应答。
   注意「您先说」这类：字面像是礼貌用语，实际是敷衍。 */
inline bool soundsDeflecting(const std::string& text) {
  return containsAny(text, {"不知道","您先说","您问","您请说","麻烦您说",
                            "这个我","我不太懂","您继续","您说说"});
}

/* 客服话里出现这些词，说明确实在正面回答（时间/费用/流程/安排/下一步…）。 */
inline bool containsAnswer(const std::string& text) {
  return containsAny(text, {"天","元","费用","流程","安排","预约","检查",
                            "可以","需要","建议","时间","方案","周期","材料","麻醉"});
}

/* 决策纯函数：给定「模型本轮输出」「上一轮存量」「客服本轮说的话」，
   返回本轮应当落库的状态。只补正向，不改负向。

   三重约束，缺一不可（改动前请用 patient_positive_floor_test 验证仍然成立）：
     1. 模型没动才介入——模型自己动过的绝不覆盖（否则会削弱模型决策权）；
     2. 客服本轮须有实质应答：长度达标、非敷衍、含明确答复信号；
     3. 增量保守：信任 +5、强度 +1，一轮只挪一格。一次训练仅 6—10 轮，
        步长过大会让学员第一轮就见到剧烈跳变，反而失去参考意义。

   情绪词只在「强度到顶 + 信任良好」时才升到「缓和」——不提前，
   避免情绪跳得比信任快，出现「情绪缓和但其实还不太信」的失真。 */
struct PositiveFloorDecision {
  bool applied = false;
  int trustLevel = 0;
  int emotionLevel = 0;
  std::string emotion;
  const char* reason = "not_applied";
};

inline PositiveFloorDecision decidePositiveFloor(int modelEmotionLevel, int modelTrustLevel,
                                                const std::string& modelEmotion,
                                                int stateEmotionLevel, int stateTrustLevel,
                                                const std::string& stateEmotion,
                                                const std::string& learnerText) {
  PositiveFloorDecision d;
  d.emotionLevel = modelEmotionLevel;
  d.trustLevel = modelTrustLevel;
  d.emotion = modelEmotion;

  // 条件 1：模型完全照抄当前状态。
  if (modelEmotion != stateEmotion || modelEmotionLevel != stateEmotionLevel ||
      modelTrustLevel != stateTrustLevel) {
    d.reason = "model_moved_state";
    return d;
  }
  // 条件 2a：这句话本身太短，不构成应答。
  if (utf8CharCount(learnerText) < 8) { d.reason = "reply_too_short"; return d; }
  // 条件 2b：敷衍特征。
  if (soundsDeflecting(learnerText)) { d.reason = "deflecting"; return d; }
  // 条件 2c：没有明确答复信号。
  if (!containsAnswer(learnerText)) { d.reason = "no_answer_signal"; return d; }

  // 条件 3：保守增量。存量先夹取，防止脏数据被持续放大。
  d.applied = true;
  d.reason = "positive_floor_applied";
  d.trustLevel = std::min(100, std::max(0, modelTrustLevel) + 5);
  d.emotionLevel = std::min(2, std::max(-2, modelEmotionLevel) + 1);
  if (d.emotionLevel >= 2 && d.trustLevel >= 70 &&
      (d.emotion == "平静" || d.emotion == "犹豫")) {
    d.emotion = "缓和";
  }
  return d;
}

} // namespace oral_training