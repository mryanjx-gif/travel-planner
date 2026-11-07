#include "planmoney.h"

#include "planpage.h"
#include "plans.h"

#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QRandomGenerator>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <functional>

namespace tp::map::plans {

using json::Value;

namespace {

const QString kBookings = QStringLiteral("bookings"), kExpenses = QStringLiteral("expenses");

QString str(const Value &o, const char *key) { return o[QString::fromLatin1(key)].toString(); }

double num(const Value &o, const char *key)
{
    const Value *v = o.asObject().find(QString::fromLatin1(key));
    if (!v || v->isArray() || v->isObject() || v->isNull()) return 0;
    const double d = v->jsToNumber();
    return std::isfinite(d) ? d : 0;
}

// 数组的键（没有或者不是数组时新建）
json::Array &mutList(Value &plan, const QString &key)
{
    json::Object &o = plan.mutObject();
    Value *v = o.find(key);
    if (!v || !v->isArray()) { o.set(key, Value::array()); v = o.find(key); }
    return v->mutArray();
}

// 和 Electron TP.uid 一样的样子：bk_ + 13 位
QString newId(const char *prefix)
{
    static const char kChars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    QString s = QString::fromLatin1(prefix) + QLatin1Char('_');
    for (int i = 0; i < 13; ++i) s += QLatin1Char(kChars[QRandomGenerator::global()->bounded(36)]);
    return s;
}

// 文字 → 数字（认 1,200）。空或认不出 *ok = false
double parseNum(const QString &text, bool *ok)
{
    QString t = text.trimmed();
    t.remove(QLatin1Char(','));
    const double v = t.toDouble(ok);
    if (!std::isfinite(v)) *ok = false;
    return v;
}

void setOrRemove(json::Object &o, const char *key, const QString &v)
{
    const QString k = QString::fromLatin1(key);
    if (v.isEmpty()) o.remove(k);
    else o.set(k, Value(v));
}

QString catOfKind(const QString &kind)
{
    return kind == QLatin1String("hotel") ? QStringLiteral("stay") : kind == QLatin1String("ticket") ? QStringLiteral("ticket") : QStringLiteral("transport");
}

QString catOf(const QString &c)
{
    static const QStringList kCats = { QStringLiteral("transport"), QStringLiteral("stay"), QStringLiteral("food"), QStringLiteral("ticket"), QStringLiteral("shop"), QStringLiteral("other") };
    return kCats.contains(c) ? c : QStringLiteral("other");
}

// 日期 + 时间 → start / end 的值（2026-12-05T15:00；只有日期时不带时间）。认不出 *ok = false
QString joinDateTime(const QString &date0, const QString &time, bool *ok)
{
    const QString date = date0.trimmed();
    bool tok = true;
    const QString t = normTime(time, &tok);
    if (!tok || (date.isEmpty() && !t.isEmpty()) || (!date.isEmpty() && !okDate(date))) { *ok = false; return {}; }
    return t.isEmpty() ? date : date + QLatin1Char('T') + t;
}

void eachBooking(const Value &plan, const std::function<void(const Value &)> &fn)
{
    for (const Value &b : plan[kBookings].asArray())
        if (b.isObject()) fn(b);
}

// ---- 分账（Electron split.js）----
const QString kPeople = QStringLiteral("people"), kSplit = QStringLiteral("split"), kPaidBy = QStringLiteral("paidBy");

struct Person { QString id, name; };

QVector<Person> peopleOf(const Value &plan)
{
    QVector<Person> out;
    for (const Value &p : plan[kPeople].asArray())
        if (!str(p, "id").isEmpty()) out.push_back({ str(p, "id"), str(p, "name") });
    return out;
}

bool isPerson(const QVector<Person> &ps, const QString &id)
{
    return std::any_of(ps.begin(), ps.end(), [&](const Person &p) { return p.id == id; });
}

QString payerOf(const QVector<Person> &ps, const Value &x)
{
    const QString id = str(x, "paidBy");
    return isPerson(ps, id) ? id : ps.isEmpty() ? QString() : ps.first().id;
}

bool hasAmount(const Value &s)
{
    const Value *v = s.asObject().find(QStringLiteral("amount"));
    return v && !v->isNull() && !(v->isString() && v->toString().isEmpty());
}

// split 里还认得的人（谁都不认得 = 空）
QVector<const Value *> splitOf(const QVector<Person> &ps, const Value &x)
{
    QVector<const Value *> out;
    for (const Value &s : x[kSplit].asArray())
        if (isPerson(ps, str(s, "who"))) out.push_back(&s);
    return out;
}

// 旧数据：付款人和 split 都没写（只算付款人自己）
bool isLegacy(const Value &x) { return str(x, "paidBy").isEmpty() && !x.asObject().contains(kSplit); }

// 这一笔分摊给谁：写了 split 就是那几个人；有付款人时所有人；旧数据（两个都没写）只算付款人自己
QStringList amongOf(const QVector<Person> &ps, const Value &x)
{
    QStringList out;
    for (const Value *s : splitOf(ps, x)) out << str(*s, "who");
    if (!out.isEmpty()) return out;
    if (isLegacy(x) && !ps.isEmpty()) return { payerOf(ps, x) };
    for (const Person &p : ps) out << p.id;
    return out;
}

// 这一笔每个人该出多少（这一笔的币种）：写了金额的按金额，剩下的钱由没写的人平摊
QHash<QString, double> shares(const QVector<Person> &ps, const Value &x)
{
    QHash<QString, double> out;
    if (ps.isEmpty()) return out;
    double rest = num(x, "amount");
    QStringList free;
    const QVector<const Value *> list = splitOf(ps, x);
    for (const Value *s : list) {
        if (hasAmount(*s)) { out[str(*s, "who")] += num(*s, "amount"); rest -= num(*s, "amount"); }
        else free << str(*s, "who");
    }
    if (list.isEmpty()) free = amongOf(ps, x);
    for (const QString &id : std::as_const(free)) out[id] += rest / free.size();
    return out;
}

double round2(double v) { return std::round(v * 100) / 100; }

// 交通住宿记一笔到消费（第一个同行人付、所有人平摊）。b 可能就在 plan 里，先读完再动 plan
void recordBooking(Value &plan, const Value &b)
{
    const QVector<Person> ps = peopleOf(plan);
    json::Object e;
    e.set(QStringLiteral("id"), Value(newId("ex")));
    e.set(QStringLiteral("date"), Value(str(b, "start").left(10)));
    e.set(QStringLiteral("cat"), Value(catOfKind(str(b, "kind"))));
    e.set(QStringLiteral("title"), Value(str(b, "title")));
    e.set(QStringLiteral("amount"), Value(num(b, "cost")));
    e.set(QStringLiteral("bookingId"), Value(str(b, "id")));
    if (!ps.isEmpty()) e.set(kPaidBy, Value(ps.first().id));
    mutList(plan, kExpenses).push_back(Value(std::move(e)));
}

// 两个人以上：旧数据改成第一个人付、所有人平摊；有费用还没记到消费的交通住宿记一笔。写了 split 的不动（单独改过）
void shareAll(Value &plan)
{
    const QVector<Person> ps = peopleOf(plan);
    if (ps.size() < 2) return;
    QSet<QString> recorded;
    if (plan[kExpenses].isArray())
        for (Value &x : mutList(plan, kExpenses)) {
            if (!x.isObject()) continue;
            recorded.insert(str(x, "bookingId"));
            if (isLegacy(x)) x.mutObject().set(kPaidBy, Value(ps.first().id));
        }
    const json::Array bookings = plan[kBookings].asArray();
    for (const Value &b : bookings)
        if (b.isObject() && num(b, "cost") != 0 && !recorded.contains(str(b, "id"))) recordBooking(plan, b);
}

// 这个人付过钱、或者被单独写进 split 的那几笔（全员平摊的不算，以后删了人自动少算一份）
QVariantList usesOf(const QVector<Person> &ps, const Value &plan, const QString &id)
{
    QVariantList out;
    for (const Value &x : plan[kExpenses].asArray()) {
        if (!x.isObject()) continue;
        bool hit = payerOf(ps, x) == id;
        for (const Value &s : x[kSplit].asArray()) if (str(s, "who") == id) hit = true;
        if (hit) out << QVariantMap{ { QStringLiteral("title"), str(x, "title") }, { QStringLiteral("cat"), catOf(str(x, "cat")) }, { QStringLiteral("date"), str(x, "date") } };
    }
    return out;
}

// 分类合计 → 色条（小于等于 0 的不进）
QVariantList catsOut(const QHash<QString, double> &byCat)
{
    double sum = 0;
    for (double v : byCat) if (v > 0) sum += v;
    QVariantList cats;
    for (const char *k : { "transport", "stay", "food", "ticket", "shop", "other" }) {
        const double v = byCat.value(QString::fromLatin1(k));
        if (v > 0) cats << QVariantMap{ { QStringLiteral("k"), QString::fromLatin1(k) }, { QStringLiteral("v"), v }, { QStringLiteral("pct"), qRound(v / sum * 100) }, { QStringLiteral("share"), v / sum } };
    }
    return cats;
}

int tripDays(const Value &plan)
{
    return dated(plan) ? int(QDate::fromString(str(plan, "startDate"), Qt::ISODate).daysTo(QDate::fromString(str(plan, "endDate"), Qt::ISODate))) + 1 : 0;
}

} // namespace

// ---------------- 汇率 ----------------

Rates parseRates(const QByteArray &body, const QString &src, const QString &base)
{
    Rates r;
    const QJsonObject j = QJsonDocument::fromJson(body).object();
    QString date, replyBase;
    if (src == QLatin1String("frankfurter")) {
        date = j.value(QStringLiteral("date")).toString();
        replyBase = j.value(QStringLiteral("base")).toString();
    } else {
        if (j.value(QStringLiteral("result")).toString() != QLatin1String("success")) return r;
        replyBase = j.value(QStringLiteral("base_code")).toString();
        const qint64 t = qint64(j.value(QStringLiteral("time_last_update_unix")).toDouble());
        if (t > 0) date = QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC).date().toString(Qt::ISODate);
    }
    if (!replyBase.isEmpty() && replyBase != base) return r;   // 基准对不上，数不能用
    const QJsonObject rates = j.value(QStringLiteral("rates")).toObject();
    for (auto it = rates.begin(); it != rates.end(); ++it) {
        const double v = it.value().toDouble();
        if (v > 0) r.table.insert(it.key(), v);
    }
    if (r.table.isEmpty()) return r;
    r.base = base;
    r.date = date;
    r.src = src;
    return r;
}

