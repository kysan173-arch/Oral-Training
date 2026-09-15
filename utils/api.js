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
  ensureAuthenticated,
  clearAuthentication,
  getCurrentUser: () => wx.getStorageSync(USER_KEY) || null,
  getHealth: () => request('/health', { public: true, acceptUnreadyHealth: true }),

  // ── 训练（学员端） ──
  getScenarios: () => request('/scenarios'),
  createSession: (scenarioId, customPatientProfile) => request('/sessions', {
    method: 'POST', data: { scenarioId, customPatientProfile }
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
  createTrainingPlan: payload => request('/supervisor/training-plans', { method: 'POST', data: payload }),
  getTrainingPlan: planId => request(`/supervisor/training-plans/${encodeURIComponent(planId)}`),
  notifyTrainingPlan: planId => request(`/supervisor/training-plans/${encodeURIComponent(planId)}/notify`, {
    method: 'POST', data: {}
  }),
  getLearnerTrainingPlans: () => request('/learning/training-plans'),
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
  getSupervisorMemberSession: (memberId, sessionId) => request(
    `/supervisor/members/${encodeURIComponent(memberId)}/sessions/${encodeURIComponent(sessionId)}`
  ),
  exportSupervisorReport: params => request(`/supervisor/reports/export?${query(params || {})}`),

  // ── 知识管理后台（RAG 语料与诊所服务） ──
  getAdminServices: () => request('/admin/services'),
  createAdminService: payload => request('/admin/services', {
    method: 'POST', data: { payload }
  }),
  getAdminServiceDraft: serviceId => request(`/admin/services/${encodeURIComponent(serviceId)}/draft`),
  saveAdminServiceDraft: (serviceId, draftVersion, payload) => request(
    `/admin/services/${encodeURIComponent(serviceId)}/draft`,
    { method: 'PUT', data: { draftVersion, payload } }
  ),
  publishAdminService: (serviceId, draftVersion, idempotencyKey) => request(
    `/admin/services/${encodeURIComponent(serviceId)}/publish`,
    { method: 'POST', data: { draftVersion }, header: { 'Idempotency-Key': idempotencyKey } }
  ),
  archiveAdminService: serviceId => request(
    `/admin/services/${encodeURIComponent(serviceId)}/archive`, { method: 'POST', data: {} }
  ),
  getAdminServiceRevisions: serviceId => request(
    `/admin/services/${encodeURIComponent(serviceId)}/revisions`
  ),
  getAdminKnowledge: () => request('/admin/knowledge'),
  createAdminKnowledge: payload => request('/admin/knowledge', { method: 'POST', data: payload }),
  getAdminKnowledgeDraft: entryId => request(`/admin/knowledge/${encodeURIComponent(entryId)}/draft`),
  saveAdminKnowledgeDraft: (entryId, payload) => request(
    `/admin/knowledge/${encodeURIComponent(entryId)}/draft`,
    { method: 'PUT', data: payload }
  ),
  publishAdminKnowledge: (entryId, draftVersion, idempotencyKey) => request(
    `/admin/knowledge/${encodeURIComponent(entryId)}/publish`,
    { method: 'POST', data: { draftVersion }, header: { 'Idempotency-Key': idempotencyKey } }
  ),
  archiveAdminKnowledge: entryId => request(
    `/admin/knowledge/${encodeURIComponent(entryId)}/archive`, { method: 'POST', data: {} }
  ),
  getAdminKnowledgeRevisions: entryId => request(
    `/admin/knowledge/${encodeURIComponent(entryId)}/revisions`
  ),
  createKnowledgeGenerationJob: payload => request('/admin/knowledge/generation-jobs', {
    method: 'POST', data: payload, header: { 'Idempotency-Key': payload.idempotencyKey }
  }),
  getKnowledgeGenerationJob: jobId => request(
    `/admin/knowledge/generation-jobs/${encodeURIComponent(jobId)}`
  ),
  retryKnowledgeGenerationJob: jobId => request(
    `/admin/knowledge/generation-jobs/${encodeURIComponent(jobId)}/retry`,
    { method: 'POST', data: {} }
  ),
  previewAdminKnowledge: payload => request('/admin/knowledge/preview', {
    method: 'POST', data: payload
  }),

  // ── 演示账号切换 ──
  // 注意：调用方（index/mine）在拿到 data 后自行落盘 accessToken 与 user，
  // switchRole 刻意不做副作用，避免与页面重复写入。
  switchRole: role => request('/auth/switch-role', { method: 'POST', data: { role } }),
  getDemoLearners: () => request('/demo/learners'),
  switchLearner: userId => request('/auth/switch-learner', { method: 'POST', data: { userId } })
    .then(persistIdentity)
};
