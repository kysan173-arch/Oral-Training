const api = require('../../utils/api.js');
const { resultStateAction } = require('../../utils/result-state.js');

const scoreFrom = value => {
  const score = Number(value);
  return Number.isFinite(score) ? Math.min(100, Math.max(0, score)) : 0;
};

const dimensionsFrom = (score = {}) => [
  { key: 'empathy', name: '情绪识别与同理心', score: scoreFrom(score.empathy), color: '#667eea' },
  { key: 'knowledgeAccuracy', name: '口腔知识准确性', score: scoreFrom(score.knowledgeAccuracy), color: '#52a67a' },
  { key: 'needsDiscovery', name: '需求挖掘', score: scoreFrom(score.needsDiscovery), color: '#f0a34b' },
  { key: 'serviceEtiquette', name: '服务礼仪', score: scoreFrom(score.serviceEtiquette), color: '#6b9de8' },
  { key: 'medicalCompliance', name: '医疗合规', score: scoreFrom(score.medicalCompliance), color: '#8b75c9' }
];

const totalScoreFrom = (evaluation, sessionTotalScore) => {
  if (evaluation.totalScore !== undefined && evaluation.totalScore !== null) {
    return Math.round(scoreFrom(evaluation.totalScore));
  }
  if (sessionTotalScore !== undefined && sessionTotalScore !== null) {
    return Math.round(scoreFrom(sessionTotalScore));
  }
  const score = evaluation.dimensionScores || {};
  return Math.round(scoreFrom(score.knowledgeAccuracy) * 0.25
    + scoreFrom(score.medicalCompliance) * 0.25
    + scoreFrom(score.empathy) * 0.20
    + scoreFrom(score.needsDiscovery) * 0.20
    + scoreFrom(score.serviceEtiquette) * 0.10);
};

const levelFrom = score => {
  if (score >= 90) return { key: 'excellent', name: '表现出色', note: '沟通与合规边界掌握较好' };
  if (score >= 80) return { key: 'good', name: '表现良好', note: '继续用具体场景巩固表达' };
  if (score >= 60) return { key: 'qualified', name: '达到练习目标', note: '可优先复练薄弱维度' };
  return { key: 'practice', name: '继续复练', note: '建议先查看错题与推荐表达' };
};

const normalizeEvaluation = (evaluation, sessionTotalScore) => Object.assign({}, evaluation, {
  totalScore: totalScoreFrom(evaluation, sessionTotalScore),
  dimensionScores: Object.assign({}, evaluation.dimensionScores || {}),
  strengths: (evaluation.strengths || []).map(item => item.content || item.evidence || item),
  improvements: (evaluation.improvements || []).map(item => item.content || item),
  violations: (evaluation.violations || []).map((item, index) => Object.assign({}, item, {
    id: item.id || `violation-${index}`,
    quote: item.originalQuote || item.quote || '',
    rewrite: item.recommendedRewrite || item.rewrite || ''
  })),
  roundComments: (evaluation.roundComments || []).map(item => Object.assign({}, item, {
    userQuote: item.userMessage || item.userQuote || '',
    rewrite: item.recommendedRewrite || item.rewrite || ''
  })),
  recommendedPhrases: (evaluation.recommendedPhrases || []).map(item => Object.assign({}, item, {
    patientSays: item.patientSays || '',
    csReply: item.csReply || item.recommendedRewrite || '',
    reason: item.reason || item.comment || ''
  }))
});