double perMain(const Rates &r, const QString &main, const QString &cur)
{
    if (cur == main) return 1;
    const double a = cur == r.base ? 1 : r.table.value(cur, -1), b = main == r.base ? 1 : r.table.value(main, -1);
    return a > 0 && b > 0 ? b / a : -1;
}

int mergeRates(Rates &into, const Rates &from)
{
    if (into.base != from.base) return 0;
    int added = 0;
    for (auto it = from.table.begin(); it != from.table.end(); ++it)
        if (!into.table.contains(it.key())) { into.table.insert(it.key(), it.value()); ++added; }
    if (added) into.src = QStringLiteral("frankfurter + er-api");   // 只有这一种补法；设置页"来源"显示它
    return added;
}

QStringList usedCurrencies(const Value &root)
{
    QStringList out;
    const auto add = [&out](const QString &c) { if (!c.isEmpty() && !out.contains(c)) out << c; };
    for (const Value &plan : root[QStringLiteral("plans")].asArray()) {
        if (!plan.isObject()) continue;
        if (num(plan, "budget") > 0) add(planCurrency(plan));
        for (const SpendRow &r : spendRows(plan))
            if (r.amount != 0) add(r.cur);
    }
    return out;
}

QStringList currencyCodes()
{
    return { QStringLiteral("CNY"), QStringLiteral("AUD"), QStringLiteral("USD"), QStringLiteral("EUR"), QStringLiteral("JPY"), QStringLiteral("HKD"),
             QStringLiteral("GBP"), QStringLiteral("NZD"), QStringLiteral("KRW"), QStringLiteral("SGD"), QStringLiteral("THB"), QStringLiteral("MYR"),
             QStringLiteral("IDR"), QStringLiteral("VND"), QStringLiteral("CHF"), QStringLiteral("TWD"), QStringLiteral("RUB"), QStringLiteral("TRY"),
             QStringLiteral("AED"), QStringLiteral("EGP"), QStringLiteral("ISK"), QStringLiteral("NOK"), QStringLiteral("SEK"), QStringLiteral("CAD") };
}

