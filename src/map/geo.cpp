#include "geo.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tp::map::geo {

double normLng(double lng) { return std::fmod(std::fmod(lng + 540.0, 360.0) + 360.0, 360.0) - 180.0; }

double unwrapTo(double lng, double ref)
{
    while (lng - ref > 180) lng -= 360;
    while (lng - ref < -180) lng += 360;
    return lng;
}

Pt project(double lat, double lng)
{
    const double la = std::clamp(lat, -kMaxLat, kMaxLat);
    const double s = std::sin(la * D2R);
    return { (lng + 180.0) / 360.0, 0.5 - std::log((1 + s) / (1 - s)) / (4 * kPi) };
}

LatLng unproject(double x, double y)
{
    const double n = kPi - 2 * kPi * y;
    return { R2D * std::atan(std::sinh(n)), x * 360.0 - 180.0 };
}

double distanceMeters(LatLng a, LatLng b)
{
    const double dLat = (b.lat - a.lat) * D2R, dLng = (b.lng - a.lng) * D2R;
    const double h = std::pow(std::sin(dLat / 2), 2) + std::cos(a.lat * D2R) * std::cos(b.lat * D2R) * std::pow(std::sin(dLng / 2), 2);
    return 2 * kEarthRadius * std::asin(std::min(1.0, std::sqrt(h)));
}

double distanceKm(LatLng a, LatLng b) { return distanceMeters(a, b) / 1000.0; }

namespace {
void toVec(double lat, double lng, double v[3])
{
    const double p = lat * D2R, l = lng * D2R;
    v[0] = std::cos(p) * std::cos(l);
    v[1] = std::cos(p) * std::sin(l);
    v[2] = std::sin(p);
}
LatLng toLatLng(double x, double y, double z) { return { std::atan2(z, std::hypot(x, y)) * R2D, std::atan2(y, x) * R2D }; }

// maplibre-gl defaultEasing = bezier(0.25, 0.1, 0.25, 1)（UnitBezier，牛顿迭代）
double unitBezier(double p1x, double p1y, double p2x, double p2y, double x)
{
    const double cx = 3 * p1x, bx = 3 * (p2x - p1x) - cx, ax = 1 - cx - bx;
    const double cy = 3 * p1y, by = 3 * (p2y - p1y) - cy, ay = 1 - cy - by;
    const auto sx = [&](double t) { return ((ax * t + bx) * t + cx) * t; };
    const auto sy = [&](double t) { return ((ay * t + by) * t + cy) * t; };
    const auto dx = [&](double t) { return (3 * ax * t + 2 * bx) * t + cx; };
    double t = x;
    for (int i = 0; i < 8; ++i) {
        const double e = sx(t) - x;
        if (std::abs(e) < 1e-7) return sy(t);
        const double d = dx(t);
        if (std::abs(d) < 1e-6) break;
        t -= e / d;
    }
    double lo = 0, hi = 1;
    t = x;
    for (int i = 0; i < 40 && lo < hi; ++i) {
        const double v = sx(t);
        if (std::abs(v - x) < 1e-7) break;
        if (x > v) lo = t; else hi = t;
        t = (hi - lo) / 2 + lo;
    }
    return sy(t);
}
} // namespace

