const api = require('../../utils/api.js');

/* 分类中文名与说明必须与学员端训练页保持一致（pages/index CATEGORY_CONFIG）：
   主管在发布页看到的分类名，学员在训练页也要看到同一个词，否则「价格异议
   专项」会被写成「价格沟通专项」，两边对不上。
   后端 sceneCategories() 只提供 id，这里负责展示文案。 */
const SCENE_CATEGORY_CONFIG = [
  { key: 'consultation', name: '咨询解答', description: '先了解患者关切，再清楚说明服务边界' },
  { key: 'price_negotiation', name: '价格异议', description: '客观说明费用构成，不承诺固定价格' },
  { key: 'complaint_handling', name: '投诉安抚', description: '先回应情绪，及时引导联系医生或复诊' },
  { key: 'recommendation', name: '项目推荐', description: '从真实需求出发，不替代医生判断' }
];

/* 难度口径与学员端 DIFFICULTY_MAP 一致：basic 归入初级 */
const DIFFICULTY_LABELS = {
  beginner: '初级',
  basic: '初级',
  intermediate: '中级',
  advanced: '高级'
};

/* 补零，用于 date/time picker 输出拼接 */
const pad = value => String(value).padStart(2, '0');

/* 成员列表单页上限与后端 /supervisor/members 的 limit 上限一致（1..100） */
const MEMBER_PAGE_LIMIT = 100;

/* 选中态一律在 JS 里预先算成布尔字段再交给 WXML，不要在模板里写
   `selectedIds.indexOf(item.id) >= 0`：WXML 表达式的函数调用只可靠支持
   字面量参数（如 displayName.charAt(0)），参数写成数据路径（item.id /
   scene.id）时整个表达式不生效，选项就永远点不出高亮，而且不报任何错。
   另外这里统一按 String 比较，避免 dataset 取回的 id 与接口返回的 id
   类型漂移后 indexOf 严格相等静默失配。 */
const idKey = value => String(value);
const toIdSet = ids => new Set((ids || []).map(idKey));

/* 按关键字过滤成员并派生「当前可见是否已全选」，搜索后全选只作用于可见项 */
const buildMemberView = (members, selectedIds, keyword) => {
  const key = String(keyword || '').trim().toLowerCase();
  const idSet = toIdSet(selectedIds);
  const filtered = (members || [])
    .filter(item => !key || String(item.displayName).toLowerCase().indexOf(key) >= 0)
    .map(item => Object.assign({}, item, { selected: idSet.has(idKey(item.id)) }));
  return {
    filteredMembers: filtered,
    filteredCount: filtered.length,
    visibleAllSelected: filtered.length > 0 && filtered.every(item => item.selected)
  };
};

const formatDate = date => `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`;
const formatTime = date => `${pad(date.getHours())}:${pad(date.getMinutes())}`;

/* 按固定分类顺序重组场景，并派生「本类已全选」供 WXML 使用。
   每个场景带 selected 布尔字段，模板只做 === 判断。 */
const buildCategories = (rawScenes, selectedIds) => {
  const idSet = toIdSet(selectedIds);
  const groups = {};
  SCENE_CATEGORY_CONFIG.forEach(config => {
    groups[config.key] = { key: config.key, name: config.name, description: config.description, scenes: [] };
  });
  (rawScenes || []).forEach(scene => {
    const group = groups[scene.category] || groups.consultation;
    group.scenes.push({
      id: scene.id,
      name: scene.name,
      difficultyLabel: DIFFICULTY_LABELS[scene.difficulty] || '初级',
      selected: idSet.has(idKey(scene.id))
    });
  });
  return SCENE_CATEGORY_CONFIG
    .map(config => groups[config.key])
    .filter(group => group.scenes.length > 0)
    .map(group => {
      const selectedCount = group.scenes.filter(scene => scene.selected).length;
      return Object.assign({}, group, {
        selectedCount,
        allSelected: selectedCount === group.scenes.length
      });
    });
};

