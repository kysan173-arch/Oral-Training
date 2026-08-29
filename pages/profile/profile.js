const api = require('../../utils/api.js');

const DIMENSIONS = [
  { key: 'knowledgeAccuracy', name: '知识准确性', color: '#667eea' },
  { key: 'medicalCompliance', name: '医疗合规', color: '#52a67a' },
  { key: 'empathy', name: '同理心', color: '#e6a24b' },
  { key: 'needsDiscovery', name: '需求挖掘', color: '#6b9de8' },
  { key: 'serviceEtiquette', name: '服务礼仪', color: '#8b75c9' }
];

Page({
  data: {
    loading: true,
    profile: null,
    dimensions: [],
    trend: [],
    weaknesses: []
  },

  onShow() { this.loadProfile(); },

  loadProfile() {
    this.setData({ loading: true });
    api.getLearningProfile().then(data => {
      const dimensionAverages = data.dimensionAverages || {};
      const dimensions = DIMENSIONS.map(item => Object.assign({}, item, {
        score: dimensionAverages[item.key] || 0
      }));
      const trend = (data.trend || []).map(item => Object.assign({}, item, {
        scoreLabel: `${item.totalScore} 分`
      }));
      const profile = Object.assign({}, data, {
        overall: Object.assign({}, data.overall, { averageScore: api.formatScore(data.overall.averageScore) })
      });
      this.setData({
        profile,
        dimensions,
        trend,
        weaknesses: data.weaknesses || [],
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

  goHistory() { wx.switchTab({ url: '/pages/report/report' }); }
});
