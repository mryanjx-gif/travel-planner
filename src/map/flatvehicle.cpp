#include "flatvehicle.h"

#include "mapcontroller.h"
#include "markerimages.h"

#include <cmath>

using namespace tp::map;

FlatVehicle::FlatVehicle(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(33);   // 约 30 帧，和地球上一样（只动一个覆盖层）
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &FlatVehicle::tick);
}

QObject *FlatVehicle::mapController() const { return m_mapc.data(); }

void FlatVehicle::setMapController(QObject *o)
{
    auto *m = qobject_cast<MapController *>(o);
    if (m == m_mapc) return;
    if (m_mapc) { disconnect(m_mapc, nullptr, this, nullptr); disconnect(m_mapc->markers(), nullptr, this, nullptr); }
    m_mapc = m;
    if (m) {
        // 停下来按新的屏幕长度重排（和地球一样）；选中、数据、地球 / 平面变了重排
        connect(m, &MapController::movingChanged, this, [this] { if (m_mapc && !m_mapc->moving()) replan(); });
        connect(m, &MapController::dataChanged, this, [this] { m_planId.clear(); replan(); updateTimer(); });
        connect(m, &MapController::globeOnChanged, this, [this] { replan(); updateTimer(); });
        connect(m->markers(), &MarkerLayer::selectionChanged, this, [this] { replan(); updateTimer(); });
    }
    updateTimer();
    emit changed();
}

void FlatVehicle::setAllowed(bool on)
{
    if (on == m_allowed) return;
    m_allowed = on;
    replan();
    updateTimer();
    emit changed();
}

void FlatVehicle::setDpr(double d)
{
    if (d <= 0 || d == m_dpr) return;
    m_dpr = d;
    emit changed();
}

void FlatVehicle::updateTimer()
{
    const bool want = m_allowed && m_mapc && !m_mapc->globeOn() && !m_mapc->markers()->selectedPlan().isEmpty();
    if (want && !m_timer.isActive()) { replan(); m_clock.start(); m_timer.start(); tick(); }
    else if (!want) {
        m_timer.stop();
        if (m_shown) { m_shown = false; emit changed(); }
    }
}

const globe::RoutePlan *FlatVehicle::plan() const
{
    if (!m_mapc || m_planId.isEmpty()) return nullptr;
    for (const globe::RoutePlan &p : m_mapc->globeRoutes()->plans)
        if (p.id == m_planId) return &p;
    return nullptr;
}

void FlatVehicle::replan()
{
    if (!m_mapc || m_mapc->globeOn() || !m_allowed) return;
    const QString sel = m_mapc->markers()->selectedPlan();
    const bool newPlan = sel != m_planId;
    m_planId = sel;
    m_plan = {};
    const globe::RoutePlan *p = plan();
    if (!p) return;
    // 每段在屏幕上看得见的长度：相邻两点都在窗口里（留 20 像素）的那几小段加起来
    const geo::Camera &cam = m_mapc->camera();
    std::vector<double> px;
    for (const globe::RouteLeg &leg : p->legs) {
        double len = 0;
        bool have = false;
        geo::Pt prev;
        for (const globe::Vec3 &v : leg.pts) {
            double lat, lng;
            globe::toLatLng(v, &lat, &lng);
            const geo::Pt q = cam.pixelFor(lat, lng);
            const bool in = q.x > -20 && q.y > -20 && q.x < cam.width + 20 && q.y < cam.height + 20;
            if (have && in) {
                const double d = std::hypot(q.x - prev.x, q.y - prev.y);
                if (d < cam.width / 2) len += d;   // 跨 180° 经线时 pixelFor 换了一份世界副本，不算
            }
            have = in;
            prev = q;
        }
        px.push_back(len);
    }
    m_plan = globe::planVehicle(px);
    if (newPlan) m_clock.restart();
}

void FlatVehicle::tick()
{
    bool shown = false;
    double vx = 0, vy = 0, angle = 0;
    bool flip = false;
    QString img;
    double vsz = MarkerImages::kVehicleSize;
    const globe::RoutePlan *p = plan();
    if (m_mapc && p && !m_plan.legs.empty()) {
        const double t = m_msOverride >= 0 ? m_msOverride : std::fmod(double(m_clock.elapsed()), m_plan.totalMs);
        int legIdx = 0;
        double frac = 0;
        globe::vehicleAt(m_plan, t, &legIdx, &frac);
        if (legIdx >= 0 && legIdx < int(p->legs.size())) {
            const globe::RouteLeg &leg = p->legs[legIdx];
            globe::Vec3 ahead;
            const globe::Vec3 pos = globe::pointAlong(leg, frac, &ahead);
            double la, lo, la2, lo2;
            globe::toLatLng(pos, &la, &lo);
            globe::toLatLng(ahead, &la2, &lo2);
            const geo::Camera &cam = m_mapc->camera();
            const geo::Pt q = cam.pixelFor(la, lo);
            geo::Pt q2 = cam.pixelFor(la2, lo2);
            if (std::hypot(q2.x - q.x, q2.y - q.y) > cam.width / 2) q2 = q;   // 跨 180° 经线换了副本：方向不变
            if (q.x > -30 && q.y > -30 && q.x < cam.width + 30 && q.y < cam.height + 30) {
                shown = true;
                vx = q.x; vy = q.y;
                if (q2.x != q.x || q2.y != q.y) angle = std::atan2(q2.y - q.y, q2.x - q.x) * 180.0 / M_PI;
                else angle = m_angle;
                flip = q2.x < q.x;
                img = MarkerImages::vehicleId(leg.mode, leg.color, m_dpr);
                vsz = leg.mode == QLatin1String("flight") ? MarkerImages::kVehicleSize : MarkerImages::kBadgeSize;
            }
        }
    }
    if (shown == m_shown && (!shown || (vx == m_x && vy == m_y && img == m_img && angle == m_angle))) return;
    m_shown = shown; m_x = vx; m_y = vy; m_angle = angle; m_flip = flip; m_size = vsz;
    if (!img.isEmpty()) m_img = img;
    emit changed();
}

QVariantMap FlatVehicle::atMs(double ms)
{
    replan();
    m_msOverride = ms;
    tick();
    m_msOverride = -1;
    int leg = -1;
    double frac = 0;
    globe::vehicleAt(m_plan, ms, &leg, &frac);
    return { { "shown", m_shown }, { "x", m_x }, { "y", m_y }, { "img", m_img }, { "leg", leg }, { "frac", frac },
             { "legs", int(m_plan.legs.size()) }, { "totalMs", m_plan.totalMs } };
}
