// .ics 的基准（0.14.0）：用 Electron travelkit.js 原样的 icsEsc / fold / ics 算一个样例计划，写成 tests/golden/ics.json（输入 + 输出）。
// 用法：node gen.js <Electron 的 travelkit.js> <输出 ics.json>。不要手改结果；Electron 改了这几段时重新生成。
// DTSTAMP 固定成 2026-11-08T03:04:05Z（把不带参数的 new Date() 换成这个时刻）。
const fs = require('fs');
const [src, out] = process.argv.slice(2);
const text = fs.readFileSync(src, 'utf8');
const a = text.indexOf('const icsEsc');
const b = text.indexOf('async function exportIcs');
if (a < 0 || b < 0) throw new Error('travelkit.js 里找不到 icsEsc / exportIcs');
const RealDate = Date;
const FIXED = RealDate.UTC(2026, 10, 8, 3, 4, 5);
class FixedDate extends RealDate { constructor(...x) { if (x.length) super(...x); else super(FIXED); } }
const validStops = p => (p.stops || []).filter(s => Number.isFinite(+s.lat) && Number.isFinite(+s.lng)).slice().sort((x, y) => (x.order ?? 0) - (y.order ?? 0));
const ics = new Function('Date', 'TP', text.slice(a, b) + '\nreturn ics;')(FixedDate, () => ({ validStops }));
const plan = {
  id: 'p1', name: '北海道, 冬天; 泡温泉', startDate: '2026-12-05', endDate: '2026-12-12', notes: '第一行\n第二行，带逗号, 和分号; 还有反斜杠 \\ 结束',
  bookings: [
    { id: 'b1', kind: 'flight', title: 'NH 123', from: '上海浦东', to: '札幌新千岁', start: '2026-12-05T09:30', ref: 'ABC123', notes: '靠窗' },
    { id: 'b2', kind: 'hotel', title: '定山溪温泉酒店', start: '2026-12-05', end: '2026-12-08' },
    { id: 'b3', kind: 'train', title: 'JR 特急', from: '札幌', to: '小樽', start: '2026-12-08T10:00', end: '2026-12-08T10:45' },
    { id: 'b4', kind: 'ticket', title: '白色恋人公园', start: '2026-12-09' },
    { id: 'b5', kind: 'other', title: '没日期的不导出' }
  ],
  stops: [
    { id: 's1', name: '札幌', lat: 43.06, lng: 141.35, order: 0, schedule: [
      { date: '2026-12-06', time: '09:00', end: '11:30', text: '二条市场吃早饭' },
      { date: '2026-12-06', text: '这是一条很长很长的日程，用来测试每行七十五个字节的折行规则：中文一个字三个字节，所以大概二十五个字就要折一次行，再加点英文 ABC def 混在一起看看' },
      { date: '2026-12-07', time: '19:00', text: '大通公园看灯\n第二行描述' },
      { text: '没日期，不导出' }
    ] },
    { id: 's2', name: '小樽', lat: 43.19, lng: 141.0, order: 1, schedule: [ { date: '2026-12-08', time: '13:00', end: '12:00', text: '运河' } ] },
    { name: '没坐标的站', schedule: [ { date: '2026-12-09', text: 'x' } ] }
  ]
};
const r = ics(plan);
fs.writeFileSync(out, JSON.stringify({ now: '2026-11-08T03:04:05Z', plan, count: r.count, text: r.text }, null, 1) + '\n');
console.log('写好了：', out, r.count, '项');