std::vector<LatLng> greatCircle(LatLng a, LatLng b)
{
    double va[3], vb[3];
    toVec(a.lat, a.lng, va);
    toVec(b.lat, b.lng, vb);
    const double dot = std::clamp(va[0] * vb[0] + va[1] * vb[1] + va[2] * vb[2], -1.0, 1.0);
    const double w = std::acos(dot), deg = w * R2D;
    if (deg < 3 || kPi - w < 1e-6) return { a, b };   // 约 330 km 内直接直线；对跖点没有唯一大圆
    const int n = int(std::ceil(deg / 1.5));
    const double s = std::sin(w);
    std::vector<LatLng> pts;
    pts.reserve(size_t(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const double t = double(i) / n, k1 = std::sin((1 - t) * w) / s, k2 = std::sin(t * w) / s;
        pts.push_back(toLatLng(k1 * va[0] + k2 * vb[0], k1 * va[1] + k2 * vb[1], k1 * va[2] + k2 * vb[2]));
    }
    return pts;
}

LatLng centerOf(const std::vector<LatLng> &pts)
{
    double x = 0, y = 0, z = 0;
    for (const LatLng &p : pts) {
        double v[3];
        toVec(p.lat, p.lng, v);
        x += v[0]; y += v[1]; z += v[2];
    }
    return toLatLng(x, y, z);
}

// ---- 相机 ----

Pt Camera::pixelForRaw(double plat, double plng) const
{
    const double ws = worldSize();
    const Pt c = project(lat, lng), p = project(plat, plng);
    return { width / 2 + (p.x - c.x) * ws, height / 2 + (p.y - c.y) * ws };
}

Pt Camera::pixelFor(double plat, double plng) const { return pixelForRaw(plat, unwrapTo(plng, lng)); }

LatLng Camera::coordinateFor(double px, double py) const
{
    const double ws = worldSize();
    const Pt c = project(lat, lng);
    return unproject(c.x + (px - width / 2) / ws, c.y + (py - height / 2) / ws);
}

void Camera::zoomAround(double newZoom, double px, double py)
{
    const LatLng anchor = coordinateFor(px, py);
    zoom = newZoom;
    // 让 anchor 仍然在 (px,py)：新的中心 = anchor 的世界坐标 - (px - 中心) / 新世界宽
    const double ws = worldSize();
    const Pt a = project(anchor.lat, unwrapTo(anchor.lng, lng));
    const LatLng c = unproject(a.x - (px - width / 2) / ws, a.y - (py - height / 2) / ws);
    lat = c.lat;
    lng = c.lng;
}

void Camera::panBy(double dx, double dy)
{
    const LatLng c = coordinateFor(width / 2 - dx, height / 2 - dy);
    lat = c.lat;
    lng = c.lng;
}

double Camera::metersPerPixelAtCenter() const
{
    const double x = width / 2, y = height / 2;
    return distanceMeters(coordinateFor(x - 50, y), coordinateFor(x + 50, y)) / 100.0;
}

Camera constrain(Camera c, double minZoom, double maxZoom)
{
    const double lo = std::max(minZoom, std::log2(std::max(1.0, c.height) / kTile));
    c.zoom = std::clamp(c.zoom, lo, std::max(lo, maxZoom));
    const double ws = c.worldSize();
    Pt p = project(c.lat, c.lng);
    const double half = c.height / 2 / ws;   // 半个窗口高度（世界坐标）
    p.y = half >= 0.5 ? 0.5 : std::clamp(p.y, half, 1 - half);
    c.lat = unproject(p.x, p.y).lat;
    c.lng = normLng(c.lng);
    return c;
}

double minZoomForScale(const Camera &c, double minScaleKm)
{
    // Electron 在缩放过程中反复量、反复修正（100 像素跨十几度时距离和像素不完全成正比）；这里直接迭代到不动点
    const double maxMpp = minScaleKm * 2 * 1000 / 100 * 0.97;   // 每像素最多多少米（留 3% 余量）
    Camera k = c;
    double z = c.zoom;
    for (int i = 0; i < 6; ++i) {
        const double mpp = k.metersPerPixelAtCenter();
        if (!(mpp > 0) || !std::isfinite(mpp)) return 0;
        const double nz = k.zoom + std::log2(mpp / maxMpp);
        if (std::abs(nz - z) < 1e-6 && i > 0) { z = nz; break; }
        z = nz;
        k.zoom = std::clamp(z, -2.0, 8.0);
    }
    return std::clamp(z, 0.0, 6.0);
}

FitResult fitBounds(const Camera &c, double west, double south, double east, double north, Padding pad, double maxZoom)
{
    FitResult r;
    const Pt nw = project(north, west), se = project(south, east);
    const double sx = (se.x - nw.x), sy = (se.y - nw.y);   // 世界坐标里的大小
    const double availW = c.width - pad.left - pad.right, availH = c.height - pad.top - pad.bottom;
    if (availW < 0 || availH < 0) return r;
    // 单点（或很小）：maplibre-gl 这时缩放到 maxZoom
    double zoom = maxZoom;
    if (sx > 0 || sy > 0) {
        const double scaleX = sx > 0 ? availW / (sx * kTile) : std::numeric_limits<double>::infinity();
        const double scaleY = sy > 0 ? availH / (sy * kTile) : std::numeric_limits<double>::infinity();
        zoom = std::min(std::log2(std::min(scaleX, scaleY)), maxZoom);
    }
    const double ws = kTile * std::exp2(zoom);
    const double offX = (pad.left - pad.right) / 2, offY = (pad.top - pad.bottom) / 2;
    const LatLng ctr = unproject((nw.x + se.x) / 2 - offX / ws, (nw.y + se.y) / 2 - offY / ws);
    r.lat = ctr.lat;
    r.lng = normLng(ctr.lng);
    r.zoom = zoom;
    r.ok = std::isfinite(zoom);
    return r;
}

Padding fitPadding(double W, double H, double left, double right)
{
    const double room = W - 80;
    if (left + right > room) {
        const double k = std::max(0.0, room) / (left + right);
        left *= k;
        right *= k;
    }
    const double v = std::min(40.0, std::max(0.0, (H - 80) / 2));
    return { v, right, v, left };
}

// ---- 飞行（maplibre-gl Camera.flyTo） ----

FlyPath::FlyPath(const Camera &from, double toLat, double toLng, double toZoom, double curve)
    : m_startZoom(from.zoom), m_rho(curve)
{
    m_scale = std::exp2(toZoom - from.zoom);
    const double ws0 = from.worldSize();
    m_from = project(from.lat, from.lng);
    const Pt to = project(toLat, unwrapTo(toLng, from.lng));
    m_delta = { to.x - m_from.x, to.y - m_from.y };
    m_u1 = std::hypot(m_delta.x, m_delta.y) * ws0;   // 起点缩放下的像素距离
    m_w0 = std::max(from.width, from.height);
    m_w1 = m_w0 / m_scale;
    const double rho2 = m_rho * m_rho;
    const auto r = [&](int i) {
        const double b = (m_w1 * m_w1 - m_w0 * m_w0 + (i ? -1 : 1) * rho2 * rho2 * m_u1 * m_u1) / (2 * (i ? m_w1 : m_w0) * rho2 * m_u1);
        return std::log(std::sqrt(b * b + 1) - b);
    };
    m_r0 = r(0);
    m_S = (r(1) - m_r0) / m_rho;
    if (std::abs(m_u1) < 1e-6 || !std::isfinite(m_S)) {
        m_simple = true;
        m_S = std::abs(m_w0 - m_w1) < 1e-6 ? 0 : std::abs(std::log(m_w1 / m_w0)) / m_rho;
    }
}

double FlyPath::w(double s) const
{
    if (m_simple) return std::exp((m_w1 < m_w0 ? -1 : 1) * m_rho * s);
    return std::cosh(m_r0) / std::cosh(m_r0 + m_rho * s);
}

double FlyPath::u(double s) const
{
    if (m_simple) return 0;
    const double rho2 = m_rho * m_rho;
    return m_w0 * ((std::cosh(m_r0) * std::tanh(m_r0 + m_rho * s) - std::sinh(m_r0)) / rho2) / m_u1;
}

void FlyPath::at(double t, double *lat, double *lng, double *zoom) const
{
    const double k = t >= 1 ? 1.0 : unitBezier(0.25, 0.1, 0.25, 1, std::clamp(t, 0.0, 1.0));
    const double s = k * m_S;
    const double sc = 1 / w(s);
    *zoom = k >= 1 ? m_startZoom + std::log2(m_scale) : m_startZoom + std::log2(sc);
    const double uu = k >= 1 ? 1.0 : (m_simple ? 0.0 : u(s));
    const LatLng c = unproject(m_from.x + m_delta.x * uu, m_from.y + m_delta.y * uu);
    *lat = c.lat;
    *lng = normLng(c.lng);
}

double FlyPath::naturalDurationMs(double speed) const { return 1000.0 * m_S / speed; }

double easeDefault(double t) { return t >= 1 ? 1.0 : t <= 0 ? 0.0 : unitBezier(0.25, 0.1, 0.25, 1, t); }

TileXY tileXY(double lng, double lat, int z)
{
    // const n = 2 ** z, x = Math.floor((lng + 180) / 360 * n);
    // const r = lat * Math.PI / 180, y = Math.floor((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2 * n);
    // return [((x % n) + n) % n, Math.min(n - 1, Math.max(0, y))];
    const double n = std::exp2(z);
    const double xf = std::floor((lng + 180) / 360 * n);
    const double r = lat * kPi / 180;
    double yf = std::floor((1 - std::log(std::tan(r) + 1 / std::cos(r)) / kPi) / 2 * n);
    if (std::isnan(yf)) yf = 0;
    const double xm = std::fmod(std::fmod(xf, n) + n, n);
    return { z, int(xm), int(std::min(n - 1, std::max(0.0, yf))) };
}

std::vector<TileXY> prefetchTiles(double west, double north, double east, double south, double zoom, int tileSize, int tileMaxZoom)
{
    std::vector<TileXY> out;
    // Electron：Math.round(zoom) + (tileSize === 128 ? 1 : 0)。0.8.6 起卫星瓦片大小按屏幕缩放比例（128–256 之间任意值），
    // 推广成 round(zoom + log2(256 / tileSize))：128 和 256 时和 Electron 完全一样
    const int zoomBase = int(std::floor(zoom + std::log2(256.0 / std::max(1, tileSize)) + 0.5));
    for (const int z : { zoomBase + 1, zoomBase - 1 }) {
        if (z < 1 || z > tileMaxZoom) continue;
        const TileXY a = tileXY(west, north, z), b = tileXY(east, south, z);
        const int n = 1 << z;
        const int w = b.x >= a.x ? b.x - a.x + 1 : n - a.x + b.x + 1;
        if (static_cast<long long>(w) * (b.y - a.y + 1) > 200) continue;   // 这一级太大就不预取
        for (int i = 0; i < w; ++i)
            for (int y = a.y; y <= b.y; ++y) out.push_back({ z, (a.x + i) % n, y });
    }
    return out;
}

int satTileSize(double /*dpr*/)
{
    return 128;
}

double smoothstep(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

double easeCubicInOut(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

} // namespace tp::map::geo
