/* ============================================================
   utils/plan.js —— 培训计划的展示派生（单一来源）

   数据全部来自 GET /api/learning/training-plans，字段已在后端算好
   （reliable_store.h listLearnerTrainingPlans），这里只做展示派生，
   不重复请求、不改契约。

   口径提醒：
   - dueAt 是后端按北京时间输出的 'YYYY-MM-DDTHH:mm:ss+08:00' 字符串，
     解析一律走 utils/datetime.js（内部 Date.UTC - 8h 反算），
     本模块及页面都不要再用 Date.parse。
   - 达标判定是「次数 ≥ requiredCount」且「均分 ≥ requiredPassRate」两个条件，
     不能只看次数，否则会出现「还差 0 次」这种伪进度。
   ============================================================ */

const datetime = require('./datetime.js');

const STATUS_TEXT = { done: '已达标', expired: '已过期', pending: '待完成' };

const fmt1 = value => {
  const num = Number(value);
  if (!isFinite(num)) return '0';
  return num % 1 === 0 ? String(Math.round(num)) : num.toFixed(1);
};

/* 时间解析与展示统一由 utils/datetime.js 提供，本模块只做计划口径的包装：
   - formatDue 保留「解析失败回退原串」语义
   - parseDueMs 即 toMs（同一 UTC 反算公式，不再各写一份正则） */
const formatDue = text => datetime.formatDateTime(text) || String(text || '');

const parseDueMs = text => datetime.toMs(text);

const buildCountdown = text => {
  const due = parseDueMs(text);
  if (!isFinite(due)) return '';
  const diff = due - Date.now();
  if (diff <= 0) return '已过截止时间';
  const days = Math.floor(diff / 86400000);
  if (days >= 1) return `距截止还有 ${days} 天`;
  const hours = Math.floor(diff / 3600000);
  if (hours >= 1) return `今天截止 · 剩余 ${hours} 小时`;
  return `今天截止 · 剩余 ${Math.max(1, Math.floor(diff / 60000))} 分钟`;
};

/* 达标摘要：把「差在哪」说清楚，避免只给一个数字 */
const buildRuleSummary = (countDone, scoreDone, remaining) => {
  if (countDone && scoreDone) return '已全部达标';
  if (!countDone && !scoreDone) return `还需 ${remaining} 次 · 均分待提升`;
  if (!countDone) return `还需 ${remaining} 次`;
  return '次数已够 · 均分待提升';
};

const resolveStatus = raw =>
  raw.status || (raw.done ? 'done' : (raw.expired ? 'expired' : 'pending'));

/* 归一化单个计划：补齐状态、倒计时、进度与达标差额 */
const normalizePlan = raw => {
  const plan = raw || {};
  const requiredCount = Math.max(1, Number(plan.requiredCount) || 1);
  const completedCount = Number(plan.completedCount) || 0;
  const requiredPassRate = Number(plan.requiredPassRate) || 0;
  const avgScore = Number(plan.avgScore) || 0;
  /* 判定分必须用后端下发的 score：计划指定了目标维度时它是该维度均分，而 avgScore
     恒为综合分。拿 avgScore 比较会得出「主管看到达标、学员看到未达标」的两种结论。
     只有后端没给 score（老数据）时才退回综合分。 */
  const judgeScore = plan.score === null || plan.score === undefined
    ? avgScore : Number(plan.score);
  const focusLabel = plan.focusDimensionLabel || '';
  /* 防刷分上限：0 = 不限（迁移 023）。它是补充约束，不塞进 requirementText。 */
  const maxPerScenario = Math.max(0, Number(plan.maxPerScenario) || 0);
  /* 逐次达标（迁移 031）：打开时后端判定分取窗口内最低分而非均分。
     文案必须跟着变，否则会写成「均分 >= X」而实际按最低分判，结论对不上。 */
  const requireEachPass = plan.requireEachPass === true;
  /* 判定依据的措辞：打开时判的是「每次」，关闭时是「均分」 */
  const judgeBasisLabel = focusLabel
    ? (requireEachPass ? `${focusLabel}每次` : `${focusLabel}均分`)
    : (requireEachPass ? '每次' : '平均');
  const status = resolveStatus(plan);
  const countDone = completedCount >= requiredCount;
  const scoreDone = judgeScore >= requiredPassRate;
  return {
    id: plan.id,
    title: plan.title || '',
    description: plan.description || '',
    requiredCount,
    completedCount,
    requiredPassRate,
    avgScore,
    judgeScore,
    judgeScoreText: fmt1(judgeScore),
    /* 目标维度 key 必须透传：调用方要按维度筛计划（例如「该弱项是否已被某个计划覆盖」）。
       只给 focusLabel 中文名不够——中文名不能拿来做等值比较。 */
    focusDimension: plan.focusDimension || '',
    focusLabel,
    maxPerScenario,
    requireEachPass,
    dueAt: plan.dueAt,
    dueMs: parseDueMs(plan.dueAt),
    dueText: formatDue(plan.dueAt),
    countdownText: buildCountdown(plan.dueAt),
    status,
    statusText: STATUS_TEXT[status] || '待完成',
    done: status === 'done',
    expired: status === 'expired',
    countDone,
    scoreDone,
    progressText: `${completedCount}/${requiredCount} 次`,
    progressPercent: Math.max(0, Math.min(100, Math.round(completedCount / requiredCount * 100))),
    avgScoreText: fmt1(avgScore),
    requirementText: `完成 ≥ ${requiredCount} 次 · ${judgeBasisLabel} ≥ ${requiredPassRate} 分`,
    ruleSummary: buildRuleSummary(countDone, scoreDone, Math.max(0, requiredCount - completedCount))
  };
};

/* 从计划数组里挑出「最该被提醒的那一个」：
   待完成中截止时间最早的。后端列表按 created_at 倒序返回，
   照原序取第一个会挑到最新发布而不是最紧急的。 */
const pickPlanNotice = rawPlans => {
  const pending = (rawPlans || [])
    .filter(item => resolveStatus(item) === 'pending')
    .map(normalizePlan)
    .sort((a, b) => (isFinite(a.dueMs) ? a.dueMs : Infinity) - (isFinite(b.dueMs) ? b.dueMs : Infinity));
  if (!pending.length) return null;
  const head = pending[0];
  return {
    id: head.id,
    title: head.title,
    countdownText: head.countdownText,
    ruleSummary: head.ruleSummary,
    progressText: head.progressText,
    progressPercent: head.progressPercent,
    // 24 小时内截止或已过截止 → 紧急态，横幅转警示色
    urgent: isFinite(head.dueMs) && head.dueMs - Date.now() <= 24 * 3600 * 1000,
    extraCount: pending.length - 1,
    totalCount: pending.length
  };
};

module.exports = {
  STATUS_TEXT,
  fmt1,
  formatDue,
  parseDueMs,
  buildCountdown,
  buildRuleSummary,
  normalizePlan,
  pickPlanNotice
};
