const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');

/* 分数分档：颜色只跟随分数（≥80 良好绿 / 60–79 中间蓝 / <60 待提升橙） */
const scoreTier = score => (score >= 80 ? 'high' : score >= 60 ? 'mid' : 'low');

const DIMENSIONS = [
  { key: 'knowledgeAccuracy', name: '知识准确性' },
  { key: 'medicalCompliance', name: '医疗合规' },
  { key: 'empathy', name: '同理心' },
  { key: 'needsDiscovery', name: '需求挖掘' },
  { key: 'serviceEtiquette', name: '服务礼仪' }
];

/* 成长曲线五条线的颜色，全部取自 app.wxss 既有调色板，不引入新色。
   末位原为 #9AA6B8（弱灰），白底上几乎看不见，换成同属既有色的 #8B6E4B。 */
const DIMENSION_COLORS = ['#1F3864', '#B97A1E', '#2E8B6C', '#6B7A93', '#8B6E4B'];
/* 相邻维度线型交替（实 / 虚）：五条线全是实线时，只靠颜色区分并不可靠（色觉障碍尤其难），
   虚线是颜色之外的第二个区分维度——组件已支持 dashed，图例也会跟着画成虚线。 */
const DIMENSION_DASHED = [false, true, false, true, false];

/* 数值格式化：整数原样显示，小数保留一位 */
const fmt1 = value => {
  const num = Number(value);
  if (!isFinite(num)) return value === null || value === undefined ? '0' : String(value);
  return num % 1 === 0 ? String(Math.round(num)) : num.toFixed(1);
};

