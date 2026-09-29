const assert = require('assert');
const api = require('../../utils/api.js');
const starts = require('../../utils/session-start.js');
const createRequest = api.createSession;
const storage = new Map();
let navigation = [];
let timers = new Map();
let timerId = 0;
global.setTimeout = fn => { timers.set(++timerId, fn); return timerId; };
global.clearTimeout = id => timers.delete(id);
global.wx = {
  getStorageSync: key => storage.get(key), setStorageSync: (key, value) => storage.set(key, value),
  removeStorageSync: key => storage.delete(key), showToast() {}, showModal() {},
  navigateTo: options => { navigation.push(options.url); }, switchTab() {}
};
let user = { id: 'u1', role: 'learner' };
api.getCurrentUser = () => user;
api.ensureAuthenticated = () => Promise.resolve();
const flush = async () => { for (let i = 0; i < 12; i++) await Promise.resolve(); };
const deferred = () => { let resolve; const promise = new Promise(r => { resolve = r; }); return { promise, resolve }; };
const page = file => {
  let definition;
  global.Page = value => { definition = value; };
  delete require.cache[require.resolve('../../' + file)];
  require('../../' + file);
  const value = Object.assign({}, definition, { data: JSON.parse(JSON.stringify(definition.data)) });
  value.setData = (patch, callback) => { Object.assign(value.data, patch); if (callback) callback(); };
  return value;
};
const detail = (status, id = 's1', version = 2) => ({
  session: { id, scenarioId: 'sc', scenarioName: '场景', status: 'in_progress', contextVersion: version,
    initializationStatus: status, publicProfile: { displayName: '李女士', ageRange: '30-39' }, currentRound: 0, maxRounds: 10 },
  messages: [], patientState: { emotion: '犹豫' }
});
(async () => {
  storage.set('oralTrainingAccessToken', 'expired');
  const bodies = [];
  wx.login = options => options.success({ code: 'wx-code' });
  wx.request = options => {
    if (options.url.endsWith('/auth/wechat')) {
      options.success({ statusCode: 200, data: { code: 0, data: { accessToken: 'new', user: { id: 'u1' } } } });
      return;
    }
    bodies.push(options.data);
    options.success(bodies.length === 1
      ? { statusCode: 401, data: { code: 'AUTH_EXPIRED', message: 'expired' } }
      : { statusCode: 202, data: { code: 0, data: { session: { id: 'auth-created' } } } });
  };
  await createRequest('sc', undefined, { serviceId: 'a', clientSessionId: 'stable-auth' });
  assert.strictEqual(bodies.length, 2);
  assert.deepStrictEqual(bodies[0], bodies[1]);
  assert.ok(!Object.prototype.hasOwnProperty.call(bodies[0], 'customPatientProfile'));

  const first = starts.intent('customer_service', 'a', 'sc');
  assert.strictEqual(starts.intent('customer_service', 'a', 'sc').clientSessionId, first.clientSessionId);
  assert.notStrictEqual(starts.intent('patient_simulation', 'a', 'sc').key, first.key);
  assert.notStrictEqual(starts.intent('customer_service', 'b', 'sc').key, first.key);
  user = { id: 'u2' }; assert.notStrictEqual(starts.intent('customer_service', 'a', 'sc').key, first.key); user = { id: 'u1' };
  const index = page('pages/index/index.js'); index._visible = true;
  api.getServices = () => Promise.resolve({ items: [] });
  await index.loadServices(); assert.strictEqual(index.data.scenarios.length, 0); assert.strictEqual(index.data.legacyMode, false);
  const old = deferred(); const fresh = deferred();
  api.getScenarios = id => id === 'a' ? old.promise : fresh.promise;
  index.data.selectedServiceId = 'a'; index.loadScenarios();
  index.data.selectedServiceId = 'b'; index.loadScenarios();
  fresh.resolve({ items: [{ id: 'b-sc', name: 'B' }] }); await flush();
  old.resolve({ items: [{ id: 'a-sc', name: 'A' }] }); await flush();
  assert.strictEqual(index.data.scenarios[0].id, 'b-sc');
  const late = deferred(); api.getServices = () => late.promise;
  index.loadServices(); index.onHide(); late.resolve({ items: [{ id: 'late' }] }); await flush();
  assert.notStrictEqual(index.data.selectedServiceId, 'late');
  index._visible = true; index.data.catalogLoading = false; index.data.selectedServiceId = 'a';
  let calls = []; const creation = deferred();
  api.createSession = (id, profile, options) => { calls.push(options); return calls.length === 1 ? creation.promise : Promise.resolve({ session: { id: 'created', status: 'in_progress' } }); };
  index.startServiceSession({ id: 'sc' }); index.startServiceSession({ id: 'sc' }); await flush();
  assert.strictEqual(calls.length, 1);
  index.onHide(); creation.resolve({ session: { id: 'created', status: 'in_progress' } }); await flush();
  assert.strictEqual(navigation.length, 0);
  index._visible = true; await index.startServiceSession({ id: 'sc' });
  assert.strictEqual(calls[0].clientSessionId, calls[1].clientSessionId);
  assert.ok(navigation[0].includes('sessionId=created'));
  const training = page('pages/training/training.js'); training.sessionId = 's1'; training._visible = true;
  api.getSession = () => Promise.resolve(detail('pending'));
  api.getScenarios = () => { throw Error('v2 must not need current scenario catalog'); };
  await training.loadSession(); assert.strictEqual(training.data.interactionBlocked, true); assert.strictEqual(timers.size, 1);
  let exitOptions; wx.showActionSheet = options => { exitOptions = options; };
  training.exitTraining();
  assert.strictEqual(exitOptions.itemList.length, 2);
  assert.ok(exitOptions.itemList.some(label => label.startsWith('暂存并退出')));
  let sends = 0; api.sendMessage = () => { sends++; }; api.requestTrainingHint = () => { sends++; }; api.finishSession = () => { sends++; };
  training.data.inputValue = 'hello'; training.sendMessage(); training.requestHint(); training.completeTraining(); assert.strictEqual(sends, 0);
  training.onHide(); assert.strictEqual(timers.size, 0);
  const response = deferred(); api.getSession = () => response.promise;
  training._visible = true; const loading = training.loadSession(); training.onHide();
  response.resolve(detail('ready')); await loading; assert.strictEqual(training.data.initializationStatus, 'pending');
  api.getSession = () => Promise.resolve(detail('failed')); training.onShow(); await flush();
  assert.strictEqual(training.data.initializationStatus, 'failed');
  let retries = 0; api.retryPatientInitialization = () => { retries++; return Promise.resolve(); };
  training.retryInitialization(); training.retryInitialization(); await flush(); assert.strictEqual(retries, 1);
  api.getSession = () => Promise.resolve(detail('ready')); await training.loadSession();
  assert.strictEqual(training.data.interactionBlocked, false); assert.strictEqual(training.data.publicProfile.displayName, '李女士');
  const network = api.getSession; api.getSession = () => Promise.reject(new Error('登录过期'));
  await training.loadSession(); assert.ok(training.data.loadError); assert.strictEqual(training.data.interactionBlocked, true);
  api.getSession = network; await training.loadSession(); assert.strictEqual(training.data.loadError, '');
  api.getSession = () => Promise.resolve(detail('', 'legacy', 1));
  api.getScenarios = () => Promise.resolve({ items: [{ id: 'sc', name: '旧版', patientProfile: {} }] });
  await training.loadSession(); assert.strictEqual(training.data.contextVersion, 1); assert.strictEqual(training.data.interactionBlocked, false);
  training.onUnload();
  console.log('patient client tests passed: durable intent, mode/service/user isolation, stale catalog/create/load, initialization gates, retry, resume, legacy');
})().catch(error => { console.error(error); process.exitCode = 1; });
