const api = require('../../utils/api.js');

const DIM_NAMES = { knowledgeAccuracy: '知识准确性', medicalCompliance: '医疗合规', empathy: '同理心', needsDiscovery: '需求挖掘', serviceEtiquette: '服务礼仪' };

Page({
  data: {
    member: null,
    dimensionAverages: {},
    trend: [],
    growthCurve: [],
    warnings: [],
    totalTrainings: 0,
    completedTrainings: 0,
    dimKeys: ['knowledgeAccuracy', 'medicalCompliance', 'empathy', 'needsDiscovery', 'serviceEtiquette'],
    dimNames: DIM_NAMES
  },

  onLoad(options) {
    if (options.id) {
      this.loadProfile(options.id);
    }
  },

  loadProfile(memberId) {
    wx.showLoading({ title: '加载中' });
    api.getMemberProfile(memberId).then(data => {
      wx.hideLoading();
      this.setData({
        member: data.member,
        dimensionAverages: data.dimensionAverages,
        trend: data.trend.slice(-15),
        growthCurve: data.growthCurve,
        warnings: data.warnings,
        totalTrainings: data.totalTrainings,
        completedTrainings: data.completedTrainings
      });
      this.drawRadarChart();
    }).catch(err => {
      wx.hideLoading();
      wx.showToast({ title: err.message, icon: 'none' });
    });
  },

  dimName(key) {
    return DIM_NAMES[key] || key;
  },

  dimColor(key) {
    const colors = { knowledgeAccuracy: '#667eea', medicalCompliance: '#52a67a', empathy: '#f0a34b', needsDiscovery: '#6b9de8', serviceEtiquette: '#e85d75' };
    return colors[key] || '#999';
  },

  drawRadarChart() {
    const dimAverages = this.data.dimensionAverages;
    const dimKeys = this.data.dimKeys;
    if (!dimKeys.every(k => dimAverages[k] !== undefined)) return;

    const query = wx.createSelectorQuery();
    query.select('#radarCanvas').fields({ node: true, size: true }).exec((res) => {
      if (!res[0] || !res[0].node) return;
      const canvas = res[0].node;
      const ctx = canvas.getContext('2d');
      const dpr = wx.getSystemInfoSync().pixelRatio;
      const w = res[0].width;
      const h = w;
      canvas.width = w * dpr;
      canvas.height = h * dpr;
      ctx.scale(dpr, dpr);

      const cx = w / 2, cy = h / 2, r = w * 0.32;
      const n = dimKeys.length;
      const angleStep = (Math.PI * 2) / n;

      // 背景网格
      for (let level = 1; level <= 4; level++) {
        ctx.beginPath();
        for (let i = 0; i < n; i++) {
          const angle = -Math.PI / 2 + i * angleStep;
          const rr = r * level / 4;
          const x = cx + Math.cos(angle) * rr;
          const y = cy + Math.sin(angle) * rr;
          if (i === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.closePath();
        ctx.strokeStyle = level === 4 ? '#e0e0e0' : '#f0f0f4';
        ctx.lineWidth = 1;
        ctx.stroke();
      }

      // 轴线
      for (let i = 0; i < n; i++) {
        const angle = -Math.PI / 2 + i * angleStep;
        ctx.beginPath();
        ctx.moveTo(cx, cy);
        ctx.lineTo(cx + Math.cos(angle) * r, cy + Math.sin(angle) * r);
        ctx.strokeStyle = '#f0f0f4';
        ctx.stroke();
      }

      // 数据区域
      ctx.beginPath();
      for (let i = 0; i < n; i++) {
        const val = (dimAverages[dimKeys[i]] || 0) / 100;
        const angle = -Math.PI / 2 + i * angleStep;
        const x = cx + Math.cos(angle) * r * val;
        const y = cy + Math.sin(angle) * r * val;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.closePath();
      ctx.fillStyle = 'rgba(102,126,234,.15)';
      ctx.fill();
      ctx.strokeStyle = '#667eea';
      ctx.lineWidth = 2;
      ctx.stroke();

      // 标签
      ctx.fillStyle = '#333';
      ctx.font = `${Math.round(w * 0.04)}px sans-serif`;
      ctx.textAlign = 'center';
      for (let i = 0; i < n; i++) {
        const angle = -Math.PI / 2 + i * angleStep;
        const x = cx + Math.cos(angle) * (r + w * 0.1);
        const y = cy + Math.sin(angle) * (r + w * 0.1);
        ctx.fillText(DIM_NAMES[dimKeys[i]], x, y + w * 0.015);
      }
    });
  }
});
