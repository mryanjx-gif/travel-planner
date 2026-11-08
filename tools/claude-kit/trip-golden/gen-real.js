// 用 Electron 的 trip-pack.js 把真实数据里照片最多的 N 个计划各打成一个 .trip（只读数据文件夹），给 tst_trip 的 realElectron 用。
// 用法：node gen-real.js <Electron 数据文件夹> <输出文件夹> [N=3] [Electron 源码目录]
'use strict';
const path = require('path'), fs = require('fs'), vm = require('vm');
const [dataDir, outDir, nArg, srcArg] = process.argv.slice(2);
const tripPack = require(path.join(srcArg || 'D:/旅行计划App', 'lib', 'trip-pack.js'));
const ctx = { window: {} };
vm.runInNewContext(fs.readFileSync(path.join(dataDir, 'data.js'), 'utf8'), ctx);
const D = ctx.window.TRAVEL_DATA;
const IMG_RE = /photos\/[^)\s"'\\]+/g;
const photosOf = (p, journal) => {
  const set = new Set();
  if (p.coverPhoto) set.add(p.coverPhoto);
  (p.stops || []).forEach(s => (s.photos || []).forEach(ph => { if (ph && ph.path) set.add(ph.path); }));
  [p.guide || '', ...journal.map(j => j.body || '')].forEach(b => (String(b).match(IMG_RE) || []).forEach(x => set.add(x)));
  return [...set].filter(x => /^photos\//.test(x));
};
const ranked = D.plans.map(p => { const j = (D.journal || []).filter(x => x && x.planId === p.id); return { p, j, ph: photosOf(p, j) }; })
  .sort((a, b) => b.ph.length - a.ph.length).slice(0, +(nArg || 3));
for (const { p, j, ph } of ranked) {
  const files = ph.filter(r => tripPack.safeRel(r) && fs.existsSync(path.join(dataDir, r))).map(r => ({ rel: r, data: fs.readFileSync(path.join(dataDir, r)) }));
  const cr = {}; ph.forEach(x => { if ((D.photoCredits || {})[x]) cr[x] = D.photoCredits[x]; });
  const buf = tripPack.pack({ plan: p, journal: j, photoCredits: cr, appVersion: '6.3.0', exportedAt: new Date().toISOString() }, files);
  const out = path.join(outDir, `real-${p.id}.trip`);
  fs.writeFileSync(out, buf);
  console.log(out, `站点 ${(p.stops || []).length}，照片 ${files.length}/${ph.length}，游记 ${j.length}`);
}
