const api = require('../../utils/api.js');

const timeOf = value => value ? value.slice(11, 16) : '';

Page({
  data: {
    session: null,
    scenario: {},
    messages: [],
    suggestions: [],
    currentRound: 0,
    maxRounds: 10,
    inputValue: '',
    sending: false,
    finishing: false,
    pendingClientMessageId: '',
    scrollToView: ''
  },

  sessionId: '',
  initialPrompt: '',

  onLoad(options) {
    this.sessionId = options.sessionId || '';
    this.initialPrompt = options.prompt ? decodeURIComponent(options.prompt) : '';
    this.loadSession();
  },

  loadSession() {
    if (!this.sessionId) return;
    Promise.all([api.getRoleplaySession(this.sessionId), api.getRoleplayScenarios()]).then(([detail, scenarioData]) => {
      if (detail.session.status === 'completed') {
        wx.redirectTo({ url: `/pages/roleplay-result/roleplay-result?sessionId=${this.sessionId}` });
        return;
      }
      const scenarioInfo = scenarioData.items.find(item => item.id === detail.session.scenarioId) || {};
      const messages = (detail.messages || []).map(item => Object.assign({}, item, {
        time: timeOf(item.createdAt),
        learningPoints: item.learningPoints || []
      }));
      const nextData = {
        session: detail.session,
        scenario: Object.assign({}, scenarioInfo, {
          patientAge: scenarioInfo.patientProfile ? `${scenarioInfo.patientProfile.age}岁` : '',
          patientConcern: scenarioInfo.patientProfile ? scenarioInfo.patientProfile.description : ''
        }),
        messages,
        suggestions: scenarioInfo.suggestedQuestions || [],
        currentRound: detail.session.currentRound,
        maxRounds: detail.session.maxRounds,
        scrollToView: messages.length ? 'message-bottom' : ''
      };
      if (detail.pendingMessage) {
        nextData.pendingClientMessageId = detail.pendingMessage.clientMessageId;
        nextData.inputValue = detail.pendingMessage.content;
      } else if (this.initialPrompt && messages.length === 0 && !this.data.inputValue) {
        nextData.inputValue = this.initialPrompt;
        this.initialPrompt = '';
      }
      this.setData(nextData);
    }).catch(error => wx.showToast({ title: error.message || '患者模拟加载失败', icon: 'none' }));
  },

  onInputChange(e) { this.setData({ inputValue: e.detail.value }); },

  useSuggestion(e) {
    if (this.data.sending || this.data.finishing) return;
    this.setData({ inputValue: e.currentTarget.dataset.prompt || '' });
  },

  sendMessage(e) {
    if (this.data.sending || this.data.finishing) return;
    const fromInput = e && e.detail && e.detail.value ? e.detail.value : this.data.inputValue;
    const content = (fromInput || '').trim();
    if (!content) {
      wx.showToast({ title: '请先输入患者想咨询的问题', icon: 'none' });
      return;
    }
    if (this.data.currentRound >= this.data.maxRounds) {
      wx.showToast({ title: '已达到最大轮数，正在生成复盘', icon: 'none' });
      return;
    }
    const clientMessageId = this.data.pendingClientMessageId || `roleplay-${Date.now()}-${Math.floor(Math.random() * 100000)}`;
    this.setData({ sending: true, inputValue: content, scrollToView: 'message-bottom' });
    api.sendRoleplayMessage(this.sessionId, clientMessageId, content).then(data => {
      this.setData({ pendingClientMessageId: '', inputValue: '', sending: false });
      if (data.session.shouldFinish) {
        this.setData({ finishing: true });
        wx.redirectTo({ url: `/pages/roleplay-result/roleplay-result?sessionId=${this.sessionId}` });
        return;
      }
      this.loadSession();
    }).catch(error => {
      this.setData({ sending: false, pendingClientMessageId: clientMessageId, inputValue: content });
      this.loadSession();
      wx.showToast({ title: error.message || '标准客服回复生成失败，可再次发送重试', icon: 'none' });
    });
  },

  finishRoleplay() {
    if (this.data.currentRound < 1) {
      wx.showToast({ title: '至少完成1轮提问后才能生成复盘', icon: 'none' });
      return;
    }
    if (this.data.sending) {
      wx.showToast({ title: '标准客服正在回复，请稍候', icon: 'none' });
      return;
    }
    if (this.data.finishing) return;
    wx.showModal({
      title: '结束患者模拟？',
      content: '结束后将根据完整问答生成学习复盘，结束后不能继续提问。',
      confirmText: '生成复盘',
      success: result => { if (result.confirm) this.completeRoleplay(); },
      fail: () => wx.showToast({ title: '确认框打开失败，请重试', icon: 'none' })
    });
  },

  completeRoleplay() {
    this.setData({ finishing: true });
    api.finishRoleplaySession(this.sessionId).then(() => {
      wx.redirectTo({ url: `/pages/roleplay-result/roleplay-result?sessionId=${this.sessionId}` });
    }).catch(error => {
      this.setData({ finishing: false });
      wx.showToast({ title: error.message || '结束患者模拟失败', icon: 'none' });
    });
  },

  leaveRoleplay() {
    if (!this.data.sending && !this.data.finishing) wx.navigateBack();
  }
});
