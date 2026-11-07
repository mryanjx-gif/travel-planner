#include "planpage.h"

#include "planmoney.h"
#include "plans.h"
#include "planweather.h"
#include "suncalc.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

namespace tp::map::plans {

using json::Value;

namespace {

QString str(const Value &o, const char *key) { return o[QString::fromLatin1(key)].toString(); }

// JS 的 setUTCMonth：超出当月的日子顺延到下个月（8 月 31 日 + 6 个月 = 3 月 3 日）
QString addMonths(const QString &s, int k)
{
    const QDate d = QDate::fromString(s, Qt::ISODate);
    const int total = d.year() * 12 + d.month() - 1 + k;
    return QDate(total / 12, total % 12 + 1, 1).addDays(d.day() - 1).toString(Qt::ISODate);
}

bool validStopNum(const Value &s, const char *key, double *out)
{
    const Value *v = s.asObject().find(QString::fromLatin1(key));
    if (!v || v->isArray() || v->isObject()) return false;
    *out = v->jsToNumber();
    return std::isfinite(*out);
}

// 站点的日期：到达日期，没有就用日程里最早的（travelkit.js stopDate）
QString stopDate(const Value &s)
{
    if (!str(s, "arriveDate").isEmpty()) return str(s, "arriveDate");
    QString best;
    for (const Value &it : s[QStringLiteral("schedule")].asArray()) {
        const QString d = str(it, "date");
        if (!d.isEmpty() && (best.isEmpty() || d < best)) best = d;
    }
    return best;
}

Value *mutSchedule(Value &stop)
{
    json::Object &o = stop.mutObject();
    Value *sch = o.find(QStringLiteral("schedule"));
    if (!sch) { o.set(QStringLiteral("schedule"), Value::array()); return o.find(QStringLiteral("schedule")); }
    if (sch->isArray()) return sch;
    // 旧格式的字符串（导入时已经转过，这里兜底）
    Value arr = Value::array();
    if (sch->isString() && !sch->toString().isEmpty()) {
        json::Object it;
        it.set(QStringLiteral("text"), Value(sch->toString()));
        arr.mutArray().push_back(Value(std::move(it)));
    }
    o.set(QStringLiteral("schedule"), std::move(arr));
    return o.find(QStringLiteral("schedule"));
}

} // namespace

QString normTime(const QString &t0, bool *ok)
{
    const QString t = t0.trimmed();
    if (t.isEmpty()) return {};
    static const QRegularExpression re(QStringLiteral("^(\\d{1,2})[:：]?(\\d{2})$"));
    const QRegularExpressionMatch m = re.match(t);
    const int h = m.captured(1).toInt(), mi = m.captured(2).toInt();
    if (!m.hasMatch() || h > 23 || mi > 59) { *ok = false; return {}; }
    return QStringLiteral("%1:%2").arg(h, 2, 10, QLatin1Char('0')).arg(mi, 2, 10, QLatin1Char('0'));
}

QVariantList schedDays(const Value &stop, const QString &today)
{
    struct Row { int j; QString date, time, end, text; };
    QVector<Row> rows;
    const Value &sch = stop[QStringLiteral("schedule")];
    if (sch.isString()) {
        if (!sch.toString().isEmpty()) rows.push_back({ 0, {}, {}, {}, sch.toString() });
    } else {
        int j = 0;
        for (const Value &it : sch.asArray()) {
            rows.push_back({ j, str(it, "date"), str(it, "time"), str(it, "end"), str(it, "text") });
            ++j;
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.date.isEmpty() != b.date.isEmpty()) return b.date.isEmpty();
        if (a.date != b.date) return a.date < b.date;
        if (a.time.isEmpty() != b.time.isEmpty()) return b.time.isEmpty();   // 0.12.9 未定时间的放后面
        return a.time < b.time;
    });
    QVariantList out, items;
    QString cur;
    const auto flush = [&] {
        if (!items.isEmpty()) out << QVariantMap{ { QStringLiteral("date"), cur }, { QStringLiteral("items"), items } };
        items.clear();
    };
    for (const Row &r : rows) {
        if (r.date != cur) { flush(); cur = r.date; }
        items << QVariantMap{ { QStringLiteral("j"), r.j }, { QStringLiteral("date"), r.date }, { QStringLiteral("time"), r.time }, { QStringLiteral("end"), r.end },
                              { QStringLiteral("text"), r.text }, { QStringLiteral("today"), !r.date.isEmpty() && r.date == today } };
    }
    flush();
    return out;
}