QString planCurrency(const Value &plan)
{
    const QString c = str(plan, "currency");
    return c.isEmpty() ? QStringLiteral("AUD") : c;
}

// ---------------- 花了的钱 ----------------

QVector<SpendRow> spendRows(const Value &plan)
{
    const QString planCur = planCurrency(plan);
    QSet<QString> bookingIds, recorded;
    eachBooking(plan, [&](const Value &b) { bookingIds.insert(str(b, "id")); });
    QVector<SpendRow> rows;
    for (const Value &x : plan[kExpenses].asArray()) {
        if (!x.isObject()) continue;
        SpendRow r;
        r.id = str(x, "id");
        r.bookingId = str(x, "bookingId");
        if (!r.bookingId.isEmpty()) recorded.insert(r.bookingId);
        r.linked = bookingIds.contains(r.bookingId);
        r.date = str(x, "date");
        r.cat = catOf(str(x, "cat"));
        r.title = str(x, "title");
        r.notes = str(x, "notes");
        r.cur = str(x, "currency").isEmpty() ? planCur : str(x, "currency");
        r.amount = num(x, "amount");
        rows.push_back(r);
    }
    eachBooking(plan, [&](const Value &b) {
        const double cost = num(b, "cost");
        if (cost == 0 || recorded.contains(str(b, "id"))) return;   // 已经记到消费的不算第二遍
        SpendRow r;
        r.bookingId = str(b, "id");
        r.fromBooking = true;
        r.kind = str(b, "kind");
        r.date = str(b, "start").left(10);
        r.cat = catOfKind(r.kind);
        r.title = str(b, "title");
        r.cur = planCur;
        r.amount = cost;
        rows.push_back(r);
    });
    std::stable_sort(rows.begin(), rows.end(), [](const SpendRow &a, const SpendRow &b) { return a.date > b.date; });
    return rows;
}

