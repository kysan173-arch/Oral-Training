const { radarImage, scored } = require('../../utils/radar-image.js');

Component({
  properties: {
    dimensions: { type: Array, value: [] },
    compare: { type: Array, value: [] },
    primaryLabel: { type: String, value: '本人' },
    compareLabel: { type: String, value: '团队平均' },
    canvasWidth: { type: Number, value: 300 },
    canvasHeight: { type: Number, value: 260 }
  },

  data: { legend: [], chartImage: '', chartError: false },

  observers: {
    'dimensions, compare, primaryLabel, compareLabel, canvasWidth, canvasHeight'() {
      if (this._attached) this.updateImage();
    }
  },

  lifetimes: {
    attached() {
      this._attached = true;
      this.updateImage();
    },
    detached() { this._attached = false; }
  },

  methods: {
    updateImage() {
      const props = this.properties;
      const chartImage = radarImage(props.dimensions, props.compare, props.canvasWidth, props.canvasHeight);
      const legend = (props.compare || []).some(scored) ? [
        { key: 'primary', text: props.primaryLabel, color: '#1F3864' },
        { key: 'compare', text: props.compareLabel, color: '#6B7A93', dashed: true }
      ] : [];
      const patch = {};
      if (chartImage !== this.data.chartImage) Object.assign(patch, { chartImage, chartError: false });
      if (JSON.stringify(legend) !== JSON.stringify(this.data.legend)) patch.legend = legend;
      if (Object.keys(patch).length) this.setData(patch);
    },

    onImageError() {
      this.setData({ chartImage: '', chartError: true });
    },

    retryDraw() { this.updateImage(); }
  }
});
