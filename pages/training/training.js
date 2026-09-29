const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');
const emotion = require('../../utils/emotion.js');
const { normalizeScenario } = require('../../utils/patient-profile.js');

/* 情绪标签在 JS 里预算成 emotionText/emotionTone：WXML 禁止对数据路径调用函数，
   派生字段必须提前算好（函数调用表达式的依赖不会被追踪）。 */
const normalizeMessages = messages => messages.map(message => {
  const emotionText = emotion.emotionTextOf(message);
  return Object.assign({}, message, {
    time: datetime.formatClock(message.createdAt),
    emotionText,
    emotionTone: emotion.emotionToneOf(emotionText)
  });
});

const QUICK_PHRASES = [
  '我先帮您确认一下目前最关心的是哪一方面。',
  '理解您的顾虑，我们可以先把相关情况了解清楚。',
  '具体情况需要由医生结合检查评估，我可以协助安排咨询。',
  '很抱歉让您感到不便，我们先一起确认下一步处理方式。',
  '费用需要结合检查后的方案确认，我可以说明咨询和报价流程。'
];

/* 引导只出现一次：辅助面板首次自动展开，用户产生任意交互后永久记录。 */
const ASSIST_GUIDED_KEY = 'training_assist_guided_v1';
/* 轮询指数退避：1s → 2s → 4s → 封顶 8s，30 秒总窗不变。 */
const POLL_BASE_DELAY = 1000;
const POLL_MAX_DELAY = 8000;

const nextPollDelay = delay => Math.min(delay * 2, POLL_MAX_DELAY);

/* 提示的条数由后端裁定（总 3 条、每轮 1 条），前端只负责展示口径一致：
   hintRemaining 是总剩余，hintRemainingThisRound 是「本轮还能不能点」。
   两者都要在 WXML 里当布尔/数字直接用，不能写成函数调用表达式。 */
const normalizeHints = hints => (hints || []).map(hint => Object.assign({}, hint, {
  roundText: hint.round > 0 ? `第 ${hint.round} 轮` : ''
}));

/* 信任档位：后端 patientState.trustLevel（0—100）按 5 档展示。
   给学员看档位而不是精确数字，避免盯着分数套路化应对。
   tone 复用 app.wxss 的 .tag-* 色 token，与情绪标签同一套视觉。 */
const TRUST_TIERS = [
  { min: 80, text: '很高', tone: 'tag-success' },
  { min: 60, text: '较好', tone: 'tag-success' },
  { min: 40, text: '中性', tone: 'tag-neutral' },
  { min: 20, text: '较低', tone: 'tag-warning' },
  { min: 0, text: '很低', tone: 'tag-danger' }
];

const trustBadge = patientState => {
  const level = patientState && typeof patientState.trustLevel === 'number'
    ? patientState.trustLevel : 50;
  const tier = TRUST_TIERS.find(item => level >= item.min) || TRUST_TIERS[TRUST_TIERS.length - 1];
  return { trustText: tier.text, trustTone: tier.tone };
};

