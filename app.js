const api = require('./utils/api.js');

App({
  onLaunch() {
    api.ensureAuthenticated().then(() => {
      this.globalData.currentUser = api.getCurrentUser();
    }).catch(() => {
      this.globalData.currentUser = null;
    });
  },

  globalData: {
    currentUser: null
  }
});
