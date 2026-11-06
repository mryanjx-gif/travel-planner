#include "night.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace tp::map::night {

using namespace geo;

LatLng subsolarPoint(double ms)
{
    const double d = ms / 86400000.0 - 10957.5;   // 距 J2000.0（2000-01-01 12:00 UTC）的天数
    const double g = (357.529 + 0.98560028 * d) * D2R;
    const double q = 280.459 + 0.98564736 * d;
    const double L = (q + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g)) * D2R;
    const double e = (23.439 - 0.00000036 * d) * D2R;
    const double ra = std::atan2(std::cos(e) * std::sin(L), std::cos(L)) * R2D;
    const double dec = std::asin(std::sin(e) * std::sin(L)) * R2D;
    const double gmst = std::fmod(std::fmod(18.697374558 + 24.06570982441908 * d, 24.0) + 24.0, 24.0);
    return { dec, normLng(ra - gmst * 15) };
}

double targetAlpha(double alt)
{
    if (alt >= kDayAlt) return 0;
    return kAlpha * smoothstep(std::min(1.0, (kDayAlt - alt) / kTwilight));
}

double sunAltitude(LatLng sun, double lat, double lng)
{
    const double s = std::sin(lat * D2R) * std::sin(sun.lat * D2R)
                     + std::cos(lat * D2R) * std::cos(sun.lat * D2R) * std::cos((lng - sun.lng) * D2R);
    return std::asin(std::clamp(s, -1.0, 1.0)) * R2D;
}

double stackedAlpha(const std::vector<Band> &bands, double alt)
{
    double keep = 1;
    for (const Band &b : bands)
        if (alt < b.alt) keep *= 1 - b.opacity;
    return 1 - keep;
}

namespace {
// 以 c 为中心、角半径 r（度）的小圆，返回经度连续展开的环；含极点时补上贴着极点的那条边
std::vector<LatLng> capRing(LatLng c, double r, int samples)
{
    const double p1 = c.lat * D2R, l1 = c.lng * D2R, rr = r * D2R;
    std::vector<LatLng> pts;
    pts.reserve(size_t(samples) + 4);
    double prev = 0;
    for (int i = 0; i < samples; ++i) {
        const double th = 2 * kPi * i / samples;
        const double p2 = std::asin(std::sin(p1) * std::cos(rr) + std::cos(p1) * std::sin(rr) * std::cos(th));
        double l2 = (l1 + std::atan2(std::sin(th) * std::sin(rr) * std::cos(p1), std::cos(rr) - std::sin(p1) * std::sin(p2))) * R2D;
        if (i > 0) l2 = unwrapTo(l2, prev);
        prev = l2;
        pts.push_back({ std::clamp(p2 * R2D, -kMaxLat, kMaxLat), l2 });
    }
    const bool north = 90 - c.lat < r, south = 90 + c.lat < r;
    if (north || south) {
        // 环绕一整圈（经度变化 ±360）：首点重复到 +360 那边，再沿极点那条纬线回来
        LatLng first = pts.front();
        const double dir = pts.back().lng - pts.front().lng > 0 ? 360 : -360;
        pts.push_back({ first.lat, first.lng + dir });
        const double poleLat = north ? kMaxLat : -kMaxLat;
        pts.push_back({ poleLat, first.lng + dir });
        pts.push_back({ poleLat, first.lng });
    }
    pts.push_back(pts.front());
    return pts;
}
} // namespace

std::vector<Band> bands(LatLng sun, int n, int samples)
{
    const LatLng anti{ -sun.lat, normLng(sun.lng + 180) };
    std::vector<Band> out;
    double prevA = 0;
    for (int k = 1; k <= n + 1; ++k) {
        Band b;
        b.alt = kDayAlt - kTwilight * (k - 1) / n;
        // 这一层到下一层之间的地方，叠起来应该有多深（取中点；最里面一层 = 全黑 .62）
        const double target = k <= n ? targetAlpha(kDayAlt - kTwilight * (k - 0.5) / n) : kAlpha;
        b.opacity = std::clamp(1 - (1 - target) / (1 - prevA), 0.0, 1.0);
        prevA = target;
        b.ring = capRing(anti, 90 + b.alt, samples);   // 太阳高度 < alt ⇔ 离反太阳点 < 90°+alt
        out.push_back(std::move(b));
    }
    return out;
}

