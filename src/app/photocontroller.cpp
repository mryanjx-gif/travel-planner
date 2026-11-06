#include "photocontroller.h"

#include "plans.h"

#include "appcontroller.h"
#include "datastore.h"
#include "exif.h"
#include "photorotate.h"
#include "thumbcache.h"
#include "thumbprovider.h"
#include "translator.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace tp::app {

using json::Value;
namespace pl = data::photolib;

namespace {

QVariantMap result(bool ok, const QString &error = {})
{
    return { { QStringLiteral("ok"), ok }, { QStringLiteral("error"), error } };
}

QVariant toVariant(const Value &v)
{
    switch (v.type()) {
    case Value::Type::Bool: return v.toBool();
    case Value::Type::Number: return v.toNumber();
    case Value::Type::String: return v.toString();
    case Value::Type::Array: { QVariantList l; for (const Value &x : v.asArray()) l << toVariant(x); return l; }
    case Value::Type::Object: { QVariantMap m; for (const auto &kv : v.asObject().members()) m.insert(kv.first, toVariant(kv.second)); return m; }
    default: return {};
    }
}

QString num(double v, int maxDecimals)
{
    QString s = QString::number(v, 'f', maxDecimals);
    if (s.contains(u'.')) { while (s.endsWith(u'0')) s.chop(1); if (s.endsWith(u'.')) s.chop(1); }
    return s;
}

} // namespace

PhotoController::PhotoController(AppController *app, data::ThumbCache *cache, ThumbProvider *provider, QObject *parent)
    : QObject(parent), m_app(app), m_cache(cache), m_provider(provider)
{
    connect(app, &AppController::dataChanged, this, &PhotoController::onDataChanged);
    connect(app, &AppController::docReplaced, this, &PhotoController::onDataChanged);
    onDataChanged();
}

PhotoController::~PhotoController() { shutdown(); }

void PhotoController::shutdown()
{
    m_cancel = true;
    for (QThread *t : { m_thread, m_trashThread, m_cacheThread })
        if (t) t->wait();
}

QString PhotoController::dataDir() const { return m_app->store()->dataDir(); }
QString PhotoController::cacheDir() const { return QDir::toNativeSeparators(m_cache->root()); }

void PhotoController::onDataChanged()
{
    if (m_provider) m_provider->setDataDir(dataDir());
    ++m_revision;
    emit libraryChanged();
}

int PhotoController::count() const { return int(m_app->store()->data()[QStringLiteral("photos")].asArray().size()); }

QString PhotoController::thumbUrl(const QString &file, int width) const
{
    return ThumbProvider::urlFor(file, width, 0);
}

QString PhotoController::planName(const QString &id) const
{
    if (id.isEmpty()) return {};
    for (const Value &p : m_app->store()->data()[QStringLiteral("plans")].asArray())
        if (p[QStringLiteral("id")].toString() == id) { const QString n = p[QStringLiteral("name")].toString(); return n.isEmpty() ? tp::tr("未命名计划") : n; }
    return {};
}

QString PhotoController::stopName(const QString &planId, const QString &stopId) const
{
    if (planId.isEmpty() || stopId.isEmpty()) return {};
    for (const Value &p : m_app->store()->data()[QStringLiteral("plans")].asArray()) {
        if (p[QStringLiteral("id")].toString() != planId) continue;
        for (const Value &s : p[QStringLiteral("stops")].asArray())
            if (s[QStringLiteral("id")].toString() == stopId) return s[QStringLiteral("name")].toString();
    }
    return {};
}

