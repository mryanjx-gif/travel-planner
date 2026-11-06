#include "planmoney.h"

#include "planpage.h"
#include "plans.h"

#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
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

} // namespace

// ---------------- 汇率 ----------------

Rates parseRates(const QByteArray &body, const QString &src, const QString &base)
{
    Rates r;
    const QJsonObject j = QJsonDocument::fromJson(body).object();
    QString date;
    if (src == QLatin1String("frankfurter")) {
        date = j.value(QStringLiteral("date")).toString();
    } else {
        if (j.value(QStringLiteral("result")).toString() != QLatin1String("success")) return r;
        const qint64 t = qint64(j.value(QStringLiteral("time_last_update_unix")).toDouble());
        if (t > 0) date = QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC).date().toString(Qt::ISODate);
    }
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
        rowsOut << QVariantMap{ { QStringLiteral("id"), r.id }, { QStringLiteral("bookingId"), r.bookingId }, { QStringLiteral("fromBooking"), r.fromBooking },
                                { QStringLiteral("linked"), r.linked }, { QStringLiteral("kind"), r.kind }, { QStringLiteral("date"), r.date }, { QStringLiteral("cat"), r.cat },
                                { QStringLiteral("title"), r.title }, { QStringLiteral("cur"), r.cur }, { QStringLiteral("notes"), r.notes },
                                { QStringLiteral("amount"), r.amount }, { QStringLiteral("foreign"), r.cur != cur } };
    }
    double catSum = 0;
    for (double v : std::as_const(byCat)) if (v > 0) catSum += v;
    QVariantList cats;
    for (const char *k : { "transport", "stay", "food", "ticket", "shop", "other" }) {
        const double v = byCat.value(QString::fromLatin1(k));
        if (v > 0) cats << QVariantMap{ { QStringLiteral("k"), QString::fromLatin1(k) }, { QStringLiteral("v"), v }, { QStringLiteral("pct"), qRound(v / catSum * 100) }, { QStringLiteral("share"), v / catSum } };
    }
    const double budget = std::max(0.0, num(plan, "budget"));
    int days = 0;
    if (dated(plan)) days = int(QDate::fromString(str(plan, "startDate"), Qt::ISODate).daysTo(QDate::fromString(str(plan, "endDate"), Qt::ISODate))) + 1;
    return { { QStringLiteral("currency"), cur }, { QStringLiteral("total"), total }, { QStringLiteral("bookTotal"), bookTotal }, { QStringLiteral("expTotal"), expTotal },
             { QStringLiteral("missing"), missing }, { QStringLiteral("mainTotal"), mainTotal }, { QStringLiteral("mainMissing"), mainMissing },
             { QStringLiteral("cats"), cats }, { QStringLiteral("rows"), rowsOut }, { QStringLiteral("budget"), budget }, { QStringLiteral("remain"), budget - total },
             { QStringLiteral("over"), budget > 0 && total > budget }, { QStringLiteral("pct"), budget > 0 ? std::clamp(total / budget, 0.0, 1.0) : 0.0 },
             { QStringLiteral("days"), days }, { QStringLiteral("daily"), days > 0 ? total / days : 0.0 } };
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

    if (id.isEmpty()) {
        if (f.value(QStringLiteral("toExp")).toBool() && cost != 0) {   // Electron "同时记到消费"
            json::Object e;
            e.set(QStringLiteral("id"), Value(newId("ex")));
            e.set(QStringLiteral("date"), Value(start.left(10)));
            e.set(QStringLiteral("cat"), Value(catOfKind(kind)));
            e.set(QStringLiteral("title"), Value(text("title")));
            e.set(QStringLiteral("amount"), Value(cost));
            e.set(QStringLiteral("bookingId"), Value(useId));
            mutList(plan, kExpenses).push_back(Value(std::move(e)));
        }
    } else if (plan[kExpenses].isArray()) {
        for (Value &x : mutList(plan, kExpenses))
            if (str(x, "bookingId") == id) {
                json::Object &e = x.mutObject();
                e.set(QStringLiteral("amount"), Value(cost));
                if (!text("title").isEmpty()) e.set(QStringLiteral("title"), Value(text("title")));
            }
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
