const request = require('../../static/api/request.js');
const util = require('../../utils/util.js');
const llm = require('../../utils/llm.js');

// ===== Mock 数据（LLM 不可用时的降级方案） =====

// 每个场景的患者开场白
const MOCK_OPENINGS = {
  'mock-1': '您好，我最近咨询了几家诊所的种植牙，发现你们的报价比别家贵了将近一倍，这个差价合理吗？',
  'mock-2': '你好，我今年30岁了，牙齿有点不整齐想矫正一下。但是听说年纪大了效果不好，而且戴牙套会很疼，是真的吗？',
  'mock-3': '你们这个项目也太贵了吧！我朋友在隔壁诊所做的，价格只有你们的一半。你给我解释解释这钱花在哪？',
  'mock-4': '我昨天在你们这拔完智齿后，到现在还疼得睡不着，吃了止痛药也没用！你们是不是手术出问题了？！',
  'mock-5': '我今天预约的10点洗牙，现在都10点40了还没排到我！前台那个小姑娘态度还爱理不理的，你们就是这样对患者的？',
  'mock-6': '我就想简简单单洗个牙，你们别给我推销那些乱七八糟的项目。之前去别的诊所就被忽悠花了好几千，现在学聪明了。'
};

// 每个场景的多轮患者回复序列（按轮次消费）
const MOCK_SEQUENCES = {
  'mock-1': [
    { content: '那你们用的到底是什么品牌的种植体？跟我之前打听的韩系的有啥区别？', emotion: '将信将疑' },
    { content: '质保方面呢？万一出了问题你们管不管？', emotion: '价格敏感' },
    { content: '听你这么说，好像贵也有贵的道理…那能给我具体讲讲方案吗？', emotion: '开始接受' },
    { content: '好的，那费用能不能分期？一次性付压力有点大。', emotion: '价格敏感' },
    { content: '行，那我回去跟家人商量一下，没什么问题就约时间。', emotion: '积极配合' }
  ],
  'mock-2': [
    { content: '那大概要戴多久牙套？我上班会不会很影响形象？', emotion: '犹豫不决' },
    { content: '费用方面呢？能不能分期？', emotion: '价格敏感' },
    { content: '我真的年纪不算大？我同事都说30岁做正畸已经晚了…', emotion: '将信将疑' },
    { content: '好吧，那我先预约一个面诊，让医生看看具体怎么弄。', emotion: '开始接受' },
    { content: '好的谢谢，你解答得挺详细的，我这周末有空，帮我约一下。', emotion: '积极配合' }
  ],
  'mock-3': [
    { content: '你光说技术好，具体好在哪？能给我举几个例子吗？', emotion: '将信将疑' },
    { content: '那医生资质怎么样？别给我安排个实习生吧？', emotion: '疑虑' },
    { content: '行吧，那你把方案详细列出来我看看。', emotion: '开始接受' },
    { content: '治疗方案能不能再详细解释一下？我怕有隐藏费用。', emotion: '价格敏感' },
    { content: '好，你说得比较清楚了，那我先预约个检查，确认一下具体情况。', emotion: '积极配合' }
  ],
  'mock-4': [
    { content: '术后疼痛是正常的？那要疼多久才算正常？我现在这种程度算正常吗？', emotion: '焦虑不安' },
    { content: '那我需要再过来复查吗？会不会有什么后遗症？', emotion: '焦虑不安' },
    { content: '好吧，那我按你说的再观察两天。如果还疼我再来找你。', emotion: '开始接受' },
    { content: '嗯…你说得对，可能是我太紧张了。那止疼药有什么推荐的吗？', emotion: '焦虑不安' },
    { content: '好的，谢谢你的耐心解答，我心里踏实多了。', emotion: '积极配合' }
  ],
  'mock-5': [
    { content: '道歉有什么用？你们管理也太混乱了吧，怎么能让患者这么等？', emotion: '愤怒不满' },
    { content: '那你说怎么办吧，总不能就这么算了吧？', emotion: '愤怒不满' },
    { content: '好吧，看你态度还算诚恳。要是下次还这样我肯定要投诉的。', emotion: '开始接受' },
    { content: '那现在能马上安排我洗牙吗？我不想再等了。', emotion: '将信将疑' },
    { content: '好的，这次就算了。希望你以后能改进，患者的时间也很宝贵。', emotion: '积极配合' }
  ],
  'mock-6': [
    { content: '哦？那你说说看，发现了什么？不过别跟我推销太贵的。', emotion: '将信将疑' },
    { content: '补牙的话大概多少钱？太贵的话我可不要。', emotion: '价格敏感' },
    { content: '行吧，那先检查看看。如果真的需要补，我再考虑。', emotion: '开始接受' },
    { content: '你确实没有像之前那些诊所一样一上来就推销，这点不错。', emotion: '开始接受' },
    { content: '好的，那就听你的，先全面检查一下，有什么问题再告诉我。', emotion: '积极配合' }
  ]
};