QVariantMap PhotoController::photoMap(const Value &ph) const
{
    QVariantMap m = toVariant(ph).toMap();
    const QString file = ph[QStringLiteral("file")].toString();
    const QString id = ph[QStringLiteral("id")].toString();
    const bool local = file.startsWith(QLatin1String("photos/"));
    const QString abs = local ? dataDir() + u'/' + file : QString();
    m.insert(QStringLiteral("missing"), !local || !QFileInfo(abs).isFile());
    const QString ext = QFileInfo(file).suffix().toLower();
    m.insert(QStringLiteral("heic"), ext == QLatin1String("heic") || ext == QLatin1String("heif") || ext == QLatin1String("avif"));
    m.insert(QStringLiteral("canRotate"), local && pl::canRotate(abs));
    m.insert(QStringLiteral("planName"), planName(ph[QStringLiteral("plan")].toString()));
    m.insert(QStringLiteral("stopName"), stopName(ph[QStringLiteral("plan")].toString(), ph[QStringLiteral("stop")].toString()));
    const int v = m_thumbVersion.value(id) + m_epoch;
    m.insert(QStringLiteral("thumb"), local ? ThumbProvider::urlFor(file, 320, v) : QString());
    m.insert(QStringLiteral("thumbLarge"), local ? ThumbProvider::urlFor(file, 1280, v) : QString());
    return m;
}

QVariantList PhotoController::library() const
{
    QVariantList l;
    for (const Value &ph : m_app->store()->data()[QStringLiteral("photos")].asArray()) l << photoMap(ph);
    return l;
}

QString PhotoController::exposureText(const QVariantMap &p) const
{
    QStringList parts;
    const QVariantMap e = p.value(QStringLiteral("exposure")).toMap();
    if (e.contains(QStringLiteral("f"))) parts << QStringLiteral("f/") + num(e.value(QStringLiteral("f")).toDouble(), 1);
    if (e.contains(QStringLiteral("t"))) {
        const double t = e.value(QStringLiteral("t")).toDouble();
        parts << (t > 0 && t < 0.5 ? QStringLiteral("1/%1 s").arg(std::lround(1.0 / t)) : num(t, 1) + QStringLiteral(" s"));
    }
    if (e.contains(QStringLiteral("iso"))) parts << QStringLiteral("ISO %1").arg(e.value(QStringLiteral("iso")).toInt());
    if (e.contains(QStringLiteral("fl35"))) parts << QStringLiteral("%1 mm").arg(e.value(QStringLiteral("fl35")).toInt());
    else if (e.contains(QStringLiteral("fl"))) parts << num(e.value(QStringLiteral("fl")).toDouble(), 1) + QStringLiteral(" mm");
    if (e.contains(QStringLiteral("ev")) && e.value(QStringLiteral("ev")).toDouble() != 0) {
        const double ev = e.value(QStringLiteral("ev")).toDouble();
        parts << (ev > 0 ? QStringLiteral("+") : QString()) + num(ev, 1) + QStringLiteral(" EV");
    }
    return parts.join(QStringLiteral(" · "));
}

// ---------- 导入 ----------

void PhotoController::setStage(const QString &s)
{
    if (m_stage == s) return;
    m_stage = s;
    emit importChanged();
}

