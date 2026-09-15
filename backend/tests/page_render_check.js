/* ============================================================
   backend/tests/page_render_check.js —— 时间展示的渲染级验收

   为什么需要它：语法检查与结构校验都抓不到「JS 把后端原始 ISO 串
   透传给 WXML」这类问题——它编译通过、结构正确，只是屏幕上难看。
   本脚本用小程序的 wx / Page 桩加载真实页面模块，喂入后端真实格式的
   载荷（'YYYY-MM-DDTHH:mm:ss+08:00'），断言页面 setData 后的展示字段。

   运行（在项目根目录或任意目录均可，脚本自行定位仓库根）：
     node backend/tests/page_render_check.js

   覆盖：
   - 对练舱 / 会话详情 / 历史记录 / 主管看板 / 结果页 / 复盘页 的时间字段
   - 对练舱 / 会话详情的患者情绪标签派生与 utils/emotion.js 映射口径
   - roleplay 气泡时间（收敛后行为必须不变）
   - utils/plan.js 委托 utils/datetime.js 后输出必须不变
   ============================================================ */

const path = require('path');

const root = path.resolve(__dirname, '..', '..');

/* 后端真实返回格式：北京时间 + '+08:00' 后缀 */
const ISO_START = '2026-09-12T21:28:55+08:00';
const ISO_END = '2026-09-12T21:40:10+08:00';
const ISO_MSG = '2026-09-12T21:29:10+08:00';

/* ---------- 小程序全局桩 ---------- */
/* 用 Proxy 兜住所有 wx.* 调用，只有需要确定返回值的少数几个显式给出 */
const chain = new Proxy(function () {}, { get: () => chain, apply: () => chain });
global.wx = new Proxy({}, {
  get: (target, key) => {
    if (key === 'getStorageSync') return () => '';
    if (key === 'getSystemInfoSync' || key === 'getWindowInfo') {
      return () => ({ windowWidth: 375, windowHeight: 667, statusBarHeight: 20, safeArea: {} });
    }
    if (key === 'createSelectorQuery') return () => chain;
    if (key === 'env') return { USER_DATA_PATH: '/tmp' };
    return chain;
  }
});

let captured = null;
global.Page = cfg => { captured = cfg; };
global.Component = () => {};
global.App = () => {};
global.getApp = () => ({ globalData: {} });
global.getCurrentPages = () => [];

const apiPath = path.join(root, 'utils', 'api.js');

/* 桩只负责「数据接口」；页面同时会用到的纯格式化工具（formatScore）直接取真实实现，
   避免页面合法使用真实导出时被桩判成假失败。必须在任何桩写入 require.cache 之前取值。 */
const PURE_API = (() => {
  try { return require(apiPath); } catch (error) {
    console.error('WARN: 无法 require 真实 utils/api.js：' + error.message);
    return {};
  }
})();
const pureDefaults = () => {
  const base = {};
  if (typeof PURE_API.formatScore === 'function') base.formatScore = PURE_API.formatScore;
  return base;
};

/* 把 utils/api.js 换成桩再加载页面：页面里的 require 会命中这个缓存条目 */
const loadPage = (relativePath, apiStub) => {
  delete require.cache[apiPath];
  require.cache[apiPath] = {
    id: apiPath, filename: apiPath, loaded: true,
    exports: Object.assign(pureDefaults(), apiStub), children: [], paths: []
  };
  const pagePath = path.join(root, relativePath);
  delete require.cache[pagePath];
  captured = null;
  require(pagePath);
  const cfg = captured;
  const instance = Object.create(cfg);
  instance.data = JSON.parse(JSON.stringify(cfg.data || {}));
  instance.setData = function (patch) { Object.assign(this.data, patch); };
  return instance;
};

/* 页面加载是 Promise 链，让微任务与定时器跑完再断言 */
const tick = () => new Promise(resolve => setTimeout(resolve, 30));

const results = [];
const expect = (name, actual, wanted) => {
  results.push({ name, pass: actual === wanted, actual, wanted });
};

