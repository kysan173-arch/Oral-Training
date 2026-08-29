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
    detail: null,
    dimensions: []
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
    api.getSupervisorMember(this.memberId).then(data => {
      const dimensions = DIMENSIONS.map(item => Object.assign({}, item, {
        score: Number((data.dimensionAverages || {})[item.key] || 0)
      }));
      const detail = Object.assign({}, data, {
        averageScore: api.formatScore(data.averageScore),
        member: Object.assign({}, data.member, {
          initial: (data.member.displayName || '学').slice(0, 1)
        }),
        trend: (data.trend || []).map(item => Object.assign({}, item, {
          scoreText: `${item.totalScore} 分`
        }))
      });
      this.setData({ detail, dimensions, loading: false });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '成员详情加载失败', icon: 'none' });
    });
  }
});