Page({
  data: {
    session: null,
    scenario: null,
    evaluation: null,
    dimensions: [],
    level: null,
    nextScenario: null,
    loading: true,
    loadingText: '正在生成训练报告…',
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
    Promise.all([api.getSession(this.sessionId), api.getScenarios()]).then(([detail, scenarioData]) => {
      const scenarios = scenarioData.items || [];
      const scenarioIndex = scenarios.findIndex(item => item.id === detail.session.scenarioId);
      const scenario = scenarioIndex >= 0 ? scenarios[scenarioIndex] : { name: detail.session.scenarioName };
      const nextScenario = scenarioIndex >= 0 && scenarios.length > 1
        ? scenarios[(scenarioIndex + 1) % scenarios.length] : null;
      this.setData({ session: detail.session, scenario, nextScenario });
      this.networkRetryIndex = 0;
      this.pollReport();
    }).catch(error => this.handleNetworkError(error, () => this.loadInitialData()));
  },

  pollReport() {
    if (!this.sessionId || !this.data.session) return;
    api.getEvaluation(this.sessionId).then(report => {
      this.networkRetryIndex = 0;
      const action = resultStateAction(report.status, this.data.session.status);
      if (action === 'ready' && report.evaluation) {
        const evaluation = normalizeEvaluation(report.evaluation, this.data.session.totalScore);
        this.setData({ evaluation, dimensions: dimensionsFrom(evaluation.dimensionScores), loading: false,
          level: levelFrom(evaluation.totalScore), retryable: false, timedOut: false });
        return;
      }
      if (action === 'failed') {
        this.setData({ loading: true, loadingText: '报告生成失败，可重新评分', retryable: true, timedOut: false });
        return;
      }
      if (action === 'recover-generation') {
        if (this.stateRecoveryAttempted) {
          if (this.waitExpired()) {
            this.showWaitActions('评分任务暂未启动，你可以继续等待或返回历史记录。');
          } else {
            this.setData({ loading: true, loadingText: '正在等待评分任务启动…', retryable: false });
            this.schedule(() => this.pollReport(), 2000);
          }
          return;
        }
        this.recoverGeneration();
        return;
      }
      if (action === 'return-to-session') {
        this.returnToTraining();
        return;
      }
      if (action === 'return-to-history') {
        this.returnToHistory();
        return;
      }
      if (this.waitExpired()) {
        this.showWaitActions('报告仍在生成，你可以继续等待或返回历史记录。');
        return;
      }
      this.setData({ loading: true, loadingText: '正在生成训练报告…', retryable: false, timedOut: false });
      this.schedule(() => this.pollReport(), 2000);
    }).catch(error => this.handleNetworkError(error, () => this.pollReport()));
  },

  recoverGeneration() {
    if (this.stateRecoveryStarted) return;
    this.stateRecoveryStarted = true;
    this.stateRecoveryAttempted = true;
    this.setData({ loading: true, loadingText: '正在恢复评分任务…', retryable: false, timedOut: false });
    api.finishSession(this.sessionId).then(() => {
      this.stateRecoveryStarted = false;
      this.waitStartedAt = Date.now();
      this.pollReport();
    }).catch(error => {
      this.stateRecoveryStarted = false;
      this.stateRecoveryAttempted = false;
      this.handleNetworkError(error, () => this.recoverGeneration());
    });
  },

  returnToTraining() {
    if (this.stateRecoveryStarted) return;
    this.stateRecoveryStarted = true;
    this.setData({ loading: true, loadingText: '训练尚未结束，正在返回会话…', retryable: false });
    wx.showModal({
      title: '训练尚未结束',
      content: '完成至少一轮对话并结束训练后，才能生成评分报告。',
      showCancel: false,
      success: () => wx.redirectTo({ url: `/pages/training/training?sessionId=${this.sessionId}` })
    });
  },

  returnToHistory() {
    if (this.stateRecoveryStarted) return;
    this.stateRecoveryStarted = true;
    this.setData({ loading: true, loadingText: '该训练无法生成报告', retryable: false });
    wx.showModal({
      title: '无法生成报告',
      content: '该训练已被放弃，请从历史记录选择其他已完成训练。',
      showCancel: false,
      success: () => wx.switchTab({ url: '/pages/report/report' })
    });
  },

  handleMissingSession() {
    this.setData({ loading: true, loadingText: '缺少训练会话信息' });
    wx.showModal({
      title: '无法打开报告',
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
    this.setData({ timedOut: false, loadingText: '继续等待训练报告…' });
    if (this.data.session) this.pollReport();
    else this.loadInitialData();
  },

  retryEvaluation() {
    api.retryEvaluation(this.sessionId).then(() => {
      this.waitStartedAt = Date.now();
      this.networkRetryIndex = 0;
      this.setData({ retryable: false, timedOut: false, loadingText: '正在重新生成报告…' });
      this.pollReport();
    }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  },

  restartTraining() { wx.switchTab({ url: '/pages/index/index' }); },
  viewScenes() { wx.switchTab({ url: '/pages/index/index' }); },
  viewHistory() { wx.switchTab({ url: '/pages/report/report' }); },
  viewPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); },
  viewMistakes() { wx.navigateTo({ url: '/pages/mistakes/mistakes' }); },
  viewProfile() { wx.navigateTo({ url: '/pages/profile/profile' }); },

  startNextScenario() {
    const scenario = this.data.nextScenario;
    if (!scenario) return this.viewScenes();
    if (scenario.activeSession) {
      wx.redirectTo({ url: `/pages/training/training?sessionId=${scenario.activeSession.id}` });
      return;
    }
    api.createSession(scenario.id).then(data => {
      wx.redirectTo({ url: `/pages/training/training?sessionId=${data.session.id}` });
    }).catch(error => wx.showToast({ title: error.message || '创建下一场训练失败', icon: 'none' }));
  }
});
