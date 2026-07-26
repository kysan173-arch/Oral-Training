const api = require('../../utils/api.js');

const normalizeScenario = item => Object.assign({}, item, {
  patientAge: `${item.patientProfile.age}岁`,
  patientConcern: item.patientProfile.description,
  patientEmotion: '需通过对话了解'
});

const normalizeMessages = messages => messages.map(message => Object.assign({}, message, {
  time: message.createdAt || ''
}));

Page({
  data: {
    session: null,
    scenario: null,
    messages: [],
    currentRound: 0,
    maxRounds: 10,
    remainingRounds: 10,
    inputValue: '',
    currentRound: 0,
    maxRounds: 10,
    scrollToView: '',
    sending: false,
    finishing: false,
    pendingClientMessageId: ''
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

  scrollToBottom() { this.setData({ scrollToView: 'message-bottom' }); }
});
