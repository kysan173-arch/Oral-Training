const api = require('../../utils/api.js');

const DIM_KEYS = ['knowledgeAccuracy', 'medicalCompliance', 'empathy', 'needsDiscovery', 'serviceEtiquette'];
const DIM_NAMES = { knowledgeAccuracy: '知识准确性', medicalCompliance: '医疗合规', empathy: '同理心', needsDiscovery: '需求挖掘', serviceEtiquette: '服务礼仪' };
const DIM_COLORS = { knowledgeAccuracy: '#667eea', medicalCompliance: '#52a67a', empathy: '#f0a34b', needsDiscovery: '#6b9de8', serviceEtiquette: '#e85d75' };

const TAG_EMOJI = { '共情达人': '🤝', '需求洞察者': '🔍', '知识专家': '📚', '合规标兵': '🛡️', '服务之星': '⭐', '潜力新星': '🌟' };
const LEVEL_EN = { '专家': 'expert', '优秀': 'great', '良好': 'good', '新手': 'beginner' };

Page({
  data: {
    loading: true,
    loadError: false,
    trend: [],
    dimensionAverages: {},
    weaknessDiagnosis: [],
    tags: [],
    overall: null,
    levelEn: 'beginner',
    dimNames: DIM_NAMES,
    dimColors: DIM_COLORS,
    dimLabel: ['知识准确性', '医疗合规', '同理心', '需求挖掘', '服务礼仪'],
    showChart: false
  },

  onLoad() {
    this.loadProfile();
  },

  onShow() {
    if (this.data.loadError) this.loadProfile();
  },

  loadProfile() {
    this.setData({ loading: true, loadError: false });
    api.getProfile().then(data => {
      this.setData({
        loading: false,
        trend: data.trend || [],
        dimensionAverages: data.dimensionAverages || {},
        weaknessDiagnosis: data.weaknessDiagnosis || [],
        tags: (data.tags || []).map(t => Object.assign(t, { emoji: TAG_EMOJI[t.name] || '⭐' })),
        overall: data.overall || { averageScore: 0, level: '新手', totalCompleted: 0, totalMistakes: 0 },
        levelEn: LEVEL_EN[(data.overall && data.overall.level) || '新手'] || 'beginner',
        showChart: (data.trend || []).length >= 2
      });
      if ((data.trend || []).length >= 2) {
        setTimeout(() => this.drawChart(), 300);
      }
    }).catch(error => {
      this.setData({ loading: false, loadError: true });
      wx.showToast({ title: error.message || '数据加载失败', icon: 'none' });
    });
  },

  drawChart() {
    const query = this.createSelectorQuery();
    query.select('#growth-canvas').fields({ node: true, size: true }).exec(res => {
      if (!res || !res[0] || !res[0].node) {
        setTimeout(() => this.drawChart(), 200);
        return;
      }
      const canvas = res[0].node;
      const ctx = canvas.getContext('2d');
      const dpr = wx.getWindowInfo().pixelRatio;
      const w = 340;
      const h = 280;

      canvas.width = w * dpr;
      canvas.height = h * dpr;
      ctx.scale(dpr, dpr);

      const trend = this.data.trend;
      const padding = { top: 30, right: 20, bottom: 50, left: 40 };
      const chartW = w - padding.left - padding.right;
      const chartH = h - padding.top - padding.bottom;

      ctx.clearRect(0, 0, w, h);

      // 网格
      ctx.strokeStyle = '#f0f0f3';
      ctx.lineWidth = 1;
      for (let i = 0; i <= 4; i++) {
        const y = padding.top + (chartH / 4) * i;
        ctx.beginPath();
        ctx.moveTo(padding.left, y);
        ctx.lineTo(w - padding.right, y);
        ctx.stroke();
        ctx.fillStyle = '#bbb';
        ctx.font = '10px sans-serif';
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        ctx.fillText(`${100 - i * 25}`, padding.left - 6, y);
      }

      // X轴标签
      const maxLabels = 7;
      const step = Math.max(1, Math.ceil(trend.length / maxLabels));
      ctx.fillStyle = '#999';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'top';
      for (let i = 0; i < trend.length; i += step) {
        const x = padding.left + (i / Math.max(trend.length - 1, 1)) * chartW;
        ctx.fillText(trend[i].date.slice(5), x, h - padding.bottom + 8);
      }

      // 画各维度曲线
      DIM_KEYS.forEach(key => {
        const color = DIM_COLORS[key];
        ctx.beginPath();
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.setLineDash([]);

        let firstPoint = true;
        trend.forEach((point, i) => {
          const x = padding.left + (i / Math.max(trend.length - 1, 1)) * chartW;
          const score = point.scores[key] || 0;
          const y = padding.top + chartH - (score / 100) * chartH;
          if (firstPoint) { ctx.moveTo(x, y); firstPoint = false; }
          else ctx.lineTo(x, y);
        });
        ctx.stroke();
      });

      // 图例
      const legendY = h - padding.bottom + 28;
      DIM_KEYS.forEach((key, i) => {
        const lx = padding.left + i * 68;
        ctx.fillStyle = DIM_COLORS[key];
        ctx.font = '10px sans-serif';
        ctx.textAlign = 'left';
        ctx.fillText('● ' + DIM_NAMES[key].slice(0, 2), lx, legendY);
      });
    });
  },

  goMistakes() {
    wx.navigateTo({ url: '/pages/mistakes/mistakes' });
  },

  goHistory() {
    wx.switchTab({ url: '/pages/report/report' });
  }
});
