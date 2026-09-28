const api = require('../../utils/api.js');

const JOB_STORAGE_KEY = 'oralTrainingKnowledgeJobIds';
const requestKey = prefix => `${prefix}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
const splitList = value => String(value || '').split(/[，,\n]/)
  .map(item => item.trim()).filter(Boolean);

const emptyForm = () => ({
  topic: '',
  scope: 'general',
  serviceId: '',
  title: '',
  body: '',
  applicability: '仅用于模拟训练',
  aliasesText: '',
  previewQuestion: '',
  generationBrief: ''
});

Page({
  data: {
    entryId: '',
    draftId: '',
    draftVersion: null,
    status: 'active',
    form: emptyForm(),
    loading: false,
    saving: false,
    generating: false,
    job: null,
    revisions: [],
    scopes: ['general', 'service']
  },

  onLoad(options) {
    if (options.id) {
      this.setData({ entryId: decodeURIComponent(options.id) });
      this.loadDraft();
    }
  },

  onShow() {
    if (this.data.job && !['succeeded', 'dead'].includes(this.data.job.status)) {
      this.startPolling(this.data.job.jobId);
    }
  },
  onHide() { this.stopPolling(); },
  onUnload() { this.stopPolling(); },

  loadDraft() {
    if (!this.data.entryId) return Promise.resolve();
    this.setData({ loading: true });
    return Promise.all([
      api.getAdminKnowledgeDraft(this.data.entryId),
      api.getAdminKnowledgeRevisions(this.data.entryId)
    ]).then(([draft, revisions]) => {
      this.setData({
        loading: false,
        draftId: draft.draftId,
        draftVersion: draft.draftVersion,
        status: draft.status,
        form: {
          topic: draft.topic,
          scope: draft.scope,
          serviceId: draft.serviceId || '',
          title: draft.title,
          body: draft.body,
          applicability: draft.metadata.applicability || '',
          aliasesText: (draft.metadata.aliases || []).join('\n'),
          previewQuestion: this.data.form.previewQuestion || '',
          generationBrief: this.data.form.generationBrief || ''
        },
        revisions: revisions.items || []
      });
      if (draft.generationId) {
        this.rememberJob(draft.generationId);
        api.getKnowledgeGenerationJob(draft.generationId).then(job => {
          this.setData({ job, generating: !['succeeded', 'dead'].includes(job.status) });
          if (!['succeeded', 'dead'].includes(job.status)) this.startPolling(job.jobId);
        }).catch(() => {});
      }
    }).catch(error => {
      this.setData({ loading: false });
      api.showCenterNotice({ title: error.message || '知识草稿加载失败' });
    });
  },

  onInput(event) {
    this.setData({ [`form.${event.currentTarget.dataset.field}`]: event.detail.value });
  },

  onScopeChange(event) {
    const scope = this.data.scopes[Number(event.detail.value)];
    this.setData({ 'form.scope': scope, 'form.serviceId': scope === 'general' ? '' : this.data.form.serviceId });
  },

  metadata() {
    return {
      origin: 'synthetic',
      verification: 'unverified',
      sourceTitle: '',
      sourceUrl: null,
      sourceLocator: '',
      applicability: String(this.data.form.applicability || '').trim(),
      trainingScope: 'demo',
      aliases: splitList(this.data.form.aliasesText)
    };
  },

  saveDraft(silent = false) {
    if (this.data.saving) return Promise.reject(new Error('正在保存'));
    this.setData({ saving: true });
    const form = this.data.form;
    const operation = this.data.entryId
      ? api.saveAdminKnowledgeDraft(this.data.entryId, {
        draftVersion: this.data.draftVersion,
        title: String(form.title || '').trim(),
        body: String(form.body || '').trim(),
        metadata: this.metadata()
      })
      : api.createAdminKnowledge({
        topic: String(form.topic || '').trim(),
        scope: form.scope,
        serviceId: form.scope === 'service' ? String(form.serviceId || '').trim() : '',
        title: String(form.title || '').trim(),
        body: String(form.body || '').trim(),
        metadata: this.metadata()
      });
    return operation.then(result => {
      this.setData({
        saving: false,
        entryId: result.id,
        draftId: result.draftId,
        draftVersion: result.draftVersion
      });
      if (!silent) api.showCenterNotice({ title: '知识草稿已保存' });
      return result;
    }).catch(error => {
      this.setData({ saving: false });
      if (error.code === 'DRAFT_VERSION_CONFLICT') {
        wx.showModal({
          title: '草稿已更新',
          content: '当前输入仍保留。确认重新加载服务器版本，取消则可先复制现有内容。',
          confirmText: '重新加载',
          success: result => { if (result.confirm) this.loadDraft(); }
        });
      } else {
        api.showCenterNotice({ title: error.message || '保存失败' });
      }
      throw error;
    });
  },

  saveTapped() { this.saveDraft(false).catch(() => {}); },

  startGeneration() {
    if (this.data.generating) return;
    this.setData({ generating: true });
    this.saveDraft(true).then(() => api.createKnowledgeGenerationJob({
      kind: 'knowledge_draft',
      draftId: this.data.draftId,
      brief: this.data.form.generationBrief || '',
      count: 1,
      idempotencyKey: requestKey('knowledge-generation')
    })).then(job => {
      this.rememberJob(job.jobId);
      this.setData({ job, generating: false });
      this.startPolling(job.jobId);
    }).catch(() => this.setData({ generating: false }));
  },

  rememberJob(jobId) {
    const storedIds = wx.getStorageSync(JOB_STORAGE_KEY);
    const ids = Array.isArray(storedIds) ? storedIds : [];
    wx.setStorageSync(JOB_STORAGE_KEY, [jobId].concat(ids.filter(id => id !== jobId)).slice(0, 20));
  },

  startPolling(jobId) {
    this.stopPolling();
    const poll = () => api.getKnowledgeGenerationJob(jobId).then(job => {
      this.setData({ job, generating: !['succeeded', 'dead'].includes(job.status) });
      if (job.status === 'succeeded') {
        if (job.resultApplied) this.loadDraft();
        wx.showToast({
          title: job.resultApplied ? '候选已写入草稿' : '新编辑已保留，候选未覆盖',
          icon: 'none'
        });
        return;
      }
      if (job.status === 'dead') return;
      this.pollTimer = setTimeout(poll, 1500);
    }).catch(error => {
      this.setData({ generating: false });
      api.showCenterNotice({ title: error.message || '任务状态读取失败' });
    });
    poll();
  },

  stopPolling() {
    if (this.pollTimer) clearTimeout(this.pollTimer);
    this.pollTimer = null;
  },

  publishDraft() {
    wx.showModal({
      title: '发布知识版本',
      content: '该条目仍是 synthetic/unverified 模拟资料。发布后版本不可修改，请确认正文与适用范围已人工复核。',
      success: modal => {
        if (!modal.confirm) return;
        this.saveDraft(true).then(result => api.publishAdminKnowledge(
          this.data.entryId, result.draftVersion, requestKey('knowledge-publish')
        )).then(() => {
          api.showCenterNotice({ title: '已发布新版本' });
          this.loadDraft();
        }).catch(() => {});
      }
    });
  },

  previewDraft() {
    api.previewAdminKnowledge({
      entityType: 'knowledge', entityId: this.data.entryId,
      draftVersion: this.data.draftVersion,
      question: String(this.data.form.previewQuestion || '').trim()
    }).then(result => {
      const evidence = (result.evidence || []).map((item, index) =>
        `${index + 1}. ${item.body}`).join('\n\n');
      wx.showModal({
        title: result.retrievalStatus === 'ok' ? '检索预览' : '未命中',
        content: `${result.answer}${evidence ? `\n\n${evidence}` : ''}`,
        showCancel: false
      });
    }).catch(error => api.showCenterNotice({ title: error.message || '预览失败' }));
  },

  archiveKnowledge() {
    if (!this.data.entryId) return;
    wx.showModal({ title: '归档知识', content: '归档只停止新上下文纳入，不删除历史版本。',
      success: modal => {
        if (!modal.confirm) return;
        api.archiveAdminKnowledge(this.data.entryId).then(() => {
          this.setData({ status: 'archived' });
          api.showCenterNotice({ title: '知识已归档' });
        }).catch(error => api.showCenterNotice({ title: error.message || '归档失败' }));
      }
    });
  }
});
