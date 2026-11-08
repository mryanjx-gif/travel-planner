// MapController：站点编辑器的草稿、框住一组点、比例尺（0.8.9，M-23 / M-25 / M-30）。
#include "mapcontroller.h"
#include "mapcontroller_p.h"

#include "markerimages.h"
#include "markers.h"

#include <cmath>

namespace tp::map {

QString MapController::draftJson() const { return m_draftJson.isEmpty() ? detail::emptyFc() : m_draftJson; }

void MapController::setDraft(const QString &planId, const json::Value &plan)
{
    const bool idChanged = planId != m_draftId;
    m_draftId = planId;
    m_draftPlan = plan;
    if (idChanged) {
        m_markers.setForceDim(!planId.isEmpty());
        rebuildData();   // 平时那一份里去掉 / 放回这个计划
    }
    rebuildDraft();
}

void MapController::clearDraft()
{
    if (m_draftId.isEmpty()) return;
    m_draftId.clear();
    m_draftPlan = json::Value();
    m_markers.setForceDim(false);
    rebuildData();
    rebuildDraft();
}

bool MapController::leaveUnlock()
{
    if (m_base != QLatin1String("unlock") || m_tilesOff) return false;
    setBase(m_tileBase);
    emit leftUnlock(m_tileBase);
    return true;
}

bool MapController::fitCountry(const QString &code)
{
    ensureUnlock();
    if (!m_unlockModel.isLoaded()) return false;
    double w, s, e, n;
    if (!m_unlockModel.countryBox(code, &w, &s, &e, &n)) return false;
    const geo::FitResult r = geo::fitBounds(fitCam(), w, s, e, n, fitPad(), 6);
    if (!r.ok) return false;
    flyTo(r.lat, r.lng, r.zoom, 1400);
    return true;
}

void MapController::rebuildDraft()
{
    m_draftJson.clear();
    m_draftMarkers.clear();
    if (!m_draftId.isEmpty() && m_draftPlan.isObject()) {
        json::Object ro;
        ro.set(QStringLiteral("plans"), json::Value(json::Array{ m_draftPlan }));
        TripData d;
        d.build(json::Value(std::move(ro)));
        m_draftJson = d.routesJsonOnly(m_draftId);
        if (!d.plans().isEmpty()) {
            const TripPlan &p = d.plans().first();
            // TripPlan.stops 只含坐标有效的站点（原生数据没有 order，顺序 = 数组顺序）：对回 stops 数组的下标
            QVector<int> raw;
            const json::Array &stops = m_draftPlan[QStringLiteral("stops")].asArray();
            for (int i = 0; i < int(stops.size()); ++i) {
                const double lat = stops[i][QStringLiteral("lat")].jsToNumber(), lng = stops[i][QStringLiteral("lng")].jsToNumber();
                if (std::isfinite(lat) && std::isfinite(lng)) raw << i;
            }
            const int n = int(p.stops.size());
            const bool wish = p.status == QLatin1String("wish");
            for (int i = 0; i < n && i < p.markerPos.size(); ++i) {
                const markers::Style st = markers::stopStyle(p, i, n);
                const markers::Geom g = markers::geomFor(st, MarkerImages::iconReady());
                m_draftMarkers << QVariantMap{ { QStringLiteral("stop"), i < raw.size() ? raw[i] : i },
                                               { QStringLiteral("lat"), p.markerPos[i].lat },
                                               { QStringLiteral("lng"), p.markerPos[i].lng },
                                               { QStringLiteral("img"), MarkerImages::stopId(st, wish, m_markers.dpr(), true) },
                                               { QStringLiteral("w"), g.w }, { QStringLiteral("h"), g.h },
                                               { QStringLiteral("ax"), g.ax }, { QStringLiteral("ay"), g.ay },
                                               { QStringLiteral("name"), p.stops[i].name },
                                               { QStringLiteral("num"), i + 1 } };
            }
        }
    }
    emit draftChanged();
}

void MapController::fitPoints(const QVariantList &pts, double maxZoom, int durationMs)
{
    if (pts.isEmpty()) return;
    QVector<geo::LatLng> ll;
    for (const QVariant &v : pts) {
        const QVariantList p = v.toList();
        if (p.size() < 2) continue;
        ll.push_back({ p[0].toDouble(), p[1].toDouble() });
    }
    if (ll.isEmpty()) return;
    const double ref = ll.first().lng;
    double w = 1e9, s = 1e9, e = -1e9, n = -1e9;
    for (const geo::LatLng &p : ll) {
        const double lng = geo::unwrapTo(p.lng, ref);
        w = std::min(w, lng); e = std::max(e, lng); s = std::min(s, p.lat); n = std::max(n, p.lat);
    }
    const geo::FitResult r = geo::fitBounds(m_cam, w, s, e, n, fitPad(), maxZoom > 0 ? maxZoom : planMaxZoom(w, s, e, n));   // 1.2.0 ≤ 0：按计划的规则
    if (!r.ok) return;
    flyTo(r.lat, r.lng, r.zoom, durationMs);
}

bool MapController::anyVisible(const QVariantList &pts) const
{
    const double l = leftInset(), r = m_cam.width - panelWidth(), t = m_insT, b = m_cam.height - m_insB;
    for (const QVariant &v : pts) {
        const QVariantList p = v.toList();
        if (p.size() < 2) continue;
        const geo::Pt q = m_cam.pixelFor(p[0].toDouble(), p[1].toDouble());
        if (q.x >= l && q.x <= r && q.y >= t && q.y <= b) return true;
    }
    return false;
}

double MapController::metersPerPixel(double lat) const
{
    // Web 墨卡托：赤道一圈 2πR，世界宽 worldSize 像素；纬度 φ 处缩短 cosφ（R = 6378137，maplibre 的地球半径）
    return 2 * M_PI * 6378137.0 * std::cos(lat * M_PI / 180) / m_cam.worldSize();
}

double MapController::scaleRoundNum(double num)
{
    // maplibre-gl getRoundNum：pow10 = 10 ^ (整数部分的位数 - 1)
    if (!(num > 0)) return 0;
    const double pow10 = std::pow(10.0, double(QString::number(qint64(std::floor(num))).length() - 1));
    double d = num / pow10;
    if (d >= 10) d = 10;
    else if (d >= 5) d = 5;
    else if (d >= 3) d = 3;
    else if (d >= 2) d = 2;
    else if (d >= 1) d = 1;
    else {
        // getDecimalRoundNum
        const double m = std::pow(10.0, std::ceil(-std::log10(d)));
        d = std::round(d * m) / m;
    }
    return pow10 * d;
}

void MapController::scaleFor(const geo::Camera &cam, double maxWidth, double *width, QString *label)
{
    // maplibre-gl ScaleControl：y = 地图高度的一半，左边 0 和 maxWidth 两点之间的大圆距离（R = 6371008.8）
    const double y = cam.height / 2;
    const geo::LatLng a = cam.coordinateFor(0, y), b = cam.coordinateFor(maxWidth, y);
    constexpr double D2R = M_PI / 180;
    const double dLat = (b.lat - a.lat) * D2R, dLng = (b.lng - a.lng) * D2R;
    const double h = std::pow(std::sin(dLat / 2), 2) + std::cos(a.lat * D2R) * std::cos(b.lat * D2R) * std::pow(std::sin(dLng / 2), 2);
    const double meters = 2 * 6371008.8 * std::atan2(std::sqrt(h), std::sqrt(1 - h));
    const bool km = meters >= 1000;
    const double maxDist = km ? meters / 1000 : meters;
    const double dist = scaleRoundNum(maxDist);
    *width = maxDist > 0 ? maxWidth * dist / maxDist : 0;
    *label = QString::number(dist) + (km ? QStringLiteral(" km") : QStringLiteral(" m"));
}

namespace {
// 0.9.5：地球上的比例尺按屏幕中心算：拿一个屏幕中心比例尺相同的平面相机套同一个公式
geo::Camera scaleCamOf(const geo::Camera &flat, const globe::View &v)
{
    geo::Camera c = flat;
    c.width = v.width;
    c.height = v.height;
    c.lat = v.lat;
    c.lng = v.lng;
    c.zoom = globe::zoomForDist(v.dist, v.focal(), v.lat);
    return c;
}
} // namespace

double MapController::scaleWidth() const
{
    double w; QString l;
    scaleFor(globeActive() ? scaleCamOf(m_cam, m_globeOps->globeView()) : m_cam, 100, &w, &l);
    return w;
}

QString MapController::scaleLabel() const
{
    double w; QString l;
    scaleFor(globeActive() ? scaleCamOf(m_cam, m_globeOps->globeView()) : m_cam, 100, &w, &l);
    return l;
}

} // namespace tp::map
