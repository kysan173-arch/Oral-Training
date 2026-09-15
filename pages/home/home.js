const api = require('../../utils/api.js');
const plan = require('../../utils/plan.js');
const datetime = require('../../utils/datetime.js');
const supervisor = require('../../utils/supervisor.js');

const DEMO_PHRASES = [
  {
    id: 'p1',
    title: '种植牙价格咨询 · 初次接待话术',
    scenario: '咨询解答',
    tag: '咨询解答',
    difficulty: '初级',
    patientType: '谨慎型',
    scenarioId: 'implant-basic',
    // 患者画像交给 createSession 的 customPatientProfile：description 需能直接接在「我最近」之后；
    // 不传 emotion 时后端会生成「现在有点 + emotion」句式，平静类场景留空更自然。
    patientProfile: {
      age: '45',
      description: '缺了一颗后牙，想先问问种植牙大概要花多少钱'
    },
    dialog: [
      { role: 'patient', text: '你好，我想了解一下种植牙大概多少钱？' },
      { role: 'agent', text: '您好！种植牙的费用主要取决于种植体品牌、牙冠材料和手术复杂度三个因素。我们这边韩国品牌大概8000起，欧美品牌15000起。方便的话我帮您预约个免费检查，医生会根据您的口腔情况给出精准方案和报价。' },
      { role: 'patient', text: '那有没有便宜点的？我还是想先了解一下。' },
      { role: 'agent', text: '理解您的顾虑。其实我们有分期付款方案，首付只需30%。而且种植牙能用几十年，平均下来每天不到几块钱。要不我先帮您看看口腔情况，再推荐最适合您预算的方案？' }
    ]
  },
  {
    id: 'p2',
    title: '患者担心手术风险 · 安抚与解释',
    scenario: '咨询解答',
    tag: '咨询解答',
    difficulty: '中级',
    patientType: '焦虑型',
    scenarioId: 'implant-basic',
    // 「担心」命中后端情绪词表，开场白会走「心里挺担心的」句式
    patientProfile: {
      age: '52',
      description: '缺牙想做种植牙，可一直听说会很痛，我拿不定主意',
      emotion: '担心',
      emotionLevel: -1
    },
    dialog: [
      { role: 'patient', text: '我听说种植牙手术很痛，有点害怕……' },
      { role: 'agent', text: '我很理解您的担心。其实现在的种植技术已经非常成熟了，手术会在局部麻醉下进行，术中是基本没有痛感的。我们很多患者反馈说，最大的感受反而是紧张，真正的不适感比拔牙还轻呢。' },
      { role: 'patient', text: '那万一失败了呢？' },
      { role: 'agent', text: '您放心，我们使用的都是国际一线品牌种植体，医生也都是有10年以上经验的。临床成功率在98%以上。而且我们会为您做全面的术前检查，如果骨量不足会提前告知，不会冒然手术。' }
    ]
  },
  {
    id: 'p3',
    title: '患者觉得太贵 · 价值塑造与引导',
    scenario: '价格异议',
    tag: '价格异议',
    difficulty: '中级',
    patientType: '比价型',
    scenarioId: 'price-comparison',
    // 比价叙说本身就是诉求，再拼「能麻烦您帮我看看是怎么回事吗」会串味，故只传 emotionLevel
    patientProfile: {
      age: '45',
      description: '在别家也问过价，觉得你们这边报价偏高，想再比较一下差别在哪',
      emotionLevel: -1
    },
    dialog: [
      { role: 'patient', text: '你们这也太贵了，我问了别家才6000多。' },
      { role: 'agent', text: '确实，市场上种植牙的价格差异挺大的。价格差异主要在几个方面：品牌档次不同、医生经验水平不同、术后保障也不同。我们用的是欧美进口品牌，提供5年质保和终身维护。您知道吗，低价种植体万一出了问题，修复的成本往往比差价还高。' },
      { role: 'patient', text: '那我想再比较一下。' },
      { role: 'agent', text: '完全理解，毕竟是一笔重要投资。要不您先把其他家的方案告诉我，我帮您对比一下材料、质保这些核心差异？有时候只看价格容易被误导。方便的话可以先来做个免费检查，给我一个向您展示的机会。' }
    ]
  },
  {
    id: 'p4',
    title: '术后不适 · 专业安抚回复',
    scenario: '投诉安抚',
    tag: '投诉安抚',
    difficulty: '高级',
    patientType: '焦虑型',
    scenarioId: 'post-treatment-discomfort',
    // 术后场景末尾的「能麻烦您帮我看看是怎么回事吗」正合诉求，可安全使用情绪词
    patientProfile: {
      age: '38',
      description: '做完种植牙三周了，牙龈还有点肿，担心是不是没做好',
      emotion: '焦虑',
      emotionLevel: -2
    },
    dialog: [
      { role: 'patient', text: '我种牙三周了，牙龈还是有点肿，是不是没做好？' },
      { role: 'agent', text: '您好，术后三周牙龈有轻微的肿胀和不适是正常的愈合反应。请问现在有没有剧烈疼痛？有没有流脓或异常出血？' },
      { role: 'patient', text: '疼倒不是很疼，就是有点担心。' },
      { role: 'agent', text: '那您别太担心，这是伤口愈合期的正常现象。每个人的愈合速度不同。建议您继续保持口腔清洁，早晚用我们开的漱口水漱口。方便的话明天来复查一下，我们免费帮您检查，让医生看看愈合情况，您也安心。' }
    ]
  }
];

