// 计划的排序、筛选、"此刻"（0.8.7）。只依赖数据层，单元测试 tst_map 共用。
// 全部照搬 Electron v6.3.0：
//   - 排序：map-app.js planGroupRank / comparePlans / comparePlansNoPin；
//   - 区域、状态、类型、筛选：REGIONS / REGION_ALIAS / planRegions / filterMatches / mapPlans / revealPlan；
//   - 此刻：mapfirst.js stopOn / pickNow / todayItems / todos / notices（"今天"一律是本地日期 YYYY-MM-DD）。
// 改数据的几个操作（设为下次出发、勾待办、置顶、标记为已完成、删除计划）也在这里，只改传进来的 json，调用方负责保存。
#pragma once

#include "jsonvalue.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace tp::map::plans {

// ---- 基本属性 ----
QString statusOf(const json::Value &plan);          // planning / wish / completed（认不出来的算 planning）
QString typeOf(const json::Value &plan);            // route / single
QStringList regionsOf(const json::Value &plan);     // 按 REGIONS 的顺序；旧名换成新名（西亚 → 西亚/中亚）
const QStringList &allRegions();                    // 9 个区域 + "未分类"
QString regionNone();                               // "未分类"
const QStringList &allStatuses();                   // planning, wish, completed
const QStringList &allTypes();                      // single, route

// ---- 排序 ----
int groupRank(const json::Value &plan);
// 计划名的比较（Electron localeCompare(…, 'zh')）：空 → 标点和符号 → 数字 → 汉字（拼音）→ 其他文字
int compareNames(const QString &a, const QString &b);
// 返回 <0 / 0 / >0（和 JS 的比较函数一样）；withPin = 地图页的置顶在最前（comparePlans），否则 comparePlansNoPin
int compare(const json::Value &a, const json::Value &b, bool withPin);
// 排好序的计划下标（稳定排序，和 JS Array.prototype.sort 一样）
QVector<int> sortedIndexes(const json::Value &root, bool withPin = true);

// ---- 筛选 ----
struct Filter
{
    QSet<QString> status, type, region;
    QSet<QString> hidden;            // 眼睛隐藏的计划 id
    static Filter all();             // 全选、没有隐藏
    bool matches(const json::Value &plan) const;   // filterMatches（不看隐藏）
    bool onMap(const json::Value &plan) const { return matches(plan) && !hidden.contains(plan[QStringLiteral("id")].toString()); }
    int excludedCount() const;       // 被排除掉的选项数（筛选按钮上的角标）
    // 被筛掉或隐藏的计划要显示时：打开它需要的选项、取消隐藏。返回打开的选项（中文标签的键：状态 / 类型 / 区域名），
    // unhid = 取消了隐藏。什么都没改返回空且 unhid = false
    QStringList reveal(const json::Value &plan, bool *unhid);
    // 存进 settings.json / 从里面读（只认已知取值，认不出来的丢掉；没有存过 = 全选）
    QVariantMap toVariant() const;
    static Filter fromVariant(const QVariantMap &m, const QSet<QString> &planIds);
    bool operator==(const Filter &o) const { return status == o.status && type == o.type && region == o.region && hidden == o.hidden; }
};

// 有效站点（validStops）在 stops 数组里的下标；按 id 找计划（没有返回 nullptr）
QVector<int> validStops(const json::Value &plan);
const json::Value *planById(const json::Value &root, const QString &id);
json::Value *mutPlan(json::Value &root, const QString &id);

// ---- 日期（mapfirst.js 的小工具） ----
bool okDate(const QString &s);
int days(const QString &a, const QString &b);       // b - a 的天数
QString addDays(const QString &s, int n);
bool dated(const json::Value &plan);

// ---- 此刻 ----
struct Now
{
    QString kind;        // none / live / next / back
    int planIndex = -1;
};
Now pickNow(const json::Value &root, const QString &today);
// 给界面的全部内容（"此刻"卡片 + 原型条上的入口）：kind、计划、第几天、今天的安排、下一站、待办、提醒…… 见 plans.cpp
QVariantMap nowInfo(const json::Value &root, const QString &today);

// ---- 简易计划面板 ----
QVariantMap sheetInfo(const json::Value &root, const QString &planId);

// ---- "我的旅程"列表（0.8.8）：{ rows: [{ id, name, color, status, type, stops, startDate, endDate, days, pinned, hidden, isNext, group, groupCount }],
//      total, planning, wish, completed }。rows 只看筛选（眼睛隐藏的照样列出、hidden 为真），顺序 = comparePlans ----
// 0.8.10 byYear（足迹模式）：按年份分组（group = "y2025"…，没有日期的 group = "undated" 放最后），年份新的在前、同一年里日期新的在前；
// 不单列置顶
QVariantMap listInfo(const json::Value &root, const Filter &f, bool byYear = false);

// ---- Ctrl K 的计划和站点（0.8.8）：按 DATA.plans 的原顺序，[{ kind: plan, id, label, status, startDate, endDate, color }
//      或 { kind: stop, id, stop（有效站点里的下标）, label, planName, color }]；没名字的站点不列（palette.js source） ----
QVariantList commandItems(const json::Value &root);

// ---- 改数据（成功返回 true；没有这个计划 / 没有变化返回 false） ----
bool setNextTrip(json::Value &root, const QString &planId, bool on);
bool setTodoDone(json::Value &root, const QString &planId, const QString &todoId, bool done);
bool togglePin(json::Value &root, const QString &planId, double nowMs);
bool markCompleted(json::Value &root, const QString &planId);
bool deletePlan(json::Value &root, const QString &planId);

} // namespace tp::map::plans
