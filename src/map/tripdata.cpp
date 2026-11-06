#include "tripdata.h"

#include "roads.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace tp::map {

namespace {
using geo::LatLng;
using LL = geo::LatLng;
using geo::normLng;
using geo::unwrapTo;

// Electron STATUS 默认色
QString statusColor(const QString &status)
{
    if (status == QLatin1String("completed")) return QStringLiteral("#2b8a3e");
    if (status == QLatin1String("wish")) return QStringLiteral("#7b61b5");
    return QStringLiteral("#e8590c");   // planning / 其他
}

// 这一段（a→b）有可用的道路折线就返回点列表，否则空（→ 大圆弧 / 直线）。Electron roadLegPts；编解码见 roads.h
QVector<LL> roadLegPts(const TripStop &a, const TripStop &b)
{
    const QString m = roads::legModeOf(a.legMode);
    if (!roads::isRoadMode(m) || a.legPath.isEmpty() || a.legKey != roads::legKeyOf(m, a.lat, a.lng, b.lat, b.lng))
        return {};
    const QVector<LL> pts = roads::decodePolyline(a.legPath);
    for (const LL &p : pts)
        if (!std::isfinite(p.lat) || !std::isfinite(p.lng)) return {};
    return pts.size() >= 2 ? pts : QVector<LL>{};
}

QJsonArray lngLatArr(const QVector<LL> &pts)
{
    QJsonArray a;
    for (const LL &p : pts) a.append(QJsonArray{ p.lng, p.lat });
    return a;
}
} // namespace

namespace {
// JS 的 Number.isFinite(+v)：没有这个键 = undefined → NaN；null → 0；字符串按数字解析（空串 0）
bool jsNum(const json::Object &o, const char *key, double *out)
{
    const json::Value *v = o.find(QString::fromLatin1(key));
    if (!v) return false;
    if (v->isArray() || v->isObject()) return false;   // +[] / +{} 只有极少数情况是数字，按无效处理
    *out = v->jsToNumber();
    return std::isfinite(*out);
}
QString str(const json::Object &o, const char *key)
{
    const json::Value *v = o.find(QString::fromLatin1(key));
    return v ? v->toString() : QString();
}
} // namespace

namespace {
QString featureCollection(const QString &features)
{
    return QStringLiteral("{\"type\":\"FeatureCollection\",\"features\":[") + features + QStringLiteral("]}");
}
QString compactJson(const QJsonObject &o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); }
} // namespace

QString TripData::planColor(const TripPlan &p) { return p.color.isEmpty() ? statusColor(p.status) : p.color; }
QString TripData::stopColor(const TripPlan &p, const TripStop &s) { return s.color.isEmpty() ? planColor(p) : s.color; }

