const request = require('../../static/api/request.js');
const util = require('../../utils/util.js');

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
    loading: true,
    errorMessage: ''
  },

  _flatScenarios: [],

  onLoad() {
    this.loadScenarios();
  },

  onShow() {
    if (this._flatScenarios.length > 0) {
      this.loadScenarios();
    }
  },

  async loadScenarios() {
    this.setData({ loading: true, errorMessage: '' });
    try {
      const data = await request.get('/scenarios');
      const flat = data.items || [];
      this._flatScenarios = flat;
      const categories = buildCategories(flat);
      this.setData({ categories, loading: false });
    } catch (error) {
      console.warn('加载场景失败，使用 Mock 数据', error);
      const flat = MOCK_SCENARIOS;
      this._flatScenarios = flat;
      const categories = buildCategories(flat);
      this.setData({ categories, loading: false });
    }
  },

  toggleCategory(e) {
    const index = e.currentTarget.dataset.index;
    const categories = this.data.categories.map((cat, i) => ({
      ...cat,
      expanded: i === index ? !cat.expanded : cat.expanded
    }));
    this.setData({ categories });
  },

  findScenario(id) {
    return this._flatScenarios.find(item => String(item.id) === String(id));
  },

  goToTraining(sessionId, scenarioId) {
    if (!sessionId) return;
    let url = `/pages/training/training?sessionId=${encodeURIComponent(sessionId)}`;
    if (scenarioId) url += `&scenarioId=${encodeURIComponent(scenarioId)}`;
    wx.navigateTo({ url });
  },

  async startTraining(e) {
    const scenarioId = e.currentTarget.dataset.id;
    const scenario = this.findScenario(scenarioId);
    if (!scenario) return;

    if (scenario.activeSession) {
      this.goToTraining(scenario.activeSession.id, scenarioId);
      return;
    }

    util.showLoading('创建训练中...');
    try {
      const data = await request.post('/sessions', { scenarioId });
      util.hideLoading();
      if (!data || !data.session || !data.session.id) {
        throw new Error('服务端未返回有效训练会话');
      }
      const app = getApp();
      app.setCurrentSession({
        id: data.session.id,
        scenarioId,
        scenarioName: scenario.name,
        scenarioCategory: scenario.category
      });
      this.goToTraining(data.session.id, scenarioId);
    } catch (error) {
      util.hideLoading();
      const existingSessionId = error.data && (error.data.sessionId || error.data.id);
      if (error.code === 'SESSION_IN_PROGRESS' && existingSessionId) {
        this.goToTraining(existingSessionId, scenarioId);
        return;
      }
      // Mock 降级：生成模拟会话ID，传递完整场景数据
      const mockSessionId = `mock-session-${scenarioId}-${Date.now()}`;
      const app = getApp();
      app.setCurrentSession({
        id: mockSessionId,
        scenarioId,
        scenarioName: scenario.name,
        scenarioCategory: scenario.category,
        scenarioData: scenario,
        isMock: true
      });
      this.goToTraining(mockSessionId, scenarioId);
    }
  },

  restartTraining(e) {
    const sessionId = e.currentTarget.dataset.sessionId;
    const scenarioId = e.currentTarget.dataset.scenarioId || '';
    if (!sessionId) return;

    util.showModal({
      title: '重新开始训练',
      content: '当前进行中的会话将标记为已放弃，确定重新开始吗？'
    }).then(async (confirmed) => {
      if (!confirmed) return;
      util.showLoading('重新创建中...');
      try {
        const data = await request.post(`/sessions/${encodeURIComponent(sessionId)}/restart`);
        util.hideLoading();
        if (!data || !data.session || !data.session.id) {
          throw new Error('服务端未返回有效训练会话');
        }
        const app = getApp();
        app.setCurrentSession({ id: data.session.id });
        this.goToTraining(data.session.id, scenarioId);
      } catch (error) {
        util.hideLoading();
        // Mock 降级
        const mockSessionId = `mock-session-${scenarioId}-${Date.now()}`;
        const scenario = this.findScenario(scenarioId);
        const app = getApp();
        app.setCurrentSession({
          id: mockSessionId,
          scenarioId,
          scenarioName: scenario ? scenario.name : '',
          scenarioCategory: scenario ? scenario.category : '',
          scenarioData: scenario,
          isMock: true
        });
        this.goToTraining(mockSessionId, scenarioId);
      }
    });
  },

  onPullDownRefresh() {
    this.loadScenarios().finally(() => wx.stopPullDownRefresh());
  }
});
