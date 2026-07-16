const createError = (code, message, statusCode, data = null) => ({
  code,
  message,
  statusCode,
  data
});

const request = (url, options = {}) => {
  return new Promise((resolve, reject) => {
    const app = getApp();
    const baseUrl = (app?.globalData?.apiBaseUrl || 'http://localhost:3000/api').replace(/\/$/, '');
    const path = url.startsWith('/') ? url : `/${url}`;
    const fullUrl = `${baseUrl}${path}`;

    console.log('[request]', options.method || 'GET', fullUrl);

    wx.request({
      url: fullUrl,
      method: options.method || 'GET',
      data: options.data || {},
      timeout: 30000,
      header: {
        'Content-Type': 'application/json',
        'X-Demo-User-Id': (app?.globalData?.demoUserId) || 'demo-user-001'
      },
      success: (res) => {
        const body = res.data || {};
        const isHttpSuccess = res.statusCode >= 200 && res.statusCode < 300;

        if (isHttpSuccess && body.code === 0) {
          resolve(body.data);
          return;
        }

        console.error('[request] 失败', fullUrl, res.statusCode, body);
        reject(createError(
          body.code || `HTTP_${res.statusCode}`,
          body.message || `网络请求失败（${res.statusCode}）`,
          res.statusCode,
          body.data || null
        ));
      },
      fail: (err) => {
        console.error('[request] 网络错误', fullUrl, err.errMsg);
        reject(createError('NETWORK_ERROR', err.errMsg || '网络请求失败', 0));
      }
    });
  });
};

const get = (url, data) => request(url, { method: 'GET', data });
const post = (url, data) => request(url, { method: 'POST', data });

module.exports = {
  request,
  get,
  post,
  getErrorMessage: (error, fallback = '操作失败，请稍后重试') => {
    if (!error) return fallback;
    return error.message || String(error) || fallback;
  }
};
