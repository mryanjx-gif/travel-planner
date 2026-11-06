// MapController：相机（拖动、缩放、飞行、动画、坐标换算）。0.8.7 从 mapcontroller.cpp 拆出来，内容没改。
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

// ---- 相机 ----

void MapController::applyCamera(geo::Camera c)
{
    const double scaleMin = geo::minZoomForScale(c);
    const geo::Camera k = geo::constrain(c, scaleMin, maxZoom());
    const double minZ = std::max(scaleMin, std::log2(std::max(1.0, k.height) / geo::kTile));
    const bool changed = k.lat != m_cam.lat || k.lng != m_cam.lng || k.zoom != m_cam.zoom || k.width != m_cam.width
                         || k.height != m_cam.height || minZ != m_minZoom;
    m_cam = k;
    m_minZoom = minZ;
    if (changed) {
        if (!m_markers.onGlobe()) m_markers.cameraChanged(m_moving);   // 0.9.5：在地球上时标记按地球摆
        emit cameraChanged();
        if (!globeActive()) emit scaleChanged();
    }
}

void MapController::setViewport(double width, double height)
{
    if (width <= 0 || height <= 0) return;
    geo::Camera c = m_cam;
    c.width = width;
    c.height = height;
    m_viewportKnown = true;
    applyCamera(c);
    if (!m_fittedOnce && m_haveData) { m_fittedOnce = true; fitAllFlat(); }
}

void MapController::setMoving(bool m)
{
    if (m_moving == m) return;
    m_moving = m;
    m_markers.movingChanged(m);
    emit movingChanged();
}

void MapController::stop()
{
    m_anim = Anim::None;
    m_fly.reset();
    m_animTimer.stop();
    setMoving(false);
}

void MapController::dragStart()
{
    stop();
    m_samples.clear();
    m_dragClock.start();
    setMoving(true);
}

void MapController::dragBy(double dx, double dy)
{
    if (!m_dragClock.isValid()) m_dragClock.start();
    geo::Camera c = m_cam;
    c.panBy(dx, dy);
    applyCamera(c);
    const qint64 now = m_dragClock.elapsed();
    m_samples.push_back({ now, dx, dy });
    while (!m_samples.isEmpty() && now - m_samples.first().t > 160) m_samples.removeFirst();
}

void MapController::dragEnd()
{
    // maplibre-gl 的拖动惯性（handler_inertia.js）：最近 160 毫秒的位移 → 速度 × linearity .3，最快 1400 px/s，
    // 减速度 2500 px/s²，时长 = 速度 / (减速度 × .3)，滑行距离 = 速度 × 时长 / 2，曲线 bezier(0, 0, .3, 1)
    const qint64 now = m_dragClock.isValid() ? m_dragClock.elapsed() : 0;
    while (!m_samples.isEmpty() && now - m_samples.first().t > 160) m_samples.removeFirst();
    if (m_samples.size() < 2) { m_samples.clear(); setMoving(false); return; }
    double sx = 0, sy = 0;
    for (const Sample &s : m_samples) { sx += s.dx; sy += s.dy; }
    const double dt = (m_samples.last().t - m_samples.first().t) / 1000.0;
    m_samples.clear();
    if (dt <= 0) { setMoving(false); return; }
    double vx = sx * 0.3 / dt, vy = sy * 0.3 / dt;
    double speed = std::hypot(vx, vy);
    if (speed > 1400) { vx *= 1400 / speed; vy *= 1400 / speed; speed = 1400; }
    const double duration = speed / (2500 * 0.3);
    if (duration < 0.05) { setMoving(false); return; }
    m_inertiaDx = vx * duration / 2;
    m_inertiaDy = vy * duration / 2;
    m_inertiaDone = 0;
    startAnim(Anim::Inertia, int(duration * 1000));
}