QVariantMap spendInfo(const Value &plan, const QString &main, const Rates &rates)
{
    const QString cur = planCurrency(plan);
    const double kPlan = perMain(rates, main, cur);
    QStringList missing, mainMissing;
    const auto note = [](QStringList &l, const QString &c) { if (!l.contains(c)) l << c; };
    double total = 0, bookTotal = 0, expTotal = 0, mainTotal = 0;
    QHash<QString, double> byCat;
    QVariantList rowsOut;
    const QVector<Person> ps = peopleOf(plan);
    QHash<QString, const Value *> records;
    for (const Value &x : plan[kExpenses].asArray()) if (!str(x, "id").isEmpty()) records.insert(str(x, "id"), &x);
    for (const SpendRow &r : spendRows(plan)) {
        const double k = perMain(rates, main, r.cur);
        double inPlan = 0;
        if (r.cur == cur) inPlan = r.amount;
        else if (k > 0 && kPlan > 0) inPlan = r.amount * k / kPlan;
        else note(missing, r.cur);
        if (k > 0) mainTotal += r.amount * k;
        else note(mainMissing, r.cur);
        total += inPlan;
        (r.fromBooking || r.linked ? bookTotal : expTotal) += inPlan;
        byCat[r.cat] += inPlan;
        QVariantMap row{ { QStringLiteral("id"), r.id }, { QStringLiteral("bookingId"), r.bookingId }, { QStringLiteral("fromBooking"), r.fromBooking },
                         { QStringLiteral("linked"), r.linked }, { QStringLiteral("kind"), r.kind }, { QStringLiteral("date"), r.date }, { QStringLiteral("cat"), r.cat },
                         { QStringLiteral("title"), r.title }, { QStringLiteral("cur"), r.cur }, { QStringLiteral("notes"), r.notes },
                         { QStringLiteral("amount"), r.amount }, { QStringLiteral("inPlan"), inPlan }, { QStringLiteral("foreign"), r.cur != cur } };
        if (const Value *x = records.value(r.id); x && !ps.isEmpty()) {
            const QString payer = payerOf(ps, *x);
            const auto nameOf = [&](const QString &id) { for (const Person &p : ps) if (p.id == id) return p.name; return QString(); };
            const QStringList among = amongOf(ps, *x);
            const QVector<const Value *> list = splitOf(ps, *x);
            const bool custom = std::any_of(list.begin(), list.end(), [](const Value *s) { return hasAmount(*s); });
            QVariantMap amounts;   // 只有写了金额的人
            for (const Value *s : list) if (hasAmount(*s)) amounts.insert(str(*s, "who"), num(*s, "amount"));
            row.insert(QStringLiteral("payer"), payer);
            row.insert(QStringLiteral("payerName"), nameOf(payer));
            row.insert(QStringLiteral("among"), among);
            row.insert(QStringLiteral("onlyName"), among.size() == 1 ? nameOf(among.first()) : QString());
            row.insert(QStringLiteral("custom"), custom);
            row.insert(QStringLiteral("amounts"), amounts);
        }
        rowsOut << row;
    }
    const QVariantList cats = catsOut(byCat);
    const double budget = std::max(0.0, num(plan, "budget"));
    const int days = tripDays(plan);
    QVariantMap out{ { QStringLiteral("currency"), cur }, { QStringLiteral("total"), total }, { QStringLiteral("bookTotal"), bookTotal }, { QStringLiteral("expTotal"), expTotal },
             { QStringLiteral("missing"), missing }, { QStringLiteral("mainTotal"), mainTotal }, { QStringLiteral("mainMissing"), mainMissing },
             { QStringLiteral("cats"), cats }, { QStringLiteral("rows"), rowsOut }, { QStringLiteral("budget"), budget }, { QStringLiteral("remain"), budget - total },
             { QStringLiteral("over"), budget > 0 && total > budget }, { QStringLiteral("pct"), budget > 0 ? std::clamp(total / budget, 0.0, 1.0) : 0.0 },
             { QStringLiteral("days"), days }, { QStringLiteral("daily"), days > 0 ? total / days : 0.0 } };
    out.insert(settleInfo(plan, main, rates));
    return out;
}

QVariantMap settleInfo(const Value &plan, const QString &main, const Rates &rates)
{
    const QVector<Person> ps = peopleOf(plan);
    const QString planCur = planCurrency(plan);
    QHash<QString, double> paid, share;
    QStringList missing;
    QVector<QPair<QString, double>> used;
    int count = 0;
    int shareCount = 0;
    for (const Value &x : plan[kExpenses].asArray()) {
        if (!x.isObject()) continue;
        ++count;
        if (x[kSplit].asArray().empty()) ++shareCount;
        if (ps.isEmpty()) continue;
        const QString cur = str(x, "currency").isEmpty() ? planCur : str(x, "currency");
        const double k = perMain(rates, main, cur);
        if (k < 0) { if (!missing.contains(cur)) missing << cur; continue; }
        if (cur != main && std::none_of(used.begin(), used.end(), [&](const auto &u) { return u.first == cur; })) used.push_back({ cur, k });
        paid[payerOf(ps, x)] += num(x, "amount") * k;
        const QHash<QString, double> sh = shares(ps, x);
        for (auto it = sh.begin(); it != sh.end(); ++it) share[it.key()] += it.value() * k;
    }
    QVariantList people, rows;
    struct Bal { QString name; double v; };
    QVector<Bal> cred, debt;
    for (const Person &p : ps) {
        const QVariantList uses = usesOf(ps, plan, p.id);
        people << QVariantMap{ { QStringLiteral("id"), p.id }, { QStringLiteral("name"), p.name }, { QStringLiteral("uses"), uses }, { QStringLiteral("used"), !uses.isEmpty() } };
        const double a = round2(paid.value(p.id)), b = round2(share.value(p.id)), net = round2(paid.value(p.id) - share.value(p.id));
        rows << QVariantMap{ { QStringLiteral("id"), p.id }, { QStringLiteral("name"), p.name }, { QStringLiteral("paid"), a }, { QStringLiteral("share"), b }, { QStringLiteral("net"), net } };
        if (net > 0.005) cred.push_back({ p.name, net });
        else if (net < -0.005) debt.push_back({ p.name, -net });
    }
    // 最少转账：每次让欠得最多的人还给被欠得最多的人
    QVariantList transfers;
    const auto byV = [](const Bal &a, const Bal &b) { return a.v > b.v; };
    for (int guard = 0; !cred.isEmpty() && !debt.isEmpty() && guard < 200; ++guard) {
        std::stable_sort(cred.begin(), cred.end(), byV);
        std::stable_sort(debt.begin(), debt.end(), byV);
        const double v = std::min(cred.first().v, debt.first().v);
        if (v > 0.005) transfers << QVariantMap{ { QStringLiteral("from"), debt.first().name }, { QStringLiteral("to"), cred.first().name }, { QStringLiteral("amount"), round2(v) } };
        cred.first().v -= v;
        debt.first().v -= v;
        if (cred.first().v <= 0.005) cred.removeFirst();
        if (debt.first().v <= 0.005) debt.removeFirst();
    }
    QVariantList usedOut;
    for (const auto &u : std::as_const(used)) usedOut << QVariantMap{ { QStringLiteral("cur"), u.first }, { QStringLiteral("per"), u.second } };
    double bookingOnly = 0;
    int bookingCount = 0;
    for (const SpendRow &r : spendRows(plan)) if (r.fromBooking) { bookingOnly += std::abs(r.amount); ++bookingCount; }
    return { { QStringLiteral("people"), people }, { QStringLiteral("multi"), ps.size() >= 2 }, { QStringLiteral("expenseCount"), count }, { QStringLiteral("bookingOnly"), bookingOnly }, { QStringLiteral("shareCount"), shareCount }, { QStringLiteral("bookingCount"), bookingCount },
             { QStringLiteral("settle"), QVariantMap{ { QStringLiteral("cur"), main }, { QStringLiteral("rows"), rows }, { QStringLiteral("transfers"), transfers },
                                                     { QStringLiteral("missing"), missing }, { QStringLiteral("rates"), usedOut }, { QStringLiteral("date"), rates.date } } } };
}

