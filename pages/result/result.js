const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');
const { resultStateAction } = require('../../utils/result-state.js');

/* 分数分档：颜色只跟随分数（≥80 良好绿 / 60–79 中间蓝 / <60 待提升橙） */
const scoreTier = score => (score >= 80 ? 'high' : score >= 60 ? 'mid' : 'low');

const scoreFrom = value => {
  if (value === null || value === undefined || value === '') return null;
  const score = Number(value);
  return Number.isFinite(score) ? Math.min(100, Math.max(0, score)) : null;
};

const dimensionsFrom = score => [
  { key: 'empathy', name: '情绪识别与同理心', score: score.empathy },
  { key: 'knowledgeAccuracy', name: '口腔知识准确性', score: score.knowledgeAccuracy },
  { key: 'needsDiscovery', name: '需求挖掘', score: score.needsDiscovery },
  { key: 'serviceEtiquette', name: '服务礼仪', score: score.serviceEtiquette },
  { key: 'medicalCompliance', name: '医疗合规', score: score.medicalCompliance }
].filter(item => typeof item.score === 'number').map(item => Object.assign(item, { tier: scoreTier(item.score) }));

const totalScoreFrom = (evaluation, sessionTotalScore) => {
  if (evaluation.totalScore !== undefined && evaluation.totalScore !== null) {
    return Math.round(scoreFrom(evaluation.totalScore));
  }
  if (evaluation.schemaVersion === 2) return null;
  if (sessionTotalScore !== undefined && sessionTotalScore !== null) {
    return Math.round(scoreFrom(sessionTotalScore));
  }
  const score = evaluation.dimensionScores || {};
  return Math.round(scoreFrom(score.knowledgeAccuracy) * 0.25
    + scoreFrom(score.medicalCompliance) * 0.25
    + scoreFrom(score.empathy) * 0.20
    + scoreFrom(score.needsDiscovery) * 0.20
    + scoreFrom(score.serviceEtiquette) * 0.10);
};

/* 总分环档位色（与全局语义色 token 一致）；score 为 null 时走 unscored 档。
   不再输出 note：档位名（表现出色 / 表现良好 / …）已说明水平，副标题一律是复述。 */
const levelFrom = score => {
  if (score === null) return { key: 'unscored', name: '暂不形成综合分', scoreColor: '#1F3864' };
  if (score >= 90) return { key: 'excellent', name: '表现出色', scoreColor: '#2E8B6C' };
  if (score >= 80) return { key: 'good', name: '表现良好', scoreColor: '#2E8B6C' };
  if (score >= 60) return { key: 'qualified', name: '达到练习目标', scoreColor: '#B97A1E' };
  return { key: 'practice', name: '继续复练', scoreColor: '#C2554A' };
};

/* 差异类型 → 中文标签。五个取值必须与后端 answer_comparison.h 的 differenceType 逐字一致，
   任一侧增删都会让另一侧静默退化成「存在差距」，所以不要单独改。 */
const DIFFERENCE_LABELS = {
  equivalent: '等效表达',
  substantive_omission: '实质遗漏',
  factual_or_boundary_error: '事实或边界错误',
  learner_better: '学员更优',
  reference_defect: '标准答案缺陷'
};

/* 进入「待改进」清单的差异，按严重度排序：说错话比漏说更该先看到。
   equivalent / learner_better / reference_defect 不进这个清单——
   前两者本来就不是缺陷，后者是标准答案自身的问题，算不到学员头上。 */
const GAP_PRIORITY = { factual_or_boundary_error: 0, substantive_omission: 1 };
const DIMENSION_LABELS = {
  knowledgeAccuracy: '知识准确性',
  medicalCompliance: '医疗合规',
  empathy: '同理心',
  needsDiscovery: '需求挖掘',
  serviceEtiquette: '服务礼仪'
};

/* 达标线 3：后端 clampLevelForDifference 会把 substantive_omission 压到 ≤2、
   factual_or_boundary_error 压到 ≤1，把 equivalent 抬到 ≥3、learner_better 固定 4，
   所以 level < 3 就是「本轮有实质差距」，页面不需要重算后端规则。 */
const GAP_LEVEL_THRESHOLD = 3;

