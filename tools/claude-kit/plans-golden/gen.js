// 生成 tests/golden/plans.json：用 Electron v6.3.0 原样的代码算排序、筛选、"此刻"，tst_map 拿原生版的结果逐项比较。
//   map-app.js：REGIONS / normRegion / planRegions / STATUS / planGroupRank / comparePlans / comparePlansNoPin / filterMatches / validStops / revealPlan
//   mapfirst.js：todayStr 之后的日期小工具、stopOn / pickNow / todayItems / todos / notices
// 输入：testdata/data.js 加几处人为改动（置顶、旧区域名、没有状态、只有结束日期、今天的安排、待办截止、证件到期），改动后的数据也写进输出，C++ 用同一份。
// 用法：node gen.js <Electron 的 map-app.js> <Electron 的 mapfirst.js> <data.js> <输出.json>
const fs = require('fs');
const [mapApp, mapFirst, dataJs, out] = process.argv.slice(2);
const A = fs.readFileSync(mapApp, 'utf8'), F = fs.readFileSync(mapFirst, 'utf8');
function fnOf(src, name) {
  const a = src.indexOf('  function ' + name + '(');
  const b = src.indexOf('\n  }\n', a);
  if (a < 0 || b < 0) throw new Error('找不到函数 ' + name);
  return src.slice(a, b + 4);
}
function lineOf(src, start) {
  const a = src.indexOf(start);
  if (a < 0) throw new Error('找不到：' + start);
  return src.slice(a, src.indexOf('\n', a) + 1);
}
function sliceOf(src, a0, b0) {
  const a = src.indexOf(a0), b = src.indexOf(b0, a);
  if (a < 0 || b < 0) throw new Error('找不到：' + a0);
  return src.slice(a, b);
}
const DATA = (() => { const t = fs.readFileSync(dataJs, 'utf8'); const i = t.indexOf('='); return JSON.parse(t.slice(i + 1).trim().replace(/;\s*$/, '')); })();

// ---- 人为改动（覆盖各个分支） ----
const P = id => DATA.plans.find(p => p.id === id);
const byIdx = i => DATA.plans[i];
byIdx(3).pinnedAt = 1700000000000;
byIdx(10).pinnedAt = 1800000000000;
byIdx(5).regions = ['西亚', '欧洲'];
byIdx(6).status = 'weird';
byIdx(7).startDate = ''; byIdx(7).endDate = '2025-01-02';
byIdx(8).regions = [];
// 一条"在路上"的路线：今天 2026-11-05 是第 3 天
const live = DATA.plans.find(p => p.type === 'route' && (p.stops || []).length >= 4);
live.startDate = '2026-11-03'; live.endDate = '2026-11-08';
const vs = (live.stops || []).slice().sort((a, b) => (a.order ?? 0) - (b.order ?? 0));
vs[0].arriveDate = '2026-11-03'; vs[1].arriveDate = '2026-11-04'; vs[2].arriveDate = '2026-11-06'; vs[3].arriveDate = '2026-11-07';
vs[1].schedule = [{ date: '2026-11-05', time: '09:30', text: '东福寺' }, { date: '2026-11-05', time: '14:00', text: '锦市场' }, { date: '2026-11-06', text: '明天的' }];
vs[1].legMode = 'train';
live.bookings = (live.bookings || []).concat([{ id: 'bk_t1', title: '晚上的船', start: '2026-11-05T19:00' }]);
live.todos = [{ id: 't1', text: '订午餐', done: false, due: '2026-11-07' }, { id: 't2', text: '换现金', done: false },
  { id: 't3', text: '已做完', done: true }, { id: 't4', text: '过期的', done: false, due: '2026-11-01' }, { id: 't5', text: '远的', done: false, due: '2026-12-30' }];
// 两个日期已过还是"计划中"的计划
byIdx(1).startDate = '2026-10-01'; byIdx(1).endDate = '2026-10-05'; byIdx(1).status = 'planning';
byIdx(2).startDate = '2026-09-01'; byIdx(2).endDate = '2026-09-05'; byIdx(2).status = 'planning';
DATA.documents = [{ label: '护照', expires: '2026-12-20' }, { label: '签证', expires: '2026-10-30' }, { label: '驾照', expires: '2027-06-01' }, { label: '学生证', expires: '2026-11-05' }];

