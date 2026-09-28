const api = require('../../utils/api.js');
const planUtil = require('../../utils/plan.js');
const scenarioUtil = require('../../utils/scenario.js');

/* 展示用的分值格式化：后端 avgDimensionScore 可能保留多位小数。
   与 api.formatScore 的分工不同——那个对空值返回「暂无评分」，
   这里空值要显示「还没练过」（语义不同，不能合并）。 */
const fmt1 = value => {
  const number = Number(value);
  return Number.isFinite(number) ? String(Number(number.toFixed(1))) : '';
};

/* 一条候选场景 → 视图字段。
   WXML 里不能对数据路径调用方法（依赖追踪失效且不报错），
   所以「比重百分比 / 其他维度文案 / 按钮文案」一律在这里算成字段。 */
const buildCandidate = (raw, dimension) => {
  const others = (raw.dimensionFocus || [])
    .filter(entry => entry && entry.key && entry.key !== dimension)
    .map(entry => ({
      key: entry.key,
      name: entry.name || entry.key,
      percent: Math.round(Number(entry.weight) * 100)
    }));
  const weight = Number(raw.dimensionWeight);
  const active = raw.activeSession || null;
  /* null 与 0 分必须分开：null 是「这个场景还没练过」，
     显示成 0 分会被读成「练过但很差」，是两种完全不同的信号。 */
  const hasHistory = raw.avgDimensionScore !== null && raw.avgDimensionScore !== undefined;
  const completedCount = Number(raw.completedCount) || 0;
  return Object.assign({}, raw, {
    categoryLabel: scenarioUtil.categoryName(raw.category),
    difficultyLabel: scenarioUtil.difficultyLabel(raw.difficulty),
    weightPercent: Number.isFinite(weight) ? Math.round(weight * 100) : 0,
    hasOthers: others.length > 0,
    othersText: others.map(item => `${item.name} ${item.percent}%`).join('、'),
    hasHistory,
    historyText: hasHistory ? `${fmt1(raw.avgDimensionScore)} 分` : '还没练过',
    hasCompleted: completedCount > 0,
    completedText: completedCount > 0 ? `已练 ${completedCount} 次` : '',
    active,
    actionText: active ? '继续这次训练' : (hasHistory ? '再练一次' : '开始训练')
  });
};

Page({
  data: {
    dimension: '',
    dimensionLabel: '',
    loading: true,
    failed: false,
    items: [],
    total: 0,
    /* 该维度恰好被某个进行中的主管计划覆盖时，说明「练这些会同时推进计划」。
       不是「别练了去待办」——按维度复练本来就在推进那个计划。 */
    planBanner: null
  },

  onLoad(query) {
    const dimension = query && query.dimension ? String(query.dimension) : '';
    const name = query && query.name ? decodeURIComponent(String(query.name)) : '';
    if (!dimension) {
      /* 参数缺失是调用方写错了，不能静默显示「暂无推荐」——
         那会让一个 bug 看起来像「系统没数据」。 */
      this.setData({ loading: false, failed: true });
      return;
    }
    this.setData({ dimension, dimensionLabel: name });
    /* 逻辑判断用实例字段而不是 this.data：setData 虽然同步更新 this.data，
       但页面逻辑不该依赖渲染状态的时序。 */
    this.dimension = dimension;
    wx.setNavigationBarTitle({ title: name ? `复练 ${name}` : '弱项复练' });
  },

  onShow() {
    /* 每次显示都重拉：从训练页返回时该场景的历史均分与「已练次数」都变了 */
    if (!this.dimension) return;
    this.setData({ dimension: this.dimension });
    this.load();
  },

  load() {
    const dimension = this.dimension;
    if (!dimension) return;
    this.setData({ loading: true, failed: false });
    api.getRetrainCandidates(dimension).then(data => {
      const items = (data.items || []).map(raw => buildCandidate(raw, dimension));
      this.setData({
        /* 后端下发的中文名是权威口径，覆盖 onLoad 里从上一个页面带过来的 */
        dimensionLabel: data.dimensionLabel || this.data.dimensionLabel,
        items,
        total: Number(data.total) || items.length,
        loading: false,
        failed: false
      }, () => this.loadPlanBanner());
    }).catch(error => {
      this.setData({ loading: false, failed: true });
      api.showCenterNotice({ title: error.message || '推荐场景加载失败' });
    });
  },

  /* 计划横幅：只找「进行中 + 目标维度正是当前维度」的计划。
     维度不匹配的计划不在这里提——泛泛显示「你有待办计划」是噪音。 */
  loadPlanBanner() {
    const dimension = this.dimension;
    api.getLearnerTrainingPlans().then(data => {
      const matched = (data.plans || [])
        .map(planUtil.normalizePlan)
        .filter(item => item.focusDimension === dimension && item.status === 'pending')
        /* dueMs 解析失败是 NaN，直接相减会让排序结果不确定（与 pickPlanNotice 同处理） */
        .sort((left, right) =>
          (isFinite(left.dueMs) ? left.dueMs : Infinity) -
          (isFinite(right.dueMs) ? right.dueMs : Infinity))[0];
      if (!matched) {
        this.setData({ planBanner: null });
        return;
      }
      this.setData({
        planBanner: {
          id: matched.id,
          title: matched.title,
          countdownText: matched.countdownText,
          /* 与训练中心横幅同一口径（ruleSummary），不另造文案 */
          progressText: matched.ruleSummary,
          progressPercent: matched.progressPercent
        }
      });
    }).catch(() => {
      /* 横幅是增强信息，拉不到就不显示，不打断主流程 */
      this.setData({ planBanner: null });
    });
  },

  openPlan() {
    const banner = this.data.planBanner;
    if (!banner || !banner.id) return;
    wx.navigateTo({
      url: `/pages/training-plan-detail/training-plan-detail?id=${encodeURIComponent(banner.id)}`
    });
  },

  /* 已有进行中会话必须走「继续」：同一场景的进行中会话有唯一约束，
     直接新建会被后端拒绝，学员会看到一条看不懂的报错。 */
  startTraining(e) {
    const id = e.currentTarget.dataset.id;
    const candidate = this.data.items.find(item => item.id === id);
    if (!candidate) return;
    if (candidate.active) {
      wx.navigateTo({ url: `/pages/training/training?sessionId=${candidate.active.id}` });
      return;
    }
    if (this.creating) return;
    this.creating = true;
    wx.showLoading({ title: '准备训练…', mask: true });
    api.createSession(id, {}).then(data => {
      wx.hideLoading();
      this.creating = false;
      wx.navigateTo({ url: `/pages/training/training?sessionId=${data.session.id}` });
    }).catch(error => {
      wx.hideLoading();
      this.creating = false;
      api.showCenterNotice({ title: error.message || '创建训练失败' });
    });
  },

  goTraining() { wx.switchTab({ url: '/pages/index/index' }); }
});
