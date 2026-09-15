const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');
/* 数值格式化与主管端派生（辅导判定 / 等级 / 原因文案）都走单一来源，
   本页不再自持副本——否则首页工作台与这里会给出不一致的结论。 */
const {
  SEVERITY_TEXT,
  COACHING_IDLE_DAYS,
  PASS_SCORE,
  memberLevel,
  isCoachingNeeded,
  coachingProfile
} = require('../../utils/supervisor.js');

const DIMENSIONS = [
  { key: 'knowledgeAccuracy', name: '知识准确性' },
  { key: 'medicalCompliance', name: '医疗合规' },
  { key: 'empathy', name: '同理心' },
  { key: 'needsDiscovery', name: '需求挖掘' },
  { key: 'serviceEtiquette', name: '服务礼仪' }
];

/* 分数分档：颜色只跟随分数（≥80 良好绿 / 60–79 中间蓝 / <60 待提升橙） */
const scoreTier = score => (score >= 80 ? 'high' : score >= 60 ? 'mid' : 'low');

/* 团队级薄弱项：五维最低项 + 通过率最低场景 */
const coachingSuggestions = dashboard => {
  const suggestions = [];
  const dimensions = dashboard.dimensionAverages || {};
  const weakest = DIMENSIONS.reduce((current, item) => {
    const score = Number(dimensions[item.key] || 0);
    return !current || score < current.score ? Object.assign({}, item, { score }) : current;
  }, null);
  if (weakest && weakest.score < 70) {
    suggestions.push({
      title: `优先关注：${weakest.name}`,
      text: `团队均值为 ${api.formatScore(weakest.score)} 分。建议安排围绕该能力的短场景复练，并在复盘中关注具体表达。`,
      severity: weakest.score < 60 ? 'high' : 'medium'
    });
  }
  /* 只在场景「确实有已评分会话」时才参与比较：passRate 为 null 表示该场景
     还没有任何已评分报告，拿它参与比较会把「未评分」误判成「通过率最低」。 */
  const weakScene = (dashboard.scenarioStats || [])
    .filter(item => item.total > 0 && item.passRate !== null && item.passRate !== undefined)
    .reduce((current, item) => !current || item.passRate < current.passRate ? item : current, null);
  if (weakScene && weakScene.passRate < 70) {
    suggestions.push({
      title: `重点场景：${weakScene.scenarioName}`,
      text: `该场景完成 ${weakScene.total} 次，通过率 ${api.formatScore(weakScene.passRate)}%。可优先组织该场景的针对性练习。`,
      severity: weakScene.passRate < 50 ? 'high' : 'medium'
    });
  }
  if (!suggestions.length) {
    suggestions.push({ title: '整体表现稳定', text: '当前没有低于关注阈值的聚合指标，可继续用真实训练记录观察变化。', severity: 'normal' });
  }
  return suggestions;
};

/* 违规词派生：条形宽度相对当前最大值，避免小样本被拉满 */
const normalizePhrases = phrases => {
  const max = Math.max(1, ...(phrases || []).map(item => Number(item.count) || 0));
  return (phrases || []).map(item => Object.assign({}, item, {
    barWidth: Math.max(6, Math.round((Number(item.count) || 0) / max * 100))
  }));
};

/* 排行榜派生：前三名给奖牌序号，其余显示数字名次 */
const MEDALS = { 1: '①', 2: '②', 3: '③' };
const normalizeLeaderboard = (entries, unit) => (entries || []).map(item => Object.assign({}, item, {
  rankText: MEDALS[item.rank] || String(item.rank),
  top: item.rank <= 3,
  scoreText: `${api.formatScore(item.score)}${unit || ''}`
}));

