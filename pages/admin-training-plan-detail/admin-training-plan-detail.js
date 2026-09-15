const api = require('../../utils/api.js');
/* 计划展示派生统一走 utils/supervisor.js（单一来源）；本页只额外用 fmt1 渲染成员均分 */
const { normalizeSupervisorPlan } = require('../../utils/supervisor.js');
const { fmt1 } = require('../../utils/plan.js');

Page({
  data: {
    loading: true,
    plan: null,
    assignments: [],
    doneCount: 0,
    pendingCount: 0,
    progressPercent: 0,
    copying: false
  },

  onLoad(options) {
    const id = options && options.id ? decodeURIComponent(options.id) : '';
    if (!id) {
      this.setData({ loading: false });
      wx.showToast({ title: '缺少计划标识', icon: 'none' });
      return;
    }
    this.planId = id;
    this.loadDetail();
  },

  loadDetail() {
    this.setData({ loading: true });
    api.getTrainingPlan(this.planId).then(data => {
      const plan = normalizeSupervisorPlan(data.plan, {
        assignmentCount: data.assignmentCount,
        doneCount: data.doneCount
      });
      const assignmentCount = Number(data.assignmentCount) || 0;
      const doneCount = Number(data.doneCount) || 0;
      const assignments = (data.assignments || []).map(item => Object.assign({}, item, {
        initial: (item.displayName || '学').slice(0, 1),
        avgScoreText: fmt1(item.avgScore),
        progressText: `${item.completedCount}/${data.plan.requiredCount}`,
        lastText: item.lastTrainingDate ? `最近训练：${item.lastTrainingDate}` : '暂无有效训练'
      }));
      this.setData({
        plan,
        assignments,
        doneCount,
        pendingCount: assignmentCount - doneCount,
        progressPercent: assignmentCount ? Math.round(doneCount / assignmentCount * 100) : 0,
        loading: false
      });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '计划详情加载失败', icon: 'none' });
    });
  },

  copyPending() {
    if (this.data.copying) return;
    this.setData({ copying: true });
    api.notifyTrainingPlan(this.planId).then(data => {
      const pending = data.pendingLearners || [];
      this.setData({ copying: false });
      if (!pending.length) {
        wx.showToast({ title: '该计划已全部达标', icon: 'none' });
        return;
      }
      const lines = pending.map((item, index) =>
        `${index + 1}. ${item.displayName}（${item.completedCount} 次 / ${item.avgScore} 分）`).join('\n');
      wx.setClipboardData({
        data: lines,
        success: () => wx.showToast({ title: `已复制 ${pending.length} 人名单`, icon: 'none' }),
        fail: () => wx.showToast({ title: '复制失败，请重试', icon: 'none' })
      });
    }).catch(error => {
      this.setData({ copying: false });
      wx.showToast({ title: error.message || '名单获取失败', icon: 'none' });
    });
  },

  /* 到期计划重发：带着原计划的内容进发布页预填。
     注意：详情接口不返回原始指派名单（targetUserIds），所以预填只含
     内容要素，发布对象默认「全团队成员」，主管可在发布页改选。 */
  republishPlan() {
    const plan = this.data.plan;
    if (!plan) return;
    wx.navigateTo({
      url: '/pages/admin-training-plan-create/admin-training-plan-create',
      success: res => {
        res.eventChannel.emit('prefillPlan', {
          title: plan.title,
          period: plan.period,
          scenarioIds: plan.scenarioIds || [],
          requiredCount: plan.requiredCount,
          requiredPassRate: plan.requiredPassRate,
          description: plan.description || ''
        });
      }
    });
  },

  openMember(e) {
    const memberId = e.currentTarget.dataset.id;
    if (!memberId) return;
    wx.navigateTo({ url: `/pages/member-detail/member-detail?id=${encodeURIComponent(memberId)}` });
  }
});
