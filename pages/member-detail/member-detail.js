const api = require('../../utils/api.js');

/* 分数分档：颜色只跟随分数（≥80 良好绿 / 60–79 中间蓝 / <60 待提升橙） */
const scoreTier = score => (score >= 80 ? 'high' : score >= 60 ? 'mid' : 'low');

const DIMENSIONS = [
  { key: 'knowledgeAccuracy', name: '知识准确性' },
  { key: 'medicalCompliance', name: '医疗合规' },
  { key: 'empathy', name: '同理心' },
  { key: 'needsDiscovery', name: '需求挖掘' },
  { key: 'serviceEtiquette', name: '服务礼仪' }
];

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
      const dimensions = DIMENSIONS.map(item => {
        const score = Number((data.dimensionAverages || {})[item.key] || 0);
        return Object.assign({}, item, { score, scoreText: fmt1(score), tier: scoreTier(score) });
      });
      const detail = Object.assign({}, data, {
        member: Object.assign({}, data.member, {
          initial: (data.member.displayName || '学').slice(0, 1)
        }),
        averageScoreText: fmt1(data.averageScore),
        passRateText: fmt1(data.passRate),
        weaknesses: (data.weaknesses || []).map(item => Object.assign({}, item, {
          scoreText: fmt1(item.score)
        })),
        trend: (data.trend || []).map(item => Object.assign({}, item, {
          scoreText: `${fmt1(item.totalScore)} 分`
        }))
      });
      this.setData({ detail, dimensions, loading: false });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '成员详情加载失败', icon: 'none' });
    });
  }
});
