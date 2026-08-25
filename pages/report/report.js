const api = require('../../utils/api.js');

const dimensionsFrom = score => [
  { key: 'empathy', name: '同理心', score: score.empathy || 0, color: '#667eea' },
  { key: 'knowledgeAccuracy', name: '知识准确性', score: score.knowledgeAccuracy || 0, color: '#52a67a' },
  { key: 'needsDiscovery', name: '需求挖掘', score: score.needsDiscovery || 0, color: '#f0a34b' },
  { key: 'serviceEtiquette', name: '服务礼仪', score: score.serviceEtiquette || 0, color: '#6b9de8' },
  { key: 'medicalCompliance', name: '医疗合规', score: score.medicalCompliance || 0, color: '#8b75c9' }
];

Page({
  data: {
    sessions: [],
    loading: true,
    expandedId: '',
    expandedEvaluationId: '',
    historyMode: 'customer_service',
    statusFilters: [
      { id: 'all', name: '全部' }, { id: 'completed', name: '已完成' },
      { id: 'in_progress', name: '进行中' }, { id: 'abandoned', name: '已放弃' }
    ],
    selectedStatus: 'all',
    roleBlocked: false
  },

  onShow() {
    const user = api.getCurrentUser();
    if (user && user.role === 'admin') {
      this.setData({ roleBlocked: true });
      return;
    }
    this.setData({ roleBlocked: false });
    this.loadSessions();
  },

  goAdminDashboard() { wx.switchTab({ url: '/pages/admin/admin' }); },

  loadSessions() {
    this.historyRequestVersion = (this.historyRequestVersion || 0) + 1;
    const requestVersion = this.historyRequestVersion;
    const requestedMode = this.data.historyMode;
    const isRoleplay = requestedMode === 'patient_simulation';
    const params = {
      status: this.data.selectedStatus,
      limit: 50
    };
    this.setData({ loading: true });
    const request = isRoleplay ? api.getRoleplaySessions(params) : api.getSessions(params);
    request.then(data => {
      if (requestVersion !== this.historyRequestVersion || requestedMode !== this.data.historyMode) return;
      const sessions = data.items.map(item => Object.assign({}, item, {
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
        evaluationDetail: null,
        evaluationLoading: false
      }));
      this.setData({ sessions, loading: false, expandedId: '', expandedEvaluationId: '' });
    }).catch(error => {
      if (requestVersion !== this.historyRequestVersion || requestedMode !== this.data.historyMode) return;
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '历史记录加载失败', icon: 'none' });
    });
  },

  switchHistoryMode(e) {
    const mode = e.currentTarget.dataset.mode;
    if (!mode || mode === this.data.historyMode) return;
    this.setData({ historyMode: mode, sessions: [], expandedId: '', expandedEvaluationId: '' }, () => this.loadSessions());
  },

  selectStatus(e) {
    const selectedStatus = e.currentTarget.dataset.id || 'all';
    if (selectedStatus === this.data.selectedStatus) return;
    this.setData({ selectedStatus }, () => this.loadSessions());
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
      this.setData({ expandedId: '' });
      return;
    }
    const session = this.data.sessions.find(item => item.id === id);
    if (!session) return;
    const requestedMode = this.data.historyMode;
    this.conversationRequestVersion = (this.conversationRequestVersion || 0) + 1;
    const requestVersion = this.conversationRequestVersion;
    const request = session.isRoleplay ? api.getRoleplaySession(id) : api.getSession(id);
    request.then(data => {
      if (requestVersion !== this.conversationRequestVersion || requestedMode !== this.data.historyMode) return;
      const messages = (data.messages || []).map(message => Object.assign({}, message, {
        learningPoints: message.learningPoints || []
      }));
      const sessions = this.data.sessions.map(item => item.id === id
        ? Object.assign({}, item, { messages })
        : item);
      this.setData({ sessions, expandedId: id });
    }).catch(error => {
      if (requestVersion !== this.conversationRequestVersion || requestedMode !== this.data.historyMode) return;
      wx.showToast({ title: error.message, icon: 'none' });
    });
  },

  toggleEvaluation(e) {
    const id = e.currentTarget.dataset.id;
    const session = this.data.sessions.find(item => item.id === id);
    if (!session || session.isRoleplay || session.status !== 'completed') return;
    if (this.data.expandedEvaluationId === id) {
      this.setData({ expandedEvaluationId: '' });
      return;
    }
    if (session.evaluationDetail) {
      this.setData({ expandedEvaluationId: id });
      return;
    }
    this.evaluationRequestVersion = (this.evaluationRequestVersion || 0) + 1;
    const requestVersion = this.evaluationRequestVersion;
    const sessions = this.data.sessions.map(item => item.id === id
      ? Object.assign({}, item, { evaluationLoading: true }) : item);
    this.setData({ sessions, expandedEvaluationId: id });
    api.getEvaluation(id).then(data => {
      if (requestVersion !== this.evaluationRequestVersion || this.data.historyMode !== 'customer_service') return;
      const detail = data.status === 'ready' && data.evaluation ? Object.assign({}, data.evaluation, {
        dimensions: dimensionsFrom(data.evaluation.dimensionScores || {})
      }) : { status: data.status, pending: true };
      const nextSessions = this.data.sessions.map(item => item.id === id
        ? Object.assign({}, item, { evaluationDetail: detail, evaluationLoading: false }) : item);
      this.setData({ sessions: nextSessions });
    }).catch(error => {
      if (requestVersion !== this.evaluationRequestVersion) return;
      const nextSessions = this.data.sessions.map(item => item.id === id
        ? Object.assign({}, item, { evaluationLoading: false }) : item);
      this.setData({ sessions: nextSessions, expandedEvaluationId: '' });
      wx.showToast({ title: error.message || '报告加载失败', icon: 'none' });
    });
  }
});
