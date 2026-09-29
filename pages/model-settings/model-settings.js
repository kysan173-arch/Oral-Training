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
    revision: 0
  },

  onShow() { return this.loadSettings(); },
  onHide() { this.setData({ apiKey: '' }); },
  onUnload() { this.setData({ apiKey: '' }); },

  applySettings(settings) {
    this.setData({
      canEdit: settings.canEdit === true,
      configured: settings.configured === true,
      hasApiKey: settings.hasApiKey === true,
      baseUrl: settings.baseUrl || '',
      model: settings.model || '',
      revision: settings.revision,
      apiKey: ''
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
    if (!this.data.canEdit || this.data.saving || !['baseUrl', 'model', 'apiKey'].includes(field)) return;
    this.setData({ [field]: event.detail.value, notice: '', error: '' });
  },

  saveSettings() {
    if (!this.data.canEdit || this.data.saving || this.data.loading) return Promise.resolve();
    const payload = {
      baseUrl: this.data.baseUrl.trim().replace(/\/+$/, ''),
      model: this.data.model.trim(),
      apiKey: this.data.apiKey.trim(),
      revision: this.data.revision
    };
    if (!/^https?:\/\/\S+$/.test(payload.baseUrl) || !payload.model || (!this.data.hasApiKey && !payload.apiKey)) {
      this.setData({ error: '请填写网关地址、模型名称和 API Key' });
      return Promise.resolve();
    }
    this.setData({ saving: true, error: '', notice: '' });
    return api.saveModelSettings(payload).then(settings => {
      this.applySettings(settings);
      this.setData({ notice: '个人配置已保存，你后续的训练将使用此配置。保存不会发起模型调用。' });
      wx.showToast({ title: '配置已保存', icon: 'success' });
    }).catch(error => {
      this.setData({ error: error.message || '保存失败，请重试' });
      if (error.code === 'MODEL_CONFIG_CONFLICT') this.setData({ canEdit: false });
    }).finally(() => this.setData({ saving: false, apiKey: '' }));
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
