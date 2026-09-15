const api = require('../../utils/api.js');
const plan = require('../../utils/plan.js');

const CATEGORY_CONFIG = require('../../utils/scenario.js').CATEGORY_CONFIG;

const DIFFICULTY_MAP = {
  beginner: { level: 'beginner', label: '初级' },
  intermediate: { level: 'intermediate', label: '中级' },
  advanced: { level: 'advanced', label: '高级' },
  basic: { level: 'beginner', label: '初级' }
};

/* 自由模拟专用的隐藏模板场景（is_template，不进场景列表）。场景描述随会话
   入库并由后端注入标准客服与复盘的 prompt，不再借用列表第一个场景，
   否则服务要点和复盘口径会跟着场景排序漂移。 */
const FREE_ROLEPLAY_TEMPLATE_ID = 'free-roleplay-template';

/* 患者画像预设。后端会把 description 直接拼在「您好，我最近」之后，并把 emotion
   拼进「心里挺X的」/「现在有点X」句式，所以预设文案必须满足：
   1) 能通顺接在「我最近」后面；2) 不以「我」开头（否则拼成「我最近我…」）；3) 末尾不带标点。
   情绪取值也刻意落在后端情绪词表内，避免开场白句式突兀。 */
const PROFILE_DESC_PRESETS = {
  'implant-basic': [
    '缺了一颗后牙，想问问种植牙大概要花多少钱',
    '缺牙很久了，一直纠结要不要种，想先了解下情况',
    '朋友做了种植牙效果不错，我也想问问自己适不适合'
  ],
  'orthodontic-basic': [
    '牙齿有点不整齐，想做隐形矫正，想知道大概要多久',
    '觉得牙齿不太整齐，想了解矫正的大概费用',
    '想矫正但怕拔牙，也怕别人看出来在戴牙套'
  ],
  'price-comparison': [
    '在别家也问过价，觉得你们这边报价偏高',
    '问了别家同样的牙，价格差挺多，想知道差在哪',
    '在比较几家诊所，主要想搞清楚材料和服务的区别'
  ],
  'post-treatment-discomfort': [
    '做完治疗后一直有点疼，还肿着',
    '拔完牙三天了，还在渗血，不知道正不正常',
    '治疗后一直不太舒服，担心是不是没处理好'
  ],
  'orthodontic-option': [
    '想做矫正，但纠结隐形牙套和传统托槽选哪个',
    '上班要见客户，担心戴牙套影响形象',
    '想做矫正，但预算有限，也怕影响平时吃饭'
  ]
};
const PROFILE_EMOTION_PRESETS = ['焦虑', '担心', '紧张', '犹豫', '害怕', '不满'];
const PROFILE_GENDER_OPTIONS = ['男', '女'];
const EMPTY_PROFILE_DRAFT = { age: '', gender: '', description: '', emotion: '' };
const DESC_MAX_LENGTH = 60;

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
    /* 序号在 JS 预算成两位字符串：场景数可能超过 9（主管可自由建场景），
       WXML 里拼 "0" + index 会显示成 "010"。 */
    items: items.map((item, index) => Object.assign({}, item, {
      indexLabel: String(index + 1).padStart(2, '0')
    })),
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
    /* 场景加载失败标记：只弹 toast 的话页面会空成「没有场景可练」，
       学员分不清「加载失败」和「真的没题」，也没有自救入口。 */
    scenariosFailed: false,
    currentRole: '',
    /* 横幅展示的是「最紧急的那个待办计划」摘要，无待办时为 null */
    planNotice: null,
    // 自由提问模式
    freeDescription: '',
    activeFreeSession: null,
    // 自定义画像（底部弹层，非阻塞）
    customProfiles: {},
    profileModalVisible: false,
    profileModalScenarioId: '',
    /* 弹层当前编辑的草稿。弹层只操作它，确认时才写回 customProfiles，
       这样 WXML 里的绑定路径短、性别选中态也只需做值比较而不必调方法。 */
    profileDraft: Object.assign({}, EMPTY_PROFILE_DRAFT),
    profileDescPresets: [],
    profileEmotionPresets: PROFILE_EMOTION_PRESETS,
    profileGenderOptions: PROFILE_GENDER_OPTIONS,
    profileDraftHasInput: false,
    descMaxLength: DESC_MAX_LENGTH,
    // 服务维度（master 可选接入）：仅当后端返回非空列表才展示选择器
    services: [],
    selectedServiceId: '',
    selectedService: null,
    roleplayScenarios: []
  },

  onShow() {
    /* 每次显示都同步 tabBar 角色列表，避免切换身份后残留另一套导航 */
    const tabBar = typeof this.getTabBar === 'function' ? this.getTabBar() : null;
    if (tabBar) {
      tabBar.applyRoleList();
      tabBar.setData({ selected: 1 });
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
    // 培训计划横幅：只展示最早截止的待办计划
    this.loadPlanNotice();
  },

  loadPlanNotice() {
    api.getLearnerTrainingPlans().then(data => {
      this.setData({ planNotice: plan.pickPlanNotice(data.plans) });
    }).catch(() => {});
  },

  // 横幅主体：直达最紧急计划的明细
  openUrgentPlan() {
    const notice = this.data.planNotice;
    if (!notice || !notice.id) return;
    wx.navigateTo({ url: `/pages/training-plan-detail/training-plan-detail?id=${encodeURIComponent(notice.id)}` });
  },

  // 横幅右上角「全部 N 个」：进计划列表看全量
  openMyPlans() {
    wx.navigateTo({ url: '/pages/training-plans/training-plans' });
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
          /* 画像缺失时不能整批炸掉：一条脏数据会让整个场景列表变成「加载失败」 */
          patientAge: item.patientProfile && item.patientProfile.age ? `${item.patientProfile.age}岁` : '',
          patientConcern: (item.patientProfile && item.patientProfile.description) || '',
          patientEmotion: isRoleplay ? '由你自由提问' : '需通过对话了解',
          passScore: item.passScore || 60,
          bestScore: item.bestScore !== undefined ? item.bestScore : null,
          /* 分数展示统一走 api.formatScore，避免各页各写一遍格式化口径 */
          bestScoreText: api.formatScore(item.bestScore),
          hasBestScore: item.bestScore !== null && item.bestScore !== undefined,
          actionText: item.activeSession
            ? (isRoleplay ? '继续模拟' : '继续训练')
            : (isRoleplay ? '开始模拟' : (item.bestScore !== null && item.bestScore !== undefined ? '再练' : '开始训练')),
          suggestedQuestions: item.suggestedQuestions || [],
          /* WXML 里不能对数据路径调用 Page 方法（依赖追踪失效且不报错），
             所以「是否已填画像」在这里预算成布尔字段。 */
          hasProfile: this.hasProfileInput(item.id)
        });
      });

      if (isRoleplay) {
        // 患者模拟模式：不构建分类，只保存场景数据（用于创建会话）
        this.setData({ scenarios, categories: [], expandedId: '', scenariosFailed: false });
        // 服务维度可选接入：空列表时静默降级，界面与无服务时完全一致
        this.loadServices();
      } else {
        const expandedCategories = this.data.expandedCategories || {};
        const categories = buildCategories(scenarios, this.data.activeCategoryId, expandedCategories);
        this.setData({ scenarios, categories, expandedId: '', scenariosFailed: false });
      }
    }).catch(error => {
      if (requestVersion !== this.scenarioRequestVersion || requestedMode !== this.data.trainingMode) return;
      this.setData({ scenariosFailed: true, scenarios: [], categories: [] });
      wx.showToast({ title: error.message || '场景加载失败', icon: 'none' });
    });
  },

  /* 服务维度（master 独有）：仅患者模拟模式下拉取；RAG 关闭或后端返回空列表时
     不设置 services，WXML 不渲染选择器，界面与现在完全一致。 */
  loadServices() {
    api.getServices().then(serviceData => {
      const services = serviceData.items || [];
      if (!services.length) {
        this.setData({ services: [], selectedServiceId: '', selectedService: null, roleplayScenarios: [] });
        return;
      }
      const selectedService = services.find(item => item.id === this.data.selectedServiceId) || services[0];
      this.setData({
        services,
        selectedServiceId: selectedService.id,
        selectedService
      }, () => this.loadRoleplayScenarios());
    }).catch(() => {
      this.setData({ services: [], selectedServiceId: '', selectedService: null, roleplayScenarios: [] });
    });
  },

  loadRoleplayScenarios() {
    const serviceId = this.data.selectedServiceId;
    if (!serviceId) return;
    api.getRoleplayScenarios(serviceId).then(data => {
      const scenarios = (data.items || []).map(item => Object.assign({}, item, {
        category: inferCategory(item),
        patientAge: item.patientProfile && item.patientProfile.age ? `${item.patientProfile.age}岁` : '',
        patientConcern: (item.patientProfile && item.patientProfile.description) || '',
        patientEmotion: '由你自由提问',
        bestScore: item.bestScore !== undefined ? item.bestScore : null,
        bestScoreText: api.formatScore(item.bestScore),
        hasBestScore: item.bestScore !== null && item.bestScore !== undefined,
        actionText: item.activeSession ? '继续模拟' : '开始模拟',
        suggestedQuestions: item.suggestedQuestions || []
      }));
      this.setData({ roleplayScenarios: scenarios });
    }).catch(() => {
      this.setData({ roleplayScenarios: [] });
    });
  },

  onServiceChange(e) {
    const selectedService = this.data.services[Number(e.detail.value)] || null;
    this.setData({
      selectedServiceId: selectedService ? selectedService.id : '',
      selectedService,
      roleplayScenarios: []
    }, () => this.loadRoleplayScenarios());
  },

  /* 场景加载失败后的自救入口 */
  retryScenarios() {
    this.setData({ scenariosFailed: false });
    this.loadScenarios();
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
      profileModalScenarioId: '',
      profileDraft: Object.assign({}, EMPTY_PROFILE_DRAFT),
      profileDescPresets: [],
      profileDraftHasInput: false,
      services: [],
      selectedServiceId: '',
      selectedService: null,
      roleplayScenarios: []
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
    // 固定用隐藏模板场景建会话，描述随会话入库并注入模型 prompt，
    // 学习要点与复盘都会围绕学员描述的场景生成。
    wx.showLoading({ title: '创建会话中…' });
    api.createRoleplaySession(FREE_ROLEPLAY_TEMPLATE_ID, { freeDescription: description }).then(data => {
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
    const { field } = e.currentTarget.dataset;
    if (!field) return;
    const profileDraft = Object.assign({}, this.data.profileDraft, { [field]: e.detail.value });
    this.setData({ profileDraft, profileDraftHasInput: this.draftHasInput(profileDraft) });
  },

  /* 预设标签：覆盖式单选。性别允许再点一次取消，回到「未指定」，
     这样学员也能明确表达「不想限定性别」而不是被迫二选一。 */
  pickProfileOption(e) {
    const { field, value } = e.currentTarget.dataset;
    if (!field || !value) return;
    const nextValue = field === 'gender' && this.data.profileDraft.gender === value ? '' : value;
    const profileDraft = Object.assign({}, this.data.profileDraft, { [field]: nextValue });
    this.setData({ profileDraft, profileDraftHasInput: this.draftHasInput(profileDraft) });
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
    // 已填过画像 → 直接开始；否则弹层引导（可跳过用默认画像）
    if (this.hasProfileInput(id)) {
      this.startWithCustomProfile(id);
    } else {
      this.openProfileModal(id);
    }
  },

  openProfileModal(id) {
    const source = this.data.customProfiles[id] || {};
    const profileDraft = {
      age: source.age || '',
      gender: source.gender || '',
      description: source.description || '',
      emotion: source.emotion || ''
    };
    this.setData({
      profileModalVisible: true,
      profileModalScenarioId: id,
      profileDraft,
      profileDescPresets: PROFILE_DESC_PRESETS[id] || [],
      profileDraftHasInput: this.draftHasInput(profileDraft)
    });
  },

  closeProfileModal() {
    this.setData({ profileModalVisible: false });
  },

  // 仅供遮罩层 catchtouchmove 挂载：拖动遮罩时不要带着背景页面滚动
  preventPageScroll() {},

  // 画像是否填过任意一项（不再要求描述必填）
  draftHasInput(draft) {
    if (!draft) return false;
    return ['age', 'gender', 'description', 'emotion'].some(
      field => !!(draft[field] && String(draft[field]).trim()));
  },

  // 用当前已填画像创建会话
  startWithCustomProfile(id) {
    const source = this.data.customProfiles[id] || {};
    const profileData = {};
    if (source.age && String(source.age).trim()) profileData.age = String(source.age).trim();
    if (source.gender && String(source.gender).trim()) profileData.gender = String(source.gender).trim();
    if (source.description && String(source.description).trim()) profileData.description = String(source.description).trim();
    if (source.emotion && String(source.emotion).trim()) profileData.emotion = String(source.emotion).trim();

    api.createSession(id, profileData).then(data => {
      const sessionId = data.session.id;
      wx.setStorageSync(`customProfile_${sessionId}`, JSON.stringify(profileData));
      this.goTraining(sessionId);
    }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  },

  /* 弹层确认：草稿写回 customProfiles 后按内容分流。
     画像全空时按钮文案已是「使用默认画像开始」，直接走跳过路径，不再拦人。 */
  confirmProfileAndStart() {
    const id = this.data.profileModalScenarioId;
    const draft = this.data.profileDraft || {};
    // 年龄校验：填了就必须是 1-120 的整数，否则影响 AI 患者扮演质量
    if (draft.age) {
      const age = parseInt(draft.age, 10);
      if (isNaN(age) || age < 1 || age > 120) {
        wx.showToast({ title: '年龄需在 1-120 之间', icon: 'none' });
        return;
      }
    }
    const stored = {};
    ['age', 'gender', 'description', 'emotion'].forEach(field => {
      const value = draft[field] ? String(draft[field]).trim() : '';
      if (value) stored[field] = value;
    });
    const customProfiles = Object.assign({}, this.data.customProfiles, { [id]: stored });
    this.setData({ profileModalVisible: false, customProfiles });

    if (!this.draftHasInput(stored)) {
      this.skipProfileAndStart();
      return;
    }
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

  // 是否已填写画像（年龄/性别/描述/情绪任一非空）
  hasProfileInput(id) {
    return this.draftHasInput(this.data.customProfiles[id]);
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
    // 选中服务时透传 serviceId（RAG 场景）；未选服务（自由模拟无服务）则走空对象
    const serviceId = this.data.selectedServiceId;
    const clientSessionId = `roleplay-session-${Date.now()}-${Math.floor(Math.random() * 100000)}`;
    const payload = serviceId ? { serviceId, clientSessionId } : {};
    api.createRoleplaySession(scenario.id, payload)
      .then(data => this.goRoleplay(data.session.id, prompt))
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
          ? api.restartRoleplaySession(scenario.activeSession.id, {})
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
            /* 切回学员时落点可能是「我的」等已存在的 tab 页，onShow 未必重跑，
               先本地把底部导航刷成新身份那套 */
            if (typeof this.getTabBar === 'function' && this.getTabBar()) {
              this.getTabBar().applyRoleList();
            }
            wx.switchTab({ url: targetRole === 'admin' ? '/pages/admin/admin' : '/pages/mine/mine' });
          }, 1600);
        }).catch(error => {
          wx.showToast({ title: error.message || '切换失败', icon: 'none' });
        });
      }
    });
  }
});
