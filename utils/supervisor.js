/* ============================================================
   utils/supervisor.js —— 主管端派生逻辑的唯一来源

   三块职责：
   1. 成员辅导判定：谁需要辅导、原因文案、严重度
      数据来自 GET /api/supervisor/members
   2. 主管侧计划口径：字段是 assignmentCount / doneCount，
      与学员侧 plan.js 的 completedCount / requiredCount 不是一套，
      所以不并入 plan.js，避免两侧口径互相污染
   3. 首页工作台组装：把「今天要处理什么」算成一个纯数据对象，
      页面只负责渲染，不在 WXML 里调用任何函数

   阈值（7 天未训练 / 60 分及格）与全部中文文案只在本文件修改。
   页面不得再各写一份判定，否则「数据」tab 与首页会给出不一致的结论。
   ============================================================ */

const datetime = require('./datetime.js');
const { fmt1, formatDue, buildCountdown } = require('./plan.js');

/* 连续未训练超过该天数即视为需要辅导（与 admin 成员管理页口径一致） */
const COACHING_IDLE_DAYS = 7;
const PASS_SCORE = 60;
/* 首页待辅导列表最多展开几条，其余折叠为「另有 N 名」 */
const COACHING_PREVIEW = 3;

const SEVERITY_TEXT = { high: '优先处理', medium: '建议关注', normal: '保持节奏' };
const SEVERITY_ORDER = { high: 0, medium: 1, normal: 2 };
const RANGE_TEXT = { week: '本周', month: '本月', quarter: '本季度', all: '全部' };

/* 距今天数（只取日期部分，按北京时间算）。
   无有效日期（从未训练或格式异常）→ -1。
   不用 new Date(字符串)：iOS 对带时区的串解析不可靠，统一走 datetime 反算；
   也不能用设备本地时区，否则跨时区会出现「今天/明天」漂移。 */
const daysSince = text => {
  const target = datetime.toMs(datetime.formatDate(text));
  if (!isFinite(target)) return -1;
  const beijing = new Date(Date.now() + 8 * 3600 * 1000);
  const todayStart = Date.UTC(
    beijing.getUTCFullYear(), beijing.getUTCMonth(), beijing.getUTCDate()
  ) - 8 * 3600 * 1000;
  const diff = Math.round((todayStart - target) / 86400000);
  return diff >= 0 ? diff : -1;
};

/* 成员等级：后端没有等级字段，按平均分推导，纯展示用 */
const memberLevel = member => {
  const score = Number(member.averageScore) || 0;
  if (!Number(member.completedSessions)) return '未开始';
  if (score >= 85) return '熟练';
  if (score >= 70) return '进阶';
  if (score >= PASS_SCORE) return '达标';
  return '成长中';
};

/* 待辅导判定：连续 7 天以上未训练，或平均分低于 60 分。
   没有有效训练记录（days < 0）同样计入，否则新成员永远不会被提醒。 */
const isCoachingNeeded = member => {
  const days = daysSince(member.lastTrainingDate);
  return days < 0 || days > COACHING_IDLE_DAYS || Number(member.averageScore) < PASS_SCORE;
};

/* 把「为什么需要辅导」说清楚，避免只给一个标签 */
const coachingProfile = member => {
  const days = daysSince(member.lastTrainingDate);
  const score = fmt1(member.averageScore);
  if (days < 0) {
    return { text: '尚无有效训练记录，建议先安排一次入门场景练习', severity: 'medium' };
  }
  if (Number(member.averageScore) < PASS_SCORE) {
    const extra = days > COACHING_IDLE_DAYS ? `，且已 ${days} 天未训练` : '';
    return { text: `平均分 ${score} 分，低于 60 分及格线${extra}，建议安排针对性复练`, severity: 'high' };
  }
  return { text: `已 ${days} 天未训练，建议提醒保持训练节奏`, severity: days > 14 ? 'high' : 'medium' };
};

/* 主管侧计划归一化：补齐状态、截止文案、达标进度。
   progressText 只给裸比值（'3/8'），措辞由消费方拼——培训页与工作台
   统一为「达标 3/8 人」，共享层不该替它们决定用词。
   counts 用于详情接口这类「计数挂在顶层而非 plan 对象上」的返回结构。 */
const normalizeSupervisorPlan = (raw, counts) => {
  const plan = raw || {};
  const source = counts || plan;
  const assignmentCount = Number(source.assignmentCount) || 0;
  const doneCount = Number(source.doneCount) || 0;
  const expired = !!plan.expired;
  return Object.assign({}, plan, {
    assignmentCount,
    doneCount,
    periodText: plan.period === 'week' ? '按周' : '按月',
    dueText: formatDue(plan.dueAt),
    dueMs: datetime.toMs(plan.dueAt),
    progressPercent: assignmentCount ? Math.round(doneCount / assignmentCount * 100) : 0,
    progressText: `${doneCount}/${assignmentCount}`,
    avgScoreText: fmt1(plan.avgScore),
    requirementText: `完成 ≥ ${plan.requiredCount} 次 · 平均 ≥ ${plan.requiredPassRate} 分`,
    statusKey: expired ? 'expired' : 'active',
    statusText: expired ? '已到期' : '进行中'
  });
};

/* 单个计划的紧迫度：只有「快到期且还没做完」才算风险，长期计划不该示警。
   全员已达标 → normal（没有可催的对象，示警就是误报）；
   已过截止时间且有缺口 → high；剩余 ≤3 天有缺口 → high；
   剩余 ≤7 天且达标不足半数 → medium；其余 normal。 */
