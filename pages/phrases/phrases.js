const api = require('../../utils/api.js');

/* 空态文案要区分「这个分类下没有」和「一条都还没有」：
   否则用户点了某个分类后看到「完成训练并生成报告后…」，会以为自己没练过。 */
const buildEmptyCopy = (sceneCategories, sceneCategory, favoritesOnly) => {
  const category = (sceneCategories || []).find(item => item.id === sceneCategory);
  if (category) {
    return favoritesOnly
      ? { title: `「${category.name}」下暂无收藏`, text: '切换其他分类，或在话术锦囊中收藏该场景的话术后再来查看。' }
      : { title: `「${category.name}」下暂无话术`, text: '完成该场景的客服训练并生成报告后，优化表达会自动出现在这里。' };
  }
  return favoritesOnly
    ? { title: '还没有收藏话术', text: '在话术锦囊中点击收藏后，可在这里集中回顾。' }
    : { title: '还没有可收录的话术', text: '完成客服训练并生成报告后，关键轮次的优化表达会自动出现在这里。' };
};

Page({
  data: {
    loading: true,
    keyword: '',
    phrases: [],
    favoritesOnly: false,
    favoriteBusyId: '',
    /* 场景分类筛选：'' = 全部。中文名只取后端下发的 sceneCategories，前端不另写映射 */
    sceneCategory: '',
    sceneCategories: [],
    emptyCopy: buildEmptyCopy([], '', false)
  },

  onLoad(options) {
    this.setData({
      keyword: options.search || '',
      favoritesOnly: options.favorites === '1',
      sceneCategory: options.category || ''
    });
    this.loadPhrases();
  },

  onSearchInput(e) { this.setData({ keyword: e.detail.value }); },

  onSearchConfirm() { this.loadPhrases(); },

  clearSearch() { this.setData({ keyword: '' }, () => this.loadPhrases()); },

  /* 分类在服务端过滤（后端在 LIMIT 之前生效），不是拿到列表后再筛 */
  selectSceneCategory(e) {
    const sceneCategory = e.currentTarget.dataset.category || '';
    if (sceneCategory === this.data.sceneCategory) return;
    this.setData({ sceneCategory }, () => this.loadPhrases());
  },

  loadPhrases() {
    this.phraseRequestVersion = (this.phraseRequestVersion || 0) + 1;
    const requestVersion = this.phraseRequestVersion;
    this.setData({ loading: true });
    api.getLearningPhrases({
      search: this.data.keyword.trim(),
      sceneCategory: this.data.sceneCategory,
      favoritesOnly: this.data.favoritesOnly,
      limit: 50
    }).then(data => {
      if (requestVersion !== this.phraseRequestVersion) return;
      const sceneCategories = data.sceneCategories || this.data.sceneCategories;
      this.setData({
        phrases: data.items || [],
        sceneCategories,
        emptyCopy: buildEmptyCopy(sceneCategories, this.data.sceneCategory, this.data.favoritesOnly),
        loading: false
      });
    }).catch(error => {
      if (requestVersion !== this.phraseRequestVersion) return;
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '话术加载失败', icon: 'none' });
    });
  },

  copyPhrase(e) {
    const phrase = e.currentTarget.dataset.phrase;
    if (!phrase) return;
    wx.setClipboardData({ data: phrase, success: () => wx.showToast({ title: '已复制话术', icon: 'success' }) });
  },

  selectPhraseView(e) {
    const favoritesOnly = e.currentTarget.dataset.favorites === 'true';
    if (favoritesOnly === this.data.favoritesOnly) return;
    this.setData({ favoritesOnly }, () => this.loadPhrases());
  },

  toggleFavorite(e) {
    const { sessionId, phraseKey } = e.currentTarget.dataset;
    const phrase = this.data.phrases.find(item => item.sessionId === sessionId && item.phraseKey === phraseKey);
    if (!phrase || this.data.favoriteBusyId) return;
    const favorite = !phrase.favorited;
    this.setData({ favoriteBusyId: phrase.id });
    api.setLearningPhraseFavorite(sessionId, phraseKey, favorite).then(() => {
      const phrases = this.data.favoritesOnly && !favorite
        ? this.data.phrases.filter(item => item.id !== phrase.id)
        : this.data.phrases.map(item => item.id === phrase.id ? Object.assign({}, item, { favorited: favorite }) : item);
      this.setData({ phrases, favoriteBusyId: '' });
      wx.showToast({ title: favorite ? '已收藏话术' : '已取消收藏', icon: 'success' });
    }).catch(error => {
      this.setData({ favoriteBusyId: '' });
      wx.showToast({ title: error.message || '收藏操作失败', icon: 'none' });
    });
  },

  startScenario(e) {
    const scenarioId = e.currentTarget.dataset.id;
    if (!scenarioId) return;
    api.getScenarios().then(data => {
      const scenario = (data.items || []).find(item => item.id === scenarioId);
      if (scenario && scenario.activeSession) {
        wx.navigateTo({ url: `/pages/training/training?sessionId=${scenario.activeSession.id}` });
        return null;
      }
      return api.createSession(scenarioId);
    }).then(data => {
      if (data && data.session) {
        wx.navigateTo({ url: `/pages/training/training?sessionId=${data.session.id}` });
      }
    }).catch(error => wx.showToast({ title: error.message || '创建训练失败', icon: 'none' }));
  }
});
