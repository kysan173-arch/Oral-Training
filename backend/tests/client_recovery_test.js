const assert = require('assert');
const path = require('path');
const { resultStateAction } = require('../../utils/result-state.js');
const {
  DEFAULT_REQUEST_TIMEOUT,
  MODEL_REQUEST_TIMEOUT
} = require('../../utils/request-policy.js');

assert.strictEqual(resultStateAction('ready', 'completed'), 'ready');
assert.strictEqual(resultStateAction('failed', 'completed'), 'failed');
assert.strictEqual(resultStateAction('generating', 'completed'), 'poll');
assert.strictEqual(resultStateAction('not_started', 'completed'), 'recover-generation');
assert.strictEqual(resultStateAction('not_started', 'in_progress'), 'return-to-session');
assert.strictEqual(resultStateAction('not_started', 'abandoned'), 'return-to-history');
assert.strictEqual(resultStateAction('unknown', 'completed'), 'poll');
assert.strictEqual(DEFAULT_REQUEST_TIMEOUT, 30000);
assert.strictEqual(MODEL_REQUEST_TIMEOUT, 120000);
assert.ok(MODEL_REQUEST_TIMEOUT > DEFAULT_REQUEST_TIMEOUT);

const loadPage = relativePath => {
  let definition = null;
  global.Page = value => { definition = value; };
  const filename = require.resolve(path.join(__dirname, '..', '..', relativePath));
  delete require.cache[filename];
  require(filename);
  assert.ok(definition, `${relativePath} did not register a page`);
  return definition;
};

const instantiatePage = definition => {
  const instance = Object.assign({}, definition);
  instance.data = Object.assign({}, definition.data);
  instance.setData = values => Object.assign(instance.data, values);
  return instance;
};

const missingSessionCases = [
  ['pages/training/training.js', '/pages/index/index'],
  ['pages/roleplay/roleplay.js', '/pages/index/index'],
  ['pages/result/result.js', '/pages/report/report'],
  ['pages/roleplay-result/roleplay-result.js', '/pages/report/report']
];

for (const [pagePath, expectedUrl] of missingSessionCases) {
  let modal = null;
  let destination = null;
  global.wx = {
    showModal: options => { modal = options; },
    switchTab: options => { destination = options.url; }
  };
  const page = instantiatePage(loadPage(pagePath));
  page.onLoad({});
  assert.ok(modal && modal.showCancel === false, `${pagePath} did not explain a missing session`);
  modal.success();
  assert.strictEqual(destination, expectedUrl);
}

const verifyRequestTimeouts = async () => {
  const observedTimeouts = [];
  global.wx = {
    getStorageSync: () => 'test-token',
    request: options => {
      observedTimeouts.push(options.timeout);
      const healthRequest = options.url.endsWith('/health');
      options.success({
        statusCode: healthRequest ? 503 : 200,
        data: { code: 0, data: healthRequest ? { ready: false } : {} }
      });
    }
  };
  const api = require('../../utils/api.js');
  const health = await api.getHealth();
  assert.strictEqual(health.ready, false);
  await api.getScenarios();
  await api.sendMessage('session-1', 'message-1', '测试消息');
  await api.sendRoleplayMessage('session-2', 'message-2', '测试问题');
  assert.deepStrictEqual(observedTimeouts, [
    DEFAULT_REQUEST_TIMEOUT,
    DEFAULT_REQUEST_TIMEOUT,
    MODEL_REQUEST_TIMEOUT,
    MODEL_REQUEST_TIMEOUT
  ]);

  let evaluationPolls = 0;
  let evaluationRepairs = 0;
  api.getEvaluation = () => {
    evaluationPolls += 1;
    return Promise.resolve({ status: 'not_started' });
  };
  api.finishSession = () => {
    evaluationRepairs += 1;
    return Promise.resolve({});
  };
  const resultPage = instantiatePage(loadPage('pages/result/result.js'));
  resultPage.sessionId = 'session-1';
  resultPage.data.session = { status: 'completed' };
  resultPage.waitStartedAt = Date.now();
  resultPage.schedule = () => {};
  resultPage.pollReport();
  await new Promise(resolve => setImmediate(resolve));
  await new Promise(resolve => setImmediate(resolve));
  assert.strictEqual(evaluationRepairs, 1);
  assert.strictEqual(evaluationPolls, 2);

  let summaryPolls = 0;
  let summaryRepairs = 0;
  api.getRoleplaySummary = () => {
    summaryPolls += 1;
    return Promise.resolve({ status: 'not_started' });
  };
  api.finishRoleplaySession = () => {
    summaryRepairs += 1;
    return Promise.resolve({});
  };
  const summaryPage = instantiatePage(loadPage('pages/roleplay-result/roleplay-result.js'));
  summaryPage.sessionId = 'session-2';
  summaryPage.data.session = { status: 'completed' };
  summaryPage.waitStartedAt = Date.now();
  summaryPage.schedule = () => {};
  summaryPage.pollSummary();
  await new Promise(resolve => setImmediate(resolve));
  await new Promise(resolve => setImmediate(resolve));
  assert.strictEqual(summaryRepairs, 1);
  assert.strictEqual(summaryPolls, 2);
};

const verifyHistorySummaryRefresh = async () => {
  const api = require('../../utils/api.js');
  for (const status of ['generating', 'failed', 'not_started']) {
    let requests = 0;
    api.getEvaluation = () => {
      requests += 1;
      return Promise.resolve(requests === 1 ? { status } : {
        status: 'ready', evaluation: { summary: '已完成', dimensionScores: { empathy: 80 } }
      });
    };
    const page = instantiatePage(loadPage('pages/report/report.js'));
    page.data.sessions = [{ id: 'history-1', status: 'completed', isRoleplay: false }];
    const event = { currentTarget: { dataset: { id: 'history-1' } } };
    page.toggleEvaluation(event);
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(page.data.sessions[0].evaluationDetail.pending, true);
    page.toggleEvaluation(event); // collapse
    page.toggleEvaluation(event); // reopen after the server completed/retried
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(requests, 2, `${status} was incorrectly cached`);
    assert.strictEqual(page.data.sessions[0].evaluationDetail.summary, '已完成');
    page.toggleEvaluation(event);
    page.toggleEvaluation(event);
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(requests, 2, 'ready summaries should remain cached');
  }
};

verifyRequestTimeouts().then(verifyHistorySummaryRefresh).then(() => {
  console.log('client recovery tests passed');
}).catch(error => {
  console.error(error);
  process.exitCode = 1;
});
