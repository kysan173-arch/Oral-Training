const api = require('../../utils/api.js');

const DIMENSIONS = [
  { key: 'knowledgeAccuracy', name: '知识准确性' },
  { key: 'medicalCompliance', name: '医疗合规' },
  { key: 'needsDiscovery', name: '需求挖掘' },
  { key: 'empathy', name: '同理心' },
  { key: 'serviceEtiquette', name: '服务礼仪' }
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
    weaknesses: []
  },

  onShow() { this.loadProfile(); },

  loadProfile() {
    this.setData({ loading: true });
    api.getLearningProfile().then(data => {
      const dimensionAverages = data.dimensionAverages || {};
      const rawDimensions = DIMENSIONS.map(item => Object.assign({}, item, {
        score: dimensionAverages[item.key] || 0,
        tier: scoreTier(dimensionAverages[item.key] || 0)
      }));
      // 标记最弱维度（分数最低项），横条标橙引导关注
      const weakestKey = rawDimensions.reduce(
        (min, item) => (item.score < min.score ? item : min),
        rawDimensions[0] || { score: 101 }
      ).key;
      const dimensions = rawDimensions.map(item => Object.assign({}, item, {
        weakest: item.key === weakestKey
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
      /* 弱项卡片要带「复练」入口：后端算出了薄弱维度，但此前没有任何入口
         从弱项跳到训练，学员读完那句建议只能自己回训练中心找场景。
         score 是后端原始 double（会出现 66.66666666666667 那种），展示前格式化。 */
      const weaknesses = (data.weaknesses || []).map(item => Object.assign({}, item, {
        scoreText: api.formatScore(item.score),
        /* WXML 不能对数据路径调方法，入口可用性在这里算成布尔字段 */
        canRetrain: !!item.key
      }));
      // data.overall.scoreDelta 由后端下发，原样透传给 wxml（首末变化数值）
      this.setData({
        profile: data,
        dimensions,
        trend,
        weaknesses,
        loading: false
      });
    }).catch(error => {
      this.setData({ loading: false });
      api.showCenterNotice({ title: error.message || '成长数据加载失败' });
    });
  },

  /* 弱项 → 场景候选。只传维度 key 就够，名称一并带上是为了让候选页
     首屏就能显示维度名（避免等接口返回前标题是空的）。 */
  goRetrain(e) {
    const { key, name } = e.currentTarget.dataset;
    if (!key) return;
    wx.navigateTo({
      url: `/pages/retrain-candidates/retrain-candidates?dimension=${encodeURIComponent(key)}&name=${encodeURIComponent(name || '')}`
    });
  },

});
