const assert = require('assert');
const { radarImage } = require('../../utils/radar-image.js');
let definition;
global.Component = value => { definition = value; };
require('../../components/radar-chart/radar-chart.js');
// 首次绘制和更新均不允许调用 Canvas、查询节点、文件导出等桥接 API。
global.wx = new Proxy({}, { get() { throw new Error('Radar must not use wx APIs'); } });
const changes = [];
const chart = Object.assign({}, definition.methods, {
  data: { legend: [], chartImage: '', chartError: false },
  properties: {
    dimensions: ['知识准确性', '医疗合规', '同理心', '需求挖掘', '服务礼仪'].map(name => ({ name, score: 80 })),
    compare: [70, 70, 70, 70, 70], primaryLabel: '成员', compareLabel: '团队平均',
    canvasWidth: 280, canvasHeight: 240
  },
  setData(patch) { changes.push(patch); Object.assign(this.data, patch); }
});
const decode = source => Buffer.from(source.split(',')[1], 'base64').toString('utf8')
  .replace(/&#(\d+);/g, (_, value) => String.fromCodePoint(Number(value)));
definition.lifetimes.attached.call(chart);
const svg = decode(chart.data.chartImage);
assert(svg.includes('知识准确性'));
assert(svg.includes('stroke-dasharray="4 3"'));
assert.strictEqual((svg.match(/<circle /g) || []).length, 5);
assert.strictEqual(chart.data.legend.length, 2);
assert.strictEqual(changes.length, 1, 'initial image and legend use a single setData');
chart.updateImage();
assert.strictEqual(changes.length, 1, 'identical data must not retransmit image');
chart.properties.dimensions[0].score = null;
chart.properties.dimensions[1].score = 0;
chart.updateImage();
assert(decode(chart.data.chartImage).includes('未评估'));
assert(decode(chart.data.chartImage).includes('>0</text>'));
assert.strictEqual((decode(chart.data.chartImage).match(/<circle /g) || []).length, 4);
chart.onImageError();
assert.strictEqual(chart.data.chartImage, '');
assert.strictEqual(chart.data.chartError, true);
chart.retryDraw();
assert.strictEqual(chart.data.chartError, false);
assert(chart.data.chartImage.startsWith('data:image/svg+xml;base64,'));
chart.properties.compare = [];
chart.updateImage();
assert.strictEqual(chart.data.legend.length, 0);
const maliciousName = '<script>&"😀';
const escaped = decode(radarImage([{ name: maliciousName, score: 100 },
  { name: '低分', score: -10 }, { name: '超量程', score: 120 }], [], 280, 240));
assert(escaped.includes('&lt;script&gt;&amp;&quot;😀'));
assert(!escaped.includes('<script>'));
assert(!escaped.includes('NaN'));
assert.strictEqual(radarImage([], [], 280, 240), '');
definition.lifetimes.detached.call(chart);
const count = changes.length;
Object.values(definition.observers)[0].call(chart);
assert.strictEqual(changes.length, count);
console.log('radar image tests passed: no Canvas/export, missing scores, zero scores, escaping, updates');
