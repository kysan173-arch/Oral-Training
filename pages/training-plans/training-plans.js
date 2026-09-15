const api = require('../../utils/api.js');
/* 计划展示派生统一走 utils/plan.js，本页不再维护副本（口径以 plan.js 为准） */
const { STATUS_TEXT, fmt1, formatDue } = require('../../utils/plan.js');

Page({
  data: {
    loading: true,
    plans: [],
    total: 0,
    pendingCount: 0
  },

  onShow() {
    this.loadPlans();
  },

  loadPlans() {
    this.setData({ loading: true });
    api.getLearnerTrainingPlans().then(data => {
      const plans = (data.plans || []).map(item => {
        const required = Number(item.requiredCount) || 1;
        const completed = Number(item.completedCount) || 0;
        const status = item.status || (item.done ? 'done' : item.expired ? 'expired' : 'pending');
        return Object.assign({}, item, {
          periodText: item.period === 'week' ? '按周' : '按月',
          dueText: formatDue(item.dueAt),
          statusText: STATUS_TEXT[status] || '待完成',
          progressPercent: Math.max(0, Math.min(100, Math.round(completed / required * 100))),
          progressText: `${completed}/${required} 次`,
          avgScoreText: fmt1(item.avgScore),
          requirementText: `完成 ≥ ${required} 次 · 平均 ≥ ${item.requiredPassRate} 分`
        });
      });
      this.setData({
        plans,
        total: Number(data.total) || plans.length,
        pendingCount: Number(data.pendingCount) || 0,
        loading: false
      });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '计划加载失败', icon: 'none' });
    });
  },

  openPlan(e) {
    const id = e.currentTarget.dataset.id;
    if (!id) return;
    wx.navigateTo({ url: `/pages/training-plan-detail/training-plan-detail?id=${encodeURIComponent(id)}` });
  },

  goTraining() {
    wx.switchTab({ url: '/pages/home/home' });
  }
});