QString toGeoJson(const std::vector<Band> &bands)
{
    QJsonArray feats;
    for (const Band &b : bands) {
        QJsonArray ring;
        for (const LatLng &p : b.ring) ring.append(QJsonArray{ p.lng, p.lat });
        // 注意：不能写 QJsonArray{ ring }——只有一个元素、类型又是 QJsonArray 时，MSVC 当成复制构造（GCC 当成一个元素的列表），
        // Windows 上多边形少套一层，maplibre 报 "Polygon must be described by 4 or more coordinate points"（0.8.0 PO 验证发现）
        QJsonArray rings;
        rings.append(ring);
        feats.append(QJsonObject{ { "type", "Feature" }, { "properties", QJsonObject{ { "a", b.opacity } } },
                                  { "geometry", QJsonObject{ { "type", "Polygon" }, { "coordinates", rings } } } });
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{ { "type", "FeatureCollection" }, { "features", feats } }).toJson(QJsonDocument::Compact));
}

// ---------------- 0.8.7：互不重叠的细带 ----------------

bool capOnMeridian(LatLng c, double r, double lng, double *lo, double *hi)
{
    // 球冠：sinφ·sinφc + cosφ·cosφc·cosΔλ ≥ cos r。令 A = sinφc、B = cosφc·cosΔλ，左边 = R·sin(φ + δ)（R = √(A²+B²)，δ = atan2(B, A)）
    const double A = std::sin(c.lat * D2R), B = std::cos(c.lat * D2R) * std::cos((lng - c.lng) * D2R), C = std::cos(r * D2R);
    const double R = std::hypot(A, B);
    if (R < 1e-12 || C > R) return false;
    const double al = std::asin(std::clamp(C / R, -1.0, 1.0)), de = std::atan2(B, A);
    // φ + δ ∈ [al, π - al]（再减 2π 的那一份也算），和 [-π/2, π/2] 取交；球冠是凸的，交只有一段
    double best0 = 1e9, best1 = -1e9;
    for (double shift : { 0.0, -2 * kPi, 2 * kPi }) {
        const double a = std::max(al - de + shift, -kPi / 2), b = std::min(kPi - al - de + shift, kPi / 2);
        if (a <= b) { best0 = std::min(best0, a); best1 = std::max(best1, b); }
    }
    if (best0 > best1) return false;
    *lo = std::clamp(best0 * R2D, -kMaxLat, kMaxLat);
    *hi = std::clamp(best1 * R2D, -kMaxLat, kMaxLat);
    return true;
}

namespace {
// 球冠左右两端的经度（经线和小圆相切处），没有（含极点、或球冠太小）时返回 false
bool capEnds(LatLng c, double r, double *west, double *east)
{
    const double sc = std::sin(c.lat * D2R), cc = std::cos(c.lat * D2R), C = std::cos(r * D2R);
    if (cc < 1e-9 || C * C < sc * sc) return false;                // 含极点：每条经线都有交
    const double k = std::sqrt(C * C - sc * sc) / cc;              // R = C 时的 |cosΔλ|
    if (k > 1) return false;
    const double d = std::acos(C >= 0 ? k : -k) * R2D;              // 球冠小于半球时 cosΔλ > 0
    *west = c.lng - d;
    *east = c.lng + d;
    return true;
}
} // namespace

