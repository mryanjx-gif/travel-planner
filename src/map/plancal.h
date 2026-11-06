// 日历（0.11.2，C-01–C-03、C-08–C-10；照 Electron pages-calendar.js）。只依赖数据层，单元测试 tst_map 共用。
// 改数据的函数只动日期相关的键，别的键原样保留；交通住宿、消费、待办里的日期不动（那些是实际发生的事）。
// 一周固定从周一开始（"一周从周一或周日开始"在 0.11.3）。
#pragma once

#include "jsonvalue.h"

#include <QSet>
#include <QString>
#include <QVariantMap>

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
// 月视图：这个月的周（周一开始，最多 6 周）。
// { weeks: [{ start, lanes, days: [{ date, d, out, today }], bars: [{ id, name, color, done, a, b（0–6）, lane, start, end }] }] }
QVariantMap calMonth(const json::Value &root, const QSet<QString> &hidden, int y, int m, const QString &today);
// 右边：{ planning: [{ id, name, color, status, days }], wish: […], hidden: [{ id, name, color }] }。
// 没定日期的、不是已完成、没隐藏的按状态分组（comparePlansNoPin）
QVariantMap calSide(const json::Value &root, const QSet<QString> &hidden);
// 选年月：{ years: [有定了日期的计划的年份], counts: { "2026-12": [颜色…（最多 4 个）] } }
QVariantMap calPicker(const json::Value &root, const QSet<QString> &hidden);

} // namespace tp::map::plans