Page({
  data: {
    greeting: '',
    currentUserName: '',
    isAdmin: false,
    searchQuery: '',
    searchFocused: false,
    hotPhrases: [],
    swiperCurrent: 0,
    expandedPhraseId: '',
    expandedPhrase: null,
    recommendScenarios: [],
    recentSessions: [],
    overview: { totalCount: 0, averageScore: 0, latestScore: '-' },
    /* 横幅展示的是「最紧急的那个待办计划」摘要，无待办时为 null，横幅整体不渲染 */
    planNotice: null,
    /* 主管工作台：由 utils/supervisor.js 组装好的纯数据对象，页面不再派生 */
    workbench: null,
    /* 失败态标记：请求挂掉时显示「加载失败，点击重试」，
       不允许失败被渲染成 0 或「暂无记录」（假阴性比空屏更误导） */
    overviewFailed: false,
    recommendFailed: false,
    recentFailed: false,
    workbenchFailed: false
  },

  onShow() {
    /* 自定义 tabBar 的角色列表只在组件创建时算一次：切换身份后旧列表会残留下来
       （学员身份却还留着主管的「培训」格，点进去就是主管页），所以每次显示都同步。 */
    const tabBar = typeof this.getTabBar === 'function' ? this.getTabBar() : null;
    if (tabBar) {
      tabBar.applyRoleList();
      tabBar.setData({ selected: 0 });
    }
    const hour = new Date().getHours();
    const timeWord = hour < 12 ? '早上好' : hour < 18 ? '下午好' : '晚上好';
    this.setData({ greeting: timeWord, hotPhrases: DEMO_PHRASES });
    const currentUser = api.getCurrentUser();
    this.setData({
      currentUserName: currentUser ? currentUser.displayName : '',
      isAdmin: currentUser ? currentUser.role === 'admin' : false
    });
    api.ensureAuthenticated().then(() => {
      const authenticatedUser = api.getCurrentUser();
      const isAdmin = authenticatedUser ? authenticatedUser.role === 'admin' : false;
      this.setData({
        currentUserName: authenticatedUser ? authenticatedUser.displayName : '',
        isAdmin
      });
      if (!authenticatedUser) return;
      /* 主管端首页是「行动工作台」：只回答「今天要处理什么」。
         趋势、五维、排行榜等分析一律留在「数据」tab，不在这里重复一份。 */
      if (isAdmin) {
        this.loadSupervisorHome();
        return;
      }
      this.loadHomeContent();
    }).catch(() => {
      wx.showToast({ title: '登录状态获取失败，请检查网络', icon: 'none' });
    });
  },

  // 各区块失败后的统一重试入口：重新拉一遍全部四路
  retryHomeContent() {
    this.loadHomeContent();
  },

  loadHomeContent() {
    // 进来先清上一轮的失败标记，避免重试成功后旧标记残留
    this.setData({ overviewFailed: false, recommendFailed: false, recentFailed: false });
    // 培训计划提醒：横幅直接显示最紧急计划的标题、剩余时间与达标差额
    // （横幅属低风险增强，失败就让横幅缺席，不单独报错）
    api.getLearnerTrainingPlans().then(data => {
      this.setData({ planNotice: plan.pickPlanNotice(data.plans) });
    }).catch(() => {});
    // 数据概览（累计训练 / 平均得分 / 最近得分，聚焦训练成绩，与「我的」打卡区分）
    api.getDashboard().then(dash => {
      const totalCount = dash.totalSessions || 0;
      const averageScore = api.formatScore(dash.averageScore);
      const recentSessions = dash.recentSessions || [];
      const latest = recentSessions.length
        ? (recentSessions[0].totalScore !== null && recentSessions[0].totalScore !== undefined
            ? recentSessions[0].totalScore : '-')
        : '-';
      this.setData({ overview: { totalCount, averageScore, latestScore: latest } });
    }).catch(() => {
      this.setData({ overviewFailed: true });
    });
    // 推荐场景（取前 4 个客服训练场景）
    api.getScenarios().then(data => {
      const items = (data.items || []).slice(0, 4).map(item => ({
        id: item.id,
        name: item.name,
        summary: item.summary,
        difficulty: item.difficulty
      }));
      this.setData({ recommendScenarios: items });
    }).catch(() => {
      this.setData({ recommendFailed: true });
    });
    // 最近训练（取最近 4 条）
    api.getSessions({ limit: 4 }).then(data => {
      const items = (data.items || []).map(item => {
        const statusText = item.status === 'in_progress' ? '进行中'
          : item.status === 'abandoned' ? '已放弃' : '已完成';
        const statusType = item.status === 'in_progress' ? 'warn'
          : item.status === 'abandoned' ? 'muted' : 'ok';
        const time = datetime.formatDate(item.updatedAt);
        return {
          id: item.id,
          scenarioName: item.scenarioName,
          time,
          statusText,
          statusType
        };
      });
      this.setData({ recentSessions: items });
    }).catch(() => {
      this.setData({ recentFailed: true });
    });
  },

  /* ── 主管工作台 ──
     四路并行：团队聚合 / 成员摘要 / 进行中计划 / 可添加学员。
     每一路单独 catch 成 null——任一接口失败只让对应区块降级为空，
     不能整页卡在加载态（与「数据」tab 的容错口径一致）。
     但失败必须留痕（workbenchFailed）：否则后端挂掉会被渲染成
     「0 待办 / 团队节奏正常」，失败与太平长得一模一样。 */
  loadSupervisorHome() {
    if (this._loadingWorkbench) return;
    this._loadingWorkbench = true;
    Promise.all([
      api.getSupervisorDashboard({ range: 'month' }).catch(() => null),
      api.getSupervisorMembers({ limit: 100 }).catch(() => null),
      api.getSupervisorTrainingPlans({ status: 'active' }).catch(() => null),
      api.getTeamCandidates({ limit: 1 }).catch(() => null)
    ]).then(results => {
      this._loadingWorkbench = false;
      const dashboard = results[0];
      const memberData = results[1];
      const planData = results[2];
      const candidateData = results[3];
      const failed = results.some(result => result === null);
      this.setData({
        workbenchFailed: failed,
        workbench: supervisor.buildWorkbench({
          dashboard,
          members: (memberData && memberData.members) || [],
          plans: (planData && planData.plans) || [],
          candidateCount: (candidateData && candidateData.totalCandidates) || 0,
          range: 'month'
        })
      });
    }).catch(() => { this._loadingWorkbench = false; });
  },

  /* 工作台失败态重试入口 */
  retryWorkbench() {
    this.loadSupervisorHome();
  },

  /* 工作台：待辅导成员 → 成员详情（与「数据」tab 成员管理是同一个目标页） */
  openWorkbenchMember(e) {
    const memberId = e.currentTarget.dataset.id;
    if (!memberId) return;
    wx.navigateTo({ url: `/pages/member-detail/member-detail?id=${encodeURIComponent(memberId)}` });
  },

  /* 工作台：计划进度 → 计划详情 */
  openWorkbenchPlan() {
    const risk = this.data.workbench && this.data.workbench.planRisk;
    if (!risk || !risk.id) return;
    wx.navigateTo({ url: `/pages/admin-training-plan-detail/admin-training-plan-detail?id=${encodeURIComponent(risk.id)}` });
  },

  /* 工作台：未归属学员 → 我的团队。只有真有人可加时才渲染入口 */
  openWorkbenchCandidates() {
    wx.navigateTo({ url: '/pages/team-members/team-members' });
  },

  /* 工作台：训练场景管理（新建 / 编辑 / 上下架） */
  openScenarioAdmin() {
    wx.navigateTo({ url: '/pages/admin-scenarios/admin-scenarios' });
  },

  // 培训计划横幅入口：主体直达最紧急计划的明细
  openUrgentPlan() {
    const notice = this.data.planNotice;
    if (!notice || !notice.id) return;
    wx.navigateTo({ url: `/pages/training-plan-detail/training-plan-detail?id=${encodeURIComponent(notice.id)}` });
  },

  // 横幅右上角「全部 N 个」：进计划列表看全量
  openMyPlans() {
    wx.navigateTo({ url: '/pages/training-plans/training-plans' });
  },

  // 导航：训练场景
  startFromHome() {
    wx.switchTab({ url: '/pages/index/index' });
  },

  viewHistory() {
    wx.navigateTo({ url: '/pages/report/report' });
  },

  // 搜索
  onSearchInput(e) {
    this.setData({ searchQuery: e.detail.value });
  },

  onSearchFocus() {
    this.setData({ searchFocused: true });
  },

  onSearchBlur() {
    this.setData({ searchFocused: false });
  },

  onSearch(e) {
    const query = (e.detail.value || this.data.searchQuery || '').trim();
    if (!query) return;
    wx.navigateTo({ url: `/pages/phrases/phrases?search=${query}` });
  },

  onSearchTap() {
    const query = (this.data.searchQuery || '').trim();
    if (!query) return;
    wx.navigateTo({ url: `/pages/phrases/phrases?search=${query}` });
  },

  // 热门话术轮播
  onSwiperChange(e) {
    // 切换卡片即收起示范，避免面板与当前卡片对不上
    this.setData({ swiperCurrent: e.detail.current, expandedPhraseId: '', expandedPhrase: null });
  },

  // 就地展开 / 收起标准对话示范
  togglePhraseDetail(e) {
    const id = e.currentTarget.dataset.id;
    const expanding = this.data.expandedPhraseId !== id;
    const phrase = expanding ? this.data.hotPhrases.find(item => item.id === id) || null : null;
    this.setData({ expandedPhraseId: expanding ? id : '', expandedPhrase: phrase });
  },

  // 直达对练舱：有进行中的会话就续练，没有再新建
  // patientProfile 留空时走场景自带的 hidden_config 设定（姓名/诉求/开场白）
  enterScenario(scenarioId, patientProfile) {
    if (this._enteringSession) return;
    this._enteringSession = true;
    const release = () => { this._enteringSession = false; };
    wx.showLoading({ title: '正在进入训练…', mask: true });
    api.getScenarios().then(data => {
      const scenario = (data.items || []).find(item => item.id === scenarioId);
      if (scenario && scenario.activeSession) {
        wx.hideLoading();
        wx.navigateTo({ url: `/pages/training/training?sessionId=${scenario.activeSession.id}` });
        return null;
      }
      return api.createSession(scenarioId, patientProfile);
    }).then(data => {
      wx.hideLoading();
      release();
      if (data && data.session) {
        wx.navigateTo({ url: `/pages/training/training?sessionId=${data.session.id}` });
      }
    }).catch(error => {
      wx.hideLoading();
      release();
      wx.showToast({ title: error.message || '进入训练失败', icon: 'none' });
      setTimeout(() => wx.switchTab({ url: '/pages/index/index' }), 800);
    });
  },

  // 推荐场景：场景 id 直接来自后端，无需自定义患者画像
  startScenarioFromHome(e) {
    if (this.data.isAdmin) {
      this.viewDashboard();
      return;
    }
    const scenarioId = e.currentTarget.dataset.id;
    if (!scenarioId) {
      wx.switchTab({ url: '/pages/index/index' });
      return;
    }
    this.enterScenario(scenarioId);
  },

  // 热门话术：携带该话术专属的患者画像，一步直达对练舱
  goTrainingFromHome(e) {
    if (this.data.isAdmin) {
      this.viewDashboard();
      return;
    }
    const id = e.currentTarget.dataset.id;
    const phrase = this.data.hotPhrases.find(item => item.id === id);
    if (!phrase || !phrase.scenarioId) {
      wx.switchTab({ url: '/pages/index/index' });
      return;
    }
    this.enterScenario(phrase.scenarioId, phrase.patientProfile || {});
  },

  // 导航
  startTraining() {
    if (this.data.isAdmin) {
      this.viewDashboard();
      return;
    }
    wx.switchTab({ url: '/pages/index/index' });
  },
  viewDashboard() { wx.switchTab({ url: '/pages/admin/admin' }); },
  viewPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); },

  showRequestError(error) {
    wx.showToast({ title: error.message || '后端服务不可用', icon: 'none' });
  }
});