void MapController::wheel(double angleDeltaY, double x, double y)
{
    const double delta = angleDeltaY / 240.0;   // 一格（120）放大半级（原型的手感，和 Electron 接近）
    const double base = m_anim == Anim::Ease ? m_wheelTarget : m_cam.zoom;
    const double target = std::clamp(base + delta, m_minZoom, maxZoom());
    if (rollToGlobe(target, x, y)) return;   // 0.9.7
    m_wheelTarget = target;
    m_from = m_cam;
    m_toZoom = target;
    m_easeAnchorX = x;
    m_easeAnchorY = y;
    startAnim(Anim::Ease, 200);
}

void MapController::zoomBy(double delta, double x, double y, int durationMs)
{
    const double target = std::clamp(m_cam.zoom + delta, m_minZoom, maxZoom());
    if (rollToGlobe(target, x, y)) return;   // 0.9.7
    if (durationMs <= 0) {
        geo::Camera c = m_cam;
        c.zoomAround(target, x, y);
        applyCamera(c);
        return;
    }
    m_wheelTarget = target;
    m_from = m_cam;
    m_toZoom = target;
    m_easeAnchorX = x;
    m_easeAnchorY = y;
    startAnim(Anim::Ease, durationMs);
}

// 0.9.7：图层是地球、自动摊平在平面上时，缩到 kGlobeRollZoom 以下交给 QML 卷回地球（终点 = 缩放后鼠标下的点不动）
bool MapController::rollToGlobe(double target, double x, double y)
{
    if (!m_autoGlobe || m_globeOn || target > kGlobeRollZoom + 1e-6 || target >= m_cam.zoom) return false;
    geo::Camera c = m_cam;
    c.zoomAround(target, x, y);
    stop();
    qInfo("地图：缩到 %.2f 级（≤ %.1f），卷回地球", target, kGlobeRollZoom);
    emit globeRollRequested(c.lat, geo::normLng(c.lng), target);
    return true;
}

void MapController::setAutoGlobe(bool on)
{
    if (on == m_autoGlobe) return;
    m_autoGlobe = on;
    emit autoGlobeChanged();
}

QVariantList MapController::fitPlanCamera(const QString &planId, double maxZoom) const
{
    double w, s, e, n;
    if (!planBounds(planId, &w, &s, &e, &n)) return {};
    const geo::FitResult r = geo::fitBounds(fitCam(), w, s, e, n, fitPad(), maxZoom);
    if (!r.ok) return {};
    return { r.lat, geo::normLng(r.lng), r.zoom };
}

void MapController::jumpTo(double lat, double lng, double zoom)
{
    stop();
    geo::Camera c = m_cam;
    c.lat = lat;
    c.lng = lng;
    c.zoom = zoom;
    applyCamera(c);
}

void MapController::flyTo(double lat, double lng, double zoom, int durationMs)
{
    if (globeGo(lat, lng, zoom, durationMs < 0 ? 1000 : durationMs)) return;   // 0.9.5：在地球上转给地球
    stop();
    const double z = std::clamp(zoom, 0.0, maxZoom());
    m_fly = std::make_unique<geo::FlyPath>(m_cam, lat, lng, z);
    m_toLat = lat;
    m_toLng = lng;
    m_toZoom = z;
    const int ms = durationMs >= 0 ? durationMs : int(m_fly->naturalDurationMs());
    if (ms <= 0) { jumpTo(lat, lng, z); return; }
    startAnim(Anim::Fly, ms);
}

void MapController::fitAllFlat()
{
    // 0.9.6：第一次有数据 / 知道窗口大小时自动框住只动平面的相机（0.9.5 在地球上也转给地球，盖掉了截图 globe-night / globe-near
    // 和启动时框好的"下次出发"）
    const bool g = m_globeOn;
    m_globeOn = false;
    fitAll(false);
    m_globeOn = g;
    // 0.9.7：上面换镜头时比例尺是按平面算的（那时 m_globeOn 临时关着）；在地球上时按地球重算一次（0.9.6 起地球上的比例尺会停在平面的数）
    if (g) emit scaleChanged();
}

