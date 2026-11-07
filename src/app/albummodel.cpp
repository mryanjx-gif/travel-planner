#include "albummodel.h"

#include "perf.h"
#include "thumbprovider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
#include <QSet>

namespace tp::app {

namespace pm = tp::map::photomap;

AlbumModel::AlbumModel(QObject *parent) : QAbstractListModel(parent) { m_pool.setMaxThreadCount(1); }

AlbumModel::~AlbumModel()
{
    *m_alive = false;
    m_pool.waitForDone();
}

void AlbumModel::setRatioSource(std::function<QString()> dataDir, const QString &cacheRoot, std::function<QSize(const QString &, const QString &)> read)
{
    m_dataDir = std::move(dataDir);
    m_cacheRoot = cacheRoot;
    m_readSize = std::move(read);
}

// 缓存文件按数据文件夹分开（路径是相对的），放在缩略图缓存里：清缓存时一起清，清了重新读
static QString ratioFile(const QString &root, const QString &dir)
{
    return root + QStringLiteral("/photo-ratios-") + QString::fromLatin1(QCryptographicHash::hash(dir.toUtf8(), QCryptographicHash::Md5).toHex().left(12)) + QStringLiteral(".txt");
}

void AlbumModel::loadRatios(const QString &dir)
{
    m_ratioDir = dir;
    m_ratios.clear();
    QFile f(ratioFile(m_cacheRoot, dir));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    while (!f.atEnd()) {   // 一行：比例 <Tab> 相对路径
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        const qsizetype tab = line.indexOf(u'\t');
        const double a = tab > 0 ? line.left(tab).toDouble() : 0;
        if (a > 0) m_ratios.insert(line.mid(tab + 1), a);
    }
}

void AlbumModel::saveRatios() const
{
    QDir().mkpath(m_cacheRoot);
    QSaveFile f(ratioFile(m_cacheRoot, m_ratioDir));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;   // 写不了下次重新读，不影响显示
    for (auto it = m_ratios.constBegin(); it != m_ratios.constEnd(); ++it)
        if (it.value() > 0) f.write(QString::number(it.value(), 'f', 4).toUtf8() + '\t' + it.key().toUtf8() + '\n');
    f.commit();
}

void AlbumModel::fillRatios()
{
    if (!m_dataDir || !m_readSize) return;
    const QString dir = m_dataDir();
    if (dir != m_ratioDir) loadRatios(dir);
    QSet<QString> need;
    for (pm::Rec &r : m_lib.recs) {
        if (r.ratio > 0) continue;
        const auto it = m_ratios.constFind(r.file);
        if (it == m_ratios.constEnd()) { if (r.file.startsWith(QLatin1String("photos/"))) need.insert(r.file); }
        else r.ratio = *it;
    }
    if (need.isEmpty() || m_scanning || dir.isEmpty()) return;
    m_scanning = true;
    qInfo("相册：%d 张没记录比例，后台读文件头", int(need.size()));
    m_pool.start([this, dir, files = need.values(), alive = m_alive, read = m_readSize] {
        QHash<QString, double> got;
        QElapsedTimer t;
        t.start();
        for (const QString &file : files) {
            if (!*alive) return;
            const QSize s = read(dir, file);
            got.insert(file, s.width() > 0 && s.height() > 0 ? double(s.width()) / s.height() : 0.0);
            if (t.elapsed() >= 2000) {   // 上万张时每 2 秒排一次，不是每张都动
                QMetaObject::invokeMethod(this, [this, dir, got] { onRatios(dir, got, false); }, Qt::QueuedConnection);
                got.clear();
                t.restart();
            }
        }
        QMetaObject::invokeMethod(this, [this, dir, got] { onRatios(dir, got, true); }, Qt::QueuedConnection);
    });
}

void AlbumModel::onRatios(const QString &dir, const QHash<QString, double> &got, bool finished)
{
    if (finished) m_scanning = false;
    if (dir != m_ratioDir) { if (finished) fillRatios(); return; }   // 中途换了数据文件夹
    for (auto it = got.constBegin(); it != got.constEnd(); ++it) m_ratios.insert(it.key(), it.value());
    saveRatios();
    for (pm::Rec &r : m_lib.recs) if (r.ratio <= 0) r.ratio = m_ratios.value(r.file);
    if (finished) fillRatios();
    rebuildRows(true);
}

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
    rebuildRows(false);
    emit stateChanged();   // 重建之后再发：列表定位要用新的行
}

void AlbumModel::setCompact(bool c)
{
    if (c == m_compact) return;
    m_compact = c;
    rebuildRows(false);
    emit stateChanged();
}

void AlbumModel::setLayout(double width, double targetHeight, double gap)
{
    width = std::max(1.0, width);
    if (width == m_layout.width && targetHeight == m_layout.targetHeight && gap == m_layout.gap) return;
    m_layout = { width, targetHeight, gap };
    emit columnsChanged();
    rebuildRows(true);
}

void AlbumModel::refresh()
{
    if (!m_active || !m_get) return;
    tp::app::perf::Scope timing("相册重建", 30);
    const pm::Library old = std::move(m_lib);
    m_lib = pm::read(m_get());
    fillRatios();
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
    QVector<pm::Row> fresh = pm::rows(m_lib, m_view, m_plan, m_compact, m_layout);
    // 行的结构没变（比如点了精选）：只通知内容变了，元素和滚动位置都不动
    bool same = keep && fresh.size() == m_rows.size();
    for (int i = 0; same && i < fresh.size(); ++i) same = fresh[i].kind == m_rows[i].kind && fresh[i].recs.size() == m_rows[i].recs.size();
    if (same) {
        // 只通知真的变了的行：变了的行照片格子会重建，其余的不闪
        const auto differs = [&](const pm::Row &a, const pm::Row &b) {
            if (a.title != b.title || a.sub != b.sub || a.count != b.count || a.group != b.group || a.open != b.open) return true;
            if (a.height != b.height || a.widths != b.widths) return true;
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
        if (changed.size() > 64) emit dataChanged(index(0), index(int(m_rows.size()) - 1));   // 窗口变了宽度：几乎每行都变
        else for (int i : std::as_const(changed)) emit dataChanged(index(i), index(i));
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
    case RowH: return r.height;
    case Thumb: return r.rep >= 0 ? ThumbProvider::urlFor(m_lib.recs[r.rep].file, 160, 0) : QString();
    case Items: {
        QVariantList out;
        out.reserve(r.recs.size());
        for (int k = 0; k < r.recs.size(); ++k) {
            const pm::Rec &p = m_lib.recs[r.recs[k]];
            out << QVariantMap{ { QStringLiteral("w"), r.widths.value(k) }, { QStringLiteral("id"), p.id }, { QStringLiteral("file"), p.file }, { QStringLiteral("caption"), p.caption },
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
             { Group, "group" }, { Items, "items" }, { Open, "open" }, { Thumb, "thumb" }, { RowH, "rowH" } };
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

QString AlbumModel::anchorPhoto(int row) const
{
    for (int i = std::max(0, row); i < m_rows.size(); ++i)
        if (!m_rows[i].recs.isEmpty()) return m_lib.recs[m_rows[i].recs.first()].id;
    return {};
}

int AlbumModel::rowOfPhoto(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        for (int k : m_rows[i].recs) if (m_lib.recs[k].id == id) return i;
    return -1;
}

int AlbumModel::firstRowOf(const QString &kind) const
{
    for (int i = 0; i < m_rows.size(); ++i) if (m_rows[i].kind == kind) return i;
    return -1;
}

} // namespace tp::app
