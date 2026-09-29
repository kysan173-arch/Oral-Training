// One durable intent per user, mode, service and scenario. Keep it on ambiguous failures.
const api = require('./api.js');
const keyFor = (mode, serviceId, scenarioId) => {
  const user = api.getCurrentUser();
  if (!user || !user.id) throw new Error('请重新登录后开始训练');
  return `session-start:${user.id}:${mode}:${serviceId}:${scenarioId}`;
};
const intent = (mode, serviceId, scenarioId) => {
  const key = keyFor(mode, serviceId, scenarioId);
  let saved = wx.getStorageSync(key);
  if (!saved || !saved.clientSessionId) {
    saved = { clientSessionId: `session-${Date.now()}-${Math.random().toString(36).slice(2)}` };
    wx.setStorageSync(key, saved); // Refuse to send if durable storage fails.
  }
  return { key, clientSessionId: saved.clientSessionId };
};
const clear = key => wx.removeStorageSync(key);
module.exports = { intent, clear };
