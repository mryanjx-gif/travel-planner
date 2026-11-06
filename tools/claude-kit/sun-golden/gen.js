// 用 Electron 打包的 suncalc 和 travelkit.js sunInfo 的逻辑算一组对照值，写到 tests/golden/sun.json（0.10.0）。
// 用法：node tools/claude-kit/sun-golden/gen.js
const fs = require('fs'), path = require('path');
const SC = require('D:/旅行计划App/app/vendor/suncalc.js');
const pad = n => String(n).padStart(2, '0');
function hm(date, tz, lng) {
  if (tz) { try { return new Intl.DateTimeFormat('en-GB', { hour: '2-digit', minute: '2-digit', hourCycle: 'h23', timeZone: tz }).format(date); } catch (e) {} }
  const d = new Date(date.getTime() + Math.round((+lng || 0) / 15) * 3600e3);
  return pad(d.getUTCHours()) + ':' + pad(d.getUTCMinutes());
}
function sunInfo(lat, lng, dateStr, tz) {
  const noon = new Date(Date.parse(dateStr + 'T12:00:00Z') - (+lng) / 15 * 3600e3);
  const t = SC.getTimes(noon, +lat, +lng);
  if (t.alwaysUp) return { polar: 'day' };
  if (t.alwaysDown) return { polar: 'night' };
  return { rise: hm(t.sunrise, tz, lng), set: hm(t.sunset, tz, lng), dayMin: Math.round((t.sunset - t.sunrise) / 60000), approx: !tz };
}
const cases = [
  [39.47, 75.99, '2026-11-22', 'Asia/Shanghai'],     // 喀什：中国一个时区
  [39.47, 75.99, '2026-11-22', ''],                  // 同一站没有时区：按经度估算
  [31.23, 121.47, '2026-06-21', 'Asia/Shanghai'],
  [35.01, 135.77, '2026-11-22', 'Asia/Tokyo'],
  [-37.81, 144.96, '2026-01-15', 'Australia/Melbourne'],   // 夏令时
  [-37.81, 144.96, '2026-07-15', 'Australia/Melbourne'],
  [-12.46, 130.84, '2026-03-01', 'Australia/Darwin'],
  [-34.93, 138.6, '2026-12-01', 'Australia/Adelaide'],     // +10:30
  [-31.95, 115.86, '2026-05-01', 'Australia/Perth'],
  [-42.88, 147.33, '2026-10-10', 'Australia/Hobart'],
  [-31.95, 141.45, '2026-02-01', 'Australia/Broken_Hill'],
  [27.71, 85.32, '2026-04-01', 'Asia/Kathmandu'],          // +05:45
  [42.87, 74.59, '2026-08-01', 'Asia/Bishkek'],
  [48.21, 16.37, '2026-03-29', 'Europe/Vienna'],           // 换夏令时那天
  [41.01, 28.98, '2026-09-01', 'Europe/Istanbul'],
  [41.72, 44.79, '2026-09-01', 'Asia/Tbilisi'],
  [39.65, 66.96, '2026-05-05', 'Asia/Samarkand'],
  [13.75, 100.5, '2026-12-25', 'Asia/Bangkok'],
  [30.04, 31.24, '2026-07-01', 'Africa/Cairo'],
  [55.75, 37.62, '2026-06-01', 'Europe/Moscow'],
  [40.71, -74.0, '2026-11-01', 'America/New_York'],
  [64.15, -21.94, '2026-12-21', 'Atlantic/Reykjavik'],
  [69.65, 18.96, '2026-06-21', 'Europe/Oslo'],             // 特罗姆瑟：极昼
  [69.65, 18.96, '2026-12-21', 'Europe/Oslo'],             // 极夜
  [-77.85, 166.67, '2026-12-21', 'Antarctica/McMurdo'],    // 南极：极昼
  [51.5, -179.0, '2026-03-20', ''],                        // 180° 附近、没有时区
];
const out = cases.map(([lat, lng, date, tz]) => ({ lat, lng, date, tz, want: sunInfo(lat, lng, date, tz) }));
fs.writeFileSync(path.join(__dirname, '../../../tests/golden/sun.json'), JSON.stringify(out, null, 1) + '\n');
console.log(out.length, 'cases');
