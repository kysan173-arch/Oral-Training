/* ============================================================
   backend/tests/supervisor_check.js —— utils/supervisor.js 的离线断言

   utils/supervisor.js 只依赖 utils/datetime.js 与 utils/plan.js，
   三者都不碰 wx API，可直接用 node 跑：
     node backend/tests/supervisor_check.js

   覆盖：辅导判定阈值 / 等级推导 / 原因文案 / 主管计划归一化 /
         计划风险分档与挑选顺序（含「全员已达标不得误报」）/
         工作台组装（含指标格随候选人增减）。

   与日期相关的用例按「相对今天」构造，避免写死日期后过期失效。
   失败用例以 ASCII 输出，避免 Windows 控制台编码干扰排查。
   ============================================================ */

const assert = require('assert');
const sup = require('../../utils/supervisor.js');

const cases = [];
const check = (name, actual, expected) => {
  cases.push({ name, actual, expected });
};

/* 北京时间口径的日期串（按天数偏移），用来构造「今天 / 昨天 / 未来」 */
const pad = value => String(value).padStart(2, '0');
const dayText = offsetDays => {
  const date = new Date(Date.now() + 8 * 3600 * 1000 + offsetDays * 86400000);
  return `${date.getUTCFullYear()}-${pad(date.getUTCMonth() + 1)}-${pad(date.getUTCDate())}`;
};
const dayIso = offsetDays => `${dayText(offsetDays)}T23:59:00+08:00`;

/* ---- daysSince：北京时间口径，仅取日期部分 ---- */
check('daysSince/today', sup.daysSince(dayText(0)), 0);
check('daysSince/yesterday', sup.daysSince(dayText(-1)), 1);
check('daysSince/8 days ago', sup.daysSince(dayText(-8)), 8);
check('daysSince/future is -1', sup.daysSince(dayText(1)), -1);
check('daysSince/never trained', sup.daysSince(null), -1);
check('daysSince/empty', sup.daysSince(''), -1);
check('daysSince/garbage', sup.daysSince('not-a-date'), -1);
check('daysSince/partial date is -1', sup.daysSince('2026-09'), -1);
check('daysSince/ignores clock', sup.daysSince(`${dayText(-3)}T23:59:00+08:00`), 3);

/* ---- memberLevel：无记录优先于分数 ---- */
check('level/no sessions', sup.memberLevel({ completedSessions: 0, averageScore: 99 }), '未开始');
check('level/expert', sup.memberLevel({ completedSessions: 3, averageScore: 88 }), '熟练');
check('level/advanced', sup.memberLevel({ completedSessions: 3, averageScore: 72 }), '进阶');
check('level/passed boundary', sup.memberLevel({ completedSessions: 3, averageScore: 60 }), '达标');
check('level/growing', sup.memberLevel({ completedSessions: 3, averageScore: 59 }), '成长中');

/* ---- isCoachingNeeded：7 天 / 60 分两条线 ---- */
check('coaching/fresh and passed', sup.isCoachingNeeded({ lastTrainingDate: dayText(0), averageScore: 80 }), false);
check('coaching/idle boundary 7 days', sup.isCoachingNeeded({ lastTrainingDate: dayText(-7), averageScore: 80 }), false);
check('coaching/idle 8 days', sup.isCoachingNeeded({ lastTrainingDate: dayText(-8), averageScore: 80 }), true);
check('coaching/low score', sup.isCoachingNeeded({ lastTrainingDate: dayText(0), averageScore: 59.9 }), true);
check('coaching/score boundary 60', sup.isCoachingNeeded({ lastTrainingDate: dayText(0), averageScore: 60 }), false);
check('coaching/never trained', sup.isCoachingNeeded({ lastTrainingDate: null, averageScore: 0 }), true);

/* ---- coachingProfile：原因文案与严重度 ---- */
check('profile/never trained',
  sup.coachingProfile({ lastTrainingDate: null, averageScore: 0 }),
  { text: '尚无有效训练记录，建议先安排一次入门场景练习', severity: 'medium' });
