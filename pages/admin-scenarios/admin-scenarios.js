const api = require('../../utils/api.js');
const scenario = require('../../utils/scenario.js');

/* 表单默认值：与后端 validateScenarioPayload 的约束一一对应
   （hidden ≤5 条每条 ≤60 字、instructions 5-400 字、focus 1-6 项等）。
   场景 id 选填：留空由后端自动生成，主管无需理解内部英文标识。 */
const emptyForm = () => ({
  id: '',
  name: '',
  summary: '',
  category: 'consultation',
  difficulty: 'basic',
  focusText: '',
  patientAge: '',
  patientGender: 'unknown',
  patientDescription: '',
  opening: '',
  hiddenItems: [],
  hiddenInput: '',
  emotion: '平静',
  emotionLevel: 0,
  trustLevel: 50,
  instructions: '',
  /* roleplay_config 的两个数组。迁移 025 之前这张表单完全没有它们的入口，
     于是主管自建的场景 serviceGuidance 恒为空——学员拿不到机构红线，
     合规维度也就永远测不出区分度。按行填写，与训练重点同一套写法。 */
  suggestedQuestionsText: '',
  serviceGuidanceText: '',
  maxRounds: 10,
  sortOrder: '',
  isActive: true,
  /* 复练变体池（P1-2 后半段）。每组 = 一组平行的隐藏顾虑 + 可选的披露节奏；
     留空的 instructions 表示沿用主值。提交时整组替换 hiddenConfig.variants。 */
  variants: [],
  /* 进阶档（迁移 026）。tierEnabled=false 提交时下发 `{}`（显式清掉进阶档），
     true 时带 advanced.initialState + openings。默认值取迁移 026 回填规则的中位：
     情绪升到不满/愤怒、强度 -1、信任度 -15——一个「不比标准档狠」的档位没有存在意义。 */
  tierEnabled: false,
  tierEmotion: '愤怒',
  tierEmotionLevel: -1,
  tierTrustLevel: 35,
  tierOpeningsText: '',
  /* 从哪个骨架模板开始（迁移 025）。不是场景字段，提交时作为 fromTemplateId 下发，
     后端据此补全前端没有覆盖的字段。空串 = 空白新建。 */
  fromTemplateId: ''
});

const splitLines = text => String(text || '')
  .split('\n')
  .map(line => line.trim())
  .filter(Boolean);

/* 变体组的稳定 key：wx:key 需要一个每项唯一且稳定的字段。
   用自增计数而不是 index——删掉中间一组后，index 会让后面的组「认领」前一组的输入框内容。 */
let variantKeySeq = 0;
const nextVariantKey = () => 'v' + (++variantKeySeq);

/* 变体池（P1-2）回填：每组保留输入中的草稿位，切换分组时输入框不会互相串。
   上限 5 组与后端 normalizedVariants 一致——变体是「防背答案」的手段，不是内容仓库，
   堆到十几组只会让质量失控。 */
const variantFormFields = hidden => {
  const variants = ((hidden || {}).variants || []);
  return {
    variants: variants.slice(0, 5).map(group => ({
      key: nextVariantKey(),
      hidden: ((group || {}).hidden || []).slice(0, 5),
      instructions: (group || {}).instructions || '',
      input: ''
    }))
  };
};

/* 维度侧重（迁移 024）回填：五个维度全部渲染成一行输入，已填的带值。
   目录来自后端 `/supervisor/plan-dimensions`（中文名唯一来源），页面不另写映射副本。 */
const dimensionRowsFrom = (weights, catalog) => (catalog || []).map(item => ({
  key: item.id,
  name: item.name,
  value: weights && weights[item.id] !== undefined ? String(weights[item.id]) : ''
}));

/* 从场景/模板的 difficultyTiers 派生表单字段（P1-1 档位编辑）。
   没有进阶档时给默认建议值（与迁移 026 的回填规则一致），主管打开开关就有合理起点。 */
const tierFormFields = item => {
  const tier = ((item || {}).difficultyTiers || {}).advanced;
  const state = (tier || {}).initialState || {};
  return {
    tierEnabled: Boolean(tier),
    tierEmotion: state.emotion || '愤怒',
    tierEmotionLevel: state.emotionLevel === undefined ? -1 : state.emotionLevel,
    tierTrustLevel: state.trustLevel === undefined ? 35 : state.trustLevel,
    tierOpeningsText: ((tier || {}).openings || []).join('\n')
  };
};

