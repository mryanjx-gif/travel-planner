#include "roads.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tp::map::roads {

using LL = geo::LatLng;

QString jsToFixed(double v, int digits)
{
    if (!std::isfinite(v)) return std::isnan(v) ? QStringLiteral("NaN") : v > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
    if (v == 0) v = 0;   // -0 → "0.00000"（JS 一样）
    // Qt 的 'f' 格式用 double-conversion 的定点模式：按 double 的精确值正确舍入，正好一半时进位（按绝对值），
    // 和 JS toFixed 的"两个一样近取大的"一致（tst_map 用 Node 的结果对照）
    return QString::number(v, 'f', digits);
}

QVector<LL> decodePolyline(const QString &str)
{
    QVector<LL> pts;
    const QByteArray b = str.toLatin1();
    int i = 0;
    long long lat = 0, lng = 0;
    while (i < b.size()) {
        for (int k = 0; k < 2; ++k) {
            int shift = 0;
            long long res = 0;
            int c;
            // Electron：do { b = str.charCodeAt(i++) - 63; … } while (b >= 32 && i <= str.length)（读过头时 charCodeAt 是 NaN → 0）
            do { c = (i < b.size() ? b[i] : 63) - 63; ++i; res |= (long long)(c & 31) << shift; shift += 5; }
            while (c >= 32 && i <= b.size() && shift < 60);
            const long long d = (res & 1) ? ~(res >> 1) : (res >> 1);
            if (k == 0) lat += d; else lng += d;
        }
        pts.push_back({ lat / 1e5, lng / 1e5 });
    }
    return pts;
}

QString encodePolyline(const QVector<LL> &pts)
{
    // Electron encodePolyline：Math.round（正好一半时往正无穷）+ 32 位整数运算
    QString out;
    int32_t pLat = 0, pLng = 0;
    const auto enc = [&out](int32_t v) {
        uint32_t u = v < 0 ? ~(uint32_t(v) << 1) : (uint32_t(v) << 1);
        while (u >= 32) { out += QLatin1Char(char((32 | (u & 31)) + 63)); u >>= 5; }
        out += QLatin1Char(char(u + 63));
    };
    for (const LL &p : pts) {
        const int32_t a = int32_t(std::floor(p.lat * 1e5 + 0.5)), b = int32_t(std::floor(p.lng * 1e5 + 0.5));
        enc(a - pLat);
        enc(b - pLng);
        pLat = a;
        pLng = b;
    }
    return out;
}

