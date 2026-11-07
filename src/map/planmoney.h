// 交通住宿和消费的计算（0.10.1，P-08、P-11、P-12；PO 选 1A：交通住宿的费用分开显示、合在一起算）。只依赖数据层，单元测试 tst_map 共用。
//   - spendRows：一个计划"花了的钱"的每一行 = 消费记录 + 填了费用、还没记到消费的交通住宿。账本（0.10.2）也从这里算；
//   - spendInfo：消费标签要的全部内容（合计、预算、分类、缺哪些汇率）；
//   - perMain / parseRates：汇率（Electron prefs.js mainPer、main.js rates:fetch）；
//   - bookingDays / stopBookings / missingNights：交通住宿按天分组、挂到站点上（J2）、没订住宿的晚上（J1）；
//   - putBooking / removeBooking / putExpense / removeExpense / setBudget / addPerson / editPerson：修改，只动界面上有的键，别的键原样保留；
//   - settleInfo：分账（0.10.2，P-13，Electron split.js）；ledgerInfo：账本（0.10.2，B-01）。
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
    QHash<QString, double> manual;  // 手动汇率（settings.json manualRates，0.12.4）：1 这种货币 = 多少主货币，优先于 table
};
// src："frankfurter" / "er-api"。回复认不出时 table 是空的
Rates parseRates(const QByteArray &body, const QString &src, const QString &base);
// 1 cur = 多少 main；不知道时 < 0。表的基准不是 main 时交叉换算（刚换主货币、离线时也能算）
// 手动填的优先（Electron prefs.js mainPer）
double perMain(const Rates &r, const QString &main, const QString &cur);
double onlinePerMain(const Rates &r, const QString &main, const QString &cur);   // 只看联网的表（设置页的占位提示）
// 设置页的手动汇率表（Electron moneyHtml）：AUD、USD、JPY、EUR + 有花销或预算的计划的币种 + 填过手动汇率的 + extra，去掉主货币。
// [{ cur, online（< 0 = 没有）, manual（0 = 没填）}]
QVariantList rateRows(const json::Value &root, const QString &main, const Rates &r, const QStringList &extra);
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
// { currency, total, bookTotal, expTotal, missing, mainTotal, mainMissing, cats: [{ k, v, pct, share }], rows, budget, remain, over, pct, days, daily, 加上 settleInfo 的 }
// total 等都按计划的币种；mainTotal 按主货币；missing / mainMissing = 缺汇率、没算进去的币种
// 有同行人时，消费记录的 rows 还有 payer、payerName、among（id）、onlyName、custom（有人写了固定金额）、amounts（只有写了金额的人：id → 金额）
QVariantMap spendInfo(const json::Value &plan, const QString &main, const Rates &rates);

// ---- 分账（Electron split.js）。只算消费记录：交通住宿上的费用没有付款人和分摊，不进结算，要分摊就先记到消费 ----
// { people: [{ id, name, used, uses: [{ title, cat, date }]（付过钱或被单独写进 split 的那几笔）}], multi（≥ 2 人）, expenseCount, bookingOnly（有多少钱只在交通住宿上）,
//   shareCount（split 为空的笔数：再加人时会跟着变）, bookingCount（有费用、还没记到消费的交通住宿，再加人时会记一笔）,
//   settle: { cur, rows: [{ id, name, paid, share, net }], transfers: [{ from, to, amount }]（名字）, missing, rates: [{ cur, per }], date } }
QVariantMap settleInfo(const json::Value &plan, const QString &main, const Rates &rates);

// ---- 账本：所有计划花的钱，都换成主货币 ----
// { total（已花）, future（还没去的预计合计）, spentCount, futureCount, trips: [{ id, name, status, state（completed / live / planning / wish）, done（算不算已花）, start, end, days, spent, budget, budgetCur, budgetMain（< 0 = 换不了）}]（花得多的在前）, emptyCount, perTrip, perDay,
//   cats: [{ k, v, pct, share }], years: [{ y（"" = 没写日期）, v, share }]（新的在前）, missing }。平均、按类别、按年份只算已花（0.10.5）。today = YYYY-MM-DD
QVariantMap ledgerInfo(const json::Value &root, const QString &main, const Rates &rates, const QString &today);

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
// id 空 = 新加。f：kind title from to startDate startTime endDate endTime ref cost notes toExp（还没记到消费时记一笔；改旧的也行，要分摊就靠它）。
// 错误：badCost / badStart / badEnd（时间不对或者填了时间没填日期）/ endBeforeStart / gone。改了费用同步对应的消费（Electron editBooking）。
// 两个人以上时不看 toExp，有费用就记一笔（0.11.5，已经有关联消费的不重复记）
QString putBooking(json::Value &plan, const QString &id, const QVariantMap &f);
// 删一条，连带删对应的消费。返回连带删了几笔；没有这条返回 -1
int removeBooking(json::Value &plan, const QString &id);
// id 空 = 新加。f：title amount currency cat date notes，可选 paidBy、split [{ who, amount? }]（有 paidBy 才动分账字段；amount 空 = 不写这个键，平分剩下的）。
// 错误：badAmount / badDate / badSplit（固定金额超过这一笔，退款按绝对值；或者全都固定却不等于这一笔）/ gone。别的键原样保留。新加又没写 paidBy 时：第一个同行人付、所有人平摊
QString putExpense(json::Value &plan, const QString &id, const QVariantMap &f);
bool removeExpense(json::Value &plan, const QString &id);
// 加同行人（0.11.5）：第一次加别人时先放 meName。人数到 2 以上时：旧数据改成第一个人付、所有人平摊，还没记到消费的交通住宿记一笔。错误：emptyName / dupName
QString addPerson(json::Value &plan, const QString &name, const QString &meName);
// 同行人改名或删除。删除只在这个人没付过钱、也没被单独写进 split 时可以（全员平摊的不算用到）。错误：used / gone
QString editPerson(json::Value &plan, const QString &id, const QString &name, bool remove);
// 预算：空或 0 = 去掉。错误：badBudget（不是数字或负数）
QString setBudget(json::Value &plan, const QString &text, const QString &currency);

} // namespace tp::map::plans
