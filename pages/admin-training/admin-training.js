const api = require('../../utils/api.js');
/* 计划展示派生（进度 / 截止 / 状态 / 要求文案）统一走 utils/supervisor.js，
   本页不再维护副本，也避免与首页工作台各算一套。 */
const { normalizeSupervisorPlan } = require('../../utils/supervisor.js');

/* 培训计划派生：直接复用单一来源，progressText 仍是裸比值（'3/8'），
   页面自己拼「达标 3/8 人」这层措辞（与工作台统一） */
const normalizePlans = plans => (plans || []).map(item => normalizeSupervisorPlan(item));

Page({
  data: {
    exporting: false,
    loading: true,
    isAdmin: false,
    plans: [],
    /* 加载失败必须留痕：否则会落进「还没有培训计划」空态并把主管
       引导去「发布第一个计划」，团队可能因此收到重复指派。 */
    plansFailed: false,
    trainingLoading: false,
    planStatus: 'all',
    planStatusFilters: [
      { id: 'all', name: '全部' },
      { id: 'active', name: '进行中' },
      { id: 'expired', name: '已到期' }
    ]
  },

  onShow() {
    /* 每次显示都同步 tabBar 角色列表：导航格与页面身份判定必须同源，
       否则会出现「底部写着培训、点进来却说你是学员」这种自相矛盾的场面。 */
    const tabBar = typeof this.getTabBar === 'function' ? this.getTabBar() : null;
    if (tabBar) {
      tabBar.applyRoleList();
      tabBar.setData({ selected: 1 });
    }
    /* 每次进入都重新拉取：从发布页 / 详情页返回后进度会自动更新。
       loading 只在首次鉴权时置位，避免来回切 Tab 时整页闪加载态。 */
    this.loadPage();
  },

  loadPage() {
    api.ensureAuthenticated().then(() => {
      const user = api.getCurrentUser();
      if (!(user && user.role === 'admin')) {
        this.setData({ loading: false, isAdmin: false });
        return;
      }
      this.setData({ loading: false, isAdmin: true }, () => this.loadTrainingPlans());
    }).catch(error => {
      this.setData({ loading: false });
      api.showCenterNotice({ title: error.message || '登录状态获取失败' });
    });
  },

  loadTrainingPlans() {
    this.setData({ trainingLoading: true });
    api.getSupervisorTrainingPlans({ status: this.data.planStatus }).then(data => {
      this.setData({
        plans: normalizePlans(data.plans || []),
        plansFailed: false,
        trainingLoading: false
      });
    }).catch(error => {
      /* 一并清空旧列表：失败时若留着上一次筛选的结果，会显示与当前
         筛选条件不符的数据，比留白更误导。 */
      this.setData({ plans: [], trainingLoading: false, plansFailed: true });
      api.showCenterNotice({ title: error.message || '培训计划加载失败' });
    });
  },

  /* 失败态的自救入口 */
  retryTrainingPlans() {
    this.setData({ plansFailed: false });
    this.loadTrainingPlans();
  },

  selectPlanStatus(e) {
    const planStatus = e.currentTarget.dataset.id;
    if (!planStatus || planStatus === this.data.planStatus) return;
    this.setData({ planStatus }, () => this.loadTrainingPlans());
  },

  openPlanCreate() {
    wx.navigateTo({ url: '/pages/admin-training-plan-create/admin-training-plan-create' });
  },

  /* 「AI 建议」「导出 CSV」都是低频操作，收进动作面板；顶栏只留主行动「＋发布计划」。
     原先 3 个按钮 + 3 个筛选胶囊挤在一行会超宽约 104rpx，把「已到期」顶到第二行。
     面板写法与 pages/roleplay 对齐；用户取消是正常交互，不提示。 */
  openPlanMore() {
    wx.showActionSheet({
      itemList: ['AI 建议', '导出 CSV'],
      success: result => {
        if (result.tapIndex === 0) this.openAiPlans();
        else if (result.tapIndex === 1) this.exportPlanMembersCsv();
      },
      fail: error => {
        if (error && /cancel/i.test(error.errMsg || '')) return;
        api.showCenterNotice({ title: '面板打开失败，请重试' });
      }
    });
  },

  /* AI 建议独立成页：草稿数量不定、每条都要展开推荐理由，塞进本页会把
     进行中的计划挤下去，主管反而看不到最该盯的进度。 */
  openAiPlans() {
    wx.navigateTo({ url: '/pages/admin-ai-plans/admin-ai-plans' });
  },

  /* 导出全部计划的学员进度明细 CSV。
     小程序无法直接下载：后端返回 JSON 包裹的 CSV 文本，前端补 UTF-8 BOM
     写入用户目录后转发文件；转发不可用时降级为复制到剪贴板。 */
  exportPlanMembersCsv() {
    if (this.data.exporting) return;
    /* 收进动作面板后按钮文案不再变化，改由 loading 承担「正在导出」的反馈——
       否则从点击到 shareFileMessage 弹出的这几秒完全没有反馈，读起来像「点了没反应」。
       exporting 仍保留作防重入标志。 */
    this.setData({ exporting: true });
    wx.showLoading({ title: '正在导出…', mask: true });
    api.exportSupervisorReport({ scope: 'plan_members' }).then(data => {
      const csv = `\uFEFF${data.csv || ''}`;
      const filePath = `${wx.env.USER_DATA_PATH}/${data.filename || 'plan-members.csv'}`;
      const filesystem = wx.getFileSystemManager();
      filesystem.writeFile({
        filePath,
        data: csv,
        encoding: 'utf8',
        success: () => {
          wx.hideLoading();
          wx.shareFileMessage({
            filePath,
            fileName: data.filename || 'plan-members.csv',
            success: () => this.setData({ exporting: false }),
            fail: () => {
              this.setData({ exporting: false });
              wx.setClipboardData({
                data: data.csv || '',
                success: () => api.showCenterNotice({ title: '已复制 CSV 内容（转发不可用）' })
              });
            }
          });
        },
        fail: () => {
          wx.hideLoading();
          this.setData({ exporting: false });
          api.showCenterNotice({ title: '文件写入失败', duration: 1800 });
        }
      });
    }).catch(error => {
      wx.hideLoading();
      this.setData({ exporting: false });
      api.showCenterNotice({ title: error.message || '导出失败' });
    });
  },

  openPlanDetail(e) {
    const planId = e.currentTarget.dataset.id;
    if (!planId) return;
    wx.navigateTo({ url: `/pages/admin-training-plan-detail/admin-training-plan-detail?id=${encodeURIComponent(planId)}` });
  },

  /* 无订阅消息通道，提醒动作降级为「复制未完成名单」，由主管自行触达 */
  copyPlanMembers(e) {
    const planId = e.currentTarget.dataset.id;
    if (!planId) return;
    wx.showLoading({ title: '整理名单…', mask: true });
    api.notifyTrainingPlan(planId).then(data => {
      wx.hideLoading();
      const pending = data.pendingLearners || [];
      if (!pending.length) {
        api.showCenterNotice({ title: '该计划已全部完成' });
        return;
      }
      const lines = pending.map((item, index) => `${index + 1}. ${item.displayName}`).join('\n');
      wx.setClipboardData({
        data: lines,
        success: () => wx.showToast({ title: `已复制 ${pending.length} 人名单`, icon: 'none' }),
        fail: () => api.showCenterNotice({ title: '复制失败，请重试' })
      });
    }).catch(error => {
      wx.hideLoading();
      api.showCenterNotice({ title: error.message || '名单获取失败' });
    });
  },

  /* 学员身份误入时的退路 */
  goHome() {
    wx.switchTab({ url: '/pages/home/home' });
  }
});