QVariantMap PhotoController::startImport(const QVariantList &urls, const QString &planId, const QString &stopId)
{
    if (!m_stage.isEmpty() || m_thread) return result(false, tp::tr("正在导入照片，请等这一批做完。"));
    if (m_app->store()->state() == data::DataStore::State::None || m_app->store()->isReadOnly())
        return result(false, tp::tr("现在的数据是只读的，不能导入照片。"));
    QStringList paths;
    for (const QVariant &u : urls) {
        const QString p = m_app->localPath(u);
        if (!p.isEmpty() && QFileInfo(p).isFile()) paths << QDir::fromNativeSeparators(p);
    }
    if (paths.isEmpty()) return result(false, tp::tr("没有选照片"));
    m_forcePlan = planId;
    m_forceStop = stopId;
    m_items.clear();
    m_done = 0;
    m_total = int(paths.size());
    m_cancel = false;
    setStage(QStringLiteral("reading"));
    emit importProgress();
    const Value doc = m_app->store()->data();   // 写时复制：后台线程读这一份
    const QString dir = dataDir();
    m_thread = QThread::create([this, paths, doc, dir, planId, stopId] {
        QList<Item> items;
        QHash<QString, QByteArray> hashCache;
        QHash<QByteArray, QString> batch;   // 同一批里重复选了同一个文件
        int n = 0;
        for (const QString &p : paths) {
            if (m_cancel) break;
            ++n;
            QMetaObject::invokeMethod(this, [this, n] { m_done = n; emit importProgress(); }, Qt::QueuedConnection);
            if (!pl::isImportable(p)) { qInfo("导入照片：不认识的格式，跳过 %s", qUtf8Printable(QDir::toNativeSeparators(p))); continue; }
            const data::exif::Info info = data::exif::readFile(p);
            Item it;
            it.in.src = p;
            it.in.name = QFileInfo(p).fileName();
            it.in.exif = data::exif::photoFields(info);
            it.container = data::exif::containerName(info.container);
            it.takenAt = data::exif::takenAt(info);
            it.tz = data::exif::timeZone(info);
            it.hasGps = info.hasGps();
            const pl::Guess g = pl::guess(doc, it.takenAt, info.lat, info.lng, planId, stopId);
            it.in.plan = g.plan;
            it.in.stop = g.stop;
            it.why = g.why;
            it.dupOf = pl::findDuplicate(doc, dir, p, &hashCache);
            if (it.dupOf.isEmpty()) {
                const QByteArray h = pl::fileSha1(p);
                if (!h.isEmpty() && batch.contains(h)) it.dupOf = QStringLiteral("batch:") + batch.value(h);
                else if (!h.isEmpty()) batch.insert(h, it.in.name);
            }
            it.in.skip = !it.dupOf.isEmpty();
            items << it;
        }
        QMetaObject::invokeMethod(this, [this, items] { readDone(items); }, Qt::QueuedConnection);
    });
    m_thread->start();
    return result(true);
}

void PhotoController::readDone(const QList<Item> &items)
{
    if (m_thread) { m_thread->wait(); delete m_thread; m_thread = nullptr; }
    if (m_cancel) {
        m_items.clear();
        setStage(QString());
        emit importFinished({ { QStringLiteral("ok"), false }, { QStringLiteral("cancelled"), true } });
        return;
    }
    m_items = items;
    emit importItemsChanged();
    if (m_items.isEmpty()) {
        setStage(QString());
        emit importFinished(result(false, tp::tr("选的文件里没有能导入的照片（支持 JPG、PNG、WebP、HEIC、GIF、BMP）。")));
        return;
    }
    setStage(QStringLiteral("review"));
}

QVariantList PhotoController::importItems() const
{
    QVariantList l;
    for (const Item &it : m_items) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), it.in.name);
        m.insert(QStringLiteral("thumb"), ThumbProvider::urlFor(it.in.src, 320, 0));
        m.insert(QStringLiteral("takenAt"), it.takenAt);
        m.insert(QStringLiteral("tz"), it.tz);
        m.insert(QStringLiteral("hasGps"), it.hasGps);
        m.insert(QStringLiteral("why"), it.why);
        m.insert(QStringLiteral("plan"), it.in.plan);
        m.insert(QStringLiteral("stop"), it.in.stop);
        m.insert(QStringLiteral("skip"), it.in.skip);
        m.insert(QStringLiteral("dup"), !it.dupOf.isEmpty());
        m.insert(QStringLiteral("dupInBatch"), it.dupOf.startsWith(QLatin1String("batch:")));
        m.insert(QStringLiteral("heic"), it.container == QLatin1String("heif"));
        const Value *cam = it.in.exif.find(QStringLiteral("camera"));
        m.insert(QStringLiteral("camera"), cam ? cam->toString() : QString());
        l << m;
    }
    return l;
}

