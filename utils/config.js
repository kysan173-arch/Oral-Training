const API_BASE_URLS = {
  develop: 'http://127.0.0.1:8080/api',
  trial: '',
  release: ''
};

const getEnvironmentVersion = () => {
  try {
    return wx.getAccountInfoSync().miniProgram.envVersion || 'develop';
  } catch (error) {
    return 'develop';
  }
};

const getApiBaseUrl = () => {
  const environment = getEnvironmentVersion();
  let extConfig = {};
  try {
    extConfig = wx.getExtConfigSync ? wx.getExtConfigSync() : {};
  } catch (error) {
    extConfig = {};
  }
  const configuredUrl = String(extConfig.apiBaseUrl || API_BASE_URLS[environment] || '').trim().replace(/\/$/, '');
  if (!configuredUrl) {
    const configError = new Error(`${environment} 环境尚未配置 API 地址`);
    configError.code = 'API_BASE_URL_NOT_CONFIGURED';
    throw configError;
  }
  if (environment !== 'develop' && !/^https:\/\//.test(configuredUrl)) {
    const protocolError = new Error('体验版和正式版必须使用 HTTPS API 地址');
    protocolError.code = 'API_BASE_URL_INSECURE';
    throw protocolError;
  }
  return configuredUrl;
};

module.exports = { getApiBaseUrl };
