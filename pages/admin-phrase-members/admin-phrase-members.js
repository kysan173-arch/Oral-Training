const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');

const RANGE_LABELS = { week: '本周', month: '本月', quarter: '本季度', all: '全部' };
/* 与后端每名成员回带上限对齐（reliable_store.h 的 kEntriesPerMember） */
const ENTRY_PREVIEW_LIMIT = 5;

/* 明细条目：日期经 utils/datetime.js 在 JS 里预算 —— WXML 禁对数据路径调函数，不预算是静默不更新。 */
const buildEntries = (list, totalCount) => {
  const entries = (list || []).slice(0, ENTRY_PREVIEW_LIMIT).map((item, index) => {
    const round = Number(item.round) || 0;
    const deduction = Number(item.deduction) || 0;
    const meta = [item.scenarioName || '', datetime.formatMonthDay(item.date)].filter(Boolean);
    return {
      entryKey: `${item.date || 'na'}-${round}-${index}`,
      roundText: round > 0 ? `第 ${round} 轮` : '轮次未知',
      metaText: meta.join(' · '),
      originalQuote: item.originalQuote || '',
      reason: item.reason || '',
      recommendedRewrite: item.recommendedRewrite || '',
      deductionText: deduction > 0 ? `-${deduction}` : ''
    };
  });
  const count = Number(totalCount) || 0;
  return {
    entries,
    /* 接口只回带扣分最高的几条，超出部分如实说明「没展示全」——不假装这就是全部 */
    overflowText: count > entries.length
      ? `仅展示扣分最高的 ${entries.length} 条，该类共 ${count} 次`
      : ''
  };
};

Page({
  data: {
    loading: true,
    categoryLabel: '',
    rangeLabel: '本月',
    members: [],
    total: 0,
    /* 手风琴：同时只展开一名成员，-1 表示全部收起 */
    expandedIndex: -1
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
      const members = (data.members || []).map(item => {
        const built = buildEntries(item.entries, item.count);
        return Object.assign({}, item, {
          initial: (item.displayName || '学').slice(0, 1),
          scenarioText: (item.scenarios || []).join('、') || '—',
          expanded: false,
          entries: built.entries,
          overflowText: built.overflowText
        });
      });
      this.setData({
        categoryLabel: data.categoryLabel || '违规',
        members,
        total: Number(data.total) || members.length,
        loading: false
      });
    }).catch(error => {
      this.setData({ loading: false });
      api.showCenterNotice({ title: error.message || '成员明细加载失败' });
    });
  },

  /* 点整行就地展开该类违规明细（替代原先直接跳页——跳过去看不到这类违规）。
     展开态在 JS 里预算成布尔字段，符合「派生/选中态不进 WXML 表达式」的约定。 */
  toggleMember(e) {
    /* dataset 兜底：事件对象异常时宁可整页纹丝不动，也不要因取属性而抛错把页面打死 */
    const dataset = (e && e.currentTarget && e.currentTarget.dataset) || {};
    const index = Number(dataset.index);
    if (!Number.isInteger(index) || index < 0 || index >= this.data.members.length) return;
    const next = this.data.expandedIndex === index ? -1 : index;
    this.setData({
      expandedIndex: next,
      members: this.data.members.map((item, i) => Object.assign({}, item, { expanded: i === next }))
    });
  },

  /* 展开区内部点击不应触发收起（否则读到一半点一下就被收走）。
     展开区容器绑 catchtap，区内自己的可点元素再各自 catchtap。 */
  swallowTap() {},

  /* 跳转降级为展开区底部的次要入口：主管想「看这个人整体怎么样」时仍有出口 */
  openMemberDetail(e) {
    const dataset = (e && e.currentTarget && e.currentTarget.dataset) || {};
    const memberId = dataset.id;
    if (!memberId) return;
    wx.navigateTo({ url: `/pages/member-detail/member-detail?id=${encodeURIComponent(memberId)}` });
  }
});