void PhotoController::setItemPlan(int index, const QString &planId)
{
    if (index < 0 || index >= m_items.size()) return;
    Item &it = m_items[index];
    it.in.plan = planId;
    it.in.stop.clear();
    if (!planId.isEmpty()) {
        const Value *lat = it.in.exif.find(QStringLiteral("lat")), *lng = it.in.exif.find(QStringLiteral("lng"));
        it.in.stop = pl::guess(m_app->store()->data(), it.takenAt, lat ? lat->toNumber() : std::nan(""), lng ? lng->toNumber() : std::nan(""), planId).stop;
    }
    it.why.clear();
    emit importItemsChanged();
}

void PhotoController::setItemStop(int index, const QString &stopId)
{
    if (index < 0 || index >= m_items.size()) return;
    m_items[index].in.stop = stopId;
    emit importItemsChanged();
}

void PhotoController::setAllPlan(const QString &planId)
{
    QSignalBlocker b(this);
    for (int i = 0; i < m_items.size(); ++i) setItemPlan(i, planId);
    b.unblock();
    emit importItemsChanged();
}

void PhotoController::setItemSkip(int index, bool skip)
{
    if (index < 0 || index >= m_items.size()) return;
    m_items[index].in.skip = skip;
    emit importItemsChanged();
}

QVariantList PhotoController::planOptions() const
{
    // 0.8.7：和地图页同一个顺序（置顶 → 计划中 / 想去 / 已完成 → 路线在前 → 新的日期在前；没日期的按中文名），
    // 见 plans.h compare（Electron comparePlans）。只列有站点坐标的计划
    QVariantList out;
    const Value &root = m_app->store()->data();
    const json::Array &arr = root[QStringLiteral("plans")].asArray();
    for (int i : map::plans::sortedIndexes(root, true)) {
        const Value &p = arr[i];
        bool hasStop = false;
        for (const Value &s : p[QStringLiteral("stops")].asArray())
            if (s[QStringLiteral("lat")].isNumber() && s[QStringLiteral("lng")].isNumber()) { hasStop = true; break; }
        if (!hasStop) continue;
        const QString n = p[QStringLiteral("name")].toString();
        out << QVariantMap { { QStringLiteral("id"), p[QStringLiteral("id")].toString() }, { QStringLiteral("name"), n.isEmpty() ? tp::tr("未命名计划") : n },
                             { QStringLiteral("date"), p[QStringLiteral("startDate")].toString() } };
    }
    return out;
}

QVariantList PhotoController::stopOptions(const QString &planId) const
{
    QVariantList out;
    for (const Value &p : m_app->store()->data()[QStringLiteral("plans")].asArray()) {
        if (p[QStringLiteral("id")].toString() != planId) continue;
        int k = 0;
        for (const Value &s : p[QStringLiteral("stops")].asArray()) {
            if (!s[QStringLiteral("lat")].isNumber() || !s[QStringLiteral("lng")].isNumber()) continue;
            out << QVariantMap { { QStringLiteral("id"), s[QStringLiteral("id")].toString() },
                                 { QStringLiteral("name"), QStringLiteral("%1. %2").arg(++k).arg(s[QStringLiteral("name")].toString()) } };
        }
    }
    return out;
}

void PhotoController::confirmImport()
{
    if (m_stage != QLatin1String("review") || m_thread) return;
    if (m_app->store()->isReadOnly()) {
        m_items.clear();
        setStage(QString());
        emit importFinished(result(false, tp::tr("现在的数据是只读的，不能导入照片。")));
        return;
    }
    QList<pl::ImportItem> items;
    int total = 0;
    for (const Item &it : std::as_const(m_items)) { items << it.in; if (!it.in.skip) ++total; }
    m_done = 0;
    m_total = total;
    m_cancel = false;
    setStage(QStringLiteral("copying"));
    emit importProgress();
    const QString dir = dataDir();
    m_thread = QThread::create([this, items, dir] {
        const pl::ImportResult r = pl::copyIn(dir, items, QDateTime::currentDateTime(), [this](int done, int) {
            QMetaObject::invokeMethod(this, [this, done] { m_done = done; emit importProgress(); }, Qt::QueuedConnection);
            return !m_cancel.load();
        });
        QMetaObject::invokeMethod(this, [this, r] { copyDone(r); }, Qt::QueuedConnection);
    });
    m_thread->start();
}