// revealPlan / mapfirst 用到的外部东西：最小的替身
const state = { filterStatus: new Set(), filterType: new Set(), filterRegion: new Set(), hiddenIds: new Set() };
const noop = () => {};
const saveHiddenPref = noop, syncFilterBoxes = noop, updateFilterCount = noop, saveFilterPref = noop, renderMap = noop, renderList = noop;
let lastToast = '';
const showToast = t => { lastToast = t; };
const window = { TPLang: 'zh', dispatchEvent: noop };
class CustomEvent { constructor() {} }
const DAY = 86400000;
let api;
const code = [
  sliceOf(A, '  const STATUS = {', '  const STATUS_KEYS'), lineOf(A, '  const STATUS_KEYS'),
  lineOf(A, '  const TYPE_LABEL'), lineOf(A, '  const REGIONS ='), lineOf(A, '  const REGION_ALIAS'), lineOf(A, '  const normRegion'), lineOf(A, '  const REGION_NONE'),
  fnOf(A, 'planRegions'), fnOf(A, 'validStops'), fnOf(A, 'planGroupRank'), fnOf(A, 'comparePlans'), fnOf(A, 'comparePlansNoPin'),
  fnOf(A, 'filterMatches'), fnOf(A, 'revealPlan'),
  sliceOf(F, '  const dt = s =>', '  const esc ='), lineOf(F, '  const dated ='),
  fnOf(F, 'stopOn'), fnOf(F, 'pickNow'), fnOf(F, 'todayItems'), fnOf(F, 'todos'), fnOf(F, 'notices'),
  'api = { STATUS_KEYS, REGIONS, REGION_NONE, planRegions, comparePlans, comparePlansNoPin, filterMatches, revealPlan, stopOn, pickNow, todayItems, todos, notices, days, okDate, addDays, validStops, TYPE_LABEL };'
].join('\n');
const TP = () => ({ DATA, validStops: api.validStops });
eval(code);

const ids = l => l.map(p => p.id);
// 写进输出的输入数据只留用得到的字段（文件小一点）
const KEEP_P = ['id', 'name', 'type', 'status', 'regions', 'startDate', 'endDate', 'color', 'pinnedAt', 'todos', 'bookings'];
const KEEP_S = ['id', 'name', 'lat', 'lng', 'order', 'arriveDate', 'departDate', 'schedule', 'legMode', 'color'];
const pick = (o, keys) => Object.fromEntries(keys.filter(k => k in o).map(k => [k, o[k]]));
const result = { input: { plans: DATA.plans.map(p => Object.assign(pick(p, KEEP_P), { stops: (p.stops || []).map(s => pick(s, KEEP_S)) })), documents: DATA.documents } };
result.sortPin = ids(DATA.plans.slice().sort(api.comparePlans));
result.sortNoPin = ids(DATA.plans.slice().sort(api.comparePlansNoPin));