const scorePlanRisk = plan => {
  const days = isFinite(plan.dueMs) ? Math.ceil((plan.dueMs - Date.now()) / 86400000) : NaN;
  const missing = Math.max(0, plan.assignmentCount - plan.doneCount);
  const rate = plan.assignmentCount ? plan.doneCount / plan.assignmentCount : 0;
  if (!isFinite(days) || missing === 0) return { days, severity: 'normal', riskText: '' };
  if (days <= 0 && missing > 0) return { days, severity: 'high', riskText: `已过截止时间，还有 ${missing} 人未达标` };
  if (days <= 3 && missing > 0) return { days, severity: 'high', riskText: `剩余 ${days} 天，还有 ${missing} 人未达标` };
  if (days <= 7 && rate < 0.5 && missing > 0) {
    return { days, severity: 'medium', riskText: `剩余 ${days} 天，仅 ${plan.doneCount}/${plan.assignmentCount} 人达标` };
  }
  if (days <= 7) return { days, severity: 'medium', riskText: `剩余 ${days} 天` };
  return { days, severity: 'normal', riskText: '' };
};

/* 从主管计划列表里挑出「最该被催的那一个」：先按严重度，再按剩余天数升序。
   列表按 created_at 倒序返回，照原序取会挑到最新发布而不是最紧急的。 */
const pickPlanRisk = rawPlans => {
  const active = (rawPlans || [])
    .map(item => normalizeSupervisorPlan(item))
    .filter(plan => !plan.expired);
  if (!active.length) return null;
  const scored = active
    .map(plan => Object.assign({ plan }, scorePlanRisk(plan)))
    .sort((a, b) => {
      const bySeverity = SEVERITY_ORDER[a.severity] - SEVERITY_ORDER[b.severity];
      if (bySeverity !== 0) return bySeverity;
      const left = isFinite(a.days) ? a.days : Infinity;
      const right = isFinite(b.days) ? b.days : Infinity;
      return left - right;
    });
  const head = scored[0];
  return {
    id: head.plan.id,
    title: head.plan.title,
    countdownText: buildCountdown(head.plan.dueAt),
    // 措辞与培训页统一为「达标 3/8 人」（admin-training.wxml 同款）
    progressText: head.plan.assignmentCount ? `达标 ${head.plan.progressText} 人` : '暂无指派对象',
    progressPercent: head.plan.progressPercent,
    avgScoreText: head.plan.avgScoreText,
    severity: head.severity,
    severityText: head.severity === 'normal' ? '进行中' : (SEVERITY_TEXT[head.severity] || ''),
    riskText: head.riskText,
    activeCount: active.length
  };
};

/* 团队速览：纯上下文数字，不做任何判定 */
const buildSnapshot = (dashboard, range) => {
  const data = dashboard || {};
  return {
    rangeText: RANGE_TEXT[range] || RANGE_TEXT.month,
    studentCount: Number(data.studentCount) || 0,
    totalSessions: Number(data.totalSessions) || 0,
    /* 均分与及格率只统计「已评分」会话：后端在没有已评分报告时返回 null，
       这里必须显示「暂无」——被 fmt1 兜底成 0 会被读成「团队 0 分」。 */
    passRateText: data.passRate === null || data.passRate === undefined
      ? '暂无' : `${fmt1(data.passRate)}%`,
    averageScoreText: data.averageScore === null || data.averageScore === undefined
      ? '暂无' : fmt1(data.averageScore)
  };
};

/* 首页工作台组装：页面拿到即可直接渲染，不需要再做任何派生 */
const buildWorkbench = (input) => {
  const source = input || {};
  const members = source.members || [];
  const plans = source.plans || [];
  const candidates = Number(source.candidateCount) || 0;

  const coaching = members
    .filter(isCoachingNeeded)
    .map(member => {
      const profile = coachingProfile(member);
      return {
        id: member.id,
        initial: String(member.displayName || '学').slice(0, 1),
        displayName: member.displayName || '未命名学员',
        reason: profile.text,
        severity: profile.severity,
        severityText: SEVERITY_TEXT[profile.severity] || '',
        days: daysSince(member.lastTrainingDate)
      };
    })
    .sort((a, b) => (SEVERITY_ORDER[a.severity] - SEVERITY_ORDER[b.severity]) || (b.days - a.days));

  const planRisk = pickPlanRisk(plans);
  const activeCount = plans.map(item => normalizeSupervisorPlan(item)).filter(plan => !plan.expired).length;
  const metrics = [
    { id: 'coaching', value: coaching.length, label: '待辅导成员', tone: coaching.length ? 'warn' : 'calm' },
    { id: 'plans', value: activeCount, label: '进行中计划', tone: 'plain' }
  ];
  if (candidates > 0) {
    metrics.push({ id: 'candidates', value: candidates, label: '未归属学员', tone: 'info' });
  }

  return {
    metrics,
    coachingMembers: coaching.slice(0, COACHING_PREVIEW),
    coachingTotal: coaching.length,
    coachingExtra: Math.max(0, coaching.length - COACHING_PREVIEW),
    planRisk,
    candidateCount: candidates,
    snapshot: buildSnapshot(source.dashboard, source.range),
    /* 有待办才渲染行动区；否则显示「当前没有需要处理的待办」 */
    hasAction: coaching.length > 0 || candidates > 0 || !!planRisk
  };
};

module.exports = {
  COACHING_IDLE_DAYS,
  PASS_SCORE,
  SEVERITY_TEXT,
  SEVERITY_ORDER,
  RANGE_TEXT,
  daysSince,
  memberLevel,
  isCoachingNeeded,
  coachingProfile,
  normalizeSupervisorPlan,
  scorePlanRisk,
  pickPlanRisk,
  buildSnapshot,
  buildWorkbench
};
