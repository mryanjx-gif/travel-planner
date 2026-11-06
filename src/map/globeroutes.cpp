#include "globeroutes.h"
#include "roads.h"

#include <algorithm>
#include <cmath>

namespace tp::map::globe {

double liftHeight(double angle)
{
    const double km = std::min(angle * kEarthRadiusM / 1000.0 * kLiftRatio, kLiftMaxKm);
    return km * 1000.0 / kEarthRadiusM;
}

namespace {
constexpr double kPi = 3.14159265358979323846;

// a、b 之间按球面插值细分，相邻两点不超过 kStepDeg（a 不放进去，b 放进去）
void appendSlerp(std::vector<Vec3> *out, const Vec3 &a, const Vec3 &b)
{
    const double ang = std::acos(std::clamp(a.dot(b), -1.0, 1.0));
    const int n = std::max(1, int(std::ceil(ang / (kStepDeg * kPi / 180.0))));
    const double s = std::sin(ang);
    for (int i = 1; i <= n; ++i) {
        const double t = double(i) / n;
        Vec3 p = (s < 1e-9) ? a * (1 - t) + b * t : a * (std::sin((1 - t) * ang) / s) + b * (std::sin(t * ang) / s);
        out->push_back(p.normalized());
    }
}
} // namespace

std::shared_ptr<const Routes> buildRoutes(const TripData &trips)
{
    auto r = std::make_shared<Routes>();
    const QVector<TripPlan> &plans = trips.plans();
    for (int pi = 0; pi < plans.size(); ++pi) {
        const TripPlan &p = plans[pi];
        if (p.segments.isEmpty()) continue;
        RoutePlan rp;
        rp.id = p.id;
        rp.index = pi;
        rp.dash = p.dash;
        for (int i = 0; i < p.segments.size(); ++i) {
            const QVector<geo::LatLng> &seg = p.segments[i];
            if (seg.size() < 2) continue;
            RouteLeg leg;
            leg.stop = i;
            const TripStop &from = p.stops[std::min(i, int(p.stops.size()) - 1)];
            leg.mode = roads::legModeOf(from.legMode);
            leg.color = TripData::stopColor(p, from);
            leg.lifted = leg.mode == QLatin1String("flight");
            std::vector<Vec3> ground{ toVec(seg[0].lat, seg[0].lng) };
            for (int j = 1; j < seg.size(); ++j) appendSlerp(&ground, ground.back(), toVec(seg[j].lat, seg[j].lng));
            leg.angle = std::acos(std::clamp(ground.front().dot(ground.back()), -1.0, 1.0));
            // 走过的比例（按角度）
            std::vector<double> cum(ground.size(), 0.0);
            for (size_t j = 1; j < ground.size(); ++j) cum[j] = cum[j - 1] + std::acos(std::clamp(ground[j - 1].dot(ground[j]), -1.0, 1.0));
            const double total = cum.back() > 0 ? cum.back() : 1.0;
            const double h = leg.lifted ? liftHeight(leg.angle) : 0.0;
            leg.pts.reserve(ground.size());
            leg.t.reserve(ground.size());
            for (size_t j = 0; j < ground.size(); ++j) {
                const double t = cum[j] / total;
                leg.t.push_back(t);
                leg.pts.push_back(ground[j] * (1.0 + h * std::sin(kPi * t)));
            }
            // 粗筛用的包围：中间方向 + 各点离它最远的角度 + 最大半径
            Vec3 sum;
            for (const Vec3 &g : ground) sum = sum + g;
            leg.mid = sum.length() > 1e-9 ? sum.normalized() : ground.front();
            for (size_t j = 0; j < ground.size(); ++j) {
                leg.cap = std::max(leg.cap, std::acos(std::clamp(leg.mid.dot(ground[j]), -1.0, 1.0)));
                leg.maxR = std::max(leg.maxR, leg.pts[j].length());
            }
            r->points += int(leg.pts.size());
            rp.legs.push_back(std::move(leg));
        }
        if (!rp.legs.empty()) r->plans.push_back(std::move(rp));
    }
    return r;
}

bool legMaybeVisible(const View &v, const RouteLeg &leg)
{
    if (v.morph >= 0.5) return true;   // 0.9.7：变形到一半以上，平面上都看得见
    const Vec3 e = toVec(v.lat, v.lng);
    const double th = std::acos(std::clamp(e.dot(leg.mid), -1.0, 1.0));
    const double reach = std::acos(std::clamp(1.0 / std::max(1.0, v.dist), -1.0, 1.0)) + std::acos(std::clamp(1.0 / std::max(1.0, leg.maxR), -1.0, 1.0));
    return th - leg.cap <= reach + 0.01;
}

std::vector<ScreenRun> screenRuns(const Camera &cam, const std::vector<Vec3> &pts, std::vector<std::vector<double>> *tOut)
{
    std::vector<ScreenRun> runs;
    std::vector<std::vector<double>> ts;
    // 0.9.6：贴地的点（半径 1）被挡住 ⇔ dot(p, 相机) ≤ 1，不用开方；抬起的点才走完整的视线判断
    const Vec3 eye = cam.view().eye();
    const bool flatish = cam.morphVisible();   // 0.9.7：变形到一半以上不判断遮挡
    auto vis = [&cam, &eye, flatish](const Vec3 &p, Pt2 *s) {
        if (flatish) return cam.project(p, &s->x, &s->y);
        if (p.dot(p) < 1.0 + 1e-9) { if (p.dot(eye) <= 1.0) return false; }
        else if (cam.occluded(p)) return false;
        return cam.project(p, &s->x, &s->y);
    };
    // 两点之间：方向按直线插值再归一、离球心的距离也插值（贴地的线中间不会掉到球里面，二分才找得到真正的地平线）
    auto between = [](const Vec3 &a, const Vec3 &b, double m) { return (a * (1 - m) + b * m).normalized() * (a.length() * (1 - m) + b.length() * m); };
    bool prevVis = false, open = false;
    for (size_t i = 0; i < pts.size(); ++i) {
        Pt2 s;
        const bool v = vis(pts[i], &s);
        if (i > 0 && v != prevVis) {
            // 边界：在 i-1 和 i 之间二分，找到正好被挡住的那一点（lo 端和 i-1 一样，hi 端和 i 一样）
            double lo = 0, hi = 1;
            for (int k = 0; k < 14; ++k) {
                const double m = (lo + hi) / 2;
                Pt2 tmp;
                (vis(between(pts[i - 1], pts[i], m), &tmp) == prevVis ? lo : hi) = m;
            }
            const double m = prevVis ? lo : hi;   // 看得见的那一端
            Pt2 edge;
            if (vis(between(pts[i - 1], pts[i], m), &edge)) {
                if (prevVis && open) { runs.back().push_back(edge); ts.back().push_back(double(i - 1) + m); }
                else if (!prevVis) { runs.push_back({ edge }); ts.push_back({ double(i - 1) + m }); open = true; }
            }
            if (prevVis) open = false;
        }
        // 0.9.7 变形：跨 180° 经线的地方断开（不然一条横线穿过整个屏幕）
        if (v && open && !runs.back().empty() && std::abs(s.x - runs.back().back().x) > cam.wrapJumpPx()) open = false;
        if (v) {
            if (!open) { runs.emplace_back(); ts.emplace_back(); open = true; }
            runs.back().push_back(s);
            ts.back().push_back(double(i));
        } else {
            open = false;
        }
        prevVis = v;
    }
    std::vector<ScreenRun> out;
    if (tOut) tOut->clear();
    for (size_t i = 0; i < runs.size(); ++i) {
        if (runs[i].size() < 2) continue;
        out.push_back(std::move(runs[i]));
        if (tOut) tOut->push_back(std::move(ts[i]));
    }
    return out;
}

double distToRuns(const std::vector<ScreenRun> &runs, double x, double y)
{
    double best = 1e18;
    for (const ScreenRun &r : runs)
        for (size_t j = 1; j < r.size(); ++j) {
            const Pt2 &a = r[j - 1], &b = r[j];
            const double dx = b.x - a.x, dy = b.y - a.y, L2 = dx * dx + dy * dy;
            double t = L2 > 0 ? ((x - a.x) * dx + (y - a.y) * dy) / L2 : 0;
            t = std::clamp(t, 0.0, 1.0);
            best = std::min(best, std::hypot(x - a.x - t * dx, y - a.y - t * dy));
        }
    return best;
}

double runsLength(const std::vector<ScreenRun> &runs)
{
    double L = 0;
    for (const ScreenRun &r : runs)
        for (size_t j = 1; j < r.size(); ++j) L += std::hypot(r[j].x - r[j - 1].x, r[j].y - r[j - 1].y);
    return L;
}

VehiclePlan planVehicle(const std::vector<double> &legScreenPx)
{
    VehiclePlan vp;
    for (size_t i = 0; i < legScreenPx.size(); ++i) {
        const double px = legScreenPx[i];
        if (!(px >= kVehicleMinPx)) continue;
        vp.legs.push_back({ int(i), std::clamp(px / kVehiclePxPerMs, kVehicleMinMs, kVehicleMaxMs) });
    }
    if (vp.legs.empty()) return vp;
    for (const VehicleLeg &l : vp.legs) vp.totalMs += l.ms + kVehicleStopMs;
    vp.totalMs += kVehicleEndMs - kVehicleStopMs;   // 最后一站停 1 秒（不是 0.3 秒）再重来
    return vp;
}

bool vehicleAt(const VehiclePlan &vp, double ms, int *leg, double *frac)
{
    if (vp.legs.empty()) return false;
    double t = ms;
    for (size_t i = 0; i < vp.legs.size(); ++i) {
        const VehicleLeg &l = vp.legs[i];
        if (t < l.ms) {
            *leg = l.leg;
            const double u = std::clamp(t / l.ms, 0.0, 1.0);
            *frac = u < 0.5 ? 2 * u * u : 1 - 2 * (1 - u) * (1 - u);   // 两头慢、中间快
            return true;
        }
        t -= l.ms;
        const double pause = i + 1 == vp.legs.size() ? kVehicleEndMs : kVehicleStopMs;
        if (t < pause) { *leg = l.leg; *frac = 1.0; return false; }
        t -= pause;
    }
    *leg = vp.legs.back().leg;
    *frac = 1.0;
    return false;
}

Vec3 pointAlong(const RouteLeg &leg, double frac, Vec3 *ahead)
{
    if (leg.pts.empty()) return {};
    if (leg.pts.size() == 1) { if (ahead) *ahead = leg.pts[0]; return leg.pts[0]; }
    frac = std::clamp(frac, 0.0, 1.0);
    auto at = [&leg](double f) {
        const auto it = std::lower_bound(leg.t.begin(), leg.t.end(), f);
        size_t j = size_t(std::max<std::ptrdiff_t>(1, it - leg.t.begin()));
        j = std::min(j, leg.pts.size() - 1);
        const double t0 = leg.t[j - 1], t1 = leg.t[j];
        const double u = t1 > t0 ? (f - t0) / (t1 - t0) : 1.0;
        return leg.pts[j - 1] * (1 - u) + leg.pts[j] * u;
    };
    const Vec3 p = at(frac);
    if (ahead) {
        const double d = 0.01;
        *ahead = frac + d <= 1.0 ? at(frac + d) : p + (p - at(frac - d));
    }
    return p;
}

} // namespace tp::map::globe
