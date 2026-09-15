const api = require('../../utils/api.js');

/* 团队成员列表与可添加学员列表都受后端 limit 限制（最多 100）。
   截断时页面必须显式提示，不能让主管以为「就这么多人」。 */
const PAGE_LIMIT = 100;

const idKey = value => String(value === undefined || value === null ? '' : value);

const toIdSet = list => (list || []).reduce((accumulator, value) => {
  accumulator[idKey(value)] = true;
  return accumulator;
}, {});

/* 数值格式化：整数原样显示，小数保留一位。与 admin / member-detail 同一口径——
   后端返回的是原始 AVG，不格式化成 78.33333333333333 会直接漏到界面上。 */
const fmt1 = value => {
  const num = Number(value);
  if (!isFinite(num)) return value === null || value === undefined ? '0' : String(value);
  return num % 1 === 0 ? String(Math.round(num)) : num.toFixed(1);
};

const scoreTextOf = member => {
  if (!member || !member.completedSessions) return '暂无已评分训练';
  return `均分 ${fmt1(member.averageScore)} · 达标率 ${fmt1(member.passRate)}%`;
};

const trainingTextOf = member => {
  if (!member || !member.completedSessions) return '还没有完成过训练';
  return member.lastTrainingDate
    ? `已完成 ${member.completedSessions} 次 · 最近 ${member.lastTrainingDate}`
    : `已完成 ${member.completedSessions} 次`;
};

