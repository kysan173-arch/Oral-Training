const api = require('../../utils/api.js');

const dimensionsFrom = score => [
  { key: 'knowledgeAccuracy', name: '知识准确性', score: score.knowledgeAccuracy || 0, color: '#667eea' },
  { key: 'medicalCompliance', name: '医疗合规', score: score.medicalCompliance || 0, color: '#52a67a' },
  { key: 'empathy', name: '同理心', score: score.empathy || 0, color: '#f0a34b' },
  { key: 'needsDiscovery', name: '需求挖掘', score: score.needsDiscovery || 0, color: '#6b9de8' },
  { key: 'serviceEtiquette', name: '服务礼仪', score: score.serviceEtiquette || 0, color: '#e85d75' }
];

const normalizeEvaluation = evaluation => Object.assign({}, evaluation, {
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
  recommendedPhrases: (evaluation.recommendedPhrases || [])
});

const getLevelInfo = totalScore => {
  if (totalScore >= 90) return { level: '优秀', passed: true, color: '#3a9a69', bg: '#e8f5ed' };
  if (totalScore >= 80) return { level: '良好', passed: true, color: '#52a67a', bg: '#edf6f0' };
  if (totalScore >= 60) return { level: '合格', passed: true, color: '#f0a34b', bg: '#fef7ee' };
  return { level: '待改进', passed: false, color: '#d26464', bg: '#fef0f0' };
};

Page({
  data: {
    session: null,
    scenario: null,
    evaluation: null,
    dimensions: [],
    levelInfo: null,
    nextScenario: null,
    loading: true,
    loadingText: '正在生成训练报告…',
    retryable: false
  },

  sessionId: '',
  pollTimer: null,
  pollCount: 0,

  onLoad(options) {
    this.sessionId = options.sessionId || '';
    this.loadReport();
  },

  onUnload() { if (this.pollTimer) clearTimeout(this.pollTimer); },

  loadReport() {
    if (!this.sessionId) return;
    Promise.all([api.getSession(this.sessionId), api.getEvaluation(this.sessionId), api.getScenarios()]).then(([detail, report, scenarioData]) => {
      const scenario = scenarioData.items.find(item => item.id === detail.session.scenarioId);
      if (report.status === 'ready' && report.evaluation) {
        const evaluation = normalizeEvaluation(report.evaluation);
        const levelInfo = getLevelInfo(evaluation.totalScore);
        const currentIndex = scenarioData.items.findIndex(item => item.id === detail.session.scenarioId);
        const nextScenarios = scenarioData.items.filter((_, i) => i > currentIndex);
        const nextScenario = nextScenarios.length > 0 ? nextScenarios[0] : null;
        this.setData({ session: detail.session, scenario, evaluation, dimensions: dimensionsFrom(evaluation.dimensionScores), levelInfo, nextScenario, loading: false });
        return;
      }
      if (report.status === 'failed') {
        this.setData({ loading: true, loadingText: '报告生成失败，可重新评分', retryable: true });
        return;
      }
      this.setData({ loading: true, loadingText: '正在生成训练报告…', retryable: false });
      if (this.pollCount++ < 15) this.pollTimer = setTimeout(() => this.loadReport(), 2000);
      else wx.showToast({ title: '报告仍在生成，可稍后从历史记录查看', icon: 'none' });
    }).catch(error => wx.showToast({ title: error.message || '报告加载失败', icon: 'none' }));
  },

  retryEvaluation() {
    api.retryEvaluation(this.sessionId).then(() => {
      this.pollCount = 0;
      this.setData({ retryable: false, loadingText: '正在重新生成报告…' });
      this.loadReport();
    }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  },

  restartTraining() { wx.switchTab({ url: '/pages/index/index' }); },
  viewScenes() { wx.switchTab({ url: '/pages/index/index' }); },
  viewHistory() { wx.switchTab({ url: '/pages/report/report' }); },

  goNextScenario() {
    if (!this.data.nextScenario) return;
    api.createSession(this.data.nextScenario.id).then(data => {
      wx.redirectTo({ url: `/pages/training/training?sessionId=${data.session.id}` });
    }).catch(error => wx.showToast({ title: error.message || '创建训练失败', icon: 'none' }));
  }
});
