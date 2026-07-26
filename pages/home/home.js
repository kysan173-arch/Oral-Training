const api = require('../../utils/api.js');

Page({
  data: {
    dashboard: { totalCount: 0, completedCount: 0, averageScore: 0 },
    demoUser: '固定演示账号',
    apiKey: '',
    keyStatus: '',
    savingKey: false
  },

  onShow() {
    api.getDashboard().then(data => {
      this.setData({ dashboard: {
        totalCount: data.totalSessions,
        completedCount: data.completedSessions,
        averageScore: data.averageScore
      } });
    }).catch(error => this.showRequestError(error));
  },

  onKeyInput(e) { this.setData({ apiKey: e.detail.value, keyStatus: '' }); },

  saveApiKey() {
    const apiKey = this.data.apiKey.trim();
    if (!apiKey || this.data.savingKey) return;
    this.setData({ savingKey: true });
    api.setDeepSeekKey(apiKey).then(() => {
      this.setData({ apiKey: '', keyStatus: '已配置到当前后端进程' });
      wx.showToast({ title: '模型密钥已配置', icon: 'success' });
    }).catch(error => this.showRequestError(error)).finally(() => this.setData({ savingKey: false }));
  },

  showRequestError(error) {
    wx.showToast({ title: error.message || '后端服务不可用', icon: 'none' });
  },

  startTraining() { wx.switchTab({ url: '/pages/index/index' }); },
  viewHistory() { wx.switchTab({ url: '/pages/report/report' }); },
  viewDashboard() { wx.switchTab({ url: '/pages/admin/admin' }); }
});