void PhotoController::copyDone(const pl::ImportResult &rIn)
{
    if (m_thread) { m_thread->wait(); delete m_thread; m_thread = nullptr; }
    pl::ImportResult r = rIn;
    QVariantMap out;
    if (r.ok && m_app->store()->isReadOnly()) {   // 复制期间变成只读了（别的程序接管了锁）
        pl::rollback(r.copiedFiles);
        r.ok = false;
        r.error = tp::tr("现在的数据是只读的，不能导入照片。");
    }
    if (r.ok) {
        int toPlan = 0, inbox = 0;
        for (const Value &rec : r.records) (rec[QStringLiteral("plan")].toString().isEmpty() ? inbox : toPlan)++;
        const json::Array records = r.records;
        m_app->store()->modify([&records](Value &doc) { pl::addRecords(doc, records); });
        m_app->noteChanged(true);   // 有照片：立即保存（Electron 一样）
        qInfo("导入照片：%d 张（归到计划 %d 张，未归类 %d 张），跳过 %d 张", int(r.records.size()), toPlan, inbox, r.skipped);
        out = { { QStringLiteral("ok"), true }, { QStringLiteral("count"), int(r.records.size()) }, { QStringLiteral("toPlan"), toPlan },
                { QStringLiteral("inbox"), inbox }, { QStringLiteral("skipped"), r.skipped } };
        ++m_revision;
        emit libraryChanged();
    } else {
        if (r.cancelled) qInfo("导入照片：已取消（复制的文件已删掉）");
        else qWarning("导入照片失败：%s", qUtf8Printable(r.error));
        out = { { QStringLiteral("ok"), false }, { QStringLiteral("cancelled"), r.cancelled }, { QStringLiteral("error"), r.error } };
    }
    m_items.clear();
    emit importItemsChanged();
    setStage(QString());
    emit importFinished(out);
}

void PhotoController::cancelImport()
{
    if (m_thread) { m_cancel = true; return; }   // 后台停下后由 readDone / copyDone 收尾
    if (m_stage == QLatin1String("review")) {
        m_items.clear();
        emit importItemsChanged();
        setStage(QString());
    }
}

// ---------- 旋转 ----------

bool PhotoController::canRotate(const QString &file) const
{
    return file.startsWith(QLatin1String("photos/")) && pl::canRotate(dataDir() + u'/' + file);
}

