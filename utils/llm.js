const request = require('../static/api/request.js');

/**
 * 大模型对话服务
 * 调用后端代理接口，由后端负责构造 Prompt 并调用大模型 API
 *
 * 后端需实现的接口：
 *   POST /api/llm/chat
 *   入参: { scenarioId, scenarioName, patientProfile, history, userMessage }
 *   返回: { content: "患者回复文本", emotion: "情绪标签" }
 *
 *   POST /api/llm/opening
 *   入参: { scenarioId, scenarioName, patientProfile }
 *   返回: { content: "患者开场白文本", emotion: "情绪标签" }
 */

/**
 * 生成患者开场白
 * @param {Object} scenario - 场景数据（含 id, name, patientProfile）
 * @returns {Promise<{content: string, emotion: string}>}
 */
function generateOpening(scenario) {
  return request
    .post('/llm/opening', {
      scenarioId: scenario.id,
      scenarioName: scenario.name,
      patientProfile: scenario.patientProfile
    })
    .then(data => ({
      content: data.content || '',
      emotion: data.emotion || scenario.patientProfile?.initialEmotion || ''
    }));
}

/**
 * 生成患者回复
 * @param {Object} params
 * @param {string} params.scenarioId        - 场景 ID
 * @param {string} params.scenarioName      - 场景名称
 * @param {Object} params.patientProfile    - 患者画像 { age, description, personality, sensitivity }
 * @param {Array}  params.history           - 历史对话 [{ role:'user'|'patient', content }]
 * @param {string} params.userMessage       - 用户最新发言
 * @param {number} params.currentRound      - 当前轮数
 * @param {number} params.maxRounds         - 总轮数
 * @returns {Promise<{content: string, emotion: string}>}
 */
function generateReply({ scenarioId, scenarioName, patientProfile, history, userMessage, currentRound, maxRounds }) {
  return request
    .post('/llm/chat', {
      scenarioId,
      scenarioName,
      patientProfile,
      history: (history || []).slice(-20),
      userMessage,
      currentRound,
      maxRounds
    })
    .then(data => ({
      content: data.content || '嗯，你说得也有道理…',
      emotion: data.emotion || ''
    }));
}

/**
 * 生成训练提示
 * @param {Object} params
 * @returns {Promise<string>}
 */
function generateHint({ scenarioId, scenarioName, patientProfile, history }) {
  return request
    .post('/llm/hint', {
      scenarioId,
      scenarioName,
      patientProfile,
      history: (history || []).slice(-10)
    })
    .then(data => data.hint || '尝试用同理心回应患者的情绪，先认可TA的感受再给出专业建议。');
}

module.exports = { generateOpening, generateReply, generateHint };
