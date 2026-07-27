require('dotenv').config();
const express = require('express');
const cors = require('cors');
const OpenAI = require('openai').default || require('openai');
const crypto = require('crypto');

const app = express();
app.use(cors());
app.use(express.json());

// 请求日志
app.use((req, res, next) => {
  console.log(`[${new Date().toLocaleTimeString()}] ${req.method} ${req.url}`);
  next();
});

// ======================== LLM 配置 ========================
const client = new OpenAI({
  apiKey: process.env.MOONSHOT_API_KEY,
  baseURL: 'https://api.moonshot.cn/v1',
  timeout: 60000,
  maxRetries: 1
});
const MODEL = process.env.MOONSHOT_MODEL || 'kimi-k2.6';

async function callLLM(messages, maxTokens = 1000) {
  console.log('[LLM] 请求中, 消息数:', messages.length);
  const start = Date.now();
  try {
    const resp = await client.chat.completions.create({
      model: MODEL,
      messages,
      max_tokens: maxTokens,
      thinking: { type: 'disabled' }
    });
    const content = resp.choices[0].message.content || '';
    console.log('[LLM] 完成, 耗时:', Date.now() - start, 'ms, 内容长度:', content.length);
    return content;
  } catch (e) {
    console.error('[LLM] 失败, 耗时:', Date.now() - start, 'ms, 错误:', e.message);
    throw e;
  }
}

// ======================== 场景数据 ========================
const SCENARIOS = [
  {
    id: 'implant-basic', name: '种植牙基础咨询',
    summary: '患者咨询种植牙的流程、疼痛和大致时间。',
    difficulty: 'basic',
    focus: ['基础信息解释', '需求挖掘', '回应担忧', '引导专业检查'],
    patientProfile: { age: 52, gender: 'unknown', description: '缺失一颗后牙，对种植牙了解较少，平静但谨慎。' },
    hiddenConfig: {
      opening: '您好，我缺了一颗后牙，想问问种植牙大概怎么做，会不会很疼，要多久？',
      hidden: ['存在预算压力', '最担心手术疼痛'],
      initialState: { emotion: '平静', emotionLevel: 0, trustLevel: 50 },
      instructions: '逐步透露对疼痛、预算、医生经验和复查安排的顾虑。'
    },
    maxRounds: 10, sortOrder: 1,
    roleplayConfig: {
      suggestedQuestions: ['种植牙一般是怎样的流程？', '做种植牙会不会很疼？', '大概需要多久才能完成？', '我现在缺一颗牙，先要做哪些检查？'],
      serviceGuidance: ['先确认患者主要顾虑并说明可安排面诊检查。', '不得承诺疼痛程度、成功率、具体疗程或价格。', '涉及方案、是否适合种植和恢复情况时，明确由医生结合检查评估。']
    }
  },
  {
    id: 'orthodontic-basic', name: '正畸基础咨询',
    summary: '患者关注隐形矫正周期、费用和是否需要拔牙。',
    difficulty: 'basic',
    focus: ['澄清外观与功能诉求', '说明检查流程', '避免越权判断', '不承诺固定周期'],
    patientProfile: { age: 22, gender: 'unknown', description: '牙齿不整齐，考虑隐形矫正，期待但犹豫。' },
    hiddenConfig: {
      opening: '我牙齿有点不整齐，想做隐形矫正。一般要多久，大概要多少钱，需要拔牙吗？',
      hidden: ['即将毕业，担心影响求职形象', '预算有限', '对是否拔牙敏感'],
      initialState: { emotion: '犹豫', emotionLevel: 0, trustLevel: 50 },
      instructions: '重视面诊与影像检查，不接受客服直接判断拔牙。'
    },
    maxRounds: 10, sortOrder: 2,
    roleplayConfig: {
      suggestedQuestions: ['隐形矫正通常需要先做什么检查？', '矫正会影响日常说话或工作吗？', '我这种情况一定要拔牙吗？', '大概多久能看到变化？'],
      serviceGuidance: ['先了解患者对外观、时间和舒适度的关注。', '不能通过文字判断是否拔牙、是否适合隐形矫正或确定疗程。', '说明医生需结合口内检查和影像资料制定方案，价格以检查后的正式方案为准。']
    }
  },
  {
    id: 'price-comparison', name: '与其他诊所比价',
    summary: '患者认为当前报价偏高，关注价格背后的差异。',
    difficulty: 'advanced',
    focus: ['询问比较标准', '解释价格构成', '不贬低其他机构', '保持收费透明'],
    patientProfile: { age: 45, gender: 'unknown', description: '已经咨询多家诊所，对报价保持理性且警惕。' },
    hiddenConfig: {
      opening: '我已经问过好几家了，你们这里的报价明显更高，为什么要比别人贵这么多？',
      hidden: ['更关心医生经验和材料', '在意后续服务与收费透明度'],
      initialState: { emotion: '犹豫', emotionLevel: -1, trustLevel: 45 },
      instructions: '要求客观说明服务和费用，不接受贬低同行或绝对化承诺。'
    },
    maxRounds: 10, sortOrder: 3,
    roleplayConfig: {
      suggestedQuestions: ['为什么你们的报价和别家不一样？', '费用里通常包含哪些服务？', '材料和医生经验会有什么差别？', '如果我想比较，应该重点问哪些内容？'],
      serviceGuidance: ['承认患者比较价格的需求，不贬低其他机构。', '说明可从检查、材料、医生资质、复诊与服务范围等维度了解，但不编造本机构价格或套餐。', '以透明沟通和安排面诊、正式报价为下一步。']
    }
  },
  {
    id: 'post-treatment-discomfort', name: '术后不适咨询',
    summary: '患者治疗后出现疼痛或肿胀，担心治疗失败。',
    difficulty: 'advanced',
    focus: ['先安抚情绪', '追问症状信息', '识别风险信号', '及时联系医生'],
    patientProfile: { age: 38, gender: 'unknown', description: '治疗后不适，焦虑并希望尽快确认下一步。' },
    hiddenConfig: {
      opening: '我做完治疗后一直疼，还有点肿，我很担心是不是治疗失败了，你们能不能先帮我判断一下？',
      hidden: ['症状发生时间和程度尚不完整', '可能存在需要及时联系医生的风险信号'],
      initialState: { emotion: '焦虑', emotionLevel: -2, trustLevel: 35 },
      instructions: '若客服忽略症状追问或保证肯定正常，应表达不安并引导联系医生。不能给出诊断。'
    },
    maxRounds: 10, sortOrder: 4,
    roleplayConfig: {
      suggestedQuestions: ['治疗后一直疼还有点肿，正常吗？', '我现在需要立刻回来检查吗？', '出现哪些情况需要尽快联系医生？', '我很担心治疗是不是失败了。'],
      serviceGuidance: ['先安抚并收集症状出现时间、程度和是否加重等信息。', '不得诊断、开药、保证症状正常或断言治疗结果。', '提示出现明显加重或紧急不适时及时联系医生或按医疗机构指引就医，并协助安排复诊。']
    }
  }
];

// ======================== 内存存储 ========================
const sessions = new Map();        // key: sessionId
const messages = new Map();        // key: sessionId → array
const evaluations = new Map();     // key: sessionId

const rpSessions = new Map();      // key: sessionId
const rpMessages = new Map();      // key: sessionId → array
const rpSummaries = new Map();     // key: sessionId

const genId = () => crypto.randomUUID();

// ======================== 工具函数 ========================
function parseEmotion(text) {
  const m = text.match(/<情绪[:：]([^>]+)>/);
  if (m) return { content: text.replace(m[0], '').trim(), emotion: m[1].trim() };
  return { content: text.trim(), emotion: '' };
}

function nowISO() {
  return new Date().toISOString().replace('T', ' ').slice(0, 19);
}

function resOk(data) {
  return { code: 0, data };
}

function findScenario(id) {
  return SCENARIOS.find(s => s.id === id);
}

// ======================== 患者 System Prompt ========================
function buildPatientSystem(scenario, patientState) {
  const p = scenario.patientProfile;
  const state = patientState || scenario.hiddenConfig.initialState;
  const trustLabel = state.trustLevel >= 70 ? '比较信任' : state.trustLevel >= 40 ? '保持观察' : '不太信任';
  const emotionLabel = state.emotionLevel >= 2 ? '情绪积极放松' : state.emotionLevel >= 0 ? '情绪平稳' : state.emotionLevel >= -2 ? '有些忧虑' : '焦虑不安';
  const cooperativeness = state.trustLevel >= 65 ? '比较高，愿意配合' : state.trustLevel >= 40 ? '一般，需要更多说服' : '较低，需要用专业和耐心来争取';
  return `你正在参与牙科诊所的口腔咨询师沟通训练，扮演一位前来咨询的模拟患者。
场景主题：${scenario.name}
患者年龄：${p.age}岁
患者描述：${p.description}
当前患者情绪状态：${emotionLabel}
当前患者对客服的信任程度：${trustLabel}
当前配合度：${cooperativeness}

要求：
1. 只以患者口吻说话，不要解释、不要替医生说话、不要输出说明性文字。
2. 体现真实患者的顾虑、犹豫、比价心理或情绪反应，配合度应随着客服的表现动态变化。
3. 每次回复 1~3 句话，自然口语化，语气贴近真实对话。
4. 回复末尾用尖括号附上一个简短的情绪标签，例如：<情绪：将信将疑>。
   可选标签：平静、疑虑、焦虑、不满、将信将疑、信任、犹豫、抵触。
5. 不使用 Markdown、列表或标题。
6. 隐藏信息（你逐步透露）：${(scenario.hiddenConfig.hidden || []).join('；')}
7. 行为指引：${scenario.hiddenConfig.instructions || ''}`;
}

// ======================== 标准客服 System Prompt（角色扮演） ========================
function buildCustomerSystem(scenario) {
  const rp = scenario.roleplayConfig || {};
  const guidance = (rp.serviceGuidance || []).join('\n- ');
  return `你是一位专业口腔诊所的标准客服人员，正在与一位模拟患者沟通。
场景：${scenario.name}

服务指引：
- ${guidance}

回复要求：
1. 以专业客服口吻回复，自然、礼貌、有温度。
2. 每次回复 1~3 句话。
3. 不使用 Markdown、列表或标题。
4. 不做出任何医疗诊断、保证或承诺。
5. 回复末尾用<学习要点:xxx>标签标注 1~2 条可以从这段回复中学到的客服技巧。
6. 如果不确定，引导患者预约医生面诊。`;
}

