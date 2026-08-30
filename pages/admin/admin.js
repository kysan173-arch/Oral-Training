const api = require('../../utils/api.js');

const DIMENSIONS = [
  { key: 'knowledgeAccuracy', name: '知识准确性' },
  { key: 'medicalCompliance', name: '医疗合规' },
  { key: 'empathy', name: '同理心' },
  { key: 'needsDiscovery', name: '需求挖掘' },
  { key: 'serviceEtiquette', name: '服务礼仪' }
];

/* 分数分档：颜色只跟随分数（≥80 良好绿 / 60–79 中间蓝 / <60 待提升橙） */
const scoreTier = score => (score >= 80 ? 'high' : score >= 60 ? 'mid' : 'low');
const SEVERITY_TEXT = { high: '优先处理', medium: '建议关注', normal: '保持节奏' };

/* 数值格式化：整数原样显示，小数保留一位 */
const fmt1 = value => {
  const num = Number(value);
  if (!isFinite(num)) return value === null || value === undefined ? '0' : String(value);
  return num % 1 === 0 ? String(Math.round(num)) : num.toFixed(1);
};

const coachingSuggestions = dashboard => {
  const suggestions = [];
  const dimensions = dashboard.dimensionAverages || {};
  const weakest = DIMENSIONS.reduce((current, item) => {
    const score = Number(dimensions[item.key] || 0);
    return !current || score < current.score ? Object.assign({}, item, { score }) : current;
  }, null);
  if (weakest && weakest.score < 70) {
    suggestions.push({
      title: `优先关注：${weakest.name}`,
      text: `团队均值为 ${fmt1(weakest.score)} 分。建议安排围绕该能力的短场景复练，并在复盘中关注具体表达。`,
      severity: weakest.score < 60 ? 'high' : 'medium'
    });
  }
  const weakScene = (dashboard.scenarioStats || []).filter(item => item.total > 0)
    .reduce((current, item) => !current || item.passRate < current.passRate ? item : current, null);
  if (weakScene && weakScene.passRate < 70) {
    suggestions.push({
      title: `重点场景：${weakScene.scenarioName}`,
      text: `该场景完成 ${weakScene.total} 次，通过率 ${fmt1(weakScene.passRate)}%。可优先组织该场景的针对性练习。`,
      severity: weakScene.passRate < 50 ? 'high' : 'medium'
    });
  }
  if (!suggestions.length) {
    suggestions.push({ title: '整体表现稳定', text: '当前没有低于关注阈值的聚合指标，可继续用真实训练记录观察变化。', severity: 'normal' });
  }
  return suggestions;
};

