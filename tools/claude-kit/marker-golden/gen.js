// 生成 tests/golden/marker-clusters.json：用 Electron v6.3.0 map-app.js 里原样的合并代码
// （CLUSTER_LAYERS … isLightColor、clusterTier、centerOf、applyClusters、stopStyle）算 testdata/data.js 在 0–6 级的合并结果，
// tst_map 拿原生版的结果逐组比较（成员、平均色、字色、圆圈中心）。
// 用法：node gen.js <Electron 的 map-app.js> <data.js> <输出.json>
const fs = require('fs');
const [mapApp, dataJs, out] = process.argv.slice(2);
const src = fs.readFileSync(mapApp, 'utf8');
function slice(startMark, endMark) {
  const a = src.indexOf(startMark);
  const b = src.indexOf(endMark, a);
  if (a < 0 || b < 0) throw new Error('map-app.js 里找不到：' + startMark);
  return src.slice(a, b);
}
// 一个函数：从 "  function name(" 到下一个两格缩进的 "\n  }\n"
function fn(name) {
  const a = src.indexOf('  function ' + name + '(');
  const b = src.indexOf('\n  }\n', a);
  if (a < 0 || b < 0) throw new Error('map-app.js 里找不到函数 ' + name);
  return src.slice(a, b + 4);
}
const DATA = (() => { const t = fs.readFileSync(dataJs, 'utf8'); const i = t.indexOf('='); return JSON.parse(t.slice(i + 1).trim().replace(/;\s*$/, '')); })();
const code = [
  slice('  const D2R = Math.PI / 180', '  // 返回 a→b 的大圆弧采样点'),       // D2R、toVec、toLatLng
  fn('normLng'), fn('validStops'),
  slice('  const CLUSTER_LAYERS = [', '  const cluster = {'),                  // 层、distKm、颜色
  fn('clusterTier'), fn('centerOf'), fn('applyClusters'),
  fn('planColor'), fn('stopColor'), fn('statusOf'), fn('stopStyle')
].join('\n');
// Electron 里的 STATUS（只用到默认色）
const STATUS = { planning: { color: '#e8590c' }, completed: { color: '#2b8a3e' }, wish: { color: '#7b61b5' } };
let zoom = 0;
const map = { getZoom: () => zoom };
const state = { stopMarkers: [] };
const cluster = { tier: undefined, markers: [] };   // Electron 原样：当前这一层的合并圆圈
const recorded = [];
function makeClusterMarker(list) {
  const bg = averageColor(list.map(m => m.color));
  recorded.push({ members: list.map(m => [m.planId, m.stopIdx]), color: bg, light: isLightColor(bg), center: centerOf(list.map(m => m.base)) });
  return { remove() {} };
}
eval(code + `
globalThis.__run = () => {
  // 和 renderMap 一样的顺序和 base；没有选中的计划，全部参与合并
  DATA.plans.forEach(plan => {
    const stops = validStops(plan);
    if (!stops.length) return;
    const n = stops.length;
    stops.forEach((s, i) => {
      const st = stopStyle(plan, s, i, n);
      state.stopMarkers.push({ mk: { remove() {}, addTo() {} }, el: { classList: { toggle() {} } }, base: { lat: +s.lat, lng: normLng(+s.lng) },
        planId: plan.id, stopIdx: i, color: stopColor(plan, s), clusterable: true, style: st });
    });
  });
  const tiers = [];
  for (let z = 0; z <= 6; z++) {
    zoom = z + 0.5; recorded.length = 0;
    applyClusters(true);
    tiers.push({ zoom: zoom, tier: clusterTier(), clusters: recorded.slice() });
  }
  const styles = state.stopMarkers.map(m => [m.planId, m.stopIdx, m.style.shape, m.style.text, !!m.style.dot, m.style.icon, m.style.color]);
  // 颜色函数的几个固定样例
  const samples = [['#e8590c'], ['#2b8a3e', '#7b61b5'], ['#fff', '#000'], ['#2a78d6', '#2a78d6', '#e8590c'], ['bad', '#123'], []];
  const colorSamples = samples.map(s => ({ in: s, avg: averageColor(s), light: isLightColor(averageColor(s)) }));
  return { tiers, styles, colorSamples };
};`);
const r = globalThis.__run();
fs.writeFileSync(out, JSON.stringify({ source: 'Electron v6.3.0 map-app.js applyClusters / stopStyle', ...r }, null, 1) + '\n');
console.log(r.tiers.map(t => `z${t.zoom}: ${t.clusters.length} 个圆圈`).join(', '), '; 标记', r.styles.length);
