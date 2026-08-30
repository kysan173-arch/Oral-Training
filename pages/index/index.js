const api = require('../../utils/api.js');

const CATEGORY_CONFIG = [
  { id: 'consultation', name: '咨询解答', icon: '咨', description: '先了解患者关切，再清楚说明服务边界' },
  { id: 'price_negotiation', name: '价格异议', icon: '价', description: '客观说明费用构成，不承诺固定价格' },
  { id: 'complaint_handling', name: '投诉安抚', icon: '诉', description: '先回应情绪，及时引导联系医生或复诊' },
  { id: 'recommendation', name: '项目推荐', icon: '推', description: '从真实需求出发，不替代医生判断' }
];

const DIFFICULTY_MAP = {
  beginner: { level: 'beginner', label: '初级' },
  intermediate: { level: 'intermediate', label: '中级' },
  advanced: { level: 'advanced', label: '高级' },
  basic: { level: 'beginner', label: '初级' }
};

const inferCategory = item => {
  if (CATEGORY_CONFIG.some(category => category.id === item.category)) return item.category;
  if (/比价|报价|价格/.test(item.name)) return 'price_negotiation';
  if (/术后|不适|投诉/.test(item.name)) return 'complaint_handling';
  return 'consultation';
};

const formatDifficulty = item => {
  const mapped = DIFFICULTY_MAP[item.difficulty] || DIFFICULTY_MAP.basic;
  return { difficultyLevel: mapped.level, difficultyLabel: mapped.label };
};

const buildCategories = (scenarios, activeCategoryId, expandedCategories) => CATEGORY_CONFIG.map(category => {
  const items = scenarios.filter(item => item.category === category.id);
  return {
    id: category.id,
    icon: category.icon,
    name: category.name,
    description: category.description,
    items: items,
    totalCount: items.length,
    completedCount: items.filter(item => item.bestScore !== null && item.bestScore !== undefined).length,
    expanded: expandedCategories ? (expandedCategories[category.id] === true) : false
  };
});