QVariantMap PhotoController::rotate(const QString &photoId, int turnsCw)
{
    data::DataStore *store = m_app->store();
    if (store->isReadOnly()) return result(false, tp::tr("现在的数据是只读的，不能旋转照片。"));
    QString file;
    for (const Value &ph : store->data()[QStringLiteral("photos")].asArray())
        if (ph[QStringLiteral("id")].toString() == photoId) { file = ph[QStringLiteral("file")].toString(); break; }
    if (file.isEmpty()) return result(false, tp::tr("照片找不到"));
    const data::RotateResult r = data::rotatePhoto(dataDir(), file, turnsCw);
    if (!r.ok) {
        qWarning("旋转照片失败（%s）：%s", qUtf8Printable(file), qUtf8Printable(r.error));
        return result(false, r.error);
    }
    qInfo("旋转照片：%s（%s%s）", qUtf8Printable(file), r.lossless ? "无损" : "重新压缩", r.backedUp ? "，原图已备份到 .originals" : "");
    bool needSize = false;
    for (const Value &ph : store->data()[QStringLiteral("photos")].asArray())
        if (ph[QStringLiteral("id")].toString() == photoId)
            needSize = r.width > 0 && r.height > 0 && (ph[QStringLiteral("width")].toNumber() != r.width || ph[QStringLiteral("height")].toNumber() != r.height);
    if (needSize) {   // 转了 90° / 270°：记录里的尺寸跟着换（转 180° 不用改数据）
        store->modify([&](Value &doc) {
            for (Value &ph : doc.mutObject().find(QStringLiteral("photos"))->mutArray()) {
                if (ph[QStringLiteral("id")].toString() != photoId) continue;
                json::Object &o = ph.mutObject();
                if (o.contains(QStringLiteral("width"))) {   // 已有的键原位替换
                    o.set(QStringLiteral("width"), Value(r.width));
                    o.set(QStringLiteral("height"), Value(r.height));
                } else {                                     // 没有尺寸：加在 addedAt 前面（和导入时的顺序一样）
                    json::Object n;
                    for (const auto &kv : o.members()) {
                        if (kv.first == QLatin1String("addedAt")) { n.set(QStringLiteral("width"), Value(r.width)); n.set(QStringLiteral("height"), Value(r.height)); }
                        n.set(kv.first, kv.second);
                    }
                    if (!n.contains(QStringLiteral("width"))) { n.set(QStringLiteral("width"), Value(r.width)); n.set(QStringLiteral("height"), Value(r.height)); }
                    o = std::move(n);
                }
                break;
            }
        });
        m_app->noteChanged(true);
    }
    m_thumbVersion[photoId] += 1;
    ++m_revision;
    emit libraryChanged();
    return result(true);
}

// ---------- 清理 ----------

QVariantMap PhotoController::scanCleanup()
{
    data::DataStore *store = m_app->store();
    if (store->state() == data::DataStore::State::None || store->isReadOnly()) return result(false, tp::tr("现在的数据是只读的，不能清理照片。"));
    if (store->isDirty() || m_app->conflictPending()) {
        m_app->saveNow(false);
        if (store->isDirty()) return result(false, tp::tr("还有没保存的改动，先保存好再清理照片。"));
    }
    QFile f(store->dataFile());
    if (f.open(QIODevice::ReadOnly) && !store->knownHash().isEmpty() && data::DataStore::sha256Hex(f.readAll()) != store->knownHash())
        return result(false, tp::tr("数据文件在别处改过，先重新加载再清理照片。"));
    const QDateTime now = QDateTime::currentDateTime();
    const pl::CleanScan s = pl::scanUnused(dataDir(), pl::referencedFiles(store->data()), m_app->undoPhotoFiles(),
                                           pl::recentlyAddedFiles(store->data(), now.toMSecsSinceEpoch()), now);
    auto list = [this](const QStringList &rels) {
        QVariantList l;
        for (const QString &r : rels) l << QVariantMap { { QStringLiteral("rel"), r }, { QStringLiteral("size"), double(QFileInfo(dataDir() + u'/' + r).size()) } };
        return l;
    };
    QVariantMap out = result(true);
    out.insert(QStringLiteral("unused"), list(s.unused));
    out.insert(QStringLiteral("unusedBytes"), double(s.unusedBytes));
    out.insert(QStringLiteral("orphans"), list(s.orphanOriginals));
    out.insert(QStringLiteral("orphanBytes"), double(s.orphanBytes));
    out.insert(QStringLiteral("keptRecent"), int(s.keptRecent.size()));
    out.insert(QStringLiteral("keptUndo"), int(s.keptUndo.size()));
    qInfo("清理照片：扫描到没用的 %lld 个、没主人的原图 %lld 个；最近加的 %lld 个、撤销记录里的 %lld 个先不动",
          qlonglong(s.unused.size()), qlonglong(s.orphanOriginals.size()), qlonglong(s.keptRecent.size()), qlonglong(s.keptUndo.size()));
    return out;
}