/* 成员列表派生：搜索 → 筛选 → 排序，全部在前端完成（接口无这些参数，数据量 < 100） */
const deriveMembers = state => {
  const keyword = String(state.memberKeyword || '').trim();
  const list = state.rawMembers.filter(item => {
    if (keyword && String(item.displayName || '').indexOf(keyword) < 0) return false;
    if (state.memberFilter === 'coaching') return item.needCoaching;
    if (state.memberFilter === 'passed') return !item.needCoaching;
    return true;
  });
  const compare = {
    recent: (a, b) => {
      /* 用 datetime.toMs 而非 Date.parse：避免部分机型对时间串的解析差异 */
      const left = datetime.toMs(a.lastTrainingDate);
      const right = datetime.toMs(b.lastTrainingDate);
      /* 无有效日期排末尾；两边都无则视为并列，不让 NaN 参与差值比较 */
      const leftKey = isFinite(left) ? left : -Infinity;
      const rightKey = isFinite(right) ? right : -Infinity;
      if (leftKey === rightKey) return 0;
      return rightKey - leftKey;
    },
    score: (a, b) => Number(b.averageScore) - Number(a.averageScore),
    count: (a, b) => Number(b.totalSessions) - Number(a.totalSessions)
  };
  return list.sort(compare[state.memberSort] || compare.recent);
};

