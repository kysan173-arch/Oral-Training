const assert = require('assert');
const path = require('path');

const apiPath = path.resolve(__dirname, '../../utils/api.js');
const pagePath = path.resolve(__dirname, '../../pages/member-detail/member-detail.js');
const deferred = () => {
  let resolve;
  let reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
};
const flush = () => new Promise(resolve => setImmediate(resolve));
const member = {
  member: { displayName: '测试成员' },
  dimensionAverages: { empathy: 80 },
  trend: [1, 2].map(day => ({ date: `2026-10-0${day}`, totalScore: 80, scores: { empathy: 80 } }))
};

function setup() {
  const detail = deferred();
  const dashboard = deferred();
  const commits = [];
  const notices = [];
  const observers = [];
  global.wx = { showToast: () => {} };
  require.cache[apiPath] = { id: apiPath, filename: apiPath, loaded: true, exports: {
    getSupervisorMember: () => detail.promise,
    getSupervisorDashboard: () => dashboard.promise,
    showCenterNotice: notice => notices.push(notice)
  } };
  let definition;
  global.Page = value => { definition = value; };
  delete require.cache[pagePath];
  require(pagePath);
  const page = Object.assign({}, definition, { data: JSON.parse(JSON.stringify(definition.data)) });
  page.setData = (patch, callback) => {
    Object.assign(page.data, patch);
    if (callback) commits.push(callback);
  };
  page.createIntersectionObserver = () => {
    const observer = {
      relativeToViewport() { return this; },
      observe(selector, callback) { this.selector = selector; this.callback = callback; },
      disconnect() { this.disconnected = true; }
    };
    observers.push(observer);
    return observer;
  };
  page.onLoad({ id: 'member' });
  return { page, detail, dashboard, commits, observers, notices };
}

async function run() {
  // 成员首屏不等看板，雷达数据随首屏一次提交，成长曲线在屏幕外不创建。
  let test = setup();
  test.page.onReady();
  test.detail.resolve(member);
  await flush();
  assert.strictEqual(test.page.data.loading, false);
  assert.strictEqual(test.page.data.detail.member.displayName, '测试成员');
  assert.strictEqual(test.page.data.dimensions[2].score, 80);
  assert.strictEqual(test.page.data.growthChartReady, false);
  assert.strictEqual(test.observers.length, 0);
  // ready 也可能落在 setData 调用与视图提交回调之间。
  test.page.onReady();
  assert.strictEqual(test.observers.length, 0);
  test.commits.shift()();
  assert.strictEqual(test.observers.length, 1);
  assert.strictEqual(test.observers[0].selector, '#growth-chart-slot');
  test.observers[0].callback({ intersectionRatio: 0 });
  assert.strictEqual(test.page.data.growthChartReady, false);
  test.observers[0].callback({ intersectionRatio: 0.1 });
  assert.strictEqual(test.page.data.growthChartReady, true);
  assert.strictEqual(test.observers[0].disconnected, true);
  test.dashboard.reject(new Error('timeout'));
  await flush();
  assert.strictEqual(test.page.data.teamComparisonText, '团队均值暂不可用');
  assert.strictEqual(test.notices.length, 0);
  assert.ok(test.page.data.detail);

  // 快网络早于 ready：先到的团队均值不能被覆盖，观察器等布局提交。
  test = setup();
  test.dashboard.resolve({ dimensionAverages: { empathy: 70 } });
  test.detail.resolve(member);
  await flush();
  test.commits.shift()();
  assert.strictEqual(test.observers.length, 0);
  assert.strictEqual(test.page.data.teamAverages[2], 70);
  test.page.onReady();
  assert.strictEqual(test.observers.length, 1);
  assert.strictEqual(test.page.data.growthChartReady, false);
  // 快速返回后，排队中的可见性事件不能继续创建图表。
  test.page.onUnload();
  test.observers[0].callback({ intersectionRatio: 1 });
  assert.strictEqual(test.page.data.growthChartReady, false);
  assert.strictEqual(test.observers[0].disconnected, true);

  // 不支持观察器的运行环境仍正常显示成长曲线。
  test = setup();
  test.page.createIntersectionObserver = undefined;
  test.detail.resolve(member);
  test.dashboard.resolve({ dimensionAverages: {} });
  await flush();
  test.commits.shift()();
  test.page.onReady();
  assert.strictEqual(test.page.data.growthChartReady, true);

  // 返回上一页后迟到的请求不再更新页面。
  test = setup();
  test.page.onUnload();
  test.detail.resolve(member);
  test.dashboard.resolve({ dimensionAverages: {} });
  await flush();
  assert.strictEqual(test.page.data.detail, null);
  assert.strictEqual(test.commits.length, 0);
  console.log('member detail loading tests passed');
}

run().catch(error => { console.error(error); process.exitCode = 1; });