QString putSchedule(Value &stop, int j, const QString &date, const QString &time0, const QString &end0, const QString &text0)
{
    const QString d = date.trimmed(), text = text0.trimmed();
    if (d.isEmpty() && time0.trimmed().isEmpty() && end0.trimmed().isEmpty() && text.isEmpty()) return QStringLiteral("empty");
    if (!d.isEmpty() && !okDate(d)) return QStringLiteral("badDate");
    bool ok = true;
    const QString time = normTime(time0, &ok), end = normTime(end0, &ok);
    if (!ok) return QStringLiteral("badTime");
    if (!end.isEmpty() && time.isEmpty()) return QStringLiteral("endNoStart");

    json::Array &arr = mutSchedule(stop)->mutArray();
    if (j >= int(arr.size())) return QStringLiteral("empty");
    Value fresh{ json::Object() };
    Value &it = j >= 0 ? arr[j] : fresh;
    json::Object &o = it.mutObject();
    o.set(QStringLiteral("date"), Value(d));
    if (time.isEmpty()) o.remove(QStringLiteral("time")); else o.set(QStringLiteral("time"), Value(time));
    if (end.isEmpty()) o.remove(QStringLiteral("end")); else o.set(QStringLiteral("end"), Value(end));
    o.set(QStringLiteral("text"), Value(text));
    if (j < 0) arr.push_back(std::move(fresh));
    return {};
}

bool removeSchedule(Value &stop, int j)
{
    Value *sch = stop.mutObject().find(QStringLiteral("schedule"));
    if (!sch || !sch->isArray() || j < 0 || j >= int(sch->asArray().size())) return false;
    json::Array &arr = sch->mutArray();
    arr.erase(arr.begin() + j);
    return true;
}

Value *mutValidStop(Value &root, const QString &planId, int k)
{
    Value *p = mutPlan(root, planId);
    if (!p) return nullptr;
    const QVector<int> st = validStops(std::as_const(*p));
    if (k < 0 || k >= st.size()) return nullptr;
    return &p->mutObject().find(QStringLiteral("stops"))->mutArray()[st[k]];
}

QVariantList docWarnings(const Value &root, const Value &plan)
{
    QVariantList out;
    const QString start = str(plan, "startDate");
    if (str(plan, "status") == QLatin1String("completed") || start.isEmpty()) return out;
    const QString end = str(plan, "endDate").isEmpty() ? start : str(plan, "endDate");
    for (const Value &d : root[QStringLiteral("documents")].asArray()) {
        const QString ex = str(d, "expires");
        if (!okDate(ex)) continue;
        QString kind;
        if (str(d, "kind") == QLatin1String("passport")) {
            if (ex < start) kind = QStringLiteral("passportExpired");
            else if (okDate(end) && ex < addMonths(end, 6)) kind = QStringLiteral("passport6m");
        } else if (str(d, "planId") == str(plan, "id") && ex < end) {
            kind = QStringLiteral("doc");
        }
        if (kind.isEmpty()) continue;
        out << QVariantMap{ { QStringLiteral("kind"), kind }, { QStringLiteral("owner"), str(d, "owner") }, { QStringLiteral("docKind"), str(d, "kind") },
                            { QStringLiteral("label"), str(d, "label") }, { QStringLiteral("expires"), ex }, { QStringLiteral("end"), end } };
    }
    return out;
}

