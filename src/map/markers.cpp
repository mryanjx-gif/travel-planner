#include "markers.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace tp::map::markers {

Style stopStyle(const TripPlan &plan, int i, int n)
{
    const bool isRoute = plan.type == QLatin1String("route") && n > 1;
    const TripStop &s = plan.stops[i];
    Style st;
    st.color = TripData::stopColor(plan, s);
    st.icon = !s.icon.isEmpty() ? s.icon : plan.icon;
    st.shape = !s.shape.isEmpty() ? s.shape : !plan.shape.isEmpty() ? plan.shape : (isRoute ? QStringLiteral("circle") : QStringLiteral("pin"));
    if (!isRoute) return st;
    const QString mode = plan.markerMode.isEmpty() ? QStringLiteral("number") : plan.markerMode;
    if (mode == QLatin1String("number")) {
        // 老样式：没指定外形时终点用圆角方块
        if (s.shape.isEmpty() && plan.shape.isEmpty() && i == n - 1) st.shape = QStringLiteral("square");
        st.icon.clear();
        st.text = QString::number(i + 1);
        return st;
    }
    if (mode == QLatin1String("dot")) { st.icon.clear(); st.dot = true; return st; }
    return st;   // 'icon'（以及认不出的取值）：用图标
}

bool showsIcon(const Style &st, bool iconReady) { return !st.icon.isEmpty() && iconReady; }

Geom geomFor(const Style &st, bool iconReady)
{
    Geom g;
    if (st.shape == QLatin1String("pin")) {
        g.w = 28; g.h = 38; g.ax = 14; g.ay = 37; g.tipY = -32;
        return g;
    }
    const double size = st.dot ? 14 : showsIcon(st, iconReady) ? 26 : 22;
    g.w = g.h = size;
    g.ax = g.ay = size / 2;
    g.tipY = -size / 2;
    return g;
}

int clusterSize(int n) { return n < 10 ? 26 : n < 50 ? 28 : 32; }

// ---- 颜色（Electron hexToRgb / rgbToOklab / oklabToRgb / averageColor / isLightColor） ----
bool hexToRgb(const QString &hex, double rgb[3])
{
    QString h = hex.trimmed();
    if (const int k = h.indexOf(QLatin1Char('#')); k >= 0) h.remove(k, 1);   // JS replace('#', '')：只去掉第一个
    if (h.size() == 3) h = QString(h[0]) + h[0] + h[1] + h[1] + h[2] + h[2];
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{6}$"));
    if (!re.match(h).hasMatch()) return false;
    for (int i = 0; i < 3; ++i) rgb[i] = h.mid(i * 2, 2).toInt(nullptr, 16) / 255.0;
    return true;
}

