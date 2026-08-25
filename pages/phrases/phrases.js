const api = require('../../utils/api.js');

Page({
  data: {
    loading: true,
    keyword: '',
    phrases: [],
    favoritesOnly: false,
    favoriteBusyId: ''
  },

  onLoad(options) {
    this.setData({ keyword: options.search || '', favoritesOnly: options.favorites === '1' });
    this.loadPhrases();
  },

  onSearchInput(e) { this.setData({ keyword: e.detail.value }); },

  onSearchConfirm() { this.loadPhrases(); },

  clearSearch() { this.setData({ keyword: '' }, () => this.loadPhrases()); },

  loadPhrases() {
    this.setData({ loading: true });
    api.getLearningPhrases({
      search: this.data.keyword.trim(),
      favoritesOnly: this.data.favoritesOnly,
      limit: 50
    }).then(data => {
      this.setData({ phrases: data.items || [], loading: false });
    }).catch(error => {
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
