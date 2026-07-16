const request = require('../../static/api/request.js');

const LEVEL_CONFIG = [
  { min: 85, level: 'excellent', text: '优秀' },
  { min: 70, level: 'good', text: '良好' },
  { min: 60, level: 'pass', text: '合格' },
  { min: 0, level: 'improve', text: '待改进' }
];

const V5_DIMENSIONS = [
  { key: 'empathy', name: '同理心与温度', color: '#52c41a', weight: 20 },
  { key: 'needsDiscovery', name: '需求挖掘力', color: '#fa8c16', weight: 20 },
  { key: 'valueShaping', name: '价值塑造力', color: '#1677e8', weight: 20 },
  { key: 'conversion', name: '邀约转化', color: '#722ed1', weight: 20 },
  { key: 'compliance', name: '合规意识', color: '#eb2f96', weight: 20 }
];

function calcLevel(score) {
  return LEVEL_CONFIG.find(item => score >= item.min) || { level: 'improve', text: '待改进' };
}

Page({
  data: {
    sessionId: '',
    scenarioId: '',
    scenarioCategory: '',
    scenarioName: '',
    loading: true,
    status: 'generating',
    retryable: false,
    timedOut: false,
    evaluation: null,
    score: 0,
    passed: false,
    level: 'improve',
    levelText: '待改进',
    radarData: [],
    dimensions: [],
    messages: [],
    recommendedPhrases: [],
    nextScenarioId: '',
    nextScenarioName: '',
    errorMessage: ''
  },

  pollTimer: null,
  pollCount: 0,

  onLoad(options) {
    const sessionId = options.sessionId || options.conversationId || '';
    this.setData({ sessionId });
    if (!sessionId) {
      this.setData({ loading: false, errorMessage: '缺少训练会话 ID' });
      return;
    }

    // 从全局上下文读取训练元信息
    const ctx = getApp().globalData.currentSession;
    if (ctx && ctx.id === sessionId) {
      this.setData({
        scenarioId: ctx.scenarioId || '',
        scenarioCategory: ctx.scenarioCategory || '',
        scenarioName: ctx.scenarioName || ''
      });
    }

    this.loadSessionMeta();
    this.loadEvaluation();
  },

  sessionPath(suffix = '') {
    return `/sessions/${encodeURIComponent(this.data.sessionId)}${suffix}`;
  },

  onUnload() {
    if (this.pollTimer) clearTimeout(this.pollTimer);
    // 清除全局训练上下文
    getApp().clearCurrentSession();
  },

  async loadSessionMeta() {
    try {
      const data = await request.get(this.sessionPath());
      if (!data || !data.session) return;
      this.setData({
        scenarioId: data.session.scenarioId || '',
        scenarioCategory: data.session.scenarioCategory || '',
        scenarioName: data.session.scenarioName || '',
        messages: Array.isArray(data.messages) ? data.messages : []
      });
    } catch (error) {
      console.warn('加载训练元数据失败', error);
    }
  },

  async loadEvaluation() {
    try {
      const data = await request.get(this.sessionPath('/evaluation'));
      const status = data.status || 'generating';

      if (status === 'ready' && data.evaluation) {
        this.applyEvaluation(data.evaluation);
        return;
      }

      if (status === 'failed') {
        this.setData({
          loading: false,
          status,
          retryable: data.retryable !== false,
          evaluation: null
        });
        return;
      }

      this.pollCount += 1;
      if (this.pollCount >= 15) {
        this.setData({
          loading: false,
          status: 'generating',
          timedOut: true,
          retryable: true
        });
        return;
      }

      this.setData({ status: 'generating', loading: true });
      this.pollTimer = setTimeout(() => this.loadEvaluation(), 2000);
    } catch (error) {
      this.setData({
        loading: false,
        status: 'generating',
        timedOut: true,
        retryable: false,
        errorMessage: request.getErrorMessage(error, '评分报告加载失败')
      });
    }
  },

  applyEvaluation(evaluation) {
    evaluation = {
      ...evaluation,
      strengths: Array.isArray(evaluation.strengths) ? evaluation.strengths : [],
      improvements: Array.isArray(evaluation.improvements) ? evaluation.improvements : [],
      violations: Array.isArray(evaluation.violations) ? evaluation.violations : [],
      roundComments: Array.isArray(evaluation.roundComments) ? evaluation.roundComments : []
    };

    const scores = evaluation.dimensionScores || {};
    const totalScore = evaluation.totalScore || 0;

    // Map backend scores to V5 product dimensions
    // Backend may use different keys; map them to the standard V5 keys
    const dimensions = V5_DIMENSIONS.map(dim => {
      let value = scores[dim.key] || 0;
      // Fallback: try alternative keys from old system
      if (!value && dim.key === 'empathy') value = scores.empathy || 0;
      if (!value && dim.key === 'needsDiscovery') value = scores.needsDiscovery || 0;
      if (!value && dim.key === 'valueShaping') value = scores.valueShaping || scores.knowledgeAccuracy || 0;
      if (!value && dim.key === 'conversion') value = scores.conversion || 0;
      if (!value && dim.key === 'compliance') value = scores.compliance || scores.medicalCompliance || 0;
      return { name: dim.name, value, color: dim.color, weight: dim.weight };
    });

    const levelInfo = calcLevel(totalScore);

    // Recommended phrases
    const recommendedPhrases = Array.isArray(evaluation.recommendedPhrases)
      ? evaluation.recommendedPhrases
      : (evaluation.strengths && evaluation.strengths.length
        ? evaluation.strengths.slice(0, 3).map(s => ({ round: s.round, content: s.evidence || '', reason: s.content }))
        : []);

    this.setData({
      loading: false,
      status: 'ready',
      retryable: false,
      timedOut: false,
      evaluation,
      score: totalScore,
      passed: totalScore >= 60,
      level: levelInfo.level,
      levelText: levelInfo.text,
      dimensions,
      recommendedPhrases,
      radarData: dimensions.map(item => ({
        name: item.name,
        value: item.value,
        max: 100,
        color: item.color
      }))
    });

    // Try to find next scenario
    this.findNextScenario();
  },

  async findNextScenario() {
    try {
      const data = await request.get('/scenarios');
      const scenarios = data.items || [];
      const currentId = String(this.data.scenarioId);
      const cat = this.data.scenarioCategory;

      // Find current scenario's index within same category
      const sameCategory = cat ? scenarios.filter(s => (s.category || 'consultation') === cat) : scenarios;
      const currentIdx = sameCategory.findIndex(s => String(s.id) === currentId);

      if (currentIdx >= 0 && currentIdx < sameCategory.length - 1) {
        const next = sameCategory[currentIdx + 1];
        this.setData({
          nextScenarioId: next.id,
          nextScenarioName: next.name
        });
      }
    } catch (error) {
      console.warn('查找下一关失败', error);
    }
  },



  async retryEvaluation() {
    try {
      await request.post(this.sessionPath('/evaluation/retry'));
    } catch (error) {
      console.warn('重试评分API调用失败，将继续轮询', error);
    }
    this.setData({ pollCount: 0, loading: true, status: 'generating', retryable: false, timedOut: false, errorMessage: '' });
    this.loadEvaluation();
  },

  refreshEvaluation() {
    this.retryEvaluation();
  },

  retryTraining() {
    if (!this.data.scenarioId) {
      wx.switchTab({ url: '/pages/index/index' });
      return;
    }
    wx.navigateTo({
      url: `/pages/training/training?scenarioId=${encodeURIComponent(this.data.scenarioId)}`
    });
  },

  goNextLevel() {
    if (!this.data.nextScenarioId) return;
    wx.redirectTo({
      url: `/pages/training/training?scenarioId=${encodeURIComponent(this.data.nextScenarioId)}`
    });
  },

  shareResult() {
    wx.showShareMenu({
      withShareTicket: true,
      menus: ['shareAppMessage', 'shareTimeline']
    });
  },

  onShareAppMessage() {
    const score = this.data.score;
    const levelText = this.data.levelText;
    return {
      title: `我在口腔客服陪练中获得${score}分，评级【${levelText}】，一起来挑战吧！`,
      path: '/pages/index/index'
    };
  },

  viewHistory() {
    wx.switchTab({ url: '/pages/report/report' });
  },

  backToScenarios() {
    wx.switchTab({ url: '/pages/index/index' });
  }
});