namespace {
double toLin(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double fromLin(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }
void rgbToOklab(const double in[3], double out[3])
{
    const double r = toLin(in[0]), g = toLin(in[1]), b = toLin(in[2]);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    out[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    out[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    out[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}
// JS Math.round：x.5 向 +∞ 舍入
int jsRound(double v) { return int(std::floor(v + 0.5)); }
} // namespace

QString averageColor(const QStringList &colors)
{
    double sum[3] = { 0, 0, 0 };
    int n = 0;
    for (const QString &c : colors) {
        double rgb[3], lab[3];
        if (!hexToRgb(c, rgb)) continue;
        rgbToOklab(rgb, lab);
        for (int i = 0; i < 3; ++i) sum[i] += lab[i];
        ++n;
    }
    if (!n) return QStringLiteral("#364152");
    // JS：labs.reduce((t, v) => t + v[i], 0) / labs.length —— 先按顺序加再除（和上面一样）
    const double L = sum[0] / n, a = sum[1] / n, b = sum[2] / n;
    const double l = std::pow(L + 0.3963377774 * a + 0.2158037573 * b, 3);
    const double m = std::pow(L - 0.1055613458 * a - 0.0638541728 * b, 3);
    const double s = std::pow(L - 0.0894841775 * a - 1.2914855480 * b, 3);
    const double lin[3] = { 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                           -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                           -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s };
    QString out = QStringLiteral("#");
    for (double c : lin) {
        const int v = jsRound(std::min(1.0, std::max(0.0, fromLin(c))) * 255);
        out += QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0'));
    }
    return out;
}

bool isLightColor(const QString &hex)
{
    double rgb[3];
    if (!hexToRgb(hex, rgb)) return false;
    return 0.2126 * toLin(rgb[0]) + 0.7152 * toLin(rgb[1]) + 0.0722 * toLin(rgb[2]) > 0.45;
}

// ---- 合并 ----
int clusterTier(double zoom)
{
    const int z = int(std::floor(zoom + 1e-6));
    return z <= 5 ? std::max(0, z) : -1;
}

double clusterKm(int tier)
{
    switch (tier) {
    case 2: return 700;
    case 3: return 350;
    case 4: return 180;
    case 5: return 90;
    default: return 1400;   // 0、1（以及 Electron 里找不到层时的 CLUSTER_LAYERS[0]）
    }
}

Groups cluster(const QVector<geo::LatLng> &pts, double km)
{
    Groups g;
    g.groupOf.resize(pts.size());
    QVector<geo::LatLng> seeds;
    for (int i = 0; i < pts.size(); ++i) {
        int best = -1;
        double bd = std::numeric_limits<double>::infinity();
        for (int k = 0; k < seeds.size(); ++k) {
            const double d = geo::distanceKm(seeds[k], pts[i]);
            if (d <= km && d < bd) { bd = d; best = k; }
        }
        if (best >= 0) { g.members[best].push_back(i); g.groupOf[i] = best; }
        else { seeds.push_back(pts[i]); g.members.push_back({ i }); g.groupOf[i] = int(seeds.size()) - 1; }
    }
    return g;
}

// ---- 错开 ----
SpreadResult spread(const QVector<SpreadItem> &items, const geo::Camera &cam)
{
    SpreadResult r;
    r.offset.fill(geo::Pt{}, items.size());
    if (cam.zoom < kSpreadMinZoom) return r;
    // 视野（maplibre getBounds：四个角，经度不归一）
    const double halfLng = cam.width / 2 / cam.worldSize() * 360.0;
    const double west = cam.lng - halfLng, east = cam.lng + halfLng;
    const double north = cam.coordinateFor(cam.width / 2, 0).lat, south = cam.coordinateFor(cam.width / 2, cam.height).lat;
    const double padLat = (north - south) * 0.5, padLng = (east - west) * 0.5;
    const double cx = (east + west) / 2;
    const auto inView = [&](const geo::LatLng &ll) {
        return ll.lat >= south - padLat && ll.lat <= north + padLat
            && std::abs(geo::unwrapTo(ll.lng, cx) - cx) <= (east - west) / 2 + padLng;
    };
    struct Group { geo::LatLng c; QVector<int> list; };
    QVector<Group> groups;
    for (int i = 0; i < items.size(); ++i) {
        const geo::LatLng &b = items[i].base;
        if (!inView(b)) continue;
        // Electron：groups.find —— 第一个在 20 m 内的组（不是最近的）
        auto it = std::find_if(groups.begin(), groups.end(), [&](const Group &g) { return geo::distanceKm(g.c, b) * 1000 <= kSpreadNearM; });
        if (it != groups.end()) it->list.push_back(i);
        else groups.push_back({ b, { i } });
    }
    for (Group &g : groups) {
        if (g.list.size() == 1) continue;
        // 固定顺序：先按计划，再按站点顺序（JS 字符串比较 = UTF-16 码元，和 QString::operator< 一样）
        std::sort(g.list.begin(), g.list.end(), [&](int a, int b) {
            const SpreadItem &A = items[a], &B = items[b];
            if (A.planId != B.planId) return A.planId < B.planId;
            return A.stopIdx < B.stopIdx;
        });
        const int n = g.list.size();
        const double rad = std::max(22.0, n * 30 / (2 * geo::kPi));   // 半径随个数增大，相邻标记中心间隔约 30 px
        const geo::Pt c = cam.pixelFor(g.c.lat, g.c.lng);
        for (int k = 0; k < n; ++k) {
            const double a = -geo::kPi / 2 + 2 * geo::kPi * k / n;   // 从正上方开始顺时针
            const geo::Pt base = cam.pixelFor(items[g.list[k]].base.lat, items[g.list[k]].base.lng);
            r.offset[g.list[k]] = { c.x + std::cos(a) * rad - base.x, c.y + std::sin(a) * rad - base.y };
        }
        r.dots.push_back(g.c);
    }
    return r;
}

} // namespace tp::map::markers
