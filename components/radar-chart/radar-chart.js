Component({
  properties: {
    dimensions: {
      type: Array,
      value: [],
      observer() { this.scheduleDraw(); }
    },
    canvasWidth: { type: Number, value: 300 },
    canvasHeight: { type: Number, value: 260 }
  },

  lifetimes: {
    ready() { this.scheduleDraw(); }
  },

  methods: {
    scheduleDraw() {
      if (this.drawTimer) clearTimeout(this.drawTimer);
      this.drawTimer = setTimeout(() => this.draw(), 20);
    },

    draw() {
      const dimensions = (this.properties.dimensions || []).filter(item => item && item.name);
      if (dimensions.length < 3) return;
      const query = this.createSelectorQuery();
      query.select('.radar-canvas').fields({ node: true, size: true }).exec(result => {
        if (!result || !result[0] || !result[0].node) return;
        const canvas = result[0].node;
        const ctx = canvas.getContext('2d');
        const width = this.properties.canvasWidth;
        const height = this.properties.canvasHeight;
        const dpr = wx.getSystemInfoSync().pixelRatio || 1;
        canvas.width = width * dpr;
        canvas.height = height * dpr;
        ctx.scale(dpr, dpr);
        this.render(ctx, width, height, dimensions);
      });
    },

    render(ctx, width, height, dimensions) {
      const count = dimensions.length;
      const centerX = width / 2;
      const centerY = height / 2;
      const radius = Math.max(42, Math.min(width, height) / 2 - 42);
      const angle = index => -Math.PI / 2 + Math.PI * 2 * index / count;
      const point = (index, ratio) => ({
        x: centerX + Math.cos(angle(index)) * radius * ratio,
        y: centerY + Math.sin(angle(index)) * radius * ratio
      });
      ctx.clearRect(0, 0, width, height);
      for (let level = 1; level <= 4; level += 1) {
        ctx.beginPath();
        for (let index = 0; index < count; index += 1) {
          const value = point(index, level / 4);
          if (index === 0) ctx.moveTo(value.x, value.y);
          else ctx.lineTo(value.x, value.y);
        }
        ctx.closePath();
        ctx.strokeStyle = level === 4 ? '#dfe4ef' : '#edf0f6';
        ctx.lineWidth = 1;
        ctx.stroke();
      }
      for (let index = 0; index < count; index += 1) {
        const value = point(index, 1);
        ctx.beginPath();
        ctx.moveTo(centerX, centerY);
        ctx.lineTo(value.x, value.y);
        ctx.strokeStyle = '#edf0f6';
        ctx.stroke();
      }
      ctx.beginPath();
      dimensions.forEach((item, index) => {
        const score = Math.max(0, Math.min(100, Number(item.score) || 0));
        const value = point(index, score / 100);
        if (index === 0) ctx.moveTo(value.x, value.y);
        else ctx.lineTo(value.x, value.y);
      });
      ctx.closePath();
      ctx.fillStyle = 'rgba(102, 126, 234, 0.18)';
      ctx.fill();
      ctx.strokeStyle = '#667eea';
      ctx.lineWidth = 2;
      ctx.stroke();
      ctx.font = '11px sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      dimensions.forEach((item, index) => {
        const score = Math.max(0, Math.min(100, Number(item.score) || 0));
        const dot = point(index, score / 100);
        ctx.beginPath();
        ctx.arc(dot.x, dot.y, 3.5, 0, Math.PI * 2);
        ctx.fillStyle = item.color || '#667eea';
        ctx.fill();
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 1;
        ctx.stroke();
        const label = point(index, 1.23);
        ctx.fillStyle = '#54627a';
        ctx.fillText(item.name, label.x, label.y);
        const scoreLabel = point(index, Math.min(1.08, score / 100 + 0.12));
        ctx.fillStyle = item.color || '#667eea';
        ctx.fillText(String(Math.round(score)), scoreLabel.x, scoreLabel.y);
      });
    }
  }
});
