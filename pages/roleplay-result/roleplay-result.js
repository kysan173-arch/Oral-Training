const api = require('../../utils/api.js');

Page({
  data: {
    session: null,
    scenario: null,
    summary: null,
    loading: true,
    loadingText: '正在生成学习复盘…',
    retryable: false
  },

  sessionId: '',
  pollTimer: null,
  pollCount: 0,

  onLoad(options) {
    this.sessionId = options.sessionId || '';
    this.loadSummary();
  },

  onUnload() { if (this.pollTimer) clearTimeout(this.pollTimer); },

  loadSummary() {
    if (!this.sessionId) return;
    Promise.all([
      api.getRoleplaySession(this.sessionId),
      api.getRoleplaySummary(this.sessionId),
      api.getRoleplayScenarios()
    ]).then(([detail, summaryData, scenarioData]) => {
      const scenario = scenarioData.items.find(item => item.id === detail.session.scenarioId);
      if (summaryData.status === 'ready' && summaryData.summary) {
        this.setData({ session: detail.session, scenario, summary: summaryData.summary, loading: false, retryable: false });
        return;
      }
      if (summaryData.status === 'failed') {
        this.setData({ session: detail.session, scenario, loading: true, loadingText: '复盘生成失败，可重新生成', retryable: true });
        return;
      }
      this.setData({ session: detail.session, scenario, loading: true, loadingText: '正在生成学习复盘…', retryable: false });
      if (this.pollCount++ < 15) this.pollTimer = setTimeout(() => this.loadSummary(), 2000);
      else wx.showToast({ title: '复盘仍在生成，可稍后从历史记录查看', icon: 'none' });
    }).catch(error => wx.showToast({ title: error.message || '复盘加载失败', icon: 'none' }));
  },

  retrySummary() {
    api.retryRoleplaySummary(this.sessionId).then(() => {
      this.pollCount = 0;
      this.setData({ retryable: false, loadingText: '正在重新生成学习复盘…' });
      this.loadSummary();
    }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  },

  viewScenes() { wx.switchTab({ url: '/pages/index/index' }); },
  viewHistory() { wx.switchTab({ url: '/pages/report/report' }); }
});