QVariantMap PhotoController::trashFiles(const QStringList &rels)
{
    if (m_trashThread) return result(false, tp::tr("正在清理，请等一下。"));
    if (rels.isEmpty()) return result(false, tp::tr("没有选要清理的文件"));
    data::DataStore *store = m_app->store();
    if (store->isReadOnly()) return result(false, tp::tr("现在的数据是只读的，不能清理照片。"));
    if (store->isDirty()) return result(false, tp::tr("还有没保存的改动，先保存好再清理照片。"));
    // 第二道保护：移动前再核对一次数据文件（PO 2026-11-03）
    QFile f(store->dataFile());
    if (f.open(QIODevice::ReadOnly) && !store->knownHash().isEmpty() && data::DataStore::sha256Hex(f.readAll()) != store->knownHash())
        return result(false, tp::tr("数据文件在别处改过，先重新加载再清理照片。"));
    const QDateTime now = QDateTime::currentDateTime();
    const QSet<QString> ref = pl::referencedFiles(store->data());
    const QSet<QString> undo = m_app->undoPhotoFiles();
    const QSet<QString> recent = pl::recentlyAddedFiles(store->data(), now.toMSecsSinceEpoch());
    const QString dir = dataDir();
    m_trashDone = 0;
    m_trashTotal = int(rels.size());
    m_trashThread = QThread::create([this, rels, ref, undo, recent, dir, now] {
        pl::TrashResult all;
        int n = 0;
        for (const QString &rel : rels) {
            const pl::TrashResult r = pl::moveToTrash(dir, { rel }, ref, undo, recent, now);
            all.moved += r.moved;
            all.failed << r.failed;
            ++n;
            QMetaObject::invokeMethod(this, [this, n] { m_trashDone = n; emit trashChanged(); }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, all] {
            if (m_trashThread) { m_trashThread->wait(); delete m_trashThread; m_trashThread = nullptr; }
            qInfo("清理照片：移到回收站 %d 个，没移动 %lld 个", all.moved, qlonglong(all.failed.size()));
            for (const QString &e : all.failed) qInfo("  没移动：%s", qUtf8Printable(e));
            emit trashChanged();
            emit trashFinished({ { QStringLiteral("moved"), all.moved }, { QStringLiteral("failed"), all.failed } });
        }, Qt::QueuedConnection);
    });
    m_trashThread->start();
    emit trashChanged();
    return result(true);
}

// ---------- 缩略图缓存 ----------

void PhotoController::refreshCacheSize()
{
    if (m_cacheThread) return;
    m_cacheThread = QThread::create([this] {
        const qint64 n = m_cache->totalBytes();
        QMetaObject::invokeMethod(this, [this, n] {
            if (m_cacheThread) { m_cacheThread->wait(); delete m_cacheThread; m_cacheThread = nullptr; }
            m_cacheBytes = n;
            emit cacheChanged();
        }, Qt::QueuedConnection);
    });
    m_cacheThread->start();
}

void PhotoController::pruneCacheLater()
{
    if (m_cacheThread) return;
    m_cacheThread = QThread::create([this] {
        const int removed = m_cache->prune();
        const qint64 n = m_cache->totalBytes();
        if (removed > 0) qInfo("缩略图缓存超过上限，删掉了最旧的 %d 张", removed);
        QMetaObject::invokeMethod(this, [this, n] {
            if (m_cacheThread) { m_cacheThread->wait(); delete m_cacheThread; m_cacheThread = nullptr; }
            m_cacheBytes = n;
            emit cacheChanged();
        }, Qt::QueuedConnection);
    });
    m_cacheThread->start();
}

void PhotoController::clearThumbCache()
{
    const int n = m_cache->clear();
    qInfo("清空缩略图缓存：删掉 %d 张", n);
    m_epoch += 1000;   // 已经显示的缩略图换个地址重新请求
    ++m_revision;
    emit libraryChanged();
    refreshCacheSize();
}

} // namespace tp::app
