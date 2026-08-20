const api = require('../../utils/api.js');

Page({
  data: {
    loading: true,
    mistakes: [],
    includeMastered: false,
    savingId: ''
  },

  onShow() { this.loadMistakes(); },

  loadMistakes() {
    this.setData({ loading: true });
    api.getLearningMistakes({ includeMastered: this.data.includeMastered, limit: 50 }).then(data => {
      this.setData({ mistakes: data.items || [], loading: false });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '错题加载失败', icon: 'none' });
    });
  },

  toggleMastered() {
    this.setData({ includeMastered: !this.data.includeMastered }, () => this.loadMistakes());
  },

  toggleMastery(e) {
    const { id, sessionId, mistakeKey, mastered } = e.currentTarget.dataset;
    if (!id || !sessionId || !mistakeKey || this.data.savingId) return;
    const isMastered = mastered === true || mastered === 'true';
    this.setData({ savingId: id });
    api.setLearningMistakeMastery(sessionId, mistakeKey, !isMastered).then(() => {
      wx.showToast({ title: isMastered ? '已恢复为待练习' : '已标记掌握', icon: 'success' });
      this.loadMistakes();
    }).catch(error => wx.showToast({ title: error.message || '状态更新失败', icon: 'none' }))
      .finally(() => this.setData({ savingId: '' }));
  },

  retrain(e) {
    const scenarioId = e.currentTarget.dataset.scenarioId;
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
    }).catch(error => wx.showToast({ title: error.message || '创建复练失败', icon: 'none' }));
  },

  goPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); },

  goProfile() { wx.navigateTo({ url: '/pages/profile/profile' }); }
});
