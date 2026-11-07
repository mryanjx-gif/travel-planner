// 生成斋月日期表 src/map/ramadan_table.inc（0.11.3）。算法照 Electron pages-calendar.js ramadanOf：
// Intl 的 en-u-ca-islamic-umalqura，伊斯兰历 9 月的连续日子算一段；这里把 2024–2075 年碰到的每一段都列出来（一年可能两段，跨公历年的连成一段）。
// 用法（项目根目录）：node tools/gen-ramadan.js
'use strict';
const fs = require('fs'), path = require('path');
const FROM = 2024, TO = 2075;
const f = new Intl.DateTimeFormat('en-u-ca-islamic-umalqura', { month: 'numeric' });
const pad = n => String(n).padStart(2, '0');
const ymd = d => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
const isRamadan = d => +f.formatToParts(d).find(x => x.type === 'month').value === 9;
const spans = [];
let a = null, b = null;
// 从前一年 11 月开始，到后一年 3 月结束：这样两头跨公历年的段是完整的
for (let d = new Date(FROM - 1, 10, 1, 12); d < new Date(TO + 1, 2, 1, 12); d.setDate(d.getDate() + 1)) {
  if (isRamadan(d)) { if (!a) a = ymd(d); b = ymd(d); }
  else if (a) { spans.push([a, b]); a = null; }
}
const inRange = spans.filter(([x, y]) => +y.slice(0, 4) >= FROM && +x.slice(0, 4) <= TO);
const out = `// 由 tools/gen-ramadan.js 生成，不要手改（乌姆库拉历伊斯兰历 9 月，${FROM}–${TO} 年，共 ${inRange.length} 段）\n` +
  inRange.map(([x, y]) => `{ "${x}", "${y}" },`).join('\n') + '\n';
fs.writeFileSync(path.join(__dirname, '..', 'src', 'map', 'ramadan_table.inc'), out);
console.log(`写了 ${inRange.length} 段`);
