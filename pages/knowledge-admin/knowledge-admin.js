const api = require('../../utils/api.js');

const JOB_STORAGE_KEY = 'oralTrainingKnowledgeJobIds';
const STATUS_LABELS = {
  pending: '排队中', running: '生成中', retry_wait: '等待重试',
  succeeded: '已完成', dead: '生成失败'
};

Page({
  data: {
    loading: true,
    activeTab: 'services',
    tabs: [
      { id: 'services', name: '服务项目' },
      { id: 'knowledge', name: '专业知识' },
      { id: 'jobs', name: '生成任务' }
    ],
    services: [],
    knowledge: [],
    jobs: []
  },

  onShow() {
    api.ensureAuthenticated().then(() => {
      const user = api.getCurrentUser();
      if (!user || user.role !== 'admin') {
        this.setData({ loading: false });
        wx.showToast({ title: '仅管理员可访问', icon: 'none' });
        return;
      }
      this.loadAll();
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '登录状态获取失败', icon: 'none' });
    });
  },

  selectTab(event) {
    this.setData({ activeTab: event.currentTarget.dataset.id });
  },

  loadAll() {
    this.setData({ loading: true });
    const storedJobIds = wx.getStorageSync(JOB_STORAGE_KEY);
    const jobIds = Array.isArray(storedJobIds) ? storedJobIds : [];
    Promise.all([
      api.getAdminServices(),
      api.getAdminKnowledge(),
      Promise.all(jobIds.slice(0, 20).map(id => api.getKnowledgeGenerationJob(id)
        .catch(() => null)))
    ]).then(([services, knowledge, jobs]) => {
      this.setData({
        loading: false,
        services: (services.items || []).map(item => Object.assign({}, item, {
          statusLabel: item.status === 'archived' ? '已归档' : item.publishedVersion ? '已发布' : '草稿'
        })),
        knowledge: (knowledge.items || []).map(item => Object.assign({}, item, {
          statusLabel: item.status === 'archived' ? '已归档' : item.publishedVersion ? '已发布' : '草稿'
        })),
        jobs: jobs.filter(Boolean).map(item => Object.assign({}, item, {
          statusLabel: STATUS_LABELS[item.status] || item.status,
          resultLabel: item.resultApplied === false ? '候选未覆盖新草稿' :
            item.resultApplied === true ? '已写入草稿' : ''
        }))
      });
    }).catch(error => {
      this.setData({ loading: false });
      wx.showToast({ title: error.message || '管理数据加载失败', icon: 'none' });
    });
  },

  newService() { wx.navigateTo({ url: '/pages/service-editor/service-editor' }); },
  editService(event) {
    wx.navigateTo({ url: `/pages/service-editor/service-editor?id=${encodeURIComponent(event.currentTarget.dataset.id)}` });
  },
  newKnowledge() { wx.navigateTo({ url: '/pages/knowledge-editor/knowledge-editor' }); },
  editKnowledge(event) {
    wx.navigateTo({ url: `/pages/knowledge-editor/knowledge-editor?id=${encodeURIComponent(event.currentTarget.dataset.id)}` });
  },
  retryJob(event) {
    api.retryKnowledgeGenerationJob(event.currentTarget.dataset.id).then(() => {
      wx.showToast({ title: '已重新排队', icon: 'success' });
      this.loadAll();
    }).catch(error => wx.showToast({ title: error.message || '重试失败', icon: 'none' }));
  }
});
