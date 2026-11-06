#include "globegeom.h"
#include "geo.h"

#include <algorithm>
#include <cmath>

namespace tp::map::globe {

namespace {
constexpr double D2R = M_PI / 180.0, R2D = 180.0 / M_PI;
}

double Vec3::length() const { return std::sqrt(x * x + y * y + z * z); }
Vec3 Vec3::normalized() const
{
    const double l = length();
    return l > 0 ? Vec3{ x / l, y / l, z / l } : Vec3{};
}

Vec3 toVec(double lat, double lng)
{
    const double p = lat * D2R, l = lng * D2R;
    return { std::cos(p) * std::cos(l), std::cos(p) * std::sin(l), std::sin(p) };
}

void toLatLng(const Vec3 &v0, double *lat, double *lng)
{
    const Vec3 v = v0.normalized();
    *lat = std::asin(std::clamp(v.z, -1.0, 1.0)) * R2D;
    *lng = std::atan2(v.y, v.x) * R2D;
}

double mercX(double lng) { return (lng + 180.0) / 360.0; }
double mercY(double lat)
{
    const double s = std::sin(std::clamp(lat, -kMaxMercLat, kMaxMercLat) * D2R);
    return 0.5 - std::log((1 + s) / (1 - s)) / (4 * M_PI);
}
double lngOfMercX(double x) { return x * 360.0 - 180.0; }
double latOfMercY(double y) { return std::atan(std::sinh(M_PI * (1 - 2 * y))) * R2D; }

double View::focal() const { return height / (2.0 * std::tan(kFovYDeg * D2R / 2.0)); }

Camera::Camera(const View &v) : m_v(v)
{
    m_eye = v.eye();
    m_fwd = (Vec3{} - m_eye).normalized();
    const Vec3 up0 = std::abs(v.lat) > 89.5 ? Vec3{ 0, 1, 0 } : Vec3{ 0, 0, 1 };   // 和 QMatrix4x4::lookAt 一样
    m_right = m_fwd.cross(up0).normalized();
    m_up = m_right.cross(m_fwd);
    m_f = v.focal();
    m_aspect = v.height > 0 ? v.width / v.height : 1;
    if (v.morph > 0) {
        m_mx0 = mercX(v.lng);
        m_my0 = mercY(v.lat);
        m_worldPx = flatWorldPx(v.dist, m_f, v.lat);
    }
}

void Camera::flatProject(const Vec3 &p, double *sx, double *sy) const
{
    double lat, lng;
    toLatLng(p, &lat, &lng);   // 抬起的飞行弧线在平面上贴地（只看方向）
    double dx = mercX(lng) - m_mx0;
    dx -= std::floor(dx + 0.5);   // 走近的那一边（跨 180° 经线）
    *sx = m_v.width / 2 + dx * m_worldPx;
    *sy = m_v.height / 2 + (mercY(std::clamp(lat, -kMaxMercLat, kMaxMercLat)) - m_my0) * m_worldPx;
}

bool Camera::project(const Vec3 &p, double *sx, double *sy) const
{
    const Vec3 d = p - m_eye;
    const double cz = d.dot(m_fwd);
    const double t = m_v.morph;
    if (t <= 0) {
        if (cz <= 1e-6) return false;
        *sx = m_v.width / 2 + d.dot(m_right) / cz * m_f;
        *sy = m_v.height / 2 - d.dot(m_up) / cz * m_f;
        return true;
    }
    // 0.9.7 变形：球面的屏幕位置和平面上的位置按比例混合（和瓦片着色器同一套）
    double fx, fy;
    flatProject(p, &fx, &fy);
    // 在相机后面、或者在球的背面（视线先碰到球）的点：球面上的位置没有意义（会挤到圆盘里），直接用平面上的位置
    // （变形到一半以前这些点由调用方当作看不见）
    if (cz <= 1e-6 || t >= 1 || occluded(p)) { *sx = fx; *sy = fy; return true; }
    const double gx = m_v.width / 2 + d.dot(m_right) / cz * m_f, gy = m_v.height / 2 - d.dot(m_up) / cz * m_f;
    *sx = gx + (fx - gx) * t;
    *sy = gy + (fy - gy) * t;
    return true;
}

bool Camera::pick(double sx, double sy, Vec3 *out) const
{
    const Vec3 dir = (m_fwd + m_right * ((sx - m_v.width / 2) / m_f) + m_up * (-(sy - m_v.height / 2) / m_f)).normalized();
    const double b = m_eye.dot(dir), c = m_eye.dot(m_eye) - 1.0;
    const double disc = b * b - c;
    if (disc < 0) return false;
    const double t = -b - std::sqrt(disc);
    if (t <= 0) return false;
    *out = m_eye + dir * t;
    return true;
}

bool Camera::facing(const Vec3 &p, double margin) const { return p.dot(m_eye) > 1.0 + margin; }

bool Camera::occluded(const Vec3 &p) const
{
    Vec3 d = p - m_eye;
    const double L = d.length();
    if (L <= 0) return false;
    d = d * (1.0 / L);
    const double b = m_eye.dot(d), c = m_eye.dot(m_eye) - 1.0, disc = b * b - c;
    if (disc <= 0) return false;
    return -b - std::sqrt(disc) < L - 1e-6;
}

double horizonAngle(double dist) { return std::acos(std::clamp(1.0 / std::max(1.0, dist), -1.0, 1.0)); }

double degreesInside(const View &v, double lat, double lng)
{
    const Vec3 p = toVec(lat, lng), e = toVec(v.lat, v.lng);
    const double th = std::acos(std::clamp(p.dot(e), -1.0, 1.0));
    return (horizonAngle(v.dist) - th) * R2D;
}

Limb limbOf(const View &v, double lat, double lng)
{
    const double in = degreesInside(v, lat, lng);
    auto smooth = [](double e0, double e1, double x) { const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3 - 2 * t); };
    Limb l;
    l.fade = smooth(0.0, kLimbFadeDeg, in);
    l.scale = kLimbMinScale + (1 - kLimbMinScale) * smooth(0.0, kLimbShrinkDeg, in);
    return l;
}

