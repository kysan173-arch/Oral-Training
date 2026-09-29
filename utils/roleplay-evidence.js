const api = require('./api.js');

function viewSummaryEvidence(sessionId, citation) {
  if (!sessionId || !citation || !citation.traceId || !citation.evidenceId) return Promise.resolve();
  return api.getRoleplayEvidence(sessionId, citation.traceId).then(result => {
    const evidence = result.evidence || {};
    if (evidence.manifestHash !== citation.manifestHash) throw new Error('依据版本不匹配，请重新加载复盘。');
    const item = (evidence.facts || []).concat(evidence.passages || []).find(entry =>
      entry.evidenceId === citation.evidenceId && entry.revisionId === citation.revisionId);
    if (!item) throw new Error('未找到该复盘引用的依据。');
    wx.showModal({
      title: item.title || '服务资料原文',
      content: item.body || item.displayText || '暂无可展示内容。',
      showCancel: false
    });
  }).catch(error => wx.showToast({ title: error.message || '依据读取失败', icon: 'none' }));
}

module.exports = { viewSummaryEvidence };
