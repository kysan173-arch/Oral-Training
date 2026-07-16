// components/radar-chart/radar-chart.js
// 五维/多维雷达图组件，基于 Canvas 2D 绘制
// Props:
//   data: Array<{name, value, max, color?}>
//   size: Number (rpx, 默认 520)

const DEFAULT_COLORS = ['#52c41a', '#fa8c16', '#1677e8', '#722ed1', '#eb2f96'];

Component({
  properties: {
    data: {
      type: Array,
      value: [],
      observer: '_redraw'
    },
    size: {
      type: Number,
      value: 520
    }
  },

  data: {
    _canvasReady: false
  },

  lifetimes: {
    attached() {
      // Canvas node is available after attached
      this.data._canvasReady = true;
      this._draw();
    },
    ready() {
      this._draw();
    }
  },

  methods: {
    _redraw() {
      if (this.data._canvasReady) {
        this._draw();
      }
    },

    _draw() {
      const query = this.createSelectorQuery();
      query.select('#radarCanvas')
        .fields({ node: true, size: true })
        .exec((res) => {
          if (!res || !res[0]) return;
          const canvas = res[0].node;
          const ctx = canvas.getContext('2d');
          const width = res[0].width;
          const height = res[0].height;
          const dpr = wx.getSystemInfoSync().pixelRatio || 2;
          canvas.width = width * dpr;
          canvas.height = height * dpr;
          ctx.scale(dpr, dpr);

          const rawData = this.data.data || [];
          if (rawData.length < 3) return;

          const points = rawData.map((item, i) => ({
            ...item,
            color: item.color || DEFAULT_COLORS[i % DEFAULT_COLORS.length],
            value: item.value || 0,
            max: item.max || 100
          }));
          const count = points.length;
          const centerX = width / 2;
          const centerY = height / 2;
          const radius = Math.min(width, height) / 2 - 52;
          const angleStep = (Math.PI * 2) / count;

          ctx.clearRect(0, 0, width, height);

          // Grid (5 layers: 20%, 40%, 60%, 80%, 100%)
          for (let level = 1; level <= 5; level++) {
            ctx.beginPath();
            for (let i = 0; i <= count; i++) {
              const angle = i * angleStep - Math.PI / 2;
              const r = (radius * level) / 5;
              const x = centerX + r * Math.cos(angle);
              const y = centerY + r * Math.sin(angle);
              if (i === 0) ctx.moveTo(x, y);
              else ctx.lineTo(x, y);
            }
            ctx.strokeStyle = '#e8edf2';
            ctx.lineWidth = 1;
            ctx.stroke();
          }

          // Axes
          ctx.beginPath();
          for (let i = 0; i < count; i++) {
            const angle = i * angleStep - Math.PI / 2;
            const x = centerX + radius * Math.cos(angle);
            const y = centerY + radius * Math.sin(angle);
            ctx.moveTo(centerX, centerY);
            ctx.lineTo(x, y);
          }
          ctx.strokeStyle = '#d0d7e2';
          ctx.lineWidth = 0.5;
          ctx.stroke();

          // Data area
          ctx.beginPath();
          for (let i = 0; i < count; i++) {
            const angle = i * angleStep - Math.PI / 2;
            const ratio = Math.min(points[i].value / points[i].max, 1);
            const r = radius * ratio;
            const x = centerX + r * Math.cos(angle);
            const y = centerY + r * Math.sin(angle);
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
          }
          ctx.closePath();
          ctx.fillStyle = 'rgba(22, 119, 232, 0.15)';
          ctx.fill();
          ctx.strokeStyle = '#1677e8';
          ctx.lineWidth = 2;
          ctx.stroke();

          // Data points
          for (let i = 0; i < count; i++) {
            const angle = i * angleStep - Math.PI / 2;
            const ratio = Math.min(points[i].value / points[i].max, 1);
            const r = radius * ratio;
            const x = centerX + r * Math.cos(angle);
            const y = centerY + r * Math.sin(angle);
            ctx.beginPath();
            ctx.arc(x, y, 4, 0, Math.PI * 2);
            ctx.fillStyle = '#1677e8';
            ctx.fill();
          }

          // Labels
          ctx.fillStyle = '#666';
          ctx.font = '12px sans-serif';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          for (let i = 0; i < count; i++) {
            const angle = i * angleStep - Math.PI / 2;
            const labelR = radius + 28;
            const x = centerX + labelR * Math.cos(angle);
            const y = centerY + labelR * Math.sin(angle);
            ctx.fillText(points[i].name, x, y);
          }
        });
    }
  }
});
