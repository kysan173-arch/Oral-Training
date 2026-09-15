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
  maxRounds: 10,
  sortOrder: '',
  isActive: true
});

const splitLines = text => String(text || '')
  .split('\n')
  .map(line => line.trim())
  .filter(Boolean);

Page({
  data: {
    mode: 'list', // list | form
    loading: true,
    loadError: false,
    items: [],
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
    emotionNames: ['平静', '犹豫', '焦虑', '缓和'],
    emotionIndex: 0,
    genderNames: ['未指定', '男', '女'],
    genderValues: ['unknown', '男', '女'],
    genderIndex: 0
  },

  onLoad() {
    this.loadScenarios();
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
      const patch = { items, loading: false };
      // 目录刷新后若正停在表单页（如提交失败未返回），同步刷新推荐与冲突提示。
      if (this.data.mode === 'form') {
        const meta = this.buildSortMeta(this.data.editingId);
        patch.takenMap = meta.takenMap;
        patch.sortFreeText = meta.sortFreeText;
        patch.sortConflictText = this.checkSortConflict();
      }
      this.setData(patch);
      if (done) done();
    }).catch(error => {
      this.setData({ loading: false, loadError: true });
      if (done) done();
      wx.showToast({ title: error.message || '场景目录加载失败', icon: 'none' });
    });
  },

  retryLoad() {
    this.loadScenarios();
  },

  startCreate() {
    const sortMeta = this.buildSortMeta(null);
    this.setData({
      mode: 'form',
      editingId: null,
      form: emptyForm(),
      categoryIndex: 0,
      difficultyIndex: 0,
      emotionIndex: 0,
      genderIndex: 0,
      takenMap: sortMeta.takenMap,
      sortFreeText: sortMeta.sortFreeText,
      sortConflictText: ''
    });
  },

  startEdit(event) {
    const item = this.data.items.find(entry => entry.id === event.currentTarget.dataset.id);
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
      isActive: item.isActive !== false
    };
    const sortMeta = this.buildSortMeta(item.id);
    this.setData({
      mode: 'form',
      editingId: item.id,
      form,
      categoryIndex: Math.max(0, this.data.categoryIds.indexOf(form.category)),
      difficultyIndex: form.difficulty === 'advanced' ? 1 : 0,
      emotionIndex: Math.max(0, this.data.emotionNames.indexOf(form.emotion)),
      genderIndex: Math.max(0, this.data.genderValues.indexOf(form.patientGender)),
      takenMap: sortMeta.takenMap,
      sortFreeText: sortMeta.sortFreeText,
      sortConflictText: ''
    });
  },

  /* 排序号提示的派生数据：只展示前 3 个空号码（推荐使用：5、6、7），
     占用再多也不刷屏；冲突判定交给输入时的实时校验（checkSortConflict）。
     编辑模式排除自身（与后端查重口径一致）。 */
  buildSortMeta(excludeId) {
    const takenMap = {};
    this.data.items.forEach(item => {
      if (item.id === excludeId) return;
      if (item.sortOrder === undefined || item.sortOrder === null) return;
      takenMap[item.sortOrder] = item;
    });
    const free = [];
    for (let n = 1; n <= 999 && free.length < 3; n++) {
      if (!takenMap[n]) free.push(n);
    }
    return {
      takenMap,
      sortFreeText: free.length ? '推荐使用：' + free.join('、') : ''
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
      f.hiddenInput || (f.id && this.data.editingId === null)
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
      wx.showToast({ title: '请先输入一条隐藏顾虑', icon: 'none' });
      return;
    }
    if (text.length > 60) {
      wx.showToast({ title: '单条不能超过 60 个字', icon: 'none' });
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

  onEmotionLevelInput(event) {
    const value = parseInt(event.detail.value, 10);
    this.setData({ 'form.emotionLevel': isNaN(value) ? 0 : Math.max(-2, Math.min(2, value)) });
  },

  onTrustLevelInput(event) {
    const value = parseInt(event.detail.value, 10);
    this.setData({ 'form.trustLevel': isNaN(value) ? 50 : Math.max(0, Math.min(100, value)) });
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
        instructions: form.instructions.trim()
      },
      maxRounds: isNaN(maxRounds) ? 0 : maxRounds,
      sortOrder: isNaN(sortOrder) ? 0 : sortOrder,
      isActive: form.isActive
    };
    if (this.data.editingId === null) {
      const id = form.id.trim();
      // 留空 = 后端自动生成；填了才带上自定义 id。
      if (id) payload.id = id;
    }
    return payload;
  },

  submitForm() {
    if (this.data.submitting) return;
    const payload = this.buildPayload();
    if (!payload.name) {
      wx.showToast({ title: '请填写场景名称', icon: 'none' });
      return;
    }
    if (!payload.hiddenConfig.hidden.length) {
      wx.showToast({ title: '请至少添加 1 条隐藏顾虑', icon: 'none' });
      return;
    }
    this.setData({ submitting: true });
    const request = this.data.editingId === null
      ? api.createSupervisorScenario(payload)
      : api.updateSupervisorScenario(this.data.editingId, payload);
    request.then(() => {
      this.setData({ submitting: false, mode: 'list' });
      wx.showToast({
        title: this.data.editingId === null ? '场景已创建' : '场景已保存',
        icon: 'success'
      });
      this.loadScenarios();
    }).catch(error => {
      this.setData({ submitting: false });
      wx.showToast({ title: error.message || '保存失败', icon: 'none' });
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
          wx.showToast({ title: error.message || '操作失败', icon: 'none' });
        });
      }
    });
  }
});
