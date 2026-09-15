/* ============================================================
   backend/tests/supervisor_home_check.js —— 主管首页工作台的接线验收

   与 supervisor_check.js 的分工：
     supervisor_check.js        只测 utils/supervisor.js 的派生逻辑（纯函数）
     supervisor_home_check.js   测 pages/home/home.js 的接线——
                                  桩掉 wx 与 utils/api.js，用后端真实字段形状的载荷
                                  驱动 onShow，断言页面最终 setData 出来的
                                  workbench 能直接喂给 WXML（无函数调用、无 ISO 串）

   跑法：node backend/tests/supervisor_home_check.js
   失败用例以 ASCII 输出，避免 Windows 控制台编码干扰排查。
   ============================================================ */

const assert = require('assert');
const path = require('path');
const Module = require('module');

const root = path.resolve(__dirname, '..', '..');

/* 北京时间口径的日期串（按天数偏移） */
const pad = value => String(value).padStart(2, '0');
const dayText = offsetDays => {
  const date = new Date(Date.now() + 8 * 3600 * 1000 + offsetDays * 86400000);
  return `${date.getUTCFullYear()}-${pad(date.getUTCMonth() + 1)}-${pad(date.getUTCDate())}`;
};
const dayIso = offsetDays => `${dayText(offsetDays)}T23:59:00+08:00`;

/* ---- 小程序全局桩：只求模块能加载、onShow 能跑完 ---- */
const chain = new Proxy(function () {}, { get: () => chain, apply: () => chain });
global.wx = new Proxy({}, {
  get: (target, key) => {
    if (key === 'getStorageSync') return () => '';
    if (key === 'getSystemInfoSync' || key === 'getWindowInfo') {
      return () => ({ windowWidth: 375, windowHeight: 667, statusBarHeight: 20, safeArea: {} });
    }
    if (key === 'env') return { USER_DATA_PATH: '/tmp' };
    return chain;
  }
});
global.getApp = () => ({ globalData: {} });
global.getCurrentPages = () => [];

/* ---- 用桩替换 utils/api.js（必须在 require home.js 之前塞进 require.cache） ---- */
const apiPath = require.resolve(path.join(root, 'utils', 'api.js'));
/* 真实 api.js 的纯格式化工具：页面合法使用它们，桩不该判成失败。
   取值必须在本文件下面改写 require.cache 之前完成。 */
const realApi = (() => {
  try { return require(apiPath); } catch (error) { return {}; }
})();
const apiStub = {
  getCurrentUser: () => ({ id: 'demo-user-001', role: 'admin', displayName: '机构主管' }),
  ensureAuthenticated: () => Promise.resolve(),
  getSupervisorDashboard: () => Promise.resolve({
    range: 'month', studentCount: 8, totalSessions: 42, completedSessions: 30,
    averageScore: 71.25, passRate: 76.5, scenarioStats: [], dimensionAverages: {}, trend: []
  }),
  /* 字段形状与 reliable_store.h listSupervisorMembers 一致 */
  getSupervisorMembers: () => Promise.resolve({
    members: [
      { id: 'm1', displayName: '王浩', joinedAt: dayText(-40), totalSessions: 9, completedSessions: 7,
        averageScore: 75, passRate: 71.4, lastTrainingDate: dayText(-9) },
      { id: 'm2', displayName: '李颖', joinedAt: dayText(-30), totalSessions: 6, completedSessions: 5,
        averageScore: 52, passRate: 40, lastTrainingDate: dayText(-1) },
      { id: 'm3', displayName: '赵敏', joinedAt: dayText(-20), totalSessions: 8, completedSessions: 8,
        averageScore: 88, passRate: 100, lastTrainingDate: dayText(0) }
    ],
    total: 3, totalTeamMembers: 3, totalLearners: 4
  }),
  /* 字段形状与 GET /supervisor/training-plans?status=active 一致 */
  getSupervisorTrainingPlans: () => Promise.resolve({
    plans: [
      { id: 'plan-old', title: '9 月种植牙话术强化', period: 'month', dueAt: dayIso(-1),
        requiredCount: 2, requiredPassRate: 70, assignmentCount: 8, doneCount: 3,
        avgScore: 68.25, expired: false }
    ]
  }),
  getTeamCandidates: () => Promise.resolve({ candidates: [], total: 0, totalTeamMembers: 3, totalCandidates: 2 })
};
if (typeof realApi.formatScore === 'function') apiStub.formatScore = realApi.formatScore;
const apiModule = new Module(apiPath, null);
apiModule.filename = apiPath;
apiModule.loaded = true;
apiModule.exports = apiStub;
require.cache[apiPath] = apiModule;

