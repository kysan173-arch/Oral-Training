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

const computeLevel = points => {
  const levels = [
    { level: 1, name: '见习客服', min: 0, max: 49 },
    { level: 2, name: '初级客服', min: 50, max: 149 },
    { level: 3, name: '资深客服', min: 150, max: 299 },
    { level: 4, name: '高级顾问', min: 300, max: 499 },
    { level: 5, name: '首席顾问', min: 500, max: Infinity }
  ];
  const current = levels.find(l => points >= l.min && points <= l.max) || levels[0];
  const progress = current.max === Infinity ? 100
    : Math.min(100, Math.round((points - current.min) / (current.max - current.min) * 100));
  return {
    level: current.level,
    levelName: current.name,
    currentPoints: points,
    nextLevelPoints: current.max === Infinity ? points : current.max,
    progress,
    isMax: current.level === 5
  };
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
    levelInfo: { level: 1, levelName: '见习客服', currentPoints: 0, nextLevelPoints: 50, progress: 0, isMax: false },
    rulesExpanded: false,
    checkinBurst: false
  },

  onShow() {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 3 });
    }
    const user = api.getCurrentUser();
    if (user && user.role) {
      this.setData({ currentRole: user.role });
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

  loadMine() {
    this.setData({ loading: true, loadError: false, loadErrorMsg: '' });
    api.getLearningMine().then(data => {
      const savedAvatar = wx.getStorageSync('mine_avatar') || '';
      const savedNickname = wx.getStorageSync('mine_nickname') || '';
      const displayName = savedNickname || data.user.displayName;
      const levelInfo = computeLevel(data.points);
      const streakDays = data.checkin.streakDays;
      let streakText = '从今天开始记录';
      if (streakDays >= 30) streakText = `🔥 已连续 ${streakDays} 天，太厉害了！`;
      else if (streakDays >= 7) streakText = `🔥 连续签到 ${streakDays} 天，保持节奏`;
      else if (streakDays > 0) streakText = `🔥 连续签到 ${streakDays} 天`;
      this.setData({
        mine: data,
        displayName,
        avatar: savedAvatar,
        levelInfo,
        calendarDays: buildCalendar(data.checkin),
        streakText,
        loading: false,
        loadError: false
      });
    }).catch(error => {
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

  goProfile() { wx.navigateTo({ url: '/pages/profile/profile' }); },
  goMistakes() { wx.navigateTo({ url: '/pages/mistakes/mistakes' }); },
  goPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); },
  goFavorites() { wx.navigateTo({ url: '/pages/phrases/phrases?favorites=1' }); }
});
