#include "photolib.h"

#include "electronimport.h"
#include "trdata.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <cmath>

namespace tp::data::photolib {

using json::Value;

namespace {

QString norm(QString p)
{
    p.replace(u'\\', u'/');
    while (p.startsWith(QLatin1String("./"))) p.remove(0, 2);
    return p.toLower();
}

// 照片库里的相对路径 → 对应 .originals 里的路径（photos/2026-11/a.jpg → photos/.originals/2026-11/a.jpg）
QString originalOf(const QString &rel) { return QStringLiteral("photos/.originals/") + rel.mid(7); }

bool skipName(const QString &name)
{
    // 隐藏文件、临时文件（旋转 / 导入时的 *.tmp-<毫秒>、*.tmp）
    return name.startsWith(u'.') || name.endsWith(QLatin1String(".tmp")) || name.contains(QLatin1String(".tmp-"))
           || name.compare(QLatin1String("Thumbs.db"), Qt::CaseInsensitive) == 0 || name.compare(QLatin1String("desktop.ini"), Qt::CaseInsensitive) == 0;
}

bool validLatLng(const Value &v) { return v.isNumber() && std::isfinite(v.toNumber()); }

bool stopValid(const Value &s) { return validLatLng(s[QStringLiteral("lat")]) && validLatLng(s[QStringLiteral("lng")]); }

QList<int> validStops(const Value &plan)
{
    QList<int> out;
    const json::Array &st = plan[QStringLiteral("stops")].asArray();
    for (size_t i = 0; i < st.size(); ++i)
        if (st[i].isObject() && stopValid(st[i])) out << int(i);
    return out;
}

} // namespace

bool isImportable(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const exif::Container c = exif::sniff(f.read(16));
    return c != exif::Container::Unknown;
}

bool canRotate(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const exif::Container c = exif::sniff(f.read(16));
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")) return c == exif::Container::Jpeg;
    if (ext == QLatin1String("png")) return c == exif::Container::Png;
    if (ext == QLatin1String("webp")) return c == exif::Container::Webp;
    return false;
}

QString newPhotoId(qint64 nowMs)
{
    static const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    QString r;
    for (int i = 0; i < 4; ++i) r += QLatin1Char(digits[QRandomGenerator::global()->bounded(36)]);
    return QStringLiteral("ph_") + QString::number(nowMs, 36) + r;
}

QString targetFile(const QString &dataDir, const QString &id, const QString &originalName, const QString &takenAt,
                   const QDateTime &now, QSet<QString> *taken)
{
    QString month = now.toString(QStringLiteral("yyyy-MM"));
    static const QRegularExpression ym(QStringLiteral("^(\\d{4})-(\\d{2})"));
    const QRegularExpressionMatch m = ym.match(takenAt);
    if (m.hasMatch()) month = m.captured(1) + u'-' + m.captured(2);
    const QString base = ElectronImport::safeName(originalName.isEmpty() ? QStringLiteral("photo") : originalName);
    const QString fname = base.startsWith(id + u'_') ? base : id + u'_' + base;
    const int dot = fname.lastIndexOf(u'.');
    const QString stem = dot > 0 ? fname.left(dot) : fname, ext = dot > 0 ? fname.mid(dot) : QString();
    QString rel = QStringLiteral("photos/%1/%2").arg(month, fname);
    for (int k = 2; (taken && taken->contains(rel.toLower())) || QFileInfo::exists(dataDir + u'/' + rel); ++k)
        rel = QStringLiteral("photos/%1/%2-%3%4").arg(month, stem).arg(k).arg(ext);
    if (taken) taken->insert(rel.toLower());
    return rel;
}

double distKm(double lat1, double lng1, double lat2, double lng2)
{
    constexpr double R = 6371.0, d2r = M_PI / 180.0;
    const double dLat = (lat2 - lat1) * d2r, dLng = (lng2 - lng1) * d2r;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
                     + std::cos(lat1 * d2r) * std::cos(lat2 * d2r) * std::sin(dLng / 2) * std::sin(dLng / 2);
    return 2 * R * std::asin(std::min(1.0, std::sqrt(a)));
}

Guess guess(const json::Value &doc, const QString &takenAt, double lat, double lng, const QString &forcePlan, const QString &forceStop)
{
    const QString day = takenAt.left(10);
    const bool hasGps = std::isfinite(lat) && std::isfinite(lng);
    const json::Array &plans = doc[QStringLiteral("plans")].asArray();

    // 最近的站：返回 (站点下标, 距离)
    auto nearest = [&](const Value &plan, int *idx, double *dist) {
        *idx = -1;
        *dist = 1e18;
        const json::Array &st = plan[QStringLiteral("stops")].asArray();
        for (int i : validStops(plan)) {
            const double d = distKm(lat, lng, st[size_t(i)][QStringLiteral("lat")].toNumber(), st[size_t(i)][QStringLiteral("lng")].toNumber());
            if (d < *dist) { *dist = d; *idx = i; }
        }
        return *idx >= 0;
    };
    auto stopId = [](const Value &plan, int i) { return plan[QStringLiteral("stops")].asArray()[size_t(i)][QStringLiteral("id")].toString(); };
    // 计划里挑站：日期在"到达–离开"之间的第一站 → GPS 最近的站 → 指定的站 / 第一站
    auto inPlan = [&](const Value &plan) -> QString {
        const QList<int> vs = validStops(plan);
        const json::Array &st = plan[QStringLiteral("stops")].asArray();
        if (!day.isEmpty()) {
            for (int i : vs) {
                const QString a = st[size_t(i)][QStringLiteral("arriveDate")].toString();
                QString d = st[size_t(i)][QStringLiteral("departDate")].toString();
                if (d.isEmpty()) d = a;
                if (!a.isEmpty() && a <= day && day <= d) return stopId(plan, i);
            }
        }
        if (hasGps) { int i; double dd; if (nearest(plan, &i, &dd)) return stopId(plan, i); }
        if (!forceStop.isEmpty()) return forceStop;
        return vs.isEmpty() ? QString() : stopId(plan, vs.first());
    };

    if (!forcePlan.isEmpty()) {
        for (const Value &p : plans) {
            if (p[QStringLiteral("id")].toString() != forcePlan) continue;
            if (!forceStop.isEmpty() && day.isEmpty() && !hasGps) return { forcePlan, forceStop, {} };
            return { forcePlan, inPlan(p), {} };
        }
    }
    if (!day.isEmpty()) {
        QList<const Value *> cands;
        for (const Value &p : plans) {
            const QString s = p[QStringLiteral("startDate")].toString();
            QString e = p[QStringLiteral("endDate")].toString();
            if (e.isEmpty()) e = s;
            if (!s.isEmpty() && s <= day && day <= e && !validStops(p).isEmpty()) cands << &p;
        }
        if (!cands.isEmpty()) {
            const Value *plan = cands.first();
            if (cands.size() > 1 && hasGps) {
                double best = 1e18;
                for (const Value *p : cands) { int i; double d; if (nearest(*p, &i, &d) && d < best) { best = d; plan = p; } }
            }
            return { (*plan)[QStringLiteral("id")].toString(), inPlan(*plan), QStringLiteral("date") };
        }
    }
    if (hasGps) {
        double best = 1e18;
        Guess g;
        for (const Value &p : plans) {
            int i; double d;
            if (nearest(p, &i, &d) && d <= kNearKm && d < best) { best = d; g = { p[QStringLiteral("id")].toString(), stopId(p, i), QStringLiteral("gps") }; }
        }
        if (!g.plan.isEmpty()) return g;
    }
    return {};
}

QByteArray fileSha1(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    if (!h.addData(&f)) return {};
    return h.result();
}

QString findDuplicate(const json::Value &doc, const QString &dataDir, const QString &srcPath, QHash<QString, QByteArray> *hashCache)
{
    const qint64 size = QFileInfo(srcPath).size();
    if (size <= 0) return {};
    QByteArray srcHash;
    for (const Value &ph : doc[QStringLiteral("photos")].asArray()) {
        const QString file = ph[QStringLiteral("file")].toString();
        if (!file.startsWith(QLatin1String("photos/"))) continue;
        const QString abs = dataDir + u'/' + file;
        const QFileInfo fi(abs);
        if (!fi.isFile() || fi.size() != size) continue;
        if (srcHash.isEmpty()) srcHash = fileSha1(srcPath);
        if (srcHash.isEmpty()) return {};
        QByteArray h = hashCache ? hashCache->value(abs) : QByteArray();
        if (h.isEmpty()) {
            h = fileSha1(abs);
            if (hashCache) hashCache->insert(abs, h);
        }
        if (h == srcHash) return ph[QStringLiteral("id")].toString();
    }
    return {};
}

ImportResult copyIn(const QString &dataDir, const QList<ImportItem> &items, const QDateTime &now,
                    const std::function<bool(int, int)> &progress)
{
    ImportResult r;
    const qint64 nowMs = now.toMSecsSinceEpoch();
    QSet<QString> taken, ids;
    int total = 0;
    for (const ImportItem &it : items) if (!it.skip) ++total;
    int done = 0;
    if (progress && !progress(0, total)) { r.cancelled = true; return r; }
    for (const ImportItem &it : items) {
        if (it.skip) { ++r.skipped; continue; }
        QString id;
        do { id = newPhotoId(nowMs); } while (ids.contains(id));
        ids.insert(id);
        const QString takenAt = it.exif.find(QStringLiteral("takenAt")) ? it.exif.find(QStringLiteral("takenAt"))->toString() : QString();
        const QString rel = targetFile(dataDir, id, it.name.isEmpty() ? QFileInfo(it.src).fileName() : it.name, takenAt, now, &taken);
        const QString dst = dataDir + u'/' + rel;
        const QString tmp = dst + QStringLiteral(".tmp-") + QString::number(nowMs);
        const qint64 size = QFileInfo(it.src).size();
        QString err;
        if (!QDir().mkpath(QFileInfo(dst).absolutePath())) err = trd("建不了文件夹 %1").arg(QFileInfo(dst).absolutePath());
        else if (QFile::exists(tmp) && !QFile::remove(tmp)) err = trd("临时文件删不掉");
        else if (!QFile::copy(it.src, tmp)) err = trd("复制失败：%1").arg(it.name);
        else if (QFileInfo(tmp).size() != size) { QFile::remove(tmp); err = trd("复制后大小不对：%1").arg(it.name); }
        else if (!QFile::rename(tmp, dst)) { QFile::remove(tmp); err = trd("改名失败：%1").arg(rel); }
        if (!err.isEmpty()) {
            rollback(r.copiedFiles);
            r.copiedFiles.clear();
            r.records.clear();
            r.error = err;
            return r;
        }
        // 复制出来的文件修改时间 = 原文件的（QFile::copy 在 Windows 上保留，Linux 上不保留）；
        // 清理时"最近 10 分钟"靠记录的 addedAt 判断，不靠文件时间
        r.copiedFiles << dst;
        json::Object o;
        o.set(QStringLiteral("id"), Value(id));
        o.set(QStringLiteral("file"), Value(rel));
        o.set(QStringLiteral("plan"), Value(it.plan));
        o.set(QStringLiteral("stop"), Value(it.plan.isEmpty() ? QString() : it.stop));
        o.set(QStringLiteral("caption"), Value(QString()));
        for (const auto &m : it.exif.members()) o.set(m.first, m.second);
        o.set(QStringLiteral("addedAt"), Value(nowMs));
        r.records.push_back(Value(std::move(o)));
        ++done;
        if (progress && !progress(done, total)) {
            rollback(r.copiedFiles);
            r.copiedFiles.clear();
            r.records.clear();
            r.cancelled = true;
            return r;
        }
    }
    r.ok = true;
    return r;
}

void rollback(const QStringList &copiedFiles)
{
    for (const QString &f : copiedFiles) QFile::remove(f);
}

void addRecords(json::Value &doc, const json::Array &records)
{
    json::Object &o = doc.mutObject();
    if (!o.find(QStringLiteral("photos")) || !o.find(QStringLiteral("photos"))->isArray()) o.set(QStringLiteral("photos"), Value::array());
    json::Array &arr = o.find(QStringLiteral("photos"))->mutArray();
    for (const Value &r : records) arr.push_back(r);
}

QSet<QString> referencedFiles(const json::Value &doc)
{
    QSet<QString> s;
    for (const Value &ph : doc[QStringLiteral("photos")].asArray()) {
        const QString f = ph[QStringLiteral("file")].toString();
        if (!f.isEmpty()) s.insert(norm(f));
    }
    return s;
}

QSet<QString> recentlyAddedFiles(const json::Value &doc, qint64 nowMs)
{
    QSet<QString> s;
    const qint64 limit = nowMs - qint64(kRecentMinutes) * 60000;
    for (const Value &ph : doc[QStringLiteral("photos")].asArray()) {
        const Value &a = ph[QStringLiteral("addedAt")];
        if (a.isNumber() && qint64(a.toNumber()) >= limit) s.insert(norm(ph[QStringLiteral("file")].toString()));
    }
    return s;
}

namespace {

enum class Verdict { Unused, Referenced, Recent, Undo };

Verdict judge(const QString &dataDir, const QString &rel, const QSet<QString> &referenced, const QSet<QString> &undoReferenced,
              const QSet<QString> &recentAdded, const QDateTime &now)
{
    // .originals 里的原图：按它对应的照片算
    const QString key = rel.startsWith(QLatin1String("photos/.originals/")) ? norm(QStringLiteral("photos/") + rel.mid(18)) : norm(rel);
    if (referenced.contains(key)) return Verdict::Referenced;
    if (undoReferenced.contains(key)) return Verdict::Undo;
    const QFileInfo fi(dataDir + u'/' + rel);
    const qint64 limit = qint64(kRecentMinutes) * 60;
    if (recentAdded.contains(key) || fi.lastModified().secsTo(now) < limit || (fi.birthTime().isValid() && fi.birthTime().secsTo(now) < limit))
        return Verdict::Recent;
    return Verdict::Unused;
}

} // namespace

CleanScan scanUnused(const QString &dataDir, const QSet<QString> &referenced, const QSet<QString> &undoReferenced,
                     const QSet<QString> &recentAdded, const QDateTime &now)
{
    CleanScan r;
    const QString root = dataDir + QStringLiteral("/photos");
    if (!QFileInfo(root).isDir()) return r;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString rel = QStringLiteral("photos/") + QDir(root).relativeFilePath(fi.absoluteFilePath());
        if (skipName(fi.fileName())) continue;
        // 跳过 .thumbs（Electron 的缩略图缓存，原生版不用）和其他点开头的文件夹，.originals 单独处理
        const bool inOriginals = rel.startsWith(QLatin1String("photos/.originals/"));
        bool hiddenDir = false;
        const QStringList parts = rel.split(u'/');
        for (int i = 1; i + 1 < parts.size(); ++i)
            if (parts[i].startsWith(u'.') && !(i == 1 && parts[i] == QLatin1String(".originals"))) hiddenDir = true;
        if (hiddenDir) continue;
        switch (judge(dataDir, rel, referenced, undoReferenced, recentAdded, now)) {
        case Verdict::Referenced: break;
        case Verdict::Recent: if (!inOriginals) r.keptRecent << rel; break;
        case Verdict::Undo: if (!inOriginals) r.keptUndo << rel; break;
        case Verdict::Unused:
            if (inOriginals) { r.orphanOriginals << rel; r.orphanBytes += fi.size(); }
            else { r.unused << rel; r.unusedBytes += fi.size(); }
            break;
        }
    }
    r.unused.sort();
    r.orphanOriginals.sort();
    r.keptRecent.sort();
    r.keptUndo.sort();
    return r;
}

TrashResult moveToTrash(const QString &dataDir, const QStringList &rels, const QSet<QString> &referenced,
                        const QSet<QString> &undoReferenced, const QSet<QString> &recentAdded, const QDateTime &now,
                        const std::function<bool(const QString &, QString *)> &trashFn)
{
    TrashResult r;
    for (const QString &rel : rels) {
        if (!rel.startsWith(QLatin1String("photos/")) || rel.contains(QLatin1String("/../")) || rel.contains(QLatin1String("\\"))) {
            r.failed << rel + trd("：路径不对");
            continue;
        }
        if (judge(dataDir, rel, referenced, undoReferenced, recentAdded, now) != Verdict::Unused) {
            r.failed << rel + trd("：又被用到了，没有动");
            continue;
        }
        const QString abs = dataDir + u'/' + rel;
        QString err;
        bool ok;
        if (trashFn) ok = trashFn(abs, &err);
        else {
            QFile f(abs);
            ok = f.moveToTrash();
            if (!ok) err = f.errorString();
        }
        if (ok) ++r.moved;
        else r.failed << rel + u'：' + (err.isEmpty() ? trd("移不到回收站") : err);
    }
    return r;
}

} // namespace tp::data::photolib
