// 道路路线的对照数据（0.8.4）：把 Electron map-app.js 里的 decodePolyline / encodePolyline / simplifyPts / legKeyOf 原样截出来，
// 用 testdata/travel.json 里的道路折线跑一遍，结果存成 tests/golden/roads.json，tst_map 用 C++ 的实现逐项对照。
// 用法：node gen.js <Electron map-app.js> <testdata/travel.json> <输出 roads.json>
const fs = require('fs');
const [, , appJs, dataJson, out] = process.argv;
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
const D2R = Math.PI / 180;
const keyLine = src.match(/const legKeyOf = [^\n]+/)[0];
const code = [grab('decodePolyline'), grab('encodePolyline'), grab('simplifyPts'), keyLine,
  'module.exports = { decodePolyline, encodePolyline, simplifyPts, legKeyOf };'].join('\n');
const m = { exports: {} };
new Function('module', 'D2R', code)(m, D2R);
const { decodePolyline, encodePolyline, simplifyPts, legKeyOf } = m.exports;
const data = JSON.parse(fs.readFileSync(dataJson, 'utf8'));
const paths = [];
for (const p of data.plans) for (const s of p.stops || []) if (s.leg && s.leg.path) paths.push(s.leg.path);
const cases = paths.slice(0, 40).map(path => {
  const pts = decodePolyline(path);
  // 抽稀容差放大到 0.003°（原来的折线已经按 0.00015 抽稀过，再按它抽结果不变，测不出东西）
  const simp = simplifyPts(pts, 0.003);
  return { path, n: pts.length, first: pts[0], last: pts[pts.length - 1], reencoded: encodePolyline(pts),
           simplified: encodePolyline(simp), simplifiedN: simp.length };
});
const fixed = [0, -0, 1.000005, -1.000005, 31.015625, -31.015625, 0.000005, 0.0000049999, 139.767125, 35.681236, -33.8688, 151.2093,
  0.125, 2.5e-6, 7.5e-6, 179.999995, -179.999995, 45.123455, 45.123465, 1e-7, -1e-7, 12.3456789, 100.000015];
const toFixed = fixed.map(v => ({ v, f5: v.toFixed(5), f6: v.toFixed(6) }));
const keys = [[35.681236, 139.767125, 34.702485, 135.495951], [-33.8688, 151.2093, -37.8136, 144.9631], [0, 0, -0.000001, 180]]
  .map(([a, b, c, d]) => ({ in: [a, b, c, d], drive: legKeyOf('drive', { lat: a, lng: b }, { lat: c, lng: d }) }));
// 编码的边界：负数、正好一半（Math.round 往正无穷）
const encEdge = [[[0.000005, -0.000005], [-0.000015, 0.000025]], [[38.5, -120.2], [40.7, -120.95], [43.252, -126.453]]]
  .map(pts => ({ pts, enc: encodePolyline(pts) }));
fs.writeFileSync(out, JSON.stringify({ cases, toFixed, keys, encEdge }, null, 1) + '\n');
console.log('写好了', out, cases.length, '条折线');
