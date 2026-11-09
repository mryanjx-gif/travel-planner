#include "rail.h"

#include "roads.h"

#include <QFile>
#include <QHash>
#include <QMutex>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <queue>

namespace tp::map::rail {

namespace {
using geo::LatLng;

struct City { QString id; double s, w, n, e; };
struct Seg { int a, b; };
struct Graph {
    QVector<LatLng> v;
    QVector<QVector<QPair<int, double>>> adj;   // 邻居、权重（米；换乘连接加罚）
    QVector<Seg> segs;                          // 只有轨道（换乘连接不参与吸附）
};

QString g_dir = QStringLiteral(":/rail");
QVector<City> g_cities;
bool g_citiesRead = false;
QHash<QString, std::shared_ptr<Graph>> g_graphs;   // ponytail: 读过的城市一直留在内存（东京约几 MB），城市多了再按最近使用丢
QHash<QString, Result> g_cache;
QMutex g_mutex;

const QVector<City> &cities()
{
    if (g_citiesRead) return g_cities;
    g_citiesRead = true;
    QFile f(g_dir + QStringLiteral("/cities.txt"));
    if (!f.open(QIODevice::ReadOnly)) return g_cities;
    for (const QByteArray &line : f.readAll().split('\n')) {
        const QList<QByteArray> t = line.simplified().split(' ');
        if (t.size() == 5) g_cities.push_back({ QString::fromLatin1(t[0]), t[1].toDouble(), t[2].toDouble(), t[3].toDouble(), t[4].toDouble() });
    }
    return g_cities;
}

const City *cityOf(double lat, double lng)
{
    for (const City &c : cities())
        if (lat >= c.s && lat <= c.n && lng >= c.w && lng <= c.e) return &c;
    return nullptr;
}

const City *cityOfBoth(double aLat, double aLng, double bLat, double bLng)
{
    for (const City &c : cities())
        if (aLat >= c.s && aLat <= c.n && aLng >= c.w && aLng <= c.e && bLat >= c.s && bLat <= c.n && bLng >= c.w && bLng <= c.e) return &c;
    return nullptr;
}

std::shared_ptr<Graph> graphOf(const QString &id)
{
    if (auto it = g_graphs.find(id); it != g_graphs.end()) return *it;
    auto g = std::make_shared<Graph>();
    QFile f(g_dir + u'/' + id + QStringLiteral(".txt"));
    if (f.open(QIODevice::ReadOnly)) {
        QHash<qint64, int> index;   // 1e-5 取整后的坐标 → 顶点（坐标相同的点就是相连的点）
        auto vid = [&](const LatLng &p) {
            const qint64 k = (qint64(std::llround(p.lat * 1e5)) << 32) ^ (qint64(std::llround(p.lng * 1e5)) & 0xffffffffLL);
            auto it = index.find(k);
            if (it != index.end()) return *it;
            const int i = int(g->v.size());
            g->v.push_back(p);
            g->adj.push_back({});
            index.insert(k, i);
            return i;
        };
        for (const QByteArray &raw : f.readAll().split('\n')) {
            if (raw.isEmpty()) continue;
            const bool link = raw.startsWith('#');
            const QVector<LatLng> pts = roads::decodePolyline(QString::fromLatin1(link ? raw.mid(1) : raw));
            for (int i = 1; i < pts.size(); ++i) {
                const int a = vid(pts[i - 1]), b = vid(pts[i]);
                if (a == b) continue;
                const double d = geo::distanceMeters(g->v[a], g->v[b]);
                const double w = link ? 2 * d + 100 : d;   // 换乘：走路比坐车慢，加罚，免得在两条线之间来回跳
                g->adj[a].push_back({ b, w });
                g->adj[b].push_back({ a, w });
                if (!link) g->segs.push_back({ a, b });
            }
        }
        qInfo("线路数据：读了 %s（%lld 个点、%lld 段）", qUtf8Printable(id), qlonglong(g->v.size()), qlonglong(g->segs.size()));
    }
    g_graphs.insert(id, g);
    return g;
}

struct Snap { int seg = -1; double t = 0, d = std::numeric_limits<double>::max(); LatLng p; };
// 离这个点最近的几段轨道（局部等距投影，城市范围内够准）：最近的那段再加 250 米以内的都算候选，
// 大车站有很多股道，最近的那股可能是尽头线，要从附近几股里挑最短的走法
QVector<Snap> snaps(const Graph &g, double lat, double lng)
{
    QVector<Snap> all;
    double best = std::numeric_limits<double>::max();
    const double kx = std::cos(lat * M_PI / 180) * 111320, ky = 111320;
    for (int i = 0; i < g.segs.size(); ++i) {
        const LatLng &A = g.v[g.segs[i].a], &B = g.v[g.segs[i].b];
        const double ax = (A.lng - lng) * kx, ay = (A.lat - lat) * ky, bx = (B.lng - lng) * kx, by = (B.lat - lat) * ky;
        const double dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
        const double t = L2 > 0 ? std::clamp(-(ax * dx + ay * dy) / L2, 0.0, 1.0) : 0.0;
        const double d = std::hypot(ax + t * dx, ay + t * dy);
        if (d > kSnapM || d > best + 250) continue;
        best = std::min(best, d);
        all.push_back({ i, t, d, { A.lat + t * (B.lat - A.lat), A.lng + t * (B.lng - A.lng) } });
    }
    all.erase(std::remove_if(all.begin(), all.end(), [best](const Snap &s) { return s.d > best + 250; }), all.end());
    return all;
}

Result compute(double aLat, double aLng, double bLat, double bLng)
{
    Result r;
    const City *c = cityOfBoth(aLat, aLng, bLat, bLng);
    if (!c) { r.reason = cityOf(aLat, aLng) || cityOf(bLat, bLng) ? QStringLiteral("out") : QStringLiteral("nodata"); return r; }
    const std::shared_ptr<Graph> gp = graphOf(c->id);
    const Graph &g = *gp;
    if (g.segs.isEmpty()) { r.reason = QStringLiteral("nodata"); return r; }
    const QVector<Snap> sa = snaps(g, aLat, aLng), sb = snaps(g, bLat, bLng);
    if (sa.isEmpty() || sb.isEmpty()) { r.reason = QStringLiteral("far"); return r; }
    auto len = [&g](const Snap &s) { const Seg &e = g.segs[s.seg]; return geo::distanceMeters(g.v[e.a], g.v[e.b]); };

    // Dijkstra（多起点、多终点）：每个起点候选先到所在段的两头（吸附距离也算进去，近的优先），终点候选从所在段的两头进
    constexpr int kNone = std::numeric_limits<int>::min();
    const int n = int(g.v.size());
    QVector<double> dist(n, std::numeric_limits<double>::infinity());
    QVector<int> prev(n, kNone);   // < 0：从第 -(prev+1) 个起点候选出发
    using QE = std::pair<double, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    auto push = [&](int v, double d, int from) { if (d < dist[v]) { dist[v] = d; prev[v] = from; pq.push({ d, v }); } };
    for (int i = 0; i < sa.size(); ++i) {
        const Seg &e = g.segs[sa[i].seg];
        const double L = len(sa[i]);
        push(e.a, sa[i].d + sa[i].t * L, -(i + 1));
        push(e.b, sa[i].d + (1 - sa[i].t) * L, -(i + 1));
    }
    QHash<int, QVector<QPair<int, double>>> tgt;   // 顶点 → (终点候选, 从这个顶点走到吸附点再到站的代价)
    for (int j = 0; j < sb.size(); ++j) {
        const Seg &e = g.segs[sb[j].seg];
        const double L = len(sb[j]);
        tgt[e.a].push_back({ j, sb[j].t * L + sb[j].d });
        tgt[e.b].push_back({ j, (1 - sb[j].t) * L + sb[j].d });
    }
    double best = std::numeric_limits<double>::infinity();
    int bestEnd = -2, bestSrc = -1, bestTgt = -1;   // bestEnd -1 = 起点终点在同一段上，直接走
    for (int i = 0; i < sa.size(); ++i)
        for (int j = 0; j < sb.size(); ++j)
            if (sa[i].seg == sb[j].seg) {
                const double d = sa[i].d + std::abs(sa[i].t - sb[j].t) * len(sa[i]) + sb[j].d;
                if (d < best) { best = d; bestEnd = -1; bestSrc = i; bestTgt = j; }
            }
    while (!pq.empty()) {
        const auto [d, v] = pq.top();
        pq.pop();
        if (d >= best) break;
        if (d > dist[v]) continue;
        if (auto it = tgt.constFind(v); it != tgt.constEnd())
            for (const auto &[j, extra] : *it)
                if (d + extra < best) { best = d + extra; bestEnd = v; bestTgt = j; }
        for (const auto &[u, w] : g.adj[v]) push(u, d + w, v);
    }
    if (bestEnd == -2) { r.reason = QStringLiteral("nopath"); return r; }

    QVector<LatLng> pts{ sb[bestTgt].p };
    for (int v = bestEnd; v >= 0;) {
        pts.push_back(g.v[v]);
        if (prev[v] < 0) { bestSrc = -(prev[v] + 1); break; }
        v = prev[v];
    }
    pts.push_back(sa[bestSrc].p);
    std::reverse(pts.begin(), pts.end());
    // 长度按画出来的整条线算：站 → 吸附点 → 沿轨道 → 吸附点 → 站
    r.distM = sa[bestSrc].d + sb[bestTgt].d;
    for (int i = 1; i < pts.size(); ++i) r.distM += geo::distanceMeters(pts[i - 1], pts[i]);
    // ponytail: 绕得太远（比直线长 3 倍多）多半是数据里线路断了、靠换乘连接硬凑出来的，宁可画直线
    const double straight = geo::distanceMeters({ aLat, aLng }, { bLat, bLng });
    if (r.distM > 3 * straight + 3000) { r.distM = 0; r.reason = QStringLiteral("nopath"); return r; }
    r.pts = pts;
    return r;
}
} // namespace

bool isRailMode(const QString &mode) { return mode == QLatin1String("train"); }

Result route(double aLat, double aLng, double bLat, double bLng)
{
    const QString key = roads::legKeyOf(QStringLiteral("train"), aLat, aLng, bLat, bLng);
    QMutexLocker lock(&g_mutex);
    if (auto it = g_cache.constFind(key); it != g_cache.constEnd()) return *it;
    Result r = compute(aLat, aLng, bLat, bLng);
    g_cache.insert(key, r);
    return r;
}

void setDataDir(const QString &dir)
{
    QMutexLocker lock(&g_mutex);
    g_dir = dir;
    g_cities.clear();
    g_citiesRead = false;
    g_graphs.clear();
    g_cache.clear();
}

} // namespace tp::map::rail
