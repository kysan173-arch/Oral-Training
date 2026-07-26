const api = require('../../utils/api.js');

const dimensionsFrom = score => [
  { key: 'empathy', name: '情绪识别与同理心', score: score.empathy, color: '#667eea' },
  { key: 'knowledgeAccuracy', name: '口腔知识准确性', score: score.knowledgeAccuracy, color: '#52a67a' },
  { key: 'needsDiscovery', name: '需求挖掘', score: score.needsDiscovery, color: '#f0a34b' },
  { key: 'serviceEtiquette', name: '服务礼仪', score: score.serviceEtiquette, color: '#6b9de8' },
  { key: 'medicalCompliance', name: '医疗合规', score: score.medicalCompliance, color: '#8b75c9' }
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
  }))
});

Page({
  data: {
    session: null,
    scenario: null,
    evaluation: null,
    dimensions: [],
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
        this.setData({ session: detail.session, scenario, evaluation, dimensions: dimensionsFrom(evaluation.dimensionScores), loading: false });
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
  viewHistory() { wx.switchTab({ url: '/pages/report/report' }); }
});