namespace {
// v 绕单位轴 k 转 ang 弧度（罗德里格斯公式）
Vec3 rotateAbout(const Vec3 &v, const Vec3 &k, double ang)
{
    const double c = std::cos(ang), s = std::sin(ang);
    return v * c + k.cross(v) * s + k * (k.dot(v) * (1 - c));
}
} // namespace

bool zoomAnchorCenter(const View &before, double newDist, double ax, double ay, double maxLat, double *lat, double *lng)
{
    Vec3 p0;
    *lat = before.lat;
    *lng = before.lng;
    if (!Camera(before).pick(ax, ay, &p0)) return false;
    p0 = p0.normalized();
    View cur = before;
    cur.dist = newDist;
    bool any = false;
    // 鼠标下的点 p0 现在在屏幕上离 (ax, ay) 多远（看不见算很远）
    auto err = [&p0, ax, ay](const View &v) {
        const Camera cam(v);
        double sx, sy;
        if (!cam.facing(p0) || !cam.project(p0, &sx, &sy)) return 1e9;
        return std::hypot(sx - ax, sy - ay);
    };
    // 最小旋转作用在中心方向上；相机的上方向跟着地轴重新算（会带一点滚转），所以转一次差一点点，再拾取、再转。
    // 极点附近滚转很大，再转反而更远：只留让误差变小的那几次（第一次总是留）
    for (int iter = 0; iter < 4; ++iter) {
        const View prev = cur;
        const double prevErr = iter > 0 ? err(cur) : 0;
        Vec3 p1;
        if (!Camera(cur).pick(ax, ay, &p1)) break;
        any = true;
        p1 = p1.normalized();
        const Vec3 axis = p1.cross(p0);
        const double s = axis.length();
        if (s < 1e-12) break;
        const Vec3 k = axis * (1.0 / s);
        const double ang = std::atan2(s, p1.dot(p0));
        // 再转只做小修正、只在中纬度做（高纬度时中心一动画面就跟着地轴滚转，鼠标下的点会换到别处，越修越远）
        if (iter > 0 && (std::abs(cur.lat) > 60.0 || ang > 2.0 * D2R)) break;
        const Vec3 c0 = toVec(cur.lat, cur.lng);
        double la, ln;
        toLatLng(rotateAbout(c0, k, ang), &la, &ln);
        bool clamped = false;
        if (std::abs(la) > maxLat) {
            // 转到纬度上限为止（二分找比例），剩下的不转（不绕极点打转）
            double lo = 0, hi = 1;
            for (int i = 0; i < 24; ++i) {
                const double m = (lo + hi) / 2;
                double l2, n2;
                toLatLng(rotateAbout(c0, k, ang * m), &l2, &n2);
                (std::abs(l2) > maxLat ? hi : lo) = m;
            }
            toLatLng(rotateAbout(c0, k, ang * lo), &la, &ln);
            la = std::clamp(la, -maxLat, maxLat);
            clamped = true;
        }
        cur.lat = la;
        cur.lng = ln;
        if (iter > 0 && err(cur) >= prevErr) { cur = prev; break; }
        if (clamped || ang < 1e-9 || err(cur) < 0.05) break;
    }
    // 极点附近（|纬度| 70° → 84°）补偿渐渐减到 0，变成以画面中心缩放：那里中心一动画面就绕极点滚转，
    // 硬要把鼠标下的点留在原处，地球会跟着打转（PO 11:02 报的抽搐的另一半）
    const double w = 1.0 - std::clamp((std::abs(before.lat) - 70.0) / 14.0, 0.0, 1.0);
    const double wk = w * w * (3 - 2 * w);
    if (wk < 1.0) {
        double la, ln;
        slerpCenter(before.lat, before.lng, cur.lat, cur.lng, wk, &la, &ln);
        cur.lat = std::clamp(la, -maxLat, maxLat);
        cur.lng = ln;
    }
    *lat = cur.lat;
    *lng = cur.lng;
    return any;
}

