// 瓦片预取的对照数据（0.8.5）：把 Electron map-app.js 里的 tileXY、prefetchTiles 原样截出来（地图、底图、Image 换成假的），
// 对一组视野算"要预取哪些瓦片"，存成 tests/golden/prefetch.json，tst_map 用 C++ 的 geo::prefetchTiles 逐项对照。
// 用法：node gen.js <Electron map-app.js> <输出 prefetch.json>
const fs = require('fs');
const [, , appJs, out] = process.argv;
const src = fs.readFileSync(appJs, 'utf8');
function grab(name) {
  const i = src.indexOf('function ' + name + '(');
  if (i < 0) throw new Error('找不到 ' + name);
  let depth = 0, j = src.indexOf('{', i);
  for (let k = j; k < src.length; k++) {
    if (src[k] === '{') depth++;
    else if (src[k] === '}') { depth--; if (depth === 0) return src.slice(i, k + 1); }
  }
  throw new Error('括号不配对 ' + name);
}
const code = [grab('tileXY'), grab('prefetchTiles'), 'module.exports = { tileXY, prefetchTiles };'].join('\n');
// 每次一个新的 prefetched 集合、上限放到很大（C++ 那边的 geo::prefetchTiles 不含"只取一次"和 48 张上限，那两条在 TileServer 里另测）
function run(view) {
  const got = [];
  const env = {
    tilesOff: () => false, navigator: { onLine: true }, PREFETCH_MAX: 1e9, prefetched: new Set(),
    baseOf: () => ({ tiles: ['{z}/{x}/{y}'], tileSize: view.tileSize, tileMaxZoom: view.tileMaxZoom }),
    baseName: 'x',
    map: { getZoom: () => view.zoom, getBounds: () => ({ getWest: () => view.w, getNorth: () => view.n, getEast: () => view.e, getSouth: () => view.s }) },
    Image: function () { const self = this; Object.defineProperty(this, 'src', { set(v) { got.push(v); } }); },
  };
  const m = { exports: {} };
  new Function('module', ...Object.keys(env), code)(m, ...Object.values(env));
  m.exports.prefetchTiles();
  return { view, tiles: got, xy: [[view.w, view.n], [view.e, view.s]].map(([lng, lat]) => m.exports.tileXY(lng, lat, 7)) };
}
const views = [
  { w: 100, n: 45, e: 125, s: 25, zoom: 4.4, tileSize: 256, tileMaxZoom: 19 },
  { w: 100, n: 45, e: 125, s: 25, zoom: 4.6, tileSize: 256, tileMaxZoom: 19 },
  { w: 139.5, n: 35.9, e: 140.1, s: 35.5, zoom: 10.2, tileSize: 256, tileMaxZoom: 19 },
  { w: 139.5, n: 35.9, e: 140.1, s: 35.5, zoom: 10.2, tileSize: 128, tileMaxZoom: 18 },
  { w: 170, n: -10, e: 195, s: -25, zoom: 5, tileSize: 256, tileMaxZoom: 19 },       // 跨 180° 经线（东边 > 180）
  { w: -190, n: 20, e: -170, s: 10, zoom: 5.5, tileSize: 256, tileMaxZoom: 19 },     // 西边 < -180
  { w: -20, n: 84, e: 40, s: 60, zoom: 3, tileSize: 256, tileMaxZoom: 19 },          // 高纬度
  { w: -180, n: 85, e: 180, s: -85, zoom: 0.4, tileSize: 256, tileMaxZoom: 19 },     // 整个世界（下一级太大跳过）
  { w: 110, n: 30, e: 112, s: 29, zoom: 15.6, tileSize: 256, tileMaxZoom: 16 },      // 浅灰：+1 级超过上限
  { w: 2.2, n: 48.9, e: 2.5, s: 48.8, zoom: 12.5, tileSize: 128, tileMaxZoom: 18 },  // 正好一半（Math.round）
  { w: 0, n: 1, e: 1, s: 0, zoom: 0.2, tileSize: 256, tileMaxZoom: 19 },             // -1 级 < 1 不要
];
fs.writeFileSync(out, JSON.stringify(views.map(run)));
console.log('写了', views.length, '个视野');
