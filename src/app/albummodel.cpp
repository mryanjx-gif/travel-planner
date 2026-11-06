#include "albummodel.h"

#include "perf.h"
#include "thumbprovider.h"

namespace tp::app {

namespace pm = tp::map::photomap;

AlbumModel::AlbumModel(QObject *parent) : QAbstractListModel(parent) {}

void AlbumModel::setViewStore(const QString &saved, std::function<void(const QString &)> save)
{
    if (saved == QLatin1String("trip") || saved == QLatin1String("year") || saved == QLatin1String("fav") || saved == QLatin1String("inbox")) m_view = saved;
    m_saveView = std::move(save);
}

void AlbumModel::setActive(bool on)
{
    if (on == m_active) return;
    m_active = on;
    emit activeChanged();
    if (on) refresh();
}

void AlbumModel::setView(const QString &v)
{
    if (v == m_view) return;
    m_view = v;
    if (m_saveView) m_saveView(v);
    emit stateChanged();
    rebuildRows(false);
}

void AlbumModel::setPlan(const QString &p)
{
    if (p == m_plan) return;
    m_plan = p;
    emit stateChanged();
    rebuildRows(false);
}

void AlbumModel::setCompact(bool c)
{
    if (c == m_compact) return;
    m_compact = c;
    emit stateChanged();
    rebuildRows(false);
}

void AlbumModel::setColumns(int n)
{
    n = std::max(1, n);
    if (n == m_columns) return;
    m_columns = n;
    emit columnsChanged();
    rebuildRows(true);
}

void AlbumModel::refresh()
{
    if (!m_active || !m_get) return;
    tp::app::perf::Scope timing("相册重建", 30);
    const pm::Library old = std::move(m_lib);
    m_lib = pm::read(m_get());
    if (!m_plan.isEmpty() && m_plan != QLatin1String(pm::kNoPos)) {   // 选中的那趟没有照片了
        bool ok = false;
        for (const pm::Trip &t : m_lib.trips) ok = ok || t.id == m_plan;
        if (!ok) { m_plan.clear(); emit stateChanged(); }
    }
    emit infoChanged();
    rebuildRows(true, &old);
}

void AlbumModel::rebuildRows(bool keep, const tp::map::photomap::Library *old)
{
    if (!m_active) return;
    QVector<pm::Row> fresh = pm::rows(m_lib, m_view, m_plan, m_compact, m_columns);
    // 行的结构没变（比如点了精选）：只通知内容变了，元素和滚动位置都不动
    bool same = keep && fresh.size() == m_rows.size();
    for (int i = 0; same && i < fresh.size(); ++i) same = fresh[i].kind == m_rows[i].kind && fresh[i].recs.size() == m_rows[i].recs.size();
    if (same) {
        // 只通知真的变了的行：变了的行照片格子会重建，其余的不闪
        const auto differs = [&](const pm::Row &a, const pm::Row &b) {
            if (a.title != b.title || a.sub != b.sub || a.count != b.count || a.group != b.group || a.open != b.open) return true;
            if (!old) return a.recs != b.recs;
            for (int k = 0; k < a.recs.size(); ++k) {
                const pm::Rec &x = old->recs.value(a.recs[k]), &y = m_lib.recs[b.recs[k]];
                if (x.id != y.id || x.fav != y.fav || x.file != y.file || x.caption != y.caption || x.planName != y.planName || x.takenAt != y.takenAt) return true;
            }
            return false;
        };
        QVector<int> changed;
        for (int i = 0; i < fresh.size(); ++i) if (differs(m_rows[i], fresh[i])) changed << i;
        m_rows = std::move(fresh);
        for (int i : std::as_const(changed)) emit dataChanged(index(i), index(i));
        return;
    }
    if (keep) emit aboutToRebuild();
    beginResetModel();
    m_rows = std::move(fresh);
    endResetModel();
    if (keep) emit rebuilt();
}

QVariantList AlbumModel::trips() const
{
    QVariantList out;
    for (const pm::Trip &t : m_lib.trips)
        out << QVariantMap{ { QStringLiteral("id"), t.id }, { QStringLiteral("name"), t.name }, { QStringLiteral("count"), t.count } };
    return out;
}

QStringList AlbumModel::inboxIds() const
{
    QStringList out;
    for (const pm::Rec &r : m_lib.recs) if (r.plan.isEmpty()) out << r.id;
    return out;
}

int AlbumModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_rows.size()); }

