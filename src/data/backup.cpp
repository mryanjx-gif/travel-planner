#include "backup.h"

#include "trdata.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace tp::data {

namespace {
QString clean(const QString &p) { return QDir::cleanPath(QFileInfo(p).absoluteFilePath()); }

Qt::CaseSensitivity pathCase()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}
}

QString Backup::folderName(const QDateTime &t)
{
    return QStringLiteral("旅行计划备份-") + t.toString(QStringLiteral("yyyyMMdd-HHmm"));
}

QString Backup::uniqueDir(const QString &root, const QString &name)
{
    const QDir r(root);
    QString dir = r.filePath(name);
    for (int k = 2; QFileInfo::exists(dir); ++k) dir = r.filePath(name + QStringLiteral("-%1").arg(k));
    return dir;
}

bool Backup::isInside(const QString &path, const QString &dataDir)
{
    if (path.isEmpty() || dataDir.isEmpty()) return false;
    const QString p = clean(path), d = clean(dataDir);
    if (p.compare(d, pathCase()) == 0) return true;
    const QString prefix = d.endsWith(u'/') ? d : d + u'/';
    return p.startsWith(prefix, pathCase());
}

QStringList Backup::collect(const QString &dataDir)
{
    QStringList out;
    const QDir d(dataDir);
    for (const char *f : { "travel.json", "travel.backup.json" })
        if (QFileInfo(d.filePath(QString::fromLatin1(f))).isFile()) out << QString::fromLatin1(f);
    const QString photos = d.filePath(QStringLiteral("photos"));
    if (!QFileInfo(photos).isDir()) return out;
    // 自己逐层走：遇到 .thumbs 整个跳过（Electron 的 filter 按名字排除，任何一层都算）
    QStringList stack { photos };
    QStringList files;
    while (!stack.isEmpty()) {
        const QString cur = stack.takeLast();
        const QFileInfoList ents = QDir(cur).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                                           QDir::Name);
        for (const QFileInfo &e : ents) {
            if (e.isDir() && !e.isSymLink()) {
                if (e.fileName() != QLatin1String(".thumbs")) stack << e.absoluteFilePath();
            } else if (e.isFile()) {
                files << d.relativeFilePath(e.absoluteFilePath());
            }
        }
    }
    files.sort();
    out << files;
    return out;
}

Backup::Result Backup::run(const QString &dataDir, const QString &destRoot, const QDateTime &now, const Progress &progress)
{
    Result r;
    if (dataDir.isEmpty()) { r.error = trd("数据文件夹未设置"); return r; }
    if (destRoot.isEmpty()) { r.error = trd("没有选备份文件夹"); return r; }
    if (isInside(destRoot, dataDir)) { r.error = trd("备份文件夹不能放在数据文件夹里面"); return r; }
    if (!QDir().mkpath(destRoot)) { r.error = trd("建不了备份文件夹：%1").arg(QDir::toNativeSeparators(destRoot)); return r; }

    const QStringList files = collect(dataDir);
    const QString dir = uniqueDir(destRoot, folderName(now));
    if (!QDir().mkpath(dir)) { r.error = trd("建不了备份文件夹：%1").arg(QDir::toNativeSeparators(dir)); return r; }
    r.dir = QDir::toNativeSeparators(dir);

    const auto abandon = [&dir]() { QDir(dir).removeRecursively(); };   // 只删这次新建的副本文件夹
    const int total = int(files.size());
    if (progress && !progress(0, total)) { abandon(); r.cancelled = true; return r; }
    const QDir src(dataDir), dst(dir);
    for (int k = 0; k < total; ++k) {
        const QString rel = files.at(k);
        const QString to = dst.filePath(rel);
        if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !QFile::copy(src.filePath(rel), to)) {
            // travel.backup.json 在列出之后被删了之类：静默跳过（和 Electron 一样）；其他文件复制失败就算备份失败
            if (rel == QLatin1String("travel.backup.json") && !QFileInfo::exists(src.filePath(rel))) continue;
            QFile in(src.filePath(rel));
            const QString why = in.exists() ? (in.open(QIODevice::ReadOnly) ? trd("写不进备份文件夹") : in.errorString())
                                            : trd("文件不见了");
            r.error = trd("复制 %1 失败：%2").arg(QDir::toNativeSeparators(rel), why);
            abandon();
            r.dir.clear();
            return r;
        }
        ++r.files;
        if (progress && !progress(k + 1, total)) { abandon(); r.cancelled = true; r.dir.clear(); return r; }
    }
    r.ok = true;
    return r;
}

} // namespace tp::data
