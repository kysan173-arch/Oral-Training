const api = require('../../utils/api.js');
const { normalizeScenario } = require('../../utils/patient-profile.js');

Page({
  data: {
    loading: true,
    loadError: '',
    context: null,
    scenario: null,
    patientQuestion: '',
    originalAnswer: '',
    inputValue: '',
    submitting: false,
    result: null,
    marking: false,
    mastered: false
  },

  sessionId: '',
  mistakeKey: '',

  onLoad(options) {
    this.sessionId = options.sessionId || '';
    this.mistakeKey = options.mistakeKey || '';
    this.loadContext();
  },

  loadContext() {
    if (!this.sessionId || !this.mistakeKey) {
      this.setData({ loading: false, loadError: '缺少错题参数，请从错题本重新进入。' });
      return;
    }
    api.getMistakeRetrainContext(this.sessionId, this.mistakeKey).then(data => {
      this.setData({
        loading: false,
        context: data,
        scenario: normalizeScenario(data.scenario, data.customPatientProfile),
        patientQuestion: data.patientQuestion || '',
        originalAnswer: data.originalAnswer || '',
        mastered: !!(data.mistake && data.mistake.mastered)
      });
    }).catch(error => {
      this.setData({ loading: false, loadError: error.message || '错题加载失败，请稍后重试。' });
    });
  },

  onInputChange(e) { this.setData({ inputValue: e.detail.value }); },

  submit() {
    const content = this.data.inputValue.trim();
    if (!content || this.data.submitting || this.data.result) return;
    this.setData({ submitting: true });
    api.retrainMistake(this.sessionId, this.mistakeKey, content).then(result => {
      this.setData({ submitting: false, result });
    }).catch(error => {
      this.setData({ submitting: false });
      wx.showToast({ title: error.message || '点评生成失败，请稍后重试', icon: 'none' });
    });
  },

  copyRewrite() {
    if (!this.data.result || !this.data.result.recommendedRewrite) return;
    wx.setClipboardData({ data: this.data.result.recommendedRewrite });
  },

  markMastered() {
    if (this.data.marking) return;
    this.setData({ marking: true });
    api.setLearningMistakeMastery(this.sessionId, this.mistakeKey, true).then(() => {
      wx.showToast({ title: '已标记掌握', icon: 'success' });
      this.setData({ mastered: true });
    }).catch(error => {
      wx.showToast({ title: error.message || '状态更新失败', icon: 'none' });
    }).finally(() => this.setData({ marking: false }));
  },

  goBack() { wx.navigateBack(); }
});
