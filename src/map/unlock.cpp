#include "unlock.h"

#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <algorithm>

namespace tp::map {

namespace {
const int kHues[] = { 8, 205, 145, 32, 275, 175, 340, 95, 230, 55, 300, 120 };   // UNLOCK_HUES
// 港澳台归到中国，各自整块算一个省级单位（Natural Earth 里它们是单独的 admin-0）
QString cnPart(const QString &adm0)
{
    if (adm0 == QLatin1String("HKG")) return QStringLiteral("香港");
    if (adm0 == QLatin1String("MAC")) return QStringLiteral("澳门");
    if (adm0 == QLatin1String("TWN")) return QStringLiteral("台湾");
    return {};
}
} // namespace

bool UnlockModel::inRing(double x, double y, const Ring &r)
{
    bool inside = false;
    const auto &p = r.pts;
    for (int i = 0, j = int(p.size()) - 1; i < p.size(); j = i++) {
        const double xi = p[i].x, yi = p[i].y, xj = p[j].x, yj = p[j].y;
        if (((yi > y) != (yj > y)) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

bool UnlockModel::inPoly(double x, double y, const QVector<Ring> &poly)
{
    if (poly.isEmpty() || !inRing(x, y, poly[0])) return false;
    for (int k = 1; k < poly.size(); ++k)
        if (inRing(x, y, poly[k])) return false;
    return true;
}

bool UnlockModel::load(const QByteArray &adm1Json, QString *error)
{
    m_units.clear();
    m_byId.clear();
    m_byGroup.clear();
    m_groupBox.clear();
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(adm1Json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = pe.errorString();
        return false;
    }
    const QJsonArray feats = doc.object().value(QLatin1String("features")).toArray();
    m_units.reserve(feats.size());
    // 先按国家（adm0）算外框，判断"很小的国家"
    QHash<QString, QVector<double>> countryBox;
    const auto grow = [](QHash<QString, QVector<double>> &m, const QString &k, const double *b) {
        auto it = m.find(k);
        if (it == m.end()) m.insert(k, { b[0], b[1], b[2], b[3] });
        else {
            (*it)[0] = std::min((*it)[0], b[0]); (*it)[1] = std::min((*it)[1], b[1]);
            (*it)[2] = std::max((*it)[2], b[2]); (*it)[3] = std::max((*it)[3], b[3]);
        }
    };
    for (const QJsonValue &fv : feats) {
        const QJsonObject f = fv.toObject();
        const QJsonObject p = f.value(QLatin1String("properties")).toObject();
        Unit u;
        u.id = p.value(QLatin1String("id")).toString();
        u.name = p.value(QLatin1String("name")).toString();
        u.adm0 = p.value(QLatin1String("adm0")).toString();
        u.country = p.value(QLatin1String("country")).toString();
        const QJsonArray bb = f.value(QLatin1String("bbox")).toArray();
        if (bb.size() == 4) {
            for (int i = 0; i < 4; ++i) u.bbox[i] = bb[i].toDouble();
            u.hasBbox = true;
            grow(countryBox, u.adm0, u.bbox);
        }
        const QJsonObject g = f.value(QLatin1String("geometry")).toObject();
        const QString type = g.value(QLatin1String("type")).toString();
        const QJsonArray coords = g.value(QLatin1String("coordinates")).toArray();
        const auto readPoly = [](const QJsonArray &rings) {
            QVector<Ring> poly;
            for (const QJsonValue &rv : rings) {
                Ring r;
                const QJsonArray ra = rv.toArray();
                r.pts.reserve(ra.size());
                for (const QJsonValue &pv : ra) {
                    const QJsonArray pa = pv.toArray();
                    r.pts.push_back({ pa.at(0).toDouble(), pa.at(1).toDouble() });
                }
                poly.push_back(r);
            }
            return poly;
        };
        if (type == QLatin1String("Polygon")) u.polys.push_back(readPoly(coords));
        else if (type == QLatin1String("MultiPolygon")) {
            u.multi = true;
            for (const QJsonValue &pv : coords) u.polys.push_back(readPoly(pv.toArray()));
        }
        m_units.push_back(std::move(u));
    }
    for (int i = 0; i < m_units.size(); ++i) {
        Unit &u = m_units[i];
        const QString c = u.adm0;
        const QString part = cnPart(c);
        if (!part.isEmpty()) {
            u.grp = c; u.gname = part; u.adm0 = QStringLiteral("CHN"); u.country = QStringLiteral("中国");
        } else {
            if (c == QLatin1String("CHN")) u.country = QStringLiteral("中国");
            const auto b = countryBox.constFind(c);
            if (b != countryBox.constEnd() && ((*b)[2] - (*b)[0]) * ((*b)[3] - (*b)[1]) < kSmallCountryDeg2) {
                u.grp = c; u.gname = u.country;
            }
        }
        m_byId.insert(u.id, i);
        if (!u.grp.isEmpty()) {
            m_byGroup[u.grp].push_back(i);
            if (u.hasBbox) grow(m_groupBox, u.grp, u.bbox);
        }
    }
    return !m_units.isEmpty();
}

int UnlockModel::unitAt(double lng, double lat) const
{
    for (int i = 0; i < m_units.size(); ++i) {
        const Unit &u = m_units[i];
        if (u.hasBbox && (lng < u.bbox[0] || lng > u.bbox[2] || lat < u.bbox[1] || lat > u.bbox[3])) continue;
        for (const auto &poly : u.polys)
            if (inPoly(lng, lat, poly)) return i;
    }
    return -1;
}

int UnlockModel::findAdmin(double lng, double lat) const
{
    if (const int i = unitAt(lng, lat); i >= 0) return i;
    // 没落在任何省里（海边、小岛：边界简化过，可能差一点）：找 0.3° 以内最近的边界点所在的省
    int best = -1;
    double bd = 0.09;
    for (int i = 0; i < m_units.size(); ++i) {
        const Unit &u = m_units[i];
        if (u.hasBbox && (lng < u.bbox[0] - .3 || lng > u.bbox[2] + .3 || lat < u.bbox[1] - .3 || lat > u.bbox[3] + .3)) continue;
        for (const auto &poly : u.polys) {
            if (poly.isEmpty()) continue;
            for (const geo::Pt &p : poly[0].pts) {
                const double d = (p.x - lng) * (p.x - lng) + (p.y - lat) * (p.y - lat);
                if (d < bd) { bd = d; best = i; }
            }
        }
    }
    return best;
}

UnlockModel::Result UnlockModel::compute(const QVector<PlanStops> &completed, const QVector<Manual> &manual) const
{
    struct Visit {
        int idx;
        QStringList plans;   // 计划名（去重，按加入顺序）
        bool manual = false;
        int manualYear = 0;
        QString first = QStringLiteral("9999");
    };
    QVector<Visit> visited;          // 按加入顺序（= JS Map 的遍历顺序）
    QHash<int, int> pos;             // 单位下标 → visited 里的位置
    const auto add = [&](int idx, const PlanStops *plan, const Manual *m) {
        const Unit &u0 = m_units[idx];
        const QVector<int> members = u0.grp.isEmpty() ? QVector<int>{ idx } : m_byGroup.value(u0.grp);
        for (int t : members) {
            auto it = pos.find(t);
            if (it == pos.end()) { it = pos.insert(t, int(visited.size())); visited.push_back({ t, {} }); }
            Visit &v = visited[*it];
            const QString d = plan ? (plan->startDate.isEmpty() ? QStringLiteral("9999") : plan->startDate)
                                   : (m->year ? QString::number(m->year) : QStringLiteral("9999"));
            if (plan) { if (!v.plans.contains(plan->name)) v.plans << plan->name; }
            else { v.manual = true; v.manualYear = m->year; }
            if (d < v.first) v.first = d;
        }
    };
    for (const PlanStops &p : completed)
        for (const geo::LatLng &s : p.stops) {
            const int i = findAdmin(s.lng, s.lat);
            if (i >= 0) add(i, &p, nullptr);
        }
    for (const Manual &m : manual) {
        const auto it = m_byId.constFind(m.id);
        if (it != m_byId.constEnd()) add(*it, nullptr, &m);
    }

    // 国家按第一次去的先后分配色相（稳定排序；同一天的按加入顺序）
    struct Country { int hue; QString name; QHash<QString, int> unitLight; int units = 0; };
    QHash<QString, int> countryIdx;
    QVector<Country> countries;
    QVector<int> order(visited.size());
    for (int i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&visited](int a, int b) { return visited[a].first < visited[b].first; });
    for (int i : order) {
        const Unit &u = m_units[visited[i].idx];
        if (!countryIdx.contains(u.adm0)) {
            countryIdx.insert(u.adm0, int(countries.size()));
            countries.push_back({ kHues[countries.size() % 12], u.country, {}, 0 });
        }
    }

    Result r;
    QJsonArray feats, dots;
    QStringList unitOrder;
    QHash<QString, QJsonObject> unitProps;
    QHash<QString, QVector<double>> unitBox;
    for (const Visit &v : visited) {   // 深浅按加入顺序（和 Electron 一样遍历 visited）
        const Unit &u = m_units[v.idx];
        Country &c = countries[countryIdx.value(u.adm0)];
        const QString key = unitKey(u);
        if (!c.unitLight.contains(key)) c.unitLight.insert(key, 62 - (c.units++ % 4) * 7);
        const QString how = !v.plans.isEmpty() ? v.plans.join(QStringLiteral("、"))
                                               : QStringLiteral("手动标记") + (v.manual && v.manualYear ? QStringLiteral(" · %1").arg(v.manualYear) : QString());
        const QString color = QStringLiteral("hsl(%1, 58%, %2%)").arg(c.hue).arg(c.unitLight.value(key));
        const QJsonObject props{ { "id", u.id }, { "name", u.gname.isEmpty() ? u.name : u.gname }, { "country", u.country },
                                 { "plans", how }, { "c", color } };
        feats.append(QJsonObject{ { "type", "Feature" }, { "properties", props }, { "geometry", geometryOf(u) } });
        r.colorById.insert(u.id, color);
        r.propsById.insert(u.id, props);
        r.plansById.insert(u.id, v.plans);
        if (!unitProps.contains(key)) {
            unitOrder << key;
            unitProps.insert(key, props);
            if (!u.grp.isEmpty()) unitBox.insert(key, m_groupBox.value(u.grp));
            else if (u.hasBbox) unitBox.insert(key, { u.bbox[0], u.bbox[1], u.bbox[2], u.bbox[3] });
        }
    }
    // 面积很小的单位（胡志明市、吉隆坡、新加坡、香港……）缩小时几乎看不见：在中心另画一个圆点，放大后淡出
    for (const QString &key : unitOrder) {
        const QVector<double> b = unitBox.value(key);
        if (b.size() != 4 || (b[2] - b[0]) * (b[3] - b[1]) >= kDotDeg2) continue;
        dots.append(QJsonObject{ { "type", "Feature" }, { "properties", unitProps.value(key) },
                                 { "geometry", QJsonObject{ { "type", "Point" }, { "coordinates", QJsonArray{ (b[0] + b[2]) / 2, (b[1] + b[3]) / 2 } } } } });
    }
    r.visit = QJsonObject{ { "type", "FeatureCollection" }, { "features", feats } };
    r.dots = QJsonObject{ { "type", "FeatureCollection" }, { "features", dots } };
    r.provinces = int(unitOrder.size());
    r.countries = int(countries.size());
    for (const Country &c : countries) r.countryNames << c.name;
    return r;
}

QString UnlockModel::unitName(int i) const
{
    if (i < 0 || i >= m_units.size()) return {};
    const Unit &u = m_units[i];
    return u.gname.isEmpty() ? u.name : u.gname;
}

UnlockModel::Sub UnlockModel::unitSub(int i) const
{
    // const unitSub = f => !f.properties.grp ? f.properties.country : (CN_PARTS[f.properties.grp] ? '中国' : '整个国家 / 地区');
    if (i < 0 || i >= m_units.size() || m_units[i].grp.isEmpty()) return Sub::Country;
    return cnPart(m_units[i].grp).isEmpty() ? Sub::WholeCountry : Sub::China;
}

bool UnlockModel::sameUnit(int a, int b) const
{
    if (a < 0 || b < 0 || a >= m_units.size() || b >= m_units.size()) return false;
    return a == b || (!m_units[a].grp.isEmpty() && m_units[a].grp == m_units[b].grp);
}

QVector<int> UnlockModel::unitMembers(int i) const
{
    if (i < 0 || i >= m_units.size()) return {};
    return m_units[i].grp.isEmpty() ? QVector<int>{ i } : m_byGroup.value(m_units[i].grp);
}

QJsonObject UnlockModel::geometryOf(const Unit &u)
{
    const auto polyJson = [](const QVector<Ring> &poly) {
        QJsonArray rings;
        for (const Ring &r : poly) {
            QJsonArray pts;
            for (const geo::Pt &p : r.pts) {
                QJsonArray xy;   // 不写成 QJsonArray{ … } 套一层的形式（Windows差异清单 #15）
                xy.append(p.x);
                xy.append(p.y);
                pts.append(xy);
            }
            rings.append(pts);
        }
        return rings;
    };
    if (!u.multi) return QJsonObject{ { "type", "Polygon" }, { "coordinates", u.polys.isEmpty() ? QJsonArray() : polyJson(u.polys.first()) } };
    QJsonArray polys;
    for (const QVector<Ring> &poly : u.polys) polys.append(polyJson(poly));
    return QJsonObject{ { "type", "MultiPolygon" }, { "coordinates", polys } };
}

QJsonObject UnlockModel::unitFeatures(int i) const
{
    QJsonArray feats;
    for (int k : unitMembers(i))
        feats.append(QJsonObject{ { "type", "Feature" }, { "properties", QJsonObject{ { "id", m_units[k].id } } }, { "geometry", geometryOf(m_units[k]) } });
    return QJsonObject{ { "type", "FeatureCollection" }, { "features", feats } };
}

geo::LatLng UnlockModel::unitCenter(int i) const
{
    if (i < 0 || i >= m_units.size()) return {};
    const Unit &u = m_units[i];
    if (!u.grp.isEmpty()) {
        const QVector<double> b = m_groupBox.value(u.grp);
        if (b.size() == 4) return { (b[1] + b[3]) / 2, (b[0] + b[2]) / 2 };
    }
    return { (u.bbox[1] + u.bbox[3]) / 2, (u.bbox[0] + u.bbox[2]) / 2 };
}

QVector<int> UnlockModel::search(const QString &q0, int max) const
{
    // [name, gname || '', country, adm0].join(' ').toLowerCase().includes(q)；整块点亮的组只列一行；.slice(0, 120)
    const QString q = q0.trimmed().toLower();
    QVector<int> out;
    if (q.isEmpty()) return out;
    QSet<QString> seen;
    for (int i = 0; i < m_units.size() && out.size() < max; ++i) {
        const Unit &u = m_units[i];
        // 注意：Electron 搜的 adm0 是整理以后的（港澳台已改成 CHN），和这里一样
        const QString hay = (u.name + u' ' + u.gname + u' ' + u.country + u' ' + u.adm0).toLower();
        if (!hay.contains(q)) continue;
        const QString key = unitKey(u);
        if (seen.contains(key)) continue;
        seen.insert(key);
        out << i;
    }
    return out;
}

bool UnlockModel::countryBox(const QString &code, double *w, double *s, double *e, double *n) const
{
    double x0 = 180, y0 = 90, x1 = -180, y1 = -90;
    bool any = false;
    for (const Unit &u : m_units) {
        if (!u.hasBbox || (u.adm0 != code && u.grp != code)) continue;
        x0 = std::min(x0, u.bbox[0]); y0 = std::min(y0, u.bbox[1]); x1 = std::max(x1, u.bbox[2]); y1 = std::max(y1, u.bbox[3]);
        any = true;
    }
    if (!any || !(x1 > x0)) return false;
    *w = x0; *s = y0; *e = x1; *n = y1;
    return true;
}

} // namespace tp::map