// ======================== 训练评估 Prompt ========================
function buildEvaluationPrompt(scenario, conversationText) {
  return `你是一位口腔客服培训专家，请评估以下客服与模拟患者的对话。

场景：${scenario.name}
患者背景：${scenario.patientProfile.age}岁，${scenario.patientProfile.description}
训练重点：${(scenario.focus || []).join('、')}

完整对话：
${conversationText}

请从以下五个维度评分（0-100分）：
- knowledgeAccuracy：口腔知识准确性（权重25%）
- medicalCompliance：医疗合规（权重25%）
- empathy：情绪识别与同理心（权重20%）
- needsDiscovery：需求挖掘（权重20%）
- serviceEtiquette：服务礼仪（权重10%）

请严格按以下JSON格式返回评估结果（不要包含markdown代码块标记）：
{
  "dimensionScores": {
    "knowledgeAccuracy": 数字,
    "medicalCompliance": 数字,
    "empathy": 数字,
    "needsDiscovery": 数字,
    "serviceEtiquette": 数字
  },
  "totalScore": 数字（五维加权平均取整）,
  "summary": "综合评价，2-3句话",
  "strengths": [{"content": "做得好的地方1"}, {"content": "做得好的地方2"}],
  "improvements": [{"content": "改进建议1"}, {"content": "改进建议2"}],
  "violations": [
    {"type": "违规类型", "deduction": 扣分数, "quote": "原话引用", "reason": "违规原因", "rewrite": "建议改写"}
  ],
  "roundComments": [
    {"round": 轮次, "userQuote": "客服原话", "comment": "点评", "rewrite": "更优表达"}
  ],
  "recommendedPhrases": [
    {"patientSays": "患者可能会怎么说", "csReplies": "推荐客服回复话术1"},
    {"patientSays": "患者可能会怎么说", "csReplies": "推荐客服回复话术2"},
    {"patientSays": "患者可能会怎么说", "csReplies": "推荐客服回复话术3"}
  ]
}

如果没有严重违规，violations 为空数组 []。
roundComments 只选取 2-4 个关键轮次进行点评。
recommendedPhrases 提供 3 条适合该场景的高质量客服话术示例。
每条包含 patientSays（患者可能的提问或说法）和 csReplies（推荐客服回复），形成完整的「患者说 → 客服回」对话示范。
totalScore 计算方式：(knowledgeAccuracy*0.25 + medicalCompliance*0.25 + empathy*0.20 + needsDiscovery*0.20 + serviceEtiquette*0.10) 再减去 violations 的扣分。

只返回纯JSON，不要有任何额外文字。`;
}

// ======================== 角色扮演复盘 Prompt ========================
function buildRoleplaySummaryPrompt(scenario, conversationText) {
  const rp = scenario.roleplayConfig || {};
  const guidance = (rp.serviceGuidance || []).join('；');
  return `你是一位口腔客服培训专家。这是一段"患者模拟训练"的对话复盘。学员扮演患者，标准客服扮演前台人员。

场景：${scenario.name}
服务指引：${guidance}

完整对话（患者=学员）：
${conversationText}

请从"患者视角"分析这段对话，评估学员作为患者的沟通能力，并提炼可以从客服回复中学到的知识点。

请严格按以下JSON格式返回（不要markdown标记）：
{
  "totalScore": 数字（0-100，衡量学员提问的完整性、清晰度和沟通有效性）,
  "summary": "总体评价，2-3句话",
  "strengths": [{"content": "学员做得好的地方1"}, {"content": "学员做得好的地方2"}],
  "learningPoints": [
    {"point": "从客服回复中学到的技巧1", "fromCustomerQuote": "对应客服原话"},
    {"point": "从客服回复中学到的技巧2", "fromCustomerQuote": "对应客服原话"}
  ],
  "suggestions": [{"content": "学员可以改进的地方1"}, {"content": "学员可以改进的地方2"}]
}

只返回纯JSON。`;
}

// ======================== API: 健康检查 ========================
app.get('/api/health', (req, res) => {
  res.json(resOk({ status: 'ok', timestamp: nowISO() }));
});

// ======================== API: 配置 ========================
app.post('/api/config/deepseek-key', (req, res) => {
  // MVP 不接受运行时修改密钥
  res.json(resOk({ configured: true }));
});

// ======================== API: 场景列表 ========================
app.get('/api/scenarios', (req, res) => {
  const items = SCENARIOS.map(s => {
    // 查找该场景是否有进行中的会话
    let activeSession = null;
    for (const [id, sess] of sessions) {
      if (sess.scenarioId === s.id && sess.status === 'in_progress') {
        activeSession = { id, scenarioId: s.id, currentRound: sess.currentRound, status: sess.status };
        break;
      }
    }
    return {
      id: s.id, name: s.name, summary: s.summary,
      difficulty: s.difficulty, focus: s.focus,
      patientProfile: s.patientProfile, hiddenConfig: s.hiddenConfig,
      maxRounds: s.maxRounds, sortOrder: s.sortOrder,
      roleplayConfig: s.roleplayConfig,
      suggestedQuestions: [],
      activeSession
    };
  });
  res.json(resOk({ items }));
});

// ======================== API: 角色扮演场景 ========================
app.get('/api/roleplay/scenarios', (req, res) => {
  const items = SCENARIOS.map(s => {
    let activeSession = null;
    for (const [id, sess] of rpSessions) {
      if (sess.scenarioId === s.id && sess.status === 'in_progress') {
        activeSession = { id, scenarioId: s.id, currentRound: sess.currentRound, status: sess.status };
        break;
      }
    }
    return {
      id: s.id, name: s.name, summary: s.summary,
      difficulty: s.difficulty, focus: s.focus,
      patientProfile: s.patientProfile,
      maxRounds: s.maxRounds, sortOrder: s.sortOrder,
      roleplayConfig: s.roleplayConfig,
      suggestedQuestions: (s.roleplayConfig && s.roleplayConfig.suggestedQuestions) || [],
      activeSession
    };
  });
  res.json(resOk({ items }));
});

// ======================== API: 训练会话 ========================

// 创建训练会话
app.post('/api/sessions', (req, res) => {
  const { scenarioId } = req.body || {};
  const scenario = findScenario(scenarioId);
  if (!scenario) return res.status(400).json({ code: 400, message: '场景不存在' });

  const id = genId();
  const session = {
    id, userId: 'demo-user-001', scenarioId: scenario.id,
    scenarioName: scenario.name, status: 'in_progress',
    currentRound: 0, maxRounds: scenario.maxRounds,
    patientState: scenario.hiddenConfig.initialState,
    startedAt: nowISO(), updatedAt: nowISO(), finishedAt: null,
    evaluationStatus: 'not_started', totalScore: null
  };
  sessions.set(id, session);
  messages.set(id, []);

  // 生成患者开场白
  const openingMsg = {
    id: genId(),
    sessionId: id,
    role: 'patient',
    content: scenario.hiddenConfig.opening,
    round: 0,
    clientMessageId: null,
    createdAt: nowISO()
  };
  messages.get(id).push(openingMsg);

  console.log(`[会话] 创建训练: ${id} 场景: ${scenario.name}`);
  res.json(resOk({ session: { id } }));
});

// 获取会话详情
app.get('/api/sessions/:id', (req, res) => {
  const session = sessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });
  const msgs = messages.get(req.params.id) || [];
  res.json(resOk({ session, messages: msgs, pendingMessage: null }));
});

// 发送消息
app.post('/api/sessions/:id/messages', async (req, res) => {
  const session = sessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });
  if (session.status !== 'in_progress') {
    return res.status(400).json({ code: 400, message: '会话已结束' });
  }

  const { clientMessageId, content } = req.body || {};
  if (!content || !content.trim()) {
    return res.status(400).json({ code: 400, message: '消息不能为空' });
  }

  const msgs = messages.get(req.params.id) || [];
  const scenario = findScenario(session.scenarioId);

  // 检查是否重复
  if (clientMessageId && msgs.some(m => m.clientMessageId === clientMessageId)) {
    // 重复消息，直接返回当前状态
    const shouldFinish = session.currentRound >= session.maxRounds;
    return res.json(resOk({ session: { shouldFinish } }));
  }

  // 添加用户消息
  const newRound = session.currentRound + 1;
  const userMsg = {
    id: genId(), sessionId: session.id, role: 'user',
    content: content.trim(), round: newRound,
    clientMessageId: clientMessageId || genId(),
    createdAt: nowISO()
  };
  msgs.push(userMsg);
  session.currentRound = newRound;
  session.updatedAt = nowISO();

  // 生成患者回复
  try {
    const systemPrompt = buildPatientSystem(scenario, session.patientState);
    const history = msgs.map(m => ({
      role: m.role === 'user' ? 'user' : 'assistant',
      content: m.content
    }));
    const llmMessages = [
      { role: 'system', content: systemPrompt },
      ...history
    ];
    const reply = await callLLM(llmMessages, 400);
    const { content: patientContent, emotion } = parseEmotion(reply);

    // 简单的信任/情绪微调——基于回复中是否包含积极/消极情绪
    if (emotion === '信任' || emotion === '平静') {
      session.patientState.trustLevel = Math.min(100, (session.patientState.trustLevel || 50) + 5);
      session.patientState.emotionLevel = Math.min(3, (session.patientState.emotionLevel || 0) + 1);
    } else if (emotion === '不满' || emotion === '抵触' || emotion === '焦虑') {
      session.patientState.trustLevel = Math.max(0, (session.patientState.trustLevel || 50) - 5);
      session.patientState.emotionLevel = Math.max(-3, (session.patientState.emotionLevel || 0) - 1);
    }
    session.patientState.emotion = emotion || session.patientState.emotion;

    const patientMsg = {
      id: genId(), sessionId: session.id, role: 'patient',
      content: patientContent, round: newRound,
      clientMessageId: null, createdAt: nowISO(),
      emotion: emotion || ''
    };
    msgs.push(patientMsg);

    const shouldFinish = session.currentRound >= session.maxRounds;
    res.json(resOk({ session: { shouldFinish }, emotion: emotion || '' }));
  } catch (e) {
    // 回滚用户消息
    msgs.pop();
    session.currentRound--;
    session.updatedAt = nowISO();
    console.error('[会话] 患者回复生成失败:', e.message);
    res.status(500).json({ code: 500, message: '患者回复生成失败，请重试' });
  }
});

// 结束训练
app.post('/api/sessions/:id/finish', async (req, res) => {
  const session = sessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });

  session.status = 'completed';
  session.finishedAt = nowISO();
  session.updatedAt = nowISO();
  session.evaluationStatus = 'generating';

  // 异步生成评估
  generateEvaluation(req.params.id).catch(e => {
    console.error('[评估] 生成失败:', e.message);
    session.evaluationStatus = 'failed';
    const ev = evaluations.get(req.params.id);
    if (ev) ev.status = 'failed';
  });

  res.json(resOk({ session }));
});

// 获取评估
app.get('/api/sessions/:id/evaluation', (req, res) => {
  const session = sessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });

  const ev = evaluations.get(req.params.id);
  if (ev && ev.status === 'ready') {
    return res.json(resOk({ status: 'ready', evaluation: ev.evaluation }));
  }
  if (ev && ev.status === 'failed') {
    return res.json(resOk({ status: 'failed' }));
  }
  if (session.evaluationStatus === 'not_started') {
    // 还没有开始评估，开始生成
    session.evaluationStatus = 'generating';
    generateEvaluation(req.params.id).catch(e => {
      console.error('[评估] 生成失败:', e.message);
      session.evaluationStatus = 'failed';
    });
  }
  res.json(resOk({ status: 'generating' }));
});

// 重新评估
app.post('/api/sessions/:id/evaluation/retry', (req, res) => {
  const session = sessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });

  session.evaluationStatus = 'generating';
  evaluations.delete(req.params.id);
  generateEvaluation(req.params.id).catch(e => {
    console.error('[评估] 重新生成失败:', e.message);
    session.evaluationStatus = 'failed';
  });

  res.json(resOk({ status: 'generating' }));
});

