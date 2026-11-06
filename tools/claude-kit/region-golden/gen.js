// 区域建议对照（0.8.9）：把 Electron map-app.js 里的 suggestRegion（连同 CHINA_OUTLINE / inPolygon / inBox）原样截出来跑，
// 生成 tests/golden/regions.json，tst_map 的 planEditRegions 和 C++（src/map/planedit.cpp）逐点对照。
// 用法：node gen.js <Electron map-app.js> <输出 json>。Electron 改了这段时重新生成。
const fs = require('fs');
const [src, out] = process.argv.slice(2);
const js = fs.readFileSync(src, 'utf8');
const a = js.indexOf('  const CHINA_OUTLINE'), b = js.indexOf('  // 一个计划的建议区域');
if (a < 0 || b < 0) throw new Error('找不到 suggestRegion 那一段');
const suggestRegion = new Function(js.slice(a, b) + '\nreturn suggestRegion;')();
// 固定种子的随机数（mulberry32）
let seed = 20261106;
const rnd = () => { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
const pts = [];
for (let i = 0; i < 4000; i++) pts.push([+(rnd() * 180 - 90).toFixed(4), +(rnd() * 400 - 200).toFixed(4)]);
// 几个真实城市和边界附近的点
[[35.0116, 135.7681], [22.3193, 114.1694], [25.033, 121.5654], [41.0082, 28.9784], [40.98, 29.05], [55.75, 37.62], [-33.87, 151.21],
 [21.3, -157.85], [64.13, -21.94], [30.04, 31.24], [29.5, 34.9], [1.35, 103.82], [-8.65, 115.2], [43.24, 76.89], [27.7, 85.3],
 [-34.6, -58.4], [9.0, -79.5], [19.4, -99.1], [61.2, -149.9], [90, 180], [-90, -180], [0, 0]].forEach(p => pts.push(p));
const rows = pts.map(([lat, lng]) => [lat, lng, suggestRegion(lat, lng)]);
fs.writeFileSync(out, JSON.stringify(rows));
console.log('写了 ' + rows.length + ' 个点 → ' + out);
