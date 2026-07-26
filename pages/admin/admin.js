const api = require('../../utils/api.js');

Page({
  data: { dashboard: { totalCount: 0, completedCount: 0, averageScore: 0, sceneStats: [], dimensionAverages: [], recentSessions: [] } },
  onShow() {
    api.getDashboard().then(data => {
      const names = {
        knowledgeAccuracy: '口腔知识准确性', medicalCompliance: '医疗合规', empathy: '情绪识别与同理心',
        needsDiscovery: '需求挖掘', serviceEtiquette: '服务礼仪'
      };
      const maxCount = Math.max(1, ...data.scenarioStats.map(item => item.trainingCount));
      this.setData({ dashboard: {
        totalCount: data.totalSessions,
        completedCount: data.completedSessions,
        averageScore: data.averageScore,
        sceneStats: data.scenarioStats.map(item => ({ id: item.scenarioId, name: item.scenarioName, count: item.trainingCount, barWidth: item.trainingCount / maxCount * 100 })),
        dimensionAverages: Object.keys(names).map(key => ({ key, name: names[key], value: data.dimensionAverages[key] || 0 })),
        recentSessions: data.recentSessions.map(item => Object.assign({}, item, { evaluation: item.totalScore === null ? null : { totalScore: item.totalScore } }))
      } });
    }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  }
});