void MapController::fitAll(bool animate)
{
    const QVector<geo::LatLng> pts = m_trips.allStops();
    if (pts.isEmpty()) return;
    const geo::LatLng c = geo::centerOf(std::vector<geo::LatLng>(pts.begin(), pts.end()));
    if (globeActive()) {
        // Electron fitAll 的球形分支：站点离中心（三维平均）超过约 3500 km 时，转到中心，地球缩放到正好放进可见区域
        // （直径 = 可见区域的 90%，左边让开列表）；分布不广就和平面一样框住（下面）
        double far = 0;
        for (const geo::LatLng &p : pts) far = std::max(far, geo::distanceMeters(c, p) / 1000.0);
        if (far > 3500) {
            const globe::View v = m_globeOps->globeView();
            const double left = leftInset(), right = 40;
            const double target = 0.45 * std::max(160.0, std::min(v.width - left - right, v.height - 60));
            const double f = v.focal();
            const double dist = std::sqrt(1.0 + (f / target) * (f / target));   // 透视下球的半径（像素）= f / √(d² − 1)
            // 可见区域的中心在窗口中心往右 (left - right) / 2：中心经度往回挪这么多像素对应的角度
            const double dx = (left - right) / 2;
            const double k = (dist - 1.0) / f * 180.0 / M_PI / std::max(0.2, std::cos(c.lat * M_PI / 180.0));
            qInfo("地图：地球上框住所有站点 %d 个（最远离中心 %.0f km）→ 中心 %.4f, %.4f，距离 %.3f", int(pts.size()), far, c.lat, c.lng, dist);
            m_globeOps->globeEaseTo(c.lat, c.lng - dx * k, -1, animate ? 800 : 0, dist);
            return;
        }
    }
    double w = 1e9, s = 1e9, e = -1e9, n = -1e9;
    for (const geo::LatLng &p : pts) {
        const double lng = geo::unwrapTo(p.lng, c.lng);   // 经度按中心连续展开：跨 180° 经线也不会框成整个世界
        w = std::min(w, lng); e = std::max(e, lng); s = std::min(s, p.lat); n = std::max(n, p.lat);
    }
    const geo::Camera fc = fitCam();
    const geo::Padding pad = geo::fitPadding(fc.width, fc.height, 40, 40);
    const geo::FitResult r = geo::fitBounds(fc, w, s, e, n, pad, 5);
    if (!r.ok) return;
    qInfo("地图：框住所有站点 %d 个（窗口 %.0f×%.0f）→ 中心 %.4f, %.4f，缩放 %.2f", int(pts.size()), fc.width, fc.height, r.lat, r.lng, r.zoom);
    if (globeGo(r.lat, r.lng, r.zoom, animate ? 800 : 0)) return;
    if (animate) flyTo(r.lat, r.lng, r.zoom, 800);
    else jumpTo(r.lat, r.lng, r.zoom);
}

void MapController::easeTo(double lat, double lng, double zoom, int durationMs)
{
    if (globeActive()) {   // 0.9.5：在地球上转给地球（zoom < 0 = 距离不变）
        if (zoom < 0) { m_globeOps->globeEaseTo(lat, geo::normLng(lng), -1, durationMs); return; }
        globeGo(lat, lng, zoom, durationMs);
        return;
    }
    stop();
    const double z = zoom < 0 ? m_cam.zoom : std::clamp(zoom, m_minZoom, maxZoom());
    if (durationMs <= 0) { jumpTo(lat, lng, z); return; }
    m_from = m_cam;
    m_toLat = lat;
    m_toLng = geo::unwrapTo(lng, m_cam.lng);   // 走近的那一边
    m_toZoom = z;
    startAnim(Anim::Pan, durationMs);
}

// ---- 浮在地图上的面板（0.8.7，S-05） ----

void MapController::setInsets(double left, double top, double right, double bottom)
{
    m_insL = std::max(0.0, left);
    m_insT = std::max(0.0, top);
    m_insR = std::max(0.0, right);
    m_insB = std::max(0.0, bottom);
}

double MapController::leftInset() const { return std::max(40.0, m_insL + 30); }
double MapController::panelWidth() const { return m_insR > 0 ? std::max(40.0, m_insR + 30) : 40.0; }

