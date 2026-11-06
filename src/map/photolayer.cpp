#include "photolayer.h"

#include "perf.h"
#include "trdata.h"

#include <algorithm>
#include <cmath>

namespace tp::map {

using data::trd;

namespace {
constexpr double kMargin = 44;      // 标记一半大小 + 一点余量：一半进了视野就画
constexpr double kMaxExpand = 16;   // 这一级以上点合并的标记不再放大，直接看大图（Electron clusterMaxZoom 16）
}

PhotoLayer::PhotoLayer(QObject *parent) : QAbstractListModel(parent) {}

void PhotoLayer::setActive(bool on)
{
    if (on == m_active) return;
    m_active = on;
    emit activeChanged();
    if (on) emit rebuildDataWanted();
}

int PhotoLayer::tier() const
{
    const double z = m_globe ? m_gzoom : (m_cam ? m_cam->zoom : 0.0);
    return std::clamp(int(std::floor(z)), 0, 20);
}

const QVector<photomap::Group> &PhotoLayer::groups()
{
    const int t = tier();
    auto it = m_tiers.find(t);
    if (it == m_tiers.end()) {
        tp::app::perf::Scope timing("照片合并", 30);
        it = m_tiers.insert(t, photomap::cluster(m_lib.recs, t));
    }
    return *it;
}

void PhotoLayer::rebuild(const json::Value &root)
{
    if (!m_active) return;
    tp::app::perf::Scope timing("照片标记重建", 30);
    m_lib = photomap::read(root);
    m_tiers.clear();
    m_tier = -1;
    layout(true);
    emit infoChanged();
}

void PhotoLayer::setGlobeView(bool on, const globe::View &v, double zoomEq)
{
    m_globe = on;
    if (on) { m_gv = v; m_gzoom = zoomEq; }
    cameraChanged();
}

void PhotoLayer::cameraChanged()
{
    if (!m_active || (!m_cam && !m_globe)) return;
    tp::app::perf::Scope timing("照片标记摆位", 30);
    layout(tier() != m_tier);
}

void PhotoLayer::layout(bool tierChanged)
{
    const QVector<photomap::Group> &gs = groups();
    if (tierChanged) {
        m_tier = tier();
        m_groupSlot.fill(-1, gs.size());
        m_free.clear();
        for (int i = int(m_slots.size()) - 1; i >= 0; --i) { m_slots[i].group = -1; m_free.push_back(i); }
    }
    const double w = m_globe ? m_gv.width : (m_cam ? m_cam->width : 0), h = m_globe ? m_gv.height : (m_cam ? m_cam->height : 0);
    const globe::Camera cam(m_gv);
    QVector<int> added;
    QVector<int> freed;
    int shown = 0;
    for (int i = 0; i < gs.size(); ++i) {
        double x = -10000, y = -10000, limb = 1, sc = 1;
        if (m_globe) {
            const globe::Limb l = globe::limbOf(m_gv, gs[i].lat, gs[i].lng);
            const double t = std::clamp(m_gv.morph, 0.0, 1.0);
            limb = l.fade + (1 - l.fade) * t;
            sc = l.scale + (1 - l.scale) * t;
            if (!cam.project(globe::toVec(gs[i].lat, gs[i].lng), &x, &y)) limb = 0;
        } else if (m_cam) {
            const geo::Pt p = m_cam->pixelFor(gs[i].lat, gs[i].lng);
            x = p.x; y = p.y;
        }
        const bool vis = limb > 0.01 && x > -kMargin && y > -kMargin && x < w + kMargin && y < h + kMargin;
        int s = m_groupSlot[i];
        if (!vis) {
            if (s >= 0) { m_slots[s].group = -1; m_free.push_back(s); m_groupSlot[i] = -1; freed.push_back(s); }
            continue;
        }
        ++shown;
        if (s < 0) {
            if (m_free.isEmpty()) {
                s = int(m_slots.size());
                beginInsertRows({}, s, s);
                m_slots.push_back({});
                endInsertRows();
            } else {
                s = m_free.takeLast();
            }
            m_slots[s].group = i;
            m_groupSlot[i] = s;
            added.push_back(s);
        }
        Slot &sl = m_slots[s];
        sl.x = x; sl.y = y; sl.limb = limb; sl.sc = sc;
    }
    if (!m_slots.isEmpty()) {
        const QModelIndex a = index(0), b = index(int(m_slots.size()) - 1);
        if (tierChanged) emit dataChanged(a, b);   // 换了一层：全部重新读
        else {
            emit dataChanged(a, b, { X, Y, Sc, Op, Hit });
            for (int s : std::as_const(freed)) emit dataChanged(index(s), index(s), { Shown, File, Count, Fav, Same });
            for (int s : std::as_const(added)) emit dataChanged(index(s), index(s), { Shown, File, Count, Fav, Same });
        }
    }
    if (shown != m_shown || !added.isEmpty()) { m_shown = shown; emit infoChanged(); }
}

int PhotoLayer::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_slots.size()); }

