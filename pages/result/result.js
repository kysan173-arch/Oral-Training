const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');
const { resultStateAction } = require('../../utils/result-state.js');

/* 分数分档：颜色只跟随分数（≥80 良好绿 / 60–79 中间蓝 / <60 待提升橙） */
const scoreTier = score => (score >= 80 ? 'high' : score >= 60 ? 'mid' : 'low');

const scoreFrom = value => {
  if (value === null || value === undefined || value === '') return null;
  const score = Number(value);
  return Number.isFinite(score) ? Math.min(100, Math.max(0, score)) : null;
};

const dimensionsFrom = score => [
  { key: 'empathy', name: '情绪识别与同理心', score: score.empathy },
  { key: 'knowledgeAccuracy', name: '口腔知识准确性', score: score.knowledgeAccuracy },
  { key: 'needsDiscovery', name: '需求挖掘', score: score.needsDiscovery },
  { key: 'serviceEtiquette', name: '服务礼仪', score: score.serviceEtiquette },
  { key: 'medicalCompliance', name: '医疗合规', score: score.medicalCompliance }
].map(item => Object.assign(item, { tier: scoreTier(item.score) }));

const totalScoreFrom = (evaluation, sessionTotalScore) => {
  if (evaluation.totalScore !== undefined && evaluation.totalScore !== null) {
    return Math.round(scoreFrom(evaluation.totalScore));
  }
  if (evaluation.schemaVersion === 2) return null;
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

/* 总分环档位色（与全局语义色 token 一致）；score 为 null 时走 unscored 档 */
const levelFrom = score => {
  if (score === null) return { key: 'unscored', name: '暂不形成综合分', note: '知识依据不足，可继续查看其余维度点评', scoreColor: '#1F3864' };
  if (score >= 90) return { key: 'excellent', name: '表现出色', note: '沟通与合规边界掌握较好', scoreColor: '#2E8B6C' };
  if (score >= 80) return { key: 'good', name: '表现良好', note: '继续用具体场景巩固表达', scoreColor: '#2E8B6C' };
  if (score >= 60) return { key: 'qualified', name: '达到练习目标', note: '可优先复练薄弱维度', scoreColor: '#B97A1E' };
  return { key: 'practice', name: '继续复练', note: '建议先查看错题与推荐表达', scoreColor: '#C2554A' };
};

const normalizeEvaluation = (evaluation, sessionTotalScore) => {
  const totalScore = totalScoreFrom(evaluation, sessionTotalScore);
  return Object.assign({}, evaluation, {
    totalScore,
    hasTotalScore: totalScore !== null,
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
};

/* WXML 不能对数据路径调用函数（依赖追踪失效且不报错），
   折叠时要显示的条目在 JS 里预算成字段。 */
const foldList = (list, expanded) => (expanded ? (list || []) : (list || []).slice(0, 1));

Page({
  data: {
    session: null,
    scenario: null,
    evaluation: null,
    dimensions: [],
    level: null,
    nextScenario: null,
    scorePercent: 0,
    scoreText: '—',
    loading: true,
    loadingText: '正在生成训练报告…',
    retryable: false,
    timedOut: false,
    violationsExpanded: false,
    roundCommentsExpanded: false,
    visibleViolations: [],
    visibleRoundComments: []
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
    if (!this.sessionId) return;
    Promise.all([api.getSession(this.sessionId), api.getScenarios()]).then(([detail, scenarioData]) => {
      const scenarios = scenarioData.items || [];
      const scenarioIndex = scenarios.findIndex(item => item.id === detail.session.scenarioId);
      const scenario = scenarioIndex >= 0 ? scenarios[scenarioIndex] : { name: detail.session.scenarioName };
      const nextScenario = scenarioIndex >= 0 && scenarios.length > 1
        ? scenarios[(scenarioIndex + 1) % scenarios.length] : null;
      /* 起止时间在 JS 预算成展示文本，WXML 里不调用函数 */
      const sessionView = Object.assign({}, detail.session, {
        rangeText: datetime.formatRange(detail.session.startedAt, detail.session.finishedAt)
      });
      this.setData({ session: sessionView, scenario, nextScenario });
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
        const hasTotalScore = evaluation.totalScore !== null;
        const scorePercent = hasTotalScore ? Math.max(0, Math.min(100, evaluation.totalScore)) : 0;
        const scoreText = hasTotalScore ? String(evaluation.totalScore) : '—';
        this.setData({
          evaluation,
          dimensions: dimensionsFrom(evaluation.dimensionScores),
          loading: false,
          level: levelFrom(evaluation.totalScore),
          retryable: false,
          timedOut: false,
          scorePercent,
          scoreText,
          visibleViolations: foldList(evaluation.violations, this.data.violationsExpanded),
          visibleRoundComments: foldList(evaluation.roundComments, this.data.roundCommentsExpanded)
        });
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
  viewMistakes() { wx.navigateTo({ url: '/pages/mistakes/mistakes' }); },
  viewPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); },
  viewProfile() { wx.navigateTo({ url: '/pages/profile/profile' }); },

  toggleViolations() {
    const violationsExpanded = !this.data.violationsExpanded;
    const evaluation = this.data.evaluation;
    this.setData({
      violationsExpanded,
      visibleViolations: foldList(evaluation && evaluation.violations, violationsExpanded)
    });
  },

  toggleRoundComments() {
    const roundCommentsExpanded = !this.data.roundCommentsExpanded;
    const evaluation = this.data.evaluation;
    this.setData({
      roundCommentsExpanded,
      visibleRoundComments: foldList(evaluation && evaluation.roundComments, roundCommentsExpanded)
    });
  },

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
