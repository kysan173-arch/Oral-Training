const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');

// 雷达图维度取数（配色按我方墨蓝体系，淘汰 master 的 #667eea 紫粉）
const dimensionsFrom = score => [
  { key: 'empathy', name: '同理心', value: score.empathy, color: '#1F3864' },
  { key: 'knowledgeAccuracy', name: '知识准确性', value: score.knowledgeAccuracy, color: '#2E6CA4' },
  { key: 'needsDiscovery', name: '需求挖掘', value: score.needsDiscovery, color: '#3E8E9E' },
  { key: 'serviceEtiquette', name: '服务礼仪', value: score.serviceEtiquette, color: '#52a67a' },
  { key: 'medicalCompliance', name: '医疗合规', value: score.medicalCompliance, color: '#C9A227' }
].filter(item => item.value !== null && item.value !== undefined && Number.isFinite(Number(item.value)))
  .map(item => Object.assign({}, item, { score: Number(item.value) }));

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
    scenarioFilters: [{ id: '', name: '全部场景' }],
    selectedStatus: 'all',
    selectedScenarioId: '',
    roleBlocked: false
  },

  onShow() {
    const user = api.getCurrentUser();
    if (user && user.role === 'admin') {
      this.setData({ roleBlocked: true });
      return;
    }
    this.setData({ roleBlocked: false });
    this.loadScenarioFilters();
    this.loadSessions();
  },

  goAdminDashboard() { wx.switchTab({ url: '/pages/admin/admin' }); },

  loadScenarioFilters() {
    api.getScenarios().then(data => {
      const scenarioFilters = [{ id: '', name: '全部场景' }].concat((data.items || []).map(item => ({
        id: item.id,
        name: item.name
      })));
      this.setData({ scenarioFilters });
    }).catch(() => {});
  },

  loadSessions() {
    this.historyRequestVersion = (this.historyRequestVersion || 0) + 1;
    const requestVersion = this.historyRequestVersion;
    const requestedMode = this.data.historyMode;
    const isRoleplay = requestedMode === 'patient_simulation';
    const params = {
      status: this.data.selectedStatus,
      scenarioId: this.data.selectedScenarioId,
      limit: 50
    };
    this.setData({ loading: true });
    const request = isRoleplay ? api.getRoleplaySessions(params) : api.getSessions(params);
    request.then(data => {
      if (requestVersion !== this.historyRequestVersion || requestedMode !== this.data.historyMode) return;
      const sessions = data.items.map(item => Object.assign({}, item, {
        statusText: item.status === 'in_progress' ? '进行中' : item.status === 'abandoned' ? '已放弃'
          : item.evaluationStatus === 'generating' ? '报告生成中' : item.evaluationStatus === 'failed' ? '报告失败'
            : item.totalScore === null ? '知识依据不足' : '已完成',
        statusClass: item.status,
        actionText: item.status === 'in_progress'
          ? (isRoleplay ? '继续模拟' : '继续训练')
          : item.status === 'completed'
            ? (isRoleplay ? '查看复盘' : '查看报告')
            : '查看对话',
        updatedAtText: datetime.formatDateTime(item.updatedAt),
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

  selectScenario(e) {
    const selectedScenarioId = e.currentTarget.dataset.id || '';
    if (selectedScenarioId === this.data.selectedScenarioId) return;
    this.setData({ selectedScenarioId }, () => this.loadSessions());
  },

  // 点击历史卡 → 进入详情页（对话/摘要/雷达图在详情页内加载）
  openDetail(e) {
    const id = e.currentTarget.dataset.id;
    if (!id) return;
    const mode = this.data.historyMode;
    wx.navigateTo({ url: `/pages/session-detail/session-detail?sessionId=${id}&mode=${mode}` });
  },

  // 主行动按钮：根据状态续练 / 看报告或复盘 / 回看对话
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

  // 内联展开对话详情（master 做法，与 openDetail 下钻互补）
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
    if (session.evaluationDetail && !session.evaluationDetail.pending) {
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