// 一个计划：读出站点（Electron validStops，按 order 稳定排序），算路线几何和给地图的要素。只依赖这个计划自己的数据
TripData::Computed TripData::compute(const json::Value &pv)
{
    Computed c;
    c.src = pv;
    const json::Object &po = pv.asObject();
    TripPlan &p = c.plan;
    p.id = str(po, "id");
    p.name = str(po, "name");
    p.status = str(po, "status");
    p.type = str(po, "type");
    p.color = str(po, "color");
    p.startDate = str(po, "startDate");
    p.icon = str(po, "icon");
    p.shape = str(po, "shape");
    p.markerMode = str(po, "markerMode");
    if (const json::Value *stops = po.find(QStringLiteral("stops"))) {
        int index = 0;
        for (const json::Value &sv : stops->asArray()) {
            const json::Object &so = sv.asObject();
            TripStop s;
            ++index;
            if (!jsNum(so, "lat", &s.lat) || !jsNum(so, "lng", &s.lng)) continue;   // validStops
            // Electron：按 order 排（没有算 0）；原生版：数组顺序就是站点顺序（没有 order）
            double ord = 0;
            s.order = so.contains(QStringLiteral("order")) ? (jsNum(so, "order", &ord) ? ord : 0) : double(index);
            s.name = str(so, "name");
            s.color = str(so, "color");
            s.icon = str(so, "icon");
            s.shape = str(so, "shape");
            if (const json::Value *leg = so.find(QStringLiteral("leg")); leg && leg->isObject()) {
                const json::Object &lo = leg->asObject();
                s.legMode = str(lo, "mode");
                s.legPath = str(lo, "path");
                s.legKey = str(lo, "key");
            } else {
                s.legMode = str(so, "legMode");
                s.legPath = str(so, "legPath");
                s.legKey = str(so, "legKey");
            }
            p.stops.push_back(s);
        }
    }
    std::stable_sort(p.stops.begin(), p.stops.end(), [](const TripStop &a, const TripStop &b) { return a.order < b.order; });

    // 几何（0.9.1 之前在 buildGeometry 里对所有计划一起算，逐个计划的结果和那时一样）
    TripPlan &plan = p;
    const int dash = plan.status == QLatin1String("wish") ? 2 : plan.status != QLatin1String("completed") ? 1 : 0;
    const bool wish = plan.status == QLatin1String("wish");
    const auto &stops = plan.stops;
    const bool route = plan.type == QLatin1String("route") && stops.size() > 1;
    QVector<LL> pos;
    QStringList planFeats;
    plan.dash = dash;
    c.route = route;
    if (route) {
        QVector<QVector<LL>> segments;
        for (int i = 0; i < stops.size(); ++i) {
            const TripStop &s = stops[i];
            if (i == 0) { pos.push_back({ s.lat, normLng(s.lng) }); continue; }
            const LL prev = pos[i - 1];
            const LL target{ s.lat, unwrapTo(s.lng, prev.lng) };
            const QVector<LL> road = roadLegPts(stops[i - 1], s);
            QVector<LL> seg;
            if (!road.isEmpty()) { seg << prev << road << LL{ s.lat, s.lng }; ++c.roadLegs; }
            else for (const LL &pt : geo::greatCircle(prev, target)) seg.push_back(pt);
            double ref = prev.lng;
            QVector<LL> segPts{ prev };
            for (int j = 1; j < seg.size(); ++j) {
                const LL pt{ seg[j].lat, unwrapTo(seg[j].lng, ref) };
                ref = pt.lng;
                segPts.push_back(pt);
            }
            pos.push_back(segPts.last());
            segments.push_back(segPts);
        }
        QVector<LL> whole{ pos.first() };
        for (const auto &seg : segments) for (int j = 1; j < seg.size(); ++j) whole.push_back(seg[j]);
        // 白色衬边：整条路线一条连续实线（k='c'）
        const QJsonObject edge{ { "type", "Feature" },
            { "properties", QJsonObject{ { "k", "c" }, { "pid", plan.id }, { "sel", 0 } } },
            { "geometry", QJsonObject{ { "type", "LineString" }, { "coordinates", lngLatArr(whole) } } } };
        c.lines.append(edge);
        planFeats << compactJson(edge);
        // 每一段颜色跟起点（A→B 用 A 的颜色）
        for (int i = 0; i < segments.size(); ++i) {
            const QJsonObject seg{ { "type", "Feature" },
                { "properties", QJsonObject{ { "k", "s" }, { "pid", plan.id }, { "sel", 0 }, { "dash", dash },
                                             { "color", stopColor(plan, stops[i]) } } },
                { "geometry", QJsonObject{ { "type", "LineString" }, { "coordinates", lngLatArr(segments[i]) } } } };
            c.lines.append(seg);
            planFeats << compactJson(seg);
            ++c.legs;
        }
        plan.segments = segments;
        plan.west = plan.south = 1e9;
        plan.east = plan.north = -1e9;
        for (const LL &pt : whole) {
            plan.west = std::min(plan.west, pt.lng); plan.east = std::max(plan.east, pt.lng);
            plan.south = std::min(plan.south, pt.lat); plan.north = std::max(plan.north, pt.lat);
        }
    } else {
        for (const TripStop &s : stops) pos.push_back({ s.lat, normLng(s.lng) });
    }
    c.feats = planFeats.join(u',');
    for (const LL &pt : pos) plan.markerPos.push_back({ pt.lat, normLng(pt.lng) });
    for (int i = 0; i < stops.size(); ++i)
        c.points.append(QJsonObject{ { "type", "Feature" },
            { "properties", QJsonObject{ { "color", stopColor(plan, stops[i]) }, { "wish", wish }, { "route", route },
                                         { "n", i + 1 }, { "plan", plan.name } } },
            { "geometry", QJsonObject{ { "type", "Point" }, { "coordinates", QJsonArray{ normLng(pos[i].lng), pos[i].lat } } } } });
    return c;
}

// 0.11.0 没关联季节的想去：颜色往白色混一半
static QString fadedColor(const QString &c)
{
    if (c.size() != 7 || c[0] != u'#') return c;   // 这个库不链接 QtGui；颜色都是 #rrggbb
    QString out = QStringLiteral("#");
    for (int i = 1; i < 7; i += 2) {
        bool ok = false;
        const int v = c.mid(i, 2).toInt(&ok, 16);
        if (!ok) return c;
        out += QStringLiteral("%1").arg((v + 255) / 2, 2, 16, QLatin1Char('0'));
    }
    return out;
}

static void fadePlan(TripPlan &p, QString &feats)
{
    for (TripStop &s : p.stops) s.color = fadedColor(TripData::stopColor(p, s));
    p.color = fadedColor(TripData::planColor(p));
    static const QRegularExpression re(QStringLiteral("\"color\":\"([^\"]+)\""));
    QString out;
    qsizetype at = 0;
    for (auto it = re.globalMatch(feats); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(feats).mid(at, m.capturedStart() - at);
        out += QStringLiteral("\"color\":\"%1\"").arg(fadedColor(m.captured(1)));
        at = m.capturedEnd();
    }
    feats = out + QStringView(feats).mid(at);
}

