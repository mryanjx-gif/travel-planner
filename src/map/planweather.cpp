#include "planweather.h"

#include "plans.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tp::map::plans {

using json::Value;

namespace {

constexpr double kPi = 3.14159265358979323846;

QString str(const Value &o, const char *key) { return o[QString::fromLatin1(key)].toString(); }
double coord(const Value &s, const char *key) { return s[QString::fromLatin1(key)].jsToNumber(); }
QDate day(const QString &s) { return QDate::fromString(s, Qt::ISODate); }

// 回复的每一行：多个地点时是数组，一个时是对象
QJsonArray rowsOf(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isArray()) return doc.array();
    QJsonArray one;
    if (doc.isObject()) one.append(doc.object());
    return one;
}

template <class T>
QString coordList(const QVector<T> &part, double T::*member, int digits)
{
    QStringList out;
    for (const T &p : part) out << QString::number(p.*member, 'f', digits);
    return out.join(QLatin1Char(','));
}

} // namespace

// ---------------- 预报 ----------------

QString geoKey(double lat, double lng) { return QString::number(lat, 'f', 2) + QLatin1Char(',') + QString::number(lng, 'f', 2); }

bool inForecastWindow(const Value &plan, const QString &today)
{
    const QString start = str(plan, "startDate");
    if (str(plan, "status") == QLatin1String("completed") || !day(start).isValid()) return false;
    const QString endRaw = str(plan, "endDate"), end = endRaw.isEmpty() ? start : endRaw;
    return end >= today && day(today).daysTo(day(start)) <= 15;
}

QString stopWxDate(const Value &stop, const QString &today)
{
    const QString a = str(stop, "arriveDate"), dep = str(stop, "departDate"), b = dep.isEmpty() ? a : dep;
    return !a.isEmpty() && today >= a && today <= b ? today : a;
}

QString wmoKind(int c)
{
    if (c == 0) return QStringLiteral("clear");
    if (c >= 1 && c <= 2) return QStringLiteral("partly");
    if (c == 3) return QStringLiteral("cloudy");
    if (c == 45 || c == 48) return QStringLiteral("fog");
    if (c >= 51 && c <= 57) return QStringLiteral("drizzle");
    if (c >= 61 && c <= 67) return QStringLiteral("rain");
    if (c >= 71 && c <= 77) return QStringLiteral("snow");
    if (c >= 80 && c <= 82) return QStringLiteral("showers");
    if (c == 85 || c == 86) return QStringLiteral("snowShowers");
    if (c >= 95) return QStringLiteral("storm");
    return QStringLiteral("unknown");
}

QUrl forecastUrl(const QVector<GeoPoint> &part)
{
    QUrl u(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("latitude"), coordList(part, &GeoPoint::lat, 3));
    q.addQueryItem(QStringLiteral("longitude"), coordList(part, &GeoPoint::lng, 3));
    q.addQueryItem(QStringLiteral("daily"), QStringLiteral("weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"));
    q.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
    q.addQueryItem(QStringLiteral("forecast_days"), QStringLiteral("16"));
    u.setQuery(q);
    return u;
}

QVector<QJsonObject> parseDaily(const QByteArray &body, int count)
{
    QVector<QJsonObject> out(count);
    const QJsonArray rows = rowsOf(body);
    for (int i = 0; i < count && i < rows.size(); ++i) out[i] = rows[i].toObject().value(QStringLiteral("daily")).toObject();
    return out;
}

