#include "crashinfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace tp::app::CrashInfo {

Last parse(const QByteArray &bytes)
{
    Last r;
    QString text = QString::fromUtf8(bytes);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const qsizetype blank = text.indexOf(QStringLiteral("\n\n"));
    const QString head = blank < 0 ? text : text.left(blank);
    r.report = blank < 0 ? QString() : text.mid(blank + 2).trimmed();
    for (const QString &line : head.split(QLatin1Char('\n'))) {
        const qsizetype c = line.indexOf(QLatin1Char(':'));
        if (c <= 0) continue;
        const QString k = line.left(c).trimmed();
        const QString v = line.mid(c + 1).trimmed();
        if (k == QLatin1String("code")) r.code = v;
        else if (k == QLatin1String("dump")) r.dump = v;
    }
    r.found = !r.code.isEmpty();
    return r;
}

Last takeLast(const QString &logDir)
{
    const QString path = logDir + QLatin1Char('/') + QLatin1String(kMarker);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = f.read(64 * 1024);
    f.close();
    QFile::remove(path);
    Last r = parse(bytes);
    if (!r.dump.isEmpty() && !QFileInfo::exists(logDir + QLatin1Char('/') + r.dump)) r.dump.clear();
    return r;
}

int pruneDumps(const QString &logDir, int keep)
{
    QDir d(logDir);
    // 文件名 crash-YYYYMMDD-HHMMSS.dmp：按名字倒序就是从新到旧
    const QStringList dumps = d.entryList({ QStringLiteral("crash-*.dmp") }, QDir::Files, QDir::Name | QDir::Reversed);
    int removed = 0;
    for (qsizetype i = keep; i < dumps.size(); ++i)
        if (d.remove(dumps[i])) ++removed;
    return removed;
}

} // namespace tp::app::CrashInfo
