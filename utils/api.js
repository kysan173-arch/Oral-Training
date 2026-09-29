const { getApiBaseUrl } = require('./config.js');
const { DEFAULT_REQUEST_TIMEOUT, MODEL_REQUEST_TIMEOUT } = require('./request-policy.js');

const TOKEN_KEY = 'oralTrainingAccessToken';
const USER_KEY = 'oralTrainingUser';
let loginPromise = null;

const rawRequest = (path, options = {}) => new Promise((resolve, reject) => {
  let baseUrl = '';
  try {
    baseUrl = getApiBaseUrl();
  } catch (error) {
    reject(error);
    return;
  }
  wx.request({
    url: `${baseUrl}${path}`,
    method: options.method || 'GET',
    data: options.data,
    timeout: options.timeout === undefined ? DEFAULT_REQUEST_TIMEOUT : options.timeout,
    header: Object.assign({ 'content-type': 'application/json' }, options.header || {}, options.token
      ? { Authorization: `Bearer ${options.token}` }
      : {}),
    success: response => {
      const payload = response.data || {};
      const acceptedStatus = response.statusCode >= 200 && response.statusCode < 300;
      const acceptedUnreadyHealth = options.acceptUnreadyHealth === true &&
        response.statusCode === 503;
      if ((acceptedStatus || acceptedUnreadyHealth) && payload.code === 0) {
        resolve(payload.data);
        return;
      }
      const error = new Error(payload.message || '服务请求失败');
      error.code = payload.code || 'NETWORK_ERROR';
      error.statusCode = response.statusCode;
      reject(error);
    },
    fail: error => {
      const requestError = new Error(error.errMsg || '无法连接后端服务');
      requestError.code = 'NETWORK_ERROR';
      reject(requestError);
    }
  });
});

const clearAuthentication = () => {
  wx.removeStorageSync(TOKEN_KEY);
  wx.removeStorageSync(USER_KEY);
};

const login = () => new Promise((resolve, reject) => {
  wx.login({
    success: result => {
      if (!result.code) {
        reject(Object.assign(new Error('微信登录未返回有效凭证'), { code: 'WECHAT_LOGIN_FAILED' }));
        return;
      }
      rawRequest('/auth/wechat', { method: 'POST', data: { code: result.code } })
        .then(data => {
          wx.setStorageSync(TOKEN_KEY, data.accessToken);
          wx.setStorageSync(USER_KEY, data.user);
          resolve(data.accessToken);
        }).catch(reject);
    },
    fail: error => reject(Object.assign(new Error(error.errMsg || '微信登录失败'), {
      code: 'WECHAT_LOGIN_FAILED'
    }))
  });
});

const ensureAuthenticated = (force = false) => {
  const existing = force ? '' : wx.getStorageSync(TOKEN_KEY);
  if (existing) return Promise.resolve(existing);
  if (!loginPromise) {
    loginPromise = login().finally(() => { loginPromise = null; });
  }
  return loginPromise;
};

const request = (path, options = {}, retried = false) => {
  if (options.public) return rawRequest(path, options);
  return ensureAuthenticated().then(token => rawRequest(path, Object.assign({}, options, { token })))
    .catch(error => {
      if (!retried && (error.code === 'AUTH_EXPIRED' || error.code === 'AUTH_INVALID' || error.code === 'AUTH_REQUIRED')) {
        clearAuthentication();
        return ensureAuthenticated(true).then(() => request(path, options, true));
      }
      throw error;
    });
};

const query = values => Object.keys(values)
  .filter(key => values[key] !== undefined && values[key] !== null && values[key] !== '')
  .map(key => `${encodeURIComponent(key)}=${encodeURIComponent(values[key])}`)
  .join('&');

const formatScore = value => {
  if (value === null || value === undefined || value === '') return '暂无评分';
  const score = Number(value);
  return Number.isFinite(score) ? Number(score.toFixed(1)) : '暂无评分';
};