QVariantMap wxDay(const QJsonObject &daily, const QString &date)
{
    if (date.isEmpty()) return {};
    const QJsonArray time = daily.value(QStringLiteral("time")).toArray();
    int i = -1;
    for (int k = 0; k < time.size() && i < 0; ++k) if (time[k].toString() == date) i = k;
    if (i < 0) return {};
    const auto at = [&](const char *key) { return daily.value(QString::fromLatin1(key)).toArray().at(i); };
    const QJsonValue code = at("weather_code"), hi = at("temperature_2m_max"), lo = at("temperature_2m_min"), pop = at("precipitation_probability_max");
    if (!code.isDouble() || !hi.isDouble() || !lo.isDouble()) return {};
    QVariantMap m{ { QStringLiteral("date"), date }, { QStringLiteral("kind"), wmoKind(code.toInt()) }, { QStringLiteral("hi"), hi.toDouble() }, { QStringLiteral("lo"), lo.toDouble() } };
    if (pop.isDouble()) m.insert(QStringLiteral("pop"), pop.toDouble());
    return m;
}

// ---------------- 往年同月气候 ----------------

ClimPlan climPlan(const Value &plan, const QString &today)
{
    const json::Array &stops = plan[QStringLiteral("stops")].asArray();
    const QVector<int> st = validStops(plan);
    ClimPlan out;
    out.stopKeys.resize(st.size());
    const auto monthOf = [&](const Value &s) {
        for (const QString &d : { str(s, "arriveDate"), str(plan, "startDate"), today })
            if (day(d).isValid()) return day(d).month();
        return 1;
    };
    struct Cluster { double lat, lng; QString key; };
    QVector<int> months;
    for (int i : st) if (!months.contains(monthOf(stops[i]))) months << monthOf(stops[i]);
    for (int m : months) {
        QVector<Cluster> cs;
        for (int k = 0; k < st.size(); ++k) {
            const Value &s = stops[st[k]];
            if (monthOf(s) != m) continue;
            const double lat = coord(s, "lat"), lng = coord(s, "lng");
            const double dLng = 0.27 / std::max(0.2, std::cos(lat * kPi / 180));
            const auto c = std::find_if(cs.begin(), cs.end(), [&](const Cluster &x) { return std::abs(x.lat - lat) < 0.27 && std::abs(x.lng - lng) < dLng; });
            if (c != cs.end()) { out.stopKeys[k] = c->key; continue; }
            if (cs.size() >= 12) continue;
            const QString key = geoKey(lat, lng) + QLatin1Char('@') + QString::number(m);
            cs.push_back({ lat, lng, key });
            out.stopKeys[k] = key;
            out.points.push_back({ key, lat, lng, m });
        }
    }
    return out;
}

QVector<int> climYears(int month, const QString &today)
{
    const QDate t = day(today);
    // 今年这个月过完 6 天以上才算（Electron 的条件写成 M > month，上个月刚过完时会算进缺几天的那年；这里照它注释的本意）
    const int M = t.month();
    const int last = M > month + 1 || (M == month + 1 && t.day() > 6) ? t.year() : t.year() - 1;
    QVector<int> out;
    for (int y = last - 9; y <= last; ++y) out << y;
    return out;
}