// ---------------- 账本 ----------------

QVariantMap ledgerInfo(const Value &root, const QString &main, const Rates &rates, const QString &today)
{
    QVector<QVariantMap> trips;
    QHash<QString, double> byCat, byYear;
    QStringList missing;
    double total = 0, future = 0, datedSpent = 0;
    int emptyCount = 0, datedDays = 0, spentCount = 0, futureCount = 0;
    for (const Value &plan : root[QStringLiteral("plans")].asArray()) {
        if (!plan.isObject()) continue;
        const QString start = str(plan, "startDate"), end = str(plan, "endDate");
        // 0.10.5（方案页第 5 题 A）：已完成或已开始（到了开始日期的计划中）的算"已花"，其余是"还没去的预计"：只有一个合计
        const QString status = statusOf(plan);
        const bool started = status == QLatin1String("planning") && okDate(start) && start <= today;
        const bool done = status == QLatin1String("completed") || started;
        double spent = 0;
        bool any = false;
        QHash<QString, double> planCat, planYear;
        for (const SpendRow &r : spendRows(plan)) {
            if (r.amount == 0) continue;
            any = true;
            const double k = perMain(rates, main, r.cur);
            if (k < 0) { if (!missing.contains(r.cur)) missing << r.cur; continue; }
            const double v = r.amount * k;
            spent += v;
            planCat[r.cat] += v;
            const QString day = okDate(r.date) ? r.date : okDate(start) ? start : end;
            planYear[okDate(day) ? day.left(4) : QString()] += v;
        }
        if (!any) { ++emptyCount; continue; }
        const int days = tripDays(plan);
        if (done) {
            total += spent;
            ++spentCount;
            if (days > 0) { datedSpent += spent; datedDays += days; }
            for (auto it = planCat.cbegin(); it != planCat.cend(); ++it) byCat[it.key()] += it.value();
            for (auto it = planYear.cbegin(); it != planYear.cend(); ++it) byYear[it.key()] += it.value();
        } else {
            future += spent;
            ++futureCount;
        }
        const QString cur = planCurrency(plan);
        const double budget = std::max(0.0, num(plan, "budget")), kb = perMain(rates, main, cur);
        trips.push_back({ { QStringLiteral("id"), str(plan, "id") }, { QStringLiteral("name"), str(plan, "name") }, { QStringLiteral("status"), status },
                          { QStringLiteral("state"), started ? QStringLiteral("live") : status }, { QStringLiteral("done"), done },
                          { QStringLiteral("start"), start }, { QStringLiteral("end"), end }, { QStringLiteral("days"), days }, { QStringLiteral("spent"), spent },
                          { QStringLiteral("budget"), budget }, { QStringLiteral("budgetCur"), cur }, { QStringLiteral("budgetMain"), kb > 0 ? budget * kb : -1.0 } });
    }
    std::stable_sort(trips.begin(), trips.end(), [](const QVariantMap &a, const QVariantMap &b) { return a.value(QStringLiteral("spent")).toDouble() > b.value(QStringLiteral("spent")).toDouble(); });
    QVariantList tripsOut;
    for (const QVariantMap &t : std::as_const(trips)) tripsOut << t;
    QStringList yearKeys = byYear.keys();
    std::sort(yearKeys.begin(), yearKeys.end(), std::greater<QString>());   // 没写日期（空）排到最后
    double top = 0;
    for (double v : std::as_const(byYear)) top = std::max(top, v);
    QVariantList years;
    for (const QString &y : std::as_const(yearKeys)) {
        const double v = byYear.value(y);
        years << QVariantMap{ { QStringLiteral("y"), y }, { QStringLiteral("v"), v }, { QStringLiteral("share"), top > 0 ? std::max(0.0, v / top) : 0.0 } };
    }
    return { { QStringLiteral("total"), total }, { QStringLiteral("future"), future }, { QStringLiteral("spentCount"), spentCount }, { QStringLiteral("futureCount"), futureCount },
             { QStringLiteral("trips"), tripsOut }, { QStringLiteral("emptyCount"), emptyCount },
             { QStringLiteral("perTrip"), spentCount == 0 ? 0.0 : total / spentCount }, { QStringLiteral("perDay"), datedDays > 0 ? datedSpent / datedDays : 0.0 },
             { QStringLiteral("cats"), catsOut(byCat) }, { QStringLiteral("years"), years }, { QStringLiteral("missing"), missing } };
}

