// 日历（0.11.2，C-01–C-03、C-08–C-10；照 Electron pages-calendar.js）。只依赖数据层，单元测试 tst_map 共用。
// 改数据的函数只动日期相关的键，别的键原样保留；交通住宿、消费、待办里的日期不动（那些是实际发生的事）。
// 一周从周一或周日开始（0.11.3，C-11）：weekStart = 1 周一（默认）/ 0 周日，和 Electron 的 weekStart 一样。
#pragma once

#include "jsonvalue.h"

#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace tp::map::plans {

// 天数：有起止日期按它；没有按站点日期的跨度；再没有按 days（1–999）；都没有 1 天（planDays）
int planDays(const json::Value &plan);
// 交通住宿里写了日期的条数（挪计划时提醒，这些不跟着平移）
int datedBookings(const json::Value &plan);
// 站点的到达、离开、日程里的日期整体平移 n 天（shiftStops）
void shiftStops(json::Value &plan, int n);
// 放到从 start 开始、共 days 天；站点日期跟着平移，参照点是原来的开始日期，没有就用站点里最早的一天（placePlan）。start 不是日期返回 false
bool placePlan(json::Value &plan, const QString &start, int days);
// 去掉日期：天数存进 days，起止、站点的到达 / 离开清空，日程的日期去掉（unplacePlan）
void unplacePlan(json::Value &plan);
// 和 [start, end] 重叠的其他计划有几个（R2）。在日历里隐藏的不算
int overlapCount(const json::Value &root, const QString &planId, const QString &start, const QString &end, const QSet<QString> &hidden);

// 年视图：从 y 年 m 月（1–12）起连续 12 个月。
// { start, spanDays, lanes, today（0–1，不在范围内 -1）, months: [{ y, m, cur, l（0–1）}],
//   bars: [{ id, name, color, done, l, r（0–1）, lane, narrow, labelLeft, start, end }] }
// narrow = 横条放不下名字（名字写在旁边，排行时把名字也算进去）
QVariantMap calYear(const json::Value &root, const QSet<QString> &hidden, int y, int m, const QString &today);
// day 所在这一周的第一天（mondayOf）
QString weekStartOf(const QString &day, int weekStart);
// 月视图：这个月的周（最多 6 周）。
// { weeks: [{ start, lanes, days: [{ date, d, out, today }], bars: [{ id, name, color, done, a, b（0–6）, lane, start, end }] }] }
QVariantMap calMonth(const json::Value &root, const QSet<QString> &hidden, int y, int m, const QString &today, int weekStart = 1);
// 周视图（C-04）：从 weekFirst 起 7 天。分钟都按当天算，跨午夜的拆成两段。
// { start, lanes, first（打开时滚到第几分钟）, empty,
//   days: [{ date, d, m, wd（0 = 周日）, today }],
//   bars: [{ kind（plan / hotel）, id, name, color, done, a, b（0–6）, lane, start, end, cutL, cutR }],
//   stops: 7 个列表 [{ id（计划）, name, plan, color, n（路线的第几站，别的 0）, loose: [没写时间的日程] }],
//   events: 7 个列表 [{ id（计划）, plan, tab, kind（交通的种类，日程是空）, title, sub, time, color, a, b（分钟）, col, n（这一组并排几个）}] }
QVariantMap calWeek(const json::Value &root, const QSet<QString> &hidden, const QString &weekFirst, const QString &today);

// 斋月（C-06，T1）：这一公历年碰到的每一段（表在 ramadan_table.inc，tools/gen-ramadan.js 生成，2024–2075 年；表外的年没有）。
// 跨公历年的是完整的一段，两边的年都能查到。日期是 ISO 字符串
struct RamadanSpan { const char *a, *b; };
QVector<RamadanSpan> ramadanSpans(int year);

// 季节表（C-05、C-07）：读 seasonGuide（和 seasonRows / seasonInfo 同一份）。visited = 去过的代码（UnlockModel::visitedCodes），nullptr = 还在算。
// { rows: 一维列表，分组头 { head: true, group, count, visited, closed } 和一行 { head: false, key, name, nameEn, visited,
//   segs: [{ from, to（1–12）, kind（没写的是 none）, text, textEn }], ram: [{ l, r（0–1）, a, b, labelLeft }],
//   plans: [{ id, name, wish }] } ；折叠的组只有头 ），
//   groups: [组名], ready: 去过算好了没有, year }
QVariantMap calSeason(const json::Value &root, int year, const QSet<QString> *visited, const QSet<QString> &closed);
// 右边：{ planning: [{ id, name, color, status, days }], wish: […], hidden: [{ id, name, color }] }。
// 没定日期的、不是已完成、没隐藏的按状态分组（comparePlansNoPin）
QVariantMap calSide(const json::Value &root, const QSet<QString> &hidden);
// 选年月：{ years: [有定了日期的计划的年份], counts: { "2026-12": [颜色…（最多 4 个）] } }
QVariantMap calPicker(const json::Value &root, const QSet<QString> &hidden);

} // namespace tp::map::plans
