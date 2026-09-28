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
      /* 计划指定了目标维度时，达标判定用的是「该维度均分」（item.score），
         而 item.avgScore 恒为综合分——两者不是同一个数。后端在该维度没有有效
         评分时会回退综合分（scoreBasis === 'total'），此处必须把这个回退写出来，
         否则主管会拿页面上显示的综合分去核对判定结论，永远对不上。 */
      const focusLabel = (data.plan && data.plan.focusDimensionLabel) || '';
      const assignments = (data.assignments || []).map(item => {
        const fallback = !!focusLabel && item.scoreBasis === 'total';
        return Object.assign({}, item, {
          initial: (item.displayName || '学').slice(0, 1),
          avgScoreText: fmt1(item.avgScore),
          progressText: `${item.completedCount}/${data.plan.requiredCount}`,
          lastText: item.lastTrainingDate ? `最近训练：${item.lastTrainingDate}` : '暂无有效训练',
          basisText: !focusLabel ? ''
            : (fallback
                ? `判定依据：该维度暂无有效评分，已按综合均分 ${fmt1(item.score)} 分判定`
                : `判定依据：${focusLabel}均分 ${fmt1(item.score)} 分`)
        });
      });
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
      api.showCenterNotice({ title: error.message || '计划详情加载失败' });
    });
  },

  copyPending() {
    if (this.data.copying) return;
    this.setData({ copying: true });
    api.notifyTrainingPlan(this.planId).then(data => {
      const pending = data.pendingLearners || [];
      this.setData({ copying: false });
      if (!pending.length) {
        api.showCenterNotice({ title: '该计划已全部达标' });
        return;
      }
      const lines = pending.map((item, index) =>
        `${index + 1}. ${item.displayName}（${item.completedCount} 次 / ${item.avgScore} 分）`).join('\n');
      wx.setClipboardData({
        data: lines,
        success: () => wx.showToast({ title: `已复制 ${pending.length} 人名单`, icon: 'none' }),
        fail: () => api.showCenterNotice({ title: '复制失败，请重试' })
      });
    }).catch(error => {
      this.setData({ copying: false });
      api.showCenterNotice({ title: error.message || '名单获取失败' });
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
          /* 目标维度与防刷上限必须一起带过去：漏掉它们会让重发的计划静默改成
             「不限维度 + 不限场景次数」，达标口径被悄悄放宽。 */
          focusDimension: plan.focusDimension || '',
          maxPerScenario: plan.maxPerScenario,
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
