Component({
  properties: {
    dimensions: {
      type: Array,
      value: [],
      observer: 'draw'
    },
    canvasWidth: {
      type: Number,
      value: 300
    },
    canvasHeight: {
      type: Number,
      value: 260
    },
    maxValue: {
      type: Number,
      value: 100
    },
    gridLevels: {
      type: Number,
      value: 5
    }
  },

  data: {},

  methods: {
    draw() {
      const dimensions = this.properties.dimensions;
      if (!dimensions || dimensions.length === 0) return;

      const query = this.createSelectorQuery();
      query.select('.radar-canvas').fields({ node: true, size: true }).exec(res => {
        if (!res || !res[0] || !res[0].node) {
          // Retry up to 10 times if canvas not ready
          if (!this._drawRetries) this._drawRetries = 0;
          if (this._drawRetries++ < 10) {
            setTimeout(() => this.draw(), 150);
          } else {
            console.error('[radar-chart] Canvas not ready after 10 retries');
            this._drawRetries = 0;
          }
          return;
        }
        this._drawRetries = 0;
        const canvas = res[0].node;
        const ctx = canvas.getContext('2d');

        const dpr = wx.getWindowInfo().pixelRatio;
        const width = this.properties.canvasWidth;
        const height = this.properties.canvasHeight;

        canvas.width = width * dpr;
        canvas.height = height * dpr;
        ctx.scale(dpr, dpr);

        this._render(ctx, width, height, dimensions);
      });
    },

    _render(ctx, w, h, dimensions) {
      const count = dimensions.length;
      if (count < 3) return;

      const cx = w / 2;
      const cy = h / 2;
      const radius = Math.min(cx, cy) - 40;
      const maxValue = this.properties.maxValue || 100;
      const gridLevels = this.properties.gridLevels || 5;

      // Clear
      ctx.clearRect(0, 0, w, h);

      // Draw grid (concentric polygons)
      for (let level = 1; level <= gridLevels; level++) {
        const r = (radius / gridLevels) * level;
        this._drawPolygon(ctx, cx, cy, r, count, '#e8e8ee', 1);
      }

      // Draw axes
      for (let i = 0; i < count; i++) {
        const angle = this._getAngle(i, count);
        const x = cx + radius * Math.cos(angle);
        const y = cy + radius * Math.sin(angle);
        ctx.beginPath();
        ctx.moveTo(cx, cy);
        ctx.lineTo(x, y);
        ctx.strokeStyle = '#e0e0e8';
        ctx.lineWidth = 1;
        ctx.stroke();
      }

      // Draw axis labels
      ctx.font = '12px sans-serif';
      ctx.fillStyle = '#666';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      for (let i = 0; i < count; i++) {
        const angle = this._getAngle(i, count);
        const labelR = radius + 24;
        let lx = cx + labelR * Math.cos(angle);
        let ly = cy + labelR * Math.sin(angle);

        // Fine-tune label position for readability
        if (Math.abs(angle - Math.PI / 2) < 0.1) {
          ly += 4;
        } else if (Math.abs(angle + Math.PI / 2) < 0.1) {
          ly -= 4;
        }

        ctx.fillText(dimensions[i].name, lx, ly);
      }

      // Draw data polygon
      this._drawDataPolygon(ctx, cx, cy, radius, count, dimensions, maxValue);

      // Draw data dots and scores
      for (let i = 0; i < count; i++) {
        const angle = this._getAngle(i, count);
        const score = Math.min(maxValue, Math.max(0, dimensions[i].score || 0));
        const r = (score / maxValue) * radius;
        const x = cx + r * Math.cos(angle);
        const y = cy + r * Math.sin(angle);

        // Dot
        ctx.beginPath();
        ctx.arc(x, y, 4, 0, 2 * Math.PI);
        ctx.fillStyle = dimensions[i].color || '#667eea';
        ctx.fill();
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 1.5;
        ctx.stroke();

        // Score label near dot
        const labelR = r + 14;
        let sx = cx + labelR * Math.cos(angle);
        let sy = cy + labelR * Math.sin(angle);

        ctx.font = 'bold 11px sans-serif';
        ctx.fillStyle = dimensions[i].color || '#667eea';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(score.toString(), sx, sy);
      }
    },

    _getAngle(index, count) {
      // Start from top (-PI/2) and go clockwise
      return -Math.PI / 2 + (2 * Math.PI * index) / count;
    },

    _drawPolygon(ctx, cx, cy, r, sides, color, lineWidth) {
      ctx.beginPath();
      for (let i = 0; i < sides; i++) {
        const angle = this._getAngle(i, sides);
        const x = cx + r * Math.cos(angle);
        const y = cy + r * Math.sin(angle);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.closePath();
      ctx.strokeStyle = color;
      ctx.lineWidth = lineWidth;
      ctx.stroke();
    },

    _drawDataPolygon(ctx, cx, cy, radius, count, dimensions, maxValue) {
      ctx.beginPath();
      for (let i = 0; i < count; i++) {
        const angle = this._getAngle(i, count);
        const score = Math.min(maxValue, Math.max(0, dimensions[i].score || 0));
        const r = (score / maxValue) * radius;
        const x = cx + r * Math.cos(angle);
        const y = cy + r * Math.sin(angle);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.closePath();

      // Fill with gradient
      const gradient = ctx.createLinearGradient(cx - radius, cy - radius, cx + radius, cy + radius);
      gradient.addColorStop(0, 'rgba(102, 126, 234, 0.35)');
      gradient.addColorStop(1, 'rgba(118, 75, 162, 0.25)');
      ctx.fillStyle = gradient;
      ctx.fill();

      // Stroke
      ctx.strokeStyle = 'rgba(102, 126, 234, 0.8)';
      ctx.lineWidth = 2;
      ctx.stroke();
    }
  }
});
