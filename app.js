const request = require('./static/api/request.js');

App({
  onLaunch() {
    // MVP 使用固定演示用户，不触发微信授权，也不保存真实用户信息。
    this.globalData.demoUser = {
      name: '固定演示账号',
      role: '新入口腔客服'
    };
  },

  globalData: {
    demoUser: null
  }
});