/* 把 evaluation.answerComparison 归一化为页面可直接渲染的结构。
   返回 null 表示「本次报告没有可展示的对比结果」——调用方据此整块不渲染。
   失败态（status !== 'ready'）与空态一律不渲染：学员端看到「评分失败」没有意义
   （失败原因属运维信息，已留在数据库与后端日志里），而把空区块渲染成空卡片
   会让人误以为「这一轮没做对」。 */
const answerComparisonFrom = (raw, roundCommentByRound, violationRounds, roundReviewByRound) => {
  if (!raw || typeof raw !== 'object') return null;
  if (raw.status !== 'ready') return null;
  const rawRounds = Array.isArray(raw.rounds) ? raw.rounds : [];
  const rounds = rawRounds.map(item => {
    const checks = Array.isArray(item.checks) ? item.checks : [];
    const gaps = checks
      .filter(check => check
        && check.applicable !== false                     /* 不适用项不构成差距 */
        && !(Number(check.repeatOfRound) > 0)             /* 重复计分项不重复展示 */
        && check.differenceType !== 'reference_defect'    /* 标准答案自身缺陷不算学员的账 */
        && check.differenceType !== 'learner_better'      /* 学员更优单独展示 */
        && check.differenceType !== 'equivalent'          /* 等效表达不是差距 */
        && check.differenceType !== 'not_applicable'      /* 不适用项不是差距 */
        && check.quoteVerified !== false                   /* 原话未核实的项不当作事实差距 */
        && Number(check.level) < GAP_LEVEL_THRESHOLD)
      .map(check => ({
        /* key 供 wx:for 用：同一轮里 criterionId 不会重复，整场项与逐轮项也不重叠。 */
        key: check.criterionId || `${check.dimension || ''}-${check.name || ''}`,
        label: DIFFERENCE_LABELS[check.differenceType] || '存在差距',
        priority: GAP_PRIORITY[check.differenceType] === undefined ? 9 : GAP_PRIORITY[check.differenceType],
        name: check.name || '',
        reason: check.reason || ''
      }))
      .sort((a, b) => a.priority - b.priority);
    const round = Number(item.round);
    const valid = round > 0;
    const coach = valid && roundCommentByRound ? roundCommentByRound.get(round) : null;
    const coachText = coach && coach.comment ? coach.comment : '';
    const generatedReview = valid && roundReviewByRound ? roundReviewByRound.get(round) : '';
    /* 后端生成的新点评优先；没有生成结果时才退回“原点评 + 重点差距类型”的兜底。 */
    const gapText = generatedReview
      ? ''
      : (gaps.length ? `重点：${gaps.slice(0, 2).map(gap => gap.label).join('、')}` : '');
    const reviewText = generatedReview || [coachText, gapText].filter(Boolean).join('；');
    let learnerAnswer = item.learnerAnswer || '';
    if (!valid && !learnerAnswer) {
      for (const check of checks) {
        if (check && typeof check.learnerQuote === 'string' && check.learnerQuote) {
          learnerAnswer = check.learnerQuote;
          break;
        }
      }
    }
    return {
      round: valid ? round : 0,
      roundLabel: valid ? `第 ${round} 轮` : '整场',
      patientQuestion: valid ? (item.patientQuestion || '') : '整场对话',
      learnerAnswer,
      referenceAnswer: item.referenceAnswer || '',
      hasGap: gaps.length > 0,
      hasViolation: valid && violationRounds ? violationRounds.has(round) : false,
      coachComment: coachText,
      reviewText,
      gaps
    };
  }).filter(item => item.learnerAnswer || item.referenceAnswer || item.gaps.length);
  if (!rounds.length) return null;
  const learnerBetter = (Array.isArray(raw.learnerBetter) ? raw.learnerBetter : [])
    .map(check => ({
      roundLabel: Number(check.round) > 0 ? `第 ${check.round} 轮` : '整场',
      name: check.name || '',
      reason: check.reason || ''
    }))
    .filter(item => item.name || item.reason);
  const comparisonDimensions = [
    'knowledgeAccuracy', 'medicalCompliance', 'empathy', 'needsDiscovery', 'serviceEtiquette'
  ].map(key => {
    const score = scoreFrom(raw.dimensionScores ? raw.dimensionScores[key] : null);
    return {
      key,
      name: DIMENSION_LABELS[key],
      score,
      display: score === null ? '未形成' : score,
      percent: score === null ? 0 : score
    };
  });
  return {
    rounds,
    learnerBetter,
    dimensions: comparisonDimensions,
    /* 分数一律可选：某维度没有可评项时后端会给 totalScore = null 并标 lowConfidence，
       这是设计行为（「没有足够证据不得自动补分」），页面上不能显示成 0 分。 */
    totalScore: typeof raw.totalScore === 'number' ? raw.totalScore : null,
    lowConfidence: raw.lowConfidence === true
  };
};