// ---------------- 交通住宿 ----------------

QVariantList bookingDays(const Value &plan)
{
    QSet<QString> recorded;
    for (const Value &x : plan[kExpenses].asArray()) if (!str(x, "bookingId").isEmpty()) recorded.insert(str(x, "bookingId"));
    QVector<const Value *> list;
    for (const Value &b : plan[kBookings].asArray()) if (b.isObject()) list.push_back(&b);
    std::stable_sort(list.begin(), list.end(), [](const Value *a, const Value *b) {
        const QString x = str(*a, "start"), y = str(*b, "start");
        return (x.isEmpty() ? QStringLiteral("9999") : x) < (y.isEmpty() ? QStringLiteral("9999") : y);
    });
    QVariantList out, rows;
    QString cur;
    const auto flush = [&] { if (!rows.isEmpty()) out << QVariantMap{ { QStringLiteral("day"), cur }, { QStringLiteral("rows"), rows } }; rows.clear(); };
    for (const Value *b : list) {
        const QString day = str(*b, "start").left(10);
        if (day != cur) { flush(); cur = day; }
        rows << QVariantMap{ { QStringLiteral("id"), str(*b, "id") }, { QStringLiteral("kind"), str(*b, "kind") }, { QStringLiteral("title"), str(*b, "title") },
                             { QStringLiteral("from"), str(*b, "from") }, { QStringLiteral("to"), str(*b, "to") }, { QStringLiteral("start"), str(*b, "start") },
                             { QStringLiteral("end"), str(*b, "end") }, { QStringLiteral("ref"), str(*b, "ref") }, { QStringLiteral("cost"), num(*b, "cost") },
                             { QStringLiteral("notes"), str(*b, "notes") }, { QStringLiteral("linked"), recorded.contains(str(*b, "id")) } };
    }
    flush();
    return out;
}

double bookingsCost(const Value &plan)
{
    double sum = 0;
    eachBooking(plan, [&](const Value &b) { sum += num(b, "cost"); });
    return sum;
}

QVector<QVariantList> stopBookings(const Value &plan)
{
    const QVector<int> st = validStops(plan);
    QVector<QVariantList> out(st.size());
    const json::Array &stops = plan[QStringLiteral("stops")].asArray();
    const bool single = st.size() == 1;
    struct Range { QString a, b; };
    QVector<Range> ranges;
    for (int i : st) {
        QString a = str(stops[i], "arriveDate"), b = str(stops[i], "departDate");
        if (single && !okDate(a)) a = str(plan, "startDate");     // 单一目的地常常只写了计划的日期
        if (single && !okDate(b)) b = str(plan, "endDate");
        ranges.push_back({ a, okDate(b) ? b : a });
    }
    eachBooking(plan, [&](const Value &b) {
        const QString d = str(b, "start").left(10);
        if (!okDate(d)) return;
        const bool hotel = str(b, "kind") == QLatin1String("hotel");
        int hit = -1;
        for (int k = 0; k < ranges.size(); ++k) {
            if (!okDate(ranges[k].a) || d < ranges[k].a || d > ranges[k].b) continue;
            hit = k;
            if (!hotel) break;   // 交通挂第一个对得上的站，住宿挂最后一个
        }
        if (hit >= 0)
            out[hit] << QVariantMap{ { QStringLiteral("id"), str(b, "id") }, { QStringLiteral("kind"), str(b, "kind") }, { QStringLiteral("title"), str(b, "title") } };
    });
    return out;
}

