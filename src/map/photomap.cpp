#include "photomap.h"

#include "geo.h"
#include "plans.h"
#include "trdata.h"

#include <algorithm>
#include <cmath>

namespace tp::map::photomap {

using json::Value;
using data::trd;

namespace {
QString str(const Value &o, const char *key) { return o[QString::fromLatin1(key)].toString(); }
bool validPos(double lat, double lng) { return std::abs(lat) <= 90 && std::abs(lng) <= 180; }
struct StopInfo { QString name, arrive; int no = 0; double lat = 0, lng = 0; bool pos = false; };
struct PlanInfo { QString name, start, end; bool route = false, completed = false; int order = 0; QHash<QString, StopInfo> stops; };
}

bool isHeic(const QString &file)
{
    return file.endsWith(QLatin1String(".heic"), Qt::CaseInsensitive) || file.endsWith(QLatin1String(".heif"), Qt::CaseInsensitive)
        || file.endsWith(QLatin1String(".avif"), Qt::CaseInsensitive);
}

Library read(const Value &root)
{
    QHash<QString, PlanInfo> plansById;
    for (const Value &p : root[QStringLiteral("plans")].asArray()) {
        PlanInfo pi;
        const QString n = str(p, "name");
        pi.name = n.isEmpty() ? trd("未命名计划") : n;
        pi.start = str(p, "startDate");
        pi.end = str(p, "endDate");
        pi.route = plans::typeOf(p) == QLatin1String("route");
        pi.completed = plans::statusOf(p) == QLatin1String("completed");
        const json::Array &stops = p[QStringLiteral("stops")].asArray();
        int no = 0;
        for (int i : plans::validStops(p)) {
            const Value &s = stops[size_t(i)];
            StopInfo si;
            si.name = str(s, "name");
            si.arrive = str(s, "arriveDate");
            si.no = ++no;
            si.lat = s[QStringLiteral("lat")].toNumber();
            si.lng = s[QStringLiteral("lng")].toNumber();
            si.pos = validPos(si.lat, si.lng);
            pi.stops.insert(str(s, "id"), si);
        }
        plansById.insert(str(p, "id"), pi);
    }
    Library lib;
    const json::Array &photos = root[QStringLiteral("photos")].asArray();
    lib.recs.reserve(int(photos.size()));
    QHash<QString, int> tripOf;
    for (const Value &ph : photos) {
        Rec r;
        r.id = str(ph, "id");
        r.file = str(ph, "file");
        r.caption = str(ph, "caption");
        r.takenAt = str(ph, "takenAt");
        r.fav = ph[QStringLiteral("fav")].toBool();
        const double dataW = ph[QStringLiteral("width")].toNumber(), dataH = ph[QStringLiteral("height")].toNumber();
        if (dataW > 0 && dataH > 0) r.ratio = dataW / dataH;
        const auto pit = plansById.constFind(str(ph, "plan"));
        const StopInfo *stop = nullptr;
        if (pit != plansById.constEnd()) {
            r.plan = pit.key();
            r.planName = pit->name;
            r.route = pit->route;
            const auto sit = pit->stops.constFind(str(ph, "stop"));
            if (sit != pit->stops.constEnd()) { stop = &*sit; r.stop = sit.key(); r.stopName = stop->name; r.stopNo = stop->no; }
        }
        const Value &la = ph[QStringLiteral("lat")], &ln = ph[QStringLiteral("lng")];
        if (la.isNumber() && ln.isNumber() && validPos(la.toNumber(), ln.toNumber())) {
            r.hasPos = true; r.lat = la.toNumber(); r.lng = ln.toNumber();
        } else if (stop && stop->pos) {
            r.hasPos = true; r.fromStop = true; r.lat = stop->lat; r.lng = stop->lng;
        }
        r.day = (!r.takenAt.isEmpty() ? r.takenAt : stop && !stop->arrive.isEmpty() ? stop->arrive : pit != plansById.constEnd() ? pit->start : QString()).left(10);
        if (r.hasPos) ++lib.pinned; else ++lib.noPos;
        if (r.fav) ++lib.favs;
        if (r.plan.isEmpty()) ++lib.inbox;
        if (isHeic(r.file)) ++lib.heic;
        const int idx = int(lib.recs.size());
        if (!r.plan.isEmpty()) {
            auto t = tripOf.find(r.plan);
            if (t == tripOf.end()) {
                t = tripOf.insert(r.plan, int(lib.trips.size()));
                lib.trips.push_back({ r.plan, r.planName, pit->start, pit->end, pit->completed, 0, -1 });
            }
            Trip &trip = lib.trips[*t];
            ++trip.count;
            if (trip.rep < 0 || (r.fav && !lib.recs[trip.rep].fav)) trip.rep = idx;
        }
        lib.recs.push_back(std::move(r));
    }
    std::stable_sort(lib.trips.begin(), lib.trips.end(), [](const Trip &a, const Trip &b) {
        if (a.completed != b.completed) return a.completed;
        return a.start > b.start;
    });
    return lib;
}

QVector<Group> cluster(const QVector<Rec> &recs, int zoom, double radiusPx)
{
    struct P { double x, y; int rec; };
    const double world = geo::kTile * std::exp2(double(zoom));
    QVector<P> pts;
    pts.reserve(recs.size());
    for (int i = 0; i < recs.size(); ++i) {
        const Rec &r = recs[i];
        if (!r.hasPos) continue;
        const double s = std::clamp(std::sin(r.lat * M_PI / 180), -0.9999, 0.9999);
        pts.push_back({ (r.lng + 180) / 360 * world, (0.5 - std::log((1 + s) / (1 - s)) / (4 * M_PI)) * world, i });
    }
    std::stable_sort(pts.begin(), pts.end(), [&recs](const P &a, const P &b) { return recs[a.rec].fav > recs[b.rec].fav; });
    const auto cell = [radiusPx](double x, double y) { return (qint64(std::floor(x / radiusPx)) << 32) ^ qint64(quint32(std::floor(y / radiusPx))); };
    QHash<qint64, QVector<int>> grid;
    grid.reserve(pts.size());
    for (int i = 0; i < pts.size(); ++i) grid[cell(pts[i].x, pts[i].y)].push_back(i);
    QVector<bool> used(pts.size(), false);
    QVector<Group> out;
    const double r2 = radiusPx * radiusPx;
    for (int i = 0; i < pts.size(); ++i) {
        if (used[i]) continue;
        const P &c = pts[i];
        Group g;
        g.rep = c.rec;
        double sumLat = 0, sumLng = 0;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy) {
                const auto it = grid.constFind(cell(c.x + dx * radiusPx, c.y + dy * radiusPx));
                if (it == grid.constEnd()) continue;
                for (int j : *it) {
                    if (used[j]) continue;
                    const double ex = pts[j].x - c.x, ey = pts[j].y - c.y;
                    if (ex * ex + ey * ey > r2) continue;
                    used[j] = true;
                    const Rec &m = recs[pts[j].rec];
                    g.members.push_back(pts[j].rec);
                    sumLat += m.lat;
                    sumLng += m.lng;
                    if (m.lat != recs[c.rec].lat || m.lng != recs[c.rec].lng) g.samePlace = false;
                }
            }
        g.lat = sumLat / g.members.size();
        g.lng = sumLng / g.members.size();
        out.push_back(std::move(g));
    }
    return out;
}

