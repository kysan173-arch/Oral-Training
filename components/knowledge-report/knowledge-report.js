const api = require('../../utils/api.js');
const verdicts = { supported: '有依据', contradicted: '与依据矛盾', incomplete: '表达不完整', evidence_missing: '依据不足', conflicted: '资料冲突', not_applicable: '不计分' };
Component({
  properties: { report: Object, sessionId: String },
  data: { checks: [], coverageText: '暂无可核验项' },
  observers: {
    report(report) {
      const assessment = report && report.knowledgeAssessment;
      this.setData({
        coverageText: assessment && typeof assessment.coverage === 'number' ? `${Math.round(assessment.coverage * 100)}%` : '暂无可核验项',
        checks: ((report && report.knowledgeChecks) || []).map(check => Object.assign({}, check, {
          verdictText: verdicts[check.verdict] || '待核实'
        }))
      });
    }
  },
  methods: {
    viewEvidence(event) {
      const traceId = event.currentTarget.dataset.trace;
      if (!traceId || !this.properties.sessionId) return;
      api.getTrainingEvidence(this.properties.sessionId, traceId).then(result => {
        wx.showModal({ title: '本次报告的锁定依据', content: (result.citations || []).map(item => item.text).join('\n'), showCancel: false });
      }).catch(error => wx.showToast({ title: error.message || '依据读取失败', icon: 'none' }));
    }
  }
});
