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
    mine: null,
    calendarDays: [],
    streakText: ''
  },

  onShow() { this.loadMine(); },

  loadMine() {
    this.setData({ loading: true });
    api.getLearningMine().then(data => {
      const mine = Object.assign({}, data, {
        stats: Object.assign({}, data.stats, { averageScore: api.formatScore(data.stats.averageScore) })
      });
      this.setData({
        mine,
        calendarDays: buildCalendar(data.checkin),
        streakText: data.checkin.streakDays > 0 ? `连续 ${data.checkin.streakDays} 天` : '从今天开始记录',
        loading: false
      });
    }).catch(error => {
      this.setData({ loading: false });
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
      this.loadMine();
    }).catch(error => {
      this.setData({ checkingIn: false });
      wx.showToast({ title: error.message || '签到失败', icon: 'none' });
    });
  },

  goProfile() { wx.navigateTo({ url: '/pages/profile/profile' }); },
  goMistakes() { wx.navigateTo({ url: '/pages/mistakes/mistakes' }); },
  goPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); },
  goFavorites() { wx.navigateTo({ url: '/pages/phrases/phrases?favorites=1' }); }
});
