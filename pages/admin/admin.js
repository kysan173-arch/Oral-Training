const api = require('../../utils/api.js');

const DIMENSIONS = [
  { key: 'knowledgeAccuracy', name: '知识准确性' },
  { key: 'medicalCompliance', name: '医疗合规' },
  { key: 'empathy', name: '同理心' },
  { key: 'needsDiscovery', name: '需求挖掘' },
  { key: 'serviceEtiquette', name: '服务礼仪' }
];

const coachingSuggestions = dashboard => {
  if (Number(dashboard.completedSessions || 0) <= 0) {
    return [{
      title: '暂无足够训练数据',
      text: '完成训练并生成报告后，这里会根据机构聚合数据给出辅导建议。',
      severity: 'normal'
    }];
  }
  const suggestions = [];
  const dimensions = dashboard.dimensionAverages || {};
  const weakest = DIMENSIONS.reduce((current, item) => {
    const score = Number(dimensions[item.key] || 0);
    return !current || score < current.score ? Object.assign({}, item, { score }) : current;
  }, null);
  if (weakest && weakest.score < 70) {
    suggestions.push({
      title: `优先关注：${weakest.name}`,
      text: `团队均值为 ${weakest.score} 分。建议安排围绕该能力的短场景复练，并在复盘中关注具体表达。`,
      severity: weakest.score < 60 ? 'high' : 'medium'
    });
  }
  const weakScene = (dashboard.scenarioStats || []).filter(item => item.total > 0)
    .reduce((current, item) => !current || item.passRate < current.passRate ? item : current, null);
  if (weakScene && weakScene.passRate < 70) {
    suggestions.push({
      title: `重点场景：${weakScene.scenarioName}`,
      text: `该场景完成 ${weakScene.total} 次，通过率 ${weakScene.passRate}%。可优先组织该场景的针对性练习。`,
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
    suggestions: [],
    personal: { totalCount: 0, completedCount: 0, averageScore: 0, sceneStats: [], dimensionAverages: [], recentSessions: [] }
  },

  onShow() { this.loadPage(); },

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
    this.supervisorRequestVersion = (this.supervisorRequestVersion || 0) + 1;
    const requestVersion = this.supervisorRequestVersion;
    const requestedRange = this.data.timeRange;
    api.getSupervisorDashboard({ range: requestedRange }).then(supervisor => {
      if (requestVersion !== this.supervisorRequestVersion || requestedRange !== this.data.timeRange) return;
      const dimensionAverages = DIMENSIONS.map(item => Object.assign({}, item, {
        value: Number((supervisor.dimensionAverages || {})[item.key] || 0)
      }));
      const maxSceneTotal = Math.max(1, ...(supervisor.scenarioStats || []).map(item => item.total));
      const scenarioStats = (supervisor.scenarioStats || []).map(item => Object.assign({}, item, {
        averageScore: api.formatScore(item.averageScore),
        barWidth: Math.max(0, Math.min(100, item.passRate)),
        totalWidth: Math.max(4, item.total / maxSceneTotal * 100)
      }));
      const normalized = Object.assign({}, supervisor, {
        averageScore: api.formatScore(supervisor.averageScore), dimensionAverages, scenarioStats
      });
      this.setData({
        supervisor: normalized,
        suggestions: coachingSuggestions(supervisor),
        loading: false
      });
    }).catch(error => {
      if (requestVersion !== this.supervisorRequestVersion || requestedRange !== this.data.timeRange) return;
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '主管数据加载失败', icon: 'none' });
    });
  },

  loadPersonal() {
    api.getDashboard().then(data => {
      const maxCount = Math.max(1, ...(data.scenarioStats || []).map(item => item.trainingCount));
      const personal = {
        totalCount: data.totalSessions,
        completedCount: data.completedSessions,
        averageScore: api.formatScore(data.averageScore),
        sceneStats: (data.scenarioStats || []).map(item => ({
          id: item.scenarioId,
          name: item.scenarioName,
          count: item.trainingCount,
          barWidth: item.trainingCount / maxCount * 100
        })),
        dimensionAverages: DIMENSIONS.map(item => Object.assign({}, item, {
          value: Number((data.dimensionAverages || {})[item.key] || 0)
        })),
        recentSessions: (data.recentSessions || []).map(item => Object.assign({}, item, {
          statusText: item.status === 'in_progress' ? '进行中' : item.status === 'abandoned' ? '已放弃'
            : item.evaluationStatus === 'generating' ? '报告生成中' : item.evaluationStatus === 'failed' ? '报告失败' : '已完成'
        }))
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
  }
});
