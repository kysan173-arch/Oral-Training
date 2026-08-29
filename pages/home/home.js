const api = require('../../utils/api.js');

Page({
  data: {
    dashboard: { totalCount: 0, completedCount: 0, averageScore: 0 },
    currentUserName: '',
    isAdmin: false,
    dataCaption: '完成训练后自动更新',
    showKeyConfig: false,
    apiKey: '',
    keyStatus: '',
    savingKey: false
  },

  onShow() {
    const currentUser = api.getCurrentUser();
    this.setData({
      currentUserName: currentUser ? currentUser.displayName : '',
      isAdmin: currentUser ? currentUser.role === 'admin' : false
    });
    api.ensureAuthenticated().then(() => {
      const authenticatedUser = api.getCurrentUser();
      this.setData({
        currentUserName: authenticatedUser ? authenticatedUser.displayName : '',
        isAdmin: authenticatedUser ? authenticatedUser.role === 'admin' : false
      });
    }).catch(() => {});
    api.getHealth().then(data => {
      this.setData({ showKeyConfig: data.runtimeApiKeyAllowed === true });
    }).catch(() => this.setData({ showKeyConfig: false }));
    api.getDashboard().then(data => {
      this.setData({ dashboard: {
        totalCount: data.totalSessions,
        completedCount: data.completedSessions,
        averageScore: api.formatScore(data.averageScore)
      }, dataCaption: data.scope === 'institution'
        ? '当前机构汇总，不含个人会话明细'
        : '完成训练后自动更新' });
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

  startTraining() {
    if (this.data.isAdmin) {
      this.viewDashboard();
      return;
    }
    wx.switchTab({ url: '/pages/index/index' });
  },
  viewHistory() { wx.switchTab({ url: '/pages/report/report' }); },
  viewDashboard() { wx.switchTab({ url: '/pages/admin/admin' }); },
  viewPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); },
  viewMistakes() { wx.navigateTo({ url: '/pages/mistakes/mistakes' }); },
  viewProfile() { wx.navigateTo({ url: '/pages/profile/profile' }); },
  viewMine() { wx.switchTab({ url: '/pages/mine/mine' }); }
});
