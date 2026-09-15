const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');

/* 分数分档：颜色只跟随分数（≥80 良好绿 / 60–79 中间蓝 / <60 待提升橙） */
const scoreTier = score => (score >= 80 ? 'high' : score >= 60 ? 'mid' : 'low');

const DIMENSIONS = [
  { key: 'knowledgeAccuracy', name: '知识准确性' },
  { key: 'medicalCompliance', name: '医疗合规' },
  { key: 'empathy', name: '同理心' },
  { key: 'needsDiscovery', name: '需求挖掘' },
  { key: 'serviceEtiquette', name: '服务礼仪' }
];

/* 成长曲线五条线的颜色，全部取自 app.wxss 既有调色板，不引入新色 */
const DIMENSION_COLORS = ['#1F3864', '#B97A1E', '#2E8B6C', '#6B7A93', '#9AA6B8'];

/* 数值格式化：整数原样显示，小数保留一位 */
const fmt1 = value => {
  const num = Number(value);
  if (!isFinite(num)) return value === null || value === undefined ? '0' : String(value);
  return num % 1 === 0 ? String(Math.round(num)) : num.toFixed(1);
};

Page({
  data: {
    loading: true,
    detail: null,
    dimensions: [],
    teamAverages: [],
    growthLabels: [],
    growthSeries: [],
    scenarioFilters: [],
    selectedScenarioId: 'all',
    filteredTrend: [],
    inspectItems: []
  },

  memberId: '',

  onLoad(options) {
    this.memberId = options.id || '';
    this.loadDetail();
  },

  loadDetail() {
    if (!this.memberId) {
      wx.showToast({ title: '成员标识无效', icon: 'none' });
      return;
    }
    this.setData({ loading: true });
    /* 团队均值作为雷达图对比线：后端没有按需接口，统一拉一次全量 dashboard 即可 */
    Promise.all([
      api.getSupervisorMember(this.memberId),
      api.getSupervisorDashboard({ range: 'all' })
    ]).then(([member, dashboard]) => {
      this.applyMember(member, dashboard);
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '成员详情加载失败', icon: 'none' });
    });
  },

  applyMember(data, dashboard) {
    const dimensions = DIMENSIONS.map(item => {
      const score = Number((data.dimensionAverages || {})[item.key] || 0);
      return Object.assign({}, item, { score, scoreText: fmt1(score), tier: scoreTier(score) });
    });
    const teamAverages = DIMENSIONS.map(item => Number((dashboard.dimensionAverages || {})[item.key] || 0));

    const trend = (data.trend || []).map(item => Object.assign({}, item, {
      scoreText: `${fmt1(item.totalScore)} 分`
    }));

    /* 成长曲线：仅在至少有 2 条已完成记录时才有意义 */
    const growthLabels = trend.map(item => datetime.formatMonthDay(item.date));
    const growthSeries = DIMENSIONS.map((item, dimIndex) => ({
      name: item.name,
      type: 'line',
      color: DIMENSION_COLORS[dimIndex],
      axis: 'right',
      values: trend.map(point => {
        const score = point && point.scores ? point.scores[item.key] : null;
        return Number.isFinite(Number(score)) ? Number(score) : null;
      })
    }));

    const scenarioMap = new Map();
    trend.forEach(item => {
      if (!item.scenarioId) return;
      if (!scenarioMap.has(item.scenarioId)) {
        scenarioMap.set(item.scenarioId, { id: item.scenarioId, name: item.scenarioName || item.scenarioId });
      }
    });
    const scenarioFilters = [{ id: 'all', name: '全部场景' }].concat(Array.from(scenarioMap.values()));

    /* 抽查列表（含进行中会话）：进入抽查页才拉取完整对话，本页只列条目 */
    const inspectItems = (data.inspectSessions || []).map(item => Object.assign({}, item, {
      statusText: item.status === 'in_progress' ? '进行中'
        : item.status === 'completed' ? '已完成' : '已放弃',
      roundText: `${item.currentRound}/${item.maxRounds} 轮`,
      scoreText: item.totalScore === null || item.totalScore === undefined ? '—' : `${fmt1(item.totalScore)} 分`
    }));

    const detail = Object.assign({}, data, {
      member: Object.assign({}, data.member, {
        initial: (data.member.displayName || '学').slice(0, 1)
      }),
      averageScoreText: fmt1(data.averageScore),
      passRateText: fmt1(data.passRate),
      weaknesses: (data.weaknesses || []).map(item => Object.assign({}, item, {
        scoreText: fmt1(item.score)
      })),
      trend
    });

    this.setData({
      detail,
      dimensions,
      teamAverages,
      growthLabels,
      growthSeries,
      scenarioFilters,
      inspectItems,
      selectedScenarioId: 'all',
      filteredTrend: trend
    }, () => {
      this.setData({ loading: false });
    });
  },

  openInspect(e) {
    const sessionId = e.currentTarget.dataset.id;
    if (!sessionId) return;
    wx.navigateTo({
      url: `/pages/session-inspect/session-inspect?memberId=${encodeURIComponent(this.memberId)}&sessionId=${encodeURIComponent(sessionId)}`
    });
  },

  selectScenarioFilter(e) {
    const selectedScenarioId = e.currentTarget.dataset.id;
    if (!selectedScenarioId || selectedScenarioId === this.data.selectedScenarioId) return;
    const trend = (this.data.detail && this.data.detail.trend) || [];
    const filteredTrend = selectedScenarioId === 'all'
      ? trend
      : trend.filter(item => item.scenarioId === selectedScenarioId);
    this.setData({ selectedScenarioId, filteredTrend });
  }
});