// ---- 筛选 ----
const ALL = { status: api.STATUS_KEYS.slice(), type: ['single', 'route'], region: [...api.REGIONS, api.REGION_NONE] };
const LABEL2KEY = { '计划中': 'planning', '想去': 'wish', '已完成': 'completed', '单一目的地': 'single', '路线': 'route' };
const filters = [
  { status: ALL.status, type: ALL.type, region: ALL.region, hidden: [] },
  { status: ['planning'], type: ALL.type, region: ALL.region, hidden: [] },
  { status: ALL.status, type: ['route'], region: ['中国', '欧洲'], hidden: [DATA.plans[0].id, DATA.plans[4].id] },
  { status: ['wish', 'completed'], type: ['single'], region: [api.REGION_NONE], hidden: [] },
  { status: [], type: [], region: [], hidden: [] },
  { status: ALL.status, type: ALL.type, region: ['西亚/中亚'], hidden: [] },
];
result.filters = filters.map(f => {
  const set = () => { state.filterStatus = new Set(f.status); state.filterType = new Set(f.type); state.filterRegion = new Set(f.region); state.hiddenIds = new Set(f.hidden); };
  set();
  const onMap = ids(DATA.plans.filter(p => api.filterMatches(p) && !state.hiddenIds.has(p.id)));
  const excluded = ['status', 'type', 'region'].reduce((t, k) => t + ALL[k].length - f[k].length, 0);
  // 对前 8 个计划逐个 reveal（每次从这个筛选重新开始）
  const reveals = DATA.plans.slice(0, 8).concat([DATA.plans[4]]).map(p => {
    set();
    const before = [...state.filterStatus, ...state.filterType, ...state.filterRegion];
    const unhidBefore = state.hiddenIds.has(p.id);
    api.revealPlan(p);
    const opened = [...state.filterStatus, ...state.filterType, ...state.filterRegion].filter(x => !before.includes(x));
    return { id: p.id, opened, unhid: unhidBefore && !state.hiddenIds.has(p.id) };
  });
  return { filter: f, onMap, excluded, reveals };
});

// ---- 此刻 ----
const routeIds = DATA.plans.filter(p => p.id !== live.id).map(p => p.id);
const cases = [
  { today: '2026-11-05', next: live.id }, { today: '2026-11-03', next: live.id }, { today: '2026-11-08', next: live.id },
  { today: '2026-11-01', next: live.id }, { today: '2026-11-12', next: live.id }, { today: '2026-11-07', next: live.id },
  { today: '2026-11-05', next: DATA.plans[0].id }, { today: '2026-11-05', next: 'no-such-id' }, { today: '2026-11-05', next: undefined },
  { today: '2026-11-05', next: DATA.plans.find(p => p.status === 'completed' && p.endDate).id },
];
result.now = cases.map(c => {
  if (c.next === undefined) delete DATA.nextTripId; else DATA.nextTripId = c.next;
  const h = api.pickNow(c.today);
  const o = { today: c.today, next: c.next === undefined ? null : c.next, kind: h.kind };
  if (h.kind === 'none') return o;
  const p = h.plan;
  o.planId = p.id;
  if (h.kind === 'live') {
    o.dayN = api.days(p.startDate, c.today) + 1; o.dayTotal = api.days(p.startDate, p.endDate) + 1;
    const { st, i } = api.stopOn(p, c.today);
    const here = st[i] || st[0];
    o.here = here ? here.name : p.name;
    const nx = i >= 0 ? st[i + 1] : null;
    if (nx && api.okDate(nx.arriveDate)) o.nextStop = { name: nx.name, arriveDate: nx.arriveDate, tomorrow: nx.arriveDate === api.addDays(c.today, 1) };
    o.todayCount = api.todayItems(p, c.today).length;
  } else if (h.kind === 'next') {
    o.daysUntil = (api.okDate(p.startDate) && api.okDate(p.endDate) && p.endDate >= p.startDate) ? api.days(c.today, p.startDate) : -1;
  } else {
    o.daysAgo = api.days(p.endDate, c.today);
  }
  o.todos = api.todos(c.today).filter(x => x.p.id === p.id).slice(0, 3).map(x => x.td.id);
  o.todoOpen = (p.todos || []).filter(x => x && !x.done && x.text).length;
  const allN = api.notices(c.today);
  const od = allN.filter(x => x.kind === 'done'), dc = allN.filter(x => x.kind === 'doc');
  const nt = od.slice(0, 1).concat(od.length > 1 ? [{ kind: 'more', n: od.length - 1 }] : [], dc.slice(0, 2));
  o.notices = nt.map(x => x.kind === 'done' ? { kind: 'done', planId: x.p.id } : x.kind === 'more' ? { kind: 'more', n: x.n } : { kind: 'doc', label: x.d.label, n: x.n });
  return o;
});
delete DATA.nextTripId;
fs.writeFileSync(out, JSON.stringify(result));
console.log('计划', DATA.plans.length, '筛选', result.filters.length, '此刻', result.now.length, '→', out);
