#include "planedit.h"

#include "plans.h"
#include "roads.h"

#include <algorithm>
#include <cmath>

namespace tp::map::planedit {

using json::Value;

namespace {

const QString kStops = QStringLiteral("stops");
const QString kLeg = QStringLiteral("leg");
const QString kPlans = QStringLiteral("plans");
const QString kId = QStringLiteral("id");

QString str(const Value &o, const QString &key) { return o[key].toString(); }

// JS 的 Number.isFinite(+v)（plans.cpp jsNum 同款）
bool jsNum(const Value &o, const QString &key, double *out)
{
    const Value *v = o.asObject().find(key);
    if (!v || v->isArray() || v->isObject()) return false;
    *out = v->jsToNumber();
    return std::isfinite(*out);
}

bool latLngOf(const Value &s, double *lat, double *lng)
{
    return jsNum(s, QStringLiteral("lat"), lat) && jsNum(s, QStringLiteral("lng"), lng);
}

// 设一个字符串键；空 = 删掉（removeIfEmpty）。返回是否有变化
bool setStr(json::Object &o, const QString &key, const QString &v, bool removeIfEmpty)
{
    const Value *cur = o.find(key);
    if (removeIfEmpty && v.isEmpty()) return o.remove(key);
    if (cur && cur->isString() && cur->toString() == v) return false;
    o.set(key, Value(v));
    return true;
}

json::Array *stopsOf(Value &plan)
{
    json::Object &po = plan.mutObject();
    Value *sv = po.find(kStops);
    if (!sv) { po.set(kStops, Value::array()); sv = po.find(kStops); }
    if (!sv->isArray()) *sv = Value::array();
    return &sv->mutArray();
}

const json::Array &stopsOfC(const Value &plan) { return plan[kStops].asArray(); }

bool inPolygon(double lng, double lat, const double (*poly)[2], int n)
{
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const double xi = poly[i][0], yi = poly[i][1], xj = poly[j][0], yj = poly[j][1];
        if ((yi > lat) != (yj > lat) && lng < (xj - xi) * (lat - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}
bool inBox(double lng, double lat, double w, double s, double e, double n) { return lng >= w && lng <= e && lat >= s && lat <= n; }

// 中国大陆的粗略轮廓（经度, 纬度），Electron CHINA_OUTLINE 原样
const double kChina[][2] = {
    { 135.0, 48.4 }, { 131.3, 47.7 }, { 131.0, 44.9 }, { 131.2, 42.9 }, { 130.6, 42.4 }, { 129.0, 42.4 }, { 126.0, 41.0 },
    { 124.3, 39.9 }, { 121.2, 38.7 }, { 122.6, 37.4 }, { 119.8, 34.9 }, { 121.9, 31.7 }, { 122.3, 29.9 }, { 120.1, 26.4 },
    { 117.2, 23.5 }, { 114.3, 22.2 }, { 111.8, 21.5 }, { 111.4, 19.7 }, { 110.4, 18.1 }, { 108.5, 18.4 }, { 108.6, 19.4 },
    { 109.7, 20.9 }, { 108.1, 21.5 }, { 106.7, 22.0 }, { 105.5, 23.2 }, { 103.5, 22.6 }, { 102.2, 22.4 }, { 101.6, 21.2 },
    { 100.1, 21.5 }, { 99.2, 22.1 }, { 98.7, 24.0 }, { 97.5, 24.8 }, { 98.7, 27.5 }, { 97.3, 28.2 }, { 96.2, 29.2 },
    { 94.0, 27.9 }, { 91.6, 27.9 }, { 88.9, 27.3 }, { 88.1, 27.9 }, { 86.0, 28.0 }, { 84.1, 28.7 }, { 82.0, 30.1 },
    { 81.1, 30.2 }, { 79.0, 31.3 }, { 78.8, 32.5 }, { 78.1, 35.5 }, { 76.5, 35.9 }, { 75.4, 36.9 }, { 74.6, 37.3 },
    { 74.4, 38.6 }, { 73.5, 39.5 }, { 73.8, 40.0 }, { 75.6, 40.6 }, { 76.8, 41.0 }, { 78.3, 41.4 }, { 80.2, 42.1 },
    { 80.2, 42.9 }, { 80.0, 44.8 }, { 82.5, 45.3 }, { 82.3, 46.9 }, { 83.1, 47.2 }, { 85.5, 47.1 }, { 85.7, 48.4 },
    { 87.3, 49.1 }, { 88.0, 48.6 }, { 90.0, 47.9 }, { 91.0, 46.6 }, { 90.5, 45.5 }, { 93.0, 44.9 }, { 95.3, 44.3 },
    { 96.4, 42.7 }, { 100.8, 42.6 }, { 105.0, 41.6 }, { 107.5, 42.4 }, { 110.4, 42.8 }, { 111.9, 43.7 }, { 111.7, 44.3 },
    { 114.5, 45.4 }, { 116.6, 46.4 }, { 119.7, 47.0 }, { 115.6, 47.9 }, { 117.8, 49.5 }, { 119.2, 50.1 }, { 120.0, 51.6 },
    { 121.5, 53.3 }, { 123.5, 53.5 }, { 126.0, 52.8 }, { 127.5, 50.0 }, { 130.6, 48.9 }, { 132.5, 47.7 }, { 134.7, 48.3 },
};
constexpr int kChinaN = int(sizeof(kChina) / sizeof(kChina[0]));

double jsMod(double a, double b) { return std::fmod(a, b); }   // JS 的 %（符号跟被除数）

} // namespace

double round6(double v)
{
    // JS +(v).toFixed(6)：roads::jsToFixed 和 JS 一致
    return roads::jsToFixed(v, 6).toDouble();
}

double normLng(double lng) { return jsMod(jsMod(lng + 540, 360) + 360, 360) - 180; }

Value blankPlan(const QString &id, double createdAtMs)
{
    json::Object o;
    o.set(kId, Value(id));
    o.set(QStringLiteral("name"), Value(QString()));
    o.set(QStringLiteral("type"), Value(QStringLiteral("single")));
    o.set(QStringLiteral("status"), Value(QStringLiteral("planning")));
    o.set(QStringLiteral("color"), Value(QString()));
    o.set(QStringLiteral("startDate"), Value(QString()));
    o.set(QStringLiteral("endDate"), Value(QString()));
    o.set(QStringLiteral("notes"), Value(QString()));
    o.set(QStringLiteral("cover"), Value(QString()));
    o.set(kStops, Value::array());
    o.set(QStringLiteral("createdAt"), Value(createdAtMs));
    return Value(std::move(o));
}

Value newStop(const QString &id, const QString &name, double lat, double lng)
{
    json::Object o;
    o.set(kId, Value(id));
    o.set(QStringLiteral("name"), Value(name));
    o.set(QStringLiteral("lat"), Value(round6(lat)));
    o.set(QStringLiteral("lng"), Value(round6(normLng(lng))));
    o.set(QStringLiteral("arriveDate"), Value(QString()));
    o.set(QStringLiteral("departDate"), Value(QString()));
    o.set(QStringLiteral("schedule"), Value::array());
    return Value(std::move(o));
}

Problems validate(const Value &plan)
{
    Problems p;
    p.nameEmpty = str(plan, QStringLiteral("name")).trimmed().isEmpty();
    const QString s = str(plan, QStringLiteral("startDate")), e = str(plan, QStringLiteral("endDate"));
    p.dateOrder = !s.isEmpty() && !e.isEmpty() && s > e;
    const json::Array &stops = stopsOfC(plan);
    p.noStops = stops.empty();
    for (const Value &st : stops) {
        double lat, lng;
        if (!latLngOf(st, &lat, &lng) || std::abs(lat) > 90 || std::abs(lng) > 180) p.badCoords << str(st, kId);
    }
    return p;
}

QString suggestRegion(double lat, double lng)
{
    if (!std::isfinite(lat) || !std::isfinite(lng)) return {};
    lng = jsMod(lng + 540, 360) - 180;   // 统一到 -180~180（Electron 原样：负数输入时 JS 的 % 也可能得负数，这里一样）
    if (inPolygon(lng, lat, kChina, kChinaN) || inBox(lng, lat, 119.3, 21.8, 122.2, 25.4) || inBox(lng, lat, 113.8, 22.1, 114.5, 22.6))
        return QStringLiteral("中国");
    if ((lat < -10 && lng > 112) || (lng >= 141 && lat < 0 && lat > -50)
        || ((lng > 155 || lng < -140) && lat < 23 && lat > -50 && !inBox(lng, lat, -161, 18, -154, 23)))
        return QStringLiteral("大洋洲");
    if (inBox(lng, lat, 124.5, 33, 146, 46) || inBox(lng, lat, 122.9, 24, 131.5, 33) || inBox(lng, lat, 87.7, 41.5, 120, 52.2)
        || (lng >= 87 && lat >= 50) || (lng >= 60 && lng < 87 && lat >= 55) || (lng > 140 && lat > 42) || (lng < -168 && lat > 60))
        return QStringLiteral("东亚");
    if (lng >= 92.2 && lng < 141 && lat > -11 && lat < 28.6) return QStringLiteral("东南亚");
    if (lng < -25 && lng > -170) {
        if (lat < 12.5 && lng > -82 && !(lat > 7 && lng < -77)) return QStringLiteral("南美洲");
        return QStringLiteral("北美洲");
    }
    if (lng >= -25 && lng <= 60 && (lat >= 35.8 || inBox(lng, lat, 23, 34.8, 26.4, 35.8))) {
        if (lat < 42.2 && lng > 26.5 && !(lat > 40.9 && lng < 29.1)) return QStringLiteral("西亚/中亚");
        if (lat < 44 && lng >= 40) return QStringLiteral("西亚/中亚");
        if (lng >= 46.5 && lat < 51.5) return QStringLiteral("西亚/中亚");
        return QStringLiteral("欧洲");
    }
    if ((lng >= -18 && lng < 32.5 && lat > -35 && lat < 37.5) || inBox(lng, lat, 32.5, 27.5, 34.9, 30.0)
        || (lat < 22 && lng < 43.3 && lat > -35 && lng >= -18) || (lat < 12 && lng < 52 && lat > -35 && lng >= -18)
        || inBox(lng, lat, 42, -26, 51, -11.5))
        return QStringLiteral("非洲");
    if (lng >= 25 && lng <= 92.2 && lat > -1 && lat < 55) return QStringLiteral("西亚/中亚");
    return {};
}

QStringList suggestPlanRegions(const Value &plan)
{
    QSet<QString> set;
    for (const Value &s : stopsOfC(plan)) {
        // Electron suggestRegion(s.lat, s.lng)：+lat / +lng，不是有限数就跳过
        const double lat = s[QStringLiteral("lat")].jsToNumber(), lng = s[QStringLiteral("lng")].jsToNumber();
        const QString r = suggestRegion(lat, lng);
        if (!r.isEmpty()) set.insert(r);
    }
    QStringList out;
    for (const QString &r : plans::allRegions())
        if (set.contains(r)) out << r;
    return out;
}

bool setField(Value &plan, const QString &key, const QString &value)
{
    static const QStringList kKeys{ QStringLiteral("name"), QStringLiteral("type"), QStringLiteral("status"), QStringLiteral("startDate"),
                                    QStringLiteral("endDate"), QStringLiteral("notes"), QStringLiteral("color"), QStringLiteral("markerMode") };
    if (!kKeys.contains(key)) return false;
    json::Object &o = plan.mutObject();
    if (key == QLatin1String("markerMode")) return setStr(o, key, value == QLatin1String("number") ? QString() : value, true);
    // Electron：p.color = v.color || ''（计划的 color 空字符串也写着）
    return setStr(o, key, value, false);
}

bool setRegions(Value &plan, const QStringList &regions)
{
    json::Array arr;
    for (const QString &r : plans::allRegions())
        if (regions.contains(r) && r != plans::regionNone()) arr.push_back(Value(r));
    json::Object &o = plan.mutObject();
    const Value nv(std::move(arr));
    const Value *cur = o.find(QStringLiteral("regions"));
    if (cur && *cur == nv) return false;
    o.set(QStringLiteral("regions"), nv);
    return true;
}

bool setStyle(Value &plan, int stopIndex, const QString &color, const QString &shape, const QString &icon)
{
    if (stopIndex < 0) {
        json::Object &o = plan.mutObject();
        bool ch = setStr(o, QStringLiteral("color"), color, false);   // p.color = v.color || ''
        ch |= setStr(o, QStringLiteral("shape"), shape, true);
        ch |= setStr(o, QStringLiteral("icon"), icon, true);
        return ch;
    }
    json::Array *stops = stopsOf(plan);
    if (stopIndex >= int(stops->size())) return false;
    json::Object &so = (*stops)[stopIndex].mutObject();
    bool ch = setStr(so, QStringLiteral("color"), color, true);
    ch |= setStr(so, QStringLiteral("shape"), shape, true);
    ch |= setStr(so, QStringLiteral("icon"), icon, true);
    return ch;
}

bool setStopField(Value &plan, int i, const QString &key, const QString &value)
{
    static const QStringList kKeys{ QStringLiteral("name"), QStringLiteral("arriveDate"), QStringLiteral("departDate"), QStringLiteral("notes") };
    if (!kKeys.contains(key)) return false;
    json::Array *stops = stopsOf(plan);
    if (i < 0 || i >= int(stops->size())) return false;
    return setStr((*stops)[i].mutObject(), key, value, key == QLatin1String("notes"));
}

bool setStopLatLng(Value &plan, int i, double lat, double lng)
{
    json::Array *stops = stopsOf(plan);
    if (i < 0 || i >= int(stops->size()) || !std::isfinite(lat) || !std::isfinite(lng)) return false;
    json::Object &so = (*stops)[i].mutObject();
    const double la = round6(lat), ln = round6(lng);
    const Value *a = so.find(QStringLiteral("lat")), *b = so.find(QStringLiteral("lng"));
    if (a && b && a->isNumber() && b->isNumber() && a->toNumber() == la && b->toNumber() == ln) return false;
    so.set(QStringLiteral("lat"), Value(la));
    so.set(QStringLiteral("lng"), Value(ln));
    return true;
}

bool setLegMode(Value &plan, int i, const QString &mode0)
{
    json::Array *stops = stopsOf(plan);
    if (i < 0 || i >= int(stops->size())) return false;
    const QString mode = roads::legModeOf(mode0);
    json::Object &so = (*stops)[i].mutObject();
    Value *lv = so.find(kLeg);
    const QString cur = roads::legModeOf(lv && lv->isObject() ? (*lv)[QStringLiteral("mode")].toString() : QString());
    if (cur == mode) return false;
    if (!lv || !lv->isObject()) {
        if (mode == QLatin1String("flight")) return false;
        so.set(kLeg, Value::object());
        lv = so.find(kLeg);
    }
    json::Object &lo = lv->mutObject();
    if (mode == QLatin1String("flight")) lo.remove(QStringLiteral("mode"));
    else lo.set(QStringLiteral("mode"), Value(mode));
    if (!roads::isRoadMode(mode))
        for (const char *k : { "path", "key", "dist", "time" }) lo.remove(QString::fromLatin1(k));
    if (lo.isEmpty()) so.remove(kLeg);
    return true;
}

bool moveStop(Value &plan, int from, int to)
{
    json::Array *stops = stopsOf(plan);
    const int n = int(stops->size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
    Value v = (*stops)[from];
    stops->erase(stops->begin() + from);
    stops->insert(stops->begin() + to, v);
    return true;
}

bool removeStop(Value &plan, int i)
{
    json::Array *stops = stopsOf(plan);
    if (i < 0 || i >= int(stops->size())) return false;
    stops->erase(stops->begin() + i);
    return true;
}

int appendStop(Value &plan, const Value &stop)
{
    json::Array *stops = stopsOf(plan);
    stops->push_back(stop);
    return int(stops->size()) - 1;
}

bool autoRouteOnSecondStop(Value &plan)
{
    if (stopsOfC(plan).size() < 2 || str(plan, QStringLiteral("type")) == QLatin1String("route")) return false;
    plan.mutObject().set(QStringLiteral("type"), Value(QStringLiteral("route")));
    return true;
}

const Value *findPlan(const Value &root, const QString &planId, int *index)
{
    const json::Array &ps = root[kPlans].asArray();
    for (int i = 0; i < int(ps.size()); ++i)
        if (str(ps[i], kId) == planId) { if (index) *index = i; return &ps[i]; }
    if (index) *index = -1;
    return nullptr;
}

bool putPlan(Value &root, const Value &plan)
{
    json::Object &ro = root.mutObject();
    Value *pv = ro.find(kPlans);
    if (!pv) { ro.set(kPlans, Value::array()); pv = ro.find(kPlans); }
    if (!pv->isArray()) *pv = Value::array();
    json::Array &ps = pv->mutArray();
    const QString id = str(plan, kId);
    for (Value &p : ps)
        if (str(p, kId) == id) {
            if (p == plan) return false;
            p = plan;
            return true;
        }
    ps.push_back(plan);
    return true;
}

bool removePlan(Value &root, const QString &planId)
{
    Value *pv = root.mutObject().find(kPlans);
    if (!pv || !pv->isArray()) return false;
    json::Array &ps = pv->mutArray();
    for (auto it = ps.begin(); it != ps.end(); ++it)
        if (str(*it, kId) == planId) { ps.erase(it); return true; }
    return false;
}

double haversineKm(double lat1, double lng1, double lat2, double lng2)
{
    // Electron distKm（R = 6371.0088）
    constexpr double D2R = M_PI / 180;
    const double dLat = (lat2 - lat1) * D2R, dLng = (lng2 - lng1) * D2R;
    const double h = std::pow(std::sin(dLat / 2), 2) + std::cos(lat1 * D2R) * std::cos(lat2 * D2R) * std::pow(std::sin(dLng / 2), 2);
    return 2 * 6371.0088 * std::asin(std::min(1.0, std::sqrt(h)));
}

QVector<LegRow> legRows(const Value &plan)
{
    QVector<LegRow> out;
    if (str(plan, QStringLiteral("type")) != QLatin1String("route")) return out;
    const json::Array &stops = stopsOfC(plan);
    for (int i = 0; i + 1 < int(stops.size()); ++i) {
        LegRow r;
        r.from = i;
        const Value &a = stops[i], &b = stops[i + 1];
        const Value &leg = a[kLeg];
        r.mode = roads::legModeOf(leg.isObject() ? leg[QStringLiteral("mode")].toString() : QString());
        r.road = roads::isRoadMode(r.mode);
        double alat = 0, alng = 0, blat = 0, blng = 0;   // 0.8.11：MSVC C4701（读不出坐标时不会用到）
        const bool ok = latLngOf(a, &alat, &alng) && latLngOf(b, &blat, &blng);
        r.badCoords = !ok;
        if (ok) {
            r.straightKm = haversineKm(alat, alng, blat, blng);
            if (r.road) {
                r.key = roads::legKeyOf(r.mode, alat, alng, blat, blng);
                const QString path = leg.isObject() ? leg[QStringLiteral("path")].toString() : QString();
                const QString key = leg.isObject() ? leg[QStringLiteral("key")].toString() : QString();
                r.computed = !path.isEmpty() && key == r.key;
                if (r.computed) {
                    r.dist = leg[QStringLiteral("dist")].jsToNumber();
                    r.time = leg[QStringLiteral("time")].jsToNumber();
                    if (!std::isfinite(r.dist)) r.dist = 0;
                    if (!std::isfinite(r.time)) r.time = 0;
                }
            }
        }
        out.push_back(r);
    }
    return out;
}

Stats stats(const Value &plan)
{
    Stats s;
    s.stops = int(stopsOfC(plan).size());
    for (const LegRow &r : legRows(plan)) {
        if (r.badCoords) continue;
        const bool drive = r.mode == QLatin1String("drive"), walk = r.mode == QLatin1String("walk");
        if (drive) s.hasDrive = true;
        if (walk) s.hasWalk = true;
        if (r.road && r.computed && r.dist > 0) {
            s.km += r.dist / 1000;
            if (drive) s.driveSec += r.time;
            if (walk) s.walkSec += r.time;
        } else {
            s.km += r.straightKm;
            if (r.road) {
                s.approx = true;
                if (drive) s.driveApprox = true;
                if (walk) s.walkApprox = true;
            }
        }
    }
    return s;
}

QVariantMap stopCounts(const Value &root, const QString &planId, const Value &stop)
{
    const QString sid = str(stop, kId);
    int photos = 0;
    if (!sid.isEmpty())
        for (const Value &ph : root[QStringLiteral("photos")].asArray())
            if (str(ph, QStringLiteral("plan")) == planId && str(ph, QStringLiteral("stop")) == sid) ++photos;
    return { { QStringLiteral("schedule"), int(stop[QStringLiteral("schedule")].asArray().size()) },
             { QStringLiteral("photos"), photos },
             { QStringLiteral("notes"), !str(stop, QStringLiteral("notes")).isEmpty() } };
}

QVariantMap todayStop(const Value &root, const QString &today)
{
    const plans::Now h = plans::pickNow(root, today);
    if (h.kind != QLatin1String("live") || h.planIndex < 0) return {};
    const Value &p = root[kPlans].asArray()[h.planIndex];
    // stopOn：有效站点里最后一个到达日期 ≤ 今天的；没有就第一个
    struct V { double lat, lng; QString name, arrive; };
    QVector<V> vs;
    for (const Value &s : stopsOfC(p)) {
        double lat, lng;
        if (latLngOf(s, &lat, &lng)) vs.push_back({ lat, lng, str(s, QStringLiteral("name")), str(s, QStringLiteral("arriveDate")) });
    }
    if (vs.isEmpty()) return {};
    int i = -1;
    for (int k = 0; k < vs.size(); ++k)
        if (plans::okDate(vs[k].arrive) && vs[k].arrive <= today) i = k;
    const V &v = vs[std::max(0, i)];
    return { { QStringLiteral("name"), v.name }, { QStringLiteral("lat"), v.lat }, { QStringLiteral("lng"), v.lng } };
}

} // namespace tp::map::planedit
