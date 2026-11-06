#include "suncalc.h"

#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QTimeZone>

#include <cmath>

namespace tp::map::sun {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double rad = PI / 180;
constexpr double dayMs = 864e5;
constexpr double J1970 = 2440588, J2000 = 2451545;
constexpr double J0 = 9e-4;

double jsRound(double x) { return std::floor(x + 0.5); }
double toDays(double ms) { return ms / dayMs - 0.5 + J1970 - J2000; }
double fromDays(double d) { return (d + J2000 + 0.5 - J1970) * dayMs; }

double deltaT(double d)
{
    const double y = 2000 + d / 365.2425;
    double t;
    if (y < 1920) { t = y - 1900; return -2.79 + t * (1.494119 + t * (-0.0598939 + t * (0.0061966 - t * 197e-6))); }
    if (y < 1941) { t = y - 1920; return 21.2 + t * (0.84493 + t * (-0.0761 + t * 0.0020936)); }
    if (y < 1961) { t = y - 1950; return 29.07 + t * (0.407 + t * (-1.0 / 233 + t / 2547)); }
    if (y < 1986) { t = y - 1975; return 45.45 + t * (1.067 + t * (-1.0 / 260 - t / 718)); }
    if (y < 2005) { t = y - 2000; return 63.86 + t * (0.3345 + t * (-0.060374 + t * (0.0017275 + t * (651814e-9 + t * 2373599e-11)))); }
    if (y < 2050) { t = y - 2000; return 62.92 + t * (0.32217 + t * 0.005589); }
    t = (y - 1820) / 100;
    return -20 + 32 * t * t - 0.5628 * (2150 - y);
}
double toDaysTT(double d) { return d + deltaT(d) / 86400; }
double altitude(double H, double phi, double dec) { return std::asin(std::sin(phi) * std::sin(dec) + std::cos(phi) * std::cos(dec) * std::cos(H)); }
double siderealTime(double d, double lw) { return rad * (280.46061837 + 360.98564736629 * d) - lw; }

struct Coords { double ra, dec; };
Coords sunCoords(double d)
{
    const double t = d / 36525;
    const double L0 = rad * (280.46646 + t * (36000.76983 + t * 3032e-7));
    const double M = rad * (357.52911 + t * (35999.05029 - t * 1537e-7));
    const double sinM = std::sin(M), cosM = std::cos(M);
    const double C = rad * ((1.914602 - t * (0.004817 + t * 14e-6)) * sinM + (0.019993 - 101e-6 * t) * 2 * sinM * cosM + 289e-6 * sinM * (3 - 4 * sinM * sinM));
    const double Om = rad * (125.04 - 1934.136 * t);
    const double L = L0 + C - rad * (0.00569 + 0.00478 * std::sin(Om));
    const double e = rad * (23.439291 - t * (0.0130042 + t * (16e-8 - t * 504e-9))) + rad * 0.00256 * std::cos(Om);
    return { std::atan2(std::cos(e) * std::sin(L), std::cos(L)), std::asin(std::sin(e) * std::sin(L)) };
}
double wrapPi(double a) { return a - 2 * PI * jsRound(a / (2 * PI)); }
double solarTransit(double dt, double lw)
{
    for (int i = 0; i < 3; ++i) dt -= wrapPi(siderealTime(dt, lw) - sunCoords(toDaysTT(dt)).ra) / (2 * PI);
    return dt;
}
double getSetJ(double h0, double dt, int sign, double lw, double phi, double decT)
{
    const double cosH0 = (std::sin(h0) - std::sin(phi) * std::sin(decT)) / (std::cos(phi) * std::cos(decT));
    if (cosH0 < -1 || cosH0 > 1) return std::nan("");
    double d = dt + sign * std::acos(cosH0) / (2 * PI);
    for (int i = 0; i < 2; ++i) {
        const Coords c = sunCoords(toDaysTT(d));
        const double H = wrapPi(siderealTime(d, lw) - c.ra);
        const double sinH = std::cos(phi) * std::cos(c.dec) * std::sin(H);
        if (std::abs(sinH) < 1e-6) break;
        d += (altitude(H, phi, c.dec) - h0) / (2 * PI * sinH);
    }
    return d;
}

// QTimeZone 在 Windows 上要查注册表，一个计划几十站都查一遍会慢：记住查过的
const QTimeZone &zone(const QString &tz)
{
    static QHash<QString, QTimeZone> cache;
    auto it = cache.find(tz);
    if (it == cache.end()) it = cache.insert(tz, QTimeZone(tz.toLatin1()));
    return *it;
}

QString hm(double ms, const QString &tz, double lng, bool *approx)
{
    const qint64 t = qint64(std::floor(ms));
    if (!tz.isEmpty() && zone(tz).isValid()) {
        *approx = false;
        return QDateTime::fromMSecsSinceEpoch(t, zone(tz)).toString(QStringLiteral("HH:mm"));
    }
    *approx = true;   // Electron：Intl 认不出时区时也退回按经度估算
    const qint64 l = t + qint64(jsRound(lng / 15)) * 3600000;
    return QDateTime::fromMSecsSinceEpoch(l, QTimeZone::UTC).toString(QStringLiteral("HH:mm"));
}

} // namespace

Times info(double lat, double lng, const QString &date, const QString &tz)
{
    Times r;
    static const QRegularExpression re(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    if (!re.match(date).hasMatch() || !std::isfinite(lat) || !std::isfinite(lng)) return r;
    const QDateTime utc = QDateTime::fromString(date + QStringLiteral("T12:00:00Z"), Qt::ISODate);
    if (!utc.isValid()) return r;
    // 取当地正午（按经度估）那一刻
    const double noon = double(utc.toMSecsSinceEpoch()) - lng / 15 * 3600e3;

    const double lw = rad * -lng, phi = rad * lat;
    const double dt = solarTransit(jsRound(toDays(noon) - J0 - lw / (2 * PI)) + J0 + lw / (2 * PI), lw);
    const double dec = sunCoords(toDaysTT(dt)).dec;
    const double h0 = -0.833 * rad;
    const double jr = getSetJ(h0, dt, -1, lw, phi, dec), js = getSetJ(h0, dt, 1, lw, phi, dec);
    r.ok = true;
    if (std::isnan(jr)) {
        r.polar = altitude(0, phi, dec) > h0 ? QStringLiteral("day") : QStringLiteral("night");
        return r;
    }
    const double rise = fromDays(jr), set = fromDays(js);
    r.rise = hm(rise, tz, lng, &r.approx);
    r.set = hm(set, tz, lng, &r.approx);
    r.dayMin = int(jsRound((set - rise) / 60000));
    return r;
}

} // namespace tp::map::sun