geo::Padding MapController::fitPad() const
{
    // Electron fitPadding(left, right)：左右按窗口宽度缩小；上下 = min(40, (H-80)/2) + 顶栏 / 底部胶囊，按窗口高度缩小
    const geo::Camera fc = fitCam();
    geo::Padding p = geo::fitPadding(fc.width, fc.height, leftInset(), panelWidth());
    double top = p.top + m_insT, bottom = p.bottom + m_insB;
    const double vroom = fc.height - 80;
    if (top + bottom > vroom) {
        const double k = std::max(0.0, vroom) / (top + bottom);
        top *= k;
        bottom *= k;
    }
    p.top = top;
    p.bottom = bottom;
    return p;
}

bool MapController::planBounds(const QString &planId, double *w, double *s, double *e, double *n) const
{
    for (const TripPlan &p : m_trips.plans()) {
        if (p.id != planId) continue;
        if (!p.segments.isEmpty()) { *w = p.west; *e = p.east; *s = p.south; *n = p.north; return true; }
        if (p.markerPos.isEmpty()) return false;
        const double ref = p.markerPos.first().lng;
        *w = 1e9; *s = 1e9; *e = -1e9; *n = -1e9;
        for (const geo::LatLng &ll : p.markerPos) {
            const double lng = geo::unwrapTo(ll.lng, ref);
            *w = std::min(*w, lng); *e = std::max(*e, lng); *s = std::min(*s, ll.lat); *n = std::max(*n, ll.lat);
        }
        return true;
    }
    return false;
}

bool MapController::flyToPlan(const QString &planId)
{
    double w, s, e, n;
    if (!planBounds(planId, &w, &s, &e, &n)) return false;
    const geo::FitResult r = geo::fitBounds(fitCam(), w, s, e, n, fitPad(), 8);
    if (!r.ok) return false;
    flyTo(r.lat, r.lng, r.zoom, 800);
    return true;
}

bool MapController::frameTrip(const QString &planId)
{
    // Electron：validStops 的经度以第一站为准连续展开（跨 180° 经线也框得对），maxZoom 5.5，duration 0
    for (const TripPlan &p : m_trips.plans()) {
        if (p.id != planId || p.stops.isEmpty()) continue;
        const double l0 = p.stops.first().lng;
        double w = 1e9, s = 1e9, e = -1e9, n = -1e9;
        for (const TripStop &st : p.stops) {
            const double lng = l0 + std::fmod(std::fmod(st.lng - l0 + 540, 360) + 360, 360) - 180;
            w = std::min(w, lng); e = std::max(e, lng); s = std::min(s, st.lat); n = std::max(n, st.lat);
        }
        const geo::FitResult r = geo::fitBounds(fitCam(), w, s, e, n, fitPad(), 5.5);
        if (!r.ok) return false;
        qInfo("地图：启动时框住下次出发 %s → 中心 %.4f, %.4f，缩放 %.2f", qUtf8Printable(planId), r.lat, r.lng, r.zoom);
        if (globeGo(r.lat, r.lng, r.zoom, 0)) return true;   // 0.9.5：在地球上（最多 5.5 级，留在地球上）
        jumpTo(r.lat, r.lng, r.zoom);
        return true;
    }
    return false;
}

void MapController::panToVisible(double lat, double lng, int durationMs)
{
    // Electron：可见区域中心相对地图中心的偏移 dx = (max(0, left-30) - max(0, right-30)) / 2，让这一点停在那里
    const double left = leftInset() - 30, right = panelWidth() - 30;
    const double dx = (std::max(0.0, left) - std::max(0.0, right)) / 2;
    if (globeActive()) {
        // 0.9.5 地球版：这一点放在中心往右 dx 像素（屏幕中心每像素对应的角度 = (dist - 1) / f，经度再除以 cos 纬度）
        const globe::View v = m_globeOps->globeView();
        const double k = (v.dist - 1.0) / v.focal() * 180.0 / M_PI / std::max(0.2, std::cos(lat * M_PI / 180.0));
        m_globeOps->globeEaseTo(std::clamp(lat, -85.0, 85.0), geo::normLng(lng - dx * k), -1, durationMs);
        return;
    }
    const geo::Pt pt = geo::project(lat, geo::unwrapTo(lng, m_cam.lng));
    const geo::LatLng c = geo::unproject(pt.x - dx / m_cam.worldSize(), pt.y);
    easeTo(c.lat, c.lng, -1, durationMs);
}

