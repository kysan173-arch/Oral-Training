const api = require('../../utils/api.js');

Page({
  data: {
    loading: true,
    saving: false,
    error: '',
    notice: '',
    canEdit: false,
    configured: false,
    hasApiKey: false,
    baseUrl: '',
    model: '',
    apiKey: '',
    revision: 0,
    /* 可选的「微调槽位」：留空即不启用，此时所有功能走主槽（与单实例完全一致）。
       本机部署时通常填第二个实例（如 8002 / sft-v4），用于话术类功能。 */
    tunedConfigured: false,
    hasTunedApiKey: false,
    tunedBaseUrl: '',
    tunedModel: '',
    tunedApiKey: ''
  },

  onShow() { return this.loadSettings(); },
  onHide() { this.setData({ apiKey: '', tunedApiKey: '' }); },
  onUnload() { this.setData({ apiKey: '', tunedApiKey: '' }); },

  applySettings(settings) {
    this.setData({
      canEdit: settings.canEdit === true,
      configured: settings.configured === true,
      hasApiKey: settings.hasApiKey === true,
      baseUrl: settings.baseUrl || '',
      model: settings.model || '',
      revision: settings.revision,
      apiKey: '',
      tunedConfigured: settings.tunedConfigured === true,
      hasTunedApiKey: settings.hasTunedApiKey === true,
      tunedBaseUrl: settings.tunedBaseUrl || '',
      tunedModel: settings.tunedModel || '',
      tunedApiKey: ''
    });
  },

  loadSettings() {
    this.setData({ loading: true, error: '', apiKey: '', canEdit: false });
    return api.getModelSettings().then(settings => {
      this.applySettings(settings);
    }).catch(error => {
      this.setData({ error: error.message || '配置加载失败，请重试' });
    }).finally(() => this.setData({ loading: false }));
  },

  onInput(event) {
    const field = event.currentTarget.dataset.field;
    const editable = ['baseUrl', 'model', 'apiKey', 'tunedBaseUrl', 'tunedModel', 'tunedApiKey'];
    if (!this.data.canEdit || this.data.saving || !editable.includes(field)) return;
    this.setData({ [field]: event.detail.value, notice: '', error: '' });
  },

  saveSettings() {
    if (!this.data.canEdit || this.data.saving || this.data.loading) return Promise.resolve();
    const tunedBaseUrl = this.data.tunedBaseUrl.trim().replace(/\/+$/, '');
    const payload = {
      baseUrl: this.data.baseUrl.trim().replace(/\/+$/, ''),
      model: this.data.model.trim(),
      apiKey: this.data.apiKey.trim(),
      revision: this.data.revision,
      /* 微调槽位：地址留空即表示「不启用」，后端会清空该槽位。 */
      tunedBaseUrl,
      tunedModel: this.data.tunedModel.trim(),
      tunedApiKey: this.data.tunedApiKey.trim()
    };
    if (!/^https?:\/\/\S+$/.test(payload.baseUrl) || !payload.model || (!this.data.hasApiKey && !payload.apiKey)) {
      this.setData({ error: '请填写网关地址、模型名称和 API Key' });
      return Promise.resolve();
    }
    if (tunedBaseUrl && !payload.tunedModel) {
      this.setData({ error: '微调槽位填了网关地址就必须同时填模型名称；若不需要，请把它留空' });
      return Promise.resolve();
    }
    if (tunedBaseUrl && !this.data.hasTunedApiKey && !payload.tunedApiKey) {
      this.setData({ error: '首次配置微调槽位需要填它的 API Key；若不需要，请把它留空' });
      return Promise.resolve();
    }
    this.setData({ saving: true, error: '', notice: '' });
    return api.saveModelSettings(payload).then(settings => {
      this.applySettings(settings);
      this.setData({ notice: '个人配置已保存，你后续的训练将使用此配置。保存不会发起模型调用。' });
      wx.showToast({ title: '已保存', icon: 'success' });
    }).catch(error => {
      this.setData({ error: error.message || '保存失败，请重试' });
      if (error.code === 'MODEL_CONFIG_CONFLICT') this.setData({ canEdit: false });
    }).finally(() => this.setData({ saving: false, apiKey: '', tunedApiKey: '' }));
  },

  clearSettings() {
    if (!this.data.canEdit || !this.data.hasApiKey || this.data.saving) return;
    wx.showModal({
      title: '清除模型配置',
      content: '清除后，你的新模型调用将暂停，直到重新配置；其他账号不受影响。',
      success: result => { if (result.confirm) this.confirmClear(); }
    });
  },

  confirmClear() {
    if (!this.data.canEdit || this.data.saving) return Promise.resolve();
    this.setData({ saving: true, error: '', notice: '', apiKey: '' });
    return api.clearModelSettings(this.data.revision).then(settings => {
      this.applySettings(settings);
      this.setData({ notice: '已清除，请重新填写配置后开始训练。' });
    }).catch(error => {
      this.setData({ error: error.message || '清除失败，请重试' });
      if (error.code === 'MODEL_CONFIG_CONFLICT') this.setData({ canEdit: false });
    }).finally(() => this.setData({ saving: false }));
  }
});
