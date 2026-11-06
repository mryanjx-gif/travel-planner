// MapController：地球上的内容（0.9.5，阶段 ③ 第 5 批）。标记按地球的相机摆、碰线、定位 / 飞过去 / 框住转给地球。
// 为什么这样做见 claude/v0.9.5记录.md §2。
#include "mapcontroller.h"
#include "mapcontroller_p.h"

#include <algorithm>
#include <cmath>

namespace tp::map {

void MapController::setGlobeOn(bool on)
{
    if (on == m_globeOn) return;
    m_globeOn = on;
    if (globeActive()) {
        const globe::View v = m_globeOps->globeView();
        m_markers.setGlobeView(true, v, m_globeOps->globeZoom(), m_globeOps->globeMoving());
        m_photoLayer.setGlobeView(true, v, m_globeOps->globeZoom());
    } else {
        m_markers.setGlobeView(false, {}, 0, m_moving);
        m_markers.cameraChanged(m_moving);
        m_photoLayer.setGlobeView(false, {}, 0);
    }
    if (m_lineHot) { setLineHot(false); m_markers.unhoverLine(); }
    emit scaleChanged();
    emit globeOnChanged();
}

void MapController::globeViewChanged()
{
    if (!globeActive()) return;
    m_markers.setGlobeView(true, m_globeOps->globeView(), m_globeOps->globeZoom(), m_globeOps->globeMoving());
    m_photoLayer.setGlobeView(true, m_globeOps->globeView(), m_globeOps->globeZoom());
    emit scaleChanged();
}

std::shared_ptr<const globe::Routes> MapController::globeRoutes() const
{
    if (m_globeRoutesDirty || !m_globeRoutes) {
        m_globeRoutes = globe::buildRoutes(m_trips);
        m_globeRoutesDirty = false;
    }
    return m_globeRoutes;
}

std::shared_ptr<const globe::Routes> MapController::globeOverlayRoutes() const
{
    if (m_globeOverlayDirty || !m_globeOverlayRoutes) {
        m_globeOverlayRoutes = globe::buildRoutes(m_overlayTrips);
        m_globeOverlayDirty = false;
    }
    return m_globeOverlayRoutes;
}

geo::Camera MapController::fitCam() const
{
    geo::Camera c = m_cam;
    if (globeActive()) {
        const globe::View v = m_globeOps->globeView();
        c.width = v.width;
        c.height = v.height;
    }
    return c;
}

bool MapController::globeGo(double lat, double lng, double zoom, int ms)
{
    if (!globeActive()) return false;
    if (zoom > kGlobeMaxZoom) {
        // 城市里的行程、我的位置：地球上几个站点会挤在一起，换成平面（0.9.5 先淡入淡出，0.9.7 换成变形过渡）
        qInfo("地图：在地球上要飞到 %.2f 级（> %.1f），换成平面", zoom, kGlobeMaxZoom);
        emit globeExitRequested(lat, geo::normLng(lng), zoom, ms);
        return true;
    }
    m_globeOps->globeEaseTo(lat, geo::normLng(lng), zoom, ms);
    return true;
}

const std::vector<MapController::GlobeHitPlan> &MapController::globeHitRuns() const
{
    const auto routes = globeRoutes();
    const globe::View v = m_globeOps->globeView();
    const bool same = m_globeHitRoutes == routes.get() && v.lat == m_globeHitView.lat && v.lng == m_globeHitView.lng && v.dist == m_globeHitView.dist
                      && v.width == m_globeHitView.width && v.height == m_globeHitView.height
                      && v.morph == m_globeHitView.morph;
    if (same) return m_globeHit;
    m_globeHit.clear();
    m_globeHitView = v;
    m_globeHitRoutes = routes.get();
    const globe::Camera cam(v);
    for (const globe::RoutePlan &p : routes->plans) {
        GlobeHitPlan hp{ p.id, p.index, p.dash, {} };
        for (int i = 0; i < int(p.legs.size()); ++i) {
            if (!globe::legMaybeVisible(v, p.legs[i])) continue;
            auto runs = globe::screenRuns(cam, p.legs[i].pts);
            if (!runs.empty()) hp.legs.push_back({ p.legs[i].stop, std::move(runs) });
        }
        if (!hp.legs.empty()) m_globeHit.push_back(std::move(hp));
    }
    return m_globeHit;
}

QString MapController::globeLineAt(double x, double y) const
{
    // 和平面的 lineAt 一样：鼠标到线的距离 ≤ 4 + 线宽 / 2；最上面 = 选中的 > 点线 > 虚线 > 实线 > 后画的
    constexpr double kR = 4;
    const QString sel = m_markers.selectedPlan();
    QString best;
    int bestRank = -1;
    for (const GlobeHitPlan &p : globeHitRuns()) {
        const bool isSel = !sel.isEmpty() && p.id == sel;
        const int rank = (isSel ? 1000000 : 0) + p.dash * 10000 + p.index;
        if (rank <= bestRank) continue;
        const double reach = kR + (isSel ? 5.0 : 3.5) / 2;
        for (const auto &leg : p.legs)
            if (globe::distToRuns(leg.second, x, y) <= reach) { best = p.id; bestRank = rank; break; }
    }
    return best;
}

QString MapController::globeDemoPlan() const
{
    QString best;
    double bestAng = 0;
    for (const globe::RoutePlan &p : globeRoutes()->plans)
        for (const globe::RouteLeg &l : p.legs)
            if (l.lifted && l.angle > bestAng && l.angle < 1.6) { bestAng = l.angle; best = p.id; }   // 太长（> 90°）的转不进一屏，不要
    return best;
}

QVariantMap MapController::globeLegInfo(const QString &planId, int leg) const
{
    for (const globe::RoutePlan &p : globeRoutes()->plans) {
        if (p.id != planId || leg < 0 || leg >= int(p.legs.size())) continue;
        const globe::RouteLeg &l = p.legs[leg];
        double maxR = 0;
        for (const globe::Vec3 &v : l.pts) maxR = std::max(maxR, v.length());
        return { { "lifted", l.lifted }, { "mode", l.mode }, { "angle", l.angle }, { "maxR", maxR }, { "points", int(l.pts.size()) },
                 { "stop", l.stop }, { "color", l.color } };
    }
    return {};
}

} // namespace tp::map
