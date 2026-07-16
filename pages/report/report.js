const request = require('../../static/api/request.js');

const CATEGORY_NAMES = {
  consultation: '咨询解答',
  price_negotiation: '价格异议',
  complaint_handling: '投诉安抚',
  recommendation: '项目推荐'
};

const DIMENSION_KEYS = [
  { key: 'empathy', name: '同理心与温度', color: '#52c41a' },
  { key: 'needsDiscovery', name: '需求挖掘力', color: '#fa8c16' },
  { key: 'valueShaping', name: '价值塑造力', color: '#1677e8' },
  { key: 'conversion', name: '邀约转化', color: '#722ed1' },
  { key: 'compliance', name: '合规意识', color: '#eb2f96' }
];

const OLD_DIMENSION_KEYS = [
  { key: 'empathy', name: '同理心与温度', color: '#52c41a' },
  { key: 'needsDiscovery', name: '需求挖掘力', color: '#fa8c16' },
  { key: 'knowledgeAccuracy', name: '知识准确性', color: '#1677e8' },
  { key: 'medicalCompliance', name: '医疗合规', color: '#722ed1' },
  { key: 'serviceEtiquette', name: '服务礼仪', color: '#eb2f96' }
];

function getDisplayDimensions(scores) {
  if (!scores) return [];
  const v5HasData = DIMENSION_KEYS.some(d => (scores[d.key] || 0) > 0);
  if (v5HasData) {
    return DIMENSION_KEYS.map(d => ({
      name: d.name,
      value: scores[d.key] || 0,
      max: 100,
      color: d.color
    }));
  }
  return OLD_DIMENSION_KEYS.map(d => ({
    name: d.name,
    value: scores[d.key] || 0,
    max: 100,
    color: d.color
  }));
}

Page({
  data: {
    sessions: [],
    filteredSessions: [],
    statusFilter: 'all',
    passFilter: 'all',
    categoryFilter: 'all',
    categories: [],
    loading: true,
    errorMessage: ''
  },

  onLoad() {
    this.loadHistory();
  },

  onShow() {
    this.loadHistory();
  },

  async loadHistory() {
    this.setData({ loading: true, errorMessage: '' });
    try {
      const data = await request.get('/sessions', {
        status: this.data.statusFilter,
        limit: 50
      });
      const sessions = (data.items || []).map(s => ({
        ...s,
        category: s.category ? (CATEGORY_NAMES[s.category] || s.category) : '',
        expanded: false,
        passed: (s.totalScore || 0) >= 60,
        displayDimensions: getDisplayDimensions(s.dimensionScores)
      }));
      this.setData({ sessions, loading: false }, () => this.applyFilters());
    } catch (error) {
      this.setData({
        loading: false,
        errorMessage: request.getErrorMessage(error, '历史记录加载失败')
      });
    }
  },

  applyFilters() {
    let filtered = [...this.data.sessions];
    const passFilter = this.data.passFilter;
    const categoryFilter = this.data.categoryFilter;

    if (passFilter === 'passed') {
      filtered = filtered.filter(s => s.status === 'completed' && s.passed);
    } else if (passFilter === 'failed') {
      filtered = filtered.filter(s => s.status === 'completed' && !s.passed);
    }

    if (categoryFilter !== 'all') {
      filtered = filtered.filter(s => s.category === categoryFilter);
    }

    // Extract unique categories for filter bar
    const categories = [...new Set(
      this.data.sessions
        .filter(s => s.category)
        .map(s => s.category)
    )];

    this.setData({ filteredSessions: filtered, categories });
  },

  toggleExpand(e) {
    const index = e.currentTarget.dataset.index;
    const sessions = this.data.filteredSessions.map((s, i) => ({
      ...s,
      expanded: i === index ? !s.expanded : false
    }));
    this.setData({ filteredSessions: sessions });
  },

  changeStatusFilter(e) {
    const statusFilter = e.currentTarget.dataset.status;
    this.setData({ statusFilter }, () => this.loadHistory());
  },

  changePassFilter(e) {
    const passFilter = e.currentTarget.dataset.pass;
    this.setData({ passFilter }, () => this.applyFilters());
  },

  changeCategoryFilter(e) {
    const categoryFilter = e.currentTarget.dataset.category;
    this.setData({ categoryFilter }, () => this.applyFilters());
  },

  viewSession(e) {
    e.stopPropagation && e.stopPropagation();
    const sessionId = e.currentTarget.dataset.id;
    const status = e.currentTarget.dataset.status;
    if (!sessionId) return;

    if (status === 'in_progress') {
      wx.navigateTo({
        url: `/pages/training/training?sessionId=${encodeURIComponent(sessionId)}`
      });
      return;
    }

    wx.navigateTo({
      url: `/pages/result/result?sessionId=${encodeURIComponent(sessionId)}`
    });
  },

  onPullDownRefresh() {
    this.loadHistory().finally(() => wx.stopPullDownRefresh());
  },

  retryLoad() {
    this.loadHistory();
  }
});
