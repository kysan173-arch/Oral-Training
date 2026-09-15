const api = require('../../utils/api.js');
const datetime = require('../../utils/datetime.js');
const emotion = require('../../utils/emotion.js');

/* 分数分档：颜色只跟随分数 */
const scoreTier = score => (score >= 80 ? 'high' : score >= 60 ? 'mid' : 'low');

const DIMENSIONS = [
  { key: 'empathy', name: '情绪识别与同理心' },
  { key: 'knowledgeAccuracy', name: '口腔知识准确性' },
  { key: 'needsDiscovery', name: '需求挖掘' },
  { key: 'serviceEtiquette', name: '服务礼仪' },
  { key: 'medicalCompliance', name: '医疗合规' }
];

Page({
  data: {
    loading: true,
    loadError: '',
    session: null,
    statusText: '',
    totalScore: null,
    dimensions: [],
    reportSummary: '',
    strengths: [],
    improvements: [],
    suspicion: null,
    suspicionText: '',
    messages: []
  },

  memberId: '',
  sessionId: '',

  onLoad(options) {
    this.memberId = options.memberId || '';
    this.sessionId = options.sessionId || '';
    this.loadDetail();
  },

  loadDetail() {
    if (!this.memberId || !this.sessionId) {
      this.setData({ loading: false, loadError: '缺少成员或会话标识' });
      return;
    }
    this.setData({ loading: true, loadError: '' });
    api.getSupervisorMemberSession(this.memberId, this.sessionId).then(data => {
      const session = data.session || {};
      const statusText = session.status === 'in_progress' ? '进行中'
        : session.status === 'completed' ? '已完成' : '已放弃';
      const report = data.report || {};
      const dimensionScores = report.dimensionScores || {};
      const dimensions = DIMENSIONS.map(item => Object.assign({}, item, {
        score: dimensionScores[item.key] || 0,
        tier: scoreTier(dimensionScores[item.key] || 0)
      }));
      const suspicion = data.suspicion || { suspected: false, flags: [] };
      const messages = (data.messages || []).map(message => {
        const emotionText = emotion.emotionTextOf(message);
        return Object.assign({}, message, {
          time: datetime.formatClock(message.createdAt),
          emotionText,
          emotionTone: emotion.emotionToneOf(emotionText)
        });
      });
      this.setData({
        loading: false,
        session: Object.assign({}, session, {
          startedAtText: datetime.formatFull(session.startedAt)
        }),
        statusText,
        totalScore: session.totalScore === undefined ? null : session.totalScore,
        dimensions,
        reportSummary: report.summary || '',
        strengths: (report.strengths || []).slice(0, 3)
          .map(item => item.content || item.evidence || item),
        improvements: (report.improvements || []).slice(0, 3)
          .map(item => item.content || item),
        suspicion,
        suspicionText: suspicion.suspected && suspicion.flags.length
          ? suspicion.flags.join('；') : '',
        messages
      });
    }).catch(error => {
      this.setData({ loading: false, loadError: error.message || '抽查内容加载失败' });
    });
  }
});
