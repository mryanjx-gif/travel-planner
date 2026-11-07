#include "tilecache.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QThread>
#include <algorithm>
#include <vector>

namespace tp::services {

namespace {
// 文件名必须是 <底图>/<z>/<x>/<y>.t（数的时候不认识的文件不管，也不删）
bool validRel(const QString &rel)
{
    const QStringList p = rel.split(u'/');
    if (p.size() != 4 || p[0].isEmpty() || !p[3].endsWith(QLatin1String(".t"))) return false;
    bool a = false, b = false, c = false;
    p[1].toInt(&a);
    p[2].toInt(&b);
    p[3].chopped(2).toInt(&c);
    return a && b && c;
}
} // namespace

TileCache::TileCache(const QString &dir, QObject *parent) : QObject(parent), m_dir(QDir::cleanPath(dir)) {}

TileCache::~TileCache()
{
    if (m_trashThread) { m_trashThread->wait(); delete m_trashThread; }
    if (m_scanThread) {
        m_scanCancel->store(true);
        m_scanThread->wait();
        delete m_scanThread;
    }
}

qint64 TileCache::now() const { return m_now ? m_now() : QDateTime::currentMSecsSinceEpoch(); }

void TileCache::startScan()
{
    if (m_scanThread || m_scanned) return;
    m_scanCancel = std::make_shared<std::atomic_bool>(false);
    m_scanResult = std::make_shared<QHash<QString, Entry>>();
    const QString dir = m_dir;
    auto cancel = m_scanCancel;
    auto result = m_scanResult;
    m_scanThread = QThread::create([dir, cancel, result] {
        QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        const int prefix = int(dir.size()) + 1;
        while (it.hasNext() && !cancel->load()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            const QString rel = fi.filePath().mid(prefix);
            if (!validRel(rel)) continue;
            result->insert(rel, Entry{ fi.size(), fi.lastModified().toMSecsSinceEpoch() });
        }
    });
    connect(m_scanThread, &QThread::finished, this, &TileCache::mergeScan, Qt::QueuedConnection);
    m_scanThread->start(QThread::LowPriority);
}

void TileCache::waitScan()
{
    if (!m_scanThread) return;
    m_scanThread->wait();
    mergeScan();
}

void TileCache::mergeScan()
{
    if (!m_scanThread) return;
    m_scanThread->wait();
    delete m_scanThread;
    m_scanThread = nullptr;
    // 数的过程中新存进来的（已经在索引里）以索引为准
    for (auto it = m_scanResult->constBegin(); it != m_scanResult->constEnd(); ++it) {
        if (m_index.contains(it.key())) continue;
        m_index.insert(it.key(), it.value());
        m_bytes += it.value().size;
    }
    m_scanResult.reset();
    m_scanned = true;
    qInfo("瓦片缓存：%d 张，%.1f MB（%s）", int(m_index.size()), m_bytes / 1048576.0, qUtf8Printable(QDir::toNativeSeparators(m_dir)));
    emit scanDone();
    emit sizeChanged();
    evictIfNeeded();
}

bool TileCache::contains(const TileKey &k) const
{
    if (m_clearing) return false;
    const QString rel = k.path();
    if (m_index.contains(rel)) return true;
    return !m_scanned && QFileInfo::exists(m_dir + u'/' + rel);
}

std::optional<TileCache::Hit> TileCache::get(const TileKey &k)
{
    if (m_clearing) return std::nullopt;
    const QString rel = k.path();
    QFile f(m_dir + u'/' + rel);
    if (!f.open(QIODevice::ReadOnly)) {
        if (const auto it = m_index.find(rel); it != m_index.end()) { m_bytes -= it->size; m_index.erase(it); }
        return std::nullopt;
    }
    Hit h;
    h.data = f.readAll();
    if (h.data.isEmpty()) return std::nullopt;
    qint64 mtime;
    if (const auto it = m_index.constFind(rel); it != m_index.constEnd()) mtime = it->mtime;
    else {
        mtime = QFileInfo(f).lastModified().toMSecsSinceEpoch();
        if (m_scanned) { m_index.insert(rel, Entry{ h.data.size(), mtime }); m_bytes += h.data.size(); }
    }
    h.stale = now() - mtime > qint64(kStaleDays) * 86400 * 1000;
    return h;
}

bool TileCache::put(const TileKey &k, const QByteArray &data)
{
    if (data.isEmpty() || m_clearing) return false;
    const QString rel = k.path();
    const QString file = m_dir + u'/' + rel;
    if (!QDir().mkpath(QFileInfo(file).absolutePath())) return false;
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        qWarning("瓦片缓存写不进去：%s（%s）", qUtf8Printable(QDir::toNativeSeparators(file)), qUtf8Printable(f.errorString()));
        return false;
    }
    Entry &e = m_index[rel];
    m_bytes += data.size() - e.size;
    e.size = data.size();
    e.mtime = now();
    emit sizeChanged();
    evictIfNeeded();
    return true;
}

