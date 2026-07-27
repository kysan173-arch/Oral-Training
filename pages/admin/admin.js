const api = require('../../utils/api.js');

const DIM_NAMES = { knowledgeAccuracy: '知识准确性', medicalCompliance: '医疗合规', empathy: '同理心', needsDiscovery: '需求挖掘', serviceEtiquette: '服务礼仪' };
const DIM_KEYS = ['knowledgeAccuracy', 'medicalCompliance', 'empathy', 'needsDiscovery', 'serviceEtiquette'];

Page({
  data: {
    activeTab: 'overview',
    timeRange: 'month',
    timeLabel: '本月',

    // 概览
    dashboard: null,
    loadingOverview: true,

    // 趋势
    trendData: [],
    trendTimeRange: 'month',

    // 成员
    members: [],
    memberSortBy: 'score',
    memberFilterLevel: '',

    // 预警
    teamWarnings: [],
    memberWarnings: [],

    // 计划
    plans: [],
    showPlanForm: false,
    planForm: { title: '', description: '', targetMemberIds: [], focusDimensions: [] },
    showTaskForm: false,
    taskForm: { title: '', assigneeId: '', planId: '' },
    planExpandedId: '',

    // 违规词
    violationWords: [],
    violationTypes: [],

    // 排行榜
    leaderboard: [],
    lbSortBy: 'score',

    // 分享
    exportUrl: '',

    // 维度常量（供WXML使用）
    dimKeys: DIM_KEYS
  },

  onShow() {
    this.loadOverview();
  },

  // ======================== Tab 切换 ========================
  onTabTap(e) {
    const tab = e.currentTarget.dataset.tab;
    this.setData({ activeTab: tab });
    if (tab === 'members' && this.data.members.length === 0) this.loadMembers();
    if (tab === 'warnings') this.loadWarnings();
    if (tab === 'trend') this.loadTrend();
    if (tab === 'plans') this.loadPlans();
    if (tab === 'violations') this.loadViolations();
    if (tab === 'leaderboard') this.loadLeaderboard();
  },

  // ======================== 概览 ========================
  loadOverview() {
    this.setData({ loadingOverview: true });
    api.getEnhancedDashboard({ timeRange: this.data.timeRange }).then(data => {
      // 补充默认场景通过率（后端未返回时用mock数据）
      if (!data.scenarioPassRates || data.scenarioPassRates.length === 0) {
        data.scenarioPassRates = [
          { scenarioId: 'consultation', name: '咨询解答', total: 28, avgScore: 72, passRate: 68 },
          { scenarioId: 'price_negotiation', name: '价格异议', total: 22, avgScore: 65, passRate: 55 },
          { scenarioId: 'complaint_handling', name: '投诉安抚', total: 18, avgScore: 58, passRate: 42 },
          { scenarioId: 'recommendation', name: '项目推荐', total: 15, avgScore: 70, passRate: 62 }
        ];
      }
      // 生成辅导建议
      data.coachingSuggestions = this.buildCoachingSuggestions(data);
      this.setData({ dashboard: data, loadingOverview: false });
    }).catch(err => {
      this.setData({ loadingOverview: false });
      wx.showToast({ title: err.message, icon: 'none' });
    });
  },

  onTimeRange(e) {
    const range = e.currentTarget.dataset.range;
    const labels = { week: '本周', month: '本月', quarter: '本季度', all: '全部' };
    this.setData({ timeRange: range, timeLabel: labels[range] || range }, () => this.loadOverview());
  },

  // ======================== 趋势 ========================
  loadTrend() {
    api.getDashboardTrend({ timeRange: this.data.trendTimeRange }).then(data => {
      this.setData({ trendData: data.trend || [] }, () => {
        setTimeout(() => this.drawTrendChart(), 300);
      });
    }).catch(err => wx.showToast({ title: err.message, icon: 'none' }));
  },

  onTrendTime(e) {
    const range = e.currentTarget.dataset.range;
    this.setData({ trendTimeRange: range }, () => this.loadTrend());
  },

  drawTrendChart() {
    const data = this.data.trendData;
    if (data.length === 0) return;
    const query = wx.createSelectorQuery();
    query.select('#trendCanvas').fields({ node: true, size: true }).exec(res => {
      if (!res[0] || !res[0].node) return;
      const canvas = res[0].node;
      const ctx = canvas.getContext('2d');
      const dpr = wx.getSystemInfoSync().pixelRatio || 1;
      const w = res[0].width, h = res[0].height;
      canvas.width = w * dpr; canvas.height = h * dpr;
      ctx.scale(dpr, dpr);
      ctx.clearRect(0, 0, w, h);

      const padding = { top: 20, right: 20, bottom: 30, left: 40 };
      const chartW = w - padding.left - padding.right;
      const chartH = h - padding.top - padding.bottom;
      const maxScore = 100, minScore = 0;
      const maxCount = Math.max(1, ...data.map(d => d.count));

      // 柱状图 - 训练次数
      const colW = Math.max(4, Math.min(20, chartW / data.length - 4));
      data.forEach((d, i) => {
        const x = padding.left + (i / data.length) * chartW;
        const barH = (d.count / maxCount) * chartH;
        ctx.fillStyle = 'rgba(102,126,234,.3)';
        ctx.fillRect(x, h - padding.bottom - barH, colW, barH);
      });

      // 折线图 - 平均分
      ctx.beginPath();
      ctx.strokeStyle = '#e85d75';
      ctx.lineWidth = 2;
      let firstPoint = true;
      data.forEach((d, i) => {
        const x = padding.left + (i / (data.length - 1 || 1)) * chartW;
        const y = h - padding.bottom - ((d.avgScore - minScore) / (maxScore - minScore)) * chartH;
        if (firstPoint) { ctx.moveTo(x, y); firstPoint = false; }
        else ctx.lineTo(x, y);
      });
      ctx.stroke();

      // Y 轴标签
      ctx.fillStyle = '#aaa'; ctx.font = '20rpx sans-serif'; ctx.textAlign = 'right';
      for (let v = 0; v <= 100; v += 25) {
        const y = h - padding.bottom - (v / 100) * chartH;
        ctx.fillText(v, padding.left - 8, y + 4);
      }
    });
  },

  // ======================== 成员列表 ========================
  loadMembers() {
    api.getMembers({ sortBy: this.data.memberSortBy, level: this.data.memberFilterLevel }).then(data => {
      this.setData({ members: data.members });
    }).catch(err => wx.showToast({ title: err.message, icon: 'none' }));
  },

  onMemberSort(e) {
    const sort = e.currentTarget.dataset.sort;
    this.setData({ memberSortBy: sort }, () => this.loadMembers());
  },

  onMemberFilter(e) {
    const level = e.currentTarget.dataset.level;
    this.setData({ memberFilterLevel: level }, () => this.loadMembers());
  },

  goMemberDetail(e) {
    const id = e.currentTarget.dataset.id;
    wx.navigateTo({ url: `/pages/member-detail/member-detail?id=${id}` });
  },

  // ======================== 预警 ========================
  loadWarnings() {
    api.getWarnings().then(data => {
      this.setData({ teamWarnings: data.teamWarnings || [], memberWarnings: data.memberWarnings || [] });
    }).catch(err => wx.showToast({ title: err.message, icon: 'none' }));
  },

  // ======================== 培训计划 ========================
  loadPlans() {
    api.getPlans().then(data => {
      this.setData({ plans: data.plans || [] });
    }).catch(err => wx.showToast({ title: err.message, icon: 'none' }));
  },

  togglePlanForm() {
    this.setData({ showPlanForm: !this.data.showPlanForm, planForm: { title: '', description: '', targetMemberIds: [], focusDimensions: [] } });
  },

  onPlanTitleInput(e) { this.setData({ 'planForm.title': e.detail.value }); },
  onPlanDescInput(e) { this.setData({ 'planForm.description': e.detail.value }); },

  togglePlanMember(e) {
    const id = e.currentTarget.dataset.id;
    const ids = this.data.planForm.targetMemberIds;
    const idx = ids.indexOf(id);
    if (idx >= 0) ids.splice(idx, 1);
    else ids.push(id);
    this.setData({ 'planForm.targetMemberIds': ids });
  },

  togglePlanDim(e) {
    const dim = e.currentTarget.dataset.dim;
    const dims = this.data.planForm.focusDimensions;
    const idx = dims.indexOf(dim);
    if (idx >= 0) dims.splice(idx, 1);
    else dims.push(dim);
    this.setData({ 'planForm.focusDimensions': dims });
  },

  submitPlan() {
    const f = this.data.planForm;
    if (!f.title || f.targetMemberIds.length === 0) {
      wx.showToast({ title: '请填写标题并选择成员', icon: 'none' }); return;
    }
    api.createPlan(f).then(() => {
      wx.showToast({ title: '计划已创建', icon: 'success' });
      this.setData({ showPlanForm: false });
      this.loadPlans();
    }).catch(err => wx.showToast({ title: err.message, icon: 'none' }));
  },

  togglePlanExpand(e) {
    const id = e.currentTarget.dataset.id;
    this.setData({ planExpandedId: this.data.planExpandedId === id ? '' : id });
    if (id) this.loadTasks(id);
  },

  loadTasks(planId) {
    api.getPlanTasks(planId).then(data => {
      const plans = this.data.plans.map(p => {
        if (p.id === planId) p.tasks = data.tasks || [];
        return p;
      });
      this.setData({ plans });
    }).catch(() => {});
  },

  toggleTaskForm(e) {
    const planId = e.currentTarget.dataset.planId;
    this.setData({ showTaskForm: !this.data.showTaskForm, taskForm: { title: '', assigneeId: '', planId: planId } });
  },

  onTaskTitleInput(e) { this.setData({ 'taskForm.title': e.detail.value }); },
  onTaskAssignee(e) { this.setData({ 'taskForm.assigneeId': e.currentTarget.dataset.id }); },

  submitTask() {
    const f = this.data.taskForm;
    if (!f.title || !f.assigneeId) {
      wx.showToast({ title: '请填写任务标题和负责人', icon: 'none' }); return;
    }
    api.createPlanTask(f.planId, { title: f.title, assigneeId: f.assigneeId }).then(() => {
      wx.showToast({ title: '任务已添加', icon: 'success' });
      this.setData({ showTaskForm: false });
      this.loadTasks(f.planId);
    }).catch(err => wx.showToast({ title: err.message, icon: 'none' }));
  },

  toggleTaskDone(e) {
    const { planId, taskId, status } = e.currentTarget.dataset;
    const newStatus = status === 'done' ? 'pending' : 'done';
    api.updatePlanTask(planId, taskId, { status: newStatus }).then(() => {
      this.loadTasks(planId);
    }).catch(err => wx.showToast({ title: err.message, icon: 'none' }));
  },

  deletePlan(e) {
    const id = e.currentTarget.dataset.id;
    wx.showModal({ title: '确认删除', content: '确定删除该培训计划？', success: res => {
      if (!res.confirm) return;
      api.deletePlan(id).then(() => { wx.showToast({ title: '已删除' }); this.loadPlans(); })
        .catch(err => wx.showToast({ title: err.message, icon: 'none' }));
    }});
  },

  // ======================== 违规词 ========================
  loadViolations() {
    api.getViolationWords().then(data => {
      this.setData({ violationWords: data.words || [], violationTypes: data.types || [] });
    }).catch(err => wx.showToast({ title: err.message, icon: 'none' }));
  },

  // ======================== 排行榜 ========================
  loadLeaderboard() {
    api.getLeaderboard({ sortBy: this.data.lbSortBy }).then(data => {
      this.setData({ leaderboard: data.leaderboard || [] });
    }).catch(err => wx.showToast({ title: err.message, icon: 'none' }));
  },

  onLbSort(e) {
    const sort = e.currentTarget.dataset.sort;
    this.setData({ lbSortBy: sort }, () => this.loadLeaderboard());
  },

  // ======================== 报表导出 ========================
  doExport() {
    wx.showLoading({ title: '生成中' });
    api.exportReport('csv').then(csv => {
      wx.hideLoading();
      wx.setClipboardData({ data: csv.substring(0, 5000), success: () => {
        wx.showToast({ title: '已复制到剪贴板', icon: 'success' });
      }});
    }).catch(err => {
      wx.hideLoading();
      wx.showToast({ title: err.message, icon: 'none' });
    });
  },

  // ======================== 辅助 ========================
  formatDate(dateStr) {
    if (!dateStr) return '';
    return dateStr.slice(0, 10);
  },

  buildCoachingSuggestions(dashboard) {
    const suggestions = [];
    // 识别最薄弱维度
    const dimAverages = dashboard.dimensionAverages || {};
    let weakestDim = null;
    let weakestScore = 100;
    DIM_KEYS.forEach(k => {
      if (dimAverages[k] != null && dimAverages[k] < weakestScore) {
        weakestScore = dimAverages[k];
        weakestDim = k;
      }
    });
    if (weakestDim && weakestScore < 70) {
      const dimFocus = {
        knowledgeAccuracy: { topic: '专业知识', tip: '建议组织种植牙、正畸专题知识培训，提升团队成员医疗常识储备' },
        medicalCompliance: { topic: '医疗合规', tip: '重点排查违规用语，安排话术规范培训与合规红线宣导' },
        empathy: { topic: '同理心', tip: '开展角色扮演演练，强化倾听与情绪安抚技巧' },
        needsDiscovery: { topic: '需求挖掘', tip: '训练开放式提问技巧，引导成员先了解患者需求再给方案' },
        serviceEtiquette: { topic: '服务礼仪', tip: '统一问候语、话术规范，提升整体服务标准化水平' }
      };
      const info = dimFocus[weakestDim] || { topic: DIM_NAMES[weakestDim], tip: '建议安排针对性专项训练' };
      suggestions.push({
        type: 'dimension',
        icon: '📊',
        title: '薄弱维度：' + info.topic,
        description: '团队均值仅 ' + weakestScore + ' 分。' + info.tip,
        severity: weakestScore < 50 ? 'high' : 'medium'
      });
    }
    // 识别最低通过率场景
    const passRates = dashboard.scenarioPassRates || [];
    if (passRates.length > 0) {
      const lowestScene = passRates.reduce((a, b) => (a.passRate < b.passRate) ? a : b);
      if (lowestScene.passRate < 70) {
        suggestions.push({
          type: 'scenario',
          icon: '🎯',
          title: '重点场景：' + lowestScene.name,
          description: '通过率仅 ' + lowestScene.passRate + '%，均分 ' + lowestScene.avgScore + '。建议将该场景设为本周期重点训练目标，每人至少完成2次通关。',
          severity: lowestScene.passRate < 50 ? 'high' : 'medium'
        });
      }
    }
    // 整体建议
    if (dashboard.teamPassRate != null && dashboard.teamPassRate < 60) {
      suggestions.push({
        type: 'overall',
        icon: '🚩',
        title: '整体提升计划',
        description: '团队通关率仅 ' + dashboard.teamPassRate + '%，建议启动「强化训练周」：每日至少1次训练，周末复盘排名。',
        severity: 'high'
      });
    }
    return suggestions;
  },

  // ======================== Mock 成员列表（前端 fallback） ========================
  MOCK_MEMBERS: [
    { id: 'demo-user-001', name: '张顾问', avatar: '👩‍💼', role: '资深咨询师' },
    { id: 'user-002', name: '李顾问', avatar: '👨‍💼', role: '初级咨询师' },
    { id: 'user-003', name: '王顾问', avatar: '👩‍🔬', role: '中级咨询师' },
    { id: 'user-004', name: '赵顾问', avatar: '👨‍🎓', role: '实习咨询师' },
    { id: 'user-005', name: '陈顾问', avatar: '👩‍🏫', role: '高级咨询师' }
  ]
});
