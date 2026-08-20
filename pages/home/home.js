const api = require('../../utils/api.js');

const DEMO_PHRASES = [
  {
    id: 'p1',
    title: '种植牙价格咨询 · 初次接待话术',
    scenario: '咨询解答',
    tag: '咨询解答',
    difficulty: '初级',
    patientType: '谨慎型',
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
    recommendScenarios: [],
    recentSessions: [],
    overview: { totalCount: 0, averageScore: 0, latestScore: '-' }
  },

  onShow() {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 0 });
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
      this.setData({
        currentUserName: authenticatedUser ? authenticatedUser.displayName : '',
        isAdmin: authenticatedUser ? authenticatedUser.role === 'admin' : false
      });
      if (!authenticatedUser || authenticatedUser.role === 'admin') return;
      this.loadHomeContent();
    }).catch(() => {});
  },

  loadHomeContent() {
    // 数据概览（累计训练 / 平均得分 / 最近得分，聚焦训练成绩，与「我的」打卡区分）
    api.getDashboard().then(dash => {
      const totalCount = dash.totalSessions || 0;
      const averageScore = typeof dash.averageScore === 'number' ? Math.round(dash.averageScore) : 0;
      const recentSessions = dash.recentSessions || [];
      const latest = recentSessions.length
        ? (recentSessions[0].totalScore !== null && recentSessions[0].totalScore !== undefined
            ? recentSessions[0].totalScore : '-')
        : '-';
      this.setData({ overview: { totalCount, averageScore, latestScore: latest } });
    }).catch(() => {});
    // 推荐场景（取前 4 个客服训练场景）
    api.getScenarios().then(data => {
      const items = (data.items || []).slice(0, 4).map(item => ({
        id: item.id,
        name: item.name,
        summary: item.summary,
        difficulty: item.difficulty
      }));
      this.setData({ recommendScenarios: items });
    }).catch(() => {});
    // 最近训练（取最近 4 条）
    api.getSessions({ limit: 4 }).then(data => {
      const items = (data.items || []).map(item => {
        const statusText = item.status === 'in_progress' ? '进行中'
          : item.status === 'abandoned' ? '已放弃' : '已完成';
        const statusType = item.status === 'in_progress' ? 'warn'
          : item.status === 'abandoned' ? 'muted' : 'ok';
        const time = (item.updatedAt || '').slice(0, 10);
        return {
          id: item.id,
          scenarioName: item.scenarioName,
          time,
          statusText,
          statusType
        };
      });
      this.setData({ recentSessions: items });
    }).catch(() => {});
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
    this.setData({ swiperCurrent: e.detail.current });
  },

  viewPhraseDetail(e) {
    const tag = e.currentTarget.dataset.tag;
    wx.navigateTo({ url: `/pages/phrases/phrases?search=${tag || ''}` });
  },

  goTrainingFromHome(e) {
    if (this.data.isAdmin) {
      this.viewDashboard();
      return;
    }
    const scenario = e.currentTarget.dataset.scenario;
    wx.switchTab({ url: '/pages/index/index' });
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
  viewPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); }
});