/* ── 浮层文案：toast 不适合承载长句 ──
   wx.showToast 的 title 有硬性宽度上限，超出部分直接截断（就是「提示显示不全」）：
     · icon 为 success / error / loading → 只有一行，约 7 个汉字
     · icon: 'none'                      → 可折两行，约 14 个汉字（官方文档原话）
   两行已经是很紧的余量，而且长句折行后断句难看；所以超过 TOAST_MAX_WIDTH
   就不用 toast。

   长文案走 showModal 而不是 showLoading —— 这是踩过坑才定下来的：
   showLoading 是**阻断态**，且与 showToast 共用同一个居中浮层。若用它承载
   「至少完成 1 轮对话」这类提示，一旦之后有人再叠一条 toast，loading 就会被顶掉；
   更糟的是 mask:true 会吃掉点击，用户又没有任何关闭它的手段（提示没有回调），
   表现就是「卡住、没反应」。showModal 自带「知道了」按钮，永远关得掉。 */
const TOAST_MAX_WIDTH = 13;
const TOAST_MAX_WIDTH_WITH_ICON = 8;

const textWidth = text => [...String(text === null || text === undefined ? '' : text)]
  .reduce((sum, char) => sum + (/[\u4e00-\u9fa5\u3000-\u303f\uff00-\uffef]/.test(char) ? 2 : 1), 0);

/* 自适应：短文案走 toast（有图标、自动消失），长文案升级为「知道了」弹窗。
   返回 true 表示是 toast（会自己消失），false 表示弹了 modal（需用户确认）。 */
const showCenterNotice = (options = {}) => {
  const { title = '', icon = 'none', mask = true, duration = 1800 } = options;
  const limit = icon === 'none' ? TOAST_MAX_WIDTH : TOAST_MAX_WIDTH_WITH_ICON;
  if (textWidth(title) <= limit) {
    wx.showToast({ title, icon, duration });
    return true;
  }
  wx.showModal({ title: '提示', content: title, showCancel: false, confirmText: '知道了' });
  return false;
};

/* 长提示专用：挂住直到调用方 hideLoading，适合「已切为…，即将跳转」这类场景 */
const showNotice = (title, options = {}) => wx.showLoading({ title, mask: options.mask !== false });

const hideNotice = () => wx.hideLoading();

/* 演示账号切换：换令牌与用户缓存，供 switchRole / switchLearner 共用 */
const persistIdentity = data => {
  if (data && data.accessToken && data.user) {
    wx.setStorageSync(TOKEN_KEY, data.accessToken);
    wx.setStorageSync(USER_KEY, data.user);
  }
  return data;
};