void TileCache::evictIfNeeded()
{
    if (!m_scanned || m_bytes <= m_max) return;
    std::vector<std::pair<qint64, QString>> order;   // (时间, 相对路径)，旧的在前
    order.reserve(m_index.size());
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) order.emplace_back(it->mtime, it.key());
    std::sort(order.begin(), order.end());
    const qint64 target = m_max / 10 * 9;
    int removed = 0;
    const qint64 before = m_bytes;
    for (const auto &entry : order) {
        if (m_bytes <= target) break;
        const QString &rel = entry.second;
        QFile::remove(m_dir + u'/' + rel);   // 删不掉（被占用）也从索引里拿掉，下次数的时候再算进来
        m_bytes -= m_index.value(rel).size;
        m_index.remove(rel);
        ++removed;
    }
    qInfo("瓦片缓存超过上限（%.0f MB）：删了最旧的 %d 张，%.1f → %.1f MB", m_max / 1048576.0, removed, before / 1048576.0, m_bytes / 1048576.0);
    emit sizeChanged();
}

void TileCache::clear()
{
    if (m_scanThread) {
        m_scanCancel->store(true);
        m_scanThread->wait();
        delete m_scanThread;
        m_scanThread = nullptr;
        m_scanResult.reset();
    }
    // 只删底图子文件夹（缓存目录本身留着）
    const QStringList subs = QDir(m_dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &s : subs) QDir(m_dir + u'/' + s).removeRecursively();
    m_index.clear();
    m_bytes = 0;
    m_scanned = true;
    qInfo("瓦片缓存已清空");
    emit sizeChanged();
}

void TileCache::clearToTrash()
{
    if (m_clearing) return;
    if (m_scanThread) {
        m_scanCancel->store(true);
        m_scanThread->wait();
        delete m_scanThread;
        m_scanThread = nullptr;
        m_scanResult.reset();
    }
    m_clearing = true;
    m_trashPath.clear();
    emit sizeChanged();
    const QString dir = m_dir;
    m_trashThread = QThread::create([this, dir] {
        QFile d(dir);
        const bool ok = !QFileInfo::exists(dir) || d.moveToTrash();   // 进不了回收站时失败，不会永久删除（photolib.h）
        const QString where = ok ? d.fileName() : QString();
        QMetaObject::invokeMethod(this, [this, ok, where] {
            m_trashThread->wait();
            delete m_trashThread;
            m_trashThread = nullptr;
            m_clearing = false;
            m_trashPath = where;
            m_index.clear();
            m_bytes = 0;
            m_scanned = ok;
            QDir().mkpath(m_dir);
            if (!ok) startScan();   // 可能只移走了一部分，重新数
            qInfo("瓦片缓存%s", ok ? "已移进回收站" : "没能移进回收站");
            emit sizeChanged();
            emit trashDone(ok);
        }, Qt::QueuedConnection);
    });
    m_trashThread->start();
}

} // namespace tp::services
