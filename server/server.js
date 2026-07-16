require('dotenv').config();
const express = require('express');
const cors = require('cors');
const OpenAI = require('openai').default || require('openai');

const app = express();
app.use(cors());
app.use(express.json());

// 请求日志
app.use((req, res, next) => {
  console.log(`[${new Date().toLocaleTimeString()}] ${req.method} ${req.url}`);
  next();
});

const client = new OpenAI({
  apiKey: process.env.MOONSHOT_API_KEY,
  baseURL: 'https://api.moonshot.cn/v1',
  timeout: 30000,
  maxRetries: 0
});
const MODEL = process.env.MOONSHOT_MODEL || 'kimi-k2.6';

/**
 * 构造「患者角色」的 system prompt
 */
function buildPatientSystem(scenarioName, profile) {
  const p = profile || {};
  const traits = [p.age, p.description, p.personality, p.sensitivity ? `敏感度：${p.sensitivity}` : '']
    .filter(Boolean)
    .join('，');
  return `你正在参与牙科诊所的口腔咨询师沟通训练，扮演一位前来咨询的模拟患者。
场景主题：${scenarioName}
患者画像：${traits}

要求：
1. 只以患者口吻说话，不要解释、不要替医生说话、不要输出说明性文字。
2. 体现真实患者的顾虑、犹豫、比价心理或情绪反应。
3. 每次回复 1~3 句话，自然口语化，语气贴近真实对话。
4. 回复末尾用尖括号附上一个简短的情绪标签，例如：<情绪：将信将疑>。
   可选标签：平静、疑虑、焦虑、不满、将信将疑、信任、犹豫、抵触。
5. 不使用 Markdown、列表或标题。`;
}

/**
 * 从文本中提取情绪标签，返回 { content, emotion }
 */
function parseEmotion(text) {
  const m = text.match(/<情绪[:：]([^>]+)>/);
  if (m) return { content: text.replace(m[0], '').trim(), emotion: m[1].trim() };
  return { content: text.trim(), emotion: '' };
}

async function callKimi(messages) {
  console.log('[callKimi] 请求中, 消息数:', messages.length);
  const start = Date.now();
  try {
    const resp = await client.chat.completions.create({
      model: MODEL,
      messages,
      max_tokens: 800,
      thinking: { type: 'disabled' }
    });
    const content = resp.choices[0].message.content || '';
    console.log('[callKimi] 完成, 耗时:', Date.now() - start, 'ms, 内容长度:', content.length);
    return content;
  } catch (e) {
    console.error('[callKimi] 失败, 耗时:', Date.now() - start, 'ms, 错误:', e.message);
    throw e;
  }
}

// ======================== 接口 ========================

// 1) 生成患者开场白
app.post('/api/llm/opening', async (req, res) => {
  try {
    const { scenarioName, patientProfile } = req.body;
    const messages = [
      { role: 'system', content: buildPatientSystem(scenarioName, patientProfile) },
      { role: 'user', content: '请说出你今天来诊所想咨询的第一个问题，作为开场白，一句话即可。' }
    ];
    const { content, emotion } = parseEmotion(await callKimi(messages));
    res.json({ code: 0, data: { content, emotion } });
  } catch (e) {
    console.error('[opening] 错误:', e.message);
    res.status(500).json({ code: 500, message: '生成开场白失败' });
  }
});

// 2) 患者对话回复
app.post('/api/llm/chat', async (req, res) => {
  try {
    const { scenarioName, patientProfile, history = [], userMessage } = req.body;
    const messages = [{ role: 'system', content: buildPatientSystem(scenarioName, patientProfile) }];
    for (const m of history) {
      messages.push({
        role: m.role === 'user' ? 'user' : 'assistant',
        content: m.content
      });
    }
    messages.push({ role: 'user', content: userMessage });
    const { content, emotion } = parseEmotion(await callKimi(messages));
    res.json({ code: 0, data: { content, emotion } });
  } catch (e) {
    console.error('[chat] 错误:', e.message);
    res.status(500).json({ code: 500, message: '生成回复失败' });
  }
});

// 3) 沟通提示
app.post('/api/llm/hint', async (req, res) => {
  try {
    const { scenarioName, history = [] } = req.body;
    const messages = [
      {
        role: 'system',
        content: `你是牙科沟通训练教练，辅导一位口腔咨询师。场景：${scenarioName}。请根据对话历史，给咨询师一句具体、可操作的沟通改进建议（不超过 2 句）。`
      }
    ];
    for (const m of history) {
      messages.push({
        role: m.role === 'user' ? 'user' : 'assistant',
        content: m.content
      });
    }
    const hint = (await callKimi(messages)).trim();
    res.json({ code: 0, data: { hint } });
  } catch (e) {
    console.error('[hint] 错误:', e.message);
    res.status(500).json({ code: 500, message: '生成提示失败' });
  }
});

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`✅ LLM 代理已启动 → http://localhost:${PORT}`);
  console.log(`   模型: ${MODEL}  |  接口: /api/llm/opening /chat /hint`);
});