Page({
  data: {
    title: '',
    period: 'week',
    periodOptions: [
      { id: 'week', name: '按周' },
      { id: 'month', name: '按月' }
    ],
    dueDate: '',
    dueTime: '23:59',
    minDate: '',
    requiredCount: 3,
    requiredPassRate: 60,
    description: '',
    categories: [],
    selectedIds: [],
    selectedCount: 0,
    sceneLoading: true,
    sceneLoadFailed: false,
    /* 发布对象：all = 全团队成员（默认），custom = 逐个指派 */
    targetMode: 'all',
    members: [],
    memberKeyword: '',
    filteredMembers: [],
    filteredCount: 0,
    visibleAllSelected: false,
    selectedMemberIds: [],
    selectedMemberCount: 0,
    totalTeamMembers: 0,
    memberLoading: false,
    memberLoadFailed: false,
    submitting: false
  },

  onLoad() {
    /* 默认截止：一周后的 23:59，避免主管每次手动选日期 */
    const due = new Date();
    due.setDate(due.getDate() + 7);
    this.rawScenes = [];
    /* 重发预填：计划详情页通过 eventChannel 送来原计划的内容要素。
       注册要在 loadScenarios 之前，避免场景先到、预填后到时选中态丢失。 */
    const channel = typeof this.getOpenerEventChannel === 'function' ? this.getOpenerEventChannel() : null;
    if (channel && typeof channel.on === 'function') {
      channel.on('prefillPlan', payload => this.applyPrefill(payload));
    }
    this.setData({ dueDate: formatDate(due), minDate: formatDate(new Date()) }, () => this.loadScenarios());
  },

  /* 用原计划内容预填表单。原始指派名单接口不返回，
     targetMode 保持默认「全团队」，主管可在发布页改选成员。 */
  applyPrefill(payload) {
    if (!payload) return;
    const selectedIds = Array.isArray(payload.scenarioIds) ? payload.scenarioIds : [];
    const patch = {
      title: payload.title ? `${payload.title}（重发）` : '',
      period: payload.period === 'month' ? 'month' : 'week',
      requiredCount: Math.max(1, Math.min(20, Number(payload.requiredCount) || 3)),
      requiredPassRate: Number(payload.requiredPassRate) || 60,
      description: payload.description || '',
      selectedIds,
      selectedCount: selectedIds.length
    };
    this.setData(patch, () => {
      /* 场景目录已就绪就直接重建选中态；否则等 loadScenarios 完成时
         它会按 this.data.selectedIds 重建（两条时序都覆盖） */
      if (this.rawScenes.length) {
        this.setData({ categories: buildCategories(this.rawScenes, this.data.selectedIds) });
      }
    });
  },

  /* 主管专属目录接口：学员侧的 /api/scenarios 是 learner_only，主管调用会 403 */
  loadScenarios() {
    this.setData({ sceneLoading: true, sceneLoadFailed: false });
    api.getSupervisorScenarios().then(data => {
      this.rawScenes = data.items || [];
      this.setData({
        categories: buildCategories(this.rawScenes, this.data.selectedIds),
        sceneLoading: false
      });
    }).catch(error => {
      this.rawScenes = [];
      /* 失败时显式置空并标注，避免「适用场景」区静默空白被误认为没有场景 */
      this.setData({ categories: [], sceneLoading: false, sceneLoadFailed: true });
      wx.showToast({ title: error.message || '场景加载失败', icon: 'none' });
    });
  },

  onTitleInput(e) {
    this.setData({ title: e.detail.value || '' });
  },

  onDescriptionInput(e) {
    this.setData({ description: e.detail.value || '' });
  },

  selectPeriod(e) {
    const period = e.currentTarget.dataset.id;
    if (!period || period === this.data.period) return;
    this.setData({ period });
  },

  onDueDateChange(e) {
    this.setData({ dueDate: e.detail.value });
  },

  onDueTimeChange(e) {
    this.setData({ dueTime: e.detail.value });
  },

  stepCount(e) {
    const delta = Number(e.currentTarget.dataset.delta) || 0;
    const next = Math.max(1, Math.min(20, this.data.requiredCount + delta));
    if (next === this.data.requiredCount) return;
    this.setData({ requiredCount: next });
  },

  onPassRateChange(e) {
    this.setData({ requiredPassRate: Number(e.detail.value) });
  },

  applySelection(selectedIds) {
    this.setData({
      selectedIds,
      selectedCount: selectedIds.length,
      categories: buildCategories(this.rawScenes, selectedIds)
    });
  },

  toggleScene(e) {
    const id = e.currentTarget.dataset.id;
    if (!id) return;
    const selected = this.data.selectedIds.slice();
    const index = selected.findIndex(value => idKey(value) === idKey(id));
    if (index >= 0) selected.splice(index, 1);
    else selected.push(id);
    this.applySelection(selected);
  },

  /* 整类全选 / 取消整类：逐个点选成本太高，尤其是「咨询解答」这类多题分类 */
  toggleCategoryGroup(e) {
    const key = e.currentTarget.dataset.key;
    const group = (this.data.categories || []).find(item => item.key === key);
    if (!group) return;
    const ids = group.scenes.map(scene => scene.id);
    const groupKeys = new Set(ids.map(idKey));
    const selected = this.data.selectedIds.slice();
    const selectedKeys = toIdSet(selected);
    const next = group.allSelected
      ? selected.filter(id => !groupKeys.has(idKey(id)))
      : selected.concat(ids.filter(id => !selectedKeys.has(idKey(id))));
    this.applySelection(next);
  },

  clearScenes() {
    if (!this.data.selectedIds.length) return;
    this.applySelection([]);
  },

  /* ── 发布对象 ── */

  selectTargetMode(e) {
    const mode = e.currentTarget.dataset.mode;
    if (!mode || mode === this.data.targetMode) return;
    this.setData({ targetMode: mode });
    /* 懒加载：只有主管真的要看学员名单时才请求，避免每次进页面都多打一次接口 */
    if (mode === 'custom' && !this.memberLoaded && !this.data.memberLoading) this.loadMembers();
  },

  loadMembers() {
    if (this.data.memberLoading) return;
    this.setData({ memberLoading: true, memberLoadFailed: false });
    api.getSupervisorMembers({ limit: MEMBER_PAGE_LIMIT }).then(data => {
      const members = (data.members || []).map(item => ({
        id: item.id,
        displayName: item.displayName || '未命名学员'
      }));
      this.memberLoaded = true;
      this.setData(Object.assign({
        members,
        memberLoading: false,
        /* 发布对象已收敛到团队，这里用团队成员总数判断列表是否被 limit 截断 */
        totalTeamMembers: Number(data.totalTeamMembers) || members.length
      }, buildMemberView(members, this.data.selectedMemberIds, this.data.memberKeyword)));
    }).catch(error => {
      this.memberLoaded = false;
      /* 失败时显式置空并给出出路，不让「发布对象」区静默空白 */
      this.setData({
        members: [], filteredMembers: [], filteredCount: 0, visibleAllSelected: false,
        memberLoading: false, memberLoadFailed: true
      });
      wx.showToast({ title: error.message || '学员列表加载失败', icon: 'none' });
    });
  },

  onMemberSearch(e) {
    const memberKeyword = e.detail.value || '';
    this.setData(Object.assign(
      { memberKeyword },
      buildMemberView(this.data.members, this.data.selectedMemberIds, memberKeyword)
    ));
  },

  applyMemberSelection(selectedIds) {
    this.setData(Object.assign({
      selectedMemberIds: selectedIds,
      selectedMemberCount: selectedIds.length
    }, buildMemberView(this.data.members, selectedIds, this.data.memberKeyword)));
  },

  toggleMember(e) {
    const id = e.currentTarget.dataset.id;
    if (!id) return;
    const selected = this.data.selectedMemberIds.slice();
    const index = selected.findIndex(value => idKey(value) === idKey(id));
    if (index >= 0) selected.splice(index, 1);
    else selected.push(id);
    this.applyMemberSelection(selected);
  },

  /* 全选 / 取消全选只作用于当前搜索结果，搜完再全选才符合直觉 */
  toggleAllMembers() {
    const visible = this.data.filteredMembers.map(item => item.id);
    if (!visible.length) return;
    const visibleKeys = new Set(visible.map(idKey));
    const selected = this.data.selectedMemberIds.slice();
    const selectedKeys = toIdSet(selected);
    const next = this.data.visibleAllSelected
      ? selected.filter(id => !visibleKeys.has(idKey(id)))
      : selected.concat(visible.filter(id => !selectedKeys.has(idKey(id))));
    this.applyMemberSelection(next);
  },

  clearMembers() {
    if (!this.data.selectedMemberIds.length) return;
    this.applyMemberSelection([]);
  },

  submit() {
    if (this.data.submitting) return;
    const title = String(this.data.title || '').trim();
    if (!title) {
      wx.showToast({ title: '请填写计划名称', icon: 'none' });
      return;
    }
    if (title.length > 100) {
      wx.showToast({ title: '计划名称不能超过 100 字', icon: 'none' });
      return;
    }
    if (!this.data.dueDate) {
      wx.showToast({ title: '请选择截止时间', icon: 'none' });
      return;
    }
    if (this.data.targetMode === 'custom' && !this.data.selectedMemberIds.length) {
      wx.showToast({ title: '请至少选择一名学员', icon: 'none' });
      return;
    }
    const dueAt = `${this.data.dueDate}T${this.data.dueTime || '23:59'}:00+08:00`;
    /* 前端先拦一次，避免提交到后端才报错；后端仍会做权威校验 */
    if (new Date(dueAt).getTime() <= Date.now()) {
      wx.showToast({ title: '截止时间需晚于当前时间', icon: 'none' });
      return;
    }
    this.setData({ submitting: true });
    api.createTrainingPlan({
      title,
      period: this.data.period,
      dueAt,
      requiredCount: this.data.requiredCount,
      requiredPassRate: this.data.requiredPassRate,
      description: String(this.data.description || '').trim(),
      scenarioIds: this.data.selectedIds,
      /* 空数组 = 全团队成员，由后端短路成对当前团队的全量指派 */
      targetUserIds: this.data.targetMode === 'custom' ? this.data.selectedMemberIds : []
    }).then(data => {
      const count = data.assignmentCount || 0;
      const skipped = data.skippedCount || 0;
      const toastTitle = skipped > 0
        ? `已发布，覆盖 ${count} 人（${skipped} 人不可用已跳过）`
        : `已发布，覆盖 ${count} 名学员`;
      wx.showToast({ title: toastTitle, icon: 'none' });
      setTimeout(() => wx.navigateBack(), 800);
    }).catch(error => {
      this.setData({ submitting: false });
      wx.showToast({ title: error.message || '发布失败', icon: 'none' });
    });
  }
});
