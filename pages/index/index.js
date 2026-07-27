const api = require('../../utils/api.js');

const CATEGORY_CONFIG = {
  consultation: { name: '咨询解答', icon: '💬' },
  price_negotiation: { name: '价格异议', icon: '💰' },
  complaint_handling: { name: '投诉安抚', icon: '🎯' },
  recommendation: { name: '项目推荐', icon: '🏥' }
};

const DIFFICULTY_MAP = {
  beginner: '初级',
  intermediate: '中级',
  advanced: '进阶'
};

const MOCK_SCENARIOS = [
  {
    id: 'mock-1',
    name: '种植牙价格对比',
    category: 'consultation',
    difficulty: 'beginner',
    passScore: 60,
    description: '患者对比多家诊所的种植牙价格，需要你专业解答价格差异',
    patientProfile: { age: '45', description: '比价型患者', personality: '理性', sensitivity: '高', initialEmotion: '疑虑' },
    focus: ['价格沟通', '品牌差异', '价值传递'],
    completedCount: 0, totalRounds: 3
  },
  {
    id: 'mock-2',
    name: '正畸方案咨询',
    category: 'consultation',
    difficulty: 'beginner',
    passScore: 60,
    description: '30岁患者咨询成人正畸是否来得及，担心疼痛和周期',
    patientProfile: { age: '30', description: '焦虑型患者', personality: '敏感多疑', sensitivity: '高', initialEmotion: '焦虑' },
    focus: ['年龄顾虑消除', '方案介绍', '疼痛预期管理'],
    completedCount: 0, totalRounds: 3
  },
  {
    id: 'mock-3',
    name: '价格异议处理',
    category: 'price_negotiation',
    difficulty: 'intermediate',
    passScore: 65,
    description: '患者质疑诊所价格远高于竞品，需要妥善处理',
    patientProfile: { age: '38', description: '比价型患者', personality: '强势', sensitivity: '中', initialEmotion: '不满' },
    focus: ['价值分解', '不贬低竞品', '提问挖掘需求'],
    completedCount: 0, totalRounds: 3
  },
  {
    id: 'mock-4',
    name: '术后投诉安抚',
    category: 'complaint_handling',
    difficulty: 'intermediate',
    passScore: 65,
    description: '患者拔牙后疼痛不止，情绪激动投诉',
    patientProfile: { age: '28', description: '术后疼痛患者', personality: '情绪化', sensitivity: '极高', initialEmotion: '愤怒' },
    focus: ['情绪安抚', '专业解释', '后续方案'],
    completedCount: 0, totalRounds: 3
  },
  {
    id: 'mock-5',
    name: '服务态度投诉',
    category: 'complaint_handling',
    difficulty: 'intermediate',
    passScore: 65,
    description: '患者投诉前台服务态度差，等了很久没人接待',
    patientProfile: { age: '35', description: '不满患者', personality: '急躁', sensitivity: '高', initialEmotion: '愤怒' },
    focus: ['道歉安抚', '承诺改进', '立即行动'],
    completedCount: 0, totalRounds: 3
  },
  {
    id: 'mock-6',
    name: '洁牙升单推荐',
    category: 'recommendation',
    difficulty: 'advanced',
    passScore: 70,
    description: '患者只想洗牙，如何自然推荐其他必要治疗',
    patientProfile: { age: '42', description: '谨慎型患者', personality: '多疑', sensitivity: '高', initialEmotion: '防备' },
    focus: ['尊重选择', '客观告知', '不强制消费'],
    completedCount: 0, totalRounds: 3
  }
];

function getCategoryName(cat) {
  return (CATEGORY_CONFIG[cat] || {}).name || '咨询解答';
}
function getCategoryIcon(cat) {
  return (CATEGORY_CONFIG[cat] || {}).icon || '💬';
}

