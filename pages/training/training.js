const api = require('../../utils/api.js');

const normalizeScenario = item => Object.assign({}, item, {
  patientAge: `${item.patientProfile.age}岁`,
  patientConcern: item.patientProfile.description,
  patientEmotion: '需通过对话了解'
});

const normalizeMessages = messages => messages.map(message => {
  const emotion = message.emotion || '';
  let emotionIcon = '';
  let emotionClass = '';
  if (emotion.includes('焦虑') || emotion.includes('紧张') || emotion.includes('担心')) {
    emotionIcon = '😟'; emotionClass = 'emotion-anxious';
  } else if (emotion.includes('愤怒') || emotion.includes('不满') || emotion.includes('投诉')) {
    emotionIcon = '😤'; emotionClass = 'emotion-angry';
  } else if (emotion.includes('怀疑') || emotion.includes('犹豫') || emotion.includes('迟疑')) {
    emotionIcon = '🤔'; emotionClass = 'emotion-doubt';
  } else if (emotion.includes('伤心') || emotion.includes('失望') || emotion.includes('沮丧')) {
    emotionIcon = '😔'; emotionClass = 'emotion-sad';
  } else if (emotion.includes('满意') || emotion.includes('信任') || emotion.includes('放心')) {
    emotionIcon = '😊'; emotionClass = 'emotion-calm';
  } else if (emotion) {
    emotionIcon = '💬'; emotionClass = 'emotion-default';
  }
  return Object.assign({}, message, {
    time: message.createdAt || '',
    emotionIcon,
    emotionClass
  });
});

Page({
  data: {
    session: null,
    scenario: null,
    messages: [],
    inputValue: '',
    currentRound: 0,
    maxRounds: 10,
    remainingRounds: 10,
    scrollToView: '',
    sending: false,
    finishing: false,
    pendingClientMessageId: '',
    hintRemaining: 3,
    hintLoading: false,
    showQuickPhrases: false,
    quickPhrases: [
      { id: 1, label: '确认信息', text: '好的，我帮您确认一下具体情况。' },
      { id: 2, label: '表达理解', text: '理解您的顾虑，很多患者都会有类似的担心。' },
      { id: 3, label: '专业解释', text: '从专业角度来说，这是因为……导致的正常现象。' },
      { id: 4, label: '安抚情绪', text: '非常抱歉让您感到不适，我们会尽快为您处理。' },
      { id: 5, label: '引导检查', text: '我建议您先来做个检查，医生会根据您的具体情况给出专业方案。' },
      { id: 6, label: '价格说明', text: '费用主要取决于方案复杂度，等检查后会有详细的费用清单供您参考。' },
      { id: 7, label: '预约引导', text: '方便的话我帮您预约一下最近的时间，您看什么时候方便？' },
      { id: 8, label: '消除顾虑', text: '您可以放心，我们所有的治疗项目都会提前和您确认，不会强制消费。' }
    ]
  },

  sessionId: '',

  onLoad(options) {
    this.sessionId = options.sessionId || '';
    this.loadSession();
  },

  loadSession() {
    if (!this.sessionId) return;
    Promise.all([api.getSession(this.sessionId), api.getScenarios()]).then(([detail, scenarioData]) => {
      const scenario = scenarioData.items.find(item => item.id === detail.session.scenarioId);
      if (!scenario) throw new Error('训练场景不存在');
      const pendingMessage = detail.pendingMessage || null;
      this.setData({
        session: detail.session,
        scenario: normalizeScenario(scenario),
        messages: normalizeMessages(detail.messages),
        pendingClientMessageId: pendingMessage ? pendingMessage.clientMessageId : '',
        inputValue: pendingMessage ? pendingMessage.content : this.data.inputValue,
        currentRound: detail.session.currentRound,
        maxRounds: detail.session.maxRounds,
        finishing: detail.session.status === 'completed'
      }, () => this.scrollToBottom());
    }).catch(error => {
      wx.showModal({ title: '会话加载失败', content: error.message || '请从场景列表重新开始训练。', showCancel: false, success: () => wx.navigateBack() });
    });
  },

  onInputChange(e) { this.setData({ inputValue: e.detail.value }); },

  dismissGoldenPhrase() {
    this.setData({ goldenPhrase: '' });
  },

  async sendMessage() {
    if (this.data.sending || this.data.finishing || this.data.isTrainingEnd) return;

    const content = this.data.inputValue.trim();
    if (!content || this.data.sending || this.data.finishing || this.data.currentRound >= this.data.maxRounds) return;
    const clientMessageId = this.data.pendingClientMessageId || `client-msg-${Date.now()}`;
    this.setData({ sending: true });
    api.sendMessage(this.sessionId, clientMessageId, content).then(data => {
      this.setData({ pendingClientMessageId: '', inputValue: '', sending: false });
      if (data.session.shouldFinish) {
        this.setData({ finishing: true });
        wx.redirectTo({ url: `/pages/result/result?sessionId=${this.sessionId}` });
        return;
      }
      this.loadSession();
    }).catch(error => {
      this.setData({ sending: false, pendingClientMessageId: clientMessageId, inputValue: content });
      this.loadSession();
      wx.showToast({ title: error.message || '患者回复生成失败，可再次发送重试', icon: 'none' });
    });
  },

  finishTraining() {
    if (this.data.currentRound < 1) {
      wx.showToast({ title: '至少完成1轮对话后才能评分', icon: 'none' });
      return;
    }
    if (this.data.sending) {
      wx.showToast({ title: '患者正在回复，请稍候', icon: 'none' });
      return;
    }
    if (this.data.finishing) {
      wx.showToast({ title: '正在生成报告，请稍候', icon: 'none' });
      return;
    }
    wx.showModal({
      title: '结束本次训练？',
      content: '结束后将根据完整对话生成训练报告，结束后不能继续发送消息。',
      confirmText: '结束评分',
      success: result => { if (result.confirm) this.completeTraining(); },
      fail: () => wx.showToast({ title: '确认框打开失败，请重试', icon: 'none' })
    });
  },

  completeTraining() {
    this.setData({ finishing: true });
    api.finishSession(this.sessionId).then(() => {
      wx.redirectTo({ url: `/pages/result/result?sessionId=${this.sessionId}` });
    }).catch(error => {
      this.setData({ finishing: false });
      wx.showToast({ title: error.message || '结束训练失败', icon: 'none' });
    });
  },

  leaveTraining() {
    if (!this.data.sending && !this.data.finishing) wx.navigateBack();
  },

  requestHint() {
    if (this.data.hintLoading || this.data.hintRemaining <= 0) return;
    this.setData({ hintLoading: true });
    api.getHint(this.sessionId).then(data => {
      this.setData({ hintRemaining: data.remaining, hintLoading: false });
      this.loadSession();
    }).catch(error => {
      this.setData({ hintLoading: false });
      wx.showToast({ title: error.message || '提示获取失败', icon: 'none' });
    });
  },

  toggleQuickPhrases() {
    this.setData({ showQuickPhrases: !this.data.showQuickPhrases });
  },

  selectQuickPhrase(e) {
    const content = e.currentTarget.dataset.content;
    this.setData({ inputValue: content, showQuickPhrases: false });
  },

  scrollToBottom() { this.setData({ scrollToView: 'message-bottom' }); }
});
