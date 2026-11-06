// 生成 tests/golden/unlock-colors.json：用 Electron v6.3.0 map-app.js 里原样的"已解锁"代码（inRing … computeUnlocked）
// 算一遍 testdata/data.js 的上色结果，tst_map 拿原生版的结果和它逐个省比较。
// 用法：node gen.js <Electron 的 map-app.js> <data.js> <adm1.geojson> <输出.json>
const fs = require('fs');
const [mapApp, dataJs, adm1Path, out] = process.argv.slice(2);
const src = fs.readFileSync(mapApp, 'utf8');
// 从 "function inRing" 到 computeUnlocked 结束（"return { type: 'FeatureCollection', features: feats };\n  }"）原样截出来
const a = src.indexOf('  function inRing(x, y, ring)');
const endMark = "    return { type: 'FeatureCollection', features: feats };\n  }";
const b = src.indexOf(endMark, a) + endMark.length;
if (a < 0 || b < endMark.length) throw new Error('map-app.js 里找不到"已解锁"代码');
let code = src.slice(a, b);
// 去掉中间和"已解锁"无关的、引用 DOM 的部分（loadUnlockData 在 inRing 之前，不在这段里）
const DATA = (() => { const t = fs.readFileSync(dataJs, 'utf8'); const i = t.indexOf('='); return JSON.parse(t.slice(i + 1).trim().replace(/;\s*$/, '')); })();
const unlockData = { adm1: JSON.parse(fs.readFileSync(adm1Path, 'utf8')) };
const UNLOCK_HUES = [8, 205, 145, 32, 275, 175, 340, 95, 230, 55, 300, 120];
function validStops(plan) {
  return (plan.stops || []).filter(s => Number.isFinite(+s.lat) && Number.isFinite(+s.lng)).slice().sort((a, b) => (a.order ?? 0) - (b.order ?? 0));
}
let unlockInfo = null;
// eval 里 let 声明的 unlockDots 只在 eval 里可见：顺手导出
eval(code + '\nglobalThis.__run = () => { const fc = computeUnlocked(); return { fc, dots: unlockDots }; };');
const { fc, dots: unlockDots } = globalThis.__run();
const colors = {};
fc.features.forEach(f => { colors[f.properties.id] = f.properties.c; });
const res = { source: 'Electron v6.3.0 map-app.js computeUnlocked', provinces: unlockInfo.provinces, countries: unlockInfo.countries,
  countryOrder: unlockInfo.list, dots: unlockDots.features.length, colors };
fs.writeFileSync(out, JSON.stringify(res, null, 1) + '\n');
console.log('provinces', res.provinces, 'countries', res.countries, 'features', fc.features.length, 'dots', res.dots);