double lerpDist(double from, double to, double t)
{
    const double a0 = std::max(1e-6, from - 1.0), a1 = std::max(1e-6, to - 1.0);
    return 1.0 + a0 * std::pow(a1 / a0, t);
}

double angleBetween(double lat0, double lng0, double lat1, double lng1)
{
    return std::acos(std::clamp(toVec(lat0, lng0).dot(toVec(lat1, lng1)), -1.0, 1.0));
}

void slerpCenter(double lat0, double lng0, double lat1, double lng1, double t, double *lat, double *lng)
{
    const Vec3 a = toVec(lat0, lng0), b = toVec(lat1, lng1);
    const double ang = std::acos(std::clamp(a.dot(b), -1.0, 1.0));
    if (ang < 1e-9) { *lat = lat1; *lng = lng1; return; }
    const double s = std::sin(ang);
    Vec3 p;
    if (s < 1e-9) p = a * (1 - t) + b * t;   // 正好相对：退化成直线插值（再归一）
    else p = a * (std::sin((1 - t) * ang) / s) + b * (std::sin(t * ang) / s);
    toLatLng(p.normalized(), lat, lng);
}

double zoomForDist(double dist, double focalLogical, double lat)
{
    const double c = std::max(0.05, std::cos(lat * D2R));
    return std::log2(focalLogical / std::max(1e-6, dist - 1.0) * 2 * M_PI * c / geo::kTile);
}

double distForZoom(double zoom, double focalLogical, double lat)
{
    const double c = std::max(0.05, std::cos(lat * D2R));
    return 1.0 + focalLogical * 2 * M_PI * c / (geo::kTile * std::exp2(zoom));
}

std::vector<double> mulberry32(quint32 seed, int n)
{
    // seed = seed + 0x6D2B79F5 | 0; t = Math.imul(seed ^ seed >>> 15, 1 | seed);
    // t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296
    std::vector<double> out;
    out.reserve(size_t(std::max(0, n)));
    quint32 s = seed;
    for (int i = 0; i < n; ++i) {
        s += 0x6D2B79F5u;
        quint32 t = (s ^ (s >> 15)) * (1u | s);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        out.push_back(double(t ^ (t >> 14)) / 4294967296.0);
    }
    return out;
}

double flatWorldPx(double dist, double focal, double lat)
{
    const double c = std::max(0.05, std::cos(lat * D2R));   // 和 zoomForDist 一样夹住
    return focal / std::max(1e-6, dist - 1.0) * 2 * M_PI * c;
}

