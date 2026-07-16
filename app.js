const request = require('./static/api/request.js');

App({
  globalData: {
    apiBaseUrl: 'http://localhost:3000/api',
    demoUserId: 'demo-user-001',

    // 当前训练上下文（组件间共享，减少 URL 参数传递）
    currentSession: {
      id: '',
      scenarioId: '',
      scenarioName: '',
      scenarioCategory: ''
    }
  },

  /**
   * 设置当前训练上下文
   */
  setCurrentSession(session) {
    this.globalData.currentSession = {
      ...this.globalData.currentSession,
      ...(session || {})
    };
  },

  /**
   * 清除当前训练上下文
   */
  clearCurrentSession() {
    this.globalData.currentSession = {
      id: '',
      scenarioId: '',
      scenarioName: '',
      scenarioCategory: ''
    };
  },

  onLaunch() {
    // 全局未捕获 Promise 错误拦截
    if (typeof wx !== 'undefined' && wx.onUnhandledRejection) {
      wx.onUnhandledRejection((res) => {
        console.warn('[全局未处理Promise拒绝]', res.reason);
      });
    }

    // 检查小程序更新
    if (wx.getUpdateManager) {
      const updateManager = wx.getUpdateManager();
      updateManager.onUpdateReady(() => {
        wx.showModal({
          title: '更新提示',
          content: '新版本已就绪，是否重启应用？',
          success: (res) => {
            if (res.confirm) updateManager.applyUpdate();
          }
        });
      });
    }
  },

  /**
   * 全局错误处理：尝试重新请求（适用于网络抖动）
   */
  async retryRequest(requestFn, maxRetries = 2) {
    let lastError;
    for (let i = 0; i <= maxRetries; i++) {
      try {
        if (i > 0) await new Promise(r => setTimeout(r, 1000 * i));
        return await requestFn();
      } catch (error) {
        lastError = error;
        if (i < maxRetries) {
          console.warn(`[全局重试] 第 ${i + 1} 次请求失败，${maxRetries - i} 次重试中`);
        }
      }
    }
    throw lastError;
  }
});