void MapController::startAnim(Anim kind, int durationMs)
{
    m_anim = kind;
    m_animMs = std::max(1, durationMs);
    m_animClock.start();
    setMoving(true);
    if (!m_animTimer.isActive()) m_animTimer.start();
    tick();
}

void MapController::tick()
{
    if (m_anim == Anim::None) { m_animTimer.stop(); return; }
    const double t = std::min(1.0, m_animClock.elapsed() / m_animMs);
    geo::Camera c = m_cam;
    switch (m_anim) {
    case Anim::Fly:
        if (m_fly) m_fly->at(t, &c.lat, &c.lng, &c.zoom);
        break;
    case Anim::Ease: {
        // 缩放缓动：ease-out（滚轮跟手），锚点（鼠标所在处）保持不动
        const double k = 1 - std::pow(1 - t, 3);
        const double z = m_from.zoom + (m_toZoom - m_from.zoom) * k;
        c = m_from;
        c.width = m_cam.width;
        c.height = m_cam.height;
        c.zoomAround(z, m_easeAnchorX < 0 ? c.width / 2 : m_easeAnchorX, m_easeAnchorY < 0 ? c.height / 2 : m_easeAnchorY);
        break;
    }
    case Anim::Inertia: {
        // bezier(0, 0, .3, 1) 近似为 ease-out cubic
        const double k = 1 - std::pow(1 - t, 3);
        const double step = k - m_inertiaDone;
        m_inertiaDone = k;
        c.panBy(m_inertiaDx * step, m_inertiaDy * step);
        break;
    }
    case Anim::Pan: {
        // maplibre-gl easeTo（不旋转）：缩放按缓动插值，中心在世界坐标里直线插值
        const double k = geo::easeDefault(t);
        const geo::Pt a = geo::project(m_from.lat, m_from.lng), b = geo::project(m_toLat, m_toLng);
        const geo::LatLng ll = geo::unproject(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k);
        c.lat = ll.lat;
        c.lng = ll.lng;
        c.zoom = m_from.zoom + (m_toZoom - m_from.zoom) * k;
        break;
    }
    case Anim::None:
        break;
    }
    applyCamera(c);
    if (t >= 1) {
        m_anim = Anim::None;
        m_fly.reset();
        m_animTimer.stop();
        setMoving(false);
    }
}

QPointF MapController::pixelFor(double lat, double lng) const
{
    const geo::Pt p = m_cam.pixelFor(lat, lng);
    return { p.x, p.y };
}

QVariantList MapController::coordinateFor(double x, double y) const
{
    const geo::LatLng c = m_cam.coordinateFor(x, y);
    return { c.lat, geo::normLng(c.lng) };
}

QVariantList MapController::randomStop() const
{
    const QVector<geo::LatLng> all = m_trips.allStops();
    if (all.isEmpty()) return { m_cam.lat, m_cam.lng };
    const geo::LatLng s = all[QRandomGenerator::global()->bounded(int(all.size()))];
    return { s.lat, s.lng };
}

void MapController::setProbe(double lat, double lng, bool on)
{
    m_probeJson = on ? compact(QJsonObject{ { "type", "FeatureCollection" }, { "features", QJsonArray{ QJsonObject{
                                                { "type", "Feature" }, { "properties", QJsonObject{} },
                                                { "geometry", QJsonObject{ { "type", "Point" }, { "coordinates", QJsonArray{ lng, lat } } } } } } } })
                     : kEmptyFc;
    emit probeChanged();
}


} // namespace tp::map
