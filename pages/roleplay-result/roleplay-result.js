const api = require('../../utils/api.js');

Page({
  data: {
    session: null,
    scenario: null,
    summary: null,
    loading: true,
    loadingText: '正在生成学习复盘…',
    retryable: false,
    timedOut: false
  },

  sessionId: '',
  pollTimer: null,
  waitStartedAt: 0,
  networkRetryIndex: 0,

  onLoad(options) {
    this.sessionId = options.sessionId || '';
    this.waitStartedAt = Date.now();
    this.loadInitialData();
  },

  onUnload() { if (this.pollTimer) clearTimeout(this.pollTimer); },

  loadInitialData() {
    if (!this.sessionId) return;
    Promise.all([
      api.getRoleplaySession(this.sessionId),
      api.getRoleplayScenarios()
    ]).then(([detail, scenarioData]) => {
      const scenario = scenarioData.items.find(item => item.id === detail.session.scenarioId) || { name: detail.session.scenarioName };
      this.setData({ session: detail.session, scenario });
      this.networkRetryIndex = 0;
      this.pollSummary();
    }).catch(error => this.handleNetworkError(error, () => this.loadInitialData()));
  },

  pollSummary() {
    if (!this.sessionId || !this.data.session) return;
    api.getRoleplaySummary(this.sessionId).then(summaryData => {
      this.networkRetryIndex = 0;
      if (summaryData.status === 'ready' && summaryData.summary) {
        this.setData({ summary: summaryData.summary, loading: false, retryable: false, timedOut: false });
        return;
      }
      if (summaryData.status === 'failed') {
        this.setData({ loading: true, loadingText: '复盘生成失败，可重新生成', retryable: true, timedOut: false });
        return;
      }
      if (this.waitExpired()) {
        this.showWaitActions('复盘仍在生成，你可以继续等待或返回历史记录。');
        return;
      }
      this.setData({ loading: true, loadingText: '正在生成学习复盘…', retryable: false, timedOut: false });
      this.schedule(() => this.pollSummary(), 2000);
    }).catch(error => this.handleNetworkError(error, () => this.pollSummary()));
  },

  handleNetworkError(error, retry) {
    if (this.waitExpired()) {
      this.showWaitActions('网络暂时不可用，你可以继续等待或返回历史记录。');
      return;
    }
    const delays = [1000, 2000, 4000];
    const delay = delays[Math.min(this.networkRetryIndex, delays.length - 1)];
    this.networkRetryIndex += 1;
    this.setData({ loading: true, loadingText: error.message || '网络异常，正在重试…', retryable: false });
    this.schedule(retry, delay);
  },

  schedule(callback, delay) {
    if (this.pollTimer) clearTimeout(this.pollTimer);
    this.pollTimer = setTimeout(callback, delay);
  },

  waitExpired() { return Date.now() - this.waitStartedAt >= 30000; },

  showWaitActions(message) {
    this.setData({ loading: true, loadingText: message, retryable: false, timedOut: true });
  },

  continueWaiting() {
    this.waitStartedAt = Date.now();
    this.networkRetryIndex = 0;
    this.setData({ timedOut: false, loadingText: '继续等待学习复盘…' });
    if (this.data.session) this.pollSummary();
    else this.loadInitialData();
  },

  retrySummary() {
    api.retryRoleplaySummary(this.sessionId).then(() => {
      this.waitStartedAt = Date.now();
      this.networkRetryIndex = 0;
      this.setData({ retryable: false, timedOut: false, loadingText: '正在重新生成学习复盘…' });
      this.pollSummary();
    }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  },

  viewScenes() { wx.switchTab({ url: '/pages/index/index' }); },
  viewHistory() { wx.navigateTo({ url: '/pages/report/report' }); }
});