QVariant PhotoLayer::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_slots.size()) return {};
    const Slot &s = m_slots[index.row()];
    const auto tit = m_tiers.constFind(m_tier);
    const photomap::Group *g = s.group >= 0 && tit != m_tiers.constEnd() && s.group < tit->size() ? &(*tit)[s.group] : nullptr;
    switch (role) {
    case X: return s.x;
    case Y: return s.y;
    case Shown: return g != nullptr;
    case Sc: return s.sc;
    case Op: return s.limb;
    case Hit: return s.limb > 0.5;
    case File: return g ? m_lib.recs[g->rep].file : QString();
    case Count: return g ? int(g->members.size()) : 0;
    case Fav: return g && m_lib.recs[g->rep].fav;
    case Same: return g && g->samePlace;
    default: return {};
    }
}

QHash<int, QByteArray> PhotoLayer::roleNames() const
{
    return { { X, "mx" }, { Y, "my" }, { File, "file" }, { Count, "count" }, { Fav, "fav" }, { Shown, "shown" }, { Sc, "sc" }, { Op, "op" }, { Hit, "hit" }, { Same, "same" } };
}

QVariantMap PhotoLayer::click(int row, bool motion)
{
    if (row < 0 || row >= m_slots.size() || m_slots[row].group < 0) return {};
    const photomap::Group &g = groups()[m_slots[row].group];
    if (g.samePlace || g.members.size() == 1 || m_tier >= kMaxExpand) {
        QStringList ids;
        ids.reserve(g.members.size());
        for (int i : g.members) ids << m_lib.recs[i].id;
        return { { QStringLiteral("ids"), ids } };
    }
    double w = 1e9, s = 1e9, e = -1e9, n = -1e9;
    const double ref = m_lib.recs[g.rep].lng;
    for (int i : g.members) {
        const double lng = geo::unwrapTo(m_lib.recs[i].lng, ref), lat = m_lib.recs[i].lat;
        w = std::min(w, lng); e = std::max(e, lng); s = std::min(s, lat); n = std::max(n, lat);
    }
    if (m_fit) m_fit(w, s, e, n, m_tier + 1, motion ? 500 : 0);   // 至少放大一级（和站点圆圈同一个做法）
    return {};
}

QString PhotoLayer::tipOf(int row) const
{
    if (row < 0 || row >= m_slots.size() || m_slots[row].group < 0) return {};
    const auto tit = m_tiers.constFind(m_tier);
    if (tit == m_tiers.constEnd()) return {};
    const photomap::Group &g = (*tit)[m_slots[row].group];
    const photomap::Rec &r = m_lib.recs[g.rep];
    if (g.members.size() > 1 && !g.samePlace && m_tier < kMaxExpand) return trd("%1 张，点一下放大").arg(g.members.size());
    QString place = r.planName.isEmpty() ? trd("未归类") : r.planName.toHtmlEscaped();
    if (!r.stopName.isEmpty()) place += QStringLiteral(" · ") + r.stopName.toHtmlEscaped();
    return g.members.size() > 1 ? trd("%1 张").arg(g.members.size()) + QStringLiteral(" · ") + place : place;
}

QVariantList PhotoLayer::pointsOf(const QString &planId) const
{
    QVariantList out;
    for (const photomap::Rec &r : m_lib.recs)
        if (r.hasPos && (planId.isEmpty() || r.plan == planId)) out << QVariant(QVariantList{ r.lat, r.lng });
    return out;
}

int PhotoLayer::rowOfPlan(const QString &planId) const
{
    const auto tit = m_tiers.constFind(m_tier);
    if (tit == m_tiers.constEnd()) return -1;
    for (int s = 0; s < m_slots.size(); ++s) {
        if (m_slots[s].group < 0) continue;
        for (int i : (*tit)[m_slots[s].group].members)
            if (m_lib.recs[i].plan == planId) return s;
    }
    return -1;
}

QVariantMap PhotoLayer::rowInfo(int row) const
{
    if (row < 0 || row >= m_slots.size()) return {};
    const QModelIndex i = index(row);
    return { { QStringLiteral("x"), data(i, X) }, { QStringLiteral("y"), data(i, Y) }, { QStringLiteral("count"), data(i, Count) }, { QStringLiteral("fav"), data(i, Fav) },
             { QStringLiteral("same"), data(i, Same) }, { QStringLiteral("file"), data(i, File) }, { QStringLiteral("shown"), data(i, Shown) } };
}

} // namespace tp::map
