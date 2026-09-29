const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const path = require('path');
let component;
let request;
let modal;
vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../../components/knowledge-report/knowledge-report.js'), 'utf8'), {
  Component: value => { component = value; },
  require: () => ({ getTrainingEvidence: (sessionId, traceId) => {
    request = { sessionId, traceId };
    return Promise.resolve({ citations: [{ text: '3980 元起/颗；检查后确认' }] });
  } }),
  wx: { showModal: value => { modal = value; }, showToast() {} }
});
const instance = { properties: { sessionId: 'session-owner' }, setData(value) { this.data = value; } };
component.observers.report.call(instance, { schemaVersion: 2, knowledgeAssessment: { coverage: null }, knowledgeChecks: [
  { verdict: 'evidence_missing', originalQuote: '费用多少', evidenceRefs: [] }
] });
assert.equal(instance.data.coverageText, '暂无可核验项');
assert.equal(instance.data.checks[0].verdictText, '依据不足');
component.observers.report.call(instance, { knowledgeAssessment: { coverage: 0.5 }, knowledgeChecks: [] });
assert.equal(instance.data.coverageText, '50%');
component.methods.viewEvidence.call(instance, { currentTarget: { dataset: { trace: 'trace-owner' } } });
Promise.resolve().then(() => {
  assert.equal(request.sessionId, 'session-owner');
  assert.equal(request.traceId, 'trace-owner');
  assert(modal.content.includes('检查后确认'));
  console.log('knowledge report client tests passed');
});