// 重启会话
app.post('/api/sessions/:id/restart', (req, res) => {
  const session = sessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });

  // 标记旧会话为已放弃
  if (session.status === 'in_progress') {
    session.status = 'abandoned';
    session.updatedAt = nowISO();
  }

  // 创建新会话
  const scenario = findScenario(session.scenarioId);
  if (!scenario) return res.status(400).json({ code: 400, message: '场景不存在' });

  const id = genId();
  const newSession = {
    id, userId: 'demo-user-001', scenarioId: scenario.id,
    scenarioName: scenario.name, status: 'in_progress',
    currentRound: 0, maxRounds: scenario.maxRounds,
    patientState: scenario.hiddenConfig.initialState,
    startedAt: nowISO(), updatedAt: nowISO(), finishedAt: null,
    evaluationStatus: 'not_started', totalScore: null
  };
  sessions.set(id, newSession);
  messages.set(id, []);
  const openingMsg = {
    id: genId(), sessionId: id, role: 'patient',
    content: scenario.hiddenConfig.opening, round: 0,
    clientMessageId: null, createdAt: nowISO()
  };
  messages.get(id).push(openingMsg);

  res.json(resOk({ session: { id } }));
});

// 获取训练提示
app.post('/api/sessions/:id/hint', async (req, res) => {
  const session = sessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });
  if (session.status !== 'in_progress') {
    return res.status(400).json({ code: 400, message: '会话已结束，无法获取提示' });
  }

  const msgs = messages.get(req.params.id) || [];
  const hintCount = msgs.filter(m => m.role === 'hint').length;
  if (hintCount >= 3) {
    return res.status(400).json({ code: 400, message: '本场训练提示次数已达上限（3次）' });
  }

  const scenario = findScenario(session.scenarioId);
  if (!scenario) return res.status(400).json({ code: 400, message: '场景不存在' });

  try {
    const history = msgs
      .filter(m => m.role === 'user' || m.role === 'patient')
      .map(m => `${m.role === 'user' ? '客服' : '患者'}：${m.content}`)
      .join('\n');

    const prompt = `你是一位口腔客服培训教练。以下是当前训练的对话记录，请分析对话后给出1条简洁的沟通策略建议，帮助客服更好地应对当前局面。

场景：${scenario.name}
训练重点：${(scenario.focus || []).join('、')}

对话记录：
${history}

请只返回一条50字以内的具体建议（例如"先共情患者对疼痛的担忧，再介绍我们采用的微创技术可以大幅减轻不适"），不要有任何其他内容。`;

    const hint = await callLLM([{ role: 'user', content: prompt }], 200);

    const hintMsg = {
      id: genId(), sessionId: session.id, role: 'hint',
      content: hint.trim(), round: session.currentRound,
      clientMessageId: null, createdAt: nowISO()
    };
    msgs.push(hintMsg);

    res.json(resOk({ hint: hint.trim(), remaining: 3 - hintCount - 1 }));
  } catch (e) {
    console.error('[提示] 生成失败:', e.message);
    res.status(500).json({ code: 500, message: '提示生成失败，请重试' });
  }
});

// 会话列表
app.get('/api/sessions', (req, res) => {
  const limit = parseInt(req.query.limit) || 50;
  const items = Array.from(sessions.values())
    .sort((a, b) => (b.updatedAt || '').localeCompare(a.updatedAt || ''))
    .slice(0, limit)
    .map(s => ({
      id: s.id, scenarioId: s.scenarioId, scenarioName: s.scenarioName,
      status: s.status, currentRound: s.currentRound, maxRounds: s.maxRounds,
      startedAt: s.startedAt, updatedAt: s.updatedAt, finishedAt: s.finishedAt,
      totalScore: s.totalScore, evaluationStatus: s.evaluationStatus
    }));
  res.json(resOk({ items }));
});

// ======================== 评估生成函数 ========================
async function generateEvaluation(sessionId) {
  const session = sessions.get(sessionId);
  if (!session) return;

  session.evaluationStatus = 'generating';
  evaluations.set(sessionId, { status: 'generating', evaluation: null });

  const msgs = messages.get(sessionId) || [];
  const scenario = findScenario(session.scenarioId);
  if (!scenario) throw new Error('场景不存在');

  const conversationText = msgs
    .filter(m => m.round > 0)
    .map(m => {
      const roleLabel = m.role === 'user' ? '客服' : '患者';
      return `[第${m.round}轮·${roleLabel}] ${m.content}`;
    })
    .join('\n\n');

  if (!conversationText.trim()) {
    // 没有有效对话
    const defaultEval = {
      dimensionScores: {
        knowledgeAccuracy: 50, medicalCompliance: 50, empathy: 50,
        needsDiscovery: 50, serviceEtiquette: 50
      },
      totalScore: 50,
      summary: '对话内容不足，无法进行有效评估。',
      strengths: [{ content: '开始了与患者的沟通' }],
      improvements: [{ content: '建议至少完成3轮以上对话以获得有意义的评估' }],
      violations: [],
      roundComments: []
    };
    evaluations.set(sessionId, { status: 'ready', evaluation: defaultEval });
    session.evaluationStatus = 'ready';
    session.totalScore = 50;
    return;
  }

  const prompt = buildEvaluationPrompt(scenario, conversationText);
  try {
    const result = await callLLM([
      { role: 'system', content: '你是口腔客服培训评估专家。请严格按JSON格式输出评估结果，不要包含markdown代码块标记。' },
      { role: 'user', content: prompt }
    ], 2000);

    // 清理可能的 markdown 标记
    let jsonStr = result.trim();
    jsonStr = jsonStr.replace(/^```json\s*/i, '').replace(/^```\s*/i, '').replace(/\s*```$/, '').trim();

    const evaluation = JSON.parse(jsonStr);

    // 确保必要字段存在
    evaluation.dimensionScores = evaluation.dimensionScores || {};
    evaluation.totalScore = Math.min(100, Math.max(0, evaluation.totalScore || 0));
    evaluation.strengths = evaluation.strengths || [];
    evaluation.improvements = evaluation.improvements || [];
    evaluation.violations = (evaluation.violations || []).map((v, i) => ({
      id: `violation-${i}`,
      type: v.type || '违规',
      deduction: v.deduction || 5,
      quote: v.originalQuote || v.quote || '',
      reason: v.reason || '',
      rewrite: v.recommendedRewrite || v.rewrite || ''
    }));
    evaluation.roundComments = (evaluation.roundComments || []).map(rc => ({
      round: rc.round,
      userQuote: rc.userMessage || rc.userQuote || '',
      comment: rc.comment || '',
      rewrite: rc.recommendedRewrite || rc.rewrite || ''
    }));

    evaluations.set(sessionId, { status: 'ready', evaluation });
    session.evaluationStatus = 'ready';
    session.totalScore = evaluation.totalScore;
    console.log(`[评估] 完成: ${sessionId} 得分: ${evaluation.totalScore}`);
  } catch (e) {
    console.error('[评估] 解析失败:', e.message);
    // 返回降级评估
    const fallback = {
      dimensionScores: {
        knowledgeAccuracy: 60, medicalCompliance: 60, empathy: 60,
        needsDiscovery: 60, serviceEtiquette: 60
      },
      totalScore: 60,
      summary: '评估生成过程出现问题，请重试。此为默认评分。',
      strengths: [],
      improvements: [{ content: '请重新生成评估以获取详细反馈' }],
      violations: [],
      roundComments: []
    };
    evaluations.set(sessionId, { status: 'ready', evaluation: fallback });
    session.evaluationStatus = 'ready';
    session.totalScore = 60;
  }
}

// ======================== API: 角色扮演会话 ========================

// 创建角色扮演会话
app.post('/api/roleplay/sessions', (req, res) => {
  const { scenarioId } = req.body || {};
  const scenario = findScenario(scenarioId);
  if (!scenario) return res.status(400).json({ code: 400, message: '场景不存在' });

  const id = genId();
  const session = {
    id, userId: 'demo-user-001', scenarioId: scenario.id,
    scenarioName: scenario.name, status: 'in_progress',
    currentRound: 0, maxRounds: scenario.maxRounds,
    startedAt: nowISO(), updatedAt: nowISO(), finishedAt: null
  };
  rpSessions.set(id, session);
  rpMessages.set(id, []);

  console.log(`[角色扮演] 创建: ${id} 场景: ${scenario.name}`);
  res.json(resOk({ session: { id } }));
});

// 获取角色扮演会话详情
app.get('/api/roleplay/sessions/:id', (req, res) => {
  const session = rpSessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });
  const msgs = rpMessages.get(req.params.id) || [];
  res.json(resOk({ session, messages: msgs, pendingMessage: null }));
});

// 角色扮演发送消息
app.post('/api/roleplay/sessions/:id/messages', async (req, res) => {
  const session = rpSessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });
  if (session.status !== 'in_progress') {
    return res.status(400).json({ code: 400, message: '会话已结束' });
  }

  const { clientMessageId, content } = req.body || {};
  if (!content || !content.trim()) {
    return res.status(400).json({ code: 400, message: '消息不能为空' });
  }

  const msgs = rpMessages.get(req.params.id) || [];
  const scenario = findScenario(session.scenarioId);

  if (clientMessageId && msgs.some(m => m.clientMessageId === clientMessageId)) {
    const shouldFinish = session.currentRound >= session.maxRounds;
    return res.json(resOk({ session: { shouldFinish } }));
  }

  // 添加学员消息（扮演患者）
  const newRound = session.currentRound + 1;
  const userMsg = {
    id: genId(), sessionId: session.id, role: 'learner_patient',
    content: content.trim(), round: newRound,
    learningPoints: [], complianceBoundary: null,
    clientMessageId: clientMessageId || genId(),
    createdAt: nowISO()
  };
  msgs.push(userMsg);
  session.currentRound = newRound;
  session.updatedAt = nowISO();

  // 生成标准客服回复
  try {
    const systemPrompt = buildCustomerSystem(scenario);
    const history = msgs.map(m => ({
      role: m.role === 'learner_patient' ? 'user' : 'assistant',
      content: m.content
    }));
    const llmMessages = [
      { role: 'system', content: systemPrompt },
      ...history
    ];
    const reply = await callLLM(llmMessages, 500);

    // 解析学习要点
    let customerContent = reply;
    const learningPoints = [];
    const lpMatch = reply.match(/<学习要点[:：]([^>]+)>/g);
    if (lpMatch) {
      lpMatch.forEach(tag => {
        const text = tag.replace(/<学习要点[:：]/, '').replace(/>$/, '').trim();
        if (text) learningPoints.push(text);
      });
      customerContent = reply.replace(/<学习要点[:：][^>]+>/g, '').trim();
    }

    const customerMsg = {
      id: genId(), sessionId: session.id, role: 'standard_customer',
      content: customerContent, round: newRound,
      learningPoints, complianceBoundary: null,
      clientMessageId: null, createdAt: nowISO()
    };
    msgs.push(customerMsg);

    const shouldFinish = session.currentRound >= session.maxRounds;
    res.json(resOk({ session: { shouldFinish } }));
  } catch (e) {
    msgs.pop();
    session.currentRound--;
    session.updatedAt = nowISO();
    console.error('[角色扮演] 客服回复生成失败:', e.message);
    res.status(500).json({ code: 500, message: '标准客服回复生成失败，请重试' });
  }
});

// 结束角色扮演
app.post('/api/roleplay/sessions/:id/finish', async (req, res) => {
  const session = rpSessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });

  session.status = 'completed';
  session.finishedAt = nowISO();
  session.updatedAt = nowISO();

  generateRoleplaySummary(req.params.id).catch(e => {
    console.error('[角色扮演复盘] 生成失败:', e.message);
  });

  res.json(resOk({ session }));
});

// 获取角色扮演复盘
app.get('/api/roleplay/sessions/:id/summary', (req, res) => {
  const session = rpSessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });

  const summary = rpSummaries.get(req.params.id);
  if (summary && summary.status === 'ready') {
    return res.json(resOk({ status: 'ready', summary: summary.data }));
  }
  if (summary && summary.status === 'failed') {
    return res.json(resOk({ status: 'failed' }));
  }

  // 开始生成
  rpSummaries.set(req.params.id, { status: 'generating', data: null });
  generateRoleplaySummary(req.params.id).catch(e => {
    console.error('[角色扮演复盘] 生成失败:', e.message);
    rpSummaries.set(req.params.id, { status: 'failed', data: null });
  });

  res.json(resOk({ status: 'generating' }));
});

// 重新生成角色扮演复盘
app.post('/api/roleplay/sessions/:id/summary/retry', (req, res) => {
  const session = rpSessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });

  rpSummaries.set(req.params.id, { status: 'generating', data: null });
  generateRoleplaySummary(req.params.id).catch(e => {
    console.error('[角色扮演复盘] 重新生成失败:', e.message);
    rpSummaries.set(req.params.id, { status: 'failed', data: null });
  });

  res.json(resOk({ status: 'generating' }));
});

// 重启角色扮演
app.post('/api/roleplay/sessions/:id/restart', (req, res) => {
  const session = rpSessions.get(req.params.id);
  if (!session) return res.status(404).json({ code: 404, message: '会话不存在' });

  if (session.status === 'in_progress') {
    session.status = 'abandoned';
    session.updatedAt = nowISO();
  }

  const scenario = findScenario(session.scenarioId);
  if (!scenario) return res.status(400).json({ code: 400, message: '场景不存在' });

  const id = genId();
  const newSession = {
    id, userId: 'demo-user-001', scenarioId: scenario.id,
    scenarioName: scenario.name, status: 'in_progress',
    currentRound: 0, maxRounds: scenario.maxRounds,
    startedAt: nowISO(), updatedAt: nowISO(), finishedAt: null
  };
  rpSessions.set(id, newSession);
  rpMessages.set(id, []);

  res.json(resOk({ session: { id } }));
});

// 角色扮演会话列表
app.get('/api/roleplay/sessions', (req, res) => {
  const limit = parseInt(req.query.limit) || 50;
  const items = Array.from(rpSessions.values())
    .sort((a, b) => (b.updatedAt || '').localeCompare(a.updatedAt || ''))
    .slice(0, limit)
    .map(s => ({
      id: s.id, scenarioId: s.scenarioId, scenarioName: s.scenarioName,
      status: s.status, currentRound: s.currentRound, maxRounds: s.maxRounds,
      startedAt: s.startedAt, updatedAt: s.updatedAt, finishedAt: s.finishedAt,
      totalScore: null
    }));
  res.json(resOk({ items }));
});

// ======================== 角色扮演复盘生成 ========================
async function generateRoleplaySummary(sessionId) {
  const session = rpSessions.get(sessionId);
  if (!session) return;

  rpSummaries.set(sessionId, { status: 'generating', data: null });

  const msgs = rpMessages.get(sessionId) || [];
  const scenario = findScenario(session.scenarioId);
  if (!scenario) throw new Error('场景不存在');

  const conversationText = msgs
    .map(m => {
      const roleLabel = m.role === 'learner_patient' ? '患者（学员）' : '客服';
      return `[第${m.round}轮·${roleLabel}] ${m.content}`;
    })
    .join('\n\n');

  if (!conversationText.trim()) {
    rpSummaries.set(sessionId, {
      status: 'ready',
      data: {
        totalScore: 50, summary: '对话内容不足。',
        strengths: [], learningPoints: [], suggestions: []
      }
    });
    return;
  }

  const prompt = buildRoleplaySummaryPrompt(scenario, conversationText);
  try {
    const result = await callLLM([
      { role: 'system', content: '你是口腔客服培训评估专家。请严格按JSON格式输出复盘结果。' },
      { role: 'user', content: prompt }
    ], 1500);

    let jsonStr = result.trim();
    jsonStr = jsonStr.replace(/^```json\s*/i, '').replace(/^```\s*/i, '').replace(/\s*```$/, '').trim();

    const summary = JSON.parse(jsonStr);
    summary.totalScore = Math.min(100, Math.max(0, summary.totalScore || 0));
    summary.strengths = summary.strengths || [];
    summary.learningPoints = summary.learningPoints || [];
    summary.suggestions = summary.suggestions || [];

    rpSummaries.set(sessionId, { status: 'ready', data: summary });
    console.log(`[角色扮演复盘] 完成: ${sessionId}`);
  } catch (e) {
    console.error('[角色扮演复盘] 解析失败:', e.message);
    rpSummaries.set(sessionId, {
      status: 'ready',
      data: {
        totalScore: 60, summary: '复盘生成出现问题，请重试。',
        strengths: [], learningPoints: [], suggestions: [{ content: '请重新生成复盘以获取详细反馈' }]
      }
    });
  }
}

