const request = require('../../static/api/request.js');

// Mock achievement data (no API in MVP)
const MOCK_ACHIEVEMENTS = [
  { id: 'first_train', name: '初次训练', desc: '完成第1次训练', icon: '🎯', earned: true },
  { id: 'ten_trains', name: '训练达人', desc: '累计完成10次训练', icon: '🏆', earned: false },
  { id: 'high_score', name: '高分突破', desc: '单次得分≥85分', icon: '⭐', earned: false },
  { id: 'checkin_7', name: '坚持之星', desc: '连续打卡7天', icon: '🔥', earned: false },
  { id: 'empathy_master', name: '共情达人', desc: '同理心维度≥85分×3次', icon: '💚', earned: false },
  { id: 'conversion_master', name: '转化高手', desc: '邀约转化维度≥85分×3次', icon: '💎', earned: false }
];

// Mock leaderboard (no API in MVP)
const MOCK_LEADERBOARD = [
  { name: '演示用户', sessions: 12, score: 82.5, isMe: true },
  { name: '优秀客服A', sessions: 25, score: 91.2, isMe: false },
  { name: '优秀客服B', sessions: 18, score: 87.6, isMe: false },
  { name: '资深顾问C', sessions: 30, score: 86.1, isMe: false },
  { name: '潜力新人D', sessions: 8, score: 78.3, isMe: false }
];

function formatDate(date) {
  const y = date.getFullYear();
  const m = date.getMonth() + 1;
  const d = date.getDate();
  return `${y}-${String(m).padStart(2, '0')}-${String(d).padStart(2, '0')}`;
}

function getMonthDays(year, month) {
  return new Date(year, month + 1, 0).getDate();
}

function getMonthFirstDay(year, month) {
  return new Date(year, month, 1).getDay();
}

Page({
  data: {
    // User info
    userInfo: {
      avatar: '/static/image/default-avatar.png',
      nickname: '演示用户',
      level: 1,
      points: 0
    },

    // Check-in
    checkInDays: [],
    todayChecked: false,
    checkInStreak: 0,
    currentYear: 2026,
    currentMonth: 7,
    monthLabel: '2026年7月',

    // Stats
    totalSessions: 0,
    completedSessions: 0,
    averageScore: 0,
    passRate: 0,

    // Achievements
    achievements: MOCK_ACHIEVEMENTS,
    earnedCount: MOCK_ACHIEVEMENTS.filter(a => a.earned).length,

    // Leaderboard
    leaderboard: MOCK_LEADERBOARD,
    showLeaderboard: false,

    loading: true,
    errorMessage: ''
  },

  onLoad() {
    this.loadCheckInData();
    this.loadDashboard();
  },

  onShow() {
    this.loadCheckInData();
    this.loadDashboard();
  },

  // ---- Check-in ----
  loadCheckInData() {
    const now = new Date();
    const year = now.getFullYear();
    const month = now.getMonth();
    const todayStr = formatDate(now);

    this.setData({
      currentYear: year,
      currentMonth: month,
      monthLabel: `${year}年${month + 1}月`
    });

    // Generate calendar grid
    const daysInMonth = getMonthDays(year, month);
    const firstDay = getMonthFirstDay(year, month);
    const checkInList = wx.getStorageSync('checkInDays') || [];

    const days = [];
    for (let i = 0; i < firstDay; i++) {
      days.push({ day: '', isToday: false, checked: false });
    }
    for (let d = 1; d <= daysInMonth; d++) {
      const dateStr = `${year}-${String(month + 1).padStart(2, '0')}-${String(d).padStart(2, '0')}`;
      days.push({
        day: d,
        isToday: dateStr === todayStr,
        checked: checkInList.includes(dateStr)
      });
    }

    const todayChecked = checkInList.includes(todayStr);

    // Calculate streak
    let streak = 0;
    const sorted = [...checkInList].sort().reverse();
    if (sorted.length > 0) {
      let expected = new Date(todayChecked ? now : new Date(now.getTime() - 86400000));
      for (const dateStr of sorted) {
        const expectedStr = formatDate(expected);
        if (dateStr === expectedStr) {
          streak++;
          expected = new Date(expected.getTime() - 86400000);
        } else {
          break;
        }
      }
    }

    this.setData({
      checkInDays: days,
      todayChecked,
      checkInStreak: streak,
      'userInfo.points': checkInList.length * 10
    });

    // Auto-update achievements
    this.updateAchievements(streak, checkInList.length);
  },

  doCheckIn() {
    if (this.data.todayChecked) return;

    const now = new Date();
    const todayStr = formatDate(now);
    let checkInList = wx.getStorageSync('checkInDays') || [];
    if (checkInList.includes(todayStr)) return;

    checkInList.push(todayStr);
    wx.setStorageSync('checkInDays', checkInList);

    wx.showToast({ title: '打卡成功 +10分', icon: 'success' });
    this.loadCheckInData();
  },

  // ---- Dashboard ----
  async loadDashboard() {
    this.setData({ loading: true, errorMessage: '' });
    try {
      const data = await request.get('/dashboard/summary');
      const totalSessions = data.totalSessions || 0;
      const completedSessions = data.completedSessions || 0;
      const averageScore = data.averageScore || 0;
      const passRate = completedSessions > 0
        ? Math.round((data.passRate || 0) * 100)
        : 0;

      this.setData({
        totalSessions,
        completedSessions,
        averageScore,
        passRate,
        loading: false
      });

      this.updateAchievements(null, null, totalSessions, averageScore);
    } catch (error) {
      this.setData({
        loading: false,
        errorMessage: request.getErrorMessage(error, '数据加载失败')
      });
    }
  },

  // ---- Achievements ----
  updateAchievements(streak, checkInCount, totalSessions, avgScore) {
    const updated = this.data.achievements.map(a => {
      switch (a.id) {
        case 'first_train':
          return { ...a, earned: (totalSessions || this.data.totalSessions) >= 1 };
        case 'ten_trains':
          return { ...a, earned: (totalSessions || this.data.totalSessions) >= 10 };
        case 'high_score':
          return { ...a, earned: (avgScore || this.data.averageScore) >= 85 };
        case 'checkin_7':
          return { ...a, earned: (streak || this.data.checkInStreak) >= 7 };
        default:
          return a;
      }
    });
    this.setData({
      achievements: updated,
      earnedCount: updated.filter(a => a.earned).length
    });
  },

  // ---- Navigation ----
  toggleLeaderboard() {
    this.setData({ showLeaderboard: !this.data.showLeaderboard });
  },

  goTraining() {
    wx.switchTab({ url: '/pages/index/index' });
  },

  goHistory() {
    wx.switchTab({ url: '/pages/report/report' });
  },

  goPhraseVault() {
    wx.navigateTo({ url: '/pages/phrase-vault/phrase-vault' });
  },

  onPullDownRefresh() {
    Promise.all([this.loadDashboard()]).finally(() => wx.stopPullDownRefresh());
  }
});