int wishFit(const Value &root, const Value &plan, int month)
{
    const QVariantList cells = seasonInfo(root, str(plan, "seasonRef")).value(QStringLiteral("cells")).toList();
    if (cells.isEmpty()) return -1;
    const QString k = cells.value(month - 1).toMap().value(QStringLiteral("kind")).toString();
    return k == QLatin1String("best") || k == QLatin1String("nice") ? 1 : 0;
}

QVariantMap wishInfo(const Value &root, const plans::Filter &f, int month, int nowMonth, const QSet<QString> &listHidden)
{
    QVariantMap l = plans::listInfo(root, f, false, listHidden);
    QVariantList rows = l.value(QStringLiteral("rows")).toList();
    QVector<int> months(12, 0);
    QHash<QString, int> rank;
    for (QVariant &v : rows) {
        QVariantMap r = v.toMap();
        const QString id = r.value(QStringLiteral("id")).toString();
        const Value *p = plans::planById(root, id);
        const QVariantList cells = p ? seasonInfo(root, str(*p, "seasonRef")).value(QStringLiteral("cells")).toList() : QVariantList();
        QStringList kinds;
        for (int i = 0; i < cells.size(); ++i) {
            const QString k = cells[i].toMap().value(QStringLiteral("kind")).toString();
            kinds << k;
            if (k == QLatin1String("best") || k == QLatin1String("nice")) months[i]++;
        }
        const QString k = kinds.value(month - 1), kn = kinds.value(nowMonth - 1);
        const bool hid = r.value(QStringLiteral("listHidden")).toBool();
        const QString group = hid ? QStringLiteral("listHidden") : kinds.isEmpty() ? QStringLiteral("noref")
                            : k == QLatin1String("best") || k == QLatin1String("nice") ? QStringLiteral("fit") : QStringLiteral("other");
        r.insert(QStringLiteral("group"), group);
        r.insert(QStringLiteral("kinds"), kinds);
        r.insert(QStringLiteral("regions"), p ? plans::regionsOf(*p) : QStringList());
        r.insert(QStringLiteral("nowFit"), kn == QLatin1String("best") || kn == QLatin1String("nice"));
        rank.insert(id, hid ? 4 : kinds.isEmpty() ? 3 : k == QLatin1String("best") ? 0 : k == QLatin1String("nice") ? 1 : 2);
        v = r;
    }
    std::stable_sort(rows.begin(), rows.end(), [&rank](const QVariant &a, const QVariant &b) {
        return rank.value(a.toMap().value(QStringLiteral("id")).toString()) < rank.value(b.toMap().value(QStringLiteral("id")).toString());
    });
    QHash<QString, int> inGroup;
    for (const QVariant &v : rows) inGroup[v.toMap().value(QStringLiteral("group")).toString()]++;
    for (QVariant &v : rows) {
        QVariantMap r = v.toMap();
        r.insert(QStringLiteral("groupCount"), inGroup.value(r.value(QStringLiteral("group")).toString()));
        v = r;
    }
    QVariantList mv;
    for (int n : months) mv << n;
    l.insert(QStringLiteral("rows"), rows);
    l.insert(QStringLiteral("month"), month);
    l.insert(QStringLiteral("months"), mv);
    return l;
}

QVariantList seasonRows(const Value &root)
{
    QVariantList out;
    for (const Value &row : root[QStringLiteral("seasonGuide")][QStringLiteral("rows")].asArray()) {
        const QString g = str(row, "group");
        out << QVariantMap{ { QStringLiteral("key"), str(row, "country") }, { QStringLiteral("name"), str(row, "country") },
                            { QStringLiteral("nameEn"), str(row, "countryEn") }, { QStringLiteral("group"), g.isEmpty() ? str(row, "region") : g } };
    }
    return out;
}