Page({
  data: {
    isAdmin: false,
    loading: true,
    timeRange: 'month',
    timeFilters: [
      { id: 'week', name: '本周' }, { id: 'month', name: '本月' },
      { id: 'quarter', name: '本季度' }, { id: 'all', name: '全部' }
    ],
    /* 与文档 3.2 对应；「培训运营」已拆为独立 Tab 页 pages/admin-training */
    activeTab: 'overview',
    moduleTabs: [
      { id: 'overview', name: '团队洞察' },
      { id: 'member', name: '成员管理' },
      { id: 'report', name: '数据报表' }
    ],
    rangeName: '本月',
    supervisor: null,
    /* 主管看板请求失败标记：失败时 wxml 三个分支都不成立，页面只剩一行
       免责声明（等同白页）。给错误态 + 重试，与报表的 reportError 同模式。 */
    supervisorFailed: false,
    metricCards: [],
    trendLabels: [],
    trendSeries: [],
    trendPoint: null,
    teamWarnings: [],
    personalWarnings: [],
    members: [],
    rawMembers: [],
    memberKeyword: '',
    memberSort: 'recent',
    memberFilter: 'all',
    sortFilters: [
      { id: 'recent', name: '最近训练' },
      { id: 'score', name: '平均得分' },
      { id: 'count', name: '训练次数' }
    ],
    memberFilters: [
      { id: 'all', name: '全部' },
      { id: 'passed', name: '已达标' },
      { id: 'coaching', name: '待辅导' }
    ],
    memberCountText: '',
    suggestions: [],
    /* ── 数据报表（文档 3.2 第四模块） ── */
    reportLoaded: false,
    reportLoading: false,
    /* 报表请求失败标记：失败时显示错误态+重试，
       不能落进「本期没有违规记录」的空态文案（零违规≠加载失败） */
    reportError: false,
    reportRange: 'month',
    reportRangeFilters: [
      { id: 'week', name: '本周' }, { id: 'month', name: '本月' },
      { id: 'quarter', name: '本季度' }, { id: 'all', name: '全部' }
    ],
    phraseCategories: [],
    sceneCategoryFilters: [],
    reportCategory: '',
    phrases: [],
    phraseTotal: 0,
    leaderboardDimension: 'weekly_sessions',
    leaderboardDimensions: [
      { id: 'weekly_sessions', name: '周训练次数', unit: '次' },
      { id: 'monthly_avg', name: '月平均分', unit: '分' },
      { id: 'completed_scenarios', name: '累计通关', unit: '次' },
      { id: 'streak_days', name: '连续打卡', unit: '天' }
    ],
    leaderboard: [],
    leaderboardExpanded: false,
    personal: { totalCount: 0, completedCount: 0, averageScore: 0, sceneStats: [], dimensionAverages: [], recentSessions: [] }
  },

  onShow() {
    /* 每次显示都同步 tabBar 角色列表，避免切换身份后残留另一套导航 */
    const tabBar = typeof this.getTabBar === 'function' ? this.getTabBar() : null;
    if (tabBar) {
      tabBar.applyRoleList();
      tabBar.setData({ selected: 2 });
    }
    this.loadPage();
  },

  /* 知识与服务管理后台入口（master 独有，页面已注册在 app.json） */
  goKnowledgeAdmin() {
    wx.navigateTo({ url: '/pages/knowledge-admin/knowledge-admin' });
  },

  loadPage() {
    this.setData({ loading: true });
    api.ensureAuthenticated().then(() => {
      const user = api.getCurrentUser();
      if (user && user.role === 'admin') {
        /* 报表 Tab 首次进入才懒加载；已加载过则在 onShow 时刷新。 */
        this.setData({ isAdmin: true }, () => {
          this.loadSupervisor();
          if (this.data.reportLoaded) this.loadReports();
        });
        return;
      }
      this.setData({ isAdmin: false }, () => this.loadPersonal());
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '登录状态获取失败', icon: 'none' });
    });
  },

  loadSupervisor() {
    /* 请求版本号防竞态：连续切时间维度 / 反复进出页面时，
       旧响应若晚回不得覆盖新数据（master 的口径，移植到本页骨架）。 */
    this.supervisorRequestVersion = (this.supervisorRequestVersion || 0) + 1;
    const requestVersion = this.supervisorRequestVersion;
    const requestedRange = this.data.timeRange;
    Promise.all([
      api.getSupervisorDashboard({ range: requestedRange }),
      api.getSupervisorMembers({ limit: 100 })
    ]).then(([supervisor, memberData]) => {
      if (requestVersion !== this.supervisorRequestVersion || requestedRange !== this.data.timeRange) return;
      this.applySupervisor(supervisor, memberData);
    }).catch(error => {
      if (requestVersion !== this.supervisorRequestVersion || requestedRange !== this.data.timeRange) return;
      this.setData({ loading: false, supervisorFailed: true });
      wx.showToast({ title: error.message || '主管数据加载失败', icon: 'none' });
    });
  },

  /* 主管看板失败后的自救入口 */
  retrySupervisor() {
    this.setData({ supervisorFailed: false, loading: true });
    this.loadSupervisor();
  },

  applySupervisor(supervisor, memberData) {
    const rangeFilter = (this.data.timeFilters || []).filter(item => item.id === this.data.timeRange)[0];
    const rangeName = rangeFilter ? rangeFilter.name : '本月';
    const dimensionAverages = DIMENSIONS.map(item => {
      const value = Number((supervisor.dimensionAverages || {})[item.key] || 0);
      const tier = scoreTier(value);
      return Object.assign({}, item, { value, valueText: api.formatScore(value), tier });
    });
    const maxSceneTotal = Math.max(1, ...(supervisor.scenarioStats || []).map(item => item.total));
    /* 场景可能「有已完成会话但一条都没评分」：此时后端给 null，
       展示层必须区分「暂无评分」和「0 分」，否则会把未评分场景标成需重点训练。 */
    const scored = value => value !== null && value !== undefined;
    const scenarioStats = (supervisor.scenarioStats || []).map(item => Object.assign({}, item, {
      barWidth: Math.max(0, Math.min(100, Number(item.passRate) || 0)),
      totalWidth: Math.max(4, item.total / maxSceneTotal * 100),
      averageScoreText: scored(item.averageScore)
        ? `${api.formatScore(item.averageScore)} 分` : '暂无评分',
      passRateText: scored(item.passRate) ? `${api.formatScore(item.passRate)}%` : '暂无',
      weak: scored(item.passRate) && Number(item.passRate) < 60
    }));

    const trendPoints = supervisor.trend || [];
    const trendLabels = trendPoints.map(item => datetime.formatMonthDay(item.date));
    const trendSeries = [
      {
        name: '训练次数',
        type: 'bar',
        axis: 'left',
        color: '#1F3864',
        values: trendPoints.map(item => Number(item.count) || 0)
      },
      {
        name: '团队均分',
        type: 'line',
        axis: 'right',
        color: '#B97A1E',
        /* 没有已完成报告的当天不画点，避免 0 分被读成真实下滑 */
        values: trendPoints.map(item => (Number(item.averageScore) > 0 ? Number(item.averageScore) : null))
      }
    ];

    const rawMembers = (memberData.members || []).map(item => {
      const member = Object.assign({}, item, {
        initial: (item.displayName || '学').slice(0, 1),
        averageScoreText: api.formatScore(item.averageScore),
        passRateText: api.formatScore(item.passRate),
        latestText: item.lastTrainingDate ? `最近训练：${item.lastTrainingDate}` : '暂未开始训练'
      });
      const needCoaching = isCoachingNeeded(member);
      const profile = needCoaching ? coachingProfile(member) : null;
      return Object.assign(member, {
        levelText: memberLevel(member),
        needCoaching,
        coachingText: profile ? profile.text : '',
        coachingSeverity: profile ? profile.severity : ''
      });
    });

    const suggestions = coachingSuggestions(supervisor).map(item => Object.assign({}, item, {
      severityText: SEVERITY_TEXT[item.severity] || ''
    }));
    const coachingMembers = rawMembers.filter(item => item.needCoaching);
    const teamWarnings = suggestions.concat(coachingMembers.length ? [{
      title: `${coachingMembers.length} 名成员待辅导`,
      text: `判定标准：连续 ${COACHING_IDLE_DAYS} 天未训练，或平均分低于 ${PASS_SCORE} 分。可在下方直接查看对应成员。`,
      severity: coachingMembers.length >= 3 ? 'high' : 'medium',
      severityText: coachingMembers.length >= 3 ? '优先处理' : '建议关注'
    }] : []);
    const personalWarnings = coachingMembers.map(item => Object.assign({
      initial: item.initial,
      severityText: SEVERITY_TEXT[item.coachingSeverity] || '建议关注'
    }, item, { text: item.coachingText, severity: item.coachingSeverity }));

    const normalized = Object.assign({}, supervisor, {
      dimensionAverages,
      scenarioStats,
      averageScoreText: supervisor.averageScore === null || supervisor.averageScore === undefined
        ? '暂无' : api.formatScore(supervisor.averageScore),
      passRateText: supervisor.passRate === null || supervisor.passRate === undefined
        ? '暂无' : `${api.formatScore(supervisor.passRate)}%`
    });

    /* 指标卡：学员数是全量口径，其余三项跟随时间维度 */
    const metricCards = [
      { key: 'students', icon: '/static/image/data/students.png', value: supervisor.studentCount, label: '总学员数', hint: '全量活跃账户' },
      { key: 'sessions', icon: '/static/image/data/sessions.png', value: supervisor.totalSessions, label: '总训练次数', hint: `${rangeName}累计` },
      { key: 'score', icon: '/static/image/data/score.png', value: normalized.averageScoreText, label: '团队平均分', hint: '已完成报告的均值' },
      { key: 'pass', icon: '/static/image/data/target.png', value: normalized.passRateText, label: '团队通关率', hint: '综合分 ≥ 60 占比' }
    ];

    this.setData({
      supervisor: normalized,
      supervisorFailed: false,
      rangeName,
      metricCards,
      trendLabels,
      trendSeries,
      trendPoint: null,
      teamWarnings,
      personalWarnings,
      rawMembers
    }, () => {
      this.applyMemberFilter();
      this.setData({ loading: false });
    });
  },

  loadPersonal() {
    api.getDashboard().then(data => {
      const totalSceneCount = (data.scenarioStats || []).reduce((sum, s) => sum + s.trainingCount, 0) || 1;
      const dimensionAverages = DIMENSIONS.map(item => {
        const value = Math.round(Number((data.dimensionAverages || {})[item.key] || 0));
        return Object.assign({}, item, { value, tier: scoreTier(value) });
      });
      const weakest = dimensionAverages.length
        ? dimensionAverages.reduce((prev, curr) => prev.value <= curr.value ? prev : curr) : null;
      const strongest = dimensionAverages.length
        ? dimensionAverages.reduce((prev, curr) => prev.value >= curr.value ? prev : curr) : null;
      const completionRate = data.totalSessions > 0
        ? Math.round(data.completedSessions / data.totalSessions * 100) : 0;
      const recentSessions = (data.recentSessions || []).map(item => {
        const statusText = item.status === 'in_progress' ? '进行中'
          : item.status === 'abandoned' ? '已放弃'
          : item.evaluationStatus === 'generating' ? '报告生成中'
          : item.evaluationStatus === 'failed' ? '报告失败' : '已完成';
        return Object.assign({}, item, {
          statusText,
          updatedAtText: datetime.formatDateTime(item.updatedAt)
        });
      });
      const avgScore = typeof data.averageScore === 'number' ? data.averageScore : 0;
      const personal = {
        totalCount: data.totalSessions,
        completedCount: data.completedSessions,
        averageScore: api.formatScore(data.averageScore),
        focalScoreRing: Math.max(0, Math.min(100, avgScore)),
        completionRate,
        sceneStats: (data.scenarioStats || []).map(item => ({
          id: item.scenarioId,
          name: item.scenarioName,
          count: item.trainingCount,
          barWidth: item.trainingCount / totalSceneCount * 100
        })),
        dimensionAverages,
        weakestDimension: weakest,
        strongestDimension: strongest,
        dimensionSummaryText: weakest && strongest && weakest.key !== strongest.key
          ? `你最强的是${strongest.name}（${strongest.value}分），可以多练${weakest.name}`
          : '',
        weaknessActionText: weakest ? `建议在后续训练中重点关注「${weakest.name}」能力提升` : '',
        recentSessions
      };
      this.setData({ personal, loading: false });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '数据加载失败', icon: 'none' });
    });
  },

  selectTimeRange(e) {
    const timeRange = e.currentTarget.dataset.id;
    if (!timeRange || timeRange === this.data.timeRange) return;
    this.setData({ timeRange, loading: true }, () => this.loadSupervisor());
  },

  switchModule(e) {
    const activeTab = e.currentTarget.dataset.id;
    if (!activeTab || activeTab === this.data.activeTab) return;
    const needReports = activeTab === 'report' && !this.data.reportLoaded;
    /* 与切 Tab 同一次 setData 打开 loading，避免先闪一帧空状态 */
    const patch = { activeTab };
    if (needReports) patch.reportLoading = true;
    this.setData(patch, () => {
      if (needReports) this.loadReports();
    });
    wx.pageScrollTo({ scrollTop: 0, duration: 0 });
  },

  gotoOverview() {
    this.setData({ activeTab: 'overview' });
    wx.pageScrollTo({ scrollTop: 0, duration: 0 });
  },

  /* ── 数据报表 ── */
  loadReports() {
    this.setData({ reportLoading: true });
    const phraseParams = { range: this.data.reportRange, limit: 10 };
    if (this.data.reportCategory) phraseParams.category = this.data.reportCategory;
    Promise.all([
      api.getSupervisorForbiddenPhrases(phraseParams),
      api.getSupervisorLeaderboard({ dimension: this.data.leaderboardDimension, limit: 50 })
    ]).then(([phraseData, boardData]) => {
      const filters = (this.data.leaderboardDimensions || [])
        .filter(item => item.id === this.data.leaderboardDimension)[0];
      this.setData({
        phrases: normalizePhrases(phraseData.phrases || []),
        phraseTotal: Number(phraseData.countedViolations) || 0,
        phraseCategories: phraseData.violationCategories || [],
        sceneCategoryFilters: [{ id: '', name: '全部场景' }].concat(phraseData.sceneCategories || []),
        leaderboard: normalizeLeaderboard(boardData.entries || [], filters ? filters.unit : ''),
        reportLoaded: true,
        reportLoading: false,
        reportError: false
      });
    }).catch(error => {
      this.setData({ reportLoading: false, reportError: true });
      wx.showToast({ title: error.message || '报表数据加载失败', icon: 'none' });
    });
  },

  selectReportRange(e) {
    const reportRange = e.currentTarget.dataset.id;
    if (!reportRange || reportRange === this.data.reportRange) return;
    this.setData({ reportRange }, () => this.loadReports());
  },

  selectReportCategory(e) {
    const reportCategory = e.currentTarget.dataset.id || '';
    if (reportCategory === this.data.reportCategory) return;
    this.setData({ reportCategory }, () => this.loadReports());
  },

  openPhraseMembers(e) {
    const category = e.currentTarget.dataset.id;
    if (!category) return;
    wx.navigateTo({
      url: `/pages/admin-phrase-members/admin-phrase-members?category=${encodeURIComponent(category)}&range=${this.data.reportRange}`
    });
  },

  selectLeaderboardDimension(e) {
    const leaderboardDimension = e.currentTarget.dataset.id;
    if (!leaderboardDimension || leaderboardDimension === this.data.leaderboardDimension) return;
    this.setData({ leaderboardDimension, leaderboardExpanded: false }, () => this.loadReports());
  },

  toggleLeaderboard() {
    this.setData({ leaderboardExpanded: !this.data.leaderboardExpanded });
  },

  resetMemberFilter() {
    this.setData({ memberKeyword: '', memberFilter: 'all', memberSort: 'recent' }, () => this.applyMemberFilter());
  },

  /* 文档 1.2：支持点击趋势图下钻到具体日期 */
  onTrendTap(e) {
    const index = e.detail && e.detail.index;
    if (index === undefined || index === null) return;
    const labels = this.data.trendLabels || [];
    const series = this.data.trendSeries || [];
    const counts = (series[0] || {}).values || [];
    const scores = (series[1] || {}).values || [];
    const score = scores[index];
    this.setData({
      trendPoint: {
        date: labels[index] || '',
        count: Number(counts[index]) || 0,
        scoreText: score === null || score === undefined ? '暂无评分' : `${api.formatScore(score)} 分`
      }
    });
  },

  applyMemberFilter() {
    const members = deriveMembers(this.data);
    this.setData({
      members,
      memberCountText: `共 ${members.length} 人 · 待辅导 ${this.data.rawMembers.filter(item => item.needCoaching).length} 人`
    });
  },

  onMemberKeyword(e) {
    this.setData({ memberKeyword: e.detail.value || '' }, () => this.applyMemberFilter());
  },

  clearMemberKeyword() {
    this.setData({ memberKeyword: '' }, () => this.applyMemberFilter());
  },

  selectMemberSort(e) {
    const memberSort = e.currentTarget.dataset.id;
    if (!memberSort || memberSort === this.data.memberSort) return;
    this.setData({ memberSort }, () => this.applyMemberFilter());
  },

  selectMemberFilter(e) {
    const memberFilter = e.currentTarget.dataset.id;
    if (!memberFilter || memberFilter === this.data.memberFilter) return;
    this.setData({ memberFilter }, () => this.applyMemberFilter());
  },

  openMember(e) {
    const memberId = e.currentTarget.dataset.id;
    if (!memberId) return;
    wx.navigateTo({ url: `/pages/member-detail/member-detail?id=${encodeURIComponent(memberId)}` });
  },

  viewHistory() {
    wx.navigateTo({ url: '/pages/report/report' });
  },

  openRecentSession(e) {
    const sessionId = e.currentTarget.dataset.id;
    if (!sessionId) return;
    wx.navigateTo({ url: `/pages/result/result?sessionId=${encodeURIComponent(sessionId)}` });
  },

  goTraining() {
    wx.switchTab({ url: '/pages/home/home' });
  }
});
