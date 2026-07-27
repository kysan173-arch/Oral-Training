const { getApiBaseUrl } = require('./config.js');

const request = (path, options = {}) => new Promise((resolve, reject) => {
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
    header: {
      'content-type': 'application/json',
      'X-Demo-User-Id': 'demo-user-001'
    },
    success: response => {
      const payload = response.data || {};
      if (response.statusCode >= 200 && response.statusCode < 300 && payload.code === 0) {
        resolve(payload.data);
        return;
      }
      const error = new Error(payload.message || '服务请求失败');
      error.code = payload.code || 'NETWORK_ERROR';
      reject(error);
    },
    fail: error => {
      const requestError = new Error(error.errMsg || '无法连接后端服务');
      requestError.code = 'NETWORK_ERROR';
      reject(requestError);
    }
  });
});

const query = values => Object.keys(values)
  .filter(key => values[key] !== undefined && values[key] !== null && values[key] !== '')
  .map(key => `${encodeURIComponent(key)}=${encodeURIComponent(values[key])}`)
  .join('&');

module.exports = {
  getHealth: () => request('/health'),
  setDeepSeekKey: apiKey => request('/config/deepseek-key', { method: 'POST', data: { apiKey } }),
  getScenarios: () => request('/scenarios'),
  createSession: scenarioId => request('/sessions', { method: 'POST', data: { scenarioId } }),
  restartSession: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/restart`, { method: 'POST', data: {} }),
  getSession: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}`),
  sendMessage: (sessionId, clientMessageId, content) => request(`/sessions/${encodeURIComponent(sessionId)}/messages`, {
    method: 'POST', data: { clientMessageId, content }
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
  getHint: sessionId => request(`/sessions/${encodeURIComponent(sessionId)}/hint`, { method: 'POST', data: {} }),
  getPhrases: (keyword) => {
    const qs = keyword ? `?search=${encodeURIComponent(keyword)}` : '';
    return request(`/phrases${qs}`);
  },
  getHomeOverview: () => request('/home/overview'),
  getProfile: () => request('/profile'),
  getMineDashboard: () => request('/mine/dashboard'),
  mineCheckin: () => request('/mine/checkin', { method: 'POST' }),
  getMineRules: () => request('/mine/rules'),
  addMinePoints: (amount) => request('/mine/add-points', { method: 'POST', data: { amount } }),
  // Admin / 主管端
  getEnhancedDashboard: (params) => request(`/dashboard/enhanced?${query(params || {})}`),
  getDashboardTrend: (params) => request(`/dashboard/trend?${query(params || {})}`),
  getMembers: (params) => request(`/members?${query(params || {})}`),
  getMemberProfile: (memberId) => request(`/members/${encodeURIComponent(memberId)}/profile`),
  getWarnings: () => request('/warnings'),
  getPlans: (params) => request(`/plans?${query(params || {})}`),
  createPlan: (data) => request('/plans', { method: 'POST', data }),
  updatePlan: (planId, data) => request(`/plans/${encodeURIComponent(planId)}`, { method: 'PUT', data }),
  deletePlan: (planId) => request(`/plans/${encodeURIComponent(planId)}`, { method: 'DELETE' }),
  getPlanTasks: (planId) => request(`/plans/${encodeURIComponent(planId)}/tasks`),
  createPlanTask: (planId, data) => request(`/plans/${encodeURIComponent(planId)}/tasks`, { method: 'POST', data }),
  updatePlanTask: (planId, taskId, data) => request(`/plans/${encodeURIComponent(planId)}/tasks/${encodeURIComponent(taskId)}`, { method: 'PUT', data }),
  getViolationWords: () => request('/violations/words'),
  getLeaderboard: (params) => request(`/leaderboard?${query(params || {})}`),
  exportReport: (format = 'csv') => request(`/export/report?format=${encodeURIComponent(format)}`)
};
