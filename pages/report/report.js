const api = require('../../utils/api.js');

Page({
  data: {
    sessions: [],
    loading: true,
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
        evaluation: !isRoleplay && item.totalScore !== null ? { totalScore: item.totalScore } : null,
        isRoleplay
      }));
      this.setData({ sessions, loading: false });
    }).catch(error => {
      if (requestVersion !== this.historyRequestVersion || requestedMode !== this.data.historyMode) return;
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '历史记录加载失败', icon: 'none' });
    });
  },

  switchHistoryMode(e) {
    const mode = e.currentTarget.dataset.mode;
    if (!mode || mode === this.data.historyMode) return;
    this.setData({ historyMode: mode, sessions: [] }, () => this.loadSessions());
  },

  selectStatus(e) {
    const selectedStatus = e.currentTarget.dataset.id || 'all';
    if (selectedStatus === this.data.selectedStatus) return;
    this.setData({ selectedStatus }, () => this.loadSessions());
  },

  // 点击历史卡 → 进入详情页（对话/摘要/雷达图在详情页内加载）
  openDetail(e) {
    const id = e.currentTarget.dataset.id;
    if (!id) return;
    const mode = this.data.historyMode;
    wx.navigateTo({ url: `/pages/session-detail/session-detail?sessionId=${id}&mode=${mode}` });
  }
});
