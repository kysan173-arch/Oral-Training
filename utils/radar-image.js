// 静态雷达图直接生成 SVG，由普通 image 展示；不创建 Canvas 或导出临时 PNG。
const scored = value => value != null && value !== '' && Number.isFinite(Number(value));
const escapeXml = value => String(value).replace(/[&<>"']/g, char => ({
  '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&apos;'
})[char]);
const number = value => Number(value.toFixed(2));

// 用字符引用保留中文，再编码 ASCII SVG；无需文件 I/O、wx API 或浏览器 btoa。
const dataUrl = svg => {
  const ascii = svg.replace(/[^\x00-\x7f]/gu, char => `&#${char.codePointAt(0)};`);
  const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  let encoded = '';
  for (let i = 0; i < ascii.length; i += 3) {
    const bits = (ascii.charCodeAt(i) << 16) | ((ascii.charCodeAt(i + 1) || 0) << 8)
      | (ascii.charCodeAt(i + 2) || 0);
    encoded += alphabet[(bits >>> 18) & 63] + alphabet[(bits >>> 12) & 63]
      + (i + 1 < ascii.length ? alphabet[(bits >>> 6) & 63] : '=')
      + (i + 2 < ascii.length ? alphabet[bits & 63] : '=');
  }
  return `data:image/svg+xml;base64,${encoded}`;
};

const radarImage = (dimensions, compare, width, height) => {
  dimensions = (dimensions || []).filter(item => item && item.name);
  if (dimensions.length < 3) return '';
  width = Number(width) || 300;
  height = Number(height) || 260;
  const count = dimensions.length;
  const centerX = width / 2;
  const centerY = height / 2;
  const radius = Math.max(42, Math.min(width, height) / 2 - 42);
  const point = (index, ratio) => {
    const angle = -Math.PI / 2 + Math.PI * 2 * index / count;
    return `${number(centerX + Math.cos(angle) * radius * ratio)},${number(centerY + Math.sin(angle) * radius * ratio)}`;
  };
  const parts = [`<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}" preserveAspectRatio="none">`];
  for (let level = 1; level <= 4; level += 1) {
    parts.push(`<polygon points="${dimensions.map((_, index) => point(index, level / 4)).join(' ')}" fill="none" stroke="${level === 4 ? '#dfe4ef' : '#edf0f6'}" stroke-width="1"/>`);
  }
  dimensions.forEach((_, index) => {
    parts.push(`<path d="M${centerX},${centerY} L${point(index, 1)}" fill="none" stroke="#edf0f6" stroke-width="1"/>`);
  });
  const drawSeries = (values, color, opacity, dashed) => {
    const complete = values.length === count && values.every(scored);
    let connected = false;
    const path = values.map((value, index) => {
      if (!scored(value)) { connected = false; return ''; }
      const command = connected ? 'L' : 'M';
      connected = true;
      return `${command}${point(index, Math.max(0, Math.min(100, Number(value))) / 100)}`;
    }).join(' ');
    parts.push(`<path d="${path}${complete ? ' Z' : ''}" fill="${complete ? color : 'none'}" fill-opacity="${opacity}" stroke="${color}" stroke-width="${dashed ? 1.5 : 2}"${dashed ? ' stroke-dasharray="4 3"' : ''}/>`);
  };
  if (compare && compare.length === count) drawSeries(compare, '#6B7A93', 0.10, true);
  drawSeries(dimensions.map(item => item.score), '#1F3864', 0.15, false);
  const text = (value, position, color) => {
    const [x, y] = position.split(',');
    // 不用 style 节点或百分比单位，兼容小程序 SVG 图片的限制。
    parts.push(`<text x="${x}" y="${number(Number(y) + 4)}" text-anchor="middle" font-family="sans-serif" font-size="11" fill="${color}">${escapeXml(value)}</text>`);
  };
  dimensions.forEach((item, index) => {
    text(item.name, point(index, 1.23), '#54627a');
    if (!scored(item.score)) {
      text('未评估', point(index, 0.82), '#54627a');
      return;
    }
    const score = Math.max(0, Math.min(100, Number(item.score)));
    const [x, y] = point(index, score / 100).split(',');
    parts.push(`<circle cx="${x}" cy="${y}" r="3.5" fill="#1F3864" stroke="#fff" stroke-width="1"/>`);
    text(Math.round(score), point(index, Math.min(1.08, score / 100 + 0.12)), '#1F3864');
  });
  parts.push('</svg>');
  return dataUrl(parts.join(''));
};

module.exports = { radarImage, scored };