Page({
  data: {
    mode: 'list', // list | form | ai | aiWait
    loading: true,
    loadError: false,
    items: [],
    /* 骨架模板：与 items 同一接口的另一段，不进业务列表（迁移 025）。
       本页只用它两件事——算排序号占用（buildSortMeta）、按 id 预填表单。
       展示交给独立的模板页，本页不再渲染它。 */
    templates: [],
    /* 保存成功后的质量提醒。用列表页顶部的可关闭提示条承载，不用 showModal——
       warnings 的单条文案很长，塞进弹窗会被截断，而它们恰恰是最该被读完的内容。 */
    qualityNotice: null,
    submitting: false,
    editingId: null,
    form: emptyForm(),
    takenMap: {},
    sortFreeText: '',
    sortConflictText: '',
    categoryNames: scenario.CATEGORY_CONFIG.map(item => item.name),
    categoryIds: scenario.CATEGORY_CONFIG.map(item => item.id),
    categoryIndex: 0,
    difficultyNames: ['初级', '高级'],
    difficultyIndex: 0,
    emotionNames: ['平静', '犹豫', '焦虑', '缓和', '不满', '愤怒'],
    emotionIndex: 0,
    tierEmotionIndex: 0,
    genderNames: ['未指定', '男', '女'],
    genderValues: ['unknown', '男', '女'],
    genderIndex: 0,
    /* 维度侧重（迁移 024）。dimensionCatalog 来自后端，页面不硬编码五维——
       硬编码意味着只要后端调整维度口径，这里就会静默错位。
       dimensionRows 是编辑期间的唯一数据源，提交时才折成 {key: weight} 对象。 */
    dimensionCatalog: [],
    dimensionRows: [],
    dimensionPreviewText: '',
    dimensionEmpty: true,
    /* AI 骨架生成（迁移 030）：主管只给「分类 + 想覆盖的顾虑 + 难度」，
       教学骨架由模型产出，机构红线留给自己填。 */
    aiForm: {
      category: 'consultation',
      difficulty: 'basic',
      concerns: [],
      input: '',
      name: '',
      brief: ''
    },
    aiSubmitting: false,
    aiJob: null,
    aiStageText: '',
    /* AI 表单自己的选择器下标：与场景编辑表单的下标分开。
       共用会让「先编辑过一条 advanced 场景、再点 AI 生成」时难度选择器显示成高级。 */
    aiCategoryIndex: 0,
    aiDifficultyIndex: 0
  },

  onLoad() {
    this.loadDimensions();
    this.loadScenarios();
  },

  onUnload() {
    this.stopAiPolling();
  },

  onPullDownRefresh() {
    this.loadScenarios(() => wx.stopPullDownRefresh());
  },

  loadScenarios(done) {
    this.setData({ loading: true, loadError: false });
    api.getSupervisorScenarioCatalog().then(data => {
      const items = (data.items || []).map(item => Object.assign({}, item, {
        categoryText: scenario.categoryName(item.category),
        difficultyText: scenario.difficultyLabel(item.difficulty),
        statusText: item.isActive ? '在用' : '已下线',
        statusClass: item.isActive ? 'active' : 'offline'
      }));
      // 按排序号升序排列：主管调整顺序时列表所见即学员端所得，方便全局重排
      items.sort((a, b) => (a.sortOrder || 0) - (b.sortOrder || 0));
      /* 模板原样保留：本页要用它的完整字段预填表单，不做展示加工
         （分类名、难度名、维度侧重都交给模板页去算） */
      const templates = data.templates || [];
      const patch = { items, templates, loading: false };
      // 目录刷新后若正停在表单页（如提交失败未返回），同步刷新推荐与冲突提示。
      if (this.data.mode === 'form') {
        /* 必须把刚拿到的 templates 传进去：buildSortMeta 要算模板占用的号码，
           而此刻 this.data.templates 还是上一轮的旧值。 */
        const meta = this.buildSortMeta(this.data.editingId, templates);
        patch.takenMap = meta.takenMap;
        patch.sortFreeText = meta.sortFreeText;
        patch.sortConflictText = this.checkSortConflict();
      }
      this.setData(patch);
      if (done) done();
    }).catch(error => {
      this.setData({ loading: false, loadError: true });
      if (done) done();
      api.showCenterNotice({ title: error.message || '场景目录加载失败' });
    });
  },

  retryLoad() {
    this.loadDimensions();
    this.loadScenarios();
  },

  /* 维度目录：中文名的唯一来源在后端 planDimensions()。
     拉不到时**整个维度区都不渲染**——宁可让主管看到「本区暂不可编辑」，
     也不能渲染出一个空白的五行表单：那样一保存就会把现有侧重静默清空。 */
  loadDimensions() {
    api.getPlanDimensions().then(data => {
      const catalog = (data.items || []).map(item => ({ id: item.id, name: item.name }));
      const patch = { dimensionCatalog: catalog };
      if (this.data.mode === 'form') {
        patch.dimensionRows = dimensionRowsFrom(this.pendingDimensionWeights || {}, catalog);
        const preview = this.dimensionPreview(patch.dimensionRows);
        patch.dimensionPreviewText = preview.text;
        patch.dimensionEmpty = preview.empty;
      }
      this.setData(patch);
    }).catch(() => {
      // 静默降级：目录缺失不该挡住整个场景管理页。
      this.setData({ dimensionCatalog: [], dimensionRows: [] });
    });
  },

  /* 打开表单时同步维度侧重区。权重只从场景读一次，之后以 dimensionRows 为唯一数据源
     （不再在 form 里留第二份拷贝，两份迟早漂移）。 */
  dimensionPatch(weights) {
    this.pendingDimensionWeights = weights || {};
    const rows = dimensionRowsFrom(this.pendingDimensionWeights, this.data.dimensionCatalog);
    const preview = this.dimensionPreview(rows);
    return { dimensionRows: rows, dimensionPreviewText: preview.text, dimensionEmpty: preview.empty };
  },

  /* 归一化预览：后端会按总和归一化，所以主管填 3/1/1 与 0.6/0.2/0.2 等价。
     把这个事实直接显示出来，比要求主管自己算成 1.0 更不容易出错。 */
  dimensionPreview(rows) {
    const entries = (rows || [])
      .map(row => ({ name: row.name, value: parseFloat(row.value) }))
      .filter(entry => !isNaN(entry.value) && entry.value > 0);
    if (!entries.length) return { text: '', empty: true };
    const total = entries.reduce((sum, entry) => sum + entry.value, 0);
    const parts = entries.map(
      entry => entry.name + ' ' + Math.round(entry.value / total * 100) + '%');
    return { text: '折算后：' + parts.join('、'), empty: false };
  },

  startCreate() {
    const sortMeta = this.buildSortMeta(null);
    const form = emptyForm();
    /* 预填推荐空号：排序号必填且全表唯一，让主管自己猜一个不冲突的号码
       是纯粹的无谓负担。仍可手填覆盖。 */
    if (sortMeta.firstFree) form.sortOrder = String(sortMeta.firstFree);
    this.setData(Object.assign({
      mode: 'form',
      editingId: null,
      form,
      categoryIndex: 0,
      difficultyIndex: 0,
      emotionIndex: 0,
      tierEmotionIndex: Math.max(0, this.data.emotionNames.indexOf(form.tierEmotion)),
      genderIndex: 0,
      takenMap: sortMeta.takenMap,
      sortFreeText: sortMeta.sortFreeText,
      sortConflictText: '',
      qualityNotice: null
    }, this.dimensionPatch({})));
  },

  /* 打开独立的模板选择页。选中后由它通过 eventChannel 把模板 id 交回来。
     表单只存在于本页，模板页刻意不复制一份——两份表单意味着两套校验各自漂移。 */
  openTemplates() {
    wx.navigateTo({
      url: '/pages/scenario-templates/scenario-templates',
      success: res => {
        if (res && res.eventChannel) {
          res.eventChannel.on('pickTemplate', id => this.startFromTemplateById(id));
        }
      }
    });
  },

  dismissQualityNotice() {
    this.setData({ qualityNotice: null });
  },

  /* 从骨架模板新建：用模板内容预填**整张表单**，主管在现成内容上改，
     而不是面对空白页。这正是「低门槛新建」的关键。
     场景名称与标识刻意留空——它们是这条新场景的身份，不能沿用模板的。
     提交时带上 fromTemplateId，前端没覆盖的字段（维度权重）由后端从模板补全。 */
  startFromTemplateById(templateId) {
    const item = (this.data.templates || []).find(entry => entry.id === templateId);
    /* 模板数据没就绪（本页 templates 为空）时不能让主管卡住：改成打开空白表单并说明原因，
       否则页面会毫无反应，看起来像模板功能坏了。 */
    if (!item) {
      api.showCenterNotice({ title: '模板数据未就绪，已按空白表单打开' });
      this.startCreate();
      return;
    }
    const profile = item.patientProfile || {};
    const hidden = item.hiddenConfig || {};
    const state = hidden.initialState || {};
    const roleplay = item.roleplayConfig || {};
    const sortMeta = this.buildSortMeta(null);
    const form = Object.assign(emptyForm(), {
      summary: item.summary || '',
      category: item.category || 'consultation',
      difficulty: item.difficulty || 'basic',
      focusText: (item.focus || []).join('\n'),
      patientAge: profile.age === undefined ? '' : String(profile.age),
      patientGender: profile.gender || 'unknown',
      patientDescription: profile.description || '',
      opening: hidden.opening || '',
      hiddenItems: (hidden.hidden || []).slice(),
      emotion: state.emotion || '平静',
      emotionLevel: state.emotionLevel === undefined ? 0 : state.emotionLevel,
      trustLevel: state.trustLevel === undefined ? 50 : state.trustLevel,
      instructions: hidden.instructions || '',
      suggestedQuestionsText: (roleplay.suggestedQuestions || []).join('\n'),
      serviceGuidanceText: (roleplay.serviceGuidance || []).join('\n'),
      maxRounds: item.maxRounds || 10,
      sortOrder: sortMeta.firstFree ? String(sortMeta.firstFree) : '',
      /* 模板自带进阶档参数（026 对模板行也做了回填）：一并预填，主管可改可关 */
      ...tierFormFields(item),
      /* 模板也带该分类的典型维度侧重（025 刻意回填过）：预填后可改，
         比让主管从空白开始猜要好——空权重的场景永远不会被弱项推荐选中 */
      ...variantFormFields(hidden),
      fromTemplateId: item.id
    });
    this.setData(Object.assign({
      mode: 'form',
      editingId: null,
      form,
      categoryIndex: Math.max(0, this.data.categoryIds.indexOf(form.category)),
      difficultyIndex: form.difficulty === 'advanced' ? 1 : 0,
      emotionIndex: Math.max(0, this.data.emotionNames.indexOf(form.emotion)),
      tierEmotionIndex: Math.max(0, this.data.emotionNames.indexOf(form.tierEmotion)),
      genderIndex: Math.max(0, this.data.genderValues.indexOf(form.patientGender)),
      takenMap: sortMeta.takenMap,
      sortFreeText: sortMeta.sortFreeText,
      sortConflictText: '',
      qualityNotice: null
    }, this.dimensionPatch(item.dimensionWeights)));
  },

  startEdit(event) {
    this.openEditorById(event.currentTarget.dataset.id);
  },

  openEditorById(scenarioId) {
    const item = this.data.items.find(entry => entry.id === scenarioId);
    /* 生成中的占位场景不进编辑器：它的内容还不满足校验（空 focus、空隐藏剧本），
       进去也存不了，只会让主管以为「AI 生成的东西是坏的」。 */
    if (item && item.generating) {
      api.showCenterNotice({ title: '这条场景的 AI 骨架还在生成，完成后才能编辑' });
      return;
    }
    if (!item) return;
    const profile = item.patientProfile || {};
    const hidden = item.hiddenConfig || {};
    const state = hidden.initialState || {};
    const form = {
      id: item.id,
      name: item.name || '',
      summary: item.summary || '',
      category: item.category || 'consultation',
      difficulty: item.difficulty || 'basic',
      focusText: (item.focus || []).join('\n'),
      patientAge: profile.age === undefined ? '' : String(profile.age),
      patientGender: profile.gender || 'unknown',
      patientDescription: profile.description || '',
      opening: hidden.opening || '',
      hiddenItems: (hidden.hidden || []).slice(),
      hiddenInput: '',
      emotion: state.emotion || '平静',
      emotionLevel: state.emotionLevel === undefined ? 0 : state.emotionLevel,
      trustLevel: state.trustLevel === undefined ? 50 : state.trustLevel,
      instructions: hidden.instructions || '',
      maxRounds: item.maxRounds || 10,
      sortOrder: item.sortOrder === undefined ? '' : String(item.sortOrder),
      isActive: item.isActive !== false,
      suggestedQuestionsText: ((item.roleplayConfig || {}).suggestedQuestions || []).join('\n'),
      serviceGuidanceText: ((item.roleplayConfig || {}).serviceGuidance || []).join('\n'),
      /* 既有档位参数回填：主管能直接看到这条场景的进阶档长什么样 */
      ...tierFormFields(item),
      /* 既有变体池回填（P1-2 后半段）。以前表单不持有 variants，于是「点一下编辑
         再保存」就会把变体池清空——后端加了兜底才没出事，但不该长期依赖兜底。 */
      ...variantFormFields(hidden),
      /* 编辑既有场景不涉及模板来源 */
      fromTemplateId: '',
      /* AI 生成的骨架：编辑页顶部给一条来源说明，重点提醒机构红线还没填 */
      aiDraft: item.aiDraft === true
    };
    const sortMeta = this.buildSortMeta(item.id);
    this.setData(Object.assign({
      mode: 'form',
      editingId: item.id,
      form,
      categoryIndex: Math.max(0, this.data.categoryIds.indexOf(form.category)),
      difficultyIndex: form.difficulty === 'advanced' ? 1 : 0,
      emotionIndex: Math.max(0, this.data.emotionNames.indexOf(form.emotion)),
      tierEmotionIndex: Math.max(0, this.data.emotionNames.indexOf(form.tierEmotion)),
      genderIndex: Math.max(0, this.data.genderValues.indexOf(form.patientGender)),
      takenMap: sortMeta.takenMap,
      sortFreeText: sortMeta.sortFreeText,
      sortConflictText: '',
      qualityNotice: null
    }, this.dimensionPatch(item.dimensionWeights)));
  },

  /* 排序号提示的派生数据：只展示前 3 个空号码（推荐使用：5、6、7），
     占用再多也不刷屏；冲突判定交给输入时的实时校验（checkSortConflict）。
     编辑模式排除自身（与后端查重口径一致）。 */
  buildSortMeta(excludeId, templatesOverride) {
    const takenMap = {};
    const collect = item => {
      if (!item || item.id === excludeId) return;
      if (item.sortOrder === undefined || item.sortOrder === null) return;
      takenMap[item.sortOrder] = item;
    };
    this.data.items.forEach(collect);
    /* 骨架模板也要算进来：sort_order 是**全表唯一**，而模板占用 9xx 保留区
       （迁移 025）。漏掉它们，主管就可能被推荐到 901，然后撞上一个
       「已被其他场景占用」、却在列表里怎么也找不到占用者的报错。 */
    (templatesOverride || this.data.templates || []).forEach(collect);
    const free = [];
    for (let n = 1; n <= 999 && free.length < 3; n++) {
      if (!takenMap[n]) free.push(n);
    }
    return {
      takenMap,
      sortFreeText: free.length ? '推荐使用：' + free.join('、') : '',
      /* 新建时用它预填：省掉主管猜号码这一步，且天然避开模板的 9xx */
      firstFree: free.length ? free[0] : ''
    };
  },

  /* 输入实时校验：号码被占用时返回提示文案（含占用场景名），否则空串。 */
  checkSortConflict() {
    const value = parseInt(this.data.form.sortOrder, 10);
    if (isNaN(value)) return '';
    const hit = this.data.takenMap[value];
    return hit ? '该序号已被「' + hit.name + '」占用' : '';
  },

  backToList() {
    /* AI 骨架的两种模式没有「未保存内容」的概念：任务已经提交到服务端，
       离开页面只是不再盯着轮询（生成照旧，回到列表能看到「生成中」标记）。 */
    if (this.data.mode === 'ai' || this.data.mode === 'aiWait') {
      this.stopAiPolling();
      this.setData({ mode: 'list', aiJob: null, aiStageText: '', aiSubmitting: false });
      this.loadScenarios();
      return;
    }
    // 表单已填内容时拦截返回，防误触丢稿（左上角按钮 / 页内「返回列表」都走这里）
    if (!this.data.submitting && this.isFormDirty()) {
      wx.showModal({
        title: '放弃当前内容？',
        content: '返回列表后，未保存的修改将全部丢失。',
        confirmText: '放弃修改',
        cancelText: '继续编辑',
        success: result => {
          if (result.confirm) this.setData({ mode: 'list' });
        }
      });
      return;
    }
    this.setData({ mode: 'list' });
  },

  /* 表单脏检查：任一实质字段有值即视为已填写（hiddenInput 是输入中草稿，也算） */
  isFormDirty() {
    const f = this.data.form;
    return Boolean(
      f.name || f.summary || f.focusText || f.patientDescription ||
      f.opening || f.instructions || (f.hiddenItems && f.hiddenItems.length) ||
      f.hiddenInput || f.suggestedQuestionsText || f.serviceGuidanceText ||
      f.tierOpeningsText || (f.variants && f.variants.length) ||
      (f.id && this.data.editingId === null)
    );
  },

  onFieldInput(event) {
    const key = event.currentTarget.dataset.field;
    this.setData({ [`form.${key}`]: event.detail.value });
    if (key === 'sortOrder') {
      this.setData({ sortConflictText: this.checkSortConflict() });
    }
  },

  /* 隐藏顾虑逐条添加：输入后点「添加」入列，每条可单独删除，
     不再要求严格的一行一条格式。 */
  addHiddenItem() {
    const text = String(this.data.form.hiddenInput || '').trim();
    if (!text) {
      api.showCenterNotice({ title: '请先输入一条隐藏顾虑' });
      return;
    }
    if (text.length > 60) {
      api.showCenterNotice({ title: '单条不能超过 60 个字' });
      return;
    }
    const items = this.data.form.hiddenItems;
    if (items.length >= 5) {
      wx.showToast({ title: '最多添加 5 条', icon: 'none' });
      return;
    }
    if (items.indexOf(text) >= 0) {
      wx.showToast({ title: '该条已存在', icon: 'none' });
      return;
    }
    this.setData({
      'form.hiddenItems': items.concat(text),
      'form.hiddenInput': ''
    });
  },

  removeHiddenItem(event) {
    const index = Number(event.currentTarget.dataset.index);
    const items = this.data.form.hiddenItems.slice();
    if (index < 0 || index >= items.length) return;
    items.splice(index, 1);
    this.setData({ 'form.hiddenItems': items });
  },

  /* ── 复练变体池（P1-2 后半段）──────────────────────────────────────
     每组是一份「平行的隐藏剧本」：同一场景骨架、换一组顾虑与松口条件，
     学员第二次、第三次练到的就不是同一套答案。上限 5 组与后端一致。 */
  addVariant() {
    const variants = this.data.form.variants.slice();
    if (variants.length >= 5) {
      wx.showToast({ title: '最多 5 组', icon: 'none' });
      return;
    }
    variants.push({ key: nextVariantKey(), hidden: [], instructions: '', input: '' });
    this.setData({ 'form.variants': variants });
  },

  removeVariant(event) {
    const group = Number(event.currentTarget.dataset.group);
    const variants = this.data.form.variants.slice();
    if (group < 0 || group >= variants.length) return;
    variants.splice(group, 1);
    this.setData({ 'form.variants': variants });
  },

  onVariantInput(event) {
    const group = Number(event.currentTarget.dataset.group);
    this.setData({ [`form.variants[${group}].input`]: event.detail.value });
  },

  onVariantInstructions(event) {
    const group = Number(event.currentTarget.dataset.group);
    this.setData({ [`form.variants[${group}].instructions`]: event.detail.value });
  },

  addVariantHidden(event) {
    const group = Number(event.currentTarget.dataset.group);
    const variants = this.data.form.variants.slice();
    if (group < 0 || group >= variants.length) return;
    const target = variants[group];
    const text = String(target.input || '').trim();
    if (!text) {
      api.showCenterNotice({ title: '请先输入一条隐藏顾虑' });
      return;
    }
    if (text.length > 60) {
      api.showCenterNotice({ title: '单条不能超过 60 个字' });
      return;
    }
    if (target.hidden.length >= 5) {
      wx.showToast({ title: '最多添加 5 条', icon: 'none' });
      return;
    }
    if (target.hidden.indexOf(text) >= 0) {
      wx.showToast({ title: '该条已存在', icon: 'none' });
      return;
    }
    target.hidden = target.hidden.concat(text);
    target.input = '';
    this.setData({ 'form.variants': variants });
  },

  removeVariantHidden(event) {
    const { group, index } = event.currentTarget.dataset;
    const variants = this.data.form.variants.slice();
    const target = variants[Number(group)];
    if (!target) return;
    const items = target.hidden.slice();
    const position = Number(index);
    if (position < 0 || position >= items.length) return;
    items.splice(position, 1);
    target.hidden = items;
    this.setData({ 'form.variants': variants });
  },

  /* ── 维度侧重（迁移 024）────────────────────────────────────────── */
  onDimensionWeightInput(event) {
    const key = event.currentTarget.dataset.key;
    const rows = this.data.dimensionRows.slice();
    const row = rows.find(entry => entry.key === key);
    if (!row) return;
    // 只留数字与小数点：中文输入法或粘贴都可能带进别的字符，
    // 那种值到了后端会被归一化丢弃，主管却以为自己填了。
    row.value = String(event.detail.value || '').replace(/[^\d.]/g, '');
    const preview = this.dimensionPreview(rows);
    this.setData({
      dimensionRows: rows,
      dimensionPreviewText: preview.text,
      dimensionEmpty: preview.empty
    });
  },

  onEmotionLevelInput(event) {
    const value = parseInt(event.detail.value, 10);
    this.setData({ 'form.emotionLevel': isNaN(value) ? 0 : Math.max(-2, Math.min(2, value)) });
  },

  onTrustLevelInput(event) {
    const value = parseInt(event.detail.value, 10);
    this.setData({ 'form.trustLevel': isNaN(value) ? 50 : Math.max(0, Math.min(100, value)) });
  },

  /* ── 进阶档（迁移 026）────────────────────────────── */
  onTierEnabledChange(event) {
    this.setData({ 'form.tierEnabled': event.detail.value });
  },

  onTierEmotionChange(event) {
    const index = Number(event.detail.value);
    this.setData({
      tierEmotionIndex: index,
      'form.tierEmotion': this.data.emotionNames[index]
    });
  },

  onTierEmotionLevelInput(event) {
    const value = parseInt(event.detail.value, 10);
    this.setData({ 'form.tierEmotionLevel': isNaN(value) ? 0 : Math.max(-2, Math.min(2, value)) });
  },

  onTierTrustLevelInput(event) {
    const value = parseInt(event.detail.value, 10);
    this.setData({ 'form.tierTrustLevel': isNaN(value) ? 50 : Math.max(0, Math.min(100, value)) });
  },

  onCategoryChange(event) {
    const index = Number(event.detail.value);
    this.setData({
      categoryIndex: index,
      'form.category': this.data.categoryIds[index]
    });
  },

  onDifficultyChange(event) {
    const index = Number(event.detail.value);
    this.setData({
      difficultyIndex: index,
      'form.difficulty': index === 1 ? 'advanced' : 'basic'
    });
  },

  onEmotionChange(event) {
    const index = Number(event.detail.value);
    this.setData({
      emotionIndex: index,
      'form.emotion': this.data.emotionNames[index]
    });
  },

  onGenderChange(event) {
    const index = Number(event.detail.value);
    this.setData({
      genderIndex: index,
      'form.patientGender': this.data.genderValues[index]
    });
  },

  onActiveChange(event) {
    this.setData({ 'form.isActive': event.detail.value });
  },

  buildPayload() {
    const form = this.data.form;
    const patientAge = parseInt(form.patientAge, 10);
    const maxRounds = parseInt(form.maxRounds, 10);
    const sortOrder = parseInt(form.sortOrder, 10);
    /* 变体池：显式提交（含空数组）。空数组 = 主管清空了变体池，语义明确；
       不带这个键才是「保持不变」——那是给旧版小程序客户端留的兼容路径，
       新表单必须显式表达意图，否则「删掉一组变体」这个操作会静默无效。 */
    const variants = [];
    form.variants.forEach(group => {
      const hidden = (group.hidden || []).filter(text => String(text).trim());
      if (!hidden.length) return;
      const variant = { hidden };
      const instructions = String(group.instructions || '').trim();
      if (instructions) variant.instructions = instructions;
      variants.push(variant);
    });
    const payload = {
      name: form.name.trim(),
      summary: form.summary.trim(),
      category: form.category,
      difficulty: form.difficulty,
      focus: splitLines(form.focusText),
      patientProfile: {
        age: isNaN(patientAge) ? 0 : patientAge,
        gender: form.patientGender,
        description: form.patientDescription.trim()
      },
      hiddenConfig: {
        opening: form.opening.trim(),
        hidden: form.hiddenItems.slice(),
        initialState: {
          emotion: form.emotion,
          emotionLevel: form.emotionLevel,
          trustLevel: form.trustLevel
        },
        instructions: form.instructions.trim(),
        variants
      },
      roleplayConfig: {
        suggestedQuestions: splitLines(form.suggestedQuestionsText),
        serviceGuidance: splitLines(form.serviceGuidanceText)
      },
      maxRounds: isNaN(maxRounds) ? 0 : maxRounds,
      sortOrder: isNaN(sortOrder) ? 0 : sortOrder,
      isActive: form.isActive
    };
    /* 维度侧重：目录没加载成功时**整个键都不发**。发一个空对象等于让后端
       把现有侧重归一化成「未标注」，而不发送走的是「保留原值」——两者差别很大，
       恰好是这条场景还能不能被弱项推荐选中的差别。 */
    if (this.data.dimensionCatalog.length) {
      const weights = {};
      this.data.dimensionRows.forEach(row => {
        const value = parseFloat(row.value);
        if (!isNaN(value) && value > 0) weights[row.key] = value;
      });
      payload.dimensionWeights = weights;
    }
    /* 档位随表单一起提交。关闭开关 = 显式下发 `{}` 清掉进阶档（后端 PUT 不带该字段
       时是「保留原值」，所以想关就必须明确发空对象）；summary 固定用回填文案，
       学员端挑战确认弹窗直接展示它。 */
    payload.difficultyTiers = form.tierEnabled
      ? { advanced: {
          summary: '信任度起点更低、开场情绪更强，一句安抚不会让患者松口',
          initialState: {
            emotion: form.tierEmotion,
            emotionLevel: form.tierEmotionLevel,
            trustLevel: form.tierTrustLevel
          },
          openings: splitLines(form.tierOpeningsText).slice(0, 3)
        } }
      : {};
    if (this.data.editingId === null) {
      const id = form.id.trim();
      // 留空 = 后端自动生成；填了才带上自定义 id。
      if (id) payload.id = id;
      /* 从模板新建时告知来源：前端没覆盖的字段由后端从模板补全。
         模板的 id 与排序号不会被继承（后端显式丢弃，否则会撞 id 与唯一索引）。 */
      if (form.fromTemplateId) payload.fromTemplateId = form.fromTemplateId;
    }
    return payload;
  },

  submitForm() {
    if (this.data.submitting) return;
    /* 变体组没填顾虑会被后端静默丢弃（normalizedVariants 只保留有 hidden 的组）。
       主管以为存了 3 组、实际只存了 2 组，比直接报错更糟——所以在提交前拦住。 */
    const emptyVariant = this.data.form.variants.findIndex(
      group => !(group.hidden || []).length);
    if (emptyVariant >= 0) {
      api.showCenterNotice({
        title: `变体 ${emptyVariant + 1} 还没有填隐藏顾虑，请补充或删除该组`
      });
      return;
    }
    const payload = this.buildPayload();
    if (!payload.name) {
      api.showCenterNotice({ title: '请填写场景名称' });
      return;
    }
    if (!payload.hiddenConfig.hidden.length) {
      api.showCenterNotice({ title: '请至少添加 1 条隐藏顾虑' });
      return;
    }
    const isCreate = this.data.editingId === null;
    this.setData({ submitting: true });
    const request = isCreate
      ? api.createSupervisorScenario(payload)
      : api.updateSupervisorScenario(this.data.editingId, payload);
    request.then(data => {
      /* warnings 是《训练场景设计规范》六条判据里机器能判的部分（后端下发）。
         有提醒时不弹 toast——提示条本身就是结果，再来个 toast 只会互相盖住。 */
      const warnings = (data && data.warnings) || [];
      this.setData({
        submitting: false,
        mode: 'list',
        qualityNotice: warnings.length ? { name: payload.name, items: warnings } : null
      });
      if (!warnings.length) {
        wx.showToast({ title: isCreate ? '场景已创建' : '场景已保存', icon: 'success' });
      }
      this.loadScenarios();
    }).catch(error => {
      this.setData({ submitting: false });
      api.showCenterNotice({ title: error.message || '保存失败' });
    });
  },

  toggleActive(event) {
    const { id, active } = event.currentTarget.dataset;
    const nextActive = !active;
    wx.showModal({
      title: nextActive ? '上架场景' : '下线场景',
      content: nextActive
        ? '上架后学员端立即可见该场景。'
        : '下线后学员端不再展示该场景，已有训练记录保留。',
      success: result => {
        if (!result.confirm) return;
        api.updateSupervisorScenario(id, { isActive: nextActive }).then(() => {
          wx.showToast({ title: nextActive ? '已上架' : '已下线', icon: 'success' });
          this.loadScenarios();
        }).catch(error => {
          api.showCenterNotice({ title: error.message || '操作失败' });
        });
      }
    });
  },

  /* ── AI 骨架生成（迁移 030）──────────────────────────────────────
     主管只给「分类 + 想覆盖的顾虑 + 难度」；模型产出教学骨架（诉求落差、三条性质
     错开的顾虑、可判定的缓和门、升级条件、施压式开场白），机构红线留空给主管填。
     前端只做三件事：提交任务、轮询状态、成功后把人送进编辑器。 */
  startAiDraft() {
    this.setData({
      mode: 'ai',
      aiForm: {
        category: this.data.categoryIds[0] || 'consultation',
        difficulty: 'basic',
        concerns: [],
        input: '',
        name: '',
        brief: ''
      },
      aiSubmitting: false,
      aiJob: null,
      aiStageText: '',
      aiCategoryIndex: 0,
      aiDifficultyIndex: 0
    });
  },

  onAiFieldInput(event) {
    const key = event.currentTarget.dataset.field;
    this.setData({ [`aiForm.${key}`]: event.detail.value });
  },

  onAiCategoryChange(event) {
    const index = Number(event.detail.value);
    this.setData({
      aiCategoryIndex: index,
      'aiForm.category': this.data.categoryIds[index]
    });
  },

  onAiDifficultyChange(event) {
    const index = Number(event.detail.value);
    this.setData({
      aiDifficultyIndex: index,
      'aiForm.difficulty': index === 1 ? 'advanced' : 'basic'
    });
  },

  addAiConcern() {
    const text = String(this.data.aiForm.input || '').trim();
    if (!text) {
      api.showCenterNotice({ title: '请先输入一条想覆盖的顾虑' });
      return;
    }
    if (text.length > 60) {
      api.showCenterNotice({ title: '单条不能超过 60 个字' });
      return;
    }
    const concerns = this.data.aiForm.concerns;
    if (concerns.length >= 5) {
      wx.showToast({ title: '最多添加 5 条', icon: 'none' });
      return;
    }
    if (concerns.indexOf(text) >= 0) {
      wx.showToast({ title: '该条已存在', icon: 'none' });
      return;
    }
    this.setData({ 'aiForm.concerns': concerns.concat(text), 'aiForm.input': '' });
  },

  removeAiConcern(event) {
    const index = Number(event.currentTarget.dataset.index);
    const concerns = this.data.aiForm.concerns.slice();
    if (index < 0 || index >= concerns.length) return;
    concerns.splice(index, 1);
    this.setData({ 'aiForm.concerns': concerns });
  },

  submitAiDraft() {
    if (this.data.aiSubmitting) return;
    const aiForm = this.data.aiForm;
    if (!aiForm.concerns.length) {
      api.showCenterNotice({ title: '请至少填写 1 条想覆盖的顾虑' });
      return;
    }
    this.setData({ aiSubmitting: true, aiStageText: '正在提交生成任务…' });
    api.createScenarioAiDraft({
      category: aiForm.category,
      difficulty: aiForm.difficulty,
      concerns: aiForm.concerns.slice(),
      name: String(aiForm.name || '').trim(),
      brief: String(aiForm.brief || '').trim(),
      /* 幂等键防连点重复建任务：同一次提交重复发送会返回同一个任务，
         而不是在场景列表里留下两条「AI 生成中」的空壳。 */
      idempotencyKey: `scenario-ai-${Date.now()}-${Math.random().toString(16).slice(2)}`
    }).then(job => {
      this.setData({
        mode: 'aiWait',
        aiSubmitting: false,
        aiJob: job,
        aiStageText: this.aiStatusText(job)
      });
      this.pollAiJob(job.jobId);
    }).catch(error => {
      this.setData({ aiSubmitting: false, aiStageText: '' });
      api.showCenterNotice({ title: error.message || '生成任务提交失败' });
    });
  },

  aiStatusText(job) {
    const status = (job || {}).status;
    if (status === 'pending') return '任务已排队，等待生成…';
    if (status === 'running') return '模型正在设计教学骨架…';
    if (status === 'retry_wait') return '生成失败，稍后自动重试…';
    if (status === 'succeeded') return '骨架已生成';
    if (status === 'dead') return '生成失败：' + ((job || {}).errorMessage || '未知原因');
    return '正在生成…';
  },

  /* 轮询 1.5 秒一次，最多约 4 分钟。超时不清任务：主管可以重进列表看「生成中」标记，
     或者手动重试——轮询只是这一个页面的等待方式，不是任务的存活条件。 */
  pollAiJob(jobId) {
    this.stopAiPolling();
    let polls = 0;
    const tick = () => {
      this.aiPollTimer = setTimeout(() => {
        api.getScenarioAiDraft(jobId).then(job => {
          if (job.status === 'succeeded') {
            this.finishAiDraft(job);
            return;
          }
          this.setData({ aiJob: job, aiStageText: this.aiStatusText(job) });
          if (job.status === 'dead') return; // 停在失败态，等主管点「重试」
          polls += 1;
          if (polls > 160) {
            this.setData({ aiStageText: '生成时间较长，已停止等待。可稍后回到列表查看，或重试。' });
            return;
          }
          tick();
        }).catch(error => {
          this.setData({ aiStageText: '查询任务失败：' + (error.message || '网络异常') });
        });
      }, 1500);
    };
    tick();
  },

  stopAiPolling() {
    if (this.aiPollTimer) {
      clearTimeout(this.aiPollTimer);
      this.aiPollTimer = null;
    }
  },

  /* 成功后直接进编辑器：这一步是设计意图，不是顺手。
     骨架的价值在于「在现成内容上改」，把人丢回列表再让他自己找那条已下线场景，
     中间的每一次点击都在劝退。 */
  finishAiDraft(job) {
    this.stopAiPolling();
    const result = job.result || {};
    const scenarioId = result.scenarioId || job.draftId;
    this.setData({ mode: 'list', aiJob: null, aiStageText: '', qualityNotice: null });
    /* 走 showCenterNotice 而不是 showToast：这句话是关键指引（骨架已就位，
       但还缺机构红线），而 toast 一行最多 7 个汉字，必然被裁掉后半句。 */
    api.showCenterNotice({ title: '骨架已生成，请补齐「服务要点」再上架' });
    this.loadScenarios(() => {
      if (scenarioId) this.openEditorById(scenarioId);
    });
  },

  retryAiDraft() {
    const job = this.data.aiJob;
    if (!job) return;
    this.setData({ aiStageText: '正在重新排队…' });
    api.retryScenarioAiDraft(job.jobId).then(updated => {
      this.setData({ aiJob: updated, aiStageText: this.aiStatusText(updated) });
      this.pollAiJob(job.jobId);
    }).catch(error => {
      api.showCenterNotice({ title: error.message || '重试失败' });
    });
  }
});