const normalizeEvaluation = (evaluation, sessionTotalScore) => {
  const totalScore = totalScoreFrom(evaluation, sessionTotalScore);
  /* 违规轮次集合：用来在轮次点评上标注「该轮含违规」，把两个区块的关系讲清楚。
     刻意不把重复轮次从点评区删掉——点评里的 comment 是违规区没有的信息，删了就丢了。
     两处都保留、并标明它们说的是同一轮，比删一处更安全。 */
  const violationRounds = new Set(
    (evaluation.violations || []).map(item => Number(item.round)).filter(round => !isNaN(round))
  );
  /* 把逐轮教练点评按 round 并进参考答案对比，页面不再单独渲染一份“建议这样说”。 */
  const roundCommentByRound = new Map();
  (evaluation.roundComments || []).forEach(item => {
    const round = Number(item.round);
    if (round > 0 && !roundCommentByRound.has(round)) roundCommentByRound.set(round, item);
  });
  /* 后端在参考答案对比之后生成的逐轮点评。 */
  const roundReviewByRound = new Map();
  (evaluation.roundReviews || []).forEach(item => {
    const round = Number(item.round);
    const review = item && typeof item.review === 'string' ? item.review : '';
    if (round > 0 && review && !roundReviewByRound.has(round)) roundReviewByRound.set(round, review);
  });
  return Object.assign({}, evaluation, {
    totalScore,
    hasTotalScore: totalScore !== null,
    dimensionScores: Object.assign({}, evaluation.dimensionScores || {}),
    strengths: (evaluation.strengths || []).map(item => item.content || item.evidence || item),
    improvements: (evaluation.improvements || []).map(item => item.content || item),
    violations: (evaluation.violations || []).map((item, index) => Object.assign({}, item, {
      id: item.id || `violation-${index}`,
      quote: item.originalQuote || item.quote || '',
      rewrite: item.recommendedRewrite || item.rewrite || ''
    })),
    roundComments: (evaluation.roundComments || []).map(item => Object.assign({}, item, {
      userQuote: item.userMessage || item.userQuote || '',
      rewrite: item.recommendedRewrite || item.rewrite || '',
      hasViolation: violationRounds.has(Number(item.round))
    })),
    recommendedPhrases: (evaluation.recommendedPhrases || []).map(item => Object.assign({}, item, {
      patientSays: item.patientSays || '',
      csReply: item.csReply || item.recommendedRewrite || '',
      /* 参考答案来源说明不再作为卡片小字展示。 */
      reason: item.reason && item.reason.indexOf('来自参考答案对比') === -1 &&
              item.reason.indexOf('可收藏到话术锦囊') === -1 ? item.reason : ''
    })),
    /* 参考答案对比：没有该区块（含开关未开启）或区块处于失败态时为 null，
       页面据此整块不渲染，保证旧报告与新报告在无数据时表现完全一致。 */
    comparison: answerComparisonFrom(evaluation.answerComparison, roundCommentByRound, violationRounds, roundReviewByRound)
  });
};

/* WXML 不能对数据路径调用函数（依赖追踪失效且不报错），
   折叠时要显示的条目在 JS 里预算成字段。
   折叠态只展示第1 条：本次优秀话术与回合点评都是「精选」性质，学员要的是
   先看最值得抄的那一条，而不是一份缩水的完整列表。折叠按钮上会写明还剩几条，
   「看不见」和「没有」的区别由那行提示承担。 */
const COLLAPSED_PREVIEW = 1;
const foldList = (list, expanded) => (expanded ? (list || []) : (list || []).slice(0, COLLAPSED_PREVIEW));