QVector<LL> simplify(const QVector<LL> &pts, double tol)
{
    if (pts.size() < 3) return pts;
    std::vector<char> keep(pts.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<int, int>> stack{ { 0, int(pts.size()) - 1 } };
    while (!stack.empty()) {
        const auto [s, e] = stack.back();
        stack.pop_back();
        const double c = std::cos(((pts[s].lat + pts[e].lat) / 2) * geo::D2R);
        const double ax = pts[s].lng * c, ay = pts[s].lat, bx = pts[e].lng * c, by = pts[e].lat;
        const double dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
        double best = -1;
        int bi = -1;
        for (int i = s + 1; i < e; ++i) {
            const double px = pts[i].lng * c, py = pts[i].lat;
            double t = L2 ? ((px - ax) * dx + (py - ay) * dy) / L2 : 0;
            t = std::max(0.0, std::min(1.0, t));
            const double d = std::hypot(px - ax - t * dx, py - ay - t * dy);
            if (d > best) { best = d; bi = i; }
        }
        if (best > tol) { keep[bi] = 1; stack.push_back({ s, bi }); stack.push_back({ bi, e }); }
    }
    QVector<LL> out;
    for (int i = 0; i < pts.size(); ++i)
        if (keep[i]) out.push_back(pts[i]);
    return out;
}

QString legModeOf(const QString &mode)
{
    static const QStringList modes = { "flight", "train", "ferry", "drive", "walk" };
    return modes.contains(mode) ? mode : QStringLiteral("flight");
}

bool isRoadMode(const QString &mode) { return mode == QLatin1String("drive") || mode == QLatin1String("walk"); }

QString legKeyOf(const QString &mode, double aLat, double aLng, double bLat, double bLng)
{
    return mode + u':' + QStringList{ jsToFixed(aLat, 5), jsToFixed(aLng, 5), jsToFixed(bLat, 5), jsToFixed(bLng, 5) }.join(u',');
}

namespace {
// JS 的 Number.isFinite(+v)（和 tripdata.cpp 的 validStops 一样）
bool jsNum(const json::Object &o, const char *key, double *out)
{
    const json::Value *v = o.find(QString::fromLatin1(key));
    if (!v || v->isArray() || v->isObject()) return false;
    *out = v->jsToNumber();
    return std::isfinite(*out);
}
QString str(const json::Object &o, const char *key)
{
    const json::Value *v = o.find(QString::fromLatin1(key));
    return v ? v->toString() : QString();
}
struct LegInfo { QString mode, path, key; };
LegInfo legOf(const json::Object &so)
{
    // 原生格式：leg 对象；也认 Electron 的平铺写法（测试数据方便）
    if (const json::Value *leg = so.find(QStringLiteral("leg")); leg && leg->isObject()) {
        const json::Object &lo = leg->asObject();
        return { legModeOf(str(lo, "mode")), str(lo, "path"), str(lo, "key") };
    }
    return { legModeOf(str(so, "legMode")), str(so, "legPath"), str(so, "legKey") };
}
struct VStop { int index; double lat, lng, order; };
// 有效站点，按 order 稳定排序（原生格式没有 order：数组顺序）——和 TripData::build 一样
QVector<VStop> validStops(const json::Array &stops)
{
    QVector<VStop> out;
    for (int i = 0; i < int(stops.size()); ++i) {
        const json::Object &so = stops[i].asObject();
        VStop v{ i, 0, 0, double(i + 1) };
        if (!jsNum(so, "lat", &v.lat) || !jsNum(so, "lng", &v.lng)) continue;
        if (so.contains(QStringLiteral("order"))) { double o = 0; v.order = jsNum(so, "order", &o) ? o : 0; }
        out.push_back(v);
    }
    std::stable_sort(out.begin(), out.end(), [](const VStop &a, const VStop &b) { return a.order < b.order; });
    return out;
}
} // namespace

QVector<Pending> pendingLegs(const json::Value &root, const QSet<QString> &failed)
{
    QVector<Pending> out;
    for (const json::Value &pv : root[QStringLiteral("plans")].asArray()) {
        const json::Object &po = pv.asObject();
        if (str(po, "type") != QLatin1String("route")) continue;
        const json::Value *sv = po.find(QStringLiteral("stops"));
        if (!sv) continue;
        const json::Array &stops = sv->asArray();
        const QVector<VStop> vs = validStops(stops);
        for (int i = 0; i + 1 < vs.size(); ++i) {
            const VStop &a = vs[i], &b = vs[i + 1];
            const LegInfo leg = legOf(stops[a.index].asObject());
            if (!isRoadMode(leg.mode)) continue;
            const QString key = legKeyOf(leg.mode, a.lat, a.lng, b.lat, b.lng);
            if (!leg.path.isEmpty() && leg.key == key) continue;
            if (failed.contains(key)) continue;
            Pending p;
            p.planId = str(po, "id");
            p.planName = str(po, "name");
            p.stopIndex = a.index;
            p.legNo = i;
            p.mode = leg.mode;
            p.key = key;
            p.aLat = a.lat; p.aLng = a.lng; p.bLat = b.lat; p.bLng = b.lng;
            out.push_back(p);
        }
    }
    return out;
}

bool applyLeg(json::Value &root, const Pending &leg, const QString &path, double dist, double time)
{
    // 请求期间站点可能又被挪了 / 改了方式 / 整份数据换了（撤销）：重新找一遍，还对得上才写
    json::Value *plans = root.mutObject().find(QStringLiteral("plans"));
    if (!plans || !plans->isArray()) return false;
    for (json::Value &pv : plans->mutArray()) {
        if (str(pv.asObject(), "id") != leg.planId) continue;
        json::Value *sv = pv.mutObject().find(QStringLiteral("stops"));
        if (!sv || !sv->isArray()) return false;
        const QVector<VStop> vs = validStops(sv->asArray());
        for (int i = 0; i + 1 < vs.size(); ++i) {
            if (vs[i].index != leg.stopIndex) continue;
            const VStop &a = vs[i], &b = vs[i + 1];
            json::Object &so = sv->mutArray()[a.index].mutObject();
            const LegInfo cur = legOf(so);
            if (cur.mode != leg.mode || legKeyOf(cur.mode, a.lat, a.lng, b.lat, b.lng) != leg.key) return false;
            json::Value *lv = so.find(QStringLiteral("leg"));
            if (!lv || !lv->isObject()) return false;   // 原生格式里道路方式一定写在 leg 里
            json::Object &lo = lv->mutObject();
            lo.set(QStringLiteral("path"), json::Value(path));
            lo.set(QStringLiteral("dist"), json::Value(dist));
            lo.set(QStringLiteral("time"), json::Value(time));
            lo.set(QStringLiteral("key"), json::Value(leg.key));
            return true;
        }
        return false;
    }
    return false;
}

QUrl osrmUrl(const QString &mode, double aLat, double aLng, double bLat, double bLng)
{
    // Electron OSRM_BASE + 经度,纬度;经度,纬度（各 6 位小数）?overview=full&geometries=polyline&steps=false
    const QString base = mode == QLatin1String("walk") ? QStringLiteral("https://routing.openstreetmap.de/routed-foot/route/v1/foot/")
                                                       : QStringLiteral("https://routing.openstreetmap.de/routed-car/route/v1/driving/");
    const QString coords = jsToFixed(aLng, 6) + u',' + jsToFixed(aLat, 6) + u';' + jsToFixed(bLng, 6) + u',' + jsToFixed(bLat, 6);
    return QUrl(base + coords + QStringLiteral("?overview=full&geometries=polyline&steps=false"));
}

OsrmResult parseOsrm(const QByteArray &body, int httpStatus)
{
    OsrmResult r;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        r.kind = OsrmResult::BadReply;
        r.detail = QString::number(httpStatus);
        return r;
    }
    const QJsonObject j = doc.object();
    const QString code = j.value(QLatin1String("code")).toString();
    const QJsonArray routes = j.value(QLatin1String("routes")).toArray();
    if (code != QLatin1String("Ok") || routes.isEmpty()) {
        if (code == QLatin1String("NoRoute")) r.kind = OsrmResult::NoRoute;
        else if (code == QLatin1String("TooBig")) r.kind = OsrmResult::TooBig;
        else {
            r.kind = OsrmResult::ServiceError;
            const QString msg = j.value(QLatin1String("message")).toString();
            r.detail = !msg.isEmpty() ? msg : !code.isEmpty() ? code : QString::number(httpStatus);
        }
        return r;
    }
    const QJsonObject route = routes.at(0).toObject();
    const QVector<LL> pts = simplify(decodePolyline(route.value(QLatin1String("geometry")).toString()), kSimplifyDeg);
    r.kind = OsrmResult::Ok;
    r.path = encodePolyline(pts);
    // Math.round
    r.dist = std::floor(route.value(QLatin1String("distance")).toDouble() + 0.5);
    r.time = std::floor(route.value(QLatin1String("duration")).toDouble() + 0.5);
    return r;
}

} // namespace tp::map::roads
