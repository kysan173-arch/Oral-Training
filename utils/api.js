const { getApiBaseUrl } = require('./config.js');

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
    timeout: options.timeout || 30000,
    header: Object.assign({ 'content-type': 'application/json' }, options.token
      ? { Authorization: `Bearer ${options.token}` }
      : {}),
    success: response => {
      const payload = response.data || {};
      if (response.statusCode >= 200 && response.statusCode < 300 && payload.code === 0) {
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
  const score = Number(value);
  return Number.isFinite(score) ? Number(score.toFixed(1)) : 0;
};

module.exports = {
  formatScore,
  ensureAuthenticated,
  clearAuthentication,
  getCurrentUser: () => wx.getStorageSync(USER_KEY) || null,
  getHealth: () => request('/health', { public: true }),
  setDeepSeekKey: apiKey => request('/config/deepseek-key', { method: 'POST', data: { apiKey } }),
  getScenarios: () => request('/scenarios'),
  createSession: scenarioId => request('/sessions', { method: 'POST', data: { scenarioId } }),
  restartSession: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/restart`, { method: 'POST', data: {} }),
  getSession: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}`),
  sendMessage: (sessionId, clientMessageId, content) => request(`/sessions/${encodeURIComponent(sessionId)}/messages`, {
    method: 'POST', data: { clientMessageId, content }
  }),
  requestTrainingHint: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/hint`, {
    method: 'POST', data: {}
  }),
  finishSession: (sessionId, reason = 'manual') => request(`/sessions/${encodeURIComponent(sessionId)}/finish`, {
    method: 'POST', data: { reason }
  }),
  getEvaluation: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/evaluation`),
  retryEvaluation: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/evaluation/retry`, { method: 'POST', data: {} }),
  getSessions: params => request(`/sessions?${query(params || {})}`),
  getRoleplayScenarios: () => request('/roleplay/scenarios'),
  createRoleplaySession: scenarioId => request('/roleplay/sessions', { method: 'POST', data: { scenarioId } }),
  restartRoleplaySession: sessionId => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/restart`, { method: 'POST', data: {} }),
  getRoleplaySession: sessionId => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}`),
  sendRoleplayMessage: (sessionId, clientMessageId, content) => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/messages`, {
    method: 'POST', data: { clientMessageId, content }
  }),
  finishRoleplaySession: (sessionId, reason = 'manual') => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/finish`, {
    method: 'POST', data: { reason }
  }),
  getRoleplaySummary: sessionId => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/summary`),
  retryRoleplaySummary: sessionId => request(`/roleplay/sessions/${encodeURIComponent(sessionId)}/summary/retry`, { method: 'POST', data: {} }),
  getRoleplaySessions: params => request(`/roleplay/sessions?${query(params || {})}`),
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
  getLearningProfile: () => request('/learning/profile'),
  getLearningMine: () => request('/learning/mine'),
  checkIn: () => request('/learning/checkins', { method: 'POST', data: {} }),
  getSupervisorDashboard: params => request(`/supervisor/dashboard?${query(params || {})}`),
  getSupervisorMembers: params => request(`/supervisor/members?${query(params || {})}`),
  getSupervisorMember: memberId => request(`/supervisor/members/${encodeURIComponent(memberId)}`)
};