void TripData::build(const json::Value &root, const std::function<bool(const json::Value &plan)> &visible,
                     const std::function<int(const json::Value &plan)> &tone)
{
    const QVector<UnlockModel::Manual> oldManual = m_manual;
    m_plans.clear();
    m_shown.clear();
    m_planFeats.clear();
    m_manual.clear();
    m_completed.clear();
    m_legs = m_stopCount = m_roadLegs = m_routeCount = 0;
    m_recomputed = 0;
    for (const json::Value &rv : root[QStringLiteral("visitedRegions")].asArray()) {
        const json::Object &r = rv.asObject();
        const json::Value *id = r.find(QStringLiteral("id"));
        if (!id || !id->isString()) continue;
        UnlockModel::Manual m;
        m.id = id->toString();
        double y = 0;
        if (jsNum(r, "year", &y) && y != 0) m.year = int(y);   // Electron：manual.year ? String(year) : '9999'
        m_manual.push_back(m);
    }
    bool unlockChanged = m_manual.size() != oldManual.size();
    for (int i = 0; !unlockChanged && i < m_manual.size(); ++i)
        unlockChanged = m_manual[i].id != oldManual[i].id || m_manual[i].year != oldManual[i].year;

    QHash<QString, Computed> next;
    QSet<QString> seen;
    QVector<QString> completedKey;
    for (const json::Value &pv : root[QStringLiteral("plans")].asArray()) {
        const QString id = pv[QStringLiteral("id")].toString();
        // 没有 id 或 id 重复的计划不进缓存（每次现算），其余按 id 找上次算好的
        const bool cacheable = !id.isEmpty() && !seen.contains(id);
        if (cacheable) seen.insert(id);
        Computed c;
        const auto it = cacheable ? m_cache.constFind(id) : m_cache.constEnd();
        if (it != m_cache.constEnd() && (it->src.sharesWith(pv) || it->src == pv)) {
            c = *it;
        } else {
            c = compute(pv);
            ++m_recomputed;
            if (c.plan.status == QLatin1String("completed")) unlockChanged = true;   // 已完成计划的站点可能变了
        }
        if (cacheable) next.insert(id, c);
        const TripPlan &p = c.plan;
        if (p.status == QLatin1String("completed")) {
            UnlockModel::PlanStops ps;
            ps.name = p.name;
            ps.startDate = p.startDate;
            for (const TripStop &s : p.stops) ps.stops.push_back({ s.lat, s.lng });
            m_completed.push_back(ps);
            completedKey.push_back(cacheable ? id : QString());
        }
        if (visible && !visible(pv)) continue;
        if (tone) {
            c.plan.tone = tone(pv);
            if (c.plan.tone < 0) fadePlan(c.plan, c.feats);
        }
        if (c.route) ++m_routeCount;
        m_legs += c.legs;
        m_roadLegs += c.roadLegs;
        m_stopCount += int(p.stops.size());
        m_plans.push_back(p);
        m_planFeats.push_back(c.feats);
        m_shown.push_back(std::move(c));
    }
    m_cache = std::move(next);
    // 已完成的计划少了 / 多了 / 换了顺序 / 没有 id 的（每次现算，当作变了）也算变了
    if (completedKey != m_completedKey || completedKey.contains(QString())) unlockChanged = true;
    m_completedKey = completedKey;
    m_unlockChanged = unlockChanged;
}

QJsonObject TripData::routesGeoJson() const
{
    QJsonArray lines;
    for (const Computed &c : m_shown) for (const QJsonValue &v : c.lines) lines.append(v);
    return QJsonObject{ { "type", "FeatureCollection" }, { "features", lines } };
}

QJsonObject TripData::stopsGeoJson() const
{
    QJsonArray points;
    for (const Computed &c : m_shown) for (const QJsonValue &v : c.points) points.append(v);
    return QJsonObject{ { "type", "FeatureCollection" }, { "features", points } };
}

QVector<geo::LatLng> TripData::allStops() const
{
    QVector<geo::LatLng> out;
    for (const TripPlan &p : m_plans)
        for (const TripStop &s : p.stops) out.push_back({ s.lat, normLng(s.lng) });
    return out;
}


QString TripData::routesJsonExcept(const QString &planId) const
{
    QStringList parts;
    for (int i = 0; i < m_plans.size(); ++i)
        if (m_plans[i].id != planId && !m_planFeats[i].isEmpty()) parts << m_planFeats[i];
    return featureCollection(parts.join(u','));
}

QString TripData::routesJsonOnly(const QString &planId) const
{
    if (planId.isEmpty()) return featureCollection(QString());
    QStringList parts;
    for (int i = 0; i < m_plans.size(); ++i)
        if (m_plans[i].id == planId && !m_planFeats[i].isEmpty()) parts << m_planFeats[i];
    return featureCollection(parts.join(u','));
}

} // namespace tp::map