// 兜底回复（序列用完时循环使用）
const MOCK_FALLBACK = [
  { content: '嗯…你说的也有道理。那我再考虑考虑吧。', emotion: '犹豫不决' },
  { content: '那你能保证效果吗？万一花了钱还没效果怎么办？', emotion: '疑虑' },
  { content: '好吧，那你把具体的流程和费用明细发我看看。', emotion: '开始接受' },
  { content: '你说的这个我倒是没想过…那这样吧，我先预约检查一下。', emotion: '积极配合' },
  { content: '我明白了。那你们这边有什么优惠活动吗？', emotion: '价格敏感' }
];

function getMockResponse(scenarioId, index) {
  const seq = MOCK_SEQUENCES[scenarioId];
  if (seq && index < seq.length) return seq[index];
  if (seq) return MOCK_FALLBACK[(index - seq.length) % MOCK_FALLBACK.length];
  return MOCK_FALLBACK[index % MOCK_FALLBACK.length];
}

function formatTime(date) {
  const d = date || new Date();
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
}

Page({
  data: {
    sessionId: '',
    scenarioId: '',
    scenarioName: 'AI 模拟患者',
    patientProfile: '',
    messages: [],
    currentRound: 0,
    maxRounds: 10,
    remainingRounds: 10,
    inputValue: '',
    scrollToView: '',
    loading: true,
    sending: false,
    finishing: false,
    isTrainingEnd: false,
    isMock: false,
    errorMessage: '',
    goldenPhrase: ''
  },

  pendingMessage: null,
  _mockSeqIndex: 0,
  _mockTimer: null,

  onLoad(options) {
    const sessionId = options.sessionId || '';
    const scenarioId = options.scenarioId || '';
    this.setData({ sessionId, scenarioId });

    if (sessionId.startsWith('mock-session-')) {
      this.initMockSession(scenarioId);
      return;
    }

    if (sessionId) {
      this.loadSession();
    } else if (scenarioId) {
      this.createSession();
    } else {
      this.setData({ loading: false, errorMessage: '缺少训练会话信息' });
    }
  },

  onUnload() {
    if (this._mockTimer) clearTimeout(this._mockTimer);
  },

  // ========== Mock 模式 ==========

  initMockSession(scenarioId) {
    const app = getApp();
    const sessionData = app.globalData.currentSession;
    const scenario = sessionData.scenarioData;

    if (!scenario) {
      this.setData({ loading: false, errorMessage: '场景数据加载失败，请返回重试' });
      return;
    }

    const profile = scenario.patientProfile || {};
    let profileParts = [`${profile.age || '--'}岁`, profile.description || ''];
    if (profile.personality) profileParts.push(profile.personality);
    if (profile.sensitivity) profileParts.push(`敏感度${profile.sensitivity}`);

    this.setData({
      isMock: true,
      loading: true,
      scenarioName: scenario.name || 'AI 模拟患者',
      scenarioId: scenario.id,
      patientProfile: profileParts.filter(Boolean).join(' · '),
      maxRounds: 10,
      remainingRounds: 10,
      goldenPhrase: '',
      messages: [],
      scrollToView: 'msg-bottom'
    });

    // 优先尝试 LLM 生成开场白，失败则用 Mock
    this.fetchOpening(scenario);
  },

  async fetchOpening(scenario) {
    try {
      const resp = await llm.generateOpening(scenario);
      const opening = resp.content || MOCK_OPENINGS[scenario.id] || '您好，我有些口腔问题想咨询一下。';
      const emotion = resp.emotion || scenario.patientProfile?.initialEmotion || '';
      this.appendPatientMessage(opening, emotion);
      this.setData({ loading: false });
    } catch (err) {
      console.warn('LLM 开场白生成失败，使用 Mock 数据', err);
      const opening = MOCK_OPENINGS[scenario.id] || '您好，我有些口腔问题想咨询一下。';
      const emotion = scenario.patientProfile?.initialEmotion || '';
      this.appendPatientMessage(opening, emotion);
      this.setData({ loading: false });
    }
  },

  appendPatientMessage(content, emotionLabel) {
    const msg = {
      id: `msg-patient-${Date.now()}`,
      role: 'patient',
      content,
      createdAt: formatTime(),
      emotionLabel: emotionLabel || ''
    };
    this.setData({
      messages: [...this.data.messages, msg],
      scrollToView: 'msg-bottom'
    });
  },

  // ========== 真实 API 模式 ==========

  sessionPath(suffix = '') {
    return `/sessions/${encodeURIComponent(this.data.sessionId)}${suffix}`;
  },

  async createSession() {
    try {
      const data = await request.post('/sessions', {
        scenarioId: this.data.scenarioId
      });
      if (!data || !data.session || !data.session.id) {
        throw new Error('服务端未返回有效训练会话');
      }
      this.setData({ sessionId: data.session.id });
      getApp().setCurrentSession({
        id: data.session.id,
        scenarioId: this.data.scenarioId
      });
      this.applySessionData(data);
      this.loadScenarioProfile(this.data.scenarioId);
    } catch (error) {
      this.setData({
        loading: false,
        errorMessage: request.getErrorMessage(error, '创建训练失败')
      });
    }
  },

  async loadSession() {
    try {
      const data = await request.get(this.sessionPath());
      this.applySessionData(data);
      if (data && data.session) {
        const s = data.session;
        getApp().setCurrentSession({
          id: s.id,
          scenarioId: s.scenarioId,
          scenarioName: s.scenarioName,
          scenarioCategory: s.scenarioCategory
        });
        this.loadScenarioProfile(s.scenarioId);
      }
    } catch (error) {
      this.setData({
        loading: false,
        errorMessage: request.getErrorMessage(error, '训练会话加载失败')
      });
    }
  },

  async loadScenarioProfile(scenarioId) {
    try {
      const data = await request.get('/scenarios');
      const scenario = (data.items || []).find(item => String(item.id) === String(scenarioId));
      if (scenario && scenario.patientProfile) {
        const profile = scenario.patientProfile;
        let profileParts = [`${profile.age}岁`, profile.description];
        if (profile.personality) profileParts.push(profile.personality);
        if (profile.sensitivity) profileParts.push(profile.sensitivity);
        this.setData({
          scenarioName: scenario.name,
          patientProfile: profileParts.filter(Boolean).join(' · ')
        });
      }
    } catch (error) {
      console.warn('加载场景公开信息失败', error);
    }
  },

  applySessionData(data) {
    const session = data.session || {};
    const messages = Array.isArray(data.messages) ? data.messages : [];
    const currentRound = session.currentRound || 0;
    const maxRounds = session.maxRounds || 10;

    this.setData({
      loading: false,
      scenarioId: session.scenarioId || this.data.scenarioId,
      scenarioName: session.scenarioName || this.data.scenarioName,
      messages,
      currentRound,
      maxRounds,
      remainingRounds: Math.max(maxRounds - currentRound, 0),
      isTrainingEnd: session.status !== 'in_progress',
      errorMessage: ''
    }, () => {
      this.setData({ scrollToView: 'msg-bottom' });
    });
  },

  // ========== 输入 & 发送 ==========

  onInputChange(e) {
    this.setData({ inputValue: e.detail.value });
  },

  dismissGoldenPhrase() {
    this.setData({ goldenPhrase: '' });
  },

  async sendMessage() {
    if (this.data.sending || this.data.finishing || this.data.isTrainingEnd) return;

    const content = this.data.inputValue.trim();
    if (!content) return;

    this.setData({ sending: true, inputValue: '' });

    if (this.data.isMock) {
      await this.sendMockMessage(content);
      return;
    }

    // 真实 API 模式
    const pending = this.pendingMessage && this.pendingMessage.content === content
      ? this.pendingMessage
      : { clientMessageId: `client-${Date.now()}`, content };

    this.pendingMessage = pending;
    try {
      const data = await request.post(this.sessionPath('/messages'), {
        clientMessageId: pending.clientMessageId,
        content: pending.content
      });
      if (!data || !data.userMessage || !data.patientMessage) {
        throw new Error('患者回复数据不完整');
      }

      const messages = [...this.data.messages, data.userMessage, data.patientMessage];
      const session = data.session || {};

      this.pendingMessage = null;
      this.setData({
        messages,
        currentRound: session.currentRound || this.data.currentRound + 1,
        remainingRounds: session.remainingRounds === undefined
          ? Math.max(this.data.maxRounds - (session.currentRound || this.data.currentRound + 1), 0)
          : session.remainingRounds,
        sending: false,
        goldenPhrase: data.goldenPhrase || '',
        scrollToView: 'msg-bottom'
      });

      if (session.shouldFinish || session.currentRound >= this.data.maxRounds) {
        this.finishTraining('max_rounds');
      }
    } catch (error) {
      this.setData({ sending: false, inputValue: pending.content });
      util.showToast(request.getErrorMessage(error, '患者回复失败，可重试'));
    }
  },

  // ========== 发送消息（LLM 优先，Mock 降级） ==========

  async sendMockMessage(content) {
    const userMsg = {
      id: `msg-user-${Date.now()}`,
      role: 'user',
      content,
      createdAt: formatTime()
    };

    const newRound = this.data.currentRound + 1;
    const remaining = Math.max(this.data.maxRounds - newRound, 0);

    // 立即显示用户消息
    this.setData({
      messages: [...this.data.messages, userMsg],
      currentRound: newRound,
      remainingRounds: remaining,
      scrollToView: 'msg-bottom'
    });

    // 先尝试 LLM，失败则用 Mock
    try {
      await this.fetchLLMReply(content, newRound, remaining);
    } catch (err) {
      console.warn('LLM 回复失败，降级使用 Mock 数据', err);
      this.fallbackMockReply(newRound, remaining);
    }
  },

  async fetchLLMReply(userContent, newRound, remaining) {
    const scenario = getApp().globalData.currentSession.scenarioData || {};
    const history = this.buildHistoryForLLM();

    const resp = await llm.generateReply({
      scenarioId: this.data.scenarioId,
      scenarioName: this.data.scenarioName,
      patientProfile: scenario.patientProfile || {},
      history,
      userMessage: userContent,
      currentRound: newRound,
      maxRounds: this.data.maxRounds
    });

    const patientMsg = {
      id: `msg-patient-${Date.now() + 1}`,
      role: 'patient',
      content: resp.content || '嗯…你说的也有道理。',
      createdAt: formatTime(),
      emotionLabel: resp.emotion || ''
    };

    this.setData({
      messages: [...this.data.messages, patientMsg],
      sending: false,
      goldenPhrase: '',
      scrollToView: 'msg-bottom'
    });

    if (remaining <= 0) {
      this._mockTimer = setTimeout(() => this.finishTraining('max_rounds'), 600);
    }
  },

  fallbackMockReply(newRound, remaining) {
    const resp = getMockResponse(this.data.scenarioId, this._mockSeqIndex);
    this._mockSeqIndex++;

    const patientMsg = {
      id: `msg-patient-${Date.now() + 1}`,
      role: 'patient',
      content: resp.content,
      createdAt: formatTime(),
      emotionLabel: resp.emotion || ''
    };

    this.setData({
      messages: [...this.data.messages, patientMsg],
      sending: false,
      goldenPhrase: '',
      scrollToView: 'msg-bottom'
    });

    if (remaining <= 0) {
      this._mockTimer = setTimeout(() => this.finishTraining('max_rounds'), 600);
    }
  },

  buildHistoryForLLM() {
    return this.data.messages
      .filter(m => m.role === 'user' || m.role === 'patient')
      .slice(-20)
      .map(m => ({ role: m.role, content: m.content }));
  },

  // ========== 提示 ==========

  async getHint() {
    if (this.data.sending || this.data.finishing) return;

    // 先尝试 LLM 生成提示
    if (this.data.isMock) {
      this.setData({ sending: true });
      try {
        const scenario = getApp().globalData.currentSession.scenarioData || {};
        const history = this.buildHistoryForLLM();
        const hint = await llm.generateHint({
          scenarioId: this.data.scenarioId,
          scenarioName: this.data.scenarioName,
          patientProfile: scenario.patientProfile || {},
          history
        });
        const messages = [...this.data.messages, {
          id: `hint-${Date.now()}`,
          role: 'system',
          content: `💡 提示：${hint}`,
          createdAt: formatTime()
        }];
        this.setData({ messages, sending: false, scrollToView: 'msg-bottom' });
      } catch (err) {
        console.warn('LLM 提示生成失败，使用静态提示', err);
        this.showStaticHint();
      }
      return;
    }

    // 真实 API 模式
    this.setData({ sending: true });
    try {
      const data = await request.post(this.sessionPath('/hint'));
      const hintText = data && data.hint ? data.hint : '请结合患者之前的顾虑，用同理心回应并引导下一步话题。';
      const messages = [...this.data.messages, {
        id: `hint-${Date.now()}`,
        role: 'system',
        content: `💡 提示：${hintText}`,
        createdAt: formatTime()
      }];
      this.setData({ messages, sending: false, scrollToView: 'msg-bottom' });
    } catch (error) {
      const messages = [...this.data.messages, {
        id: `hint-${Date.now()}`,
        role: 'system',
        content: '💡 提示：尝试用同理心回应患者的情绪，确认需求后再给出专业建议。',
        createdAt: formatTime()
      }];
      this.setData({ messages, sending: false, scrollToView: 'msg-bottom' });
    }
  },

  showStaticHint() {
    const hints = [
      '尝试用同理心回应患者的情绪，先认可TA的感受再给出专业建议。',
      '不要急于否定患者的看法，先倾听再引导，用开放式问题了解深层需求。',
      '展示专业性的同时保持亲和力，用通俗易懂的语言解释专业问题。',
      '注意捕捉患者话语中的关键顾虑点，逐一回应而不是笼统带过。',
      '当患者有价格疑虑时，先共情再分解价值，不要直接和竞品比价。'
    ];
    const hint = hints[Math.floor(Math.random() * hints.length)];
    const messages = [...this.data.messages, {
      id: `hint-${Date.now()}`,
      role: 'system',
      content: `💡 提示：${hint}`,
      createdAt: formatTime()
    }];
    this.setData({ messages, sending: false, scrollToView: 'msg-bottom' });
  },

  // ========== 结束训练 ==========

  finishTraining(reasonOrEvent) {
    // 兼容 bindtap 事件传入
    if (reasonOrEvent && reasonOrEvent.currentTarget) {
      reasonOrEvent = reasonOrEvent.currentTarget.dataset.reason || 'manual';
    }
    const reason = reasonOrEvent || 'manual';

    if (this.data.finishing || this.data.isTrainingEnd) return;
    if (this.data.currentRound < 1) {
      util.showToast('至少完成 1 轮对话后才能结束训练');
      return;
    }

    // Mock 模式
    if (this.data.isMock) {
      if (reason !== 'max_rounds') {
        wx.showModal({
          title: '结束训练',
          content: '确定结束本次训练吗？',
          success: (res) => {
            if (!res.confirm) return;
            this.setData({ finishing: true, isTrainingEnd: true });
            this.finishMockTraining();
          }
        });
        return;
      }
      this.setData({ finishing: true, isTrainingEnd: true });
      this.finishMockTraining();
      return;
    }

    // 真实 API 模式
    this.doFinishReal(reason);
  },

  async doFinishReal(reason) {
    const confirmed = reason === 'max_rounds'
      ? true
      : await util.showModal({ title: '结束训练', content: '确定结束本次训练并生成评分吗？' });
    if (!confirmed) return;

    this.setData({ finishing: true, isTrainingEnd: true });
    try {
      if (!this.data.sessionId) throw new Error('缺少训练会话 ID');
      await request.post(this.sessionPath('/finish'), { reason });
      getApp().clearCurrentSession();
      wx.redirectTo({
        url: `/pages/result/result?sessionId=${encodeURIComponent(this.data.sessionId)}`
      });
    } catch (error) {
      this.setData({ finishing: false, isTrainingEnd: false });
      util.showToast(request.getErrorMessage(error, '结束训练失败，请重试'));
    }
  },

  finishMockTraining() {
    if (this._mockTimer) {
      clearTimeout(this._mockTimer);
      this._mockTimer = null;
    }
    getApp().clearCurrentSession();
    this.setData({ finishing: false, isTrainingEnd: true });
    wx.showModal({
      title: '训练完成',
      content: '本次模拟训练已结束。\n接入AI后，这里将展示五维评分报告和逐轮点评。',
      showCancel: false,
      confirmText: '返回场景列表',
      success: (res) => {
        if (res.confirm) {
          wx.navigateBack({ delta: 1 });
        }
      },
      fail: () => {
        wx.navigateBack({ delta: 1 });
      }
    });
  }
});
