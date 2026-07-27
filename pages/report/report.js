const api = require('../../utils/api.js');

const SCENARIO_FILTERS = [
  { id: '', name: '全部场景' },
  { id: 'implant-basic', name: '种植牙咨询' },
  { id: 'orthodontic-basic', name: '正畸咨询' },
  { id: 'price-comparison', name: '比价异议' },
  { id: 'post-treatment-discomfort', name: '术后不适' }
];

const DIFF_MAP = { basic: '基础', advanced: '进阶' };
const DIFF_COLOR = { basic: '#5672bd', advanced: '#bd7a35' };

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

const getPassStatus = (item) => {
  if (item.status !== 'completed' || item.totalScore === null) return null;
  return item.totalScore >= 60 ? 'passed' : 'failed';
};

Page({
  data: {
    sessions: [],
    allSessions: [],
    expandedId: '',
    expandedEval: null,
    expandedDims: [],
    expandedLoading: false,
    historyMode: 'customer_service',
    statusFilter: 'all',       // 'all' | 'passed' | 'failed'
    scenarioFilter: '',        // scenarioId or ''
    scenarioFilters: SCENARIO_FILTERS,
    scenarioFilterLabel: '场景分类 ▾',
    showScenarioFilter: false
  },

  onShow() { this.loadSessions(); },

  loadSessions() {
    const isRoleplay = this.data.historyMode === 'patient_simulation';
    const request = isRoleplay ? api.getRoleplaySessions({ status: 'all', limit: 50 }) : api.getSessions({ status: 'all', limit: 50 });
    request.then(data => {
      const sessions = (data.items || []).map(item => {
        const passStatus = getPassStatus(item);
        return Object.assign({}, item, {
          statusText: item.status === 'in_progress' ? '进行中' : item.status === 'completed' ? '已完成' : '已放弃',
          statusClass: item.status,
          actionText: item.status === 'in_progress'
            ? (isRoleplay ? '继续模拟' : '继续训练')
            : item.status === 'completed'
              ? (isRoleplay ? '查看复盘' : '查看报告')
              : '查看对话',
          evaluation: !isRoleplay && item.totalScore !== null ? { totalScore: item.totalScore } : null,
          isRoleplay,
          messages: [],
          passStatus,
          passLabel: passStatus === 'passed' ? '已通过' : passStatus === 'failed' ? '未通过' : '',
          totalScore: item.totalScore
        });
      });
      this.setData({ allSessions: sessions, expandedId: '', expandedEval: null });
      this.applyFilters();
    }).catch(error => wx.showToast({ title: error.message || '历史记录加载失败', icon: 'none' }));
  },

  applyFilters() {
    let sessions = this.data.allSessions;
    const { statusFilter, scenarioFilter } = this.data;

    // 按通过状态筛选（仅对已完成的客服训练）
    if (statusFilter !== 'all') {
      sessions = sessions.filter(s => {
        if (s.status !== 'completed' || s.isRoleplay) return false;
        return statusFilter === 'passed' ? (s.totalScore >= 60) : (s.totalScore < 60);
      });
    }

    // 按场景筛选
    if (scenarioFilter) {
      sessions = sessions.filter(s => s.scenarioId === scenarioFilter);
    }

    this.setData({ sessions });
  },

  onStatusFilter(e) {
    const status = e.currentTarget.dataset.status;
    this.setData({ statusFilter: status }, () => this.applyFilters());
  },

  toggleScenarioFilter() {
    this.setData({ showScenarioFilter: !this.data.showScenarioFilter });
  },

  onScenarioFilter(e) {
    const id = e.currentTarget.dataset.id;
    const label = this.computeFilterLabel(id);
    this.setData({ scenarioFilter: id, scenarioFilterLabel: label, showScenarioFilter: false }, () => this.applyFilters());
  },

  computeFilterLabel(filterId) {
    if (!filterId) return '场景分类 ▾';
    const match = SCENARIO_FILTERS.find(f => f.id === filterId);
    return match ? '场景：' + match.name : '场景分类 ▾';
  },

  switchHistoryMode(e) {
    const mode = e.currentTarget.dataset.mode;
    if (!mode || mode === this.data.historyMode) return;
    this.setData({
      historyMode: mode,
      sessions: [],
      allSessions: [],
      expandedId: '',
      expandedEval: null,
      statusFilter: 'all',
      scenarioFilter: '',
      scenarioFilterLabel: '场景分类 ▾',
      showScenarioFilter: false
    }, () => this.loadSessions());
  },

  handleAction(e) {
    const session = this.data.sessions.find(item => item.id === e.currentTarget.dataset.id);
    if (!session) return;
    if (session.status === 'in_progress') {
      const page = session.isRoleplay ? 'roleplay/roleplay' : 'training/training';
      wx.navigateTo({ url: `/pages/${page}?sessionId=${session.id}` });
    } else if (session.status === 'completed') {
      const page = session.isRoleplay ? 'roleplay-result/roleplay-result' : 'result/result';
      wx.navigateTo({ url: `/pages/${page}?sessionId=${session.id}` });
    } else {
      this.toggleConversation({ currentTarget: { dataset: { id: session.id } } });
    }
  },

  toggleConversation(e) {
    const id = e.currentTarget.dataset.id;
    if (this.data.expandedId === id) {
      this.setData({ expandedId: '', expandedEval: null, expandedDims: [] });
      return;
    }
    const session = this.data.sessions.find(item => item.id === id);
    if (!session) return;

    this.setData({ expandedId: id, expandedEval: null, expandedDims: [], expandedLoading: true });

    const request = session.isRoleplay ? api.getRoleplaySession(id) : api.getSession(id);
    request.then(data => {
      const messages = (data.messages || []).map(message => Object.assign({}, message, {
        learningPoints: message.learningPoints || []
      }));
      const sessions = this.data.sessions.map(item => item.id === id
        ? Object.assign({}, item, { messages })
        : item);
      this.setData({ sessions, expandedLoading: false });
    }).catch(error => {
      wx.showToast({ title: error.message, icon: 'none' });
      this.setData({ expandedLoading: false });
    });

    // 如果是已完成的客服训练，加载评估数据
    if (session.status === 'completed' && !session.isRoleplay) {
      api.getEvaluation(id).then(report => {
        if (report.status === 'ready' && report.evaluation) {
          const evaluation = normalizeEvaluation(report.evaluation);
          this.setData({
            expandedEval: evaluation,
            expandedDims: dimensionsFrom(evaluation.dimensionScores)
          });
        }
      }).catch(() => {});
    }
  },

  goProfile() {
    wx.navigateTo({ url: '/pages/profile/profile' });
  },

  goMistakes() {
    wx.navigateTo({ url: '/pages/mistakes/mistakes' });
  }
});
