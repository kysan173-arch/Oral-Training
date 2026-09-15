/* ============================================================
   utils/emotion.js —— 患者情绪标签的唯一来源

   后端在每条 AI 患者回复上带一个 emotion 字段（messages.emotion，
   迁移 012_message_emotion.sql）。两种来源：
   - 模型生成的回复：必属白名单「平静 / 犹豫 / 焦虑 / 缓和」
     （main.cpp normalizePatientReply 的 allowed_emotions）
   - 开场白：可能来自学员在「自定义患者画像」里手填的情绪，是自由文本
     （如「烦躁」「担心」），不限于上述 4 值。

   本模块只做「已知值 → 既有语义色 token」的映射，**不改写字面、不编造语义**：
   未知值一律走中性色，照原样展示。颜色全部复用 app.wxss 的 .tag-* 系列，
   不新增颜色 token。

   展示位置：对练舱（pages/training）与历史详情（pages/session-detail）。
   两个页面共用本模块，避免各写一份映射表导致口径漂移。
   ============================================================ */

const EMOTION_TONES = {
  '平静': 'tag-neutral',
  '犹豫': 'tag-warning',
  '焦虑': 'tag-danger',
  '缓和': 'tag-success'
};

const DEFAULT_EMOTION_TONE = 'tag-neutral';

/* 只有 AI 患者回复带情绪标签；学员消息（role = 'user'）与患者模拟
   消息（learner_patient / standard_customer）一律返回空串。 */
const emotionTextOf = message => {
  if (!message || message.role !== 'patient') return '';
  return String(message.emotion || '').trim();
};

/* 未知/空值走中性色；调用方用 emotionTextOf 的结果做显隐判断 */
const emotionToneOf = emotion =>
  EMOTION_TONES[String(emotion || '').trim()] || DEFAULT_EMOTION_TONE;

module.exports = {
  EMOTION_TONES,
  DEFAULT_EMOTION_TONE,
  emotionTextOf,
  emotionToneOf
};