Page({
  data: {
    contextVersion: 1,
    initializationStatus: '',
    initializationText: '',
    interactionBlocked: true,
    publicProfile: null,
    currentEmotion: '',
    loadError: '',
    retryingInitialization: false,
    session: null,
    scenario: null,
    messages: [],
    inputValue: '',
    currentRound: 0,
    maxRounds: 10,
    scrollToView: '',
    sending: false,
    finishing: false,
    pendingClientMessageId: '',
    failedMessage: null,
    showAssistGuide: false,
    hints: [],
    hintLimit: 3,
    hintRemaining: 3,
    hintRemainingThisRound: 1,
    requestingHint: false,
    quickPhrases: QUICK_PHRASES,
    assistExpanded: false,
    customProfile: null,
    trustText: '中性',
    trustTone: 'tag-neutral',
    exitSheetVisible: false
  },

  sessionId: '',
  pendingPollTimer: null,

  onLoad(options) {
    this._visible = true;
    this._epoch = (this._epoch || 0) + 1;
    this.sessionId = options.sessionId || '';
    // master：缺 sessionId 直接弹窗回场景列表，而不是让后续请求炸在 getSession 上
    if (!this.sessionId) {
      this.handleMissingSession();
      return;
    }
    // 读取本地存储的自定义画像（兜底，后端持久化为准）
    let customProfile = null;
    try {
      const stored = wx.getStorageSync(`customProfile_${this.sessionId}`);
      if (stored) customProfile = JSON.parse(stored);
    } catch (e) { /* ignore parse error */ }
    this.setData({ customProfile });
    // 首次训练：辅助面板默认展开 + 一次性引导气泡（storage 记录，交互后落盘）
    let showAssistGuide = false;
    try {
      showAssistGuide = !wx.getStorageSync(ASSIST_GUIDED_KEY);
    } catch (e) { /* ignore storage error */ }
    this.setData({ customProfile, showAssistGuide, assistExpanded: showAssistGuide });
    this.loadSession();
  },

  markAssistGuided() {
    if (!this.data.showAssistGuide) return;
    try { wx.setStorageSync(ASSIST_GUIDED_KEY, '1'); } catch (e) { /* ignore */ }
    this.setData({ showAssistGuide: false });
  },

  onShow() {
    this._visible = true;
    this._epoch = (this._epoch || 0) + 1;
    if (this.sessionId) {
      this.setData({ sending: false, requestingHint: false, retryingInitialization: false });
      this.loadSession();
    }
  },

  onHide() {
    this._visible = false;
    this._epoch = (this._epoch || 0) + 1;
    clearTimeout(this.pendingPollTimer);
    clearTimeout(this.initializationTimer);
  },

  onUnload() { this.onHide(); },

  requestScope() {
    const epoch = this._epoch;
    const sessionId = this.sessionId;
    return () => this._visible !== false && epoch === this._epoch && sessionId === this.sessionId;
  },

  retryInitialization() {
    if (this.data.initializationStatus !== 'failed' || this.data.retryingInitialization) return;
    const active = this.requestScope();
    this.setData({ retryingInitialization: true });
    api.retryPatientInitialization(this.sessionId).then(() => {
      if (active()) this.loadSession();
    }).catch(error => {
      if (active()) this.setData({ loadError: error.message || '重试失败，请稍后重试' });
    }).finally(() => { if (active()) this.setData({ retryingInitialization: false }); });
  },

  handleMissingSession() {
    wx.showModal({
      title: '无法打开训练',
      content: '页面链接缺少会话信息，请从场景列表重新进入。',
      showCancel: false,
      success: () => wx.switchTab({ url: '/pages/index/index' })
    });
  },

  loadSession() {
    const active = this.requestScope();
    const version = this._loadVersion = (this._loadVersion || 0) + 1;
    clearTimeout(this.initializationTimer);
    return api.getSession(this.sessionId).then(detail => {
      if (!active() || version !== this._loadVersion) return null;
      if (detail.session.contextVersion >= 2) return [detail, { items: [{ id: detail.session.scenarioId, name: detail.session.scenarioName }] }];
      return api.getScenarios().then(scenarios => [detail, scenarios]);
    }).then(result => {
      if (!result || !active() || version !== this._loadVersion) return;
      const [detail, scenarioData] = result;
      const scenario = scenarioData.items.find(item => item.id === detail.session.scenarioId) || { name: detail.session.scenarioName };
      const v2 = detail.session.contextVersion >= 2;
      const status = v2 ? detail.session.initializationStatus : 'ready';
      const labels = { pending: '患者正在排队生成，可暂存退出后继续', generating: '正在生成患者，请稍候', failed: '患者初始化失败，请重试', ready: '患者已就绪' };
      this.setData({ contextVersion: v2 ? 2 : 1, initializationStatus: status,
        initializationText: labels[status] || '初始化状态暂不可用，请刷新',
        interactionBlocked: status !== 'ready' || detail.session.status !== 'in_progress',
        publicProfile: v2 ? detail.session.publicProfile : null,
        currentEmotion: (detail.patientState || {}).emotion || '', loadError: '' });
      if (v2 && ['pending', 'generating'].includes(status) && detail.session.status === 'in_progress') {
        this.initializationTimer = setTimeout(() => { if (active()) this.loadSession(); }, 2000);
      }
      // 优先使用后端持久化的自定义画像，本地缓存仅作兜底
      const backendProfile = (detail.session && detail.session.customPatientProfile) || null;
      const customProfile = v2 ? null : backendProfile || this.data.customProfile;
      const pendingMessage = detail.pendingMessage || null;
      this.setData(Object.assign({
        session: detail.session,
        scenario: v2 ? scenario : normalizeScenario(scenario, customProfile),
        customProfile,
        messages: normalizeMessages(detail.messages),
        pendingClientMessageId: pendingMessage ? pendingMessage.clientMessageId : '',
        inputValue: pendingMessage ? pendingMessage.content : this.data.inputValue,
        currentRound: detail.session.currentRound,
        maxRounds: detail.session.maxRounds,
        hints: normalizeHints(detail.hints),
        hintLimit: detail.hintLimit || 3,
        hintRemaining: detail.hintRemaining === undefined ? 3 : detail.hintRemaining,
        hintRemainingThisRound: detail.hintRemainingThisRound === undefined
          ? 1 : detail.hintRemainingThisRound,
        finishing: detail.session.status === 'completed',
        /* 档位必须在训练中可见（D1 的核心论据）：否则学员打完进阶档看到低分，
           会归因成「我退步了」，而这正是要避免的那种情绪。
           「不计入计划达标」也要写在这里——学员有权知道挑战不会拖累自己的计划。 */
        tierText: detail.session.difficultyTier === 'advanced'
          ? '进阶档 · 成绩不计入培训计划达标' : ''
      }, trustBadge(detail.patientState)), () => {
        this.scrollToBottom();
        if (pendingMessage && pendingMessage.replyStatus === 'generating') {
          this.pollPendingReply(pendingMessage.clientMessageId, pendingMessage.content, Date.now(), POLL_BASE_DELAY);
        }
        // 离开后回来发现上一轮已失败：恢复失败气泡，避免只剩一句 toast 线索
        if (pendingMessage && pendingMessage.replyStatus === 'failed') {
          this.setData({
            failedMessage: {
              clientMessageId: pendingMessage.clientMessageId,
              content: pendingMessage.content,
              reason: '回复生成失败，请重试'
            }
          });
        }
      });
    }).catch(error => {
      if (!active() || version !== this._loadVersion) return;
      this.setData({ loadError: error.message || '会话加载失败，请刷新重试', interactionBlocked: true });
    });
  },

  onInputChange(e) { this.setData({ inputValue: e.detail.value }); },

  useQuickPhrase(e) {
    if (this.data.interactionBlocked || this.data.sending || this.data.finishing) return;
    const phrase = e.currentTarget.dataset.phrase || '';
    if (!phrase) return;
    this.setData({ inputValue: phrase });
  },

  toggleAssist() {
    this.markAssistGuided();
    this.setData({ assistExpanded: !this.data.assistExpanded });
  },

  requestHint() {
    const active = this.requestScope();
    this.markAssistGuided();
    // 总数用完或本轮已用过都直接返回：后端的 409 是裁判，前端这里只是别让按钮变成
    // 「点了没反应」——文案由 hintRemainingThisRound 决定。
    if (this.data.interactionBlocked || this.data.currentRound < 1 || this.data.requestingHint || this.data.finishing) return;
    if (this.data.hintRemaining <= 0 || this.data.hintRemainingThisRound <= 0) return;
    this.setData({ requestingHint: true });
    api.requestTrainingHint(this.sessionId).then(data => {
      if (!active()) return;
      const hint = data.hint;
      const hints = hint ? this.data.hints.concat(normalizeHints([hint])) : this.data.hints;
      this.setData({
        hints,
        hintLimit: data.hintLimit || this.data.hintLimit,
        hintRemaining: data.hintRemaining === undefined ? this.data.hintRemaining : data.hintRemaining,
        hintRemainingThisRound: data.hintRemainingThisRound === undefined
          ? this.data.hintRemainingThisRound : data.hintRemainingThisRound,
        requestingHint: false
      }, () => this.scrollToBottom());
    }).catch(error => {
      if (!active()) return;
      // 限额类冲突要顺带刷新「本轮/总剩余」，否则界面会一直显示还能点。
      this.setData({ requestingHint: false });
      const limited = error.code === 'HINT_LIMIT_REACHED' || error.code === 'HINT_ROUND_LIMIT_REACHED';
      if (limited) this.loadSession();
      api.showCenterNotice({ title: error.message || '训练提示获取失败' });
    });
  },

  sendMessage() {
    const active = this.requestScope();
    const content = this.data.inputValue.trim();
    if (this.data.interactionBlocked || !content || this.data.sending || this.data.finishing || this.data.currentRound >= this.data.maxRounds) return;
    this.markAssistGuided();
    const clientMessageId = this.data.pendingClientMessageId || `client-msg-${Date.now()}`;
    this.setData({ sending: true, failedMessage: null });
    api.sendMessage(this.sessionId, clientMessageId, content).then(data => {
      if (!active()) return;
      this.setData({ pendingClientMessageId: '', inputValue: '', sending: false, failedMessage: null });
      if (data.session.shouldFinish) {
        this.setData({ finishing: true });
        wx.redirectTo({ url: `/pages/result/result?sessionId=${this.sessionId}` });
        return;
      }
      this.loadSession();
    }).catch(error => {
      if (!active()) return;
      this.setData({ pendingClientMessageId: clientMessageId, inputValue: content });
      if (error.code === 'SESSION_RESPONSE_PENDING') {
        this.pollPendingReply(clientMessageId, content, Date.now(), POLL_BASE_DELAY);
        return;
      }
      // 失败不再只靠 toast：渲染成失败气泡 + 重试按钮，重试复用同一 client_message_id（后端幂等）
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
    // 复用原 client_message_id：后端对同 id 幂等，不会产生重复消息
    this.setData({
      failedMessage: null,
      pendingClientMessageId: failed.clientMessageId,
      inputValue: failed.content
    });
    this.sendMessage();
  },

  pollPendingReply(clientMessageId, content, startedAt, delay) {
    const active = this.requestScope();
    if (this.pendingPollTimer) clearTimeout(this.pendingPollTimer);
    const wait = delay || POLL_BASE_DELAY;
    this.setData({ sending: true, pendingClientMessageId: clientMessageId, inputValue: content });
    api.getSession(this.sessionId).then(detail => {
      if (!active()) return;
      const pending = detail.pendingMessage || null;
      this.setData(Object.assign({
        session: detail.session,
        messages: normalizeMessages(detail.messages || []),
        currentRound: detail.session.currentRound,
        maxRounds: detail.session.maxRounds,
        hints: normalizeHints(detail.hints),
        hintLimit: detail.hintLimit === undefined ? this.data.hintLimit : detail.hintLimit,
        hintRemaining: detail.hintRemaining === undefined
          ? this.data.hintRemaining : detail.hintRemaining,
        hintRemainingThisRound: detail.hintRemainingThisRound === undefined
          ? this.data.hintRemainingThisRound : detail.hintRemainingThisRound
        , currentEmotion: (detail.patientState || {}).emotion || ''
      }, trustBadge(detail.patientState)), () => this.scrollToBottom());
      if (detail.session.status === 'completed') {
        this.setData({ sending: false, finishing: true, pendingClientMessageId: '', inputValue: '' });
        wx.redirectTo({ url: `/pages/result/result?sessionId=${this.sessionId}` });
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
        // 超时不是失败：模型可能仍在生成，原消息保留，学员可稍后回来或再点发送续查
        this.setData({ sending: false, pendingClientMessageId: clientMessageId, inputValue: content });
        api.showCenterNotice({ title: '回复仍在生成，原消息已保留，可稍后回来查看' });
        return;
      }
      this.pendingPollTimer = setTimeout(
        () => this.pollPendingReply(clientMessageId, content, startedAt, nextPollDelay(wait)), wait);
    }).catch(() => {
      if (!active()) return;
      if (Date.now() - startedAt >= 30000) {
        this.setData({ sending: false, pendingClientMessageId: clientMessageId, inputValue: content });
        api.showCenterNotice({ title: '网络异常，进度已保存，原消息已保留' });
        return;
      }
      this.pendingPollTimer = setTimeout(
        () => this.pollPendingReply(clientMessageId, content, startedAt, nextPollDelay(wait)), wait);
    });
  },

  exitTraining() {
    const active = this.requestScope();
    if (this.data.sending) {
      api.showCenterNotice({ title: '患者回复中，请稍候' });
      return;
    }
    if (this.data.pendingClientMessageId) {
      api.showCenterNotice({ title: '请先重试未回复的原消息' });
      return;
    }
    if (this.data.finishing) {
      api.showCenterNotice({ title: '报告生成中，请稍候' });
      return;
    }
    /* 一轮都没完成时，后端必然以 MIN_ROUNDS_NOT_REACHED 拒绝评分，
       所以这里不给「结束并生成报告」——避免点了必然失败、还会卡住的入口。
       剩下的两档都保留：「暂存并退出」留进度；「强制结束」作废本次训练。
       把限制写进面板本身（微信操作面板不支持禁用单项），而不是让用户点了才知道。 */
    const hasRounds = !this.data.interactionBlocked && this.data.currentRound >= 1;
    const items = hasRounds
      ? ['结束并生成报告', '暂存并退出', '强制结束（不生成报告）']
      : ['暂存并退出（本轮不足，报告需完成 1 轮后生成）', '强制结束（不生成报告）'];
    wx.showActionSheet({
      itemList: items,
      success: result => {
        if (!active()) return;
        const label = items[result.tapIndex];
        if (label.indexOf('结束并生成报告') === 0) this.finishTraining();
        else if (label.indexOf('暂存并退出') === 0) this.leaveTraining();
        else if (label.indexOf('强制结束') === 0) this.abandonTraining();
      },
      fail: error => {
        // 用户点空白处/返回键取消是正常交互，不提示
        if (error && /cancel/i.test(error.errMsg || '')) return;
        api.showCenterNotice({ title: '面板打开失败，请重试' });
      }
    });
  },

  finishTraining() {
    const active = this.requestScope();
    if (this.data.interactionBlocked) return;
    if (this.data.currentRound < 1) {
      api.showCenterNotice({ title: '至少完成 1 轮对话' });
      return;
    }
    if (this.data.sending) {
      api.showCenterNotice({ title: '患者回复中，请稍候' });
      return;
    }
    if (this.data.pendingClientMessageId) {
      api.showCenterNotice({ title: '请先重试未回复的原消息' });
      return;
    }
    if (this.data.finishing) {
      api.showCenterNotice({ title: '报告生成中，请稍候' });
      return;
    }
    wx.showModal({
      title: '结束本次训练？',
      content: '结束后将根据完整对话生成训练报告，结束后不能继续发送消息。',
      confirmText: '结束评分',
      success: result => { if (active() && result.confirm) this.completeTraining(); },
      fail: () => api.showCenterNotice({ title: '确认框打开失败，请重试' })
    });
  },

  completeTraining() {
    const active = this.requestScope();
    if (this.data.interactionBlocked) return;
    this.setData({ finishing: true });
    api.finishSession(this.sessionId).then(() => {
      if (!active()) return;
      // 训练完成，清理自定义画像缓存
      wx.removeStorageSync(`customProfile_${this.sessionId}`);
      wx.redirectTo({ url: `/pages/result/result?sessionId=${this.sessionId}` });
    }).catch(error => {
      if (!active()) return;
      this.setData({ finishing: false });
      api.showCenterNotice({ title: error.message || '结束训练失败' });
    });
  },

  abandonTraining() {
    const active = this.requestScope();
    if (this.data.sending || this.data.finishing) {
      api.showCenterNotice({ title: '回复生成中，请稍候' });
      return;
    }
    wx.showModal({
      title: '强制结束训练？',
      content: '强制结束不会生成报告，该次训练也不会计入你的训练结果。',
      confirmText: '强制结束',
      cancelText: '取消',
      success: result => {
        if (!active() || !result.confirm) return;
        wx.showLoading({ title: '正在结束…', mask: true });
        api.abandonSession(this.sessionId).then(() => {
          wx.hideLoading();
          if (!active()) return;
          wx.removeStorageSync(`customProfile_${this.sessionId}`);
          api.showCenterNotice({ title: '已强制结束' });
          setTimeout(() => { if (active()) wx.switchTab({ url: '/pages/index/index' }); }, 800);
        }).catch(error => {
          wx.hideLoading();
          if (!active()) return;
          api.showCenterNotice({ title: error.message || '强制结束失败' });
        });
      }
    });
  },

  /* master：发送/生成中禁止返回，避免会话状态半开。 */
  leaveTraining() {
    if (!this.data.sending && !this.data.finishing) wx.navigateBack();
  },

  scrollToBottom() { this.setData({ scrollToView: 'message-bottom' }); }
});
