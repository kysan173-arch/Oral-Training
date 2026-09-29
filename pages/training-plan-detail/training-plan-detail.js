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
    /* 防刷分上限：0 = 不限（迁移 023）。不为 0 时学员必须把训练铺到多个场景，
       否则「同一场景反复练」会把完成次数算满但训练量没有真正展开。 */
    const cap = Math.max(0, Number(rawPlan.maxPerScenario) || 0);
    /* 判定用的分数必须用后端下发的 score，不能用综合分重算：计划指定了目标维度时
       score 等于「该维度均分」，而 avgScore 恒为综合分。前端若拿 avgScore 比较，
       会出现「主管看到达标、学员看到未达标」的同一份计划两种结论。 */
    const judgeScore = rawPlan.score === null || rawPlan.score === undefined
      ? avgScore : Number(rawPlan.score);
    const focusLabel = rawPlan.focusDimensionLabel || '';
    /* 后端在「计划指定了目标维度、但计划期内还没有该维度的有效评分」时会回退综合分
       （scoreBasis === 'total'）。回退本身是有意设计——缺失绝不能当 0 分；但学员必须
       知道此刻自己在被拿什么评判，否则会以为自己一直在按目标维度达标。 */
    const scoreFallback = !!focusLabel && rawPlan.scoreBasis === 'total';
    /* 逐次达标（迁移 031）：打开后判定取窗口内最低分，标签与说明必须跟着改，
       否则学员会按「均分」去理解判定结论。 */
    const eachPass = rawPlan.requireEachPass === true;
    const basisWord = eachPass ? '最低分' : '均分';
    const scoreLabel = !focusLabel || scoreFallback ? `综合${basisWord}` : `${focusLabel}${basisWord}`;
    /* 有目标维度时，达标只看这一维——必须写清楚，否则学员会以为靠其他维度拉高综合分也能过 */
    const scoreName = !focusLabel || scoreFallback ? (eachPass ? '最低得分' : '平均得分') : `${focusLabel}得分`;
    const scoreHint = !focusLabel
      ? (eachPass ? '取计划期内上述训练的最低分（要求每次都达标）' : '取计划期内上述训练的综合平均分')
      : (scoreFallback
          ? `计划针对「${focusLabel}」补强，但计划期内还没有该维度的有效评分，当前先按综合${basisWord}判定`
          : `计划针对「${focusLabel}」补强，只看这一维的${eachPass ? '最低分（每次都要达标）' : '均分'}，其他维度不计入`);
    const status = rawPlan.status || (rawPlan.done ? 'done' : rawPlan.expired ? 'expired' : 'pending');
    const countDone = completed >= required;
    const scoreDone = judgeScore >= passRate;

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
        hasFocus: !!focusLabel,
        focusLabel,
        scoreLabel,
        scoreName,
        scoreHint,
        requirementText: `完成 ≥ ${required} 次 · ${focusLabel
          ? (eachPass ? `${focusLabel}每次` : `${focusLabel}均分`)
          : (eachPass ? '每次' : '综合均分')} ≥ ${passRate} 分`,
        countHint: cap > 0
          ? `计划期内、命中适用场景的已完成训练；同一场景最多计入 ${cap} 次，其余次数需练其他场景`
          : '计划期内、命中适用场景的已完成训练',
        ruleSummary: buildRuleSummary(countDone, scoreDone, Math.max(0, required - completed)),
        progressText: `${completed}/${required} 次`,
        progressPercent: Math.max(0, Math.min(100, Math.round(completed / required * 100))),
        avgScoreText: fmt1(avgScore),
        /* 三个数字卡里的「均分」必须显示判定用的那个分，否则学员会拿综合分
           去对照阈值，看到 70 却判未达标。 */
        scoreMetaValue: fmt1(judgeScore),
        countText: `${completed}/${required} 次`,
        scoreText: `${fmt1(judgeScore)}/${passRate} 分`,
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