QVariant AlbumModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const pm::Row &r = m_rows[index.row()];
    switch (role) {
    case Kind: return r.kind;
    case Title: return r.title;
    case Sub: return r.sub;
    case PlanId: return r.planId;
    case StopNo: return r.stopNo;
    case RowCount: return r.count;
    case Group: return r.group;
    case Open: return r.open;
    case Thumb: return r.rep >= 0 ? ThumbProvider::urlFor(m_lib.recs[r.rep].file, 160, 0) : QString();
    case Items: {
        QVariantList out;
        out.reserve(r.recs.size());
        for (int i : r.recs) {
            const pm::Rec &p = m_lib.recs[i];
            out << QVariantMap{ { QStringLiteral("id"), p.id }, { QStringLiteral("file"), p.file }, { QStringLiteral("caption"), p.caption },
                                { QStringLiteral("fav"), p.fav }, { QStringLiteral("planName"), p.planName }, { QStringLiteral("takenAt"), p.takenAt },
                                { QStringLiteral("heic"), pm::isHeic(p.file) } };
        }
        return out;
    }
    default: return {};
    }
}

QHash<int, QByteArray> AlbumModel::roleNames() const
{
    return { { Kind, "kind" }, { Title, "title" }, { Sub, "sub" }, { PlanId, "planId" }, { StopNo, "stopNo" }, { RowCount, "count" },
             { Group, "group" }, { Items, "items" }, { Open, "open" }, { Thumb, "thumb" } };
}

QVariantMap AlbumModel::lightItem(const pm::Rec &r) const
{
    return { { QStringLiteral("id"), r.id }, { QStringLiteral("file"), r.file }, { QStringLiteral("thumb"), r.file.startsWith(QLatin1String("photos/")) ? ThumbProvider::urlFor(r.file, 320, 0) : QString() },
             { QStringLiteral("caption"), r.caption }, { QStringLiteral("stop"), r.stopName }, { QStringLiteral("stopNo"), r.stopNo }, { QStringLiteral("route"), r.route },
             { QStringLiteral("takenAt"), r.takenAt }, { QStringLiteral("fav"), r.fav }, { QStringLiteral("plan"), r.planName } };
}

QVariantMap AlbumModel::openGroup(int group, const QString &photoId) const
{
    QVariantList list;
    int at = 0;
    for (const pm::Row &row : m_rows) {
        if (row.group != group) continue;
        for (int i : row.recs) {
            if (m_lib.recs[i].id == photoId) at = int(list.size());
            list << lightItem(m_lib.recs[i]);
        }
    }
    return { { QStringLiteral("list"), list }, { QStringLiteral("index"), at } };
}

QVariantList AlbumModel::itemsFor(const QStringList &ids) const
{
    QHash<QString, int> byId;
    byId.reserve(m_lib.recs.size());
    for (int i = 0; i < m_lib.recs.size(); ++i) byId.insert(m_lib.recs[i].id, i);
    QVariantList out;
    for (const QString &id : ids) {
        const auto it = byId.constFind(id);
        if (it == byId.constEnd()) continue;
        QVariantMap m = lightItem(m_lib.recs[*it]);
        m.insert(QStringLiteral("fromStop"), m_lib.recs[*it].fromStop);
        out << m;
    }
    return out;
}

int AlbumModel::tripRow(const QString &planId) const
{
    for (int i = 0; i < m_rows.size(); ++i) if (m_rows[i].kind == QLatin1String("trip") && m_rows[i].planId == planId) return i;
    return -1;
}

int AlbumModel::firstRowOf(const QString &kind) const
{
    for (int i = 0; i < m_rows.size(); ++i) if (m_rows[i].kind == kind) return i;
    return -1;
}

} // namespace tp::app
