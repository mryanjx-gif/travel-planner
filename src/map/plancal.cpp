#include "plancal.h"

#include "planpage.h"
#include "plans.h"

#include <QDate>
#include <QHash>
#include <QRegularExpression>
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

// Qt 的 dayOfWeek 是周一 1 … 周日 7，% 7 以后和 JS 的 getDay 一样（周日 0）
QDate firstOfWeek(const QDate &d, int weekStart) { return d.addDays(-((d.dayOfWeek() % 7 - weekStart + 7) % 7)); }

// "09:30…" → 分钟；不是时间给 -1（toMin）
int toMin(const QString &t)
{
    static const QRegularExpression re(QStringLiteral("^(\\d{1,2}):(\\d{2})"));
    const QRegularExpressionMatch m = re.match(t);
    return m.hasMatch() ? m.captured(1).toInt() * 60 + m.captured(2).toInt() : -1;
}
QString fmtMin(int m) { return QStringLiteral("%1:%2").arg(m / 60 % 24, 2, 10, QLatin1Char('0')).arg(m % 60, 2, 10, QLatin1Char('0')); }

struct WeekEvent { QVariantMap m; int a, b, col = 0, n = 1; };

// 重叠的并排：一组互相有重叠的事件平分宽度（layoutDay）
void layoutDay(QVector<WeekEvent> &evs)
{
    std::stable_sort(evs.begin(), evs.end(), [](const WeekEvent &x, const WeekEvent &y) { return x.a != y.a ? x.a < y.a : x.b > y.b; });
    QVector<int> group, cols;
    int end = -1;
    const auto flush = [&] { for (int i : group) evs[i].n = int(cols.size()); group.clear(); cols.clear(); end = -1; };
    for (int i = 0; i < evs.size(); ++i) {
        if (!group.isEmpty() && evs[i].a >= end) flush();
        int c = 0;
        while (c < cols.size() && cols[c] > evs[i].a) ++c;
        if (c == cols.size()) cols << evs[i].b; else cols[c] = evs[i].b;
        evs[i].col = c;
        group << i;
        end = std::max(end, evs[i].b);
    }
    if (!group.isEmpty()) flush();
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

QString weekStartOf(const QString &dayStr, int weekStart)
{
    const QDate d = day(dayStr);
    return d.isValid() ? iso(firstOfWeek(d, weekStart)) : QString();
}

QVariantMap calMonth(const Value &root, const QSet<QString> &hidden, int y, int m, const QString &today, int weekStart)
{
    const QDate first(y, m, 1);
    const QDate startW = firstOfWeek(first, weekStart);
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

QVariantMap calWeek(const Value &root, const QSet<QString> &hidden, const QString &weekFirst, const QString &today)
{
    const QDate ws = day(weekFirst), we = ws.addDays(6);
    if (!ws.isValid()) return {};
    QVector<Span> items;
    for (const Span &it : datedSpans(root, hidden))
        if (it.b >= ws && it.a <= we) items << it;
    QVariantList days;
    for (int i = 0; i < 7; ++i) {
        const QDate d = ws.addDays(i);
        days << QVariantMap{ { QStringLiteral("date"), iso(d) }, { QStringLiteral("d"), d.day() }, { QStringLiteral("m"), d.month() },
                             { QStringLiteral("wd"), d.dayOfWeek() % 7 }, { QStringLiteral("today"), iso(d) == today } };
    }
    // ---- 全天区：计划 + 住宿（住到退房前一晚）----
    QVector<QVariantMap> maps;
    QVector<QPair<int, int>> ranges;
    const auto addBar = [&](QVariantMap m, const QDate &a, const QDate &b) {
        const int ca = int(std::max<qint64>(0, ws.daysTo(a))), cb = int(std::min<qint64>(6, ws.daysTo(b)));
        m.insert(QStringLiteral("a"), ca);
        m.insert(QStringLiteral("b"), cb);
        m.insert(QStringLiteral("cutL"), a < ws);
        m.insert(QStringLiteral("cutR"), b > we);
        maps << m;
        ranges.push_back({ ca, cb + 1 });
    };
    for (const Span &it : items) {
        QVariantMap m = barBase(it);
        m.insert(QStringLiteral("kind"), QStringLiteral("plan"));
        addBar(m, it.a, it.b);
    }
    for (const Span &it : items)
        for (const Value &bk : (*it.p)[QStringLiteral("bookings")].asArray()) {
            if (str(bk, "kind") != QLatin1String("hotel")) continue;
            const QDate a = day(str(bk, "start").left(10));
            const QString e = str(bk, "end");
            const QDate b0 = e.isEmpty() ? a : day(e.left(10));
            if (!a.isValid() || !b0.isValid()) continue;
            const QDate b = b0 > a ? b0.addDays(-1) : a;
            if (b < ws || a > we) continue;
            addBar({ { QStringLiteral("kind"), QStringLiteral("hotel") }, { QStringLiteral("id"), str(*it.p, "id") }, { QStringLiteral("name"), str(bk, "title") },
                     { QStringLiteral("color"), planColorOf(*it.p) }, { QStringLiteral("start"), iso(a) }, { QStringLiteral("end"), iso(b) } }, a, b);
        }
    int nLanes = 0;
    const QVector<int> lane = layLanes(ranges, &nLanes);
    QVariantList bars;
    for (int i = 0; i < maps.size(); ++i) { maps[i].insert(QStringLiteral("lane"), lane[i]); bars << maps[i]; }

    // ---- 当天在哪一站、有时间的事件 ----
    QVector<QVariantList> stopsByDay(7);
    QVector<QVector<WeekEvent>> evs(7);
    // 跨午夜的拆段：b 超过 24:00 的部分放到第二天（第二天不在这一周就不画）
    const auto addEvent = [&](const QDate &d, int a, int b, const QVariantMap &m) {
        for (QDate at = d; b > 0; at = at.addDays(1), b -= 1440, a = 0) {
            const qint64 k = ws.daysTo(at);
            if (k >= 0 && k < 7 && a < 1440) evs[int(k)].push_back({ m, a, std::min(b, 1440) });
        }
    };
    for (const Span &it : items) {
        const Value &p = *it.p;
        const QString planId = str(p, "id"), planName = str(p, "name"), planColor = planColorOf(p);
        for (const Value &bk : p[QStringLiteral("bookings")].asArray()) {
            const QString kind = str(bk, "kind"), start = str(bk, "start"), end = str(bk, "end");
            if (kind == QLatin1String("hotel") || start.size() < 12 || start[10] != QLatin1Char('T')) continue;
            const QDate sd = day(start.left(10));
            const int sm = toMin(start.mid(11));
            if (!sd.isValid() || sm < 0) continue;
            QDate ed = end.isEmpty() ? sd : day(end.left(10));
            if (!ed.isValid() || ed < sd) ed = sd;
            const int em0 = end.size() > 11 && end[10] == QLatin1Char('T') ? toMin(end.mid(11)) : -1;
            const int em = em0 >= 0 ? em0 : sm + 60;
            const QString from = str(bk, "from"), to = str(bk, "to");
            const QVariantMap m{ { QStringLiteral("id"), planId }, { QStringLiteral("plan"), planName }, { QStringLiteral("tab"), QStringLiteral("bookings") }, { QStringLiteral("kind"), kind.isEmpty() ? QStringLiteral("other") : kind },
                                 { QStringLiteral("title"), str(bk, "title") }, { QStringLiteral("sub"), from.isEmpty() && to.isEmpty() ? planName : from + QStringLiteral(" → ") + to },
                                 { QStringLiteral("color"), planColor }, { QStringLiteral("time"), fmtMin(sm) + (end.isEmpty() ? QString() : QStringLiteral("–") + fmtMin(em % 1440)) } };
            const qint64 span = sd.daysTo(ed);
            // 一段连续的时间：开始那天从 sm 起，按分钟算到结束（addEvent 自己拆成每天一段）；至少画半小时
            addEvent(sd, sm, int(std::max<qint64>(span * 1440 + em, sm + 30)), m);
        }
        const QVector<int> valid = validStops(p);
        const json::Array &stops = p[QStringLiteral("stops")].asArray();
        const bool route = str(p, "type") == QLatin1String("route");
        for (int vi = 0; vi < valid.size(); ++vi) {
            const Value &s = stops[size_t(valid[vi])];
            const QString color = str(s, "color").isEmpty() ? planColor : str(s, "color");
            const QString arr = str(s, "arriveDate"), dep0 = str(s, "departDate"), dep = dep0.isEmpty() ? arr : dep0;
            const json::Array &sched = s[QStringLiteral("schedule")].asArray();
            // 当天的站
            if (okDate(arr))
                for (int i = 0; i < 7; ++i) {
                    const QDate d = ws.addDays(i);
                    const QString ds = iso(d);
                    if (d < it.a || d > it.b || ds < arr || ds > dep) continue;
                    QStringList loose;
                    for (const Value &sc : sched) {
                        const QString date = str(sc, "date"), text = str(sc, "text");
                        if (str(sc, "time").isEmpty() && (date.isEmpty() || date == ds) && !text.isEmpty()) loose << text;
                    }
                    stopsByDay[i] << QVariantMap{ { QStringLiteral("id"), planId }, { QStringLiteral("plan"), planName }, { QStringLiteral("name"), str(s, "name") }, { QStringLiteral("color"), color },
                                                  { QStringLiteral("n"), route ? vi + 1 : 0 }, { QStringLiteral("loose"), loose } };
                }
            // 写了开始时间的日程（没写结束算 1 小时；结束早于开始算到第二天）
            for (const Value &sc : sched) {
                const int a = toMin(str(sc, "time"));
                if (a < 0) continue;
                const QString date = str(sc, "date");
                const QDate d = day(!date.isEmpty() ? date : arr == dep ? arr : QString());
                if (!d.isValid()) continue;
                const int e = toMin(str(sc, "end"));
                const QString text = str(sc, "text");
                const int nl = text.indexOf(QLatin1Char('\n'));
                const QString endT = str(sc, "end");
                addEvent(d, a, e < 0 || e == a ? a + 60 : e > a ? e : e + 1440,
                         { { QStringLiteral("id"), planId }, { QStringLiteral("plan"), planName }, { QStringLiteral("tab"), QStringLiteral("itinerary") }, { QStringLiteral("kind"), QString() },
                           { QStringLiteral("title"), text.isEmpty() ? str(s, "name") : text.left(nl) }, { QStringLiteral("sub"), str(s, "name") }, { QStringLiteral("color"), color },
                           { QStringLiteral("time"), str(sc, "time") + (endT.isEmpty() ? QString() : QStringLiteral("–") + endT) } });
            }
        }
    }
    QVariantList stopRows, evRows;
    int first = 7 * 60;
    for (int i = 0; i < 7; ++i) {
        stopRows << QVariant(stopsByDay[i]);
        layoutDay(evs[i]);
        QVariantList l;
        for (const WeekEvent &e : evs[i]) {
            QVariantMap m = e.m;
            m.insert(QStringLiteral("a"), e.a);
            m.insert(QStringLiteral("b"), e.b);
            m.insert(QStringLiteral("col"), e.col);
            m.insert(QStringLiteral("n"), e.n);
            l << m;
            first = std::min(first, e.a);
        }
        evRows << QVariant(l);
    }
    return { { QStringLiteral("start"), iso(ws) }, { QStringLiteral("lanes"), nLanes }, { QStringLiteral("first"), first }, { QStringLiteral("empty"), items.isEmpty() },
             { QStringLiteral("days"), days }, { QStringLiteral("bars"), bars }, { QStringLiteral("stops"), stopRows }, { QStringLiteral("events"), evRows } };
}

QVector<RamadanSpan> ramadanSpans(int year)
{
    static const RamadanSpan table[] = {
#include "ramadan_table.inc"
    };
    const QByteArray y = QByteArray::number(year);
    QVector<RamadanSpan> out;
    for (const RamadanSpan &s : table)
        if (qstrncmp(s.a, y.constData(), 4) == 0 || qstrncmp(s.b, y.constData(), 4) == 0) out << s;
    return out;
}

namespace {
// 季节表的一行属于哪些区域、对应哪些国家代码：行里写了就用（新数据都写了），旧数据按国家名查（Electron SEASON_REGION、SEASON_ISO）
QStringList seasonRegionsOf(const Value &row)
{
    static const QHash<QString, QStringList> table = [] {
        QHash<QString, QStringList> h;
        const auto put = [&h](const char *region, std::initializer_list<const char *> names) {
            for (const char *n : names) h[QString::fromUtf8(n)] << QString::fromUtf8(region);
        };
        put("东亚", { "日本", "韩国", "俄罗斯", "蒙古" });
        put("东南亚", { "新加坡", "印尼", "马来西亚", "泰国", "菲律宾", "越南", "柬埔寨", "老挝" });
        put("西亚/中亚", { "土耳其", "马尔代夫", "阿联酋", "尼泊尔", "印度", "斯里兰卡", "约旦", "格鲁吉亚" });
        put("大洋洲", { "新西兰", "澳大利亚", "斐济", "塞班岛" });
        put("欧洲", { "俄罗斯", "瑞士", "意大利", "奥匈捷", "北欧四国", "法国", "克罗地亚", "塞波黑", "西葡", "英国", "冰岛", "希腊", "德国" });
        put("北美洲", { "奥兰多迪士尼+环球", "墨西哥", "美国", "加拿大" });
        put("南美洲", { "阿根廷", "秘鲁", "智利", "巴西" });
        put("非洲", { "埃及", "肯尼亚/坦桑尼亚", "摩洛哥", "南非" });
        put("其他", { "南极" });
        return h;
    }();
    const Value &r = row[QStringLiteral("region")];
    if (r.isString() && !r.toString().isEmpty()) return { r.toString() };
    if (r.isArray()) { QStringList l; for (const Value &v : r.asArray()) l << v.toString(); if (!l.isEmpty()) return l; }
    const QString country = str(row, "country");
    const auto it = table.constFind(country);
    if (it != table.constEnd()) return *it;
    static const QRegularExpression cn(QStringLiteral("中国|香港|澳门|台湾|西藏|新疆|云南"));
    if (cn.match(country).hasMatch()) return { QStringLiteral("中国") };
    return { allRegions().contains(str(row, "group")) ? str(row, "group") : QStringLiteral("其他") };
}

QStringList seasonIsoOf(const Value &row)
{
    const Value &code = row[QStringLiteral("iso")];
    if (code.isString() && !code.toString().isEmpty()) return { code.toString() };
    if (code.isArray() && !code.asArray().empty()) { QStringList l; for (const Value &v : code.asArray()) l << v.toString(); return l; }
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> h;
        for (const auto &[name, c] : std::initializer_list<std::pair<const char *, const char *>>{
                 { "日本", "JPN" }, { "韩国", "KOR" }, { "土耳其", "TUR" }, { "马尔代夫", "MDV" }, { "阿联酋", "ARE" }, { "新西兰", "NZL" }, { "澳大利亚", "AUS" }, { "斐济", "FJI" },
                 { "俄罗斯", "RUS" }, { "瑞士", "CHE" }, { "意大利", "ITA" }, { "奥匈捷", "AUT,HUN,CZE" }, { "北欧四国", "NOR,SWE,FIN,DNK" }, { "法国", "FRA" },
                 { "克罗地亚", "HRV" }, { "塞波黑", "SRB,BIH" }, { "西葡", "ESP,PRT" }, { "英国", "GBR" }, { "冰岛", "ISL" }, { "新加坡", "SGP" }, { "印尼", "IDN" },
                 { "马来西亚", "MYS" }, { "泰国", "THA" }, { "菲律宾", "PHL" }, { "越南", "VNM" }, { "南极", "ATA" }, { "塞班岛", "MNP" }, { "阿根廷", "ARG" }, { "墨西哥", "MEX" },
                 { "埃及", "EGY" }, { "肯尼亚/坦桑尼亚", "KEN,TZA" }, { "中国", "CHN" }, { "德国", "DEU" }, { "希腊", "GRC" }, { "美国", "USA" }, { "加拿大", "CAN" } })
            h.insert(QString::fromUtf8(name), QString::fromLatin1(c));
        return h;
    }();
    const QString c = table.value(str(row, "country"));
    return c.isEmpty() ? QStringList() : c.split(QLatin1Char(','));
}

// 某一天在 12 个月里的位置（0–1，每个月等宽，和表头的格子对齐）
double yearX(const QDate &d) { return ((d.month() - 1) + (d.day() - 1.0) / d.daysInMonth()) / 12.0; }
} // namespace

QVariantMap calSeason(const Value &root, int year, const QSet<QString> *visited, const QSet<QString> &closed)
{
    // 一行的相关计划
    QHash<QString, QVariantList> plansBy;
    for (const Value &p : root[QStringLiteral("plans")].asArray()) {
        const QString ref = str(p, "seasonRef");
        if (!ref.isEmpty()) plansBy[ref] << QVariantMap{ { QStringLiteral("id"), str(p, "id") }, { QStringLiteral("name"), str(p, "name") }, { QStringLiteral("wish"), statusOf(p) == QLatin1String("wish") } };
    }
    // 这一年的斋月（跨年的只画这一年里的部分，日期写完整的）
    const QDate y0(year, 1, 1), y1(year, 12, 31);
    QVariantList ramBars;
    for (const RamadanSpan &s : ramadanSpans(year)) {
        const QDate a = day(QString::fromLatin1(s.a)), b = day(QString::fromLatin1(s.b));
        const double l = yearX(std::max(a, y0)), r = b >= y1 ? 1.0 : yearX(b.addDays(1));
        ramBars << QVariantMap{ { QStringLiteral("l"), l }, { QStringLiteral("r"), std::max(r, l + 0.012) }, { QStringLiteral("a"), iso(a) }, { QStringLiteral("b"), iso(b) }, { QStringLiteral("labelLeft"), l > 0.8 } };
    }
    QStringList order = allRegions();
    order.removeLast();   // "未分类"
    order << QStringLiteral("其他");
    QHash<QString, QVariantList> groups;
    QHash<QString, int> seen;
    for (const Value &row : root[QStringLiteral("seasonGuide")][QStringLiteral("rows")].asArray()) {
        const QString key = str(row, "country");
        // 12 个月 → 相邻同色的连成一段（segments）
        QVariantList segs;
        const QVariantList cells = seasonInfo(root, key).value(QStringLiteral("cells")).toList();
        int from = 1;
        QString kind;
        QStringList texts, textsEn;
        const auto flushSeg = [&](int to) {
            segs << QVariantMap{ { QStringLiteral("from"), from }, { QStringLiteral("to"), to }, { QStringLiteral("kind"), kind },
                                 { QStringLiteral("text"), texts.join(QStringLiteral("；")) }, { QStringLiteral("textEn"), textsEn.join(QStringLiteral("; ")) } };
        };
        for (int m = 1; m <= 12; ++m) {
            const QVariantMap c = cells.value(m - 1).toMap();
            const QString k = c.value(QStringLiteral("kind"), QStringLiteral("none")).toString();
            if (m > 1 && k != kind) { flushSeg(m - 1); from = m; texts.clear(); textsEn.clear(); }
            kind = k;
            const QString t = c.value(QStringLiteral("text")).toString(), te = c.value(QStringLiteral("textEn")).toString();
            if (!t.isEmpty() && !texts.contains(t)) texts << t;
            const QString en = te.isEmpty() ? t : te;
            if (!en.isEmpty() && !textsEn.contains(en)) textsEn << en;
        }
        flushSeg(12);
        bool v = false;
        if (visited) for (const QString &code : seasonIsoOf(row)) v = v || visited->contains(code);
        const QVariantMap m{ { QStringLiteral("head"), false }, { QStringLiteral("key"), key }, { QStringLiteral("name"), key }, { QStringLiteral("nameEn"), str(row, "countryEn") },
                             { QStringLiteral("visited"), v }, { QStringLiteral("segs"), segs }, { QStringLiteral("ram"), row[QStringLiteral("ramadan")].toBool() ? ramBars : QVariantList() },
                             { QStringLiteral("plans"), plansBy.value(key) } };
        for (const QString &g0 : seasonRegionsOf(row)) {
            const QString g = order.contains(g0) ? g0 : QStringLiteral("其他");
            groups[g] << m;
            if (v) seen[g]++;
        }
    }
    QVariantList rows;
    QStringList names;
    for (const QString &g : order) {
        const QVariantList &list = groups[g];
        if (list.isEmpty()) continue;
        names << g;
        const bool isClosed = closed.contains(g);
        rows << QVariantMap{ { QStringLiteral("head"), true }, { QStringLiteral("group"), g }, { QStringLiteral("count"), int(list.size()) }, { QStringLiteral("visited"), seen.value(g) }, { QStringLiteral("closed"), isClosed } };
        if (!isClosed) rows += list;
    }
    return { { QStringLiteral("rows"), rows }, { QStringLiteral("groups"), names }, { QStringLiteral("ready"), visited != nullptr }, { QStringLiteral("year"), year } };
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
