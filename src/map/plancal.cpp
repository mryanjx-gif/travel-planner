#include "plancal.h"

#include "plans.h"

#include <QDate>
#include <QVector>

#include <algorithm>

namespace tp::map::plans {

using json::Value;

namespace {
QString str(const Value &o, const char *key) { return o[QString::fromLatin1(key)].toString(); }
QDate day(const QString &s) { return QDate::fromString(s, Qt::ISODate); }
QString iso(const QDate &d) { return d.toString(Qt::ISODate); }

// 站点写了日期的范围（stopRange）
bool stopRange(const Value &plan, QString *lo, QString *hi)
{
    lo->clear(); hi->clear();
    for (const Value &s : plan[QStringLiteral("stops")].asArray())
        for (const char *k : { "arriveDate", "departDate" }) {
            const QString d = str(s, k);
            if (!okDate(d)) continue;
            if (lo->isEmpty() || d < *lo) *lo = d;
            if (hi->isEmpty() || d > *hi) *hi = d;
        }
    return !lo->isEmpty();
}

struct Span { const Value *p; QDate a, b; };

// 定了日期、没隐藏的（dated），早的在前，同一天开始的长的在前
QVector<Span> datedSpans(const Value &root, const QSet<QString> &hidden)
{
    QVector<Span> out;
    for (const Value &p : root[QStringLiteral("plans")].asArray()) {
        if (hidden.contains(str(p, "id"))) continue;
        const QString s = str(p, "startDate"), e = str(p, "endDate");
        const QDate a = day(s.isEmpty() ? e : s), b = day(e.isEmpty() ? s : e);
        if (!a.isValid() || !b.isValid()) continue;
        out.push_back({ &p, std::min(a, b), std::max(a, b) });
    }
    std::stable_sort(out.begin(), out.end(), [](const Span &x, const Span &y) { return x.a != y.a ? x.a < y.a : x.b > y.b; });
    return out;
}

QVariantMap barBase(const Span &it)
{
    return { { QStringLiteral("id"), str(*it.p, "id") }, { QStringLiteral("name"), str(*it.p, "name") }, { QStringLiteral("color"), planColorOf(*it.p) },
             { QStringLiteral("done"), statusOf(*it.p) == QLatin1String("completed") }, { QStringLiteral("start"), iso(it.a) }, { QStringLiteral("end"), iso(it.b) } };
}

// 分行：同一行里的区间不重叠（lanes）。区间按 [x0, x1) 算，返回每项在第几行
template <class T>
QVector<int> layLanes(const QVector<QPair<T, T>> &ranges, int *count)
{
    QVector<QVector<QPair<T, T>>> rows;
    QVector<int> out;
    for (const auto &r : ranges) {
        int k = 0;
        for (; k < rows.size(); ++k) {
            bool free = true;
            for (const auto &o : rows[k]) free = free && (o.second <= r.first || o.first >= r.second);
            if (free) break;
        }
        if (k == rows.size()) rows.push_back({});
        rows[k].push_back(r);
        out << k;
    }
    *count = int(rows.size());
    return out;
}
} // namespace

int planDays(const Value &plan)
{
    const QString s = str(plan, "startDate"), e = str(plan, "endDate");
    const QString a = s.isEmpty() ? e : s, b = e.isEmpty() ? s : e;
    if (okDate(a) && okDate(b)) return std::max(1, days(a, b) + 1);
    QString lo, hi;
    if (stopRange(plan, &lo, &hi)) return days(lo, hi) + 1;
    const Value &d = plan[QStringLiteral("days")];
    const int n = d.isNumber() ? qRound(d.toNumber()) : qRound(d.toString().toDouble());
    return n >= 1 && n <= 999 ? n : 1;
}

int datedBookings(const Value &plan)
{
    int n = 0;
    for (const Value &b : plan[QStringLiteral("bookings")].asArray())
        if (okDate(str(b, "start").left(10))) ++n;
    return n;
}

void shiftStops(Value &plan, int n)
{
    if (n == 0 || !plan[QStringLiteral("stops")].isArray()) return;
    for (Value &s : plan.mutObject().find(QStringLiteral("stops"))->mutArray()) {
        if (!s.isObject()) continue;
        json::Object &so = s.mutObject();
        for (const char *k : { "arriveDate", "departDate" }) {
            const QString d = str(s, k);
            if (okDate(d)) so.set(QString::fromLatin1(k), Value(addDays(d, n)));
        }
        if (Value *sc = so.find(QStringLiteral("schedule")); sc && sc->isArray())
            for (Value &it : sc->mutArray()) {
                if (!it.isObject()) continue;
                const QString d = str(it, "date");
                if (okDate(d)) it.mutObject().set(QStringLiteral("date"), Value(addDays(d, n)));
            }
    }
}

bool placePlan(Value &plan, const QString &start, int n)
{
    if (!okDate(start) || !plan.isObject()) return false;
    const QString s = str(plan, "startDate"), e = str(plan, "endDate");
    QString ref = okDate(s) ? s : okDate(e) ? e : QString(), hi;
    if (ref.isEmpty()) stopRange(plan, &ref, &hi);
    if (!ref.isEmpty() && ref != start) shiftStops(plan, days(ref, start));
    plan.mutObject().set(QStringLiteral("startDate"), Value(start));
    plan.mutObject().set(QStringLiteral("endDate"), Value(addDays(start, std::max(1, n) - 1)));
    return true;
}

void unplacePlan(Value &plan)
{
    if (!plan.isObject()) return;
    const int n = planDays(plan);
    json::Object &o = plan.mutObject();
    if (n >= 1 && n <= 999) o.set(QStringLiteral("days"), Value(double(n)));
    o.set(QStringLiteral("startDate"), Value(QString()));
    o.set(QStringLiteral("endDate"), Value(QString()));
    Value *stops = o.find(QStringLiteral("stops"));
    if (!stops || !stops->isArray()) return;
    for (Value &s : stops->mutArray()) {
        if (!s.isObject()) continue;
        json::Object &so = s.mutObject();
        so.set(QStringLiteral("arriveDate"), Value(QString()));
        so.set(QStringLiteral("departDate"), Value(QString()));
        if (Value *sc = so.find(QStringLiteral("schedule")); sc && sc->isArray())
            for (Value &it : sc->mutArray())
                if (it.isObject()) it.mutObject().remove(QStringLiteral("date"));
    }
}

int overlapCount(const Value &root, const QString &planId, const QString &start, const QString &end, const QSet<QString> &hidden)
{
    const QDate a = day(start), b = day(end);
    if (!a.isValid() || !b.isValid()) return 0;
    int n = 0;
    for (const Span &it : datedSpans(root, hidden))
        if (str(*it.p, "id") != planId && it.a <= b && it.b >= a) ++n;
    return n;
}

QVariantMap calYear(const Value &root, const QSet<QString> &hidden, int y, int m, const QString &today)
{
    constexpr double kNarrow = 0.12, kLabel = 0.17;   // 横条窄于 12% 名字写旁边；名字约占 17%（一起排行，不互相压）
    const QDate start(y, m, 1), end = start.addMonths(12);
    const double span = double(start.daysTo(end));
    QVariantList months, bars;
    const QDate now = day(today);
    for (int i = 0; i < 12; ++i) {
        const QDate d = start.addMonths(i);
        months << QVariantMap{ { QStringLiteral("y"), d.year() }, { QStringLiteral("m"), d.month() },
                               { QStringLiteral("cur"), now.isValid() && d.year() == now.year() && d.month() == now.month() },
                               { QStringLiteral("l"), start.daysTo(d) / span } };
    }
    QVector<QPair<double, double>> ranges;
    QVector<QVariantMap> maps;
    for (const Span &it : datedSpans(root, hidden)) {
        if (it.b < start || it.a >= end) continue;
        const double l = std::clamp(start.daysTo(it.a) / span, 0.0, 1.0), r = std::clamp(start.daysTo(it.b.addDays(1)) / span, 0.0, 1.0);
        const bool narrow = r - l < kNarrow, labelLeft = narrow && r > 0.75;
        QVariantMap b = barBase(it);
        b.insert(QStringLiteral("l"), l);
        b.insert(QStringLiteral("r"), r);
        b.insert(QStringLiteral("narrow"), narrow);
        b.insert(QStringLiteral("labelLeft"), labelLeft);
        maps << b;
        ranges.push_back({ labelLeft ? l - kLabel : l, narrow && !labelLeft ? r + kLabel : r });
    }
    int nLanes = 0;
    const QVector<int> lane = layLanes(ranges, &nLanes);
    for (int i = 0; i < maps.size(); ++i) { maps[i].insert(QStringLiteral("lane"), lane[i]); bars << maps[i]; }
    return { { QStringLiteral("start"), iso(start) }, { QStringLiteral("spanDays"), int(span) }, { QStringLiteral("lanes"), nLanes }, { QStringLiteral("months"), months }, { QStringLiteral("bars"), bars },
             { QStringLiteral("today"), now.isValid() && now >= start && now < end ? start.daysTo(now) / span : -1.0 } };
}

QVariantMap calMonth(const Value &root, const QSet<QString> &hidden, int y, int m, const QString &today)
{
    const QDate first(y, m, 1);
    const QDate startW = first.addDays(-(first.dayOfWeek() - 1));
    const QVector<Span> all = datedSpans(root, hidden);
    QVariantList weeks;
    for (int w = 0; w < 6; ++w) {
        const QDate ws = startW.addDays(7 * w);
        if (w >= 4 && ws.month() != m && ws > first) break;
        const QDate we = ws.addDays(6);
        QVariantList dayRows, bars;
        for (int i = 0; i < 7; ++i) {
            const QDate d = ws.addDays(i);
            dayRows << QVariantMap{ { QStringLiteral("date"), iso(d) }, { QStringLiteral("d"), d.day() }, { QStringLiteral("out"), d.month() != m }, { QStringLiteral("today"), iso(d) == today } };
        }
        QVector<QPair<int, int>> ranges;
        QVector<QVariantMap> maps;
        for (const Span &it : all) {
            if (it.b < ws || it.a > we) continue;
            const int a = int(std::max<qint64>(0, ws.daysTo(it.a))), b = int(std::min<qint64>(6, ws.daysTo(it.b)));
            QVariantMap bar = barBase(it);
            bar.insert(QStringLiteral("a"), a);
            bar.insert(QStringLiteral("b"), b);
            maps << bar;
            ranges.push_back({ a, b + 1 });
        }
        int nLanes = 0;
        const QVector<int> lane = layLanes(ranges, &nLanes);
        for (int i = 0; i < maps.size(); ++i) { maps[i].insert(QStringLiteral("lane"), lane[i]); bars << maps[i]; }
        weeks << QVariantMap{ { QStringLiteral("start"), iso(ws) }, { QStringLiteral("lanes"), nLanes }, { QStringLiteral("days"), dayRows }, { QStringLiteral("bars"), bars } };
    }
    return { { QStringLiteral("weeks"), weeks } };
}

QVariantMap calSide(const Value &root, const QSet<QString> &hidden)
{
    QVector<const Value *> undated;
    QVariantList hid;
    for (const Value &p : root[QStringLiteral("plans")].asArray()) {
        const QString id = str(p, "id");
        if (hidden.contains(id)) { hid << QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("name"), str(p, "name") }, { QStringLiteral("color"), planColorOf(p) } }; continue; }
        if (str(p, "startDate").isEmpty() && str(p, "endDate").isEmpty() && statusOf(p) != QLatin1String("completed")) undated << &p;
    }
    std::stable_sort(undated.begin(), undated.end(), [](const Value *a, const Value *b) { return compare(*a, *b, false) < 0; });
    QVariantList planning, wish;
    for (const Value *p : undated) {
        const QString status = statusOf(*p);
        const QVariantMap row{ { QStringLiteral("id"), str(*p, "id") }, { QStringLiteral("name"), str(*p, "name") }, { QStringLiteral("color"), planColorOf(*p) },
                               { QStringLiteral("status"), status }, { QStringLiteral("days"), planDays(*p) } };
        (status == QLatin1String("wish") ? wish : planning) << row;
    }
    return { { QStringLiteral("planning"), planning }, { QStringLiteral("wish"), wish }, { QStringLiteral("hidden"), hid } };
}

QVariantMap calPicker(const Value &root, const QSet<QString> &hidden)
{
    QVariantMap counts;
    QSet<int> years;
    for (const Span &it : datedSpans(root, hidden)) {
        const QString color = planColorOf(*it.p);
        for (QDate d(it.a.year(), it.a.month(), 1); d <= it.b; d = d.addMonths(1)) {
            years.insert(d.year());
            const QString key = d.toString(QStringLiteral("yyyy-MM"));
            QVariantList l = counts.value(key).toList();
            if (l.size() < 4) { l << color; counts.insert(key, l); }
        }
    }
    QList<int> sorted(years.begin(), years.end());
    std::sort(sorted.begin(), sorted.end());
    QVariantList ys;
    for (int v : sorted) ys << v;
    return { { QStringLiteral("years"), ys }, { QStringLiteral("counts"), counts } };
}

} // namespace tp::map::plans
