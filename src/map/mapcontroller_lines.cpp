// MapController：路线的三份数据、线条命中（0.8.4）。0.8.7 从 mapcontroller.cpp 拆出来，内容没改。
#include "mapcontroller.h"
#include "mapcontroller_p.h"

#include "datastore.h"
#include "settings.h"
#include "tileserver.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <cmath>


namespace tp::map {

using detail::compact;
using detail::kUnlockMaxZoom;
[[maybe_unused]] static const QString &kEmptyFc = detail::emptyFc();

// ---- 路线的三份数据、线条命中（0.8.4） ----

void MapController::refreshRoutes()
{
    const QString sel = m_markers.selectedPlan();
    m_routesJson = m_trips.routesJsonExcept(sel);
    m_routesSelJson = m_trips.routesJsonOnly(sel);
    emit routesChanged();
}

void MapController::refreshHv()
{
    const QString json = m_trips.routesJsonOnly(m_markers.hvPlan());
    if (json == m_routesHvJson) return;
    m_routesHvJson = json;
    emit routesHvChanged();
    emit lineStyleChanged();
}

void MapController::setLineHot(bool on)
{
    if (on == m_lineHot) return;
    m_lineHot = on;
    emit lineHotChanged();
}

namespace {
double distToSegment(double px, double py, const geo::Pt &a, const geo::Pt &b)
{
    const double dx = b.x - a.x, dy = b.y - a.y, L2 = dx * dx + dy * dy;
    double t = L2 > 0 ? ((px - a.x) * dx + (py - a.y) * dy) / L2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(px - a.x - t * dx, py - a.y - t * dy);
}
} // namespace

QString MapController::lineAt(double x, double y) const
{
    // Electron lineAt：queryRenderedFeatures(±4 px 的方框, 只查彩线图层)，取最上面那条。
    // 这里自己算：鼠标到线的距离 ≤ 4 + 线宽 / 2；最上面 = 选中的 > 点线 > 虚线 > 实线 > 后画的（和图层顺序一样）
    if (globeActive()) return globeLineAt(x, y);   // 0.9.5：地球上的线
    constexpr double kR = 4;
    const QString sel = m_markers.selectedPlan();
    const double world = m_cam.worldSize();
    QString best;
    int bestRank = -1;
    const QVector<TripPlan> &plans = m_trips.plans();
    for (int pi = 0; pi < plans.size(); ++pi) {
        const TripPlan &p = plans[pi];
        if (p.segments.isEmpty()) continue;
        const bool isSel = !sel.isEmpty() && p.id == sel;
        const int rank = (isSel ? 1000000 : 0) + p.dash * 10000 + pi;
        if (rank <= bestRank) continue;
        const double reach = kR + (isSel ? 5.0 : 3.5) / 2;
        // 线的经度是展开的（可能超出 ±180）；maplibre 在每一份世界副本上都画，鼠标对应的副本挨个试
        const geo::Pt nw = m_cam.pixelForRaw(p.north, p.west), se = m_cam.pixelForRaw(p.south, p.east);
        bool hit = false;
        for (int k = -2; k <= 2 && !hit; ++k) {
            const double sx = k * world;
            if (x < nw.x + sx - reach || x > se.x + sx + reach || y < nw.y - reach || y > se.y + reach) continue;
            for (const QVector<geo::LatLng> &seg : p.segments) {
                if (seg.size() < 2) continue;
                geo::Pt a = m_cam.pixelForRaw(seg[0].lat, seg[0].lng);
                a.x += sx;
                for (int j = 1; j < seg.size(); ++j) {
                    geo::Pt b = m_cam.pixelForRaw(seg[j].lat, seg[j].lng);
                    b.x += sx;
                    if (distToSegment(x, y, a, b) <= reach) { hit = true; break; }
                    a = b;
                }
                if (hit) break;
            }
        }
        if (hit) { best = p.id; bestRank = rank; }
    }
    return best;
}

QString MapController::lineHover(double x, double y)
{
    // 在标记 / 圆圈上：标记自己处理（Electron：t.closest('.maplibregl-marker') → return）
    if (m_markers.overMarker()) { setLineHot(false); return {}; }
    const bool isMoving = globeActive() ? m_globeOps->globeMoving() : m_moving;
    const QString pid = ((!globeActive() && m_base == QLatin1String("unlock")) || isMoving) ? QString() : lineAt(x, y);
    if (pid.isEmpty()) {
        setLineHot(false);
        m_markers.unhoverLine();
        return {};
    }
    setLineHot(true);
    m_markers.hoverLine(pid);
    for (const TripPlan &p : m_trips.plans())
        if (p.id == pid) return (p.name.isEmpty() ? trd("未命名计划") : p.name).toHtmlEscaped();
    return {};
}

void MapController::lineLeave()
{
    setLineHot(false);
    m_markers.unhoverLine();
}

bool MapController::lineClick(double x, double y)
{
    if (m_markers.overMarker() || (!globeActive() && m_base == QLatin1String("unlock"))) return false;
    const QString pid = lineAt(x, y);
    if (pid.isEmpty()) return false;
    m_markers.clickLine(pid);
    return true;
}

QPointF MapController::linePoint(const QString &planId, int seg) const
{
    if (globeActive()) {
        // 0.9.5 地球上：这一段看得见的部分里，屏幕长度一半处的点
        const globe::Camera cam(m_globeOps->globeView());
        for (const globe::RoutePlan &p : globeRoutes()->plans) {
            if (p.id != planId) continue;
            for (const globe::RouteLeg &leg : p.legs) {
                if (leg.stop != seg) continue;
                const auto runs = globe::screenRuns(cam, leg.pts);
                double half = globe::runsLength(runs) / 2;
                for (const globe::ScreenRun &r : runs)
                    for (size_t j = 1; j < r.size(); ++j) {
                        const double L = std::hypot(r[j].x - r[j - 1].x, r[j].y - r[j - 1].y);
                        if (half <= L && L > 0) { const double t = half / L; return { r[j - 1].x + (r[j].x - r[j - 1].x) * t, r[j - 1].y + (r[j].y - r[j - 1].y) * t }; }
                        half -= L;
                    }
            }
        }
        return { -1, -1 };
    }
    for (const TripPlan &p : m_trips.plans()) {
        if (p.id != planId || seg < 0 || seg >= p.segments.size()) continue;
        const QVector<geo::LatLng> &s = p.segments[seg];
        if (s.size() < 2) break;
        // 中间那一小段的中点（长段是大圆弧，取折线中间的点才在线上）
        const int j = int(s.size()) / 2;
        // 在屏幕上取中点（maplibre 在墨卡托平面上把相邻两点连成直线）
        const geo::Pt pa = m_cam.pixelFor(s[j - 1].lat, s[j - 1].lng);
        geo::Pt pb = m_cam.pixelForRaw(s[j].lat, s[j].lng);
        pb.x += pa.x - m_cam.pixelForRaw(s[j - 1].lat, s[j - 1].lng).x;   // 和 a 放在同一份世界副本上
        return { (pa.x + pb.x) / 2, (pa.y + pb.y) / 2 };
    }
    return { -1, -1 };
}


} // namespace tp::map