Page({
  data: {
    scenarios: [],
    categories: [],
    activeCategoryId: '',
    expandedId: '',
    expandedCategories: {},
    trainingMode: 'customer_service',
    roleBlocked: false,
    currentRole: '',
    // 自由提问模式
    freeDescription: '',
    activeFreeSession: null,
    // 自定义画像（底部弹层，非阻塞）
    customProfiles: {},
    profileModalVisible: false,
    profileModalScenarioId: ''
  },

  onShow() {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 1 });
    }
    const user = api.getCurrentUser();
    if (user && user.role) {
      this.setData({ currentRole: user.role });
    }
    if (user && user.role === 'admin') {
      this.setData({ loading: false, roleBlocked: true });
      return;
    }
    this.setData({ roleBlocked: false });
    this.loadScenarios();
    // 检查是否有进行中的自由模拟会话
    this.checkActiveFreeSession();
  },

  checkActiveFreeSession() {
    api.getRoleplaySessions({ status: 'active', limit: 1 }).then(data => {
      const sessions = data.items || [];
      if (sessions.length > 0) {
        this.setData({ activeFreeSession: sessions[0] });
      } else {
        this.setData({ activeFreeSession: null });
      }
    }).catch(() => {
      this.setData({ activeFreeSession: null });
    });
  },

  loadScenarios() {
    this.scenarioRequestVersion = (this.scenarioRequestVersion || 0) + 1;
    const requestVersion = this.scenarioRequestVersion;
    const requestedMode = this.data.trainingMode;
    const isRoleplay = requestedMode === 'patient_simulation';
    const request = isRoleplay ? api.getRoleplayScenarios() : api.getScenarios();
    request.then(data => {
      if (requestVersion !== this.scenarioRequestVersion || requestedMode !== this.data.trainingMode) return;
      const difficultyOverride = isRoleplay ? { beginner: { level: 'beginner', label: '初级' } } : null;
      const scenarios = data.items.map(item => {
        const difficulty = difficultyOverride
          ? difficultyOverride[item.difficulty] || formatDifficulty(item)
          : formatDifficulty(item);
        return Object.assign({}, item, difficulty, {
          category: inferCategory(item),
          patientAge: `${item.patientProfile.age}岁`,
          patientConcern: item.patientProfile.description,
          patientEmotion: isRoleplay ? '由你自由提问' : '需通过对话了解',
          passScore: item.passScore || 60,
          bestScore: item.bestScore !== undefined ? item.bestScore : null,
          actionText: item.activeSession
            ? (isRoleplay ? '继续模拟' : '继续训练')
            : (isRoleplay ? '开始模拟' : (item.bestScore !== null && item.bestScore !== undefined ? '再练' : '开始训练')),
          suggestedQuestions: item.suggestedQuestions || []
        });
      });

      if (isRoleplay) {
        // 患者模拟模式：不构建分类，只保存场景数据（用于创建会话）
        this.setData({ scenarios, categories: [], expandedId: '' });
      } else {
        const expandedCategories = this.data.expandedCategories || {};
        const categories = buildCategories(scenarios, this.data.activeCategoryId, expandedCategories);
        this.setData({ scenarios, categories, expandedId: '' });
      }
    }).catch(error => {
      if (requestVersion !== this.scenarioRequestVersion || requestedMode !== this.data.trainingMode) return;
      wx.showToast({ title: error.message || '场景加载失败', icon: 'none' });
    });
  },

  toggleCategory(e) {
    const id = e.currentTarget.dataset.id;
    const expandedCategories = Object.assign({}, this.data.expandedCategories);
    expandedCategories[id] = !expandedCategories[id];
    const categories = buildCategories(this.data.scenarios, this.data.activeCategoryId, expandedCategories);
    this.setData({ expandedCategories, categories });
  },

  switchMode(e) {
    const mode = e.currentTarget.dataset.mode;
    if (!mode || mode === this.data.trainingMode) return;
    this.setData({
      trainingMode: mode,
      scenarios: [],
      categories: [],
      expandedId: '',
      expandedCategories: {},
      freeDescription: '',
      profileModalVisible: false,
      profileModalScenarioId: ''
    }, () => {
      this.loadScenarios();
      if (mode === 'patient_simulation') {
        this.checkActiveFreeSession();
      }
    });
  },

  toggleProfile(e) {
    const id = e.currentTarget.dataset.id;
    this.setData({ expandedId: this.data.expandedId === id ? '' : id });
  },

  // ═══════════════════════════════════════
  // 自由提问模式
  // ═══════════════════════════════════════

  onFreeDescriptionChange(e) {
    this.setData({ freeDescription: e.detail.value });
  },

  fillExample(e) {
    const example = e.currentTarget.dataset.example || '';
    this.setData({ freeDescription: example });
  },

  startFreeRoleplay() {
    const description = this.data.freeDescription.trim();
    if (!description) {
      wx.showToast({ title: '请先描述你想模拟的场景', icon: 'none' });
      return;
    }
    // 使用第一个 roleplay 场景创建会话（作为通用模板）
    const scenarios = this.data.scenarios;
    if (!scenarios.length) {
      wx.showToast({ title: '场景数据未加载，请稍后重试', icon: 'none' });
      return;
    }
    const scenarioId = scenarios[0].id;
    wx.showLoading({ title: '创建会话中…' });
    api.createRoleplaySession(scenarioId).then(data => {
      wx.hideLoading();
      this.goRoleplay(data.session.id, description);
    }).catch(error => {
      wx.hideLoading();
      wx.showToast({ title: error.message || '创建模拟会话失败', icon: 'none' });
    });
  },

  // ═══════════════════════════════════════
  // 自定义患者画像
  // ═══════════════════════════════════════

  onCustomProfileChange(e) {
    const { id, field } = e.currentTarget.dataset;
    const value = e.detail.value;
    const customProfiles = Object.assign({}, this.data.customProfiles);
    if (!customProfiles[id]) customProfiles[id] = {};
    customProfiles[id][field] = value;
    this.setData({ customProfiles });
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
    // 有已填画像 → 直接开始；否则弹层引导（可跳过用默认画像）
    if (this.hasCustomProfile(id)) {
      this.startWithCustomProfile(id);
    } else {
      this.setData({ profileModalVisible: true, profileModalScenarioId: id });
    }
  },

  closeProfileModal() {
    this.setData({ profileModalVisible: false });
  },

  // 用当前已填画像创建会话（描述必填校验放在这里）
  startWithCustomProfile(id) {
    const source = this.data.customProfiles[id] || {};
    const profileData = {};
    if (source.age && String(source.age).trim()) profileData.age = String(source.age).trim();
    if (source.description && String(source.description).trim()) profileData.description = String(source.description).trim();
    if (source.emotion && String(source.emotion).trim()) profileData.emotion = String(source.emotion).trim();

    if (!profileData.description) {
      wx.showToast({ title: '请填写患者描述（描述为必填）', icon: 'none' });
      return;
    }

    api.createSession(id, profileData).then(data => {
      const sessionId = data.session.id;
      wx.setStorageSync(`customProfile_${sessionId}`, JSON.stringify(profileData));
      this.goTraining(sessionId);
    }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  },

  // 弹层确认：以当前填写的画像开始（描述仍必填，但允许跳过到默认画像）
  confirmProfileAndStart() {
    const id = this.data.profileModalScenarioId;
    const source = this.data.customProfiles[id] || {};
    const hasDescription = !!(source.description && String(source.description).trim());
    if (!hasDescription) {
      wx.showToast({ title: '请填写患者描述（描述为必填）', icon: 'none' });
      return;
    }
    this.setData({ profileModalVisible: false });
    this.startWithCustomProfile(id);
  },

  // 跳过画像：使用场景默认患者画像直接开始
  skipProfileAndStart() {
    const id = this.data.profileModalScenarioId;
    this.setData({ profileModalVisible: false });
    api.createSession(id, {}).then(data => {
      this.goTraining(data.session.id);
    }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  },

  // 是否已填写必填画像（描述）
  hasCustomProfile(id) {
    const source = this.data.customProfiles[id] || {};
    return !!(source.description && String(source.description).trim());
  },

  // 生成画像摘要
  profileSummary(id) {
    const source = this.data.customProfiles[id] || {};
    const parts = [];
    if (source.age && String(source.age).trim()) parts.push(`${source.age}岁`);
    if (source.description && String(source.description).trim()) parts.push(source.description);
    if (source.emotion && String(source.emotion).trim()) parts.push(source.emotion);
    return parts.join('，');
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
  },

  continueFreeRoleplay(e) {
    const sessionId = e.currentTarget.dataset.sessionid;
    if (sessionId) this.goRoleplay(sessionId, '');
  },

  abandonFreeRoleplay(e) {
    const sessionId = e.currentTarget.dataset.sessionid;
    if (!sessionId) return;
    wx.showModal({
      title: '放弃进行中的模拟？',
      content: '放弃后当前问答将标记为已放弃，无法恢复，之后可重新开始新的患者模拟。',
      confirmText: '确认放弃',
      confirmColor: '#C65A4E',
      success: result => {
        if (!result.confirm) return;
        api.abandonRoleplaySession(sessionId).then(() => {
          wx.showToast({ title: '已放弃，可重新开始', icon: 'success' });
          this.setData({ activeFreeSession: null });
        }).catch(error => {
          wx.showToast({ title: error.message || '放弃失败', icon: 'none' });
          this.checkActiveFreeSession();
        });
      }
    });
  },

  switchRole() {
    const targetRole = this.data.currentRole === 'admin' ? 'learner' : 'admin';
    wx.showModal({
      title: '切换身份',
      content: `确定要切换为「${targetRole === 'admin' ? '主管' : '学员'}」身份吗？`,
      success: res => {
        if (!res.confirm) return;
        api.switchRole(targetRole).then(data => {
          wx.setStorageSync('oralTrainingAccessToken', data.accessToken);
          wx.setStorageSync('oralTrainingUser', data.user);
          wx.showToast({ title: '已切换，即将刷新', icon: 'success', duration: 1500 });
          setTimeout(() => {
            wx.switchTab({ url: targetRole === 'admin' ? '/pages/admin/admin' : '/pages/mine/mine' });
          }, 1600);
        }).catch(error => {
          wx.showToast({ title: error.message || '切换失败', icon: 'none' });
        });
      }
    });
  }
});