check('profile/low score is high',
  sup.coachingProfile({ lastTrainingDate: dayText(-3), averageScore: 52 }),
  { text: '平均分 52 分，低于 60 分及格线，建议安排针对性复练', severity: 'high' });
check('profile/low score and idle spells out days',
  sup.coachingProfile({ lastTrainingDate: dayText(-10), averageScore: 52 }),
  { text: '平均分 52 分，低于 60 分及格线，且已 10 天未训练，建议安排针对性复练', severity: 'high' });
check('profile/idle 9 days is medium',
  sup.coachingProfile({ lastTrainingDate: dayText(-9), averageScore: 75 }),
  { text: '已 9 天未训练，建议提醒保持训练节奏', severity: 'medium' });
check('profile/idle 15 days is high',
  sup.coachingProfile({ lastTrainingDate: dayText(-15), averageScore: 75 }),
  { text: '已 15 天未训练，建议提醒保持训练节奏', severity: 'high' });

/* ---- normalizeSupervisorPlan：主管侧字段口径 ---- */
const planFixture = {
  id: 'plan-1', title: '9 月话术强化', period: 'week', dueAt: dayIso(5),
  requiredCount: 2, requiredPassRate: 70, assignmentCount: 8, doneCount: 3, avgScore: 68.25, expired: false
};
const normalized = sup.normalizeSupervisorPlan(planFixture);
check('plan/periodText', normalized.periodText, '按周');
check('plan/progressText is a bare ratio', normalized.progressText, '3/8');
check('plan/progressPercent', normalized.progressPercent, 38);
check('plan/avgScoreText', normalized.avgScoreText, '68.3');
check('plan/statusKey active', normalized.statusKey, 'active');
check('plan/dueText has no iso junk', normalized.dueText.indexOf('T') < 0 && normalized.dueText.indexOf('+08:00') < 0, true);
check('plan/counts override wins', sup.normalizeSupervisorPlan({ id: 'p', assignmentCount: 0, doneCount: 0 }, { assignmentCount: 4, doneCount: 4 }).progressText, '4/4');
check('plan/zero assignment keeps ratio', sup.normalizeSupervisorPlan({ id: 'p', assignmentCount: 0, doneCount: 0 }).progressText, '0/0');
check('plan/expired', sup.normalizeSupervisorPlan(Object.assign({}, planFixture, { expired: true })).statusText, '已到期');

/* ---- scorePlanRisk：四档 + 「全员已达标不误报」 ---- */
const planAt = (offsetDays, assignmentCount, doneCount) => sup.normalizeSupervisorPlan({
  id: `p${offsetDays}-${doneCount}`, title: 't', period: 'month', dueAt: dayIso(offsetDays),
  requiredCount: 1, requiredPassRate: 60, assignmentCount, doneCount, avgScore: 70, expired: false
});
check('risk/overdue with gap is high', sup.scorePlanRisk(planAt(-1, 8, 3)).severity, 'high');
check('risk/overdue message', sup.scorePlanRisk(planAt(-1, 8, 3)).riskText, '已过截止时间，还有 5 人未达标');
check('risk/within 3 days is high', sup.scorePlanRisk(planAt(2, 8, 3)).severity, 'high');
check('risk/within 7 days low rate is medium', sup.scorePlanRisk(planAt(6, 8, 3)).severity, 'medium');
check('risk/within 7 days good rate is medium', sup.scorePlanRisk(planAt(6, 8, 7)).severity, 'medium');
check('risk/far deadline is normal', sup.scorePlanRisk(planAt(30, 8, 1)).severity, 'normal');
check('risk/far deadline has no text', sup.scorePlanRisk(planAt(30, 8, 1)).riskText, '');
check('risk/all done is normal even when due', sup.scorePlanRisk(planAt(1, 8, 8)).severity, 'normal');
check('risk/all done has no text', sup.scorePlanRisk(planAt(1, 8, 8)).riskText, '');