QVariantMap seasonInfo(const Value &root, const QString &ref)
{
    if (ref.isEmpty()) return {};
    for (const Value &row : root[QStringLiteral("seasonGuide")][QStringLiteral("rows")].asArray()) {
        if (str(row, "country") != ref) continue;
        QVariantList cells;
        for (int m = 1; m <= 12; ++m) {
            QVariantMap c;
            for (const Value &sp : row[QStringLiteral("spans")].asArray()) {
                const int a = int(sp[QStringLiteral("from")].jsToNumber()), b = int(sp[QStringLiteral("to")].jsToNumber());
                if (a <= b ? (m >= a && m <= b) : (m >= a || m <= b)) {
                    c = { { QStringLiteral("kind"), str(sp, "kind") }, { QStringLiteral("text"), str(sp, "text") }, { QStringLiteral("textEn"), str(sp, "textEn") } };
                    break;
                }
            }
            cells << c;
        }
        return { { QStringLiteral("name"), ref }, { QStringLiteral("nameEn"), str(row, "countryEn") }, { QStringLiteral("cells"), cells } };
    }
    return {};
}

QVariantMap pageInfo(const Value &root, const QString &planId, const QString &today)
{
    QVariantMap m = sheetInfo(root, planId);
    if (m.isEmpty()) return m;
    const Value *pp = planById(root, planId);
    const Value &p = *pp;
    double days = 0;
    m.insert(QStringLiteral("days"), validStopNum(p, "days", &days) && days >= 1 ? int(std::lround(days)) : 0);
    m.insert(QStringLiteral("season"), seasonInfo(root, str(p, "seasonRef")));
    m.insert(QStringLiteral("warnings"), docWarnings(root, p));

    // 在路上：今天在这趟的日期里（I3）；今天所在的站 = 最后一个到达日期 ≤ 今天的（mapfirst.js stopOn）
    const QString start = str(p, "startDate"), end = str(p, "endDate");
    const bool live = dated(p) && start <= today && today <= end && statusOf(p) != QLatin1String("wish");
    const json::Array &stops = p[QStringLiteral("stops")].asArray();
    const QVector<int> st = validStops(p);
    int todayStop = -1;
    if (live)
        for (int k = 0; k < st.size(); ++k) {
            const QString ad = str(stops[st[k]], "arriveDate");
            if (okDate(ad) && ad <= today) todayStop = k;
        }
    m.insert(QStringLiteral("live"), live);
    m.insert(QStringLiteral("todayStop"), todayStop);

    // 0.10.1 交通住宿（消费要汇率，由 PlanPageController 加）
    m.insert(QStringLiteral("bookings"), bookingDays(p));
    m.insert(QStringLiteral("bookCount"), int(p[QStringLiteral("bookings")].asArray().size()));
    m.insert(QStringLiteral("bookCost"), bookingsCost(p));
    int undated = 0;
    m.insert(QStringLiteral("nights"), missingNights(p, today, &undated));
    m.insert(QStringLiteral("nightsUndated"), undated);
    const QVector<QVariantList> books = stopBookings(p);
    // 0.10.3 高反
    const QVariantMap alt = altitudeInfo(p);
    m.insert(QStringLiteral("altWarnings"), alt.value(QStringLiteral("warnings")));
    m.insert(QStringLiteral("altApprox"), alt.value(QStringLiteral("approx")));
    const QStringList altTags = alt.value(QStringLiteral("tags")).toStringList();

    QVariantList out = m.value(QStringLiteral("stops")).toList();
    for (int k = 0; k < out.size() && k < st.size(); ++k) {
        const Value &s = stops[st[k]];
        QVariantMap sm = out[k].toMap();
        double lat = 0, lng = 0;
        validStopNum(s, "lat", &lat);
        validStopNum(s, "lng", &lng);
        sm.insert(QStringLiteral("id"), str(s, "id"));
        sm.insert(QStringLiteral("lat"), lat);
        sm.insert(QStringLiteral("lng"), lng);
        sm.insert(QStringLiteral("days"), schedDays(s, live ? today : QString()));
        sm.insert(QStringLiteral("books"), books[k]);
        double elev = 0;
        if (stopElev(s, &elev)) sm.insert(QStringLiteral("elev"), elev);
        if (k < altTags.size()) sm.insert(QStringLiteral("altTag"), altTags[k]);
        const QString d = stopDate(s);
        const sun::Times t = sun::info(lat, lng, d, str(s, "tz"));
        if (t.ok)
            sm.insert(QStringLiteral("sun"), QVariantMap{ { QStringLiteral("date"), d }, { QStringLiteral("polar"), t.polar }, { QStringLiteral("rise"), t.rise },
                                                          { QStringLiteral("set"), t.set }, { QStringLiteral("dayMin"), t.dayMin }, { QStringLiteral("approx"), t.approx } });
        out[k] = sm;
    }
    m.insert(QStringLiteral("stops"), out);
    return m;
}