function inferCategory(scenario) {
  if (scenario.category && CATEGORY_CONFIG[scenario.category]) {
    return scenario.category;
  }
  const name = (scenario.name || '').toLowerCase();
  if (name.includes('种植') || name.includes('正畸') || name.includes('咨询')) return 'consultation';
  if (name.includes('比价') || name.includes('价格')) return 'price_negotiation';
  if (name.includes('投诉') || name.includes('安抚') || name.includes('术后')) return 'complaint_handling';
  if (name.includes('推荐') || name.includes('升单') || name.includes('美白')) return 'recommendation';
  return 'consultation';
}

function buildCategories(scenarios) {
  const map = {};
  scenarios.forEach(s => {
    const cat = inferCategory(s);
    if (!map[cat]) map[cat] = [];
    map[cat].push(s);
  });
  const order = ['consultation', 'price_negotiation', 'complaint_handling', 'recommendation'];
  return order
    .filter(key => map[key] && map[key].length)
    .map((key, index) => ({
      id: key,
      name: getCategoryName(key),
      icon: getCategoryIcon(key),
      expanded: index === 0,
      scenarios: map[key].map(s => ({
        ...s,
        category: s.category || key,
        difficultyText: DIFFICULTY_MAP[s.difficulty] || '初级',
        passScore: s.passScore || 60,
        completedCount: s.completedCount || 0,
        totalRounds: s.totalRounds || 1,
        patientProfile: {
          age: (s.patientProfile && s.patientProfile.age) || '--',
          description: (s.patientProfile && s.patientProfile.description) || '',
          personality: (s.patientProfile && s.patientProfile.personality) || '',
          sensitivity: (s.patientProfile && s.patientProfile.sensitivity) || '',
          initialEmotion: (s.patientProfile && s.patientProfile.initialEmotion) || ''
        }
      }))
    }));
}

Page({
  data: {
    categories: [],
    expandedId: '',
    trainingMode: 'customer_service'
  },

  onShow() { this.loadScenarios(); },

  loadScenarios() {
    const isRoleplay = this.data.trainingMode === 'patient_simulation';
    const request = isRoleplay ? api.getRoleplayScenarios() : api.getScenarios();
    request.then(data => {
      const enrichedScenarios = data.items.map(item => Object.assign({}, item, {
        patientAge: `${item.patientProfile.age}岁`,
        patientConcern: item.patientProfile.description,
        patientEmotion: isRoleplay ? '由你自由提问' : '需通过对话了解',
        actionText: item.activeSession
          ? (isRoleplay ? '继续模拟' : '继续训练')
          : (isRoleplay ? '开始模拟' : '开始训练'),
        activeSessionRounds: item.activeSession ? `${item.activeSession.currentRound}/10轮` : '',
        suggestedQuestions: item.suggestedQuestions || []
      }));
      const categories = buildCategories(enrichedScenarios);
      this.setData({ categories, expandedId: '' });
    }).catch(error => wx.showToast({ title: error.message || '场景加载失败', icon: 'none' }));
  },

  switchMode(e) {
    const mode = e.currentTarget.dataset.mode;
    if (!mode || mode === this.data.trainingMode) return;
    this.setData({ trainingMode: mode, categories: [], expandedId: '' }, () => this.loadScenarios());
  },

  toggleCategory(e) {
    const catId = e.currentTarget.dataset.catId;
    const categories = this.data.categories.map(cat => {
      cat.expanded = cat.id === catId ? !cat.expanded : cat.expanded;
      return cat;
    });
    this.setData({ categories });
  },

  toggleProfile(e) {
    const id = e.currentTarget.dataset.id;
    this.setData({ expandedId: this.data.expandedId === id ? '' : id });
  },

  findScenario(id) {
    for (const cat of this.data.categories) {
      const found = cat.scenarios.find(s => s.id === id);
      if (found) return found;
    }
    return null;
  },

  openTraining(e) {
    const { id, mode } = e.currentTarget.dataset;
    const scenario = this.findScenario(id);
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
    const scenario = this.findScenario(e.currentTarget.dataset.id);
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
        const scenario = this.findScenario(id);
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