const run = async () => {
  /* ---------- 1. 对练舱：气泡时间必须是 HH:MM ---------- */
  {
    const page = loadPage('pages/training/training.js', {
      getSession: () => Promise.resolve({
        session: {
          id: 's1', scenarioId: 'sc1', currentRound: 2, maxRounds: 10,
          status: 'in_progress', customPatientProfile: null
        },
        messages: [
          { id: 'm1', role: 'patient', content: 'hi', createdAt: ISO_MSG, emotion: '焦虑' },
          { id: 'm2', role: 'user', content: 'yo', createdAt: ISO_START, emotion: '焦虑' },
          { id: 'm3', role: 'patient', content: 'aiya', createdAt: ISO_MSG, emotion: '烦躁' },
          { id: 'm4', role: 'patient', content: 'legacy', createdAt: ISO_MSG }
        ],
        pendingMessage: null
      }),
      getScenarios: () => Promise.resolve({
        items: [{ id: 'sc1', name: '种植牙基础咨询', patientProfile: { gender: 'unknown' } }]
      })
    });
    page.sessionId = 's1';
    page.loadSession();
    await tick();
    expect('training/patient bubble time', page.data.messages[0].time, '21:29');
    expect('training/user bubble time', page.data.messages[1].time, '21:28');
    expect('training/patient emotion text', page.data.messages[0].emotionText, '焦虑');
    expect('training/patient emotion tone', page.data.messages[0].emotionTone, 'tag-danger');
    expect('training/user message has no emotion', page.data.messages[1].emotionText, '');
    expect('training/custom free-text emotion kept verbatim', page.data.messages[2].emotionText, '烦躁');
    expect('training/unknown emotion falls back to neutral', page.data.messages[2].emotionTone, 'tag-neutral');
    expect('training/legacy message has no emotion', page.data.messages[3].emotionText, '');
  }

  /* ---------- 2. 历史记录列表卡 ---------- */
  {
    const page = loadPage('pages/report/report.js', {
      getCurrentUser: () => ({ role: 'learner' }),
      getSessions: () => Promise.resolve({
        items: [{
          id: 's1', status: 'completed', totalScore: 88,
          updatedAt: ISO_START, currentRound: 6, maxRounds: 10
        }]
      })
    });
    page.loadSessions();
    await tick();
    expect('report/card time', page.data.sessions[0].updatedAtText, '09-12 21:28');
  }

  /* ---------- 3. 会话详情：头部开始时间 + 气泡时间 ---------- */
  {
    const page = loadPage('pages/session-detail/session-detail.js', {
      getSession: () => Promise.resolve({
        session: {
          id: 's1', status: 'completed', startedAt: ISO_START,
          scenarioName: 'x', currentRound: 6, maxRounds: 10
        },
        messages: [
          { id: 'm1', role: 'user', content: 'a', createdAt: ISO_MSG },
          { id: 'm2', role: 'patient', content: 'b', createdAt: ISO_MSG, emotion: '缓和' },
          { id: 'm3', role: 'standard_customer', content: 'c', createdAt: ISO_MSG, emotion: '缓和' }
        ]
      }),
      getEvaluation: () => Promise.resolve({ status: 'ready' })
    });
    page.sessionId = 's1';
    page.data.isRoleplay = false;
    page.loadDetail();
    await tick();
    expect('session-detail/header startedAt', page.data.session.startedAtText, '2026-09-12 21:28');
    expect('session-detail/message time', page.data.messages[0].time, '21:29');
    expect('session-detail/patient emotion text', page.data.messages[1].emotionText, '缓和');
    expect('session-detail/patient emotion tone', page.data.messages[1].emotionTone, 'tag-success');
    expect('session-detail/roleplay message has no emotion', page.data.messages[2].emotionText, '');
  }

  /* ---------- 4. 结果页起止区间（同日折叠） ---------- */
  {
    /* 注意层级：pollReport 读的是 report.evaluation（外层是任务状态，内层才是评分），
       旧桩把评分字段平铺在顶层，导致这条分支实际从未被驱动过。 */
    const report = {
      status: 'ready',
      evaluation: {
        totalScore: 85,
        dimensionScores: {
          empathy: 80, knowledgeAccuracy: 85, needsDiscovery: 75,
          serviceEtiquette: 90, medicalCompliance: 88
        },
        strengths: [], improvements: [], recommendedPhrases: [],
        violations: [
          { id: 'v1', type: 'a', deduction: 5, originalQuote: 'q1', reason: 'r1', recommendedRewrite: 'w1' },
          { id: 'v2', type: 'b', deduction: 5, originalQuote: 'q2', reason: 'r2', recommendedRewrite: 'w2' },
          { id: 'v3', type: 'c', deduction: 5, originalQuote: 'q3', reason: 'r3', recommendedRewrite: 'w3' }
        ],
        roundComments: [
          { round: 1, userMessage: 'u1', comment: 'c1', recommendedRewrite: 'w1' },
          { round: 2, userMessage: 'u2', comment: 'c2', recommendedRewrite: 'w2' }
        ]
      }
    };
    const page = loadPage('pages/result/result.js', {
      getSession: () => Promise.resolve({
        session: { id: 's1', scenarioId: 'sc1', status: 'completed', startedAt: ISO_START, finishedAt: ISO_END }
      }),
      getScenarios: () => Promise.resolve({ items: [{ id: 'sc1', name: 'x' }] }),
      getEvaluation: () => Promise.resolve(report)
    });
    page.sessionId = 's1';
    page.loadInitialData();
    await tick();
    expect('result/range text', page.data.session && page.data.session.rangeText, '09-12 21:28 — 21:40');
    /* 折叠派生：默认只显示首条。WXML 不支持函数调用，可见列表必须在 JS 预算；
       若退回 {{list.slice(0,1)}} 这类写法，以下断言会红。 */
    expect('result/violations folded', page.data.visibleViolations.length, 1);
    expect('result/roundComments folded', page.data.visibleRoundComments.length, 1);
    page.toggleViolations();
    page.toggleRoundComments();
    expect('result/violations expanded', page.data.visibleViolations.length, 3);
    expect('result/roundComments expanded', page.data.visibleRoundComments.length, 2);
    page.toggleViolations();
    expect('result/violations re-folded', page.data.visibleViolations.length, 1);
  }

  /* ---------- 4b. 训练页：场景加载失败必须留痕（不能空成「没有场景可练」） ---------- */
  {
    const page = loadPage('pages/index/index.js', {
      getCurrentUser: () => ({ role: 'learner' }),
      getScenarios: () => Promise.reject(new Error('boom')),
      getRoleplayScenarios: () => Promise.resolve({ items: [] }),
      getRoleplaySessions: () => Promise.resolve({ items: [] }),
      getLearnerTrainingPlans: () => Promise.resolve({ plans: [] })
    });
    page.onShow();
    await tick();
    expect('index/scenarios failed flag', page.data.scenariosFailed, true);
    expect('index/scenarios cleared on failure', page.data.scenarios.length, 0);
    page.retryScenarios();
    await tick();
    expect('index/scenarios failed flag survives retry', page.data.scenariosFailed, true);
  }

  /* ---------- 5. 复盘页起止区间 ---------- */
  {
    const page = loadPage('pages/roleplay-result/roleplay-result.js', {
      getRoleplaySession: () => Promise.resolve({
        session: { id: 'r1', scenarioId: 'sc1', startedAt: ISO_START, finishedAt: ISO_END }
      }),
      getRoleplayScenarios: () => Promise.resolve({ items: [{ id: 'sc1', name: 'x' }] }),
      getRoleplaySummary: () => Promise.resolve({ status: 'ready', summary: {} })
    });
    page.sessionId = 'r1';
    page.loadInitialData();
    await tick();
    expect('roleplay-result/range text', page.data.session && page.data.session.rangeText, '09-12 21:28 — 21:40');
  }

  /* ---------- 6. 主管看板「近期训练」 ---------- */
  {
    const page = loadPage('pages/admin/admin.js', {
      getCurrentUser: () => ({ role: 'admin' }),
      getDashboard: () => Promise.resolve({
        totalSessions: 10, completedSessions: 8, averageScore: 82,
        sceneStats: [], dimensionAverages: {},
        recentSessions: [{
          id: 's1', status: 'completed', evaluationStatus: 'ready',
          updatedAt: ISO_START, currentRound: 6
        }]
      })
    });
    page.loadPersonal();
    await tick();
    const recent = page.data.personal && page.data.personal.recentSessions;
    expect('admin/recent session time', recent && recent[0] && recent[0].updatedAtText, '09-12 21:28');
  }

  /* ---------- 7. roleplay 气泡时间：收敛 timeOf 后行为必须不变 ---------- */
  {
    const page = loadPage('pages/roleplay/roleplay.js', {
      getRoleplaySession: () => Promise.resolve({
        session: { id: 'r1', scenarioId: 'sc1', status: 'in_progress' },
        messages: [{ id: 'm1', role: 'learner_patient', content: 'a', createdAt: ISO_MSG }]
      }),
      getRoleplayScenarios: () => Promise.resolve({ items: [{ id: 'sc1', name: 'x' }] })
    });
    page.sessionId = 'r1';
    page.loadSession();
    await tick();
    expect('roleplay/bubble time unchanged', page.data.messages[0].time, '21:29');
  }

  /* ---------- 8. utils/plan.js 委托 datetime 后输出必须不变 ---------- */
  {
    const plan = require(path.join(root, 'utils', 'plan.js'));
    expect('plan/formatDue unchanged', plan.formatDue(ISO_START), '09-12 21:28');
    expect('plan/formatDue fallback', plan.formatDue('garbage'), 'garbage');
    expect('plan/formatDue empty', plan.formatDue(''), '');
    expect('plan/parseDueMs finite', Number.isFinite(plan.parseDueMs(ISO_START)), true);
    expect('plan/parseDueMs matches UTC formula', plan.parseDueMs(ISO_START), Date.UTC(2026, 8, 12, 13, 28, 55));
    expect('plan/countdown non-empty', plan.buildCountdown(ISO_START).length > 0, true);
    const normalized = plan.normalizePlan({
      id: 'p1', title: 't', dueAt: ISO_START,
      requiredCount: 2, completedCount: 1, requiredPassRate: 70, avgScore: 60
    });
    expect('plan/normalizePlan dueText', normalized.dueText, '09-12 21:28');
  }

  /* ---------- 9. 患者情绪标签映射（唯一来源 utils/emotion.js） ---------- */
  {
    const emotion = require(path.join(root, 'utils', 'emotion.js'));
    expect('emotion/平静 neutral', emotion.emotionToneOf('平静'), 'tag-neutral');
    expect('emotion/犹豫 warning', emotion.emotionToneOf('犹豫'), 'tag-warning');
    expect('emotion/焦虑 danger', emotion.emotionToneOf('焦虑'), 'tag-danger');
    expect('emotion/缓和 success', emotion.emotionToneOf('缓和'), 'tag-success');
    expect('emotion/unknown falls back to neutral', emotion.emotionToneOf('烦躁'), 'tag-neutral');
    expect('emotion/empty falls back to neutral', emotion.emotionToneOf(''), 'tag-neutral');
    expect('emotion/patient text kept verbatim', emotion.emotionTextOf({ role: 'patient', emotion: '犹豫' }), '犹豫');
    expect('emotion/user message suppressed', emotion.emotionTextOf({ role: 'user', emotion: '犹豫' }), '');
    expect('emotion/standard_customer suppressed', emotion.emotionTextOf({ role: 'standard_customer', emotion: '犹豫' }), '');
    expect('emotion/learner_patient suppressed', emotion.emotionTextOf({ role: 'learner_patient', emotion: '犹豫' }), '');
    expect('emotion/missing field is empty', emotion.emotionTextOf({ role: 'patient' }), '');
    expect('emotion/null message is safe', emotion.emotionTextOf(null), '');
  }

  /* ---------- 10. 错题重练页：画像合并 + 原话透传 ---------- */
  {
    const page = loadPage('pages/mistake-retrain/mistake-retrain.js', {
      getMistakeRetrainContext: () => Promise.resolve({
        session: { id: 's1', scenarioId: 'sc1', status: 'completed' },
        mistake: {
          mistakeKey: 'violation-3-1', kind: 'violation', priority: 'high', round: 3,
          originalQuote: '包治好', reason: '承诺疗效', recommendedRewrite: 'x', mastered: false
        },
        patientQuestion: '我这牙疼是不是拔了就好了？',
        patientEmotion: '焦虑',
        originalAnswer: '拔了就好了。',
        scenario: { id: 'sc1', name: '拔牙咨询', patientProfile: { age: 35, gender: '女', description: '智齿发炎' } },
        customPatientProfile: { emotion: '烦躁' }
      })
    });
    page.sessionId = 's1';
    page.mistakeKey = 'violation-3-1';
    page.loadContext();
    await tick();
    expect('mistake-retrain/patient question passthrough', page.data.patientQuestion, '我这牙疼是不是拔了就好了？');
    expect('mistake-retrain/original answer passthrough', page.data.originalAnswer, '拔了就好了。');
    expect('mistake-retrain/scenario age merged', page.data.scenario.patientAge, '35岁');
    expect('mistake-retrain/custom emotion overrides default', page.data.scenario.patientEmotion, '烦躁');
    expect('mistake-retrain/concern from scenario profile', page.data.scenario.patientConcern, '智齿发炎');
    expect('mistake-retrain/mastered flag initialised', page.data.mastered, false);
  }

  /* ---------- 输出 ---------- */
  const failed = results.filter(item => !item.pass);
  for (const item of failed) {
    console.error(`FAIL ${item.name}: wanted ${JSON.stringify(item.wanted)}, got ${JSON.stringify(item.actual)}`);
  }
  if (failed.length) {
    console.error(`\n${failed.length}/${results.length} case(s) failed.`);
    process.exit(1);
  }
  console.log(JSON.stringify({ result: 'passed', cases: results.length }));
};

run().catch(error => {
  console.error('HARNESS ERROR: ' + (error && error.stack ? error.stack.split('\n').slice(0, 3).join(' | ') : error));
  process.exit(2);
});
