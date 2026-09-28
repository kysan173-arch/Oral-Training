const api = require('../../utils/api.js');
const scenario = require('../../utils/scenario.js');

/* 骨架模板选择页。
   独立成页而不是塞在场景管理页里折叠：8 条模板会把「场景列表」压到首屏之外，
   而两者是两种不同的意图（一个是「我要挑一条改」，一个是「我要看现在有哪些场景」）。 */
Page({
  data: {
    loading: true,
    failed: false,
    groups: [],
    total: 0
  },

  onLoad() { this.load(); },

  onPullDownRefresh() { this.load(() => wx.stopPullDownRefresh()); },

  load(done) {
    this.setData({ loading: true, failed: false });
    api.getSupervisorScenarioCatalog().then(data => {
      const templates = data.templates || [];
      /* 按分类分组：主管的思路是「先想练哪一类，再挑骨架」。
         平铺 8 条会让分类信息淹在列表里，还得靠自己找。 */
      const groups = scenario.CATEGORY_CONFIG.map(category => {
        const items = templates
          .filter(item => item.category === category.id)
          .map(item => ({
            id: item.id,
            name: item.name,
            summary: item.summary || '',
            difficultyText: scenario.difficultyLabel(item.difficulty),
            /* 维度侧重：让主管在选之前就知道这个骨架练什么 */
            focusText: (item.dimensionFocus || []).map(entry => entry.name).join(' · ')
          }));
        return {
          id: category.id,
          name: category.name,
          description: category.description,
          items
        };
      }).filter(group => group.items.length > 0);
      this.setData({ groups, total: templates.length, loading: false });
      if (done) done();
    }).catch(error => {
      this.setData({ loading: false, failed: true });
      if (done) done();
      api.showCenterNotice({ title: error.message || '模板加载失败' });
    });
  },

  retry() { this.load(); },

  /* 选中模板：只把 id 交回训练场景管理页，由它进入预填好的表单。
     刻意不在这里也放一份表单——两份表单意味着两套校验会各自漂移。 */
  pick(event) {
    const id = event.currentTarget.dataset.id;
    if (!id) return;
    let channel = null;
    if (typeof this.getOpenerEventChannel === 'function') {
      channel = this.getOpenerEventChannel();
    }
    if (channel && typeof channel.emit === 'function') {
      channel.emit('pickTemplate', id);
    }
    wx.navigateBack({
      /* 直接进入本页（没有上一页）时 navigateBack 会失败，兜底回到场景管理 */
      fail: () => wx.redirectTo({ url: '/pages/admin-scenarios/admin-scenarios' })
    });
  },

  goBack() { wx.navigateBack({ fail: () => wx.redirectTo({ url: '/pages/admin-scenarios/admin-scenarios' }) }); }
});
