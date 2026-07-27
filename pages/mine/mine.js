const api = require('../../utils/api.js');

const POINTS_PER_TRAINING = 100;
const POINTS_PER_PASS = 50;

Page({
  data: {
    points: 0,
    level: { name: '见习客服', icon: '🆕' },
    levelProgress: 0,
    calendar: { year: 2026, month: 1, days: [] },
    checkedToday: false,
    streakDays: 0,
    checkinDays: 0,
    stats: { totalCompleted: 0, passRate: 0, avgScore: 0, checkinDays: 0 },
    leaderboard: [],
    favoritesCount: 0,
    showRules: false,
    pointsRules: []
  },

  onShow() {
    this.loadDashboard();
  },

  loadDashboard() {
    api.getMineDashboard().then(data => {
      this.setData({
        points: data.points,
        level: data.level,
        levelProgress: this.calcLevelProgress(data.points),
        calendar: data.calendar,
        checkedToday: data.calendar.checkedToday,
        streakDays: data.calendar.streakDays,
        checkinDays: data.calendar.checkinDays,
        stats: data.stats,
        leaderboard: data.leaderboard,
        favoritesCount: data.favoritesCount
      });
    }).catch(error => {
      // 后端不可用时，使用本地 storage
      this.loadFromLocal();
      wx.showToast({ title: error.message || '数据加载失败', icon: 'none', duration: 1500 });
    });
  },

  loadFromLocal() {
    try {
      const local = wx.getStorageSync('mineData') || {};
      const points = local.points || 0;
      const checkinDates = local.checkinDates || [];
      const now = new Date();
      const year = now.getFullYear();
      const month = now.getMonth();
      const todayStr = `${year}-${String(month + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
      const daysInMonth = new Date(year, month + 1, 0).getDate();
      const firstDayOfWeek = new Date(year, month, 1).getDay();
      const calendarDays = [];
      for (let i = 0; i < firstDayOfWeek; i++) calendarDays.push({ day: '', checked: false, isToday: false });
      for (let d = 1; d <= daysInMonth; d++) {
        const ds = `${year}-${String(month + 1).padStart(2, '0')}-${String(d).padStart(2, '0')}`;
        calendarDays.push({ day: d, checked: checkinDates.includes(ds), isToday: ds === todayStr });
      }
      const checkedToday = checkinDates.includes(todayStr);

      const levelMap = [
        { min: 0, name: '见习客服', icon: '🆕' },
        { min: 200, name: '初级客服', icon: '🌱' },
        { min: 500, name: '进阶客服', icon: '📈' },
        { min: 1000, name: '资深客服', icon: '💎' },
        { min: 2000, name: '金牌客服', icon: '👑' },
        { min: 4000, name: '首席客服', icon: '🏆' }
      ];
      const level = [...levelMap].reverse().find(l => points >= l.min) || levelMap[0];

      this.setData({
        points,
        level,
        levelProgress: this.calcLevelProgress(points),
        calendar: { year, month: month + 1, days: calendarDays },
        checkedToday,
        streakDays: 0,
        checkinDays: checkinDates.length,
        stats: local.stats || { totalCompleted: 0, passRate: 0, avgScore: 0, checkinDays: checkinDates.length },
        favoritesCount: (local.favorites || []).length
      });
    } catch (e) {}
  },

  calcLevelProgress(points) {
    const thresholds = [200, 500, 1000, 2000, 4000];
    for (const t of thresholds) {
      if (points < t) return Math.min(100, Math.round((points / t) * 100));
    }
    return 100;
  },

  // 打卡
  onCheckin() {
    if (this.data.checkedToday) {
      wx.showToast({ title: '今日已打卡', icon: 'none' });
      return;
    }
    api.mineCheckin().then(data => {
      this.saveLocalCheckin(data);
    }).catch(() => {
      // 本地打卡
      this.localCheckin();
    });
  },

  saveLocalCheckin(data) {
    wx.showToast({ title: data.message, icon: 'success' });
    const now = new Date();
    const todayStr = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
    this.setData({ points: data.points, checkedToday: true });
    this.updateCalendarCell(todayStr);
    this.persistLocal();
  },

  localCheckin() {
    const now = new Date();
    const todayStr = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
    const local = wx.getStorageSync('mineData') || {};
    local.points = (local.points || 0) + 10;
    local.checkinDates = local.checkinDates || [];
    local.checkinDates.push(todayStr);
    wx.setStorageSync('mineData', local);
    wx.showToast({ title: '打卡成功 +10 积分', icon: 'success' });
    this.updateCalendarCell(todayStr);
    this.setData({
      points: local.points,
      level: this.getLevel(local.points),
      levelProgress: this.calcLevelProgress(local.points),
      checkedToday: true,
      checkinDays: local.checkinDates.length
    });
  },

  updateCalendarCell(dateStr) {
    const days = this.data.calendar.days.map(d => {
      if (!d.day) return d;
      const now = new Date();
      const ds = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(d.day).padStart(2, '0')}`;
      if (ds === dateStr) return Object.assign({}, d, { checked: true });
      return d;
    });
    this.setData({ 'calendar.days': days });
  },

  persistLocal() {
    const local = wx.getStorageSync('mineData') || {};
    local.points = this.data.points;
    const dates = this.data.calendar.days.filter(d => d.checked).map(d => {
      const c = this.data.calendar;
      return `${c.year}-${String(c.month).padStart(2, '0')}-${String(d.day).padStart(2, '0')}`;
    });
    local.checkinDates = dates;
    wx.setStorageSync('mineData', local);
  },

  getLevel(points) {
    const levelMap = [
      { min: 0, name: '见习客服', icon: '🆕' },
      { min: 200, name: '初级客服', icon: '🌱' },
      { min: 500, name: '进阶客服', icon: '📈' },
      { min: 1000, name: '资深客服', icon: '💎' },
      { min: 2000, name: '金牌客服', icon: '👑' },
      { min: 4000, name: '首席客服', icon: '🏆' }
    ];
    return [...levelMap].reverse().find(l => points >= l.min) || levelMap[0];
  },

  // 积分规则
  toggleRules() {
    if (!this.data.pointsRules.length) {
      api.getMineRules().then(data => {
        this.setData({ pointsRules: data.rules, showRules: true });
      }).catch(() => {
        this.setData({
          showRules: true,
          pointsRules: [
            { action: '每日打卡', points: '+10', desc: '每天可打卡一次' },
            { action: '完成一次训练', points: '+100', desc: '完成任意模式训练' },
            { action: '训练通过（≥60分）', points: '+50', desc: '额外奖励' },
            { action: '连续打卡3天', points: '+30', desc: '连续奖励' },
            { action: '连续打卡7天', points: '+80', desc: '周奖励' }
          ]
        });
      });
    } else {
      this.setData({ showRules: !this.data.showRules });
    }
  },

  // 导航
  goPhrases() {
    wx.switchTab({ url: '/pages/home/home' });
  },

  goProfile() {
    wx.navigateTo({ url: '/pages/profile/profile' });
  },

  goMistakes() {
    wx.navigateTo({ url: '/pages/mistakes/mistakes' });
  },

  goFavorites() {
    wx.navigateTo({ url: '/pages/phrase-vault/phrase-vault' });
  }
});
