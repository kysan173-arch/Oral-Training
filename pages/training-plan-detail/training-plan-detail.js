const api = require('../../utils/api.js');

/* 计划展示派生统一走 utils/plan.js，本页不再维护副本（口径以 plan.js 为准） */
const { STATUS_TEXT, fmt1, formatDue, buildCountdown, buildRuleSummary } = require('../../utils/plan.js');

/* 难度与分类口径与学员端、发布页保持一致，不另起一套中文名 */
const DIFFICULTY_LABELS = {
  beginner: '初级',
  basic: '初级',
  intermediate: '中级',
  advanced: '高级'
};

const CATEGORY_LABELS = {
  consultation: '咨询解答',
  price_negotiation: '价格异议',
  complaint_handling: '投诉安抚',
  recommendation: '项目推荐'
};

Page({
  data: {
    loading: true,
    plan: null,
    scenarios: [],
    allScenarios: false,
    scenarioCaption: '',
    catalogFailed: false,
    missing: false,
    loadError: false,
    loadErrorMsg: ''
  },

  onLoad(options) {
    const id = options && options.id ? decodeURIComponent(options.id) : '';
    if (!id) {
      this.setData({ loading: false, missing: true });
      return;
    }
    this.planId = id;
    this.loadDetail();
  },

  loadDetail() {
    this.setData({ loading: true, loadError: false, missing: false });
    /* 计划明细复用学员侧的 /learning/training-plans：它已经带回了计划期内的
       完成次数与均分，无需额外的详情接口。场景目录只用来把 scenarioIds 翻成
       可读名称，属于锦上添花，拉取失败不应让整页失败。 */
    Promise.all([
      api.getLearnerTrainingPlans(),
      api.getScenarios().catch(() => null)
    ]).then(([data, catalog]) => {
      const plan = (data.plans || []).find(item => String(item.id) === String(this.planId));
      if (!plan) {
        this.setData({ loading: false, missing: true });
        return;
      }
      this.applyPlan(plan, catalog);
    }).catch(error => {
      this.setData({
        loading: false,
        loadError: true,
        loadErrorMsg: error.message || '计划明细加载失败'
      });
    });
  },

  applyPlan(rawPlan, catalog) {
    const required = Math.max(1, Number(rawPlan.requiredCount) || 1);
    const completed = Number(rawPlan.completedCount) || 0;
    const passRate = Number(rawPlan.requiredPassRate) || 0;
    const avgScore = Number(rawPlan.avgScore) || 0;
    const status = rawPlan.status || (rawPlan.done ? 'done' : rawPlan.expired ? 'expired' : 'pending');
    const countDone = completed >= required;
    const scoreDone = avgScore >= passRate;

    const catalogMap = {};
    ((catalog && catalog.items) || []).forEach(item => { catalogMap[String(item.id)] = item; });
    const rawIds = Array.isArray(rawPlan.scenarioIds) ? rawPlan.scenarioIds : [];
    const allScenarios = rawIds.length === 0;
    const scenarios = rawIds.map(id => {
      const matched = catalogMap[String(id)];
      const best = matched ? Number(matched.bestScore) || 0 : 0;
      return {
        id,
        name: matched && matched.name ? matched.name : id,
        difficultyLabel: DIFFICULTY_LABELS[(matched && matched.difficulty) || ''] || '',
        categoryLabel: CATEGORY_LABELS[(matched && matched.category) || ''] || '',
        bestScoreText: matched ? `${fmt1(best)} 分` : '—',
        trained: matched ? best > 0 : false
      };
    });

    this.setData({
      loading: false,
      missing: false,
      catalogFailed: !catalog && !allScenarios,
      allScenarios,
      scenarios,
      scenarioCaption: allScenarios ? '不限场景' : `共 ${scenarios.length} 项`,
      plan: Object.assign({}, rawPlan, {
        periodText: rawPlan.period === 'week' ? '按周' : '按月',
        dueText: formatDue(rawPlan.dueAt),
        countdownText: buildCountdown(rawPlan.dueAt),
        statusKey: status,
        statusText: STATUS_TEXT[status] || '待完成',
        requiredCount: required,
        requirementText: `完成 ≥ ${required} 次 · 平均 ≥ ${passRate} 分`,
        ruleSummary: buildRuleSummary(countDone, scoreDone, Math.max(0, required - completed)),
        progressText: `${completed}/${required} 次`,
        progressPercent: Math.max(0, Math.min(100, Math.round(completed / required * 100))),
        avgScoreText: fmt1(avgScore),
        countText: `${completed}/${required} 次`,
        scoreText: `${fmt1(avgScore)}/${passRate} 分`,
        countDone,
        scoreDone
      })
    });
  },

  retry() {
    this.loadDetail();
  },

  goTraining() {
    wx.switchTab({ url: '/pages/index/index' });
  },

  goPlans() {
    const pages = getCurrentPages();
    if (pages.length > 1) {
      wx.navigateBack();
      return;
    }
    wx.redirectTo({ url: '/pages/training-plans/training-plans' });
  }
});
