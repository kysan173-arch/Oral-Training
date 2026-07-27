const api = require('../../utils/api.js');

Page({
  data: {
    mistakes: [],
    allMistakes: [],
    loading: true,
    loadError: false,
    scenarioFilter: '',
    masteredIds: [],
    showMastered: false,
    scenarioOptions: [
      { id: '', name: '全部场景' }
    ]
  },

  onLoad() {
    this.setData({ masteredIds: wx.getStorageSync('mastered_mistakes') || [] });
    this.loadMistakes();
  },

  loadMistakes() {
    this.setData({ loading: true, loadError: false });
    api.getProfile().then(data => {
      const raw = data.mistakes || [];
      // 去重
      const seen = new Set();
      const mistakes = [];
      raw.forEach(m => {
        const key = m.id;
        if (!seen.has(key)) {
          seen.add(key);
          mistakes.push(m);
        }
      });

      // 构建场景选项
      const scenarioIds = new Set(mistakes.map(m => m.scenarioId));
      const scenarioOptions = [
        { id: '', name: '全部场景' },
        ...Array.from(scenarioIds).map(id => {
          const sample = mistakes.find(m => m.scenarioId === id);
          return { id, name: sample.scenarioName };
        })
      ];

      this.setData({ allMistakes: mistakes, scenarioOptions, loading: false });
      this.applyFilters();
    }).catch(error => {
      this.setData({ loading: false, loadError: true });
      wx.showToast({ title: error.message || '数据加载失败', icon: 'none' });
    });
  },

  applyFilters() {
    const { allMistakes, scenarioFilter, masteredIds, showMastered } = this.data;
    let mistakes = allMistakes;

    if (scenarioFilter) {
      mistakes = mistakes.filter(m => m.scenarioId === scenarioFilter);
    }

    if (!showMastered) {
      mistakes = mistakes.filter(m => !masteredIds.includes(m.id));
    }

    this.setData({ mistakes });
  },

  onScenarioFilter(e) {
    this.setData({ scenarioFilter: e.currentTarget.dataset.id }, () => this.applyFilters());
  },

  toggleMastered() {
    this.setData({ showMastered: !this.data.showMastered }, () => this.applyFilters());
  },

  // 标记为已掌握
  markMastered(e) {
    const id = e.currentTarget.dataset.id;
    const masteredIds = [...this.data.masteredIds];
    if (!masteredIds.includes(id)) {
      masteredIds.push(id);
      wx.setStorageSync('mastered_mistakes', masteredIds);
      this.setData({ masteredIds }, () => this.applyFilters());
      wx.showToast({ title: '已标记为掌握', icon: 'success' });
    }
  },

  // 取消已掌握标记
  unmarkMastered(e) {
    const id = e.currentTarget.dataset.id;
    const masteredIds = this.data.masteredIds.filter(mid => mid !== id);
    wx.setStorageSync('mastered_mistakes', masteredIds);
    this.setData({ masteredIds }, () => this.applyFilters());
    wx.showToast({ title: '已移除掌握标记', icon: 'none' });
  },

  // 去复练：跳转到对应场景的训练
  goRetrain(e) {
    const item = e.currentTarget.dataset.item;
    if (!item || !item.scenarioId) return;
    // 如果存在进行中的同场景会话则续练，否则新建
    api.createSession(item.scenarioId).then(data => {
      wx.navigateTo({ url: `/pages/training/training?sessionId=${data.session.id}` });
    }).catch(error => wx.showToast({ title: error.message || '创建训练失败', icon: 'none' }));
  },

  goProfile() {
    wx.navigateTo({ url: '/pages/profile/profile' });
  }
});
