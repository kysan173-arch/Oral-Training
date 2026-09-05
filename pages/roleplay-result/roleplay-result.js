const api = require('../../utils/api.js');
const { resultStateAction } = require('../../utils/result-state.js');

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
  stateRecoveryStarted: false,
  stateRecoveryAttempted: false,

  onLoad(options) {
    this.sessionId = options.sessionId || '';
    if (!this.sessionId) {
      this.handleMissingSession();
      return;
    }
    this.waitStartedAt = Date.now();
    this.loadInitialData();
  },

  onUnload() { if (this.pollTimer) clearTimeout(this.pollTimer); },

  loadInitialData() {
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
      const action = resultStateAction(summaryData.status, this.data.session.status);
      if (action === 'ready' && summaryData.summary) {
        this.setData({ summary: summaryData.summary, loading: false, retryable: false, timedOut: false });
        return;
      }
      if (action === 'failed') {
        this.setData({ loading: true, loadingText: '复盘生成失败，可重新生成', retryable: true, timedOut: false });
        return;
      }
      if (action === 'recover-generation') {
        if (this.stateRecoveryAttempted) {
          if (this.waitExpired()) {
            this.showWaitActions('复盘任务暂未启动，你可以继续等待或返回历史记录。');
          } else {
            this.setData({ loading: true, loadingText: '正在等待复盘任务启动…', retryable: false });
            this.schedule(() => this.pollSummary(), 2000);
          }
          return;
        }
        this.recoverGeneration();
        return;
      }
      if (action === 'return-to-session') {
        this.returnToRoleplay();
        return;
      }
      if (action === 'return-to-history') {
        this.returnToHistory();
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

  recoverGeneration() {
    if (this.stateRecoveryStarted) return;
    this.stateRecoveryStarted = true;
    this.stateRecoveryAttempted = true;
    this.setData({ loading: true, loadingText: '正在恢复复盘任务…', retryable: false, timedOut: false });
    api.finishRoleplaySession(this.sessionId).then(() => {
      this.stateRecoveryStarted = false;
      this.waitStartedAt = Date.now();
      this.pollSummary();
    }).catch(error => {
      this.stateRecoveryStarted = false;
      this.stateRecoveryAttempted = false;
      this.handleNetworkError(error, () => this.recoverGeneration());
    });
  },

  returnToRoleplay() {
    if (this.stateRecoveryStarted) return;
    this.stateRecoveryStarted = true;
    this.setData({ loading: true, loadingText: '患者模拟尚未结束，正在返回会话…', retryable: false });
    wx.showModal({
      title: '患者模拟尚未结束',
      content: '完成至少一轮问答并结束模拟后，才能生成学习复盘。',
      showCancel: false,
      success: () => wx.redirectTo({ url: `/pages/roleplay/roleplay?sessionId=${this.sessionId}` })
    });
  },

  returnToHistory() {
    if (this.stateRecoveryStarted) return;
    this.stateRecoveryStarted = true;
    this.setData({ loading: true, loadingText: '该患者模拟无法生成复盘', retryable: false });
    wx.showModal({
      title: '无法生成复盘',
      content: '该患者模拟已被放弃，请从历史记录选择其他已完成会话。',
      showCancel: false,
      success: () => wx.switchTab({ url: '/pages/report/report' })
    });
  },

  handleMissingSession() {
    this.setData({ loading: true, loadingText: '缺少患者模拟会话信息' });
    wx.showModal({
      title: '无法打开复盘',
      content: '页面链接缺少会话信息，请从历史记录重新进入。',
      showCancel: false,
      success: () => wx.switchTab({ url: '/pages/report/report' })
    });
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
  viewHistory() { wx.switchTab({ url: '/pages/report/report' }); }
});