std::vector<Stripe> stripes(LatLng sun, int n, int columns)
{
    const LatLng anti{ -sun.lat, normLng(sun.lng + 180) };
    // 边界：第 k 条线是"太阳高度 = kDayAlt - kTwilight·k/n"（k = 0…n），球冠半径 90 + 高度
    std::vector<double> alts, radii;
    for (int k = 0; k <= n; ++k) { alts.push_back(kDayAlt - kTwilight * k / n); radii.push_back(90 + alts.back()); }
    // 所有带共用一套经线：均匀的 columns 列 + 每个球冠的两个端点（相邻的带在同一组经线上取边界，共用的边一模一样，不会有缝）
    std::vector<double> lngs;
    for (int j = 0; j <= columns; ++j) lngs.push_back(anti.lng - 180 + 360.0 * j / columns);
    for (double r : radii) {
        double w, e;
        if (capEnds(anti, r, &w, &e)) {
            for (double x : { w, e })
                if (x > anti.lng - 180 && x < anti.lng + 180) lngs.push_back(x);
        }
    }
    std::sort(lngs.begin(), lngs.end());
    lngs.erase(std::unique(lngs.begin(), lngs.end(), [](double a, double b) { return std::abs(a - b) < 1e-9; }), lngs.end());
    const int L = int(lngs.size());
    // iv[k][j]：第 k 个球冠在第 j 条经线上的区间
    struct Iv { bool ok = false; double lo = 0, hi = 0; };
    std::vector<std::vector<Iv>> iv(size_t(n + 1), std::vector<Iv>(size_t(L)));
    for (int k = 0; k <= n; ++k)
        for (int j = 0; j < L; ++j) {
            Iv &v = iv[size_t(k)][size_t(j)];
            v.ok = capOnMeridian(anti, radii[size_t(k)], lngs[size_t(j)], &v.lo, &v.hi);
        }
    // 切点那条经线上外圈区间缩成一点：数值误差可能算成"没有交"，补成一点
    for (int k = 0; k <= n; ++k) {
        double w, e;
        if (!capEnds(anti, radii[size_t(k)], &w, &e)) continue;
        for (int j = 0; j < L; ++j)
            if (!iv[size_t(k)][size_t(j)].ok && (std::abs(lngs[size_t(j)] - w) < 1e-7 || std::abs(lngs[size_t(j)] - e) < 1e-7)) {
                // 用两边相邻经线区间中点的平均当这一点（足够准：只是一个尖）
                double sum = 0; int cnt = 0;
                for (int dj : { -1, 1 }) {
                    const int jj = j + dj;
                    if (jj >= 0 && jj < L && iv[size_t(k)][size_t(jj)].ok) { sum += (iv[size_t(k)][size_t(jj)].lo + iv[size_t(k)][size_t(jj)].hi) / 2; ++cnt; }
                }
                if (cnt) iv[size_t(k)][size_t(j)] = { true, sum / cnt, sum / cnt };
            }
    }
    auto closeRing = [](std::vector<LatLng> &ring) { if (ring.size() >= 3) ring.push_back(ring.front()); };
    std::vector<Stripe> out;
    for (int k = 0; k <= n; ++k) {
        Stripe st;
        st.hiAlt = alts[size_t(k)];
        st.loAlt = k < n ? alts[size_t(k + 1)] : -90;
        st.opacity = k < n ? targetAlpha((st.hiAlt + st.loAlt) / 2) : kAlpha;
        const std::vector<Iv> &outer = iv[size_t(k)];
        // 外圈有交的连续几列（球冠是凸的，一般只有一段；含极点时是整圈）
        int j0 = 0;
        while (j0 < L) {
            while (j0 < L && !outer[size_t(j0)].ok) ++j0;
            if (j0 >= L) break;
            int j1 = j0;
            while (j1 + 1 < L && outer[size_t(j1 + 1)].ok) ++j1;
            if (k == n) {   // 最里面：整段区间
                std::vector<LatLng> ring;
                for (int j = j0; j <= j1; ++j) ring.push_back({ outer[size_t(j)].lo, lngs[size_t(j)] });
                for (int j = j1; j >= j0; --j) ring.push_back({ outer[size_t(j)].hi, lngs[size_t(j)] });
                closeRing(ring);
                if (ring.size() >= 4) st.polygons.push_back(std::move(ring));
            } else {
                // 内圈的边：有交时是内圈区间，没交时取外圈区间的中点（上下两段在这里接上，共用同一条边）
                const std::vector<Iv> &inner = iv[size_t(k + 1)];
                std::vector<double> midLo, midHi;
                for (int j = j0; j <= j1; ++j) {
                    if (inner[size_t(j)].ok) { midLo.push_back(inner[size_t(j)].lo); midHi.push_back(inner[size_t(j)].hi); }
                    else { const double m = (outer[size_t(j)].lo + outer[size_t(j)].hi) / 2; midLo.push_back(m); midHi.push_back(m); }
                }
                double hLo = 0, hHi = 0;
                std::vector<LatLng> lower, upper;
                for (int j = j0; j <= j1; ++j) { lower.push_back({ outer[size_t(j)].lo, lngs[size_t(j)] }); hLo = std::max(hLo, midLo[size_t(j - j0)] - outer[size_t(j)].lo); }
                for (int j = j1; j >= j0; --j) lower.push_back({ midLo[size_t(j - j0)], lngs[size_t(j)] });
                for (int j = j0; j <= j1; ++j) { upper.push_back({ midHi[size_t(j - j0)], lngs[size_t(j)] }); hHi = std::max(hHi, outer[size_t(j)].hi - midHi[size_t(j - j0)]); }
                for (int j = j1; j >= j0; --j) upper.push_back({ outer[size_t(j)].hi, lngs[size_t(j)] });
                closeRing(lower);
                closeRing(upper);
                if (hLo > 1e-9 && lower.size() >= 4) st.polygons.push_back(std::move(lower));
                if (hHi > 1e-9 && upper.size() >= 4) st.polygons.push_back(std::move(upper));
            }
            j0 = j1 + 1;
        }
        out.push_back(std::move(st));
    }
    return out;
}

