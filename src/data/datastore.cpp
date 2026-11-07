#include "datastore.h"

#include "trdata.h"

#include "atomicfile.h"
#include "jsonio.h"
#include "nativeformat.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QThreadPool>
#include <algorithm>
#include <memory>

namespace tp::data {

namespace {

const QRegularExpression &historyRe()
{
    static const QRegularExpression re(QStringLiteral("^travel-\\d{4}-\\d{2}-\\d{2}\\.json$"));
    return re;
}
const QRegularExpression &beforeAiRe()
{
    static const QRegularExpression re(QStringLiteral("^travel\\.before-ai-\\d{8}-\\d{6}\\.json$"));
    return re;
}

bool readAll(const QString &path, QByteArray *out, bool *exists = nullptr)
{
    QFile f(path);
    if (exists) *exists = f.exists();
    if (!f.open(QIODevice::ReadOnly)) return false;
    *out = f.readAll();
    return true;
}

QString two(int v) { return QStringLiteral("%1").arg(v, 2, 10, QChar(u'0')); }

// 计划的"签名"：去掉 updatedAt 后的紧凑 JSON（判断内容变没变）
QString planSig(const json::Value &plan)
{
    json::Value copy = plan;
    if (copy.isObject()) copy.mutObject().remove(QStringLiteral("updatedAt"));
    return json::stringify(copy, 0);
}

QString planId(const json::Value &plan) { return plan[QStringLiteral("id")].toString(); }

// 解析 travel.json 的文字（有 BOM 也接受）。失败时 error 是带行号的说明
json::Value parseText(const QByteArray &bytes, QString *error)
{
    QByteArray b = bytes;
    if (b.startsWith("\xEF\xBB\xBF")) b.remove(0, 3);
    const QString text = QString::fromUtf8(b);
    json::ParseError pe;
    int consumed = 0;
    json::Value v = json::parse(text, &pe, &consumed);
    if (pe.ok) {
        // 值后面只允许空白
        for (int k = consumed; k < text.size(); ++k) {
            const QChar c = text.at(k);
            if (c != u' ' && c != u'\t' && c != u'\n' && c != u'\r') {
                pe.ok = false;
                int line = 1, col = 1;
                for (int j = 0; j < k; ++j) { if (text.at(j) == u'\n') { ++line; col = 1; } else ++col; }
                pe.line = line; pe.column = col;
                pe.message = trd("JSON 后面还有多余的内容");
                break;
            }
        }
    }
    if (!pe.ok) {
        if (error) *error = trd("第 %1 行第 %2 列：%3").arg(pe.line).arg(pe.column).arg(pe.message);
        return {};
    }
    return v;
}

// 进程池只给本对象用，析构时等它做完
QThreadPool *hashPool()
{
    static QThreadPool pool;
    pool.setMaxThreadCount(1);
    return &pool;
}

} // namespace

DataStore::DataStore(QObject *parent) : QObject(parent)
{
    m_watch.setTimerType(Qt::CoarseTimer);
    connect(&m_watch, &QTimer::timeout, this, &DataStore::tick);
    m_lockTimer.setTimerType(Qt::CoarseTimer);
    m_lockTimer.setInterval(LockFile::kHeartbeatMs);
    connect(&m_lockTimer, &QTimer::timeout, this, &DataStore::onLockTimer);
}

DataStore::~DataStore()
{
    m_watch.stop();
    m_lockTimer.stop();
    m_lock.release();
    hashPool()->waitForDone();
}

QString DataStore::dataFile() const { return QDir(m_dir).filePath(nativefmt::dataFileName()); }

QByteArray DataStore::sha256Hex(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

void DataStore::setDirty(bool d)
{
    if (m_dirty == d) return;
    m_dirty = d;
    emit dirtyChanged(d);
}

QString DataStore::stampName(const QString &prefix) const
{
    const QDateTime t = now();
    return QStringLiteral("%1-%2%3%4-%5%6%7.json")
        .arg(prefix).arg(t.date().year()).arg(two(t.date().month()), two(t.date().day()),
                                              two(t.time().hour()), two(t.time().minute()), two(t.time().second()));
}

bool DataStore::load(const QString &dataDir)
{
    if (m_gone) { m_gone = false; emit goneChanged(); }
    const bool ok = loadInner(dataDir);
    emit contentChanged();
    return ok;
}

bool DataStore::loadInner(const QString &dataDir)
{
    ++m_generation;
    const QString dir = QDir::cleanPath(QFileInfo(dataDir).absoluteFilePath());
    if (dir != m_dir) m_lock.release();
    m_dir = dir;
    m_loadError.clear();
    m_fileVersion = 0;
    m_knownHash.clear();
    m_lastNotified.clear();
    m_hasElectronData = false;
    setDirty(false);

    // 锁：别人正开着就只读（照样读出来给用户看）
    const bool wasLocked = m_lockedByOther;
    m_lockOwner = {};
    m_lockedByOther = m_lock.acquire(m_dir, false, &m_lockOwner) == LockFile::Result::HeldByOther;
    if (m_lockedByOther || m_lock.held()) m_lockTimer.start(); else m_lockTimer.stop();
    if (wasLocked != m_lockedByOther) emit lockChanged();

    const auto fallback = [this](State st) {
        m_state = st;
        m_data = nativefmt::emptyDoc(m_appVersion, now());
        snapPlans();
    };

    QByteArray bytes;
    bool exists = false;
    if (!readAll(dataFile(), &bytes, &exists)) {
        if (exists) {
            m_loadError = trd("travel.json 打不开（可能被别的程序占用或没有权限）");
            fallback(State::Unreadable);
            return false;
        }
        m_hasElectronData = QFileInfo(QDir(m_dir).filePath(nativefmt::electronDataFileName())).isFile();
        fallback(State::Missing);
        return true;
    }

    m_knownHash = sha256Hex(bytes);
    QString err;
    json::Value doc = parseText(bytes, &err);
    if (!err.isEmpty() || !doc.isObject()) {
        m_loadError = err.isEmpty() ? trd("travel.json 的内容不是一个对象") : err;
        fallback(State::Unreadable);
        return false;
    }
    switch (nativefmt::check(doc, &m_fileVersion)) {
    case nativefmt::Check::NotOurs:
        m_loadError = trd("travel.json 不是旅行计划的数据文件");
        fallback(State::Unreadable);
        return false;
    case nativefmt::Check::Newer:
        m_state = State::Newer;
        break;
    case nativefmt::Check::Ok:
        m_state = State::Loaded;
        break;
    }
    m_data = std::move(doc);
    nativefmt::normalize(m_data);
    snapPlans();
    return true;
}

void DataStore::takeOverLock()
{
    if (m_dir.isEmpty()) return;
    m_lockOwner = {};
    m_lock.acquire(m_dir, true);
    m_lockedByOther = false;
    m_lockTimer.start();
    emit lockChanged();
}

void DataStore::onLockTimer()
{
    if (m_dir.isEmpty()) return;
    if (m_lockedByOther) {
        // 只读中：对方关掉了（锁没了或过时了）就自动接管
        const LockOwner cur = LockFile::read(m_dir);
        if (!cur.valid || m_lock.isStale(cur)) {
            if (m_lock.acquire(m_dir, false, &m_lockOwner) != LockFile::Result::HeldByOther) {
                m_lockedByOther = false;
                m_lockOwner = {};
                emit lockChanged();
            }
        } else if (cur.machine != m_lockOwner.machine || cur.pid != m_lockOwner.pid) {
            m_lockOwner = cur;
            emit lockChanged();
        }
        return;
    }
    LockOwner other;
    if (!m_lock.refresh(&other)) {
        m_lockedByOther = true;
        m_lockOwner = other;
        emit lockChanged();
    }
}

void DataStore::modify(const std::function<void(json::Value &)> &fn)
{
    fn(m_data);
    setDirty(true);
    emit contentChanged();
}

void DataStore::replaceData(json::Value v)
{
    m_data = std::move(v);
    setDirty(true);
    emit contentChanged();
}

void DataStore::restoreData(json::Value v)
{
    m_data = std::move(v);
    snapPlans();
    setDirty(true);
    emit contentChanged();
}

void DataStore::snapPlans()
{
    m_planSigs.clear();
    for (const json::Value &p : m_data[QStringLiteral("plans")].asArray())
        m_planSigs.insert(planId(p), planSig(p));
}

// 内容和上次保存时不一样的计划，写 updatedAt = 现在（毫秒）
void DataStore::stampUpdated()
{
    const double nowMs = double(now().toMSecsSinceEpoch());
    json::Object &o = m_data.mutObject();
    json::Value *plans = o.find(QStringLiteral("plans"));
    if (!plans || !plans->isArray()) return;
    // 先只读地找出要改的，避免没变化时也把整个数组复制一份（写时复制）
    std::vector<size_t> changed;
    const json::Array &ro = plans->asArray();
    for (size_t k = 0; k < ro.size(); ++k) {
        const auto it = m_planSigs.constFind(planId(ro[k]));
        if (it == m_planSigs.constEnd() || *it != planSig(ro[k])) changed.push_back(k);
    }
    if (changed.empty()) return;
    json::Array &arr = plans->mutArray();
    for (size_t k : changed)
        if (arr[k].isObject()) arr[k].mutObject().set(QStringLiteral("updatedAt"), json::Value(nowMs));
}

void DataStore::snapshotDaily(const QByteArray &current)
{
    // 备份失败不影响正常保存
    const QDate d = now().date();
    const QString dir = QDir(m_dir).filePath(nativefmt::historyDirName());
    const QString name = QStringLiteral("travel-%1-%2-%3.json").arg(d.year()).arg(two(d.month()), two(d.day()));
    const QString file = QDir(dir).filePath(name);
    if (QFileInfo::exists(file)) return;   // 今天已经存过（存的是今天第一次保存之前的样子）
    if (!QDir().mkpath(dir)) return;
    if (!platform::writeFile(file, current).ok) return;
    QStringList list = QDir(dir).entryList(QDir::Files | QDir::Hidden);
    list = list.filter(historyRe());
    list.sort();
    for (int k = 0; k < list.size() - kHistoryKeep; ++k) QFile::remove(QDir(dir).filePath(list.at(k)));
}

DataStore::SaveResult DataStore::save(bool force)
{
    if (m_state == State::None) return { false, false, trd("还没有打开数据文件夹") };
    if (m_state == State::Unreadable)
        return { false, false, trd("travel.json 读不出来，为了不覆盖原文件，现在是只读模式。请从 travel.backup.json 或历史版本恢复后重新加载。") };
    if (m_state == State::Newer)
        return { false, false, trd("这份数据是更新版本的旅行计划写的，这个版本只能看、不能改。请先更新软件。") };
    if (m_lockedByOther)
        return { false, false, trd("这份数据正在 %1 里打开，现在只能看。").arg(m_lockOwner.describe()) };
    if (!QFileInfo(m_dir).isDir()) return { false, false, trd("数据文件夹不存在：%1").arg(QDir::toNativeSeparators(m_dir)) };

    stampUpdated();
    nativefmt::stamp(m_data, m_appVersion, now());
    const QByteArray text = nativefmt::serialize(m_data);
    const QString file = dataFile();

    QByteArray current;
    const bool haveCurrent = readAll(file, &current);
    if (haveCurrent && !force && !m_knownHash.isEmpty() && sha256Hex(current) != m_knownHash)
        return { false, true, {} };

    ++m_generation;   // 正在跑的外部修改检测结果作废
    if (haveCurrent) {
        snapshotDaily(current);
        const auto b = platform::writeFile(QDir(m_dir).filePath(nativefmt::backupFileName()), current);
        if (!b.ok) return { false, false, b.error };
    }
    // 改名前最后再看一眼：备份这段时间里又被别处改了，就不覆盖
    if (!force) {
        QByteArray again;
        const bool haveAgain = readAll(file, &again);
        if (haveAgain != haveCurrent || again != current) return { false, true, {} };
    }
    const auto w = platform::writeViaTemp(file, QDir(m_dir).filePath(nativefmt::tmpFileName()), text);
    if (!w.ok) return { false, false, w.error };
    QByteArray back;
    if (!readAll(file, &back) || back != text) return { false, false, trd("写入后回读内容不一致") };

    m_knownHash = sha256Hex(text);
    m_lastNotified.clear();
    m_hasElectronData = false;
    if (m_state == State::Missing) m_state = State::Loaded;
    snapPlans();
    setDirty(false);
    return { true, false, {} };
}

QString DataStore::flushBeforeExit()
{
    if (!m_dirty || m_state == State::None) return {};
    if (!isReadOnly()) {
        const SaveResult r = save(false);
        if (r.ok) return {};
    }
    // 冲突、失败或只读：不碰 travel.json，另存一份，保证改动不丢
    return saveRescueCopy();
}

QString DataStore::saveRescueCopy()
{
    if (m_state == State::None || m_dir.isEmpty()) return {};
    const QString name = stampName(QStringLiteral("travel.unsaved"));
    json::Value copy = m_data;
    nativefmt::stamp(copy, m_appVersion, now());
    const auto w = platform::writeFile(QDir(m_dir).filePath(name), nativefmt::serialize(copy));
    return w.ok ? name : QString();
}

QString DataStore::snapshotBeforeAi(QString *error)
{
    const QString name = stampName(QStringLiteral("travel.before-ai"));
    const auto c = platform::copyFileOverwrite(dataFile(), QDir(m_dir).filePath(name));
    if (!c.ok) { if (error) *error = c.error; return {}; }
    QStringList list = QDir(m_dir).entryList(QDir::Files).filter(beforeAiRe());
    list.sort();
    std::reverse(list.begin(), list.end());
    for (int k = kBeforeAiKeep; k < list.size(); ++k) QFile::remove(QDir(m_dir).filePath(list.at(k)));
    return name;
}

QString DataStore::snapshotBeforeImport(QString *error)
{
    if (!QFileInfo::exists(dataFile())) return {};
    const QString name = stampName(QStringLiteral("travel.before-import"));
    const auto c = platform::copyFileOverwrite(dataFile(), QDir(m_dir).filePath(name));
    if (!c.ok) { if (error) *error = c.error; return {}; }
    return name;
}

QList<DataStore::HistoryEntry> DataStore::historyList() const
{
    QList<HistoryEntry> out;
    const QDir dir(QDir(m_dir).filePath(nativefmt::historyDirName()));
    QStringList names = dir.entryList(QDir::Files | QDir::Hidden).filter(historyRe());
    names.sort();
    std::reverse(names.begin(), names.end());
    for (const QString &n : names) {
        QByteArray bytes;
        if (!readAll(dir.filePath(n), &bytes)) continue;
        QString err;
        const json::Value v = parseText(bytes, &err);
        const int plans = err.isEmpty() ? int(v[QStringLiteral("plans")].asArray().size()) : 0;
        out.append({ n, n.mid(7, 10), bytes.size(), plans });
    }
    return out;
}

DataStore::SaveResult DataStore::restoreHistory(const QString &name)
{
    if (m_dir.isEmpty()) return { false, false, trd("还没有打开数据文件夹") };
    if (isReadOnly()) return { false, false, trd("现在是只读模式，不能恢复。") };
    if (!historyRe().match(name).hasMatch()) return { false, false, trd("备份名称不对") };
    QByteArray text;
    if (!readAll(QDir(QDir(m_dir).filePath(nativefmt::historyDirName())).filePath(name), &text))
        return { false, false, trd("读不到这份备份") };
    QString err;
    const json::Value v = parseText(text, &err);
    if (!err.isEmpty() || nativefmt::check(v) == nativefmt::Check::NotOurs)
        return { false, false, trd("这份备份的内容不对") };
    ++m_generation;
    const QString file = dataFile();
    if (QFileInfo::exists(file)) platform::copyFileOverwrite(file, QDir(m_dir).filePath(nativefmt::backupFileName()));
    const auto w = platform::writeViaTemp(file, QDir(m_dir).filePath(nativefmt::tmpFileName()), text);
    if (!w.ok) return { false, false, w.error };
    QByteArray back;
    if (!readAll(file, &back) || back != text) return { false, false, trd("写入后回读内容不一致") };
    load(m_dir);
    emit reloaded();
    return { true, false, {} };
}

void DataStore::startWatching(int intervalMs)
{
    m_watch.start(intervalMs);
}

void DataStore::stopWatching()
{
    m_watch.stop();
}

void DataStore::tick()
{
    if (m_hashing || m_knownHash.isEmpty() || m_dir.isEmpty()) return;
    m_hashing = true;
    const quint64 gen = m_generation;
    const QString file = dataFile();
    QPointer<DataStore> self(this);
    hashPool()->start([self, gen, file] {
        QByteArray bytes, hash;
        bool exists = true;
        if (readAll(file, &bytes, &exists)) hash = sha256Hex(bytes);
        if (self) QMetaObject::invokeMethod(self.data(), [self, gen, hash, exists] { if (self) self->onHashResult(gen, hash, exists); }, Qt::QueuedConnection);
    });
}

void DataStore::onHashResult(quint64 generation, const QByteArray &hash, bool exists)
{
    m_hashing = false;
    if (generation != m_generation) return;   // 这期间保存 / 重新读取过，结果作废
    if (m_gone == exists) { m_gone = !exists; emit goneChanged(); }
    if (!hash.isEmpty() && hash != m_knownHash && hash != m_lastNotified) {
        m_lastNotified = hash;
        emit externallyChanged();
    }
}

bool DataStore::checkExternalNow()
{
    if (m_knownHash.isEmpty()) return false;
    QByteArray bytes;
    if (!readAll(dataFile(), &bytes)) return false;
    const QByteArray hash = sha256Hex(bytes);
    if (hash != m_knownHash && hash != m_lastNotified) {
        m_lastNotified = hash;
        emit externallyChanged();
        return true;
    }
    return false;
}

} // namespace tp::data
