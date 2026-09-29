const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');

const KIND_TEXT = { violation: '合规风险', improvement: '改进练习' };
const PRIORITY_TEXT = { high: '优先复练', medium: '需要关注', practice: '练习建议' };
const PRIORITY_CLASS = { high: 'high', medium: 'medium', practice: 'practice' };
const PREVIEW_LIMIT = 26;

/* 「当时表达」原样铺开会撑爆折叠行，压成单行预览 */
const previewOf = text => {
  const value = String(text === null || text === undefined ? '' : text).replace(/\s+/g, ' ').trim();
  if (!value) return '';
  return value.length > PREVIEW_LIMIT ? `${value.slice(0, PREVIEW_LIMIT)}…` : value;
};

/* 把扁平的错题列表按训练场次归并。
   后端 listLearningMistakes 已按 finished_at DESC 下发，这里只做「保持顺序的归并」，不重排。
   展开态由 data.expandedGroups / expandedItems 独立持有，列表重建时读回，不丢。
   注意：所有派生态（dateText / summaryText / *Label）都必须在这里算好——
   WXML 里对数据路径调用函数会让依赖追踪失效，setData 后不重求值且不报错。 */
const buildGroups = (items, expandedGroups, expandedItems) => {
  const groups = [];
  const bySession = {};

  items.forEach(raw => {
    /* 分组键兜底：sessionId 缺失时退到 scenarioId + 日期，保证条目一定归得进某一组。
       绝不用 `if (!sessionId) return` 丢条目——那会让字段异常直接表现成「错题全没了」。 */
    const sessionId = String(raw.sessionId || '')
      || `fallback:${raw.scenarioId || 'unknown'}:${raw.finishedDate || 'unknown'}`;
    let group = bySession[sessionId];
    if (!group) {
      group = {
        key: sessionId,
        scenarioName: raw.scenarioName || '未命名场景',
        scenarioId: raw.scenarioId || '',
        dateText: datetime.formatMonthDay(raw.finishedDate) || '日期未知',
        items: [],
        pendingCount: 0,
        masteredCount: 0,
        highCount: 0
      };
      bySession[sessionId] = group;
      groups.push(group);
    }

    const itemId = String(raw.id || `${sessionId}:${raw.mistakeKey || ''}`);
    const mastered = raw.mastered === true;
    group.items.push({
      id: itemId,
      sessionId,
      mistakeKey: String(raw.mistakeKey || ''),
      scenarioId: group.scenarioId,
      roundLabel: `第 ${raw.round} 轮`,
      kindLabel: KIND_TEXT[raw.kind] || '练习建议',
      priorityLabel: PRIORITY_TEXT[raw.priority] || '练习建议',
      priorityClass: PRIORITY_CLASS[raw.priority] || 'practice',
      originalQuote: raw.originalQuote || '',
      previewText: previewOf(raw.originalQuote),
      reason: raw.reason || '',
      recommendedRewrite: raw.recommendedRewrite || '',
      mastered,
      /* 条目默认展开：分组是为了「看得清」，不是为了「藏起来」。
         只有用户显式收起过（记录为 false）才折叠。 */
      expanded: expandedItems[itemId] !== false
    });

    if (mastered) group.masteredCount += 1;
    else group.pendingCount += 1;
    if (!mastered && raw.priority === 'high') group.highCount += 1;
  });

  groups.forEach((group, position) => {
    if (group.pendingCount > 0) {
      group.summaryText = group.highCount > 0
        ? `${group.pendingCount} 条待复练 · ${group.highCount} 条优先`
        : `${group.pendingCount} 条待复练`;
    } else {
      group.summaryText = `${group.masteredCount} 条已掌握`;
    }
    const remembered = expandedGroups[group.key];
    group.expanded = remembered === undefined ? position === 0 : remembered === true;
  });

  return groups;
};

Page({
  data: {
    loading: true,
    groups: [],
    expandedGroups: {},
    expandedItems: {},
    includeMastered: false,
    savingId: ''
  },

  onShow() { this.loadMistakes(); },

  loadMistakes() {
    this.setData({ loading: true });
    api.getLearningMistakes({ includeMastered: this.data.includeMastered, limit: 50 }).then(data => {
      const items = (data && data.items) || [];
      this.setData({
        groups: buildGroups(items, this.data.expandedGroups, this.data.expandedItems),
        loading: false
      });
    }).catch(error => {
      this.setData({ loading: false });
      api.showCenterNotice({ title: error.message || '错题加载失败' });
    });
  },

  toggleMastered() {
    this.setData({ includeMastered: !this.data.includeMastered }, () => this.loadMistakes());
  },

  toggleGroup(e) {
    const key = String(e.currentTarget.dataset.groupKey || '');
    if (!key) return;
    const position = this.data.groups.findIndex(group => group.key === key);
    if (position < 0) return;
    const expanded = !this.data.groups[position].expanded;
    const expandedGroups = Object.assign({}, this.data.expandedGroups, { [key]: expanded });
    this.setData({ expandedGroups, [`groups[${position}].expanded`]: expanded });
  },

  toggleItem(e) {
    const itemId = String(e.currentTarget.dataset.itemId || '');
    if (!itemId) return;
    const groups = this.data.groups;
    for (let g = 0; g < groups.length; g += 1) {
      const position = groups[g].items.findIndex(item => item.id === itemId);
      if (position < 0) continue;
      const expanded = !groups[g].items[position].expanded;
      const expandedItems = Object.assign({}, this.data.expandedItems, { [itemId]: expanded });
      this.setData({
        expandedItems,
        [`groups[${g}].items[${position}].expanded`]: expanded
      });
      return;
    }
  },

  toggleMastery(e) {
    const { id, sessionId, mistakeKey, mastered } = e.currentTarget.dataset;
    if (!id || !sessionId || !mistakeKey || this.data.savingId) return;
    const isMastered = mastered === true || mastered === 'true';
    this.setData({ savingId: id });
    api.setLearningMistakeMastery(sessionId, mistakeKey, !isMastered).then(() => {
      wx.showToast({ title: isMastered ? '已恢复为待练习' : '已标记掌握', icon: 'success' });
      this.loadMistakes();
    }).catch(error => api.showCenterNotice({ title: error.message || '状态更新失败' }))
      .finally(() => this.setData({ savingId: '' }));
  },

  retrainRound(e) {
    const { sessionId, mistakeKey } = e.currentTarget.dataset;
    if (!sessionId || !mistakeKey) return;
    wx.navigateTo({
      url: `/pages/mistake-retrain/mistake-retrain?sessionId=${encodeURIComponent(sessionId)}&mistakeKey=${encodeURIComponent(mistakeKey)}`
    });
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
    }).catch(error => api.showCenterNotice({ title: error.message || '创建复练失败' }));
  },

  goPhrases() { wx.navigateTo({ url: '/pages/phrases/phrases' }); }
});