/* ---- pickPlanRisk：跨计划排序 ---- */
check('pick/empty list', sup.pickPlanRisk([]), null);
check('pick/only expired plans', sup.pickPlanRisk([Object.assign({}, planFixture, { expired: true })]), null);
const picked = sup.pickPlanRisk([
  planAt(30, 8, 1),
  planAt(6, 8, 3),
  planAt(-1, 8, 3)
]);
check('pick/chooses the overdue one', picked.severity, 'high');
check('pick/overdue beats sooner-but-later deadline', picked.riskText, '已过截止时间，还有 5 人未达标');
check('pick/activeCount counts only active', picked.activeCount, 3);
check('pick/progressText carries wording', picked.progressText, '达标 3/8 人');
check('pick/severityText for high', picked.severityText, '优先处理');
check('pick/normal severity reads 进行中', sup.pickPlanRisk([planAt(30, 8, 8)]).severityText, '进行中');

/* ---- buildWorkbench：工作台组装 ---- */
const membersFixture = [
  { id: 'm1', displayName: '王浩', lastTrainingDate: dayText(-9), averageScore: 75, completedSessions: 5 },
  { id: 'm2', displayName: '李颖', lastTrainingDate: dayText(-1), averageScore: 52, completedSessions: 5 },
  { id: 'm3', displayName: '赵敏', lastTrainingDate: dayText(0), averageScore: 88, completedSessions: 5 },
  { id: 'm4', displayName: '', lastTrainingDate: null, averageScore: 0, completedSessions: 0 }
];
const workbench = sup.buildWorkbench({
  dashboard: { studentCount: 8, totalSessions: 42, passRate: 76.5, averageScore: 71.2 },
  members: membersFixture,
  plans: [planAt(30, 8, 8)],
  candidateCount: 3,
  range: 'month'
});
check('workbench/coaching total', workbench.coachingTotal, 3);
check('workbench/high severity first', workbench.coachingMembers[0].displayName, '李颖');
check('workbench/idle days desc within severity', workbench.coachingMembers[1].displayName, '王浩');
check('workbench/unnamed fallback', workbench.coachingMembers[2].displayName, '未命名学员');
check('workbench/coaching preview capped', workbench.coachingMembers.length, 3);
check('workbench/preview cap works', sup.buildWorkbench({ members: membersFixture.concat([
  { id: 'm5', displayName: '陈', lastTrainingDate: null, averageScore: 0, completedSessions: 0 },
  { id: 'm6', displayName: '周', lastTrainingDate: null, averageScore: 0, completedSessions: 0 }
]) }).coachingExtra, 2);
check('workbench/healthy member excluded', workbench.coachingMembers.some(item => item.id === 'm3'), false);
check('workbench/metrics with candidates', workbench.metrics.length, 3);
check('workbench/metrics without candidates', sup.buildWorkbench({ members: membersFixture }).metrics.length, 2);
check('workbench/calm tone when nobody needs coaching',
  sup.buildWorkbench({ members: [membersFixture[2]] }).metrics[0].tone, 'calm');
check('workbench/snapshot passRateText', workbench.snapshot.passRateText, '76.5%');
check('workbench/snapshot rangeText', workbench.snapshot.rangeText, '本月');
check('workbench/active plan count ignores expired',
  sup.buildWorkbench({ plans: [planAt(30, 8, 8), Object.assign({}, planFixture, { expired: true })] }).metrics[1].value, 1);
check('workbench/hasAction true on candidates', sup.buildWorkbench({ candidateCount: 2 }).hasAction, true);
check('workbench/hasAction false when idle', sup.buildWorkbench({ members: [membersFixture[2]] }).hasAction, false);
check('workbench/hasAction false on empty input', sup.buildWorkbench({}).hasAction, false);
check('workbench/empty input is safe', sup.buildWorkbench({}).coachingTotal, 0);

let failed = 0;
for (const item of cases) {
  try {
    assert.deepStrictEqual(item.actual, item.expected);
  } catch (error) {
    failed += 1;
    console.error(`FAIL ${item.name}: expected ${JSON.stringify(item.expected)}, got ${JSON.stringify(item.actual)}`);
  }
}

if (failed > 0) {
  console.error(`\n${failed}/${cases.length} case(s) failed.`);
  process.exit(1);
}

console.log(JSON.stringify({ result: 'passed', cases: cases.length }));
