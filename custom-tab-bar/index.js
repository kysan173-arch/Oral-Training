const api = require('../utils/api.js');

/* 底部导航按角色渲染：学员第 2 格是「训练」，主管第 2 格是「培训」(培训运营独立页)。
   两份列表的 home / 第二格 / 数据 / 我的 下标一致（0/1/2/3），
   因此各 tab 页 onShow 里的 getTabBar().setData({ selected: N }) 无需改动。
   注意：list 只影响渲染，wx.switchTab 的合法目标仍由 app.json tabBar.list 决定，
   该页已登记（共 5 项，达小程序上限）。 */
const HOME = { pagePath: '/pages/home/home', text: '首页', iconPath: '/static/image/tabbar/home.png', selectedIconPath: '/static/image/tabbar/home_selected.png' };
const DATA = { pagePath: '/pages/admin/admin', text: '数据', iconPath: '/static/image/tabbar/report.png', selectedIconPath: '/static/image/tabbar/report_selected.png' };
const MINE = { pagePath: '/pages/mine/mine', text: '我的', iconPath: '/static/image/tabbar/mine.png', selectedIconPath: '/static/image/tabbar/mine_selected.png' };
const TRAINING = { pagePath: '/pages/index/index', text: '训练', iconPath: '/static/image/tabbar/training.png', selectedIconPath: '/static/image/tabbar/training_selected.png' };
const ADMIN_TRAINING = { pagePath: '/pages/admin-training/admin-training', text: '培训', iconPath: '/static/image/tabbar/training.png', selectedIconPath: '/static/image/tabbar/training_selected.png' };

Component({
  data: {
    selected: 0,
    role: '',
    list: [HOME, TRAINING, DATA, MINE]
  },

  lifetimes: {
    attached() {
      this.applyRoleList();
    }
  },

  /* 每次所在页面显示时重读身份：切换角色后无需手动刷新导航 */
  pageLifetimes: {
    show() {
      this.applyRoleList();
    }
  },

  methods: {
    /* 角色 → 导航列表。各 tab 页的 onShow 会主动调用本方法，
       所以判定必须同时看 role 和当前列表：只比 role 的话，
       一旦列表落后于 role（切换身份后残留另一套导航）就再也纠不回来。 */
    applyRoleList() {
      const user = api.getCurrentUser();
      const role = (user && user.role) || 'learner';
      const list = [HOME, role === 'admin' ? ADMIN_TRAINING : TRAINING, DATA, MINE];
      if (role === this.data.role && this.data.list[1] === list[1]) return;
      this.setData({ role, list });
    },

    switchTab(e) {
      const data = e.currentTarget.dataset;
      const url = data.path;
      wx.switchTab({ url });
    }
  }
});
