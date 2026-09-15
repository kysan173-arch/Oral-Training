/* 通用趋势图：柱状 + 折线双轴，canvas 2d 绘制
   数据契约：
     labels: ['09-01', ...]
     series: [{ name, type: 'bar' | 'line', axis: 'left' | 'right', color, dashed, values: [] }]
   两种用途共用本组件：团队训练趋势（柱状次数 + 折线均分）、成员五维成长曲线（多条折线）。 */

const getWindowInfo = () => {
  if (typeof wx.getWindowInfo === 'function') return wx.getWindowInfo() || {};
  return wx.getSystemInfoSync() || {};
};

/* 左轴上限取整到好看的刻度，避免顶部出现 7.5 这类数值 */
const niceCeil = value => {
  const max = Number(value) || 0;
  if (max <= 4) return 4;
  const magnitude = Math.pow(10, Math.floor(Math.log10(max)));
  const normalized = max / magnitude;
  const step = normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 2.5 ? 2.5 : normalized <= 5 ? 5 : 10;
  return step * magnitude;
};

const roundRect = (ctx, x, y, width, height, radius) => {
  const r = Math.min(radius, width / 2, height / 2);
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + width - r, y);
  ctx.quadraticCurveTo(x + width, y, x + width, y + r);
  ctx.lineTo(x + width, y + height);
  ctx.lineTo(x, y + height);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
};