double maxDistFor(double focalLogical)
{
    // MIN_SCALE_KM = 1000：每像素最多 1000 × 2 × 1000 / 100 × 0.97 = 19400 米
    const double maxMpp = 1000.0 * 2 * 1000 / 100 * 0.97;
    return std::clamp(1.0 + focalLogical * maxMpp / kEarthRadiusM, 1.6, kMaxDistCap);
}

namespace {

struct Selector {
    Camera cam;
    SelectOptions opt;
    double thr;
    double subLat, subLng;
    double x0, y0, x1, y1;   // 屏幕范围（含边距）
    Selection out;

    explicit Selector(const View &v, const SelectOptions &o, double t)
        : cam(v), opt(o), thr(t), subLat(v.lat), subLng(v.lng)
    {
        const double m = 0.06 * std::max(v.width, v.height);
        x0 = -m; y0 = -m; x1 = v.width + m; y1 = v.height + m;
    }

    void visit(int z, int x, int y)
    {
        ++out.visited;
        const int N = z <= 3 ? 6 : z <= 6 ? 4 : 3;
        const double n = std::exp2(z);
        // 相机正下方的点在这块里：一定要（拉得很近时整块比屏幕还大，采样点可能都在屏幕外）
        const double smx = mercX(subLng) * n - x, smy = mercY(subLat) * n - y;
        const bool holdsSub = smx >= 0 && smx <= 1 && smy >= 0 && smy <= 1;
        double px[7][7], py[7][7];
        bool ok[7][7];
        bool any = false;
        double bx0 = 1e18, by0 = 1e18, bx1 = -1e18, by1 = -1e18;
        for (int j = 0; j <= N; ++j) {
            const double lat = latOfMercY((y + double(j) / N) / n);
            for (int i = 0; i <= N; ++i) {
                const Vec3 p = toVec(lat, lngOfMercX((x + double(i) / N) / n));
                ok[j][i] = cam.facing(p, -0.015) && cam.project(p, &px[j][i], &py[j][i]);
                if (!ok[j][i]) continue;
                any = true;
                bx0 = std::min(bx0, px[j][i]); bx1 = std::max(bx1, px[j][i]);
                by0 = std::min(by0, py[j][i]); by1 = std::max(by1, py[j][i]);
            }
        }
        if (!any && !holdsSub) return;
        if (!holdsSub && (bx1 < x0 || bx0 > x1 || by1 < y0 || by0 > y1)) return;   // 整块在屏幕外
        // 屏幕上的边长：每一行 / 每一列相邻采样点的距离加起来，取最长的
        double edge = 0;
        for (int j = 0; j <= N; ++j) {
            double row = 0, col = 0;
            for (int i = 0; i < N; ++i) {
                if (ok[j][i] && ok[j][i + 1]) row += std::hypot(px[j][i + 1] - px[j][i], py[j][i + 1] - py[j][i]);
                if (ok[i][j] && ok[i + 1][j]) col += std::hypot(px[i + 1][j] - px[i][j], py[i + 1][j] - py[i][j]);
            }
            edge = std::max({ edge, row, col });
        }
        if (holdsSub && edge == 0) edge = thr * 2;   // 采样点都看不见但相机就在它上面：一定再细分
        if (z < opt.maxZ && edge > thr) {
            for (int k = 0; k < 4; ++k) visit(z + 1, x * 2 + (k & 1), y * 2 + (k >> 1));
            return;
        }
        out.tiles.push_back({ z, x, y });
        out.maxZ = std::max(out.maxZ, z);
    }
};

} // namespace

Selection selectTiles(const View &v, const SelectOptions &opt)
{
    double thr = opt.maxTilePx;
    Selection best;
    for (int attempt = 0; attempt < 6; ++attempt) {
        Selector s(v, opt, thr);
        const int n = 1 << opt.minZ;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) s.visit(opt.minZ, x, y);
        s.out.usedTilePx = thr;
        best = std::move(s.out);
        if (int(best.tiles.size()) <= opt.budget) break;
        thr *= 1.35;   // 超预算（很大的屏幕）：整体放粗一点
    }
    return best;
}

} // namespace tp::map::globe
