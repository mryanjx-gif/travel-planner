// 交通住宿和消费的计算（0.10.1，P-08、P-11、P-12；PO 选 1A：交通住宿的费用分开显示、合在一起算）。只依赖数据层，单元测试 tst_map 共用。
//   - spendRows：一个计划"花了的钱"的每一行 = 消费记录 + 填了费用、还没记到消费的交通住宿。账本（0.10.2）也从这里算；
//   - spendInfo：消费标签要的全部内容（合计、预算、分类、缺哪些汇率）；
//   - perMain / parseRates：汇率（Electron prefs.js mainPer、main.js rates:fetch）；
//   - bookingDays / stopBookings / missingNights：交通住宿按天分组、挂到站点上（J2）、没订住宿的晚上（J1）；
//   - putBooking / removeBooking / putExpense / removeExpense / setBudget：修改，只动界面上有的键，别的键原样保留。
// 界面文字由 QML 翻译，这里只给结构化的数据和原因的键。
#pragma once

#include "jsonvalue.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace tp::map::plans {

// ---- 汇率 ----
struct Rates {
    QString base, date, src;
    QHash<QString, double> table;   // 1 base = 多少这种货币
};
// src："frankfurter" / "er-api"。回复认不出时 table 是空的
Rates parseRates(const QByteArray &body, const QString &src, const QString &base);
// 1 cur = 多少 main；不知道时 < 0。表的基准不是 main 时交叉换算（刚换主货币、离线时也能算）
double perMain(const Rates &r, const QString &main, const QString &cur);
QStringList currencyCodes();                        // 24 种（Electron prefs.js CURRENCIES）
QString planCurrency(const json::Value &plan);      // 计划的币种，没写时 AUD（Electron planCur 的默认）

// ---- 花了的钱 ----
struct SpendRow {
    QString id;             // 消费的 id；来自交通住宿时是空的
    QString bookingId;      // 来自哪条交通住宿（fromBooking，或者 linked 的消费）
    bool fromBooking = false;   // 交通住宿的费用，没有对应的消费记录：只能在交通住宿那边改
    bool linked = false;        // 消费记录，但记着 bookingId、那条交通住宿还在
    QString kind;               // fromBooking 时的交通住宿类型（没写名字时界面用类型名代替）
    QString date, cat, title, cur, notes;
    double amount = 0;
};
// 按日期从新到旧（没写日期的放最后）
QVector<SpendRow> spendRows(const json::Value &plan);
// { currency, total, bookTotal, expTotal, missing, mainTotal, mainMissing, cats: [{ k, v, pct, share }], rows, budget, remain, over, pct, days, daily }
// total 等都按计划的币种；mainTotal 按主货币；missing / mainMissing = 缺汇率、没算进去的币种
QVariantMap spendInfo(const json::Value &plan, const QString &main, const Rates &rates);

// ---- 交通住宿 ----
// [{ day, rows: [{ id, kind, title, from, to, start, end, ref, cost, notes, linked }] }]，从早到晚，没填时间的放最后
QVariantList bookingDays(const json::Value &plan);
double bookingsCost(const json::Value &plan);
// 每个有效站点当天的交通住宿（J2）：[{ id, kind, title }]。一条只挂一次：住宿挂到入住那天所在的站（换站那天算下一站），
// 其余挂到出发那天所在的站（换站那天算这一站，"坐什么走"）
QVector<QVariantList> stopBookings(const json::Value &plan);
// 还没有住宿记录的晚上（J1）：只算"计划中"的、今天以后的晚上。过夜的交通算住了。*undated = 没填日期的住宿有几条（说不准它们住哪晚）
QStringList missingNights(const json::Value &plan, const QString &today, int *undated = nullptr);

// ---- 修改。返回空 = 改好了；否则是原因的键，调用方要丢掉改了一半的副本（PlanPageController::applyPlan）----
// id 空 = 新加。f：kind title from to startDate startTime endDate endTime ref cost notes toExp。
// 错误：badCost / badStart / badEnd（时间不对或者填了时间没填日期）/ endBeforeStart / gone。改了费用同步对应的消费（Electron editBooking）
QString putBooking(json::Value &plan, const QString &id, const QVariantMap &f);
// 删一条，连带删对应的消费。返回连带删了几笔；没有这条返回 -1
int removeBooking(json::Value &plan, const QString &id);
// id 空 = 新加。f：title amount currency cat date notes。错误：badAmount / badDate / gone。别的键（paidBy、split）原样保留
QString putExpense(json::Value &plan, const QString &id, const QVariantMap &f);
bool removeExpense(json::Value &plan, const QString &id);
// 预算：空或 0 = 去掉。错误：badBudget（不是数字或负数）
QString setBudget(json::Value &plan, const QString &text, const QString &currency);

} // namespace tp::map::plans