// ======================== API: 数据面板 ========================
app.get('/api/dashboard/summary', (req, res) => {
  const allSessions = Array.from(sessions.values());

  const totalSessions = allSessions.length;
  const completedSessions = allSessions.filter(s => s.status === 'completed');
  const completedCount = completedSessions.length;
  const totalScoreSum = completedSessions.reduce((sum, s) => sum + (s.totalScore || 0), 0);
  const averageScore = completedCount > 0 ? Math.round(totalScoreSum / completedCount) : 0;

  // 各场景训练次数
  const sceneStatMap = {};
  allSessions.forEach(s => {
    if (!sceneStatMap[s.scenarioId]) {
      sceneStatMap[s.scenarioId] = { scenarioId: s.scenarioId, scenarioName: s.scenarioName, trainingCount: 0 };
    }
    sceneStatMap[s.scenarioId].trainingCount++;
  });
  // 补充未训练过的场景
  SCENARIOS.forEach(sc => {
    if (!sceneStatMap[sc.id]) {
      sceneStatMap[sc.id] = { scenarioId: sc.id, scenarioName: sc.name, trainingCount: 0 };
    }
  });
  const scenarioStats = Object.values(sceneStatMap).sort((a, b) => b.trainingCount - a.trainingCount);

  // 五维平均得分
  const dimKeys = ['knowledgeAccuracy', 'medicalCompliance', 'empathy', 'needsDiscovery', 'serviceEtiquette'];
  const dimSums = {};
  dimKeys.forEach(k => { dimSums[k] = 0; });
  let evalCount = 0;
  completedSessions.forEach(s => {
    const ev = evaluations.get(s.id);
    if (ev && ev.status === 'ready' && ev.evaluation && ev.evaluation.dimensionScores) {
      evalCount++;
      dimKeys.forEach(k => {
        dimSums[k] += ev.evaluation.dimensionScores[k] || 0;
      });
    }
  });
  const dimensionAverages = {};
  dimKeys.forEach(k => {
    dimensionAverages[k] = evalCount > 0 ? Math.round(dimSums[k] / evalCount) : 0;
  });

  // 最近训练
  const recentSessions = allSessions
    .sort((a, b) => (b.updatedAt || '').localeCompare(a.updatedAt || ''))
    .slice(0, 5)
    .map(s => ({
      id: s.id, scenarioName: s.scenarioName, scenarioId: s.scenarioId,
      status: s.status, currentRound: s.currentRound, totalScore: s.totalScore,
      updatedAt: s.updatedAt, evaluation: s.totalScore !== null ? { totalScore: s.totalScore } : null
    }));

  res.json(resOk({
    totalSessions,
    completedSessions: completedCount,
    averageScore,
    scenarioStats,
    dimensionAverages,
    recentSessions
  }));
});

// ======================== API: 话术锦囊 ========================
app.get('/api/phrases', (req, res) => {
  const searchKeyword = (req.query.search || '').trim().toLowerCase();
  const allSessions = Array.from(sessions.values()).filter(s => s.status === 'completed');
  const phraseMap = {}; // key: scenarioId

  allSessions.forEach(s => {
    const ev = evaluations.get(s.id);
    if (!ev || ev.status !== 'ready' || !ev.evaluation) return;
    const phrases = ev.evaluation.recommendedPhrases || [];
    if (phrases.length === 0) return;

    if (!phraseMap[s.scenarioId]) {
      const sc = findScenario(s.scenarioId);
      phraseMap[s.scenarioId] = {
        scenarioId: s.scenarioId,
        scenarioName: sc ? sc.name : s.scenarioName,
        difficulty: sc ? sc.difficulty : 'basic',
        phrases: []
      };
    }

    // 去重：按 csReplies 内容去重
    const existing = new Set(phraseMap[s.scenarioId].phrases.map(p => p.csReplies || p.phrase));
    phrases.forEach(p => {
      const csReplies = p.csReplies || p.phrase || '';
      const patientSays = p.patientSays || '';
      if (!existing.has(csReplies) && csReplies) {
        phraseMap[s.scenarioId].phrases.push({
          patientSays,
          csReplies,
          phrase: csReplies // 兼容旧版
        });
        existing.add(csReplies);
      }
    });
  });

  // 也补充未训练场景的空数据
  SCENARIOS.forEach(sc => {
    if (!phraseMap[sc.id]) {
      phraseMap[sc.id] = {
        scenarioId: sc.id,
        scenarioName: sc.name,
        difficulty: sc.difficulty,
        phrases: []
      };
    }
  });

  let groups = Object.values(phraseMap)
    .sort((a, b) => b.phrases.length - a.phrases.length);

  // 搜索过滤
  if (searchKeyword) {
    groups = groups.map(g => ({
      ...g,
      phrases: g.phrases.filter(p =>
        (p.csReplies || '').toLowerCase().includes(searchKeyword) ||
        (p.patientSays || '').toLowerCase().includes(searchKeyword) ||
        (g.scenarioName || '').toLowerCase().includes(searchKeyword)
      )
    })).filter(g => g.phrases.length > 0);
  }

  const totalPhrases = groups.reduce((sum, g) => sum + g.phrases.length, 0);

  res.json(resOk({ groups, totalPhrases, keyword: searchKeyword || null }));
});