Component({
  properties: {
    labels: { type: Array, value: [], observer() { this.scheduleDraw(); } },
    series: {
      type: Array,
      value: [],
      observer(list) {
        this.setData({
          legend: (list || []).filter(item => item && item.name).map(item => ({
            name: item.name,
            color: item.color || (item.type === 'line' ? '#B97A1E' : '#1F3864'),
            dashed: !!item.dashed
          }))
        });
        this.scheduleDraw();
      }
    },
    height: { type: Number, value: 200 },
    /* 画布左右需要留出的间距折算成的 rpx（页面 padding + 卡片内边距），用于反算可用宽度 */
    paddingRpx: { type: Number, value: 100 },
    /* 右轴固定量程，默认百分制 */
    rightMax: { type: Number, value: 100 },
    /* 左轴量程，0 表示按数据自动取整 */
    maxValue: { type: Number, value: 0 }
  },

  data: {
    canvasWidth: 320,
    legend: [],
    /* 绘制结果的图片路径：非空时由 image 展示，canvas 移出视口 */
    chartImage: ''
  },

  lifetimes: {
    attached() {
      const info = getWindowInfo();
      const windowWidth = info.windowWidth || 375;
      const padding = Math.round(windowWidth * this.properties.paddingRpx / 750);
      this.setData({ canvasWidth: Math.max(200, windowWidth - padding) });
    },
    ready() {
      this.scheduleDraw();
    },
    detached() {
      if (this.drawTimer) clearTimeout(this.drawTimer);
    }
  },

  methods: {
    scheduleDraw() {
      if (this.drawTimer) clearTimeout(this.drawTimer);
      this.drawTimer = setTimeout(() => this.draw(), 20);
    },

    draw() {
      const labels = this.properties.labels || [];
      const series = (this.properties.series || []).filter(item => item && Array.isArray(item.values));
      if (!labels.length || !series.length) return;
      this.createSelectorQuery().select('.trend-canvas').fields({ node: true, size: true })
        .exec(result => {
          if (!result || !result[0] || !result[0].node) return;
          const canvas = result[0].node;
          const ctx = canvas.getContext('2d');
          const width = this.data.canvasWidth;
          const height = this.properties.height;
          const dpr = getWindowInfo().pixelRatio || 1;
          canvas.width = width * dpr;
          canvas.height = height * dpr;
          ctx.scale(dpr, dpr);
          this.render(ctx, width, height, labels, series);
          this.exportImage(canvas, width, height, dpr);
        });
    },

    /* canvas 是原生组件，层级由客户端原生控制，z-index 无效 —— 它会盖住自定义
       tabBar（纯 view/image 实现），表现为图表「压」在底部导航上。因此绘制完成后
       立刻导出为图片，用普通 image 承载展示，canvas 本身移出视口。
       导出失败时不动 chartImage，canvas 留在原地按原样显示（等于旧行为），
       保证任何机型都不会出现「图表空白」。 */
    exportImage(canvas, width, height, dpr) {
      if (typeof wx.canvasToTempFilePath !== 'function') return;
      wx.canvasToTempFilePath({
        canvas,
        x: 0,
        y: 0,
        width,
        height,
        destWidth: Math.round(width * dpr),
        destHeight: Math.round(height * dpr),
        fileType: 'png',
        success: result => {
          const path = result && result.tempFilePath;
          if (path && path !== this.data.chartImage) this.setData({ chartImage: path });
        },
        fail: () => {}
      }, this);
    },

    /* 图片加载失败（路径失效等）时退回 canvas 直接渲染，宁可它压住 tabBar，
       也不能让图表整块空白。 */
    onImageError() {
      if (this.data.chartImage) this.setData({ chartImage: '' });
    },

    render(ctx, width, height, labels, series) {
      const padLeft = 30;
      const padRight = series.some(item => item.axis === 'right') ? 30 : 12;
      const padTop = 16;
      const padBottom = 26;
      const plotWidth = Math.max(10, width - padLeft - padRight);
      const plotHeight = Math.max(10, height - padTop - padBottom);
      const count = labels.length;
      const step = plotWidth / count;
      const centerX = index => padLeft + step * (index + 0.5);
      const baseY = padTop + plotHeight;

      const leftValues = [];
      series.forEach(item => {
        if (item.axis === 'right') return;
        item.values.forEach(value => leftValues.push(Math.max(0, Number(value) || 0)));
      });
      const leftMax = this.properties.maxValue > 0
        ? Number(this.properties.maxValue)
        : niceCeil(Math.max.apply(null, leftValues.concat([0])));
      const rightMax = this.properties.rightMax || 100;

      const mapY = (item, value) => {
        const num = Number(value);
        if (!isFinite(num) || num < 0) return null;
        const max = item.axis === 'right' ? rightMax : leftMax;
        return padTop + plotHeight * (1 - Math.min(1, num / max));
      };

      ctx.clearRect(0, 0, width, height);
      ctx.font = '10px sans-serif';
      ctx.lineWidth = 1;

      /* 横向网格 + 双侧刻度 */
      ctx.textBaseline = 'middle';
      for (let level = 0; level <= 4; level += 1) {
        const y = padTop + plotHeight * level / 4;
        ctx.beginPath();
        ctx.moveTo(padLeft, y);
        ctx.lineTo(padLeft + plotWidth, y);
        ctx.strokeStyle = level === 4 ? '#dfe4ef' : '#edf0f6';
        ctx.stroke();
        ctx.fillStyle = '#9AA6B8';
        ctx.textAlign = 'right';
        ctx.fillText(String(Math.round(leftMax * (4 - level) / 4)), padLeft - 6, y);
        if (padRight > 20) {
          ctx.textAlign = 'left';
          ctx.fillText(String(Math.round(rightMax * (4 - level) / 4)), padLeft + plotWidth + 6, y);
        }
      }

      /* 柱状系列（可多组并排） */
      const barSeries = series.filter(item => item.type !== 'line');
      if (barSeries.length) {
        const slot = Math.max(6, step * 0.62);
        const barWidth = Math.max(4, Math.min(22, slot / barSeries.length - 2));
        barSeries.forEach((item, seriesIndex) => {
          ctx.fillStyle = item.color || '#1F3864';
          item.values.forEach((value, index) => {
            const y = mapY(item, value);
            if (y === null) return;
            const num = Number(value) || 0;
            if (num <= 0) return;
            const groupWidth = slot / barSeries.length;
            const x = centerX(index) - slot / 2 + groupWidth * seriesIndex + (groupWidth - barWidth) / 2;
            roundRect(ctx, x, y, barWidth, baseY - y, Math.min(3, barWidth / 2));
            ctx.fill();
          });
        });
      }

      /* 折线系列：无效点断开，不跨空缺连线 */
      series.filter(item => item.type === 'line').forEach(item => {
        const color = item.color || '#B97A1E';
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.setLineDash(item.dashed ? [4, 3] : []);
        ctx.beginPath();
        let started = false;
        item.values.forEach((value, index) => {
          const y = mapY(item, value);
          if (y === null) {
            started = false;
            return;
          }
          const x = centerX(index);
          if (!started) {
            ctx.moveTo(x, y);
            started = true;
          } else {
            ctx.lineTo(x, y);
          }
        });
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.fillStyle = color;
        item.values.forEach((value, index) => {
          const y = mapY(item, value);
          if (y === null) return;
          ctx.beginPath();
          ctx.arc(centerX(index), y, 2.6, 0, Math.PI * 2);
          ctx.fill();
        });
      });

      /* 横轴日期：点多时抽稀，末位始终保留 */
      ctx.textAlign = 'center';
      ctx.textBaseline = 'top';
      ctx.fillStyle = '#9AA6B8';
      const every = Math.max(1, Math.ceil(count / 6));
      labels.forEach((label, index) => {
        if (index % every !== 0 && index !== count - 1) return;
        ctx.fillText(String(label), centerX(index), baseY + 7);
      });

      this.plot = { padLeft, step, count, baseY };
    },

    /* canvas 无子元素事件：按触摸坐标反算柱/点下标。
       rect 会随页面滚动变化，缓存会失效，因此每次点击实时取一次。
       量取画布容器而不是 canvas：canvas 可能已被移出视口改为图片展示。 */
    onTap(e) {
      if (!this.plot) return;
      const touch = e.touches && e.touches[0];
      if (!touch) return;
      const plot = this.plot;
      this.createSelectorQuery().select('.trend-canvas-wrap').boundingClientRect().exec(result => {
        if (!result || !result[0]) return;
        const rect = result[0];
        const x = touch.x - rect.left;
        const y = touch.y - rect.top;
        if (y < 0 || y > rect.height) return;
        const index = Math.floor((x - plot.padLeft) / plot.step);
        if (index < 0 || index >= plot.count) return;
        this.triggerEvent('tappoint', { index, label: (this.properties.labels || [])[index] });
      });
    }
  }
});
