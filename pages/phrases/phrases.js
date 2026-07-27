const api = require('../../utils/api.js');

Page({
  data: {
    groups: [],
    totalPhrases: 0,
    loading: true,
    searchKeyword: '',
    isSearching: false
  },

  onLoad(options) {
    // 从首页搜索传入
    if (options && options.search) {
      this.setData({ searchKeyword: decodeURIComponent(options.search) });
      this.loadPhrases(options.search);
    }
  },

  onShow() {
    if (!this.data.isSearching) {
      this.loadPhrases(this.data.searchKeyword || '');
    }
  },

  loadPhrases(keyword) {
    this.setData({ loading: true });
    api.getPhrases(keyword || '').then(data => {
      this.setData({
        groups: data.groups || [],
        totalPhrases: data.totalPhrases || 0,
        loading: false,
        isSearching: !!(data.keyword)
      });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '加载失败', icon: 'none' });
    });
  },

  onSearchInput(e) {
    this.setData({ searchKeyword: e.detail.value });
  },

  onSearchConfirm() {
    const keyword = this.data.searchKeyword.trim();
    this.loadPhrases(keyword);
  },

  onClearSearch() {
    this.setData({ searchKeyword: '', isSearching: false });
    this.loadPhrases('');
  },

  copyPhrase(e) {
    const phrase = e.currentTarget.dataset.phrase;
    if (!phrase) return;
    wx.setClipboardData({ data: phrase, success: () => wx.showToast({ title: '已复制', icon: 'success' }) });
  },

  goTraining(e) {
    const id = e.currentTarget.dataset.id;
    if (!id) return;
    api.createSession(id).then(data => {
      wx.navigateTo({ url: `/pages/training/training?sessionId=${data.session.id}` });
    }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  }
});