// ======================== API: 首页概览 ========================
app.get('/api/home/overview', (req, res) => {
  const allSessions = Array.from(sessions.values());
  const completedSessions = allSessions.filter(s => s.status === 'completed');
  const completedCount = completedSessions.length;
  const totalScoreSum = completedSessions.reduce((sum, s) => sum + (s.totalScore || 0), 0);
  const averageScore = completedCount > 0 ? Math.round(totalScoreSum / completedCount) : 0;

  // 获取评级
  let level = '新手';
  if (averageScore >= 90) level = '专家';
  else if (averageScore >= 80) level = '优秀';
  else if (averageScore >= 60) level = '良好';

  // 场景完成状态
  const scenarioStatus = SCENARIOS.map(sc => {
    const completed = completedSessions.filter(s => s.scenarioId === sc.id);
    const bestScore = completed.length > 0
      ? Math.max(...completed.map(s => s.totalScore || 0))
      : null;
    const activeSession = allSessions.find(s => s.scenarioId === sc.id && s.status === 'in_progress');
    return {
      scenarioId: sc.id,
      name: sc.name,
      difficulty: sc.difficulty,
      summary: sc.summary,
      completedCount: completed.length,
      bestScore,
      hasActive: !!activeSession
    };
  });

  // 话术锦囊预览（去重，最多 6 条供轮播）
  const previewSeen = new Set();
  const phrasePreview = [];
  for (const s of completedSessions.reverse()) {
    const ev = evaluations.get(s.id);
    if (!ev || ev.status !== 'ready' || !ev.evaluation) continue;
    const phrases = ev.evaluation.recommendedPhrases || [];
    const sc = findScenario(s.scenarioId);
    phrases.forEach(p => {
      const csReplies = p.csReplies || p.phrase || '';
      if (!csReplies || previewSeen.has(csReplies) || phrasePreview.length >= 6) return;
      previewSeen.add(csReplies);
      phrasePreview.push({
        scenarioId: s.scenarioId,
        scenarioName: sc ? sc.name : s.scenarioName,
        patientSays: p.patientSays || '',
        csReplies,
        phrase: csReplies
      });
    });
  }

  res.json(resOk({
    totalSessions: allSessions.length,
    completedCount,
    averageScore,
    level,
    scenarioStatus,
    phrasePreview
  }));
});

// ======================== API: 成长档案 / 能力画像 ========================
app.get('/api/profile', (req, res) => {
  const allSessions = Array.from(sessions.values());
  const completedSessions = allSessions
    .filter(s => s.status === 'completed' && s.totalScore !== null)
    .sort((a, b) => (a.finishedAt || '').localeCompare(b.finishedAt || ''));

  // 维度趋势（最多30条记录）
  const dimKeys = ['knowledgeAccuracy', 'medicalCompliance', 'empathy', 'needsDiscovery', 'serviceEtiquette'];
  const dimNames = { knowledgeAccuracy: '知识准确性', medicalCompliance: '医疗合规', empathy: '同理心', needsDiscovery: '需求挖掘', serviceEtiquette: '服务礼仪' };
  const trend = [];
  const allDimensionScores = { knowledgeAccuracy: [], medicalCompliance: [], empathy: [], needsDiscovery: [], serviceEtiquette: [] };

  completedSessions.forEach(s => {
    const ev = evaluations.get(s.id);
    if (!ev || ev.status !== 'ready' || !ev.evaluation || !ev.evaluation.dimensionScores) return;
    const ds = ev.evaluation.dimensionScores;
    const point = { date: (s.finishedAt || '').slice(0, 10), scenarioName: s.scenarioName, scores: {} };
    dimKeys.forEach(k => {
      point.scores[k] = ds[k] || 0;
      allDimensionScores[k].push(ds[k] || 0);
    });
    point.totalScore = ev.evaluation.totalScore;
    trend.push(point);
  });

  // 最近30天过滤
  const thirtyDaysAgo = new Date();
  thirtyDaysAgo.setDate(thirtyDaysAgo.getDate() - 30);
  const trend30 = trend.filter(t => {
    const d = new Date(t.date);
    return d >= thirtyDaysAgo;
  });

  // 维度均值
  const dimensionAverages = {};
  dimKeys.forEach(k => {
    const scores = allDimensionScores[k];
    dimensionAverages[k] = scores.length > 0 ? Math.round(scores.reduce((a, b) => a + b, 0) / scores.length) : 0;
  });

  // 短板诊断：识别最近3次中持续低于60分的维度
  const recent3 = trend.slice(-3);
  const weaknessDiagnosis = [];
  dimKeys.forEach(k => {
    if (recent3.length >= 2) {
      const below60Count = recent3.filter(t => (t.scores[k] || 0) < 60).length;
      if (below60Count >= recent3.length - 1) {
        weaknessDiagnosis.push({
          dimension: k,
          dimensionName: dimNames[k],
          recentScores: recent3.map(t => t.scores[k]),
          suggestion: getWeaknessSuggestion(k)
        });
      }
    }
  });

  // 能力标签
  const tags = [];
  dimKeys.forEach(k => {
    const avg = dimensionAverages[k];
    if (avg >= 85) {
      const tagMap = { knowledgeAccuracy: '知识专家', medicalCompliance: '合规标兵', empathy: '共情达人', needsDiscovery: '需求洞察者', serviceEtiquette: '服务之星' };
      tags.push({ name: tagMap[k], dimension: k, dimensionName: dimNames[k], score: avg });
    }
  });
  if (tags.length === 0 && Object.values(dimensionAverages).some(v => v > 0)) {
    tags.push({ name: '潜力新星', dimension: '', dimensionName: '综合表现', score: 0 });
  }

  // 错题本数据
  const mistakes = [];
  completedSessions.forEach(s => {
    const ev = evaluations.get(s.id);
    if (!ev || ev.status !== 'ready' || !ev.evaluation) return;
    const e = ev.evaluation;

    // 从 roundComments 提取
    (e.roundComments || []).forEach(rc => {
      if (rc.comment && (rc.comment.includes('待改进') || rc.comment.includes('可优化') || rc.comment.includes('不够'))) {
        mistakes.push({
          id: `mistake-${s.id}-round-${rc.round}`,
          sessionId: s.id,
          scenarioId: s.scenarioId,
          scenarioName: s.scenarioName,
          round: rc.round,
          type: 'round',
          userQuote: rc.userQuote || '',
          comment: rc.comment,
          rewrite: rc.rewrite || '',
          date: s.finishedAt
        });
      }
    });

    // 从 violations 提取
    (e.violations || []).forEach(v => {
      mistakes.push({
        id: `mistake-${s.id}-violation-${v.id || v.type}`,
        sessionId: s.id,
        scenarioId: s.scenarioId,
        scenarioName: s.scenarioName,
        round: v.round || null,
        type: 'violation',
        violationType: v.type,
        userQuote: v.quote || '',
        comment: `${v.type}: ${v.reason || ''}`,
        rewrite: v.rewrite || '',
        deduction: v.deduction,
        date: s.finishedAt
      });
    });
  });

  // 总体评级
  const overallAvg = Object.values(dimensionAverages).reduce((a, b) => a + b, 0) / 5;
  let level = '新手';
  if (overallAvg >= 90) level = '专家';
  else if (overallAvg >= 80) level = '优秀';
  else if (overallAvg >= 60) level = '良好';

  res.json(resOk({
    trend: trend30,
    dimensionAverages,
    weaknessDiagnosis,
    tags,
    mistakes,
    overall: {
      averageScore: Math.round(overallAvg),
      level,
      totalCompleted: completedSessions.length,
      totalMistakes: mistakes.length
    }
  }));
});

function getWeaknessSuggestion(dimension) {
  const map = {
    knowledgeAccuracy: '建议加强口腔专业知识学习，多练习「种植牙基础咨询」场景，确保信息准确无误。',
    medicalCompliance: '建议回顾合规知识点，避免越权判断或不当承诺，多用"建议医生面诊后确定"。',
    empathy: '建议多练习「术后不适咨询」场景，注意先安抚情绪再回答实际问题。',
    needsDiscovery: '建议多练习「种植牙基础咨询」场景，养成先提问再回答的习惯。',
    serviceEtiquette: '建议多练习「与其他诊所比价」场景，学会在不贬低竞品的前提下保持专业礼貌。'
  };
  return map[dimension] || '建议增加该维度的专项训练。';
}

