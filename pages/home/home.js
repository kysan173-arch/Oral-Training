const api = require('../../utils/api.js');

const DIFF_MAP = { basic: '基础', advanced: '进阶' };

Page({
  data: {
    overview: null,
    demoUser: '固定演示账号',
    phrasePreview: [],
    scenarioStatus: [],
    searchKeyword: ''
  },

  onShow() {
    this.loadOverview();
  },

  loadOverview() {
    Promise.all([
      api.getScenarios(),
      api.getDashboard()
    ]).then(([scenarioData, dashboardData]) => {
      // 合并 dashboard 的 per-scenario 统计
      const statsMap = {};
      (dashboardData.scenarioStats || []).forEach(s => {
        statsMap[s.scenarioId] = s.trainingCount || 0;
      });

      const scenarioStatus = (scenarioData.items || []).map(s => {
        const completedCount = statsMap[s.id] || 0;
        return {
          scenarioId: s.id,
          name: s.name,
          summary: s.summary,
          difficulty: s.difficulty,
          difficultyText: DIFF_MAP[s.difficulty] || '基础',
          bestScore: s.bestScore || 0,
          completedCount,
          progressText: completedCount > 0
            ? `已完成 ${completedCount} 次 · 最高 ${s.bestScore || 0} 分`
            : '未训练',
          activeSession: s.activeSession
        };
      });

      this.setData({
        overview: {
          totalCount: dashboardData.totalSessions || 0,
          completedCount: dashboardData.completedSessions || 0,
          averageScore: dashboardData.averageScore || 0
        },
        phrasePreview: [
          { scenarioName: '种植牙基础咨询', csReplies: '种植牙一般需要3-6个月的愈合期。如果您时间比较紧，可以帮您安排尽早检查，医生会给出最适合您的时间方案。' },
          { scenarioName: '正畸基础咨询', csReplies: '30多岁完全可以做正畸。成人正畸虽然周期会比青少年稍长，但现在隐形矫正技术很成熟，美观舒适，很多人这个年龄开始矫正。' },
          { scenarioName: '服务态度安抚', csReplies: '非常抱歉给您带来不好的体验。我们非常重视您的反馈，会马上核实情况并做出改进。现在有什么我可以立即帮您处理的吗？' },
          { scenarioName: '价格异议处理', csReplies: '理解您会对比。种植体品牌、医生经验、质保年限、后续维护都是影响价格的因素。您主要在意的是价格还是品质保障呢？' }
        ],
        scenarioStatus
      });
    }).catch(error => wx.showToast({ title: error.message || '数据加载失败', icon: 'none' }));
  },

  // 搜索
  onSearchInput(e) {
    const keyword = e.detail.value;
    this.setData({ searchKeyword: keyword });
  },

  onSearchConfirm() {
    const keyword = this.data.searchKeyword.trim();
    if (keyword) {
      wx.navigateTo({ url: `/pages/phrases/phrases?search=${encodeURIComponent(keyword)}` });
    }
  },

  // 点击话术卡片 → 跳话术锦囊
  goPhrases() {
    wx.navigateTo({ url: '/pages/phrases/phrases' });
  },

  // 场景训练入口
  startScenario(e) {
    const id = e.currentTarget.dataset.id;
    api.createSession(id).then(data => {
      wx.navigateTo({ url: `/pages/training/training?sessionId=${data.session.id}` });
    }).catch(error => wx.showToast({ title: error.message, icon: 'none' }));
  },

  goTraining() { wx.switchTab({ url: '/pages/index/index' }); },
  goHistory() { wx.switchTab({ url: '/pages/report/report' }); },
  goAdmin() { wx.switchTab({ url: '/pages/admin/admin' }); },
  goProfile() { wx.navigateTo({ url: '/pages/profile/profile' }); },
  goMistakes() { wx.navigateTo({ url: '/pages/mistakes/mistakes' }); }
});