Page({
  data: {
    session: null,
    scenario: null,
    evaluation: null,
    dimensions: [],
    level: null,
    scorePercent: 0,
    scoreText: '—',
    loading: true,
    loadingText: '正在生成训练报告…',
    retryable: false,
    timedOut: false,
    violationsExpanded: false,
    roundCommentsExpanded: false,
    phrasesExpanded: false,
    comparisonExpanded: false,
    visibleViolations: [],
    visibleRoundComments: [],
    visiblePhrases: [],
    visibleComparisonRounds: []
  },

  sessionId: '',
  pollTimer: null,
  waitStartedAt: 0,
  networkRetryIndex: 0,
  stateRecoveryStarted: false,
  stateRecoveryAttempted: false,

  onLoad(options) {
    this.sessionId = options.sessionId || '';
    if (!this.sessionId) {
      this.handleMissingSession();
      return;
    }
    this.waitStartedAt = Date.now();
    this.loadInitialData();
  },

  onUnload() { if (this.pollTimer) clearTimeout(this.pollTimer); },

  loadInitialData() {
    if (!this.sessionId) return;
    Promise.all([api.getSession(this.sessionId), api.getScenarios()]).then(([detail, scenarioData]) => {
      const scenarios = scenarioData.items || [];
      const scenarioIndex = scenarios.findIndex(item => item.id === detail.session.scenarioId);
      const scenario = scenarioIndex >= 0 ? scenarios[scenarioIndex] : { name: detail.session.scenarioName };
      /* 起止时间在 JS 预算成展示文本，WXML 里不调用函数 */
      const sessionView = Object.assign({}, detail.session, {
        rangeText: datetime.formatRange(detail.session.startedAt, detail.session.finishedAt)
      });
      this.setData({ session: sessionView, scenario });
      this.networkRetryIndex = 0;
      this.pollReport();
    }).catch(error => this.handleNetworkError(error, () => this.loadInitialData()));
  },

  pollReport() {
    if (!this.sessionId || !this.data.session) return;
    api.getEvaluation(this.sessionId).then(report => {
      this.networkRetryIndex = 0;
      const action = resultStateAction(report.status, this.data.session.status);
      if (action === 'ready' && report.evaluation) {
        const evaluation = normalizeEvaluation(report.evaluation, this.data.session.totalScore);
        const hasTotalScore = evaluation.totalScore !== null;
        const scorePercent = hasTotalScore ? Math.max(0, Math.min(100, evaluation.totalScore)) : 0;
        const scoreText = hasTotalScore ? String(evaluation.totalScore) : '—';
        this.setData({
          evaluation,
          dimensions: dimensionsFrom(evaluation.dimensionScores),
          loading: false,
          level: levelFrom(evaluation.totalScore),
          retryable: false,
          timedOut: false,
          scorePercent,
          scoreText,
          visibleViolations: foldList(evaluation.violations, this.data.violationsExpanded),
          visibleRoundComments: foldList(evaluation.roundComments, this.data.roundCommentsExpanded),
          visiblePhrases: foldList(evaluation.recommendedPhrases, this.data.phrasesExpanded),
          visibleComparisonRounds: foldList(evaluation.comparison && evaluation.comparison.rounds, this.data.comparisonExpanded)
        });
        return;
      }
      if (action === 'failed') {
        this.setData({ loading: true, loadingText: '报告生成失败，可重新评分', retryable: true, timedOut: false });
        return;
      }
      if (action === 'recover-generation') {
        if (this.stateRecoveryAttempted) {
          if (this.waitExpired()) {
            this.showWaitActions('评分任务暂未启动，你可以继续等待或返回历史记录。');
          } else {
            this.setData({ loading: true, loadingText: '正在等待评分任务启动…', retryable: false });
            this.schedule(() => this.pollReport(), 2000);
          }
          return;
        }
        this.recoverGeneration();
        return;
      }
      if (action === 'return-to-session') {
        this.returnToTraining();
        return;
      }
      if (action === 'return-to-history') {
        this.returnToHistory();
        return;
      }
      if (this.waitExpired()) {
        this.showWaitActions('报告仍在生成，你可以继续等待或返回历史记录。');
        return;
      }
      this.setData({ loading: true, loadingText: '正在生成训练报告…', retryable: false, timedOut: false });
      this.schedule(() => this.pollReport(), 2000);
    }).catch(error => this.handleNetworkError(error, () => this.pollReport()));
  },

  recoverGeneration() {
    if (this.stateRecoveryStarted) return;
    this.stateRecoveryStarted = true;
    this.stateRecoveryAttempted = true;
    this.setData({ loading: true, loadingText: '正在恢复评分任务…', retryable: false, timedOut: false });
    api.finishSession(this.sessionId).then(() => {
      this.stateRecoveryStarted = false;
      this.waitStartedAt = Date.now();
      this.pollReport();
    }).catch(error => {
      this.stateRecoveryStarted = false;
      this.stateRecoveryAttempted = false;
      this.handleNetworkError(error, () => this.recoverGeneration());
    });
  },

  returnToTraining() {
    if (this.stateRecoveryStarted) return;
    this.stateRecoveryStarted = true;
    this.setData({ loading: true, loadingText: '训练尚未结束，正在返回会话…', retryable: false });
    wx.showModal({
      title: '训练尚未结束',
      content: '完成至少一轮对话并结束训练后，才能生成评分报告。',
      showCancel: false,
      success: () => wx.redirectTo({ url: `/pages/training/training?sessionId=${this.sessionId}` })
    });
  },

  returnToHistory() {
    if (this.stateRecoveryStarted) return;
    this.stateRecoveryStarted = true;
    this.setData({ loading: true, loadingText: '该训练无法生成报告', retryable: false });
    wx.showModal({
      title: '无法生成报告',
      content: '该训练已被放弃，请从历史记录选择其他已完成训练。',
      showCancel: false,
      success: () => wx.redirectTo({ url: '/pages/report/report' })
    });
  },

  handleMissingSession() {
    this.setData({ loading: true, loadingText: '缺少训练会话信息' });
    wx.showModal({
      title: '无法打开报告',
      content: '页面链接缺少会话信息，请从历史记录重新进入。',
      showCancel: false,
      success: () => wx.redirectTo({ url: '/pages/report/report' })
    });
  },

  handleNetworkError(error, retry) {
    if (this.waitExpired()) {
      this.showWaitActions('网络暂时不可用，你可以继续等待或返回历史记录。');
      return;
    }
    const delays = [1000, 2000, 4000];
    const delay = delays[Math.min(this.networkRetryIndex, delays.length - 1)];
    this.networkRetryIndex += 1;
    this.setData({ loading: true, loadingText: error.message || '网络异常，正在重试…', retryable: false });
    this.schedule(retry, delay);
  },

  schedule(callback, delay) {
    if (this.pollTimer) clearTimeout(this.pollTimer);
    this.pollTimer = setTimeout(callback, delay);
  },

  waitExpired() { return Date.now() - this.waitStartedAt >= 30000; },

  showWaitActions(message) {
    this.setData({ loading: true, loadingText: message, retryable: false, timedOut: true });
  },

  continueWaiting() {
    this.waitStartedAt = Date.now();
    this.networkRetryIndex = 0;
    this.setData({ timedOut: false, loadingText: '继续等待训练报告…' });
    if (this.data.session) this.pollReport();
    else this.loadInitialData();
  },

  retryEvaluation() {
    api.retryEvaluation(this.sessionId).then(() => {
      this.waitStartedAt = Date.now();
      this.networkRetryIndex = 0;
      this.setData({ retryable: false, timedOut: false, loadingText: '正在重新生成报告…' });
      this.pollReport();
    }).catch(error => api.showCenterNotice({ title: error.message }));
  },

  viewHistory() { wx.navigateTo({ url: '/pages/report/report' }); },

  toggleViolations() {
    const violationsExpanded = !this.data.violationsExpanded;
    const evaluation = this.data.evaluation;
    this.setData({
      violationsExpanded,
      visibleViolations: foldList(evaluation && evaluation.violations, violationsExpanded)
    });
  },

  toggleRoundComments() {
    const roundCommentsExpanded = !this.data.roundCommentsExpanded;
    const evaluation = this.data.evaluation;
    this.setData({
      roundCommentsExpanded,
      visibleRoundComments: foldList(evaluation && evaluation.roundComments, roundCommentsExpanded)
    });
  },

  togglePhrases() {
    const phrasesExpanded = !this.data.phrasesExpanded;
    const evaluation = this.data.evaluation;
    this.setData({
      phrasesExpanded,
      visiblePhrases: foldList(evaluation && evaluation.recommendedPhrases, phrasesExpanded)
    });
  },

  toggleComparison() {
    const comparisonExpanded = !this.data.comparisonExpanded;
    const evaluation = this.data.evaluation;
    this.setData({
      comparisonExpanded,
      visibleComparisonRounds: foldList(evaluation && evaluation.comparison && evaluation.comparison.rounds, comparisonExpanded)
    });
  }
});