// ======================== API: 我的页（积分/打卡/成就） ========================
app.get('/api/mine/dashboard', (req, res) => {
  // 积分数据（持久化到内存 storage，生产环境应存 DB）
  if (!userPoints) userPoints = { points: 0, checkinDates: [], favorites: [] };
  
  const allSessions = Array.from(sessions.values());
  const completedSessions = allSessions.filter(s => s.status === 'completed' && s.totalScore !== null);
  const totalCompleted = completedSessions.length;
  const passedCount = completedSessions.filter(s => s.totalScore >= 60).length;
  const passRate = totalCompleted > 0 ? Math.round((passedCount / totalCompleted) * 100) : 0;
  const avgScore = totalCompleted > 0 ? Math.round(completedSessions.reduce((a, s) => a + s.totalScore, 0) / totalCompleted) : 0;

  // 等级映射
  const levelMap = [
    { min: 0, name: '见习客服', icon: '🆕' },
    { min: 200, name: '初级客服', icon: '🌱' },
    { min: 500, name: '进阶客服', icon: '📈' },
    { min: 1000, name: '资深客服', icon: '💎' },
    { min: 2000, name: '金牌客服', icon: '👑' },
    { min: 4000, name: '首席客服', icon: '🏆' }
  ];
  const userLevel = [...levelMap].reverse().find(l => userPoints.points >= l.min) || levelMap[0];

  // 本月打卡数据
  const now = new Date();
  const year = now.getFullYear();
  const month = now.getMonth();
  const daysInMonth = new Date(year, month + 1, 0).getDate();
  const firstDayOfWeek = new Date(year, month, 1).getDay();
  const todayStr = `${year}-${String(month + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;

  const calendarDays = [];
  // 补齐上月末尾空白
  for (let i = 0; i < firstDayOfWeek; i++) {
    calendarDays.push({ day: '', checked: false, isToday: false });
  }
  for (let d = 1; d <= daysInMonth; d++) {
    const dateStr = `${year}-${String(month + 1).padStart(2, '0')}-${String(d).padStart(2, '0')}`;
    calendarDays.push({
      day: d,
      checked: userPoints.checkinDates.includes(dateStr),
      isToday: dateStr === todayStr
    });
  }
  const checkinDays = userPoints.checkinDates.length;
  const checkedToday = userPoints.checkinDates.includes(todayStr);
  const streakDays = calcStreak(userPoints.checkinDates, todayStr);

  // 成就徽章
  const badges = [];
  // 里程碑
  if (totalCompleted >= 1) badges.push({ id: 'first', name: '初出茅庐', desc: '完成 1 次训练', icon: '🌟', earned: true });
  else badges.push({ id: 'first', name: '初出茅庐', desc: '完成 1 次训练', icon: '🌟', earned: false });
  if (totalCompleted >= 10) badges.push({ id: 'ten', name: '训练达人', desc: '完成 10 次训练', icon: '🔥', earned: true });
  else badges.push({ id: 'ten', name: '训练达人', desc: '完成 10 次训练', icon: '🔥', earned: false });
  if (totalCompleted >= 50) badges.push({ id: 'fifty', name: '百炼成钢', desc: '完成 50 次训练', icon: '⚡', earned: true });
  else badges.push({ id: 'fifty', name: '百炼成钢', desc: '完成 50 次训练', icon: '⚡', earned: false });
  if (passRate >= 90 && totalCompleted >= 5) badges.push({ id: 'highpass', name: '高分学霸', desc: '通过率 ≥ 90%', icon: '📚', earned: true });
  else badges.push({ id: 'highpass', name: '高分学霸', desc: '通过率 ≥ 90%', icon: '📚', earned: false });
  // 打卡成就
  if (streakDays >= 3) badges.push({ id: 'streak3', name: '连续打卡3天', desc: '坚持 3 天连续打卡', icon: '📅', earned: true });
  else badges.push({ id: 'streak3', name: '连续打卡3天', desc: '坚持 3 天连续打卡', icon: '📅', earned: false });
  if (streakDays >= 7) badges.push({ id: 'streak7', name: '周打卡王', desc: '连续 7 天打卡', icon: '🔔', earned: true });
  else badges.push({ id: 'streak7', name: '周打卡王', desc: '连续 7 天打卡', icon: '🔔', earned: false });
  if (checkinDays >= 30) badges.push({ id: 'checkin30', name: '月度全勤', desc: '累计打卡 30 天', icon: '🏅', earned: true });
  else badges.push({ id: 'checkin30', name: '月度全勤', desc: '累计打卡 30 天', icon: '🏅', earned: false });

  // 排行榜（简单按积分排）
  const leaderboard = [{ rank: 1, name: '你', points: userPoints.points, isMe: true }];

  res.json(resOk({
    points: userPoints.points,
    level: userLevel,
    calendar: {
      year, month: month + 1,
      days: calendarDays,
      checkedToday,
      streakDays,
      checkinDays
    },
    stats: {
      totalCompleted,
      passRate,
      avgScore,
      checkinDays
    },
    badges,
    leaderboard,
    favoritesCount: userPoints.favorites.length
  }));
});

// 打卡
app.post('/api/mine/checkin', (req, res) => {
  if (!userPoints) userPoints = { points: 0, checkinDates: [], favorites: [] };
  const now = new Date();
  const todayStr = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
  if (userPoints.checkinDates.includes(todayStr)) {
    return res.json(resOk({ checked: true, points: userPoints.points, message: '今日已打卡' }));
  }
  userPoints.checkinDates.push(todayStr);
  userPoints.points += 10;
  res.json(resOk({ checked: true, points: userPoints.points, addedPoints: 10, message: '打卡成功 +10 积分' }));
});

// 积分规则
app.get('/api/mine/rules', (req, res) => {
  res.json(resOk({
    rules: [
      { action: '每日打卡', points: '+10', desc: '每天可打卡一次' },
      { action: '完成一次训练', points: '+100', desc: '完成任意模式训练（含通过/未通过）' },
      { action: '训练通过（≥60分）', points: '+50', desc: '额外奖励' },
      { action: '连续打卡3天', points: '+30', desc: '连续奖励' },
      { action: '连续打卡7天', points: '+80', desc: '周奖励' }
    ]
  }));
});

// 收藏话术（简易版）
app.post('/api/mine/favorites', (req, res) => {
  if (!userPoints) userPoints = { points: 0, checkinDates: [], favorites: [] };
  const { phraseId, action } = req.body; // action: 'add' | 'remove'
  if (action === 'add') {
    if (!userPoints.favorites.includes(phraseId)) {
      userPoints.favorites.push(phraseId);
    }
  } else if (action === 'remove') {
    userPoints.favorites = userPoints.favorites.filter(id => id !== phraseId);
  }
  res.json(resOk({ favorites: userPoints.favorites, count: userPoints.favorites.length }));
});

app.get('/api/mine/favorites', (req, res) => {
  if (!userPoints) userPoints = { points: 0, checkinDates: [], favorites: [] };
  res.json(resOk({ favorites: userPoints.favorites, count: userPoints.favorites.length }));
});

// 训练完成时加积分（供训练流程调用）
app.post('/api/mine/add-points', (req, res) => {
  if (!userPoints) userPoints = { points: 0, checkinDates: [], favorites: [] };
  const { amount } = req.body; // 100 基础 + 50 通过奖励
  const addAmount = parseInt(amount) || 100;
  userPoints.points += addAmount;
  res.json(resOk({ points: userPoints.points, addedPoints: addAmount }));
});

function calcStreak(checkinDates, todayStr) {
  if (checkinDates.length === 0) return 0;
  const sorted = [...checkinDates].sort((a, b) => b.localeCompare(a));
  const latest = sorted[0];
  let streak = 0;
  const today = new Date(todayStr);
  const todayTime = today.getTime();
  const latestTime = new Date(latest).getTime();
  const diffDays = Math.round((todayTime - latestTime) / (1000 * 60 * 60 * 24));
  if (diffDays > 1) return 0; // 最近没有打卡
  streak = 1;
  for (let i = 1; i < sorted.length; i++) {
    const prev = new Date(sorted[i - 1]).getTime();
    const curr = new Date(sorted[i]).getTime();
    if (Math.round((prev - curr) / (1000 * 60 * 60 * 24)) === 1) streak++;
    else break;
  }
  return streak;
}

// 全局用户积分（内存存储）
let userPoints = null;

// ======================== 团队模拟数据（多成员） ========================
const TEAM_MEMBERS = [
  { id: 'demo-user-001', name: '张顾问', avatar: '👩‍💼', role: '资深咨询师', joinedAt: '2025-06-01', totalHours: 42, level: '专家' },
  { id: 'user-002', name: '李顾问', avatar: '👨‍💼', role: '初级咨询师', joinedAt: '2026-01-15', totalHours: 18, level: '良好' },
  { id: 'user-003', name: '王顾问', avatar: '👩‍🔬', role: '中级咨询师', joinedAt: '2025-09-10', totalHours: 28, level: '良好' },
  { id: 'user-004', name: '赵顾问', avatar: '👨‍🎓', role: '实习咨询师', joinedAt: '2026-03-01', totalHours: 8, level: '新手' },
  { id: 'user-005', name: '陈顾问', avatar: '👩‍🏫', role: '高级咨询师', joinedAt: '2025-03-20', totalHours: 56, level: '优秀' }
];

// 模拟为每个成员生成训练记录
function initMemberSessions() {
  const memberSessions = {};
  TEAM_MEMBERS.forEach(m => { memberSessions[m.id] = []; });

  // 为 demo-user-001 保留真实 sessions
  memberSessions['demo-user-001'] = Array.from(sessions.values());

  // 为其他成员生成模拟数据
  TEAM_MEMBERS.filter(m => m.id !== 'demo-user-001').forEach(member => {
    const count = 3 + Math.floor(Math.random() * 8); // 3~10 条
    for (let i = 0; i < count; i++) {
      const sc = SCENARIOS[Math.floor(Math.random() * SCENARIOS.length)];
      const score = 40 + Math.floor(Math.random() * 56); // 40~95
      const sessionId = `${member.id}-session-${i}`;
      const session = {
        id: sessionId, userId: member.id, scenarioId: sc.id,
        scenarioName: sc.name, status: 'completed',
        currentRound: 4 + Math.floor(Math.random() * 6),
        maxRounds: sc.maxRounds,
        startedAt: randomDate(2026, 1), updatedAt: randomDate(2026, 6),
        finishedAt: randomDate(2026, 6),
        evaluationStatus: 'ready', totalScore: score
      };
      memberSessions[member.id].push(session);

      // 模拟评估数据
      const dimScores = {
        knowledgeAccuracy: 35 + Math.floor(Math.random() * 60),
        medicalCompliance: 35 + Math.floor(Math.random() * 60),
        empathy: 35 + Math.floor(Math.random() * 60),
        needsDiscovery: 35 + Math.floor(Math.random() * 60),
        serviceEtiquette: 35 + Math.floor(Math.random() * 60)
      };
      evaluations.set(sessionId, {
        status: 'ready',
        evaluation: {
          dimensionScores: dimScores,
          totalScore: score,
          summary: '模拟评估结果',
          strengths: [{ content: '模拟优点' }],
          improvements: [{ content: '模拟改进' }],
          violations: Math.random() > 0.6 ? [{
            id: 'v1', type: '过度承诺', deduction: 5,
            quote: '这个价格肯定是最低的', reason: '价格承诺',
            rewrite: '建议您来院面诊后获取准确报价'
          }] : [],
          roundComments: []
        }
      });
    }

    // 模拟一些进行中的会话
    if (Math.random() > 0.6) {
      const sc = SCENARIOS[Math.floor(Math.random() * SCENARIOS.length)];
      const sessionId = `${member.id}-active`;
      const session = {
        id: sessionId, userId: member.id, scenarioId: sc.id,
        scenarioName: sc.name, status: 'in_progress',
        currentRound: 2 + Math.floor(Math.random() * 3),
        maxRounds: sc.maxRounds,
        startedAt: randomDate(2026, 6), updatedAt: nowISO(),
        finishedAt: null, evaluationStatus: 'not_started', totalScore: null
      };
      memberSessions[member.id].push(session);
      messages.set(sessionId, []);
    }
  });

  return memberSessions;
}

function randomDate(year, maxMonth) {
  const m = 1 + Math.floor(Math.random() * maxMonth);
  const d = 1 + Math.floor(Math.random() * 28);
  const now = new Date();
  const date = new Date(year, m - 1, d, 9 + Math.floor(Math.random() * 12), Math.floor(Math.random() * 60));
  if (date > now) date.setMonth(date.getMonth() - 1);
  return date.toISOString().replace('T', ' ').slice(0, 19);
}

let memberSessionsCache = null;
function getMemberSessions() {
  if (!memberSessionsCache) memberSessionsCache = initMemberSessions();
  return memberSessionsCache;
}

// ======================== 培训计划存储 ========================
const plans = [];
let planIdCounter = 0;

// ======================== API: 增强仪表盘（含时间维度、团队指标） ========================
app.get('/api/dashboard/enhanced', (req, res) => {
  const timeRange = req.query.timeRange || 'all'; // week, month, quarter, all
  const memberSessions = getMemberSessions();

  // 时间过滤
  const now = new Date();
  let timeStart = null;
  if (timeRange === 'week') {
    timeStart = new Date(now);
    timeStart.setDate(timeStart.getDate() - 7);
  } else if (timeRange === 'month') {
    timeStart = new Date(now);
    timeStart.setMonth(timeStart.getMonth() - 1);
  } else if (timeRange === 'quarter') {
    timeStart = new Date(now);
    timeStart.setMonth(timeStart.getMonth() - 3);
  }

  const filterByTime = (s) => {
    if (!timeStart || !s.finishedAt) return true;
    return new Date(s.finishedAt) >= timeStart;
  };

  // 收集所有成员的完成会话
  let allCompleted = [];
  TEAM_MEMBERS.forEach(m => {
    const memberSess = memberSessions[m.id] || [];
    memberSess.filter(s => s.status === 'completed' && s.totalScore !== null && filterByTime(s))
      .forEach(s => allCompleted.push({ ...s, memberName: m.name, memberId: m.id }));
  });

  const totalCompleted = allCompleted.length;
  const totalScoreSum = allCompleted.reduce((sum, s) => sum + s.totalScore, 0);
  const avgScore = totalCompleted > 0 ? Math.round(totalScoreSum / totalCompleted) : 0;
  const passedCount = allCompleted.filter(s => s.totalScore >= 60).length;
  const teamPassRate = totalCompleted > 0 ? Math.round((passedCount / totalCompleted) * 100) : 0;

  // 学员数（有训练记录的）
  const activeMemberIds = new Set(allCompleted.map(s => s.memberId));
  const studentCount = activeMemberIds.size || TEAM_MEMBERS.length;

  // 各场景通过率
  const sceneData = {};
  SCENARIOS.forEach(sc => { sceneData[sc.id] = { scenarioId: sc.id, name: sc.name, total: 0, passed: 0, avgScore: 0, sumScore: 0 }; });
  allCompleted.forEach(s => {
    if (sceneData[s.scenarioId]) {
      sceneData[s.scenarioId].total++;
      sceneData[s.scenarioId].sumScore += s.totalScore;
      if (s.totalScore >= 60) sceneData[s.scenarioId].passed++;
    }
  });
  const scenarioPassRates = Object.values(sceneData).map(d => ({
    ...d,
    passRate: d.total > 0 ? Math.round((d.passed / d.total) * 100) : 0,
    avgScore: d.total > 0 ? Math.round(d.sumScore / d.total) : 0
  })).sort((a, b) => b.total - a.total);

  // 五维平均
  const dimKeys = ['knowledgeAccuracy', 'medicalCompliance', 'empathy', 'needsDiscovery', 'serviceEtiquette'];
  const dimSums = {}; let dimCount = 0;
  dimKeys.forEach(k => { dimSums[k] = 0; });
  allCompleted.forEach(s => {
    const ev = evaluations.get(s.id);
    if (ev && ev.status === 'ready' && ev.evaluation && ev.evaluation.dimensionScores) {
      dimCount++;
      dimKeys.forEach(k => { dimSums[k] += ev.evaluation.dimensionScores[k] || 0; });
    }
  });
  const dimensionAverages = {};
  dimKeys.forEach(k => { dimensionAverages[k] = dimCount > 0 ? Math.round(dimSums[k] / dimCount) : 0; });

  // 总训练次数
  let totalSessions = 0;
  TEAM_MEMBERS.forEach(m => {
    totalSessions += (memberSessions[m.id] || []).filter(s => filterByTime(s)).length;
  });

  // 最近训练
  const recentSessions = allCompleted
    .sort((a, b) => (b.finishedAt || '').localeCompare(a.finishedAt || ''))
    .slice(0, 8);

  res.json(resOk({
    studentCount,
    totalSessions,
    totalCompleted,
    averageScore: avgScore,
    teamPassRate,
    scenarioPassRates,
    dimensionAverages,
    recentSessions: recentSessions.map(s => ({
      id: s.id, memberName: s.memberName, scenarioName: s.scenarioName,
      totalScore: s.totalScore, finishedAt: s.finishedAt
    }))
  }));
});

// ======================== API: 训练趋势 ========================
app.get('/api/dashboard/trend', (req, res) => {
  const timeRange = req.query.timeRange || 'month'; // week, month, quarter
  const memberSessions = getMemberSessions();
  const now = new Date();
  let days = 30;
  let groupFormat = 'day';
  if (timeRange === 'week') { days = 7; }
  else if (timeRange === 'month') { days = 30; }
  else if (timeRange === 'quarter') { days = 90; groupFormat = 'week'; }

  const startDate = new Date(now);
  startDate.setDate(startDate.getDate() - days);
  startDate.setHours(0, 0, 0, 0);

  // 收集数据
  const allCompleted = [];
  TEAM_MEMBERS.forEach(m => {
    (memberSessions[m.id] || [])
      .filter(s => s.status === 'completed' && s.totalScore !== null && s.finishedAt && new Date(s.finishedAt) >= startDate)
      .forEach(s => allCompleted.push(s));
  });

  // 按日分组
  const dateMap = {};
  const currentDate = new Date(startDate);
  while (currentDate <= now) {
    const key = currentDate.toISOString().slice(0, 10);
    dateMap[key] = { date: key, count: 0, totalScore: 0, avgScore: 0 };
    currentDate.setDate(currentDate.getDate() + 1);
  }

  allCompleted.forEach(s => {
    const key = (s.finishedAt || '').slice(0, 10);
    if (dateMap[key]) {
      dateMap[key].count++;
      dateMap[key].totalScore += s.totalScore;
    }
  });

  const trendData = Object.values(dateMap).map(d => ({
    ...d,
    avgScore: d.count > 0 ? Math.round(d.totalScore / d.count) : 0
  }));

  // 如果超过60天，按周聚合
  if (days > 60) {
    const weekData = [];
    for (let i = 0; i < trendData.length; i += 7) {
      const week = trendData.slice(i, i + 7);
      const count = week.reduce((a, b) => a + b.count, 0);
      const totalScore = week.reduce((a, b) => a + b.totalScore, 0);
      weekData.push({
        date: week[0].date + '~' + week[week.length - 1].date,
        count,
        totalScore,
        avgScore: count > 0 ? Math.round(totalScore / count) : 0
      });
    }
    return res.json(resOk({ trend: weekData, timeRange }));
  }

  res.json(resOk({ trend: trendData, timeRange }));
});

// ======================== API: 成员列表 ========================
app.get('/api/members', (req, res) => {
  const sortBy = req.query.sortBy || 'score'; // score, total, hours, name
  const filterLevel = req.query.level || '';
  const memberSessions = getMemberSessions();

  let members = TEAM_MEMBERS.map(m => {
    const memberSess = memberSessions[m.id] || [];
    const completed = memberSess.filter(s => s.status === 'completed' && s.totalScore !== null);
    const totalCount = memberSess.length;
    const sumScore = completed.reduce((a, s) => a + s.totalScore, 0);
    const avgScore = completed.length > 0 ? Math.round(sumScore / completed.length) : 0;
    const passRate = completed.length > 0 ? Math.round((completed.filter(s => s.totalScore >= 60).length / completed.length) * 100) : 0;
    const inProgress = memberSess.filter(s => s.status === 'in_progress');

    return {
      id: m.id, name: m.name, avatar: m.avatar, role: m.role,
      joinedAt: m.joinedAt, totalHours: m.totalHours,
      totalTrainings: totalCount,
      completedTrainings: completed.length,
      avgScore, passRate,
      hasActive: inProgress.length > 0,
      level: avgScore >= 90 ? '专家' : avgScore >= 80 ? '优秀' : avgScore >= 60 ? '良好' : '新手'
    };
  });

  if (filterLevel) {
    members = members.filter(m => m.level === filterLevel);
  }

  switch (sortBy) {
    case 'total': members.sort((a, b) => b.totalTrainings - a.totalTrainings); break;
    case 'hours': members.sort((a, b) => b.totalHours - a.totalHours); break;
    case 'name': members.sort((a, b) => a.name.localeCompare(b.name)); break;
    default: members.sort((a, b) => b.avgScore - a.avgScore); break;
  }

  res.json(resOk({ members }));
});

// ======================== API: 成员详情/能力画像 ========================
app.get('/api/members/:id/profile', (req, res) => {
  const member = TEAM_MEMBERS.find(m => m.id === req.params.id);
  if (!member) return res.status(404).json({ code: 404, message: '成员不存在' });

  const memberSessions = getMemberSessions();
  const allSess = memberSessions[member.id] || [];
  const completed = allSess
    .filter(s => s.status === 'completed' && s.totalScore !== null)
    .sort((a, b) => (a.finishedAt || '').localeCompare(b.finishedAt || ''));

  // 维度分析
  const dimKeys = ['knowledgeAccuracy', 'medicalCompliance', 'empathy', 'needsDiscovery', 'serviceEtiquette'];
  const dimNames = { knowledgeAccuracy: '知识准确性', medicalCompliance: '医疗合规', empathy: '同理心', needsDiscovery: '需求挖掘', serviceEtiquette: '服务礼仪' };
  const dimScores = { knowledgeAccuracy: [], medicalCompliance: [], empathy: [], needsDiscovery: [], serviceEtiquette: [] };
  const trend = [];

  completed.forEach(s => {
    const ev = evaluations.get(s.id);
    if (!ev || ev.status !== 'ready' || !ev.evaluation || !ev.evaluation.dimensionScores) return;
    trend.push({
      date: (s.finishedAt || '').slice(0, 10),
      scenarioName: s.scenarioName,
      totalScore: s.totalScore,
      scores: ev.evaluation.dimensionScores
    });
    dimKeys.forEach(k => dimScores[k].push(ev.evaluation.dimensionScores[k] || 0));
  });

  const dimensionAverages = {};
  dimKeys.forEach(k => {
    const arr = dimScores[k];
    dimensionAverages[k] = arr.length > 0 ? Math.round(arr.reduce((a, b) => a + b, 0) / arr.length) : 0;
  });

  // 短板诊断
  const warnings = [];
  const recent5 = trend.slice(-5);
  dimKeys.forEach(k => {
    if (recent5.length >= 3) {
      const below60 = recent5.filter(t => (t.scores[k] || 0) < 60).length;
      if (below60 >= 2) {
        warnings.push({
          dimension: k, dimensionName: dimNames[k],
          severity: below60 >= recent5.length - 1 ? 'high' : 'medium',
          recentScores: recent5.map(t => t.scores[k]),
          suggestion: getWeaknessSuggestion(k)
        });
      }
    }
  });

  // 成长曲线（按时间排序的总分变化）
  const growthCurve = trend.map(t => ({ date: t.date, totalScore: t.totalScore }));

  // 通过率
  const passRate = completed.length > 0
    ? Math.round((completed.filter(s => s.totalScore >= 60).length / completed.length) * 100) : 0;

  const avgScore = completed.length > 0
    ? Math.round(completed.reduce((a, s) => a + s.totalScore, 0) / completed.length) : 0;

  res.json(resOk({
    member: { ...member, avgScore, passRate },
    dimensionAverages,
    trend,
    growthCurve: growthCurve.slice(-20),
    warnings,
    totalTrainings: allSess.length,
    completedTrainings: completed.length
  }));
});

// ======================== API: 薄弱项预警（团队 + 个人） ========================
app.get('/api/warnings', (req, res) => {
  const memberSessions = getMemberSessions();
  const dimKeys = ['knowledgeAccuracy', 'medicalCompliance', 'empathy', 'needsDiscovery', 'serviceEtiquette'];
  const dimNames = { knowledgeAccuracy: '知识准确性', medicalCompliance: '医疗合规', empathy: '同理心', needsDiscovery: '需求挖掘', serviceEtiquette: '服务礼仪' };

  const teamWarnings = [];
  const memberWarnings = [];

  // 团队维度预警
  const teamDimScores = {};
  dimKeys.forEach(k => { teamDimScores[k] = []; });
  TEAM_MEMBERS.forEach(m => {
    (memberSessions[m.id] || [])
      .filter(s => s.status === 'completed' && s.totalScore !== null)
      .slice(-5)
      .forEach(s => {
        const ev = evaluations.get(s.id);
        if (ev && ev.status === 'ready' && ev.evaluation && ev.evaluation.dimensionScores) {
          dimKeys.forEach(k => teamDimScores[k].push(ev.evaluation.dimensionScores[k] || 0));
        }
      });
  });

  dimKeys.forEach(k => {
    const arr = teamDimScores[k];
    const avg = arr.length > 0 ? arr.reduce((a, b) => a + b, 0) / arr.length : 0;
    if (avg < 60) {
      teamWarnings.push({ dimension: k, dimensionName: dimNames[k], avgScore: Math.round(avg), severity: 'high', type: 'team' });
    } else if (avg < 70) {
      teamWarnings.push({ dimension: k, dimensionName: dimNames[k], avgScore: Math.round(avg), severity: 'medium', type: 'team' });
    }
  });

  // 个人预警
  TEAM_MEMBERS.forEach(m => {
    const completed = (memberSessions[m.id] || [])
      .filter(s => s.status === 'completed' && s.totalScore !== null)
      .sort((a, b) => (a.finishedAt || '').localeCompare(b.finishedAt || ''));
    if (completed.length < 3) return;

    const recent3 = completed.slice(-3);
    const memberDimScores = {};
    dimKeys.forEach(k => { memberDimScores[k] = []; });
    recent3.forEach(s => {
      const ev = evaluations.get(s.id);
      if (ev && ev.status === 'ready' && ev.evaluation && ev.evaluation.dimensionScores) {
        dimKeys.forEach(k => memberDimScores[k].push(ev.evaluation.dimensionScores[k] || 0));
      }
    });

    const memberAlerts = [];
    dimKeys.forEach(k => {
      const scores = memberDimScores[k];
      if (scores.length >= 2 && scores.every(v => v < 60)) {
        memberAlerts.push({ dimension: k, dimensionName: dimNames[k], scores, avgScore: Math.round(scores.reduce((a, b) => a + b, 0) / scores.length) });
      }
    });

    if (memberAlerts.length > 0) {
      const avgAll = completed.reduce((sum, s) => sum + s.totalScore, 0) / completed.length;
      memberWarnings.push({
        memberId: m.id, memberName: m.name, memberAvatar: m.avatar,
        avgScore: Math.round(avgAll),
        alerts: memberAlerts
      });
    }
  });

  res.json(resOk({ teamWarnings, memberWarnings }));
});

// ======================== API: 培训计划 ========================
app.get('/api/plans', (req, res) => {
  const statusFilter = req.query.status || '';
  let list = [...plans];
  if (statusFilter === 'active') list = list.filter(p => p.status === 'active');
  else if (statusFilter === 'completed') list = list.filter(p => p.status === 'completed');
  list.sort((a, b) => (b.createdAt || '').localeCompare(a.createdAt || ''));
  res.json(resOk({ plans: list }));
});

app.post('/api/plans', (req, res) => {
  const { title, description, targetMemberIds, startDate, endDate, focusDimensions } = req.body || {};
  if (!title || !targetMemberIds || targetMemberIds.length === 0) {
    return res.status(400).json({ code: 400, message: '标题和成员不能为空' });
  }
  const planId = `plan-${++planIdCounter}`;
  const plan = {
    id: planId, title, description: description || '',
    targetMemberIds, startDate: startDate || nowISO().slice(0, 10),
    endDate: endDate || '',
    focusDimensions: focusDimensions || [],
    status: 'active', createdAt: nowISO(),
    taskCount: 0, completedTaskCount: 0,
    tasks: []
  };
  plans.push(plan);
  res.json(resOk({ plan }));
});

app.put('/api/plans/:id', (req, res) => {
  const plan = plans.find(p => p.id === req.params.id);
  if (!plan) return res.status(404).json({ code: 404, message: '计划不存在' });
  const { status, title, description } = req.body || {};
  if (status) plan.status = status;
  if (title) plan.title = title;
  if (description !== undefined) plan.description = description;
  res.json(resOk({ plan }));
});

app.delete('/api/plans/:id', (req, res) => {
  const idx = plans.findIndex(p => p.id === req.params.id);
  if (idx === -1) return res.status(404).json({ code: 404, message: '计划不存在' });
  plans.splice(idx, 1);
  res.json(resOk({ deleted: true }));
});

// 任务管理
app.get('/api/plans/:id/tasks', (req, res) => {
  const plan = plans.find(p => p.id === req.params.id);
  if (!plan) return res.status(404).json({ code: 404, message: '计划不存在' });
  res.json(resOk({ tasks: plan.tasks || [] }));
});

app.post('/api/plans/:id/tasks', (req, res) => {
  const plan = plans.find(p => p.id === req.params.id);
  if (!plan) return res.status(404).json({ code: 404, message: '计划不存在' });
  const { title, scenarioId, assigneeId, dueDate } = req.body || {};
  if (!title || !assigneeId) {
    return res.status(400).json({ code: 400, message: '标题和负责人不能为空' });
  }
  const task = {
    id: `task-${Date.now()}`, planId: plan.id,
    title, scenarioId: scenarioId || '',
    assigneeId, dueDate: dueDate || '',
    status: 'pending', createdAt: nowISO(),
    completedAt: null, resultNote: ''
  };
  plan.tasks = plan.tasks || [];
  plan.tasks.push(task);
  plan.taskCount = plan.tasks.length;
  plan.completedTaskCount = plan.tasks.filter(t => t.status === 'done').length;
  res.json(resOk({ task }));
});

app.put('/api/plans/:planId/tasks/:taskId', (req, res) => {
  const plan = plans.find(p => p.id === req.params.planId);
  if (!plan) return res.status(404).json({ code: 404, message: '计划不存在' });
  const task = (plan.tasks || []).find(t => t.id === req.params.taskId);
  if (!task) return res.status(404).json({ code: 404, message: '任务不存在' });
  const { status, resultNote } = req.body || {};
  if (status) {
    task.status = status;
    if (status === 'done') task.completedAt = nowISO();
  }
  if (resultNote !== undefined) task.resultNote = resultNote;
  plan.completedTaskCount = plan.tasks.filter(t => t.status === 'done').length;
  if (plan.completedTaskCount === plan.taskCount && plan.tasks.length > 0) {
    plan.status = 'completed';
  }
  res.json(resOk({ task }));
});

// ======================== API: 高频违规词 ========================
app.get('/api/violations/words', (req, res) => {
  const memberSessions = getMemberSessions();
  const wordFreq = {};
  const violationTypeFreq = {};

  const allSessions = [];
  TEAM_MEMBERS.forEach(m => {
    (memberSessions[m.id] || [])
      .filter(s => s.status === 'completed')
      .forEach(s => allSessions.push(s));
  });

  allSessions.forEach(s => {
    const ev = evaluations.get(s.id);
    if (!ev || ev.status !== 'ready' || !ev.evaluation || !ev.evaluation.violations) return;
    ev.evaluation.violations.forEach(v => {
      const type = v.type || '未知';
      violationTypeFreq[type] = (violationTypeFreq[type] || 0) + 1;

      if (v.quote) {
        const words = v.quote.split(/[\s，。！？、；：""''（）【】《》\.,!?;:\s]+/).filter(w => w.length >= 2);
        words.forEach(w => { wordFreq[w] = (wordFreq[w] || 0) + 1; });
      }
    });
  });

  const sortedWords = Object.entries(wordFreq)
    .sort((a, b) => b[1] - a[1])
    .slice(0, 20)
    .map(([word, count]) => ({ word, count }));

  const sortedTypes = Object.entries(violationTypeFreq)
    .sort((a, b) => b[1] - a[1])
    .map(([type, count]) => ({ type, count }));

  res.json(resOk({ words: sortedWords, types: sortedTypes }));
});

// ======================== API: 团队排行榜 ========================
app.get('/api/leaderboard', (req, res) => {
  const sortBy = req.query.sortBy || 'score'; // score, count, hours, pass
  const memberSessions = getMemberSessions();

  let leaderboard = TEAM_MEMBERS.map(m => {
    const sess = memberSessions[m.id] || [];
    const completed = sess.filter(s => s.status === 'completed' && s.totalScore !== null);
    const avgScore = completed.length > 0 ? Math.round(completed.reduce((a, s) => a + s.totalScore, 0) / completed.length) : 0;
    const passCount = completed.filter(s => s.totalScore >= 60).length;
    const passRate = completed.length > 0 ? Math.round((passCount / completed.length) * 100) : 0;
    return {
      id: m.id, name: m.name, avatar: m.avatar, role: m.role,
      completedCount: completed.length, avgScore, passRate, totalHours: m.totalHours
    };
  });

  switch (sortBy) {
    case 'count': leaderboard.sort((a, b) => b.completedCount - a.completedCount); break;
    case 'hours': leaderboard.sort((a, b) => b.totalHours - a.totalHours); break;
    case 'pass': leaderboard.sort((a, b) => b.passRate - a.passRate); break;
    default: leaderboard.sort((a, b) => b.avgScore - a.avgScore); break;
  }

  leaderboard = leaderboard.map((item, idx) => ({ ...item, rank: idx + 1 }));
  res.json(resOk({ leaderboard }));
});

// ======================== API: 报表导出 ========================
app.get('/api/export/report', (req, res) => {
  const format = req.query.format || 'csv';
  const memberSessions = getMemberSessions();
  const dimNames = { knowledgeAccuracy: '知识准确性', medicalCompliance: '医疗合规', empathy: '同理心', needsDiscovery: '需求挖掘', serviceEtiquette: '服务礼仪' };

  const rows = [];
  rows.push(['成员', '场景', '得分', '状态', '完成时间', '知识准确性', '医疗合规', '同理心', '需求挖掘', '服务礼仪']);

  TEAM_MEMBERS.forEach(m => {
    (memberSessions[m.id] || [])
      .filter(s => s.status === 'completed' && s.totalScore !== null)
      .forEach(s => {
        const ev = evaluations.get(s.id);
        const dims = (ev && ev.status === 'ready' && ev.evaluation && ev.evaluation.dimensionScores) || {};
        rows.push([
          m.name, s.scenarioName, s.totalScore,
          s.totalScore >= 60 ? '通过' : '未通过',
          (s.finishedAt || '').slice(0, 10),
          dims.knowledgeAccuracy || 0, dims.medicalCompliance || 0, dims.empathy || 0,
          dims.needsDiscovery || 0, dims.serviceEtiquette || 0
        ]);
      });
  });

  const csv = rows.map(r => r.map(c => `"${String(c).replace(/"/g, '""')}"`).join(',')).join('\n');
  const bom = '\uFEFF';
  res.setHeader('Content-Type', 'text/csv; charset=utf-8');
  res.setHeader('Content-Disposition', 'attachment; filename=training_report.csv');
  res.send(bom + csv);
});

// ======================== 启动服务 ========================
const PORT = process.env.PORT || 8080;
app.listen(PORT, () => {
  console.log(`✅ 口腔客服智能陪练 API 已启动 → http://localhost:${PORT}`);
  console.log(`   模型: ${MODEL}  |  场景数: ${SCENARIOS.length}`);
});
