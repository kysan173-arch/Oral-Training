const api = require('../../utils/api.js');

const CATEGORY_CONFIG = [
  { id: 'consultation', name: '咨询解答', icon: '💬', description: '先了解患者关切，再清楚说明服务边界。' },
  { id: 'price_negotiation', name: '价格异议', icon: '🧾', description: '客观说明费用流程，不承诺固定价格。' },
  { id: 'complaint_handling', name: '投诉与不适', icon: '🤝', description: '先回应情绪，及时引导联系医生或复诊。' },
  { id: 'recommendation', name: '项目推荐', icon: '🧭', description: '从真实需求出发，不替代医生判断。' }
];

const inferCategory = item => {
  if (CATEGORY_CONFIG.some(category => category.id === item.category)) return item.category;
  if (/比价|报价|价格/.test(item.name)) return 'price_negotiation';
  if (/术后|不适|投诉/.test(item.name)) return 'complaint_handling';
  return 'consultation';
};

const buildCategories = scenarios => CATEGORY_CONFIG.map(category => Object.assign({}, category, {
  items: scenarios.filter(item => item.category === category.id)
})).filter(category => category.items.length > 0);

Page({
  data: {
    scenarios: [],
    categories: [],
    visibleCategories: [],
    activeCategoryId: '',
    expandedId: '',
    trainingMode: 'customer_service'
  },

  onShow() { this.loadScenarios(); },

  loadScenarios() {
    this.scenarioRequestVersion = (this.scenarioRequestVersion || 0) + 1;
    const requestVersion = this.scenarioRequestVersion;
    const requestedMode = this.data.trainingMode;
    const isRoleplay = requestedMode === 'patient_simulation';
    const request = isRoleplay ? api.getRoleplayScenarios() : api.getScenarios();
    request.then(data => {
      if (requestVersion !== this.scenarioRequestVersion || requestedMode !== this.data.trainingMode) return;
      const scenarios = data.items.map(item => Object.assign({}, item, {
        category: inferCategory(item),
        difficulty: item.difficulty === 'advanced' ? '进阶' : '基础',
        patientAge: `${item.patientProfile.age}岁`,
        patientConcern: item.patientProfile.description,
        patientEmotion: isRoleplay ? '由你自由提问' : '需通过对话了解',
        actionText: item.activeSession
          ? (isRoleplay ? '继续模拟' : '继续训练')
          : (isRoleplay ? '开始模拟' : '开始训练'),
        suggestedQuestions: item.suggestedQuestions || []
      }));
      const categories = buildCategories(scenarios);
      const activeCategoryId = categories.some(item => item.id === this.data.activeCategoryId)
        ? this.data.activeCategoryId : '';
      const visibleCategories = activeCategoryId
        ? categories.filter(item => item.id === activeCategoryId) : categories;
      this.setData({ scenarios, categories, visibleCategories, activeCategoryId, expandedId: '' });
    }).catch(error => {
      if (requestVersion !== this.scenarioRequestVersion || requestedMode !== this.data.trainingMode) return;
      wx.showToast({ title: error.message || '场景加载失败', icon: 'none' });
    });
  },

  switchMode(e) {
    const mode = e.currentTarget.dataset.mode;
    if (!mode || mode === this.data.trainingMode) return;
    this.setData({ trainingMode: mode, scenarios: [], categories: [], visibleCategories: [], expandedId: '' }, () => this.loadScenarios());
  },

  selectCategory(e) {
    const activeCategoryId = e.currentTarget.dataset.id || '';
    const visibleCategories = activeCategoryId
      ? this.data.categories.filter(item => item.id === activeCategoryId) : this.data.categories;
    this.setData({ activeCategoryId, visibleCategories, expandedId: '' });
  },

  toggleProfile(e) {
    const id = e.currentTarget.dataset.id;
    this.setData({ expandedId: this.data.expandedId === id ? '' : id });
  },

  openTraining(e) {
    const { id, mode } = e.currentTarget.dataset;
    const scenario = this.data.scenarios.find(item => item.id === id);
    if (!scenario) return;
    if (this.data.trainingMode === 'patient_simulation') {
      this.openRoleplay(scenario, mode, '');
      return;
    }
    if (mode === 'continue' && scenario.activeSession) {
      this.goTraining(scenario.activeSession.id);
      return;
    }
    api.createSession(id).then(data => this.goTraining(data.session.id))
      .catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  },

  openSuggestion(e) {
    const scenario = this.data.scenarios.find(item => item.id === e.currentTarget.dataset.id);
    if (!scenario) return;
    this.openRoleplay(scenario, scenario.activeSession ? 'continue' : 'new', e.currentTarget.dataset.prompt || '');
  },

  openRoleplay(scenario, mode, prompt) {
    if (mode === 'continue' && scenario.activeSession) {
      this.goRoleplay(scenario.activeSession.id, prompt);
      return;
    }
    api.createRoleplaySession(scenario.id).then(data => this.goRoleplay(data.session.id, prompt))
      .catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  },

  restartTraining(e) {
    const id = e.currentTarget.dataset.id;
    const isRoleplay = this.data.trainingMode === 'patient_simulation';
    wx.showModal({
      title: isRoleplay ? '重新开始患者模拟？' : '重新开始训练？',
      content: '当前未完成会话会标记为已放弃，历史记录仍会保留。',
      confirmText: '重新开始',
      success: result => {
        if (!result.confirm) return;
        const scenario = this.data.scenarios.find(item => item.id === id);
        if (!scenario || !scenario.activeSession) return;
        const request = isRoleplay
          ? api.restartRoleplaySession(scenario.activeSession.id)
          : api.restartSession(scenario.activeSession.id);
        request.then(data => {
          if (isRoleplay) this.goRoleplay(data.session.id, '');
          else this.goTraining(data.session.id);
        }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
      }
    });
  },

  goTraining(sessionId) {
    wx.navigateTo({ url: `/pages/training/training?sessionId=${sessionId}` });
  },

  goRoleplay(sessionId, prompt) {
    const suffix = prompt ? `&prompt=${encodeURIComponent(prompt)}` : '';
    wx.navigateTo({ url: `/pages/roleplay/roleplay?sessionId=${sessionId}${suffix}` });
  }
});