QString setDays(Value &root, const QString &planId, const QString &text)
{
    Value *p = mutPlan(root, planId);
    if (!p) return {};
    const QString t = text.trimmed();
    if (t.isEmpty()) { p->mutObject().remove(QStringLiteral("days")); return {}; }
    bool ok = false;
    const double v = t.toDouble(&ok);
    const int n = ok ? int(std::lround(v)) : 0;
    if (!ok || n < 1 || n > 999) return QStringLiteral("badDays");
    p->mutObject().set(QStringLiteral("days"), Value(double(n)));
    return {};
}

bool startPlanning(Value &root, const QString &planId, const QString &start, const QString &end)
{
    Value *p = mutPlan(root, planId);
    if (!p) return false;
    json::Object &o = p->mutObject();
    o.set(QStringLiteral("status"), Value(QStringLiteral("planning")));
    if (!start.isEmpty()) o.set(QStringLiteral("startDate"), Value(start));
    if (!end.isEmpty() || !start.isEmpty()) o.set(QStringLiteral("endDate"), Value(end.isEmpty() ? start : end));
    return true;
}

// ---------------- 补时区 ----------------

QVector<TzStop> stopsWithoutTz(const Value &plan)
{
    QVector<TzStop> out;
    const json::Array &stops = plan[QStringLiteral("stops")].asArray();
    for (int i : validStops(plan)) {
        const Value &s = stops[i];
        if (!str(s, "tz").isEmpty() || str(s, "id").isEmpty()) continue;
        TzStop t;
        t.id = str(s, "id");
        validStopNum(s, "lat", &t.lat);
        validStopNum(s, "lng", &t.lng);
        out.push_back(t);
    }
    return out;
}

QUrl tzUrl(const QVector<TzStop> &part)
{
    QStringList la, lo;
    for (const TzStop &s : part) { la << QString::number(s.lat, 'f', 3); lo << QString::number(s.lng, 'f', 3); }
    QUrl u(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("latitude"), la.join(QLatin1Char(',')));
    q.addQueryItem(QStringLiteral("longitude"), lo.join(QLatin1Char(',')));
    q.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
    q.addQueryItem(QStringLiteral("forecast_days"), QStringLiteral("1"));
    u.setQuery(q);
    return u;
}

QStringList parseTz(const QByteArray &body, int count)
{
    QStringList out;
    for (int i = 0; i < count; ++i) out << QString();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonArray rows = doc.array();
    if (doc.isObject()) rows.append(doc.object());
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_]+/[A-Za-z_\\-/]+$|^UTC$"));
    for (int i = 0; i < count && i < rows.size(); ++i) {
        const QString tz = rows[i].toObject().value(QStringLiteral("timezone")).toString();
        if (re.match(tz).hasMatch()) out[i] = tz;
    }
    return out;
}

int applyTz(Value &root, const QString &planId, const QStringList &ids, const QStringList &tz)
{
    Value *p = mutPlan(root, planId);
    Value *stops = p ? p->mutObject().find(QStringLiteral("stops")) : nullptr;
    if (!stops || !stops->isArray()) return 0;
    int n = 0;
    for (Value &s : stops->mutArray()) {
        const int k = ids.indexOf(str(std::as_const(s), "id"));
        if (k < 0 || k >= tz.size() || tz[k].isEmpty() || !str(std::as_const(s), "tz").isEmpty()) continue;
        s.mutObject().set(QStringLiteral("tz"), Value(tz[k]));
        ++n;
    }
    return n;
}

} // namespace tp::map::plans
