const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');

/* 轮询指数退避：1s → 2s → 4s → 封顶 8s，30 秒总窗不变（与 training 页一致）。 */
const POLL_BASE_DELAY = 1000;
const POLL_MAX_DELAY = 8000;

const nextPollDelay = delay => Math.min(delay * 2, POLL_MAX_DELAY);

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
    failedMessage: null,
    scrollToView: '',
    isFreeMode: false,
    freeDescription: ''
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
    const isFreeMode = !!this.initialPrompt;
    this.setData({ isFreeMode, freeDescription: this.initialPrompt });
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
        time: datetime.formatClock(item.createdAt),
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
            detail.pendingMessage.content, Date.now(), POLL_BASE_DELAY);
        }
        // 回到页面时上一轮已失败：恢复失败气泡
        if (detail.pendingMessage && detail.pendingMessage.replyStatus === 'failed') {
          this.setData({
            failedMessage: {
              clientMessageId: detail.pendingMessage.clientMessageId,
              content: detail.pendingMessage.content,
              reason: '回复生成失败，请重试'
            }
          });
        }
      });
    }).catch(error => wx.showToast({ title: error.message || '患者模拟加载失败', icon: 'none' }));
  },

  onInputChange(e) { this.setData({ inputValue: e.detail.value }); },

  useSuggestion(e) {
    if (this.data.sending || this.data.finishing) return;
    this.setData({ inputValue: e.currentTarget.dataset.prompt || '' });
  },

  viewEvidence(e) {
    const traceId = e.currentTarget.dataset.trace;
    if (!traceId) return;
    api.getRoleplayEvidence(this.sessionId, traceId).then(result => {
      const evidence = result.evidence || {};
      const facts = (evidence.facts || []).map(item => `• ${item.displayText}`);
      const passages = (evidence.passages || []).map(item =>
        `• ${item.title}：${item.body}`);
      const missing = (evidence.missingFields || []).length
        ? [`• 未提供字段：${evidence.missingFields.join('、')}`] : [];
      wx.showModal({
        title: '本轮回答依据',
        content: facts.concat(passages, missing).join('\n') || '本轮没有命中可引用资料。',
        showCancel: false
      });
    }).catch(error => wx.showToast({ title: error.message || '依据读取失败', icon: 'none' }));
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
    this.setData({ sending: true, inputValue: content, scrollToView: 'message-bottom', failedMessage: null });
    api.sendRoleplayMessage(this.sessionId, clientMessageId, content).then(data => {
      this.setData({ pendingClientMessageId: '', inputValue: '', sending: false, failedMessage: null });
      if (data.session.shouldFinish) {
        this.setData({ finishing: true });
        wx.redirectTo({ url: `/pages/roleplay-result/roleplay-result?sessionId=${this.sessionId}` });
        return;
      }
      this.loadSession();
    }).catch(error => {
      this.setData({ pendingClientMessageId: clientMessageId, inputValue: content });
      if (error.code === 'ROLEPLAY_RESPONSE_PENDING') {
        this.pollPendingReply(clientMessageId, content, Date.now(), POLL_BASE_DELAY);
        return;
      }
      // 失败渲染成气泡 + 重试按钮：重试复用同一 client_message_id（后端幂等）
      this.setData({
        sending: false,
        failedMessage: {
          clientMessageId,
          content,
          reason: error.message || '回复生成失败，请重试'
        }
      });
      this.loadSession();
    });
  },

  retryFailed() {
    const failed = this.data.failedMessage;
    if (!failed || this.data.sending || this.data.finishing) return;
    this.setData({
      failedMessage: null,
      pendingClientMessageId: failed.clientMessageId,
      inputValue: failed.content
    });
    this.sendMessage();
  },

  pollPendingReply(clientMessageId, content, startedAt, delay) {
    if (this.pendingPollTimer) clearTimeout(this.pendingPollTimer);
    const wait = delay || POLL_BASE_DELAY;
    this.setData({ sending: true, pendingClientMessageId: clientMessageId, inputValue: content });
    api.getRoleplaySession(this.sessionId).then(detail => {
      const pending = detail.pendingMessage || null;
      const messages = (detail.messages || []).map(item => Object.assign({}, item, {
        time: datetime.formatClock(item.createdAt),
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
        this.setData({ sending: false, pendingClientMessageId: '', inputValue: '', failedMessage: null });
        return;
      }
      if (pending.replyStatus === 'failed') {
        this.setData({
          sending: false,
          pendingClientMessageId: clientMessageId,
          inputValue: content,
          failedMessage: {
            clientMessageId,
            content,
            reason: '回复生成失败，请重试'
          }
        });
        return;
      }
      if (Date.now() - startedAt >= 30000) {
        this.setData({ sending: false, pendingClientMessageId: clientMessageId, inputValue: content });
        wx.showToast({ title: '回复仍在生成，原问题已保留，可稍后回来查看', icon: 'none' });
        return;
      }
      this.pendingPollTimer = setTimeout(
        () => this.pollPendingReply(clientMessageId, content, startedAt, nextPollDelay(wait)), wait);
    }).catch(() => {
      if (Date.now() - startedAt >= 30000) {
        this.setData({ sending: false, pendingClientMessageId: clientMessageId, inputValue: content });
        wx.showToast({ title: '网络异常，进度已保存，原问题已保留', icon: 'none' });
        return;
      }
      this.pendingPollTimer = setTimeout(
        () => this.pollPendingReply(clientMessageId, content, startedAt, nextPollDelay(wait)), wait);
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
