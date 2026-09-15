const api = require('../../utils/api.js');

const RANGE_LABELS = { week: '本周', month: '本月', quarter: '本季度', all: '全部' };

Page({
  data: {
    loading: true,
    categoryLabel: '',
    rangeLabel: '本月',
    members: [],
    total: 0
  },

  onLoad(options) {
    const category = options && options.category ? decodeURIComponent(options.category) : '';
    const range = options && options.range ? decodeURIComponent(options.range) : 'month';
    if (!category) {
      this.setData({ loading: false });
      wx.showToast({ title: '缺少违规类型', icon: 'none' });
      return;
    }
    this.category = category;
    this.range = range;
    this.setData({ rangeLabel: RANGE_LABELS[range] || '本月' });
    api.getSupervisorForbiddenPhraseMembers(category, { range, limit: 100 }).then(data => {
      const members = (data.members || []).map(item => Object.assign({}, item, {
        initial: (item.displayName || '学').slice(0, 1),
        scenarioText: (item.scenarios || []).join('、') || '—'
      }));
      this.setData({
        categoryLabel: data.categoryLabel || '违规',
        members,
        total: Number(data.total) || members.length,
        loading: false
      });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '成员明细加载失败', icon: 'none' });
    });
  },

  openMember(e) {
    const memberId = e.currentTarget.dataset.id;
    if (!memberId) return;
    wx.navigateTo({ url: `/pages/member-detail/member-detail?id=${encodeURIComponent(memberId)}` });
  }
});