QUrl climateUrl(const QVector<ClimPoint> &points, int year, int month)
{
    QUrl u(QStringLiteral("https://archive-api.open-meteo.com/v1/archive"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("latitude"), coordList(points, &ClimPoint::lat, 3));
    q.addQueryItem(QStringLiteral("longitude"), coordList(points, &ClimPoint::lng, 3));
    const QDate first(year, month, 1);
    q.addQueryItem(QStringLiteral("start_date"), first.toString(Qt::ISODate));
    q.addQueryItem(QStringLiteral("end_date"), first.addMonths(1).addDays(-1).toString(Qt::ISODate));
    q.addQueryItem(QStringLiteral("daily"), QStringLiteral("temperature_2m_max,temperature_2m_min,precipitation_sum"));
    q.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
    u.setQuery(q);
    return u;
}

void climAdd(QVector<ClimAcc> &acc, const QByteArray &body)
{
    const QJsonArray rows = rowsOf(body);
    for (int j = 0; j < acc.size() && j < rows.size(); ++j) {
        const QJsonObject d = rows[j].toObject().value(QStringLiteral("daily")).toObject();
        if (d.isEmpty()) continue;
        ClimAcc &a = acc[j];
        ++a.years;
        for (const QJsonValue v : d.value(QStringLiteral("temperature_2m_max")).toArray()) if (v.isDouble()) { a.hi += v.toDouble(); ++a.nHi; }
        for (const QJsonValue v : d.value(QStringLiteral("temperature_2m_min")).toArray()) if (v.isDouble()) { a.lo += v.toDouble(); ++a.nLo; }
        for (const QJsonValue v : d.value(QStringLiteral("precipitation_sum")).toArray())
            if (v.isDouble()) { a.mm += v.toDouble(); if (v.toDouble() >= 1) ++a.rainDays; }
    }
}

QJsonObject climResult(const ClimAcc &a, const QVector<int> &years)
{
    if (!a.years || !a.nHi || !a.nLo || years.isEmpty()) return {};
    return { { QStringLiteral("hi"), a.hi / a.nHi }, { QStringLiteral("lo"), a.lo / a.nLo },
             { QStringLiteral("rainDays"), double(a.rainDays) / a.years }, { QStringLiteral("mm"), a.mm / a.years },
             { QStringLiteral("years"), QStringLiteral("%1–%2").arg(years.first()).arg(years.last()) } };
}

// ---------------- 海拔 ----------------

bool stopElev(const Value &s, double *out)
{
    const Value &v = s[QStringLiteral("elev")];
    if (v.isNull() || v.isArray() || v.isObject() || (v.isString() && v.toString().trimmed().isEmpty())) return false;
    *out = v.jsToNumber();
    return std::isfinite(*out);
}

QVector<TzStop> stopsWithoutElev(const Value &plan)
{
    QVector<TzStop> out;
    const json::Array &stops = plan[QStringLiteral("stops")].asArray();
    for (int i : validStops(plan)) {
        const Value &s = stops[i];
        const Value &e = s[QStringLiteral("elev")];
        // 写了认不出的内容（手改的）不覆盖
        if (!(e.isNull() || (e.isString() && e.toString().trimmed().isEmpty())) || str(s, "id").isEmpty()) continue;
        TzStop t;
        t.id = str(s, "id");
        t.lat = coord(s, "lat");
        t.lng = coord(s, "lng");
        out.push_back(t);
    }
    return out;
}

QUrl elevUrl(const QVector<TzStop> &part)
{
    QUrl u(QStringLiteral("https://api.open-meteo.com/v1/elevation"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("latitude"), coordList(part, &TzStop::lat, 5));
    q.addQueryItem(QStringLiteral("longitude"), coordList(part, &TzStop::lng, 5));
    u.setQuery(q);
    return u;
}

QVector<double> parseElev(const QByteArray &body, int count)
{
    QVector<double> out(count, std::numeric_limits<double>::quiet_NaN());
    const QJsonArray e = QJsonDocument::fromJson(body).object().value(QStringLiteral("elevation")).toArray();
    for (int i = 0; i < count && i < e.size(); ++i) if (e[i].isDouble()) out[i] = e[i].toDouble();
    return out;
}

int applyElev(Value &root, const QString &planId, const QStringList &ids, const QVector<double> &elev)
{
    Value *p = mutPlan(root, planId);
    Value *stops = p ? p->mutObject().find(QStringLiteral("stops")) : nullptr;
    if (!stops || !stops->isArray()) return 0;
    int n = 0;
    double old = 0;
    for (Value &s : stops->mutArray()) {
        const int k = ids.indexOf(str(std::as_const(s), "id"));
        if (k < 0 || k >= elev.size() || !std::isfinite(elev[k]) || stopElev(std::as_const(s), &old)) continue;
        s.mutObject().set(QStringLiteral("elev"), Value(int(std::lround(elev[k]))));
        ++n;
    }
    return n;
}

QString setStopElev(Value &plan, int k, const QString &text)
{
    const QVector<int> st = validStops(plan);
    Value *stops = plan.mutObject().find(QStringLiteral("stops"));
    if (k < 0 || k >= st.size() || !stops) return QStringLiteral("gone");
    json::Object &s = stops->mutArray()[st[k]].mutObject();
    const QString t = text.trimmed();
    if (t.isEmpty()) { s.remove(QStringLiteral("elev")); return {}; }
    bool ok = false;
    const double v = t.toDouble(&ok);
    if (!ok || !std::isfinite(v) || v < -500 || v > 9000) return QStringLiteral("badElev");
    s.set(QStringLiteral("elev"), Value(int(std::lround(v))));
    return {};
}

// ---------------- 高反 ----------------

QVariantMap altitudeInfo(const Value &plan)
{
    constexpr double kHigh = 3000, kStep = 500;
    const json::Array &stops = plan[QStringLiteral("stops")].asArray();
    const QVector<int> st = validStops(plan);
    QStringList tags;
    for (int i = 0; i < st.size(); ++i) tags << QString();
    QVariantList warnings;
    const auto result = [&](bool approx) {
        return QVariantMap{ { QStringLiteral("warnings"), warnings }, { QStringLiteral("tags"), tags }, { QStringLiteral("approx"), approx } };
    };

    double top = -1e9;
    bool dated = true;
    struct Sleep { int k; int nights; double e; };
    QVector<Sleep> sleep;
    for (int k = 0; k < st.size(); ++k) {
        const Value &s = stops[st[k]];
        const QDate a = day(str(s, "arriveDate")), b = day(str(s, "departDate"));
        dated = dated && a.isValid();
        const int n = a.isValid() && b.isValid() ? int(std::max<qint64>(0, a.daysTo(b))) : 1;   // 没写齐日期当作一晚
        double e = 0;
        if (!stopElev(s, &e)) continue;
        top = std::max(top, e);
        if (n > 0) sleep.push_back({ k, n, e });
    }
    if (top < 2500) return result(false);

    for (int i = 1; i < sleep.size(); ++i) {
        const Sleep &a = sleep[i - 1], &b = sleep[i];
        const QDate da = day(str(stops[st[a.k]], "arriveDate")), db = day(str(stops[st[b.k]], "arriveDate"));
        const int days = dated ? int(std::max<qint64>(1, da.daysTo(db))) : 1;
        const double gain = b.e - a.e;
        if (b.e >= kHigh && gain > kStep * days) {
            warnings << QVariantMap{ { QStringLiteral("kind"), QStringLiteral("rise") }, { QStringLiteral("from"), a.k + 1 }, { QStringLiteral("to"), b.k + 1 },
                                     { QStringLiteral("fromE"), a.e }, { QStringLiteral("toE"), b.e }, { QStringLiteral("gain"), gain }, { QStringLiteral("days"), days } };
            tags[b.k] = QStringLiteral("rise");
        }
    }
    // 每晚的睡觉海拔排成一列：3000 m 以上连续 4 晚都在往上，没有休整日
    QVector<Sleep> seq;
    for (const Sleep &x : sleep) for (int n = 0; n < x.nights; ++n) seq.push_back(x);
    int run = 0;
    for (int i = 1; i < seq.size(); ++i) {
        run = seq[i].e >= kHigh && seq[i].e > seq[i - 1].e ? run + 1 : 0;
        if (run >= 4) {
            warnings << QVariantMap{ { QStringLiteral("kind"), QStringLiteral("run") }, { QStringLiteral("stop"), seq[i].k + 1 }, { QStringLiteral("nights"), run } };
            break;
        }
    }
    for (int k = 0; k < st.size(); ++k) {
        double e = 0;
        if (tags[k].isEmpty() && stopElev(stops[st[k]], &e) && e >= kHigh) tags[k] = QStringLiteral("high");
    }
    return result(!dated);
}

} // namespace tp::map::plans