Page({
  data: {
    loading: true,
    detail: null,
    dimensions: [],
    teamAverages: [],
    growthLabels: [],
    growthSeries: [],
    scenarioFilters: [],
    selectedScenarioId: 'all',
    filteredTrend: [],
    inspectItems: [],
    /* 趋势分档（P1-1）：默认只看标准档。混档趋势本身不可比——
       「85 → 72」可能是退步，也可能是主动挑战了更难的一档。 */
    tierFilter: 'standard',
    tierFilters: [
      { id: 'standard', name: '只看标准档' },
      { id: 'all', name: '含进阶档' }
    ],
    growthEmptyText: '',
    /* 成长曲线要显示的维度（默认全选，取消勾选即隐藏该线，避免五条线互相缠绕）。
       WXML 里不能调 indexOf —— 对数据路径调函数会让依赖追踪失效、setData 后不重算且不报错。
       所以「是否选中」在这里预算成布尔字段，模板只做属性访问。 */
    dimensionChips: DIMENSIONS.map(item => ({ key: item.key, name: item.name, selected: true })),
    /* 两个长列表区块的展开态：默认收起以省纵向空间，
       区块头的计数 / 说明与箭头仍常驻，需要时点开。 */
    sections: { growth: false, inspect: false },
    /* 收起时至少露出几条：整块只留标题会看不出内容，
       露 3 条 + 「还有 N 条」既能扫一眼又不占纵向空间。 */
    previewRows: 3
  },

  memberId: '',
  /* 全量趋势存在实例上（含进阶档点），按档位/场景两个维度过滤后渲染 */
  allTrend: [],

  onLoad(options) {
    this.memberId = options.id || '';
    this.loadDetail();
  },

  loadDetail() {
    if (!this.memberId) {
      wx.showToast({ title: '成员标识无效', icon: 'none' });
      return;
    }
    this.setData({ loading: true });
    /* 团队均值作为雷达图对比线：后端没有按需接口，统一拉一次全量 dashboard 即可 */
    Promise.all([
      api.getSupervisorMember(this.memberId),
      api.getSupervisorDashboard({ range: 'all' })
    ]).then(([member, dashboard]) => {
      this.applyMember(member, dashboard);
    }).catch(error => {
      this.setData({ loading: false });
      api.showCenterNotice({ title: error.message || '成员详情加载失败' });
    });
  },

  applyMember(data, dashboard) {
    const dimensions = DIMENSIONS.map(item => {
      const score = Number((data.dimensionAverages || {})[item.key] || 0);
      return Object.assign({}, item, { score, scoreText: fmt1(score), tier: scoreTier(score) });
    });
    const teamAverages = DIMENSIONS.map(item => Number((dashboard.dimensionAverages || {})[item.key] || 0));

    const trend = (data.trend || []).map(item => Object.assign({}, item, {
      scoreText: `${fmt1(item.totalScore)} 分`
    }));
    this.allTrend = trend;

    /* 分档统计卡与进阶挑战率（P1-1）。后端对「没练过进阶档」下发 null，
       前端必须显示「未挑战」而不是 0——那是两种完全不同的信号。 */
    const advancedCount = Number(data.advancedCount) || 0;
    const advancedChallengeRateText = data.advancedChallengeRate === null ||
        data.advancedChallengeRate === undefined
      ? '—' : `${fmt1(data.advancedChallengeRate)}%`;
    let tierStripSubText;
    if (advancedChallengeRateText === '—') {
      tierStripSubText = '还没有已评分的训练，暂无挑战数据。';
    } else if (advancedCount === 0) {
      tierStripSubText = '从未挑战进阶档——可能一直待在舒适区，建议鼓励尝试（进阶成绩不计入计划达标）。';
    } else {
      tierStripSubText = `已挑战 ${advancedCount} 次进阶档 · 进阶成绩不计入计划达标，只进成长趋势。`;
    }

    /* 成长曲线：按档位过滤后计算，默认只画标准档 */
    const growth = this.buildGrowth(trend);

    const scenarioMap = new Map();
    trend.forEach(item => {
      if (!item.scenarioId) return;
      if (!scenarioMap.has(item.scenarioId)) {
        scenarioMap.set(item.scenarioId, { id: item.scenarioId, name: item.scenarioName || item.scenarioId });
      }
    });
    const scenarioFilters = [{ id: 'all', name: '全部场景' }].concat(Array.from(scenarioMap.values()));

    /* 抽查列表（含进行中会话）：进入抽查页才拉取完整对话，本页只列条目 */
    const inspectItems = (data.inspectSessions || []).map(item => Object.assign({}, item, {
      statusText: item.status === 'in_progress' ? '进行中'
        : item.status === 'completed' ? '已完成' : '已放弃',
      roundText: `${item.currentRound}/${item.maxRounds} 轮`,
      scoreText: item.totalScore === null || item.totalScore === undefined ? '—' : `${fmt1(item.totalScore)} 分`
    }));

    const detail = Object.assign({}, data, {
      member: Object.assign({}, data.member, {
        initial: (data.member.displayName || '学').slice(0, 1)
      }),
      averageScoreText: fmt1(data.averageScore),
      /* 两档均分并列：没练过显示「未挑战」，不能显示 0 分 */
      standardScoreText: data.standardAvgScore === null || data.standardAvgScore === undefined
        ? '未挑战' : fmt1(data.standardAvgScore),
      advancedScoreText: data.advancedAvgScore === null || data.advancedAvgScore === undefined
        ? '未挑战' : fmt1(data.advancedAvgScore),
      advancedChallengeRateText,
      tierStripSubText,
      passRateText: fmt1(data.passRate),
      weaknesses: (data.weaknesses || []).map(item => Object.assign({}, item, {
        scoreText: fmt1(item.score)
      })),
      trend
    });

    this.setData({
      detail,
      dimensions,
      teamAverages,
      growthLabels: growth.labels,
      growthSeries: growth.series,
      scenarioFilters,
      inspectItems,
      selectedScenarioId: 'all',
      filteredTrend: growth.timeline
    }, () => {
      this.setData({ loading: false });
    });
  },

  /* 按当前档位 + 场景筛选重算图表与时间线。
     抽成方法是因为两个筛选维度（档位/场景）正交，任何一处变化都要整组重算。 */
  buildGrowth(source) {
    const tierFiltered = source.filter(item =>
      this.data.tierFilter === 'all' || item.difficultyTier !== 'advanced');
    const labels = tierFiltered.map(item => datetime.formatMonthDay(item.date));
    /* 只画被勾选的维度：五条线全上会互相缠绕，单条趋势根本看不出来。
       颜色 / 线型按维度在 DIMENSIONS 里的**原始下标**取，
       所以取消勾选再勾回来时颜色不会串到别的维度上。 */
    const series = DIMENSIONS
      .map((item, dimIndex) => ({ item, dimIndex }))
      .filter(entry => this.data.dimensionChips.some(
        chip => chip.key === entry.item.key && chip.selected))
      .map(entry => ({
        name: entry.item.name,
        type: 'line',
        color: DIMENSION_COLORS[entry.dimIndex],
        dashed: DIMENSION_DASHED[entry.dimIndex],
        axis: 'right',
        values: tierFiltered.map(point => {
          const score = point && point.scores ? point.scores[entry.item.key] : null;
          return Number.isFinite(Number(score)) ? Number(score) : null;
        })
      }));
    /* 时间线在档位过滤之上再叠加场景筛选 */
    const timeline = this.data.selectedScenarioId === 'all'
      ? tierFiltered
      : tierFiltered.filter(item => item.scenarioId === this.data.selectedScenarioId);
    /* 空图要区分「真的没数据」和「被档位过滤掉了」：后者要提示切换开关 */
    let emptyText = '该成员至少完成 2 次训练后，会在这里看到五维得分随时间的变化。';
    if (!tierFiltered.length && source.length) {
      emptyText = '当前筛选下没有标准档记录——该成员只练了进阶档，可切换「含进阶档」查看。';
    }
    return { labels, series, timeline, emptyText };
  },

  refreshTrend() {
    const growth = this.buildGrowth(this.allTrend);
    this.setData({
      growthLabels: growth.labels,
      growthSeries: growth.series,
      filteredTrend: growth.timeline,
      growthEmptyText: growth.emptyText
    });
  },

  selectTierFilter(e) {
    const tierFilter = e.currentTarget.dataset.id;
    if (!tierFilter || tierFilter === this.data.tierFilter) return;
    this.setData({ tierFilter }, () => this.refreshTrend());
  },

  /* 成长曲线维度显隐。至少保留一个维度——全关掉图会变成空白，
     而那看起来像「数据丢了」，不如直接拦住并说明原因。
     key 走 DIMENSIONS 白名单校验，不用 dataset 拼任何数据路径。 */
  toggleDimension(e) {
    const key = e.currentTarget.dataset.key;
    if (!DIMENSIONS.some(item => item.key === key)) return;
    const next = this.data.dimensionChips.map(chip =>
      (chip.key === key ? Object.assign({}, chip, { selected: !chip.selected }) : chip));
    if (!next.some(chip => chip.selected)) {
      api.showCenterNotice({ title: '至少保留一个维度' });
      return;
    }
    this.setData({ dimensionChips: next }, () => this.refreshTrend());
  },

  /* 区块收起 / 展开。key 限定白名单，避免用 dataset 拼出意料之外的 setData 路径。 */
  toggleSection(e) {
    const key = e.currentTarget.dataset.key;
    if (key !== 'growth' && key !== 'inspect') return;
    this.setData({ [`sections.${key}`]: !this.data.sections[key] });
  },

  openInspect(e) {
    const sessionId = e.currentTarget.dataset.id;
    if (!sessionId) return;
    wx.navigateTo({
      url: `/pages/session-inspect/session-inspect?memberId=${encodeURIComponent(this.memberId)}&sessionId=${encodeURIComponent(sessionId)}`
    });
  },

  selectScenarioFilter(e) {
    const selectedScenarioId = e.currentTarget.dataset.id;
    if (!selectedScenarioId || selectedScenarioId === this.data.selectedScenarioId) return;
    /* 走统一重算：场景筛选与档位筛选正交，时间线要同时满足两个条件 */
    this.setData({ selectedScenarioId }, () => this.refreshTrend());
  }
});
