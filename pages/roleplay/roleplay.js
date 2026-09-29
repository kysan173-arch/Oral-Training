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
    expandedLearning: {},
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

  toggleLearning(event) {
    const id = event.currentTarget.dataset.id;
    if (!id || !this.data.messages.some(item => item.id === id && item.role === 'standard_customer')) return;
    const expandedLearning = Object.assign({}, this.data.expandedLearning);
    expandedLearning[id] = !expandedLearning[id];
    this.setData({ expandedLearning });
  },
  initialPrompt: '',
  pendingPollTimer: null,

  requestEpoch: 0,

  isCurrentRequest(epoch) { return this._visible !== false && this.requestEpoch === epoch; },

  suspendRequests() {
    this._visible = false;
    this.requestEpoch += 1;
    if (this.pendingPollTimer) clearTimeout(this.pendingPollTimer);
    this.pendingPollTimer = null;
  },

  onHide() { this.suspendRequests(); },
  onUnload() { this.suspendRequests(); },
  onShow() {
    const resume = this._visible === false;
    this._visible = true;
    if (resume && this.sessionId) {
      this.setData({ sending: false, finishing: false });
      this.loadSession();
    }
  },

  onLoad(options) {
    this._visible = true;
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
    const epoch = this.requestEpoch;
    return Promise.all([api.getRoleplaySession(this.sessionId), api.getRoleplayScenarios()]).then(([detail, scenarioData]) => {
      if (!this.isCurrentRequest(epoch)) return;
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
        if (!this.isCurrentRequest(epoch)) return;
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
    }).catch(error => {
      if (this.isCurrentRequest(epoch)) api.showCenterNotice({ title: error.message || '患者模拟加载失败' });
    });
  },

  onInputChange(e) { this.setData({ inputValue: e.detail.value }); },

  useSuggestion(e) {
    if (this.data.sending || this.data.finishing) return;
    this.setData({ inputValue: e.currentTarget.dataset.prompt || '' });
  },

  viewEvidence(e) {
    const epoch = this.requestEpoch;
    const traceId = e.currentTarget.dataset.trace;
    if (!traceId) return;
    api.getRoleplayEvidence(this.sessionId, traceId).then(result => {
      if (!this.isCurrentRequest(epoch)) return;
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
    }).catch(error => {
      if (this.isCurrentRequest(epoch)) api.showCenterNotice({ title: error.message || '依据读取失败' });
    });
  },

  sendMessage(e) {
    const epoch = this.requestEpoch;
    if (this.data.sending || this.data.finishing) return;
    const fromInput = e && e.detail && e.detail.value ? e.detail.value : this.data.inputValue;
    const content = (fromInput || '').trim();
    if (!content) {
      api.showCenterNotice({ title: '请先输入要咨询的问题' });
      return;
    }
    if (this.data.currentRound >= this.data.maxRounds) {
      api.showCenterNotice({ title: '已达最大轮数，生成复盘中' });
      return;
    }
    const clientMessageId = this.data.pendingClientMessageId || `roleplay-${Date.now()}-${Math.floor(Math.random() * 100000)}`;
    this.setData({ sending: true, inputValue: content, scrollToView: 'message-bottom', failedMessage: null });
    api.sendRoleplayMessage(this.sessionId, clientMessageId, content).then(data => {
      if (!this.isCurrentRequest(epoch)) return;
      this.setData({ pendingClientMessageId: '', inputValue: '', sending: false, failedMessage: null });
      if (data.session.shouldFinish) {
        this.setData({ finishing: true });
        wx.redirectTo({ url: `/pages/roleplay-result/roleplay-result?sessionId=${this.sessionId}` });
        return;
      }
      this.loadSession();
    }).catch(error => {
      if (!this.isCurrentRequest(epoch)) return;
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
    const epoch = this.requestEpoch;
    if (!this.isCurrentRequest(epoch)) return;
    if (this.pendingPollTimer) clearTimeout(this.pendingPollTimer);
    const wait = delay || POLL_BASE_DELAY;
    this.setData({ sending: true, pendingClientMessageId: clientMessageId, inputValue: content });
    api.getRoleplaySession(this.sessionId).then(detail => {
      if (!this.isCurrentRequest(epoch)) return;
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
        api.showCenterNotice({ title: '回复仍在生成，原问题已保留，可稍后回来查看' });
        return;
      }
      this.pendingPollTimer = setTimeout(
        () => this.pollPendingReply(clientMessageId, content, startedAt, nextPollDelay(wait)), wait);
    }).catch(() => {
      if (!this.isCurrentRequest(epoch)) return;
      if (Date.now() - startedAt >= 30000) {
        this.setData({ sending: false, pendingClientMessageId: clientMessageId, inputValue: content });
        api.showCenterNotice({ title: '网络异常，进度已保存，原问题已保留' });
        return;
      }
      this.pendingPollTimer = setTimeout(
        () => this.pollPendingReply(clientMessageId, content, startedAt, nextPollDelay(wait)), wait);
    });
  },

  /* 底部只留一个「结束训练」入口：把「生成复盘 / 暂存并退出」收敛到操作面板里 */
  exitRoleplay() {
    if (this.data.sending) {
      api.showCenterNotice({ title: '客服回复中，请稍候' });
      return;
    }
    if (this.data.pendingClientMessageId) {
      api.showCenterNotice({ title: '请先重试未回复的原问题' });
      return;
    }
    if (this.data.finishing) return;
    /* 一轮都没完成时，后端必然以 MIN_ROUNDS_NOT_REACHED 拒绝生成复盘，
       所以这里只保留「暂存并退出」——不给一个点了必然失败、还会卡住的入口。
       「已放弃」动作在学员端列表页提供，不放在对练仓底部。 */
    if (this.data.currentRound < 1) {
      wx.showActionSheet({
        itemList: ['暂存并退出'],
        success: () => this.leaveRoleplay(),
        fail: error => {
          if (error && /cancel/i.test(error.errMsg || '')) return;
          api.showCenterNotice({ title: '面板打开失败，请重试' });
        }
      });
      return;
    }
    wx.showActionSheet({
      itemList: ['结束并生成复盘', '暂存并退出'],
      success: result => {
        if (result.tapIndex === 0) this.finishRoleplay();
        else if (result.tapIndex === 1) this.leaveRoleplay();
      },
      fail: error => {
        // 用户点空白处/返回键取消是正常交互，不提示
        if (error && /cancel/i.test(error.errMsg || '')) return;
        api.showCenterNotice({ title: '面板打开失败，请重试' });
      }
    });
  },

  finishRoleplay() {
    const epoch = this.requestEpoch;
    if (this.data.currentRound < 1) {
      // exitRoleplay 已按轮数收窄过选项，正常路径到不了这里；留作兜底
      api.showCenterNotice({ title: '至少完成 1 轮提问' });
      return;
    }
    if (this.data.sending) {
      api.showCenterNotice({ title: '客服回复中，请稍候' });
      return;
    }
    if (this.data.pendingClientMessageId) {
      api.showCenterNotice({ title: '请先重试未回复的原问题' });
      return;
    }
    if (this.data.finishing) return;
    wx.showModal({
      title: '结束患者模拟？',
      content: '结束后将根据完整问答生成学习复盘，结束后不能继续提问。',
      confirmText: '生成复盘',
      success: result => { if (result.confirm && this.isCurrentRequest(epoch)) this.completeRoleplay(); },
      fail: () => { if (this.isCurrentRequest(epoch)) api.showCenterNotice({ title: '确认框打开失败，请重试' }); }
    });
  },

  completeRoleplay() {
    const epoch = this.requestEpoch;
    this.setData({ finishing: true });
    api.finishRoleplaySession(this.sessionId).then(() => {
      if (!this.isCurrentRequest(epoch)) return;
      wx.redirectTo({ url: `/pages/roleplay-result/roleplay-result?sessionId=${this.sessionId}` });
    }).catch(error => {
      if (!this.isCurrentRequest(epoch)) return;
      this.setData({ finishing: false });
      api.showCenterNotice({ title: error.message || '结束患者模拟失败' });
    });
  },

  leaveRoleplay() {
    if (!this.data.sending && !this.data.finishing) wx.navigateBack();
  }
});
