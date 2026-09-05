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
  pendingPollTimer: null,

  onUnload() { if (this.pendingPollTimer) clearTimeout(this.pendingPollTimer); },

  onLoad(options) {
    this.sessionId = options.sessionId || '';
    if (!this.sessionId) {
      this.handleMissingSession();
      return;
    }
    this.initialPrompt = options.prompt ? decodeURIComponent(options.prompt) : '';
    this.loadSession();
  },

  handleMissingSession() {
    wx.showModal({
      title: '无法打开患者模拟',
      content: '页面链接缺少会话信息，请从场景列表重新进入。',
      showCancel: false,
      success: () => wx.switchTab({ url: '/pages/index/index' })
    });
  },

  loadSession() {
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
        scrollToView: messages.length ? 'message-bottom' : '',
        pendingClientMessageId: detail.pendingMessage ? detail.pendingMessage.clientMessageId : ''
      };
      if (detail.pendingMessage) {
        nextData.pendingClientMessageId = detail.pendingMessage.clientMessageId;
        nextData.inputValue = detail.pendingMessage.content;
      } else if (this.initialPrompt && messages.length === 0 && !this.data.inputValue) {
        nextData.inputValue = this.initialPrompt;
        this.initialPrompt = '';
      }
      this.setData(nextData, () => {
        if (detail.pendingMessage && detail.pendingMessage.replyStatus === 'generating') {
          this.pollPendingReply(detail.pendingMessage.clientMessageId,
            detail.pendingMessage.content, Date.now());
        }
      });
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
      this.setData({ pendingClientMessageId: clientMessageId, inputValue: content });
      if (error.code === 'ROLEPLAY_RESPONSE_PENDING') {
        this.pollPendingReply(clientMessageId, content, Date.now());
        return;
      }
      this.setData({ sending: false });
      this.loadSession();
      wx.showToast({ title: error.message || '标准客服回复生成失败，可再次发送重试', icon: 'none' });
    });
  },

  pollPendingReply(clientMessageId, content, startedAt) {
    if (this.pendingPollTimer) clearTimeout(this.pendingPollTimer);
    this.setData({ sending: true, pendingClientMessageId: clientMessageId, inputValue: content });
    api.getRoleplaySession(this.sessionId).then(detail => {
      const pending = detail.pendingMessage || null;
      const messages = (detail.messages || []).map(item => Object.assign({}, item, {
        time: timeOf(item.createdAt),
        learningPoints: item.learningPoints || []
      }));
      this.setData({
        session: detail.session,
        messages,
        currentRound: detail.session.currentRound,
        maxRounds: detail.session.maxRounds,
        scrollToView: 'message-bottom'
      });
      if (detail.session.status === 'completed') {
        this.setData({ sending: false, finishing: true, pendingClientMessageId: '', inputValue: '' });
        wx.redirectTo({ url: `/pages/roleplay-result/roleplay-result?sessionId=${this.sessionId}` });
        return;
      }
      if (!pending) {
        this.setData({ sending: false, pendingClientMessageId: '', inputValue: '' });
        return;
      }
      if (pending.replyStatus === 'failed') {
        this.setData({ sending: false, pendingClientMessageId: clientMessageId, inputValue: content });
        wx.showToast({ title: '回复生成失败，可使用原问题安全重试', icon: 'none' });
        return;
      }
      if (Date.now() - startedAt >= 30000) {
        this.setData({ sending: false, pendingClientMessageId: clientMessageId, inputValue: content });
        wx.showToast({ title: '回复仍在生成，原问题已保留', icon: 'none' });
        return;
      }
      this.pendingPollTimer = setTimeout(
        () => this.pollPendingReply(clientMessageId, content, startedAt), 1000);
    }).catch(() => {
      if (Date.now() - startedAt >= 30000) {
        this.setData({ sending: false, pendingClientMessageId: clientMessageId, inputValue: content });
        wx.showToast({ title: '网络异常，原问题已保留', icon: 'none' });
        return;
      }
      this.pendingPollTimer = setTimeout(
        () => this.pollPendingReply(clientMessageId, content, startedAt), 1000);
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
    if (this.data.pendingClientMessageId) {
      wx.showToast({ title: '请先重试尚未生成回复的原问题', icon: 'none' });
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
