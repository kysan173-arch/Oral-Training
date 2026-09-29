const assert = require('assert');
const api = require('../../utils/api.js');
let definition;
global.Page = value => { definition = value; };
global.wx = { showToast() {}, showModal() {} };
require('../../pages/model-settings/model-settings.js');
const page = () => Object.assign({}, definition, {
  data: JSON.parse(JSON.stringify(definition.data)),
  setData(values) { Object.assign(this.data, values); }
});

(async () => {
  const settings = { canEdit: true, configured: false, hasApiKey: false, revision: 0 };
  api.getModelSettings = async () => settings;
  const editor = page();
  await editor.onShow();
  editor.onInput({ currentTarget: { dataset: { field: 'baseUrl' } }, detail: { value: 'https://gateway.example/v1' } });
  editor.onInput({ currentTarget: { dataset: { field: 'model' } }, detail: { value: 'ds-primary' } });
  editor.onInput({ currentTarget: { dataset: { field: 'apiKey' } }, detail: { value: 'sk-test-key' } });
  let payload;
  api.saveModelSettings = async value => {
    payload = value;
    return { ...value, apiKey: undefined, canEdit: true, configured: true, hasApiKey: true, revision: 1 };
  };
  await editor.saveSettings();
  assert.strictEqual(payload.apiKey, 'sk-test-key');
  assert.strictEqual(payload.model, 'ds-primary');
  assert.strictEqual(editor.data.apiKey, '', 'secret must be cleared after save');
  assert.strictEqual(editor.data.configured, true);
  await editor.saveSettings();
  assert.strictEqual(payload.apiKey, '', 'blank keeps saved key');
  api.saveModelSettings = async () => { throw Object.assign(new Error('conflict'), { code: 'MODEL_CONFIG_CONFLICT' }); };
  editor.data.apiKey = 'sk-replacement';
  await editor.saveSettings();
  assert.strictEqual(editor.data.canEdit, false, 'stale settings require reload');
  assert.strictEqual(editor.data.apiKey, '');
  settings.canEdit = true;
  const learner = page();
  await learner.onShow();
  let writes = 0;
  api.saveModelSettings = async value => { writes += 1; return { ...value, canEdit: true, configured: true, hasApiKey: true }; };
  learner.onInput({ currentTarget: { dataset: { field: 'baseUrl' } }, detail: { value: 'https://my-gateway.example/v1' } });
  learner.onInput({ currentTarget: { dataset: { field: 'model' } }, detail: { value: 'my-model' } });
  learner.onInput({ currentTarget: { dataset: { field: 'apiKey' } }, detail: { value: 'personal-key' } });
  await learner.saveSettings();
  assert.strictEqual(writes, 1, 'learners can save personal configuration');
  assert.strictEqual(learner.data.apiKey, '');
  settings.canEdit = true;
  await editor.loadSettings();
  editor.data.apiKey = 'not-persisted';
  editor.onHide();
  assert.strictEqual(editor.data.apiKey, '', 'leaving clears secret');
  api.clearModelSettings = async revision => ({ canEdit: true, configured: false, hasApiKey: false, revision: revision + 1 });
  await editor.confirmClear();
  assert.strictEqual(editor.data.configured, false);
  assert.strictEqual(editor.data.baseUrl, '');
  console.log('Model settings client tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