QVector<Row> rows(const Library &lib, const QString &view, const QString &plan, bool compact, const RowLayout &layout)
{
    QVector<Row> out;
    int group = 0;
    // 不知道比例的先按 4:3；太极端的按边界算（格子里完整显示，留边），不然一行只剩一条缝
    const auto ratioOf = [&lib](int i) { const double a = lib.recs[i].ratio; return a > 0 ? std::clamp(a, 0.2, 6.0) : 4.0 / 3.0; };
    const double W = std::max(1.0, layout.width), H = std::max(1.0, layout.targetHeight), gap = layout.gap;
    const auto grid = [&](const QVector<int> &ids) {
        for (int i = 0; i < ids.size();) {
            // 往这一行放照片，直到再放一张反而离目标高度更远
            double sum = 0, h = 0;
            int j = i;
            for (; j < ids.size(); ++j) {
                const double s = sum + ratioOf(ids[j]);
                const double next = std::max(1.0, W - gap * (j - i)) / s;
                if (j > i && std::abs(next - H) > std::abs(h - H)) break;
                sum = s;
                h = next;
            }
            const bool stretch = j < ids.size() || h <= H;   // 一组的最后一行放不满就保持目标高度
            Row r;
            r.kind = QStringLiteral("photos");
            r.group = group;
            r.recs = ids.mid(i, j - i);
            r.height = std::round(stretch ? h : H);
            // 宽度取整，整行铺满时最后一张补齐（差不到 1 像素，格子里 PreserveAspectFit 看不出）
            double used = 0;
            for (int k = i; k < j; ++k) {
                const double w = std::max(1.0, std::round(ratioOf(ids[k]) * r.height));
                r.widths.push_back(w);
                used += w;
            }
            if (stretch) r.widths.last() = std::max(1.0, r.widths.last() + (W - gap * (j - i - 1) - used));
            out.push_back(std::move(r));
            i = j;
        }
        ++group;
    };
    const auto byDayDesc = [&lib](QVector<int> &ids) { std::stable_sort(ids.begin(), ids.end(), [&lib](int a, int b) { return lib.recs[a].day > lib.recs[b].day; }); };

    if (view == QLatin1String("year")) {
        QHash<QString, QVector<int>> months;
        for (int i = 0; i < lib.recs.size(); ++i) months[lib.recs[i].day.left(7)].push_back(i);
        QStringList keys = months.keys();
        std::sort(keys.begin(), keys.end(), std::greater<QString>());
        QString lastYear = QStringLiteral("?");
        for (const QString &k : std::as_const(keys)) {
            const QString y = k.left(4);
            if (y != lastYear) {
                Row r; r.kind = QStringLiteral("year"); r.title = y;
                out.push_back(r);
                lastYear = y;
            }
            QVector<int> ids = months.value(k);
            std::stable_sort(ids.begin(), ids.end(), [&lib](int a, int b) { return lib.recs[a].day < lib.recs[b].day; });
            if (!k.isEmpty()) {
                Row r; r.kind = QStringLiteral("month"); r.sub = k.mid(5); r.count = int(ids.size());
                out.push_back(r);
            }
            grid(ids);
        }
        return out;
    }
    if (view == QLatin1String("fav") || view == QLatin1String("inbox")) {
        const bool fav = view == QLatin1String("fav");
        QVector<int> ids;
        for (int i = 0; i < lib.recs.size(); ++i)
            if (fav ? lib.recs[i].fav : lib.recs[i].plan.isEmpty()) ids.push_back(i);
        if (fav) byDayDesc(ids);
        if (!ids.isEmpty()) grid(ids);
        return out;
    }

    // 按旅程
    const auto planPhotos = [&](const Trip &t) {
        if (!compact) {
            Row h; h.kind = QStringLiteral("plan"); h.title = t.name; h.planId = t.id; h.count = t.count;
            h.sub = t.start.isEmpty() ? QString() : t.end.isEmpty() ? t.start : t.start + QStringLiteral(" → ") + t.end;
            out.push_back(h);
        }
        QVector<QString> order;   // 站点按第一次出现；没归到站点的放最后
        QHash<QString, QVector<int>> byStop;
        for (int i = 0; i < lib.recs.size(); ++i) {
            if (lib.recs[i].plan != t.id) continue;
            if (!byStop.contains(lib.recs[i].stop)) order.push_back(lib.recs[i].stop);
            byStop[lib.recs[i].stop].push_back(i);
        }
        std::stable_sort(order.begin(), order.end(), [&](const QString &a, const QString &b) {
            const int na = lib.recs[byStop[a].first()].stopNo, nb = lib.recs[byStop[b].first()].stopNo;
            return (na ? na : 1 << 30) < (nb ? nb : 1 << 30);
        });
        for (const QString &sid : std::as_const(order)) {
            const QVector<int> &ids = byStop[sid];
            Row s; s.kind = QStringLiteral("stop"); s.title = lib.recs[ids.first()].stopName; s.stopNo = lib.recs[ids.first()].stopNo;
            s.count = int(ids.size());
            s.sub = lib.recs[ids.first()].route ? QStringLiteral("route") : QString();
            out.push_back(s);
            grid(ids);
        }
    };
    const auto noPosPhotos = [&] {
        QVector<int> ids;
        for (int i = 0; i < lib.recs.size(); ++i) if (!lib.recs[i].hasPos) ids.push_back(i);
        grid(ids);
    };
    if (compact) {
        for (const Trip &t : lib.trips) {
            Row r; r.kind = QStringLiteral("trip"); r.title = t.name; r.planId = t.id; r.count = t.count; r.rep = t.rep; r.open = plan == t.id;
            out.push_back(r);
            if (r.open) planPhotos(t);
        }
        if (lib.noPos > 0) {
            Row r; r.kind = QStringLiteral("trip"); r.planId = QString::fromLatin1(kNoPos); r.count = lib.noPos; r.open = plan == r.planId;
            out.push_back(r);
            if (r.open) noPosPhotos();
        }
        return out;
    }
    if (plan == QLatin1String(kNoPos)) { noPosPhotos(); return out; }
    for (const Trip &t : lib.trips)
        if (plan.isEmpty() || plan == t.id) planPhotos(t);
    return out;
}

} // namespace tp::map::photomap
