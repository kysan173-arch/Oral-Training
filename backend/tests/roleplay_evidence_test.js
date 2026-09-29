const assert = require('node:assert/strict');
const api = require('../../utils/api.js');
const { viewSummaryEvidence } = require('../../utils/roleplay-evidence.js');
let modal;
let toast;
let calls = 0;
global.wx = { showModal: data => { modal = data; }, showToast: data => { toast = data; } };
const citation = { traceId: 't1', evidenceId: 'E1', revisionId: 'r1', manifestHash: 'sha256:one' };
api.getRoleplayEvidence = async (session, trace) => {
  calls += 1;
  assert.equal(session, 'session1');
  assert.equal(trace, 't1');
  return { evidence: { manifestHash: 'sha256:one', facts: [
    { evidenceId: 'E1', revisionId: 'r1', displayText: '3980 元起/颗；以检查为准' },
    { evidenceId: 'E2', revisionId: 'r1', displayText: 'unselected' }
  ] } };
};
(async () => {
  await viewSummaryEvidence('session1', citation);
  assert.equal(modal.content, '3980 元起/颗；以检查为准');
  modal = null;
  await viewSummaryEvidence('session1', { ...citation, manifestHash: 'other' });
  assert.equal(modal, null);
  assert.match(toast.title, /版本不匹配/);
  await viewSummaryEvidence('session1', { ...citation, evidenceId: 'missing' });
  assert.equal(modal, null);
  assert.match(toast.title, /未找到/);
  await viewSummaryEvidence('session1', { ...citation, revisionId: 'foreign' });
  assert.equal(modal, null);
  await viewSummaryEvidence('', citation);
  assert.equal(calls, 4);
  console.log('roleplay summary evidence tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