Page({
  data: {
    isAdmin: false,
    loading: true,
    timeRange: 'month',
    timeFilters: [
      { id: 'week', name: '本周' }, { id: 'month', name: '本月' },
      { id: 'quarter', name: '本季度' }, { id: 'all', name: '全部' }
    ],
    supervisor: null,
    members: [],
    suggestions: [],
    personal: { totalCount: 0, completedCount: 0, averageScore: 0, sceneStats: [], dimensionAverages: [], recentSessions: [] }
  },

  onShow() {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 2 });
    }
    this.loadPage();
  },

  loadPage() {
    this.setData({ loading: true });
    api.ensureAuthenticated().then(() => {
      const user = api.getCurrentUser();
      if (user && user.role === 'admin') {
        this.setData({ isAdmin: true }, () => this.loadSupervisor());
        return;
      }
      this.setData({ isAdmin: false }, () => this.loadPersonal());
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '登录状态获取失败', icon: 'none' });
    });
  },

  loadSupervisor() {
    Promise.all([
      api.getSupervisorDashboard({ range: this.data.timeRange }),
      api.getSupervisorMembers({ limit: 100 })
    ]).then(([supervisor, memberData]) => {
      const dimensionAverages = DIMENSIONS.map(item => {
        const value = Number((supervisor.dimensionAverages || {})[item.key] || 0);
        const tier = scoreTier(value);
        return Object.assign({}, item, { value, valueText: fmt1(value), tier });
      });
      const maxSceneTotal = Math.max(1, ...(supervisor.scenarioStats || []).map(item => item.total));
      const scenarioStats = (supervisor.scenarioStats || []).map(item => Object.assign({}, item, {
        barWidth: Math.max(0, Math.min(100, item.passRate)),
        totalWidth: Math.max(4, item.total / maxSceneTotal * 100),
        averageScoreText: fmt1(item.averageScore),
        passRateText: fmt1(item.passRate)
      }));
      const normalized = Object.assign({}, supervisor, {
        dimensionAverages,
        scenarioStats,
        averageScoreText: fmt1(supervisor.averageScore),
        passRateText: fmt1(supervisor.passRate)
      });
      this.setData({
        supervisor: normalized,
        members: (memberData.members || []).map(item => Object.assign({}, item, {
          initial: (item.displayName || '学').slice(0, 1),
          averageScoreText: fmt1(item.averageScore),
          passRateText: fmt1(item.passRate),
          latestText: item.lastTrainingDate ? `最近训练：${item.lastTrainingDate}` : '暂未开始训练'
        })),
        suggestions: coachingSuggestions(supervisor).map(s => Object.assign({}, s, {
          severityText: SEVERITY_TEXT[s.severity] || ''
        })),
        loading: false
      });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '主管数据加载失败', icon: 'none' });
    });
  },

  loadPersonal() {
    api.getDashboard().then(data => {
      const totalSceneCount = (data.scenarioStats || []).reduce((sum, s) => sum + s.trainingCount, 0) || 1;
      const dimensionAverages = DIMENSIONS.map(item => {
        const value = Math.round(Number((data.dimensionAverages || {})[item.key] || 0));
        return Object.assign({}, item, { value, tier: scoreTier(value) });
      });
      const weakest = dimensionAverages.length
        ? dimensionAverages.reduce((prev, curr) => prev.value <= curr.value ? prev : curr) : null;
      const strongest = dimensionAverages.length
        ? dimensionAverages.reduce((prev, curr) => prev.value >= curr.value ? prev : curr) : null;
      const completionRate = data.totalSessions > 0
        ? Math.round(data.completedSessions / data.totalSessions * 100) : 0;
      const recentSessions = (data.recentSessions || []).map(item => {
        const statusText = item.status === 'in_progress' ? '进行中'
          : item.status === 'abandoned' ? '已放弃'
          : item.evaluationStatus === 'generating' ? '报告生成中'
          : item.evaluationStatus === 'failed' ? '报告失败' : '已完成';
        return Object.assign({}, item, { statusText });
      });
      const avgScore = typeof data.averageScore === 'number' ? data.averageScore : 0;
      const personal = {
        totalCount: data.totalSessions,
        completedCount: data.completedSessions,
        averageScore: typeof data.averageScore === 'number'
          ? (Math.round(data.averageScore * 10) / 10).toFixed(1)
          : data.averageScore,
        focalScoreRing: Math.max(0, Math.min(100, avgScore)),
        completionRate,
        sceneStats: (data.scenarioStats || []).map(item => ({
          id: item.scenarioId,
          name: item.scenarioName,
          count: item.trainingCount,
          sceneAvg: typeof item.averageScore === 'number'
            ? (Math.round(item.averageScore * 10) / 10).toFixed(1)
            : null,
          barWidth: item.trainingCount / totalSceneCount * 100
        })),
        dimensionAverages,
        weakestDimension: weakest,
        strongestDimension: strongest,
        dimensionSummaryText: weakest && strongest && weakest.key !== strongest.key
          ? `你最强的是${strongest.name}（${strongest.value}分），可以多练${weakest.name}`
          : '',
        weaknessActionText: weakest ? `建议在后续训练中重点关注「${weakest.name}」能力提升` : '',
        recentSessions
      };
      this.setData({ personal, loading: false });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '数据加载失败', icon: 'none' });
    });
  },

  selectTimeRange(e) {
    const timeRange = e.currentTarget.dataset.id;
    if (!timeRange || timeRange === this.data.timeRange) return;
    this.setData({ timeRange, loading: true }, () => this.loadSupervisor());
  },

  openMember(e) {
    const memberId = e.currentTarget.dataset.id;
    if (!memberId) return;
    wx.navigateTo({ url: `/pages/member-detail/member-detail?id=${encodeURIComponent(memberId)}` });
  },

  scrollToSection(e) {
    const selector = e.currentTarget.dataset.selector;
    if (!selector) return;
    wx.pageScrollTo({ selector, duration: 300 });
  },

  viewHistory() {
    wx.navigateTo({ url: '/pages/report/report' });
  },

  openRecentSession(e) {
    const sessionId = e.currentTarget.dataset.id;
    if (!sessionId) return;
    wx.navigateTo({ url: `/pages/result/result?sessionId=${encodeURIComponent(sessionId)}` });
  },

  goTraining() {
    wx.switchTab({ url: '/pages/home/home' });
  }
});
