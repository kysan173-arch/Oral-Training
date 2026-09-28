const api = require('../../utils/api.js');

const JOB_STORAGE_KEY = 'oralTrainingKnowledgeJobIds';
const splitList = value => String(value || '').split(/[，,\n]/)
  .map(item => item.trim()).filter(Boolean);
const requestKey = prefix => `${prefix}-${Date.now()}-${Math.random().toString(16).slice(2)}`;

const emptyService = () => ({
  name: '',
  category: 'general',
  dataOrigin: 'synthetic',
  price: {
    status: 'unknown', reason: '尚未录入', type: 'starting_from', currency: 'CNY',
    amountMinor: '', minimumMinor: '', maximumMinor: '', unit: 'per_case', conditions: ''
  },
  visitDuration: { status: 'unknown', reason: '尚未录入', minimum: '', maximum: '', unit: 'minute', estimated: true },
  treatmentDuration: { status: 'unknown', reason: '尚未录入', minimum: '', maximum: '', unit: 'day', estimated: true },
  followupInterval: { status: 'unknown', reason: '尚未录入', minimum: '', maximum: '', unit: 'day', estimated: true },
  appointment: {
    status: 'unknown', reason: '尚未录入', type: 'consultation_hours',
    timezone: 'Asia/Shanghai', text: '', isLiveAvailability: false
  },
  includedItems: [],
  excludedItems: [],
  professionalTopics: [],
  scenarioIds: [],
  adminNotes: ''
});

const durationPayload = value => value.status === 'unknown'
  ? { status: 'unknown', reason: value.reason || '尚未录入' }
  : {
    status: 'known', minimum: Number(value.minimum), maximum: Number(value.maximum),
    unit: value.unit, estimated: value.estimated !== false,
    phase: value.phase || '', conditions: value.conditions || ''
  };

