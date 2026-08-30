const api = require('../../utils/api.js');

const buildCalendar = checkin => {
  const year = checkin.year;
  const month = checkin.month;
  const checkedDates = new Set(checkin.checkedDates || []);
  const today = checkin.today || '';
  const firstWeekday = new Date(Date.UTC(year, month - 1, 1)).getUTCDay();
  const daysInMonth = new Date(Date.UTC(year, month, 0)).getUTCDate();
  const cells = [];
  for (let index = 0; index < firstWeekday; index += 1) cells.push({ empty: true });
  for (let day = 1; day <= daysInMonth; day += 1) {
    const date = `${year}-${String(month).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
    cells.push({ day, checked: checkedDates.has(date), isToday: date === today, empty: false });
  }
  return cells;
};

Page({
  data: {
    loading: true,
    checkingIn: false,
    isAdmin: false,
    mine: null,
    adminData: null,
    calendarDays: [],
    streakText: '',
    currentRole: '',
    switchingRole: false,
    loadError: false,
    loadErrorMsg: '',
    displayName: '',
    avatar: '',
    showDemoPicker: false,
    demoUsers: [],
    currentUserId: '',
    checkinExpanded: false,
    rulesExpanded: false
  },

  onShow() {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 3 });
    }
    const user = api.getCurrentUser();
    if (user) {
      this.setData({ currentRole: user.role, currentUserId: user.id });
    }
    if (user && user.role === 'admin') {
      this.setData({ isAdmin: true });
      this.loadAdminMine();
      return;
    }
    this.setData({ isAdmin: false });
    this.loadMine();
  },

  loadAdminMine() {
    this.setData({ loading: true, loadError: false, loadErrorMsg: '' });
    Promise.all([
      api.getSupervisorDashboard({ range: 'month' }),
      api.getSupervisorMembers({ limit: 100 })
    ]).then(([dashboard, memberData]) => {
      const savedAvatar = wx.getStorageSync('mine_avatar') || '';
      const savedNickname = wx.getStorageSync('mine_nickname') || '';
      const user = api.getCurrentUser();
      const displayName = savedNickname || (user && user.displayName) || '主管';
      const memberCount = (memberData.members || []).length;
      const avgScore = typeof dashboard.averageScore === 'number'
        ? (Math.round(dashboard.averageScore * 10) / 10).toFixed(1)
        : dashboard.averageScore || 0;
      this.setData({
        displayName,
        avatar: savedAvatar,
        adminData: {
          studentCount: dashboard.studentCount || memberCount || 0,
          totalSessions: dashboard.totalSessions || 0,
          averageScore: avgScore,
          passRate: dashboard.passRate || 0
        },
        loading: false,
        loadError: false
      });
    }).catch(error => {
      this.setData({ loading: false, loadError: true, loadErrorMsg: error.message || '管理数据加载失败' });
      wx.showToast({ title: error.message || '管理数据加载失败', icon: 'none' });
    });
  },

  // ── 切换演示账号（仅 Demo 模式测试） ──
  openDemoPicker() {
    this.setData({ showDemoPicker: true });
    api.getDemoLearners().then(data => {
      const user = api.getCurrentUser();
      this.setData({ demoUsers: data.items || [], currentUserId: user ? user.id : '' });
    }).catch(error => {
      this.setData({ showDemoPicker: false });
      wx.showToast({ title: error.message || '加载演示账号失败', icon: 'none' });
    });
  },

  closeDemoPicker() {
    this.setData({ showDemoPicker: false });
  },

  selectDemoUser(e) {
    const userId = e.currentTarget.dataset.id;
    if (!userId || userId === this.data.currentUserId) {
      this.setData({ showDemoPicker: false });
      return;
    }
    wx.showLoading({ title: '切换中…', mask: true });
    api.switchLearner(userId).then(() => {
      wx.hideLoading();
      this.setData({ showDemoPicker: false });
      wx.showToast({ title: '已切换账号', icon: 'success', duration: 1500 });
      setTimeout(() => {
        const role = api.getCurrentUser() ? api.getCurrentUser().role : 'learner';
        wx.switchTab({ url: role === 'admin' ? '/pages/admin/admin' : '/pages/mine/mine' });
      }, 1600);
    }).catch(error => {
      wx.hideLoading();
      wx.showToast({ title: error.message || '切换失败', icon: 'none' });
    });
  },

  loadMine() {
    this.setData({ loading: true, loadError: false, loadErrorMsg: '' });
    api.getLearningMine().then(data => {
      const savedAvatar = wx.getStorageSync('mine_avatar') || '';
      const savedNickname = wx.getStorageSync('mine_nickname') || '';
      const displayName = savedNickname || data.user.displayName;
      const streakDays = data.checkin.streakDays;
      let streakText = data.checkin.checkedToday ? '今天已打卡' : '今天还没打卡';
      if (streakDays > 0) streakText += ` · 已连续 ${streakDays} 天`;
      this.setData({
        mine: data,
        displayName,
        avatar: savedAvatar,
        calendarDays: buildCalendar(data.checkin),
        streakText,
        loading: false,
        loadError: false
      });
    }).catch(error => {
      // 403 ROLE_FORBIDDEN：本地缓存的 role 跟后端实际身份不一致，按后端为准自动降级到主管视图
      if (error && (error.code === 'ROLE_FORBIDDEN' || /管理员账号不能使用学员/.test(error.message || ''))) {
        const cached = api.getCurrentUser();
        if (cached) {
          const fixed = Object.assign({}, cached, { role: 'admin' });
          try { wx.setStorageSync('oralTrainingUser', fixed); } catch (e) {}
        }
        this.setData({ isAdmin: true, loading: true, loadError: false, loadErrorMsg: '' });
        this.loadAdminMine();
        return;
      }
      this.setData({ loading: false, loadError: true, loadErrorMsg: error.message || '成长数据加载失败' });
      wx.showToast({ title: error.message || '成长数据加载失败', icon: 'none' });
    });
  },

  checkIn() {
    if (!this.data.mine || this.data.checkingIn) return;
    if (this.data.mine.checkin.checkedToday) {
      wx.showToast({ title: '今日已签到', icon: 'none' });
      return;
    }
    this.setData({ checkingIn: true });
    api.checkIn().then(data => {
      this.setData({ checkingIn: false });
      wx.showToast({ title: data.checkedIn ? `签到成功 +${data.pointsAwarded} 积分` : '今日已签到', icon: 'success' });
      if (data.checkedIn) {
        this.setData({ checkinBurst: true });
        setTimeout(() => this.setData({ checkinBurst: false }), 700);
      }
      this.loadMine();
    }).catch(error => {
      this.setData({ checkingIn: false });
      wx.showToast({ title: error.message || '签到失败', icon: 'none' });
    });
  },

  switchRole() {
    if (this.data.switchingRole) return;
    const targetRole = this.data.currentRole === 'admin' ? 'learner' : 'admin';
    wx.showModal({
      title: '切换身份',
      content: `确定要切换为「${targetRole === 'admin' ? '主管' : '学员'}」身份吗？`,
      success: res => {
        if (!res.confirm) return;
        this.setData({ switchingRole: true });
        api.switchRole(targetRole).then(data => {
          wx.setStorageSync('oralTrainingAccessToken', data.accessToken);
          wx.setStorageSync('oralTrainingUser', data.user);
          wx.showToast({ title: '已切换，即将刷新', icon: 'success', duration: 1500 });
          setTimeout(() => {
            this.setData({ switchingRole: false });
            wx.switchTab({ url: targetRole === 'admin' ? '/pages/admin/admin' : '/pages/mine/mine' });
          }, 1600);
        }).catch(error => {
          this.setData({ switchingRole: false });
          wx.showToast({ title: error.message || '切换失败', icon: 'none' });
        });
      }
    });
  },

  goAdminDashboard() { wx.switchTab({ url: '/pages/admin/admin' }); },
  goAdminMembers() { wx.switchTab({ url: '/pages/admin/admin' }); },

  toggleRules() {
    this.setData({ rulesExpanded: !this.data.rulesExpanded });
  },

  toggleCheckin() {
    this.setData({ checkinExpanded: !this.data.checkinExpanded });
  },

  goProfile() { wx.navigateTo({ url: '/pages/profile/profile' }); },
  goMistakes() { wx.navigateTo({ url: '/pages/mistakes/mistakes' }); },
  goPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); },
  goFavorites() { wx.navigateTo({ url: '/pages/phrases/phrases?favorites=1' }); }
});