module.exports = {
  formatScore,
  showCenterNotice,
  showNotice,
  hideNotice,
  textWidth,
  ensureAuthenticated,
  clearAuthentication,
  getCurrentUser: () => wx.getStorageSync(USER_KEY) || null,
  getHealth: () => request('/health', { public: true, acceptUnreadyHealth: true }),

  // ── 训练（学员端） ──
  getScenarios: () => request('/scenarios'),
  /* tier 选填：'advanced' 时按场景的难度档位覆盖患者初始状态（迁移 026）。
     不传就**不要塞空串**——后端把「缺省」当 standard，空串反而会被判成非法值。 */
  createSession: (scenarioId, customPatientProfile, tier) => request('/sessions', {
    method: 'POST',
    data: tier ? { scenarioId, customPatientProfile, tier } : { scenarioId, customPatientProfile }
  }),
  restartSession: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/restart`, { method: 'POST', data: {} }),
  getSession: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}`),
  sendMessage: (sessionId, clientMessageId, content) => request(`/sessions/${encodeURIComponent(sessionId)}/messages`, {
    method: 'POST', data: { clientMessageId, content }, timeout: MODEL_REQUEST_TIMEOUT
  }),
  requestTrainingHint: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/hint`, {
    method: 'POST', data: {}
  }),
  finishSession: (sessionId, reason = 'manual') => request(`/sessions/${encodeURIComponent(sessionId)}/finish`, {
    method: 'POST', data: { reason }
  }),
  abandonSession: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/abandon`, { method: 'POST', data: {} }),
  getEvaluation: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/evaluation`),
  retryEvaluation: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/evaluation/retry`, { method: 'POST', data: {} }),
  getSessions: params => request(`/sessions?${query(params || {})}`),

  // ── 患者模拟（roleplay） ──
  // 服务目录：RAG 语料按「服务」组织；开关关闭时后端返回空列表，前端隐藏选择器。
  getServices: () => request('/services'),
  getRoleplayScenarios: serviceId => request(`/roleplay/scenarios?${query({ serviceId: serviceId || '' })}`),
  // options: { freeDescription } 自由模拟模板描述 | { serviceId, clientSessionId } RAG 场景与幂等键
  createRoleplaySession: (scenarioId, options = {}) => request('/roleplay/sessions', {
    method: 'POST', data: Object.assign({ scenarioId }, options)
  }),
  restartRoleplaySession: (sessionId, options = {}) => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/restart`, {
    method: 'POST', data: Object.assign({}, options)
  }),
  getRoleplaySession: sessionId => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}`),
  sendRoleplayMessage: (sessionId, clientMessageId, content) => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/messages`, {
    method: 'POST', data: { clientMessageId, content }, timeout: MODEL_REQUEST_TIMEOUT
  }),
  finishRoleplaySession: (sessionId, reason = 'manual') => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/finish`, {
    method: 'POST', data: { reason }
  }),
  abandonRoleplaySession: sessionId => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/abandon`, {
    method: 'POST', data: {}
  }),
  getRoleplaySummary: sessionId => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/summary`),
  retryRoleplaySummary: sessionId => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/summary/retry`, { method: 'POST', data: {} }),
  getRoleplaySessions: params => request(`/roleplay/sessions?${query(params || {})}`),
  // RAG 引用依据（患者回复所依据的语料片段）
  getRoleplayEvidence: (sessionId, traceId) => request(
    `/roleplay/sessions/${encodeURIComponent(sessionId)}/evidence/${encodeURIComponent(traceId)}`
  ),

  // ── 数据看板 / 学员画像 ──
  getDashboard: () => request('/dashboard/summary'),
  getLearningPhrases: params => request(`/learning/phrases?${query(params || {})}`),
  setLearningPhraseFavorite: (sessionId, phraseKey, favorite) => request(
    `/learning/phrases/${encodeURIComponent(sessionId)}/${encodeURIComponent(phraseKey)}/favorite`,
    { method: 'PUT', data: { favorite } }
  ),
  getLearningMistakes: params => request(`/learning/mistakes?${query(params || {})}`),
  setLearningMistakeMastery: (sessionId, mistakeKey, mastered) => request(
    `/learning/mistakes/${encodeURIComponent(sessionId)}/${encodeURIComponent(mistakeKey)}`,
    { method: 'PUT', data: { mastered } }
  ),
  getMistakeRetrainContext: (sessionId, mistakeKey) => request(
    `/learning/mistakes/${encodeURIComponent(sessionId)}/${encodeURIComponent(mistakeKey)}/context`
  ),
  retrainMistake: (sessionId, mistakeKey, answer) => request(
    `/learning/mistakes/${encodeURIComponent(sessionId)}/${encodeURIComponent(mistakeKey)}/retrain`,
    { method: 'POST', data: { answer }, timeout: MODEL_REQUEST_TIMEOUT }
  ),
  getLearningProfile: () => request('/learning/profile'),
  getLearningMine: () => request('/learning/mine'),
  checkIn: () => request('/learning/checkins', { method: 'POST', data: {} }),

  // ── 主管端 ──
  getSupervisorDashboard: params => request(`/supervisor/dashboard?${query(params || {})}`),
  getSupervisorMembers: params => request(`/supervisor/members?${query(params || {})}`),
  getSupervisorMember: memberId => request(`/supervisor/members/${encodeURIComponent(memberId)}`),
  getSupervisorTrainingPlans: params => request(`/supervisor/training-plans?${query(params || {})}`),
  getSupervisorScenarios: () => request('/supervisor/scenarios'),
  /* 计划目标维度目录（五维 key + 中文名）。中文名的唯一来源在后端
     planDimensions()，前端不另建映射副本。 */
  getPlanDimensions: () => request('/supervisor/plan-dimensions'),
  createTrainingPlan: payload => request('/supervisor/training-plans', { method: 'POST', data: payload }),
  getTrainingPlan: planId => request(`/supervisor/training-plans/${encodeURIComponent(planId)}`),
  notifyTrainingPlan: planId => request(`/supervisor/training-plans/${encodeURIComponent(planId)}/notify`, {
    method: 'POST', data: {}
  }),
  // ── AI 训练建议（迁移 021：模型按薄弱项出草稿，主管审核后才指派） ──
  suggestTrainingPlans: payload => request('/supervisor/training-plans/suggest', {
    method: 'POST', data: payload || {}
  }),
  getTrainingPlanDrafts: () => request('/supervisor/training-plan-drafts'),
  publishTrainingPlan: (planId, payload) => request(
    `/supervisor/training-plans/${encodeURIComponent(planId)}/publish`,
    { method: 'POST', data: payload || {} }
  ),
  dismissTrainingPlan: planId => request(
    `/supervisor/training-plans/${encodeURIComponent(planId)}/dismiss`, { method: 'POST', data: {} }
  ),
  getLearnerTrainingPlans: () => request('/learning/training-plans'),
  /* 弱项 → 复练场景候选。dimension 是五维 key（空值由后端 400 拒绝，
     前端不要传空串——那会把「调用方写错了」掩盖成「暂无推荐」）。 */
  getRetrainCandidates: dimension => request(
    `/learning/retrain-candidates?dimension=${encodeURIComponent(dimension)}`
  ),
  getSupervisorForbiddenPhrases: params => request(`/supervisor/reports/forbidden-phrases?${query(params || {})}`),
  getSupervisorForbiddenPhraseMembers: (category, params) => request(
    `/supervisor/reports/forbidden-phrases/${encodeURIComponent(category)}?${query(params || {})}`
  ),
  getSupervisorLeaderboard: params => request(`/supervisor/reports/leaderboard?${query(params || {})}`),
  getTeamMembers: params => request(`/supervisor/team/members?${query(params || {})}`),
  getTeamCandidates: params => request(`/supervisor/team/candidates?${query(params || {})}`),
  addTeamMembers: learnerIds => request('/supervisor/team/members', { method: 'POST', data: { learnerIds } }),
  removeTeamMember: learnerId => request(`/supervisor/team/members/${encodeURIComponent(learnerId)}/remove`, {
    method: 'POST', data: {}
  }),
  // ── 场景管理 / 抽查 / 导出（主管端） ──
  getSupervisorScenarioCatalog: () => request('/supervisor/scenarios/manage'),
  createSupervisorScenario: payload => request('/supervisor/scenarios', { method: 'POST', data: payload }),
  updateSupervisorScenario: (scenarioId, payload) => request(
    `/supervisor/scenarios/${encodeURIComponent(scenarioId)}`, { method: 'PUT', data: payload }
  ),
  /* AI 生成场景骨架（迁移 030）。走的是与知识库同一套生成队列：
     提交时带幂等键（防连点重复建任务），随后轮询任务状态，
     成功后拿 result.scenarioId 进入编辑器补机构红线。 */
  createScenarioAiDraft: payload => request('/supervisor/scenarios/ai-draft', {
    method: 'POST', data: payload, header: { 'Idempotency-Key': payload.idempotencyKey }
  }),
  getScenarioAiDraft: jobId => request(
    `/supervisor/scenarios/ai-draft/${encodeURIComponent(jobId)}`
  ),
  retryScenarioAiDraft: jobId => request(
    `/supervisor/scenarios/ai-draft/${encodeURIComponent(jobId)}/retry`,
    { method: 'POST', data: {} }
  ),
  getSupervisorMemberSession: (memberId, sessionId) => request(
    `/supervisor/members/${encodeURIComponent(memberId)}/sessions/${encodeURIComponent(sessionId)}`
  ),
  exportSupervisorReport: params => request(`/supervisor/reports/export?${query(params || {})}`),

  // ── 演示账号切换 ──
  // 注意：调用方（index/mine）在拿到 data 后自行落盘 accessToken 与 user，
  // switchRole 刻意不做副作用，避免与页面重复写入。
  switchRole: role => request('/auth/switch-role', { method: 'POST', data: { role } }),
  getDemoLearners: () => request('/demo/learners'),
  switchLearner: userId => request('/auth/switch-learner', { method: 'POST', data: { userId } })
    .then(persistIdentity)
};
