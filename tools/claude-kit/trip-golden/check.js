// 用 Electron 版的 lib/trip-pack.js unpack() 读原生版导出的 .trip（0.14.1 互通检查）。用法：node check.js 文件.trip [Electron 源码目录]
'use strict';
const path = require('path'), fs = require('fs');
const src = process.argv[3] || 'D:/旅行计划App';
const tripPack = require(path.join(src, 'lib', 'trip-pack.js'));
const { manifest: m, files } = tripPack.unpack(fs.readFileSync(process.argv[2]));
const plans = Array.isArray(m.plans) ? m.plans : [m.plan];
const bad = [];
plans.forEach(p => {
  (p.stops || []).forEach((s, i) => {
    if (s.order !== i) bad.push(`${p.name} 第 ${i} 站 order=${s.order}`);
    if (s.leg) bad.push(`${p.name} 站点还带着 leg 对象`);
    (s.photos || []).forEach(ph => { if (!files.has(ph.path)) bad.push(`缺照片 ${ph.path}`); });
  });
  if (p.cover !== undefined) bad.push(`${p.name} 还带着原生的 cover`);
  if (p.coverPhoto && !files.has(p.coverPhoto)) bad.push(`封面不在包里 ${p.coverPhoto}`);
  if (/\(photo:/.test(p.guide || '')) bad.push(`${p.name} 攻略里还是 photo:id`);
});
(m.journal || []).forEach(j => { if (!j.planId || j.plan !== undefined) bad.push(`游记 ${j.id} 没换成 planId`); if (/\(photo:/.test(j.body || '')) bad.push(`游记 ${j.id} 正文还是 photo:id`); });
console.log(`formatVersion ${m.formatVersion}，${plans.length} 个计划，${files.size} 个照片文件，游记 ${(m.journal || []).length} 篇，署名 ${Object.keys(m.photoCredits || {}).length} 条`);
console.log(bad.length ? '问题：\n' + bad.join('\n') : 'Electron 读得了，结构对');
process.exit(bad.length ? 1 : 0);
