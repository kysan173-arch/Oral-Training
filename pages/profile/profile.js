const api = require('../../utils/api.js');

const DIMENSIONS = [
  { key: 'knowledgeAccuracy', name: '知识准确性', color: '#667eea' },
  { key: 'medicalCompliance', name: '医疗合规', color: '#52a67a' },
  { key: 'needsDiscovery', name: '需求挖掘', color: '#6b9de8' },
  { key: 'empathy', name: '同理心', color: '#e6a24b' },
  { key: 'serviceEtiquette', name: '服务礼仪', color: '#8b75c9' }
];

function scoreTier(score) {
  if (score >= 80) return 'high';
  if (score >= 60) return 'mid';
  return 'low';
}

Page({
  data: {
    loading: true,
    profile: null,
    dimensions: [],
    trend: [],
    weaknesses: [],
    mistakePercent: 0
  },

  onShow() { this.loadProfile(); },

  loadProfile() {
    this.setData({ loading: true });
    api.getLearningProfile().then(data => {
      const dimensionAverages = data.dimensionAverages || {};
      const dimensions = DIMENSIONS.map(item => Object.assign({}, item, {
        score: dimensionAverages[item.key] || 0,
        tier: scoreTier(dimensionAverages[item.key] || 0)
      }));
      const rawTrend = data.trend || [];
      const trend = rawTrend.map((item, i) => {
        const prev = i > 0 ? rawTrend[i - 1] : null;
        const delta = prev ? item.totalScore - prev.totalScore : null;
        return Object.assign({}, item, {
          scoreLabel: `${item.totalScore} 分`,
          tier: scoreTier(item.totalScore),
          delta,
          arrow: delta === null ? '' : delta > 0 ? '↑' : delta < 0 ? '↓' : '→',
          arrowClass: delta === null ? '' : delta > 0 ? 'up' : delta < 0 ? 'down' : 'flat'
        });
      });
      const mistakes = data.mistakes || {};
      const mistakePercent = mistakes.total > 0
        ? Math.round((mistakes.mastered / mistakes.total) * 100)
        : 0;
      this.setData({
        profile: data,
        dimensions,
        trend,
        weaknesses: data.weaknesses || [],
        mistakePercent,
        loading: false
      });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '成长数据加载失败', icon: 'none' });
    });
  },

  goPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); },

  goMistakes() { wx.navigateTo({ url: '/pages/mistakes/mistakes' }); },

  goTraining() { wx.switchTab({ url: '/pages/index/index' }); },

  goHistory() { wx.navigateTo({ url: '/pages/report/report' }); }
});