Page({
  data: {
    loading: true,
    loadError: false,
    loadErrorMsg: '',

    members: [],
    memberTotal: 0,
    memberTruncated: false,

    candidates: [],
    candidateTotal: 0,

    keyword: '',
    filteredCandidates: [],
    filteredAllSelected: false,
    selectedKeys: [],
    selectedCount: 0,

    adding: false,
    removingId: ''
  },

  onLoad() {
    this.loadTeam();
  },

  onPullDownRefresh() {
    this.loadTeam(() => wx.stopPullDownRefresh());
  },

  loadTeam(done) {
    this.setData({ loading: true, loadError: false, loadErrorMsg: '' });
    Promise.all([
      api.getTeamMembers({ limit: PAGE_LIMIT }),
      api.getTeamCandidates({ limit: PAGE_LIMIT })
    ]).then(([teamData, candidateData]) => {
      const members = (teamData.members || []).map(item => Object.assign({}, item, {
        initial: String(item.displayName || '学').slice(0, 1),
        scoreText: scoreTextOf(item),
        trainingText: trainingTextOf(item)
      }));
      const candidates = (candidateData.candidates || []).map(item => Object.assign({}, item, {
        initial: String(item.displayName || '学').slice(0, 1),
        selected: false
      }));
      const memberTotal = Number(teamData.totalTeamMembers) || members.length;
      const candidateTotal = Number(candidateData.totalCandidates) || candidates.length;
      /* 保留仍然存在的已选人员，避免刷新后出现「已选 3 人」却只勾中 1 人的错位。 */
      const available = toIdSet(candidates.map(item => item.id));
      const selectedKeys = this.data.selectedKeys.filter(key => available[idKey(key)]);
      this.setData({
        loading: false,
        members,
        memberTotal,
        memberTruncated: memberTotal > members.length,
        candidates,
        candidateTotal,
        selectedKeys
      }, () => {
        this.applyCandidateView();
        if (typeof done === 'function') done();
      });
    }).catch(error => {
      this.setData({
        loading: false,
        loadError: true,
        loadErrorMsg: error.message || '团队成员加载失败'
      });
      if (typeof done === 'function') done();
    });
  },

  /* 候选区是派生态：搜索词、勾选态都在 JS 里预先算成布尔字段。
     WXML 里对数据路径调用函数（如 indexOf）依赖追踪会失效且不报错。 */
  applyCandidateView() {
    const keyword = String(this.data.keyword || '').trim().toLowerCase();
    const selectedSet = toIdSet(this.data.selectedKeys);
    const filteredCandidates = this.data.candidates
      .filter(item => !keyword || String(item.displayName || '').toLowerCase().indexOf(keyword) >= 0)
      .map(item => Object.assign({}, item, { selected: !!selectedSet[idKey(item.id)] }));
    const filteredAllSelected = filteredCandidates.length > 0 &&
      filteredCandidates.every(item => item.selected);
    this.setData({
      filteredCandidates,
      filteredAllSelected,
      selectedCount: this.data.selectedKeys.length
    });
  },

  onSearchInput(e) {
    this.setData({ keyword: e.detail.value }, () => this.applyCandidateView());
  },

  toggleCandidate(e) {
    const id = idKey(e.currentTarget.dataset.id);
    if (!id) return;
    const selectedKeys = this.data.selectedKeys.slice();
    const index = selectedKeys.findIndex(key => idKey(key) === id);
    if (index >= 0) selectedKeys.splice(index, 1);
    else selectedKeys.push(e.currentTarget.dataset.id);
    this.setData({ selectedKeys }, () => this.applyCandidateView());
  },

  toggleAllFiltered() {
    const selectedSet = toIdSet(this.data.selectedKeys);
    const allSelected = this.data.filteredAllSelected;
    const selectedKeys = this.data.selectedKeys.slice();
    this.data.filteredCandidates.forEach(item => {
      const key = idKey(item.id);
      const index = selectedKeys.findIndex(value => idKey(value) === key);
      if (allSelected) {
        if (index >= 0) selectedKeys.splice(index, 1);
      } else if (index < 0) {
        if (!selectedSet[key]) selectedKeys.push(item.id);
      }
    });
    this.setData({ selectedKeys }, () => this.applyCandidateView());
  },

  clearSelection() {
    this.setData({ selectedKeys: [] }, () => this.applyCandidateView());
  },

  submitAdd() {
    const learnerIds = this.data.selectedKeys.slice();
    if (!learnerIds.length || this.data.adding) return;
    this.setData({ adding: true });
    api.addTeamMembers(learnerIds).then(result => {
      const added = Number(result.addedCount) || 0;
      const skipped = Number(result.skippedCount) || 0;
      this.setData({ selectedKeys: [], adding: false });
      wx.showToast({
        title: skipped ? `已添加 ${added} 人，跳过 ${skipped} 人` : `已添加 ${added} 人`,
        icon: 'none'
      });
      this.loadTeam();
    }).catch(error => {
      this.setData({ adding: false });
      wx.showToast({ title: error.message || '添加成员失败', icon: 'none' });
      /* 失败常见于「这些人已被其他主管拉走」，重新拉取候选名单让列表回到真实状态。 */
      this.loadTeam();
    });
  },

  confirmRemove(e) {
    const id = idKey(e.currentTarget.dataset.id);
    const name = e.currentTarget.dataset.name || '该学员';
    if (!id || this.data.removingId) return;
    wx.showModal({
      title: '移出团队',
      content: `将「${name}」移出你的团队？其账号与全部训练记录都会保留，之后可以重新加入。同时会从尚未到期的培训计划名单中移除。`,
      confirmText: '移出',
      confirmColor: '#C2554A',
      success: result => {
        if (result.confirm) this.removeMember(e.currentTarget.dataset.id, name);
      }
    });
  },

  removeMember(learnerId, name) {
    this.setData({ removingId: idKey(learnerId) });
    api.removeTeamMember(learnerId).then(result => {
      this.setData({ removingId: '' });
      const cleared = Number(result.clearedAssignments) || 0;
      wx.showToast({
        title: cleared ? `已移出，并撤销 ${cleared} 条未到期计划指派` : '已移出团队',
        icon: 'none'
      });
      this.loadTeam();
    }).catch(error => {
      this.setData({ removingId: '' });
      wx.showToast({ title: error.message || `移出「${name}」失败`, icon: 'none' });
      this.loadTeam();
    });
  },

  openMember(e) {
    const id = idKey(e.currentTarget.dataset.id);
    if (!id) return;
    wx.navigateTo({ url: `/pages/member-detail/member-detail?id=${encodeURIComponent(id)}` });
  },

  retryLoad() {
    this.loadTeam();
  }
});
