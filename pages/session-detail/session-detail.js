const api = require('../../utils/api.js');

/* 分数分档：颜色只跟随分数 */
const scoreTier = score => (score >= 80 ? 'high' : score >= 60 ? 'mid' : 'low');

const dimensionsFrom = score => [
  { key: 'empathy', name: '情绪识别与同理心', score: score.empathy || 0 },
  { key: 'knowledgeAccuracy', name: '口腔知识准确性', score: score.knowledgeAccuracy || 0 },
  { key: 'needsDiscovery', name: '需求挖掘', score: score.needsDiscovery || 0 },
  { key: 'serviceEtiquette', name: '服务礼仪', score: score.serviceEtiquette || 0 },
  { key: 'medicalCompliance', name: '医疗合规', score: score.medicalCompliance || 0 }
].map(item => Object.assign(item, { tier: scoreTier(item.score) }));

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
    loading: true,
    loadError: '',
    session: null,
    scenario: null,
    isRoleplay: false,
    statusText: '',
    messages: [],
    dimensions: [],
    evaluation: null,
    summary: null,
    violationsExpanded: false,
    roundCommentsExpanded: false
  },

  sessionId: '',
  pollTimer: null,

  onLoad(options) {
    this.sessionId = options.sessionId || '';
    this.setData({ isRoleplay: options.mode === 'patient_simulation' });
    this.loadDetail();
  },

  onUnload() { if (this.pollTimer) clearTimeout(this.pollTimer); },

  loadDetail() {
    if (!this.sessionId) return;
    this.setData({ loading: true, loadError: '' });
    const sessionRequest = this.data.isRoleplay ? api.getRoleplaySession(this.sessionId) : api.getSession(this.sessionId);
    sessionRequest.then(detail => {
      const session = detail.session;
      const statusText = session.status === 'in_progress' ? '进行中'
        : session.status === 'completed' ? '已完成' : '已放弃';
      const messages = (detail.messages || []).map(message => Object.assign({}, message, {
        time: message.createdAt || '',
        learningPoints: message.learningPoints || []
      }));
      this.setData({
        session,
        statusText,
        messages,
        loading: false
      });
      // 客服训练已完成 → 加载评估摘要
      if (!this.data.isRoleplay && session.status === 'completed') {
        this.loadEvaluation();
      }
      // 患者模拟已完成 → 加载复盘摘要（独立接口）
      if (this.data.isRoleplay && session.status === 'completed') {
        this.loadSummary();
      }
    }).catch(error => {
      this.setData({ loading: false, loadError: error.message || '加载失败' });
    });
  },

  loadSummary() {
    api.getRoleplaySummary(this.sessionId).then(data => {
      if (data.status === 'ready' && data.summary) {
        this.setData({ summary: data.summary });
      } else {
        this.setData({ summary: null });
      }
    }).catch(() => {
      this.setData({ summary: null });
    });
  },

  loadEvaluation() {
    api.getEvaluation(this.sessionId).then(data => {
      if (data.status === 'ready' && data.evaluation) {
        const evaluation = normalizeEvaluation(data.evaluation);
        this.setData({
          evaluation,
          dimensions: dimensionsFrom(evaluation.dimensionScores || {})
        });
      } else if (data.status === 'pending' || data.status === 'failed') {
        this.setData({ evaluation: { pending: true, status: data.status } });
      }
    }).catch(() => {
      this.setData({ evaluation: { pending: true, status: 'unknown' } });
    });
  },

  toggleViolations() { this.setData({ violationsExpanded: !this.data.violationsExpanded }); },
  toggleRoundComments() { this.setData({ roundCommentsExpanded: !this.data.roundCommentsExpanded }); },

  goTraining() {
    if (!this.data.session) return;
    const page = this.data.isRoleplay ? '/pages/roleplay/roleplay' : '/pages/training/training';
    wx.navigateTo({ url: `${page}?sessionId=${this.sessionId}` });
  },

  goResult() {
    const page = this.data.isRoleplay ? '/pages/roleplay-result/roleplay-result' : '/pages/result/result';
    wx.navigateTo({ url: `${page}?sessionId=${this.sessionId}` });
  }
});