QStringList missingNights(const Value &plan, const QString &today, int *undated)
{
    if (undated) *undated = 0;
    if (statusOf(plan) != QLatin1String("planning")) return {};
    const auto addSpan = [](QStringList &l, const QString &a, const QString &b) {
        const QDate end = QDate::fromString(b, Qt::ISODate);
        for (QDate d = QDate::fromString(a, Qt::ISODate); d.isValid() && d < end; d = d.addDays(1)) l << d.toString(Qt::ISODate);
    };
    QStringList need;
    const json::Array &stops = plan[QStringLiteral("stops")].asArray();
    for (int i : validStops(plan)) {
        const QString a = str(stops[i], "arriveDate"), b = str(stops[i], "departDate");
        if (okDate(a) && okDate(b)) addSpan(need, a, b);
    }
    if (need.isEmpty() && dated(plan)) addSpan(need, str(plan, "startDate"), str(plan, "endDate"));
    QSet<QString> covered;
    eachBooking(plan, [&](const Value &b) {
        const QString kind = str(b, "kind"), s = str(b, "start").left(10), e = str(b, "end").left(10);
        if (kind == QLatin1String("hotel")) {
            if (!okDate(s)) { if (undated) ++*undated; return; }
            covered.insert(s);
        } else if (kind == QLatin1String("car") || kind == QLatin1String("ticket") || kind == QLatin1String("other") || !okDate(s)) {
            return;
        }
        if (okDate(e)) {   // 过夜的交通也算住了
            QStringList span;
            addSpan(span, s, e);
            for (const QString &d : std::as_const(span)) covered.insert(d);
        }
    });
    QStringList out;
    for (const QString &d : std::as_const(need))
        if (d >= today && !covered.contains(d) && !out.contains(d)) out << d;
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------- 修改 ----------------

QString putBooking(Value &plan, const QString &id, const QVariantMap &f)
{
    const auto text = [&](const char *k) { return f.value(QString::fromLatin1(k)).toString().trimmed(); };
    static const QStringList kKinds = { QStringLiteral("flight"), QStringLiteral("train"), QStringLiteral("bus"), QStringLiteral("ferry"), QStringLiteral("car"),
                                        QStringLiteral("hotel"), QStringLiteral("ticket"), QStringLiteral("other") };
    const QString kind = kKinds.contains(text("kind")) ? text("kind") : QStringLiteral("other");
    bool ok = true;
    const QString start = joinDateTime(text("startDate"), text("startTime"), &ok);
    if (!ok) return QStringLiteral("badStart");
    const QString end = joinDateTime(text("endDate"), text("endTime"), &ok);
    if (!ok) return QStringLiteral("badEnd");
    // 有一边只写了日期时只比日期（当天退房没写时间不算早于入住）
    const qsizetype cmp = std::min(start.size(), end.size());
    if (cmp > 0 && end.left(cmp) < start.left(cmp)) return QStringLiteral("endBeforeStart");
    double cost = 0;
    if (!text("cost").isEmpty()) {
        cost = parseNum(text("cost"), &ok);
        if (!ok) return QStringLiteral("badCost");
    }

    json::Array &list = mutList(plan, kBookings);
    Value *b = nullptr;
    if (!id.isEmpty()) {
        for (Value &x : list) if (str(x, "id") == id) b = &x;
        if (!b) return QStringLiteral("gone");
    }
    QString useId = id;
    if (!b) {
        useId = newId("bk");
        json::Object o;
        o.set(QStringLiteral("id"), Value(useId));
        list.push_back(Value(std::move(o)));
        b = &list.back();
    }
    json::Object &o = b->mutObject();
    o.set(QStringLiteral("kind"), Value(kind));
    setOrRemove(o, "title", text("title"));
    const bool hotel = kind == QLatin1String("hotel");
    setOrRemove(o, "from", hotel ? QString() : text("from"));
    setOrRemove(o, "to", hotel ? QString() : text("to"));
    setOrRemove(o, "start", start);
    setOrRemove(o, "end", end);
    setOrRemove(o, "ref", text("ref"));
    setOrRemove(o, "notes", text("notes"));
    if (cost != 0) o.set(QStringLiteral("cost"), Value(cost));
    else o.remove(QStringLiteral("cost"));

    bool recorded = false;
    if (!id.isEmpty() && plan[kExpenses].isArray()) {
        for (Value &x : mutList(plan, kExpenses))
            if (str(x, "bookingId") == id) {
                recorded = true;
                json::Object &e = x.mutObject();
                e.set(QStringLiteral("amount"), Value(cost));
                if (!text("title").isEmpty()) e.set(QStringLiteral("title"), Value(text("title")));
            }
    }
    // Electron "同时记到消费"；两个人以上不用勾，总是记（费用才进结算）
    if (!recorded && cost != 0 && (f.value(QStringLiteral("toExp")).toBool() || peopleOf(plan).size() >= 2)) {
        const Value booking = *b;
        recordBooking(plan, booking);
    }
    return {};
}

int removeBooking(Value &plan, const QString &id)
{
    if (id.isEmpty() || !plan[kBookings].isArray()) return -1;
    json::Array &list = mutList(plan, kBookings);
    const auto it = std::find_if(list.begin(), list.end(), [&](const Value &x) { return str(x, "id") == id; });
    if (it == list.end()) return -1;
    list.erase(it);
    if (!plan[kExpenses].isArray()) return 0;
    json::Array &ex = mutList(plan, kExpenses);
    const auto cut = std::remove_if(ex.begin(), ex.end(), [&](const Value &x) { return str(x, "bookingId") == id; });
    const int n = int(ex.end() - cut);
    ex.erase(cut, ex.end());
    return n;
}

QString putExpense(Value &plan, const QString &id, const QVariantMap &f)
{
    const auto text = [&](const char *k) { return f.value(QString::fromLatin1(k)).toString().trimmed(); };
    bool ok = false;
    const double amount = parseNum(text("amount"), &ok);
    if (!ok) return QStringLiteral("badAmount");
    if (!text("date").isEmpty() && !okDate(text("date"))) return QStringLiteral("badDate");

    // 分账字段先算好、先查：没填金额的人不写 amount 键（平分剩下的）；固定的加起来不能超过这一笔，全都固定时要正好等于
    const bool hasPaid = f.contains(kPaidBy);
    json::Array split;
    double fixedSum = 0;
    bool anyFixed = false, allFixed = true;
    for (const QVariant &v : f.value(kSplit).toList()) {
        const QVariantMap m = v.toMap();
        json::Object o;
        o.set(QStringLiteral("who"), Value(m.value(QStringLiteral("who")).toString()));
        const QString t = m.value(QStringLiteral("amount")).toString();
        if (t.trimmed().isEmpty()) {
            allFixed = false;
        } else {
            bool amtOk = true;
            const double a = parseNum(t, &amtOk);
            if (!amtOk) return QStringLiteral("badSplit");
            o.set(QStringLiteral("amount"), Value(a));
            fixedSum += a;
            anyFixed = true;
        }
        split.push_back(Value(std::move(o)));
    }
    if (anyFixed && (allFixed ? std::abs(fixedSum - amount) > 0.005 : std::abs(fixedSum) > std::abs(amount) + 0.005)) return QStringLiteral("badSplit");
    const QVector<Person> ps = peopleOf(plan);

    json::Array &list = mutList(plan, kExpenses);
    Value *x = nullptr;
    if (!id.isEmpty()) {
        for (Value &e : list) if (str(e, "id") == id) x = &e;
        if (!x) return QStringLiteral("gone");
    } else {
        json::Object o;
        o.set(QStringLiteral("id"), Value(newId("ex")));
        list.push_back(Value(std::move(o)));
        x = &list.back();
    }
    if (str(plan, "currency").isEmpty()) plan.mutObject().set(QStringLiteral("currency"), Value(planCurrency(plan)));   // Electron editExpense
    const QString cur = text("currency");
    json::Object &o = x->mutObject();
    o.set(QStringLiteral("date"), Value(text("date")));
    o.set(QStringLiteral("cat"), Value(catOf(text("cat"))));
    o.set(QStringLiteral("title"), Value(text("title")));
    o.set(QStringLiteral("amount"), Value(amount));
    setOrRemove(o, "notes", text("notes"));
    // 和计划币种一样就不写（"不写 = 计划的币种"），已经写了的照写
    if (!cur.isEmpty() && (cur != planCurrency(plan) || o.contains(QStringLiteral("currency")))) o.set(QStringLiteral("currency"), Value(cur));
    if (hasPaid) {
        o.set(kPaidBy, Value(f.value(kPaidBy).toString()));
        if (split.empty()) o.remove(kSplit);
        else o.set(kSplit, Value(std::move(split)));
    } else if (id.isEmpty() && !ps.isEmpty()) {
        o.set(kPaidBy, Value(ps.first().id));   // 一行快记：我付、所有人平摊（Electron 新加一笔的默认）
    }
    return {};
}

bool removeExpense(Value &plan, const QString &id)
{
    if (id.isEmpty() || !plan[kExpenses].isArray()) return false;
    json::Array &list = mutList(plan, kExpenses);
    const auto it = std::find_if(list.begin(), list.end(), [&](const Value &x) { return str(x, "id") == id; });
    if (it == list.end()) return false;
    list.erase(it);
    return true;
}

QString addPerson(Value &plan, const QString &name0, const QString &meName)
{
    const QString name = name0.trimmed().left(20);
    if (name.isEmpty()) return QStringLiteral("emptyName");
    const QVector<Person> ps = peopleOf(plan);
    if (std::any_of(ps.begin(), ps.end(), [&](const Person &p) { return p.name == name; })) return QStringLiteral("dupName");
    const auto push = [&](const QString &n) {
        json::Object o;
        o.set(QStringLiteral("id"), Value(newId("pp")));
        o.set(QStringLiteral("name"), Value(n));
        mutList(plan, kPeople).push_back(Value(std::move(o)));
    };
    if (ps.isEmpty() && name != meName) push(meName.left(20));   // 第一次加别人：先放"我"，以前的账都算"我"付的
    push(name);
    shareAll(plan);
    return {};
}

QString editPerson(Value &plan, const QString &id, const QString &name, bool remove)
{
    const QVector<Person> ps = peopleOf(plan);
    if (!isPerson(ps, id)) return QStringLiteral("gone");
    if (remove) {
        if (!usesOf(ps, plan, id).isEmpty()) return QStringLiteral("used");
        json::Array &list = mutList(plan, kPeople);
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Value &p) { return str(p, "id") == id; }), list.end());
        if (list.empty()) plan.mutObject().remove(kPeople);
        return {};
    }
    const QString n = name.trimmed().left(20);
    if (n.isEmpty()) return {};
    for (Value &p : mutList(plan, kPeople))
        if (str(p, "id") == id) p.mutObject().set(QStringLiteral("name"), Value(n));
    return {};
}

QString setBudget(Value &plan, const QString &text, const QString &currency)
{
    bool ok = true;
    const double v = text.trimmed().isEmpty() ? 0 : parseNum(text, &ok);
    if (!ok || v < 0) return QStringLiteral("badBudget");
    json::Object &o = plan.mutObject();
    if (v > 0) o.set(QStringLiteral("budget"), Value(v));
    else o.remove(QStringLiteral("budget"));
    const QString cur = currency.trimmed().toUpper().left(6);
    o.set(QStringLiteral("currency"), Value(cur.isEmpty() ? planCurrency(plan) : cur));
    return {};
}

} // namespace tp::map::plans
