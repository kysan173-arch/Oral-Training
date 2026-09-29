const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');

/* 派生字段一律在 JS 里算好：WXML 表达式不支持方法调用，写 {{scenes[id]}} 这类
   依赖追踪会失效且不报错（详见 MEMORY「WXML 渲染硬约束」）。 */
Page({
  data: {
    loading: true,
    isAdmin: false,
    generating: false,
    drafts: [],
    /* 加载失败必须与「暂无建议」区分：否则网络抖动会把主管引导去重新生成，
       白等一轮模型调用还多花钱。 */
    draftsFailed: false
  },

  onShow() {
    /* 与「培训」页同源：都在培训 tab 下，导航格与页面身份判定必须一致 */
    const tabBar = typeof this.getTabBar === 'function' ? this.getTabBar() : null;
    if (tabBar) {
      tabBar.applyRoleList();
      tabBar.setData({ selected: 1 });
    }
    this.loadPage();
  },

  loadPage() {
    api.ensureAuthenticated().then(() => {
      const user = api.getCurrentUser();
      if (!(user && user.role === 'admin')) {
        this.setData({ loading: false, isAdmin: false });
        return;
      }
      this.setData({ loading: false, isAdmin: true }, () => this.loadDrafts());
    }).catch(error => {
      this.setData({ loading: false });
      api.showCenterNotice({ title: error.message || '登录状态获取失败' });
    });
  },

  /* 场景目录与草稿并行取：草稿里只有 scenarioIds，主管必须看到中文场景名
     才能判断这条建议值不值得采纳。目录取不到时降级显示原始 id，不阻断审核。 */
  loadDrafts() {
    return Promise.all([
      api.getTrainingPlanDrafts(),
      api.getSupervisorScenarios().catch(() => ({ items: [] }))
    ]).then(results => {
      const draftData = results[0] || {};
      const sceneData = results[1] || {};
      const sceneNames = {};
      (sceneData.items || []).forEach(scene => {
        if (scene && scene.id) sceneNames[scene.id] = scene.name;
      });
      const drafts = (draftData.items || []).map(item => this.decorateDraft(item, sceneNames));
      this.setData({ drafts, draftsFailed: false });
    }).catch(error => {
      this.setData({ draftsFailed: true });
      api.showCenterNotice({ title: error.message || '训练建议加载失败' });
    });
  },

  decorateDraft(item, sceneNames) {
    const ids = item.scenarioIds || [];
    /* 目标维度的中文名由后端下发（focusDimensionLabel，源自 planDimensions()），
       前端不另建 key→中文名映射，避免两套叫法。 */
    const focusLabel = item.focusDimensionLabel || '';
    return Object.assign({}, item, {
      sceneNames: ids.map(id => sceneNames[id] || id),
      hasFocus: !!focusLabel,
      focusText: focusLabel ? '针对「' + focusLabel + '」' : '',
      /* 达标口径随目标维度变化：有计划维度时看的不是综合分，文案必须跟着改，
         否则主管会以为「平均分 60」仍指综合分。 */
      requirementText: '完成 ' + item.requiredCount + ' 次，' + (focusLabel ? focusLabel : '综合')
        + (item.requireEachPass === true ? '每次' : '均分') + ' ≥ ' + item.requiredPassRate,
      periodText: item.period === 'month' ? '按月' : '按周',
      dueText: datetime.formatDate(item.dueAt) || '待定',
      publishing: false,
      dismissing: false
    });
  },

  generateSuggestions() {
    if (this.data.generating) return;
    this.setData({ generating: true });
    api.suggestTrainingPlans({}).then(data => {
      const generated = (data && data.generatedCount) || 0;
      const skipped = (data && data.skipped) || [];
      this.setData({ generating: false });
      if (generated === 0) {
        /* 全失败时把第一个原因透出来，否则主管只看到「没有生成」无从下手 */
        const reason = skipped.length ? '：' + skipped[0].reason : '';
        api.showCenterNotice({ title: '没有生成新的训练建议' + reason });
      } else if (skipped.length) {
        api.showCenterNotice({ title: '已生成 ' + generated + ' 条，' + skipped.length + ' 名学员失败' });
      } else {
        api.showCenterNotice({ title: '已生成 ' + generated + ' 条训练建议' });
      }
      return this.loadDrafts();
    }).catch(error => {
      this.setData({ generating: false });
      api.showCenterNotice({ title: error.message || '生成训练建议失败' });
    });
  },

  /* 采纳即发布：草稿一旦指派就会进学员待办，属于对外动作，必须二次确认。 */
  publishDraft(event) {
    const planId = event.currentTarget.dataset.id;
    const draft = this.data.drafts.filter(item => item.id === planId)[0];
    if (!draft || draft.publishing) return;
    wx.showModal({
      title: '采纳并发布',
      content: '将「' + draft.title + '」指派给 ' + draft.learnerName + '？发布后学员会在待办中看到，截止时间按草稿设定生效。',
      confirmText: '采纳发布',
      success: res => {
        if (!res.confirm) return;
        this.setDraftFlag(planId, { publishing: true });
        api.publishTrainingPlan(planId, {}).then(() => {
          this.setDraftFlag(planId, { publishing: false });
          api.showCenterNotice({ title: '已发布给学员' });
          return this.loadDrafts();
        }).catch(error => {
          this.setDraftFlag(planId, { publishing: false });
          api.showCenterNotice({ title: error.message || '发布失败' });
        });
      }
    });
  },

  /* 「编辑后发布」：把草稿内容整体带给建计划页，由它以 publishMode 提交
     publishPlan（覆盖草稿字段并发布），而不是新建计划。不在这里内嵌一套
     表单——场景多选、维度选择、时间选择都已在那一页实现好，复制第二份必然
     会漂移出两套字段口径。 */
  editDraft(event) {
    const planId = event.currentTarget.dataset.id;
    const draft = this.data.drafts.filter(item => item.id === planId)[0];
    if (!draft) return;
    wx.navigateTo({
      url: '/pages/admin-training-plan-create/admin-training-plan-create?mode=publish&planId='
        + encodeURIComponent(planId),
      success: res => {
        /* 目标页在 onLoad 里就注册了 channel.on；navigateTo 的 success 在 onLoad
           之后触发，因此这里 emit 不会丢。字段名与 listPlanDrafts 的下发一致。 */
        if (!res.eventChannel || typeof res.eventChannel.emit !== 'function') return;
        res.eventChannel.emit('prefillPlan', {
          title: draft.title,
          period: draft.period,
          scenarioIds: draft.scenarioIds,
          requiredCount: draft.requiredCount,
          requiredPassRate: draft.requiredPassRate,
          description: draft.description,
          dueAt: draft.dueAt,
          focusDimension: draft.focusDimension,
          learnerName: draft.learnerName
        });
      }
    });
  },

  dismissDraft(event) {
    const planId = event.currentTarget.dataset.id;
    const draft = this.data.drafts.filter(item => item.id === planId)[0];
    if (!draft || draft.dismissing) return;
    wx.showModal({
      title: '丢弃建议',
      content: '丢弃后不再显示（记录会保留，用于统计 AI 建议的采纳率）。',
      confirmText: '丢弃',
      confirmColor: '#A32D2D',
      success: res => {
        if (!res.confirm) return;
        this.setDraftFlag(planId, { dismissing: true });
        api.dismissTrainingPlan(planId).then(() => {
          this.setDraftFlag(planId, { dismissing: false });
          return this.loadDrafts();
        }).catch(error => {
          this.setDraftFlag(planId, { dismissing: false });
          api.showCenterNotice({ title: error.message || '丢弃失败' });
        });
      }
    });
  },

  /* 只改单张卡片的进行态，避免整列表重渲染导致滚动位置跳动 */
  setDraftFlag(planId, patch) {
    const drafts = this.data.drafts.map(item =>
      item.id === planId ? Object.assign({}, item, patch) : item);
    this.setData({ drafts });
  },

  retryDrafts() {
    this.loadDrafts();
  },

  backToTraining() {
    wx.navigateBack({ delta: 1 });
  }
});
