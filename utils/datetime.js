/* ============================================================
   utils/datetime.js —— 时间展示的唯一来源

   后端下发的时间只有两种形态，展示一律走本模块，页面不要自己 slice：
   - 完整时间 '2026-09-12T21:28:55+08:00'
     （main.cpp kSessionTimes 的 started_at/updated_at/finished_at、
       reliable_store.h messages/session_hints 的 created_at）
   - 纯日期   '2026-09-12'
     （joined_at / last_training_date / trend.date，后端 to_char 'YYYY-MM-DD'）

   解析一律用手写正则 + Date.UTC，**不用 Date.parse / new Date(字符串)**：
   部分 iOS 内核对带 '+08:00' 的串解析失败，或按本地时区误判。
   串本身已经是北京时间，展示无需换算时区，只做重排。
   ============================================================ */

const FULL_RE = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(?::(\d{2}))?/;
const DATE_RE = /^(\d{4})-(\d{2})-(\d{2})/;

/* 拆出时间分量；纯日期串的 hour/minute 为空串，用于区分「有没有时刻」 */
const parse = value => {
  const text = value === null || value === undefined ? '' : String(value);
  const full = text.match(FULL_RE);
  if (full) {
    return {
      year: full[1], month: full[2], day: full[3],
      hour: full[4], minute: full[5], second: full[6] || '00'
    };
  }
  const date = text.match(DATE_RE);
  if (date) {
    return { year: date[1], month: date[2], day: date[3], hour: '', minute: '', second: '' };
  }
  return null;
};

const clockOf = parts => (parts.hour ? `${parts.hour}:${parts.minute}` : '');

/* 消息气泡时间：'...T21:28:55+08:00' → '21:28'；无时刻或非法 → '' */
const formatClock = value => {
  const parts = parse(value);
  return parts ? clockOf(parts) : '';
};

/* 月日：'2026-09-12' 或完整 ISO → '09-12' */
const formatMonthDay = value => {
  const parts = parse(value);
  return parts ? `${parts.month}-${parts.day}` : '';
};

/* 日期：→ '2026-09-12' */
const formatDate = value => {
  const parts = parse(value);
  return parts ? `${parts.year}-${parts.month}-${parts.day}` : '';
};

/* 月日 + 时刻：完整 ISO → '09-12 21:28'；纯日期 → '09-12' */
const formatDateTime = value => {
  const parts = parse(value);
  if (!parts) return '';
  const clock = clockOf(parts);
  return clock ? `${parts.month}-${parts.day} ${clock}` : `${parts.month}-${parts.day}`;
};

/* 带年份的完整时间：→ '2026-09-12 21:28'；纯日期 → '2026-09-12' */
const formatFull = value => {
  const parts = parse(value);
  if (!parts) return '';
  const clock = clockOf(parts);
  return clock
    ? `${parts.year}-${parts.month}-${parts.day} ${clock}`
    : `${parts.year}-${parts.month}-${parts.day}`;
};

/* 起止区间：同日折叠 '09-12 21:28 — 21:40'；跨天 '09-12 21:28 — 09-13 00:05'。
   任一端缺失时退化成只显示已解析的那一端，不产出空占位符。 */
const formatRange = (start, end) => {
  const from = parse(start);
  if (!from) return '';
  const fromClock = clockOf(from);
  const head = fromClock
    ? `${from.month}-${from.day} ${fromClock}`
    : `${from.month}-${from.day}`;
  const to = parse(end);
  if (!to) return head;
  const sameDay = from.year === to.year && from.month === to.month && from.day === to.day;
  const toClock = clockOf(to);
  const tail = sameDay && toClock ? toClock : `${to.month}-${to.day}${toClock ? ` ${toClock}` : ''}`;
  return `${head} — ${tail}`;
};

/* 北京时间墙上时钟 → UTC 时间轴毫秒，供排序与「距今多久」这类计算。
   纯日期按当天 00:00 处理（last_training_date 就是纯日期）；
   非法值 → NaN，调用方必须用 isFinite 兜底，别让 NaN 参与差值比较。 */
const toMs = value => {
  const parts = parse(value);
  if (!parts) return NaN;
  return Date.UTC(
    Number(parts.year), Number(parts.month) - 1, Number(parts.day),
    Number(parts.hour || 0), Number(parts.minute || 0), Number(parts.second || 0)
  ) - 8 * 3600 * 1000;
};

module.exports = {
  formatClock,
  formatMonthDay,
  formatDate,
  formatDateTime,
  formatFull,
  formatRange,
  toMs
};
