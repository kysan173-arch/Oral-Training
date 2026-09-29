const assert = require('assert');
const fs = require('fs');
const path = require('path');
const root = path.resolve(__dirname, '../..');
const api = require(path.join(root, 'utils/api.js'));
const capture = relative => {
  let definition;
  global.Page = value => { definition = value; };
  delete require.cache[require.resolve(path.join(root, relative))];
  require(path.join(root, relative));
  const page = Object.assign({}, definition, { data: JSON.parse(JSON.stringify(definition.data)) });
  page.setData = function (values, callback) { Object.assign(this.data, values); if (callback) callback(); };
  return page;
};
global.wx = { showToast() {}, getStorageSync() { return ''; } };
(async () => {
  const originalMetadata = { origin: 'reference', verification: 'reviewed', trainingScope: 'verified',
    sourceTitle: 'Audit source', sourceUrl: 'https://example.com/audit', sourceLocator: 'p. 1',
    applicability: 'audit scope', effectiveFrom: '2026-01-01', effectiveUntil: '2026-12-31', aliases: [] };
  api.getAdminKnowledgeDraft = async () => ({ draftId: 'audit-draft', draftVersion: 1,
    status: 'active', topic: 'audit', scope: 'general', title: 'Before', body: 'audit body', metadata: originalMetadata });
  api.getAdminKnowledgeRevisions = async () => ({ items: [] });
  let saved;
  api.saveAdminKnowledgeDraft = async (id, payload) => { saved = payload; return { id, draftId: 'audit-draft', draftVersion: 2 }; };
  const editor = capture('pages/knowledge-editor/knowledge-editor.js');
  editor.data.entryId = 'audit-entry';
  await editor.loadDraft();
  editor.data.form.title = 'Only title changed';
  await editor.saveDraft(true);
  assert.deepStrictEqual(saved.metadata, originalMetadata, 'title-only edits preserve provenance and validity');

  const app = JSON.parse(fs.readFileSync(path.join(root, 'app.json'), 'utf8'));
  const tabs = new Set(app.tabBar.list.map(item => '/' + item.pagePath));
  const invalidRoutes = [];
  for (const pageName of fs.readdirSync(path.join(root, 'pages'))) {
    const file = path.join(root, 'pages', pageName, pageName + '.js');
    if (!fs.existsSync(file)) continue;
    const content = fs.readFileSync(file, 'utf8');
    const re = /wx\.switchTab\(\{\s*url:\s*'([^']+)'/g;
    for (const match of content.matchAll(re)) if (!tabs.has(match[1])) {
      invalidRoutes.push({ file: path.relative(root, file), line: content.slice(0, match.index).split('\n').length, url: match[1] });
    }
  }
  assert.deepStrictEqual(invalidRoutes, [], 'switchTab targets must be registered tabs');

  const partialDimensions = { knowledgeAccuracy: null, medicalCompliance: 80, empathy: 80, needsDiscovery: 80, serviceEtiquette: 80 };
  api.getLearningProfile = async () => ({ dimensionAverages: partialDimensions, trend: [], mistakes: {} });
  const profile = capture('pages/profile/profile.js');
  profile.loadProfile();
  await Promise.resolve(); await Promise.resolve();
  const adminPage = capture('pages/admin/admin.js');
  adminPage.applySupervisor({ dimensionAverages: partialDimensions, scenarioStats: [], trend: [],
    averageScore: null, passRate: null, studentCount: 1, totalSessions: 1 }, { members: [] });
  assert.strictEqual(profile.data.dimensions[0].score, null);
  assert.strictEqual(profile.data.dimensions[0].weakest, false);
  assert.strictEqual(adminPage.data.supervisor.dimensionAverages[0].value, null);
  assert(!adminPage.data.teamWarnings.some(item => item.title.includes('知识准确性')));
  partialDimensions.knowledgeAccuracy = 0;
  profile.loadProfile();
  await Promise.resolve(); await Promise.resolve();
  assert.strictEqual(profile.data.dimensions[0].score, 0);
  assert.strictEqual(profile.data.dimensions[0].weakest, true);
  adminPage.applySupervisor({ dimensionAverages: partialDimensions }, { members: [] });
  assert(adminPage.data.teamWarnings.some(item => item.title.includes('知识准确性')));

  // Canvas must not turn a missing dimension into a central zero-score point.
  let component;
  global.Component = definition => { component = definition; };
  require('../../components/radar-chart/radar-chart.js');
  const labels = [];
  let points = 0;
  const ctx = new Proxy({}, { get(target, key) {
    if (key === 'fillText') return text => labels.push(text);
    if (key === 'arc') return () => { points += 1; };
    return () => {};
  } });
  component.methods.render.call({ properties: { compare: [null, 80, 80, 80, 80] } }, ctx, 300, 260,
    [{ name: '知识', score: null }, { name: '合规', score: 80 }, { name: '共情', score: 80 },
      { name: '需求', score: 80 }, { name: '礼仪', score: 80 }]);
  assert(labels.includes('未评估'));
  assert(!labels.includes('0'));
  assert.strictEqual(points, 4);

  const serviceEditor = capture('pages/service-editor/service-editor.js');
  serviceEditor.data.form.dataOrigin = 'manual';
  assert.strictEqual(serviceEditor.buildPayload().dataOrigin, 'manual');

  let resolveSession;
  api.getRoleplaySession = () => new Promise(resolve => { resolveSession = resolve; });
  const roleplay = capture('pages/roleplay/roleplay.js');
  roleplay.sessionId = 'audit-old-session';
  let redirectAfterUnload;
  global.wx.redirectTo = options => { redirectAfterUnload = options.url; };
  roleplay.pollPendingReply('audit-message', 'test', Date.now(), 1000);
  roleplay.onUnload();
  resolveSession({ session: { status: 'completed', currentRound: 10, maxRounds: 10 }, messages: [], pendingMessage: null });
  await Promise.resolve(); await Promise.resolve();
  assert.strictEqual(redirectAfterUnload, undefined, 'unloaded polling must not navigate');
  assert.strictEqual(roleplay.pendingPollTimer, null);

  // Returning to the same page must not reactivate a previous visibility epoch.
  const resumed = capture('pages/roleplay/roleplay.js');
  resumed.sessionId = 'resumed';
  resumed.pollPendingReply('message', 'question', Date.now(), 1000);
  const oldResolve = resolveSession;
  resumed.onHide();
  let reloads = 0;
  resumed.loadSession = () => { reloads += 1; };
  resumed.onShow();
  oldResolve({ session: { status: 'completed' }, messages: [] });
  await Promise.resolve(); await Promise.resolve();
  assert.strictEqual(reloads, 1);
  assert.strictEqual(redirectAfterUnload, undefined);
  assert.strictEqual(resumed.pendingPollTimer, null);
  console.log('Audit client regression tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
