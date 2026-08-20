Component({
  data: {
    selected: 0,
    list: [
      { pagePath: '/pages/home/home', text: '首页', iconPath: '/static/image/tabbar/home.png', selectedIconPath: '/static/image/tabbar/home_selected.png' },
      { pagePath: '/pages/index/index', text: '训练', iconPath: '/static/image/tabbar/training.png', selectedIconPath: '/static/image/tabbar/training_selected.png' },
      { pagePath: '/pages/admin/admin', text: '数据', iconPath: '/static/image/tabbar/report.png', selectedIconPath: '/static/image/tabbar/report_selected.png' },
      { pagePath: '/pages/mine/mine', text: '我的', iconPath: '/static/image/tabbar/mine.png', selectedIconPath: '/static/image/tabbar/mine_selected.png' }
    ]
  },

  methods: {
    switchTab(e) {
      const data = e.currentTarget.dataset;
      const url = data.path;
      wx.switchTab({ url });
    }
  }
});