int stripeAt(const std::vector<Stripe> &s, double lat, double lng)
{
    for (int i = 0; i < int(s.size()); ++i)
        for (const auto &ring : s[size_t(i)].polygons)
            for (double off : { 0.0, 360.0, -360.0 }) {
                const double x = lng + off;
                bool in = false;
                for (size_t a = 0, b = ring.size() - 1; a < ring.size(); b = a++) {
                    const double xa = ring[a].lng, ya = ring[a].lat, xb = ring[b].lng, yb = ring[b].lat;
                    if (((ya > lat) != (yb > lat)) && x < (xb - xa) * (lat - ya) / (yb - ya) + xa) in = !in;
                }
                if (in) return i;
            }
    return -1;
}

QString toGeoJson(const std::vector<Stripe> &s)
{
    // 一条带一个 MultiPolygon；坐标留 3 位小数（约 100 米，缩放 ≤ 4.5 时看不出来），字符串小一半
    QString out;
    out.reserve(600000);
    out += QStringLiteral("{\"type\":\"FeatureCollection\",\"features\":[");
    bool firstF = true;
    for (const Stripe &st : s) {
        if (st.polygons.empty() || st.opacity <= 0) continue;
        if (!firstF) out += u',';
        firstF = false;
        out += QStringLiteral("{\"type\":\"Feature\",\"properties\":{\"a\":%1},\"geometry\":{\"type\":\"MultiPolygon\",\"coordinates\":[").arg(st.opacity, 0, 'g', 6);
        bool firstP = true;
        for (const auto &ring : st.polygons) {
            if (!firstP) out += u',';
            firstP = false;
            out += QStringLiteral("[[");
            for (size_t i = 0; i < ring.size(); ++i) {
                if (i) out += u',';
                out += u'[' + QString::number(ring[i].lng, 'f', 3) + u',' + QString::number(ring[i].lat, 'f', 3) + u']';
            }
            out += QStringLiteral("]]");
        }
        out += QStringLiteral("]}}");
    }
    out += QStringLiteral("]}");
    return out;
}

} // namespace tp::map::night
