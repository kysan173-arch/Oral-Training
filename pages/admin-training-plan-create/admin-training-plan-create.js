const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');

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

/* 「同一场景最多计入」的派生文案。0 = 不限，与后端 max_per_scenario 语义一致
   （见 migrations/023_plan_scenario_cap.sql）。不限定场景 + 不限次数时，学员可以
   反复练最容易的那一个场景把维度分刷上去，所以默认值不动、但要把风险说出来。 */
const scenarioCapText = value => (value > 0 ? String(value) : '不限');
const scenarioCapHint = value => (value > 0
  ? `同一场景最多按 ${value} 次计入完成数，其余次数需靠其他场景补齐，防止反复练最容易的场景刷分。`
  : '不限制同一场景的计入次数。若同时不限定场景，学员可能反复练同一个场景刷分，建议按需设置。');

/* 适用场景区的折叠标题与说明：随选中数变化，模板只做属性访问 */
const sceneScopeView = (selectedCount, publishMode) => ({
  sceneScopeCaption: publishMode
    ? '至少保留 1 个'
    : (selectedCount ? `已选 ${selectedCount} 个` : '不限 · 统计全部场景'),
  sceneAdvancedHint: selectedCount
    ? `已选定 ${selectedCount} 个场景，进度只统计命中这些场景的训练。`
    : '不限定场景时，完成任意场景的训练都会计入进度。'
});

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
    /* 目标维度：默认「不限」——达标看综合分。选了具体维度后达标只看该维度均分，
       用于针对学员弱项定向补强。中文名来自后端 planDimensions()，前端不另建映射。 */
    dimensionOptions: [{ id: '', name: '不限（按综合分）' }],
    dimensionIndex: 0,
    dimensionLabel: '不限（按综合分）',
    /* 阈值标签随维度变化，否则主管会以为 60 分仍指综合分 */
    passRateLabel: '最低综合平均分',
    /* 防刷分上限：0 = 不限。与指标同组，属于「练到什么程度」的一部分 */
    maxPerScenario: 0,
    maxPerScenarioText: scenarioCapText(0),
    maxPerScenarioHint: scenarioCapHint(0),
    description: '',
    categories: [],
    selectedIds: [],
    selectedCount: 0,
    /* 场景范围默认收起：它是可选的高级约束，不该抢在指标前面要求主管决策 */
    sceneAdvancedOpen: false,
    sceneScopeCaption: '不限 · 统计全部场景',
    sceneAdvancedHint: '不限定场景时，完成任意场景的训练都会计入进度。',
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
    submitting: false,
    /* 「编辑后发布」模式：本页被 AI 建议页复用为草稿编辑器（`?mode=publish&planId=xxx`）。
       publishMode 下提交走 publishPlan（改草稿并发布），且不显示「发布对象」——
       草稿的目标学员在生成时就已确定，不该在发布环节被改成全团队。 */
    publishMode: false,
    planId: '',
    learnerName: ''
  },

  onLoad(options) {
    const query = options || {};
    /* 只有同时给出 planId 才算编辑后发布：缺了 id 就退回新建，
       否则会提交到一个空 plan_id 上，后端只会回 404 */
    const publishMode = query.mode === 'publish' && !!query.planId;
    if (publishMode) {
      wx.setNavigationBarTitle({ title: '编辑后发布' });
    }
    /* 默认截止：一周后的 23:59，避免主管每次手动选日期。
       编辑后发布时会被草稿的真实截止时间覆盖（见 applyPrefill）。 */
    const due = new Date();
    due.setDate(due.getDate() + 7);
    this.rawScenes = [];
    /* 预填：来源页通过 eventChannel 送内容要素——AI 建议页送草稿，计划详情页送原计划（重发）。
       注册要在 loadScenarios 之前，避免场景先到、预填后到时选中态丢失。 */
    const channel = typeof this.getOpenerEventChannel === 'function' ? this.getOpenerEventChannel() : null;
    if (channel && typeof channel.on === 'function') {
      channel.on('prefillPlan', payload => this.applyPrefill(payload));
    }
    this.setData(Object.assign({
      dueDate: formatDate(due),
      minDate: formatDate(new Date()),
      publishMode,
      planId: publishMode ? String(query.planId) : ''
    }, sceneScopeView(this.data.selectedIds.length, publishMode)), () => this.loadScenarios());
    /* 维度目录与场景目录互不依赖，分开取；失败时保留「不限」一项，不阻断发布 */
    this.loadDimensions();
  },

  /* 目标维度目录。取不到就把「不限」留下来：主管仍能发布计划，
     只是选不了定向补强，属于可接受的降级。 */
  loadDimensions() {
    api.getPlanDimensions().then(data => {
      const options = [{ id: '', name: '不限（按综合分）' }].concat(
        ((data && data.items) || []).map(item => ({ id: item.id, name: item.name })));
      const current = this.data.dimensionOptions[this.data.dimensionIndex];
      let index = 0;
      for (let i = 0; i < options.length; i += 1) {
        if (current && options[i].id === current.id) { index = i; break; }
      }
      this.setData({
        dimensionOptions: options,
        dimensionIndex: index,
        dimensionLabel: options[index].name,
        passRateLabel: options[index].id ? `最低「${options[index].name}」均分` : '最低综合平均分'
      });
      /* 预填先于目录到达时，在这里补选目标维度 */
      this.applyPendingDimension();
    }).catch(() => {
      /* 静默降级：目录拉不到不该弹错误打断主管填表 */
    });
  },

  /* 用原计划内容预填表单。原始指派名单接口不返回，
     targetMode 保持默认「全团队」，主管可在发布页改选成员。 */
  applyPrefill(payload) {
    if (!payload) return;
    const selectedIds = Array.isArray(payload.scenarioIds) ? payload.scenarioIds : [];
    /* 重发/编辑时必须回填防刷上限：否则重发一次会把原计划的「同场景最多 N 次」
       静默改成「不限」，达标口径被悄悄放宽。 */
    const cap = Math.max(0, Math.min(10, Number(payload.maxPerScenario) || 0));
    const patch = Object.assign({
      /* 编辑后发布用原标题；重发才加后缀，否则会变成「xx（重发）（重发）」 */
      title: payload.title ? (this.data.publishMode ? String(payload.title) : `${payload.title}（重发）`) : '',
      period: payload.period === 'month' ? 'month' : 'week',
      requiredCount: Math.max(1, Math.min(20, Number(payload.requiredCount) || 3)),
      requiredPassRate: Number(payload.requiredPassRate) || 60,
      maxPerScenario: cap,
      maxPerScenarioText: scenarioCapText(cap),
      maxPerScenarioHint: scenarioCapHint(cap),
      description: payload.description || '',
      selectedIds,
      selectedCount: selectedIds.length,
      /* 继承了原计划的场景时自动展开该区，否则主管看不到自己正在沿用哪些场景 */
      sceneAdvancedOpen: this.data.sceneAdvancedOpen || selectedIds.length > 0
    }, sceneScopeView(selectedIds.length, this.data.publishMode));
    if (this.data.publishMode) {
      /* 编辑后发布必须回填草稿的真实截止时间：本页默认「一周后 23:59」是给新建计划用的，
         直接沿用会把 AI 定的截止日悄悄改掉。 */
      const dueDate = datetime.formatDate(payload.dueAt);
      const dueTime = datetime.formatClock(payload.dueAt);
      if (dueDate) patch.dueDate = dueDate;
      if (dueTime) patch.dueTime = dueTime;
      if (payload.learnerName) patch.learnerName = String(payload.learnerName);
    }
    /* 维度选项是异步拉的，预填可能先到。先记下来、等目录到了再补选——
       否则会静默停在「不限」，发布时把草稿的达标口径从「看维度分」改成「看综合分」。 */
    this.pendingFocusDimension = payload.focusDimension || '';
    this.setData(patch, () => {
      /* 场景目录已就绪就直接重建选中态；否则等 loadScenarios 完成时
         它会按 this.data.selectedIds 重建（两条时序都覆盖） */
      if (this.rawScenes.length) {
        this.setData({ categories: buildCategories(this.rawScenes, this.data.selectedIds) });
      }
      this.applyPendingDimension();
    });
  },

  /* 把预填带来的目标维度落到选择器上。目录还没到就留着，由 loadDimensions 再调一次；
     匹配不上时不清空 pendingFocusDimension——submit 会原样提交，交给后端校验。 */
  applyPendingDimension() {
    const want = this.pendingFocusDimension || '';
    if (!want) return;
    for (let i = 0; i < this.data.dimensionOptions.length; i += 1) {
      if (this.data.dimensionOptions[i].id === want) {
        this.setData({
          dimensionIndex: i,
          dimensionLabel: this.data.dimensionOptions[i].name,
          passRateLabel: `最低「${this.data.dimensionOptions[i].name}」均分`
        });
        this.pendingFocusDimension = '';
        return;
      }
    }
  },

  /* 主管专属目录接口：学员侧的 /api/scenarios 是 learner_only，主管调用会 403 */
  loadScenarios() {
    this.setData({ sceneLoading: true, sceneLoadFailed: false });
    api.getSupervisorScenarios().then(data => {
      this.rawScenes = data.items || [];
      this.setData(Object.assign({
        categories: buildCategories(this.rawScenes, this.data.selectedIds),
        sceneLoading: false
      }, sceneScopeView(this.data.selectedIds.length, this.data.publishMode)));
    }).catch(error => {
      this.rawScenes = [];
      /* 失败时显式置空并标注，避免「适用场景」区静默空白被误认为没有场景 */
      this.setData({ categories: [], sceneLoading: false, sceneLoadFailed: true });
      api.showCenterNotice({ title: error.message || '场景加载失败' });
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

  /* 目标维度是单选 picker：索引一旦越界（目录异步替换过）就退回「不限」 */
  onDimensionChange(e) {
    const index = Number(e.detail.value) || 0;
    const option = this.data.dimensionOptions[index] || this.data.dimensionOptions[0];
    this.setData({
      dimensionIndex: index,
      dimensionLabel: option.name,
      passRateLabel: option.id ? `最低「${option.name}」均分` : '最低综合平均分'
    });
  },

  stepCount(e) {
    const delta = Number(e.currentTarget.dataset.delta) || 0;
    const next = Math.max(1, Math.min(20, this.data.requiredCount + delta));
    if (next === this.data.requiredCount) return;
    this.setData({ requiredCount: next });
  },

  /* 防刷分上限：0（不限）是合法值，所以下限是 0 而不是 1 */
  stepMaxPerScenario(e) {
    const delta = Number(e.currentTarget.dataset.delta) || 0;
    const next = Math.max(0, Math.min(10, this.data.maxPerScenario + delta));
    if (next === this.data.maxPerScenario) return;
    this.setData({
      maxPerScenario: next,
      maxPerScenarioText: scenarioCapText(next),
      maxPerScenarioHint: scenarioCapHint(next)
    });
  },

  toggleSceneAdvanced() {
    this.setData({ sceneAdvancedOpen: !this.data.sceneAdvancedOpen });
  },

  onPassRateChange(e) {
    this.setData({ requiredPassRate: Number(e.detail.value) });
  },

  applySelection(selectedIds) {
    this.setData(Object.assign({
      selectedIds,
      selectedCount: selectedIds.length,
      categories: buildCategories(this.rawScenes, selectedIds)
    }, sceneScopeView(selectedIds.length, this.data.publishMode)));
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
      api.showCenterNotice({ title: error.message || '学员列表加载失败' });
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
      api.showCenterNotice({ title: '请填写计划名称' });
      return;
    }
    if (title.length > 100) {
      api.showCenterNotice({ title: '名称最多 100 字' });
      return;
    }
    if (!this.data.dueDate) {
      api.showCenterNotice({ title: '请选择截止时间' });
      return;
    }
    /* 编辑后发布没有「发布对象」这一步：草稿在生成时就绑定了学员 */
    if (!this.data.publishMode && this.data.targetMode === 'custom' && !this.data.selectedMemberIds.length) {
      api.showCenterNotice({ title: '请至少选一名学员' });
      return;
    }
    /* 编辑后发布不能清空场景：后端 publishPlan 对空数组是「保持原值」，
       前端放行会让主管以为改了场景、其实没改。 */
    if (this.data.publishMode && !this.data.selectedIds.length) {
      api.showCenterNotice({ title: '请至少保留一个训练场景' });
      return;
    }
    const dueAt = `${this.data.dueDate}T${this.data.dueTime || '23:59'}:00+08:00`;
    /* 前端先拦一次，避免提交到后端才报错；后端仍会做权威校验 */
    if (new Date(dueAt).getTime() <= Date.now()) {
      api.showCenterNotice({ title: '截止时间需晚于现在' });
      return;
    }
    this.setData({ submitting: true });
    /* pendingFocusDimension 还留着，说明预填的维度没能落到选择器上（目录没拉到），
       原样提交让后端裁决——不能当成「不限」丢掉，那会静默改掉达标口径。 */
    const focusDimension = this.pendingFocusDimension
      || (this.data.dimensionOptions[this.data.dimensionIndex] || {}).id || '';
    const payload = {
      title,
      period: this.data.period,
      dueAt,
      requiredCount: this.data.requiredCount,
      requiredPassRate: this.data.requiredPassRate,
      /* 0 = 不限；非 0 时同一场景最多只按该次数计入完成数（迁移 023） */
      maxPerScenario: this.data.maxPerScenario,
      /* 空字符串 = 不限维度，后端按综合分判定（与历史计划语义一致） */
      focusDimension,
      description: String(this.data.description || '').trim(),
      scenarioIds: this.data.selectedIds,
      /* 空数组 = 全团队成员，由后端短路成对当前团队的全量指派 */
      targetUserIds: this.data.targetMode === 'custom' ? this.data.selectedMemberIds : []
    };
    const task = this.data.publishMode
      ? api.publishTrainingPlan(this.data.planId, payload)
      : api.createTrainingPlan(payload);
    task.then(data => {
      let toastTitle;
      if (this.data.publishMode) {
        this.pendingFocusDimension = '';
        toastTitle = `已发布给 ${this.data.learnerName || '该学员'}`;
      } else {
        const count = (data && data.assignmentCount) || 0;
        const skipped = (data && data.skippedCount) || 0;
        toastTitle = skipped > 0
          ? `已发布，覆盖 ${count} 人（${skipped} 人不可用已跳过）`
          : `已发布，覆盖 ${count} 名学员`;
      }
      wx.showToast({ title: toastTitle, icon: 'none' });
      setTimeout(() => wx.navigateBack(), 800);
    }).catch(error => {
      this.setData({ submitting: false });
      api.showCenterNotice({ title: error.message || '发布失败' });
    });
  }
});