Page({
  data: {
    serviceId: '',
    draftId: '',
    draftVersion: null,
    status: 'active',
    form: emptyService(),
    listText: { includedItems: '', excludedItems: '', professionalTopics: '', scenarioIds: '' },
    loading: false,
    saving: false,
    generating: false,
    job: null,
    revisions: [],
    priceTypes: ['fixed', 'starting_from', 'range', 'quote_after_assessment'],
    priceUnits: ['per_tooth', 'per_case', 'per_visit', 'per_arch', 'per_item'],
    durationUnits: ['minute', 'hour', 'day', 'week', 'month', 'year'],
    appointmentTypes: ['consultation_hours', 'appointment_slots'],
    durationFields: [
      { key: 'visitDuration', name: '单次到诊时长' },
      { key: 'treatmentDuration', name: '全程治疗周期' },
      { key: 'followupInterval', name: '复诊间隔' }
    ]
  },

  onLoad(options) {
    if (options.id) {
      this.setData({ serviceId: decodeURIComponent(options.id) });
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
    if (!this.data.serviceId) return Promise.resolve();
    this.setData({ loading: true });
    return Promise.all([
      api.getAdminServiceDraft(this.data.serviceId),
      api.getAdminServiceRevisions(this.data.serviceId)
    ]).then(([draft, revisions]) => {
      const payload = Object.assign(emptyService(), draft.payload || {});
      payload.price = Object.assign(emptyService().price, payload.price || {});
      ['visitDuration', 'treatmentDuration', 'followupInterval'].forEach(key => {
        payload[key] = Object.assign(emptyService()[key], payload[key] || {});
      });
      payload.appointment = Object.assign(emptyService().appointment, payload.appointment || {});
      this.setData({
        loading: false,
        draftId: draft.draftId,
        draftVersion: draft.draftVersion,
        status: draft.status,
        form: payload,
        listText: {
          includedItems: (payload.includedItems || []).join('\n'),
          excludedItems: (payload.excludedItems || []).join('\n'),
          professionalTopics: (payload.professionalTopics || []).join('\n'),
          scenarioIds: (payload.scenarioIds || []).join('\n')
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
      api.showCenterNotice({ title: error.message || '服务草稿加载失败' });
    });
  },

  onInput(event) {
    this.setData({ [`form.${event.currentTarget.dataset.path}`]: event.detail.value });
  },

  onListInput(event) {
    this.setData({ [`listText.${event.currentTarget.dataset.key}`]: event.detail.value });
  },

  onSelect(event) {
    const options = this.data[event.currentTarget.dataset.options];
    this.setData({ [`form.${event.currentTarget.dataset.path}`]: options[Number(event.detail.value)] });
  },

  toggleStatus(event) {
    const path = event.currentTarget.dataset.path;
    const current = path.split('.').reduce((value, key) => value[key], this.data.form);
    this.setData({ [`form.${path}.status`]: current.status === 'known' ? 'unknown' : 'known' });
  },

  onEstimatedChange(event) {
    this.setData({ [`form.${event.currentTarget.dataset.path}.estimated`]: event.detail.value });
  },

  buildPayload() {
    const form = this.data.form;
    const price = form.price.status === 'unknown'
      ? { status: 'unknown', reason: form.price.reason || '尚未录入' }
      : {
        status: 'known', type: form.price.type, currency: 'CNY', unit: form.price.unit,
        conditions: form.price.conditions || '', validFrom: form.price.validFrom || '',
        validUntil: form.price.validUntil || ''
      };
    if (form.price.status === 'known') {
      if (form.price.type === 'fixed' || form.price.type === 'starting_from') {
        price.amountMinor = Number(form.price.amountMinor);
      } else if (form.price.type === 'range') {
        price.minimumMinor = Number(form.price.minimumMinor);
        price.maximumMinor = Number(form.price.maximumMinor);
      }
    }
    const appointment = form.appointment.status === 'unknown'
      ? { status: 'unknown', reason: form.appointment.reason || '尚未录入' }
      : {
        status: 'known', type: form.appointment.type, timezone: form.appointment.timezone,
        text: form.appointment.text, isLiveAvailability: false,
        updatedAt: form.appointment.updatedAt || ''
      };
    return {
      name: String(form.name || '').trim(),
      category: String(form.category || '').trim(),
      dataOrigin: 'synthetic',
      price,
      includedItems: splitList(this.data.listText.includedItems),
      excludedItems: splitList(this.data.listText.excludedItems),
      visitDuration: durationPayload(form.visitDuration),
      treatmentDuration: durationPayload(form.treatmentDuration),
      followupInterval: durationPayload(form.followupInterval),
      appointment,
      professionalTopics: splitList(this.data.listText.professionalTopics),
      scenarioIds: splitList(this.data.listText.scenarioIds),
      adminNotes: form.adminNotes || ''
    };
  },

  saveDraft(silent = false) {
    if (this.data.saving) return Promise.reject(new Error('正在保存'));
    const payload = this.buildPayload();
    this.setData({ saving: true });
    const operation = this.data.serviceId
      ? api.saveAdminServiceDraft(this.data.serviceId, this.data.draftVersion, payload)
      : api.createAdminService(payload);
    return operation.then(result => {
      this.setData({
        saving: false,
        serviceId: result.id,
        draftId: result.draftId,
        draftVersion: result.draftVersion,
        form: Object.assign({}, this.data.form, result.payload || payload)
      });
      if (!silent) api.showCenterNotice({ title: '草稿已保存' });
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
      kind: 'service_draft',
      draftId: this.data.draftId,
      brief: this.data.form.generationBrief || '',
      count: 1,
      idempotencyKey: requestKey('service-generation')
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
      title: '发布服务版本',
      content: '请确认模拟标识、金额单位、适用条件和有效期均已人工复核。发布后版本不可修改。',
      success: modal => {
        if (!modal.confirm) return;
        this.saveDraft(true).then(result => api.publishAdminService(
          this.data.serviceId, result.draftVersion, requestKey('service-publish')
        )).then(() => {
          api.showCenterNotice({ title: '已发布新版本' });
          this.loadDraft();
        }).catch(() => {});
      }
    });
  },

  previewDraft() {
    api.previewAdminKnowledge({
      entityType: 'service', entityId: this.data.serviceId,
      draftVersion: this.data.draftVersion
    }).catch(error => api.showCenterNotice({ title: error.message || '预览暂不可用' }));
  },

  archiveService() {
    if (!this.data.serviceId) return;
    wx.showModal({ title: '归档服务', content: '归档只停止新训练选择，不删除历史版本。',
      success: modal => {
        if (!modal.confirm) return;
        api.archiveAdminService(this.data.serviceId).then(() => {
          this.setData({ status: 'archived' });
          api.showCenterNotice({ title: '服务已归档' });
        }).catch(error => api.showCenterNotice({ title: error.message || '归档失败' }));
      }
    });
  }
});