/* ---- 捕获 Page 配置并造一个可跑的页面实例 ---- */
let pageConfig = null;
global.Page = config => { pageConfig = config; };
require(path.join(root, 'pages', 'home', 'home.js'));
assert.ok(pageConfig, 'home.js 未调用 Page()');

const cases = [];
const check = (name, actual, expected) => cases.push({ name, actual, expected });

const page = Object.assign({}, pageConfig, {
  data: JSON.parse(JSON.stringify(pageConfig.data)),
  setData(patch) { Object.assign(this.data, patch); },
  getTabBar: () => null
});

const settle = () => new Promise(resolve => setTimeout(resolve, 0));

(async () => {
  page.onShow();
  /* onShow 里是一层 ensureAuthenticated 的 then，再套一层四路 Promise.all */
  await settle();
  await settle();
  await settle();

  const workbench = page.data.workbench;
  check('page/identifies admin', page.data.isAdmin, true);
  check('page/workbench built', !!workbench, true);
  if (!workbench) {
    console.error('FAIL page/workbench built: workbench 未生成，后续断言无法进行');
    process.exit(1);
  }

  /* 学习者区块不得被填充：主管不该看到「我的训练概览」 */
  check('page/learner content untouched', page.data.recentSessions.length, 0);
  check('page/learner planNotice untouched', page.data.planNotice, null);

  /* 指标格 */
  check('page/metric count', workbench.metrics.length, 3);
  check('page/coaching metric value', workbench.metrics[0].value, 2);
  check('page/coaching metric tone', workbench.metrics[0].tone, 'warn');
  check('page/candidate metric value', workbench.metrics[2].value, 2);

  /* 待辅导成员：李颖(低分, high) 应排在王浩(久未训练, medium) 之前 */
  check('page/coaching rows', workbench.coachingMembers.length, 2);
  check('page/high severity first', workbench.coachingMembers[0].displayName, '李颖');
  check('page/healthy member excluded', workbench.coachingMembers.some(item => item.id === 'm3'), false);
  check('page/row carries initial', workbench.coachingMembers[0].initial, '李');
  check('page/row carries severityText', workbench.coachingMembers[0].severityText, '优先处理');

  /* 计划风险：昨日已截止且 5 人未达标 */
  check('page/plan risk picked', workbench.planRisk.id, 'plan-old');
  check('page/plan risk severity', workbench.planRisk.severity, 'high');
  check('page/plan risk text', workbench.planRisk.riskText, '已过截止时间，还有 5 人未达标');
  check('page/plan progress wording', workbench.planRisk.progressText, '达标 3/8 人');

  /* WXML 契约：不能把 ISO 串直接送进模板，也不能出现 undefined */
  const serialized = JSON.stringify(workbench);
  check('page/no ISO time leaked', serialized.indexOf('+08:00') < 0 && serialized.indexOf('T23:59') < 0, true);
  check('page/no undefined leaked', serialized.indexOf('undefined') < 0, true);
  check('page/snapshot passRateText', workbench.snapshot.passRateText, '76.5%');
  check('page/snapshot rangeText', workbench.snapshot.rangeText, '本月');
  check('page/hasAction', workbench.hasAction, true);

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
})();
