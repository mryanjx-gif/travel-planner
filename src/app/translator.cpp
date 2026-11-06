#include "translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace tp {

QString tr(const char *zh) { return QCoreApplication::translate("TP", zh); }

namespace app {

QHash<QString, QString> DictTranslator::parse(const QByteArray &bytes, bool native)
{
    QHash<QString, QString> out;
    QByteArray b = bytes;
    if (b.startsWith("\xEF\xBB\xBF")) b.remove(0, 3);
    for (QByteArray line : b.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        const int tab = line.indexOf('\t');
        if (tab <= 0) continue;
        QString zh = QString::fromUtf8(line.left(tab));
        QString en = QString::fromUtf8(line.mid(tab + 1));
        if (native) {
            if (zh.startsWith(QLatin1Char('#'))) continue;   // 注释
            for (QString *s : { &zh, &en }) s->replace(QLatin1String("\\n"), QLatin1String("\n")).replace(QLatin1String("\\t"), QLatin1String("\t"));
        }
        if (en.isEmpty()) continue;
        out.insert(zh, en);
    }
    return out;
}

int DictTranslator::loadDir(const QString &dir)
{
    QStringList electron;
    QString native;
    QDirIterator it(dir, { QStringLiteral("*.txt") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        if (QFileInfo(p).fileName() == QLatin1String("native.txt")) native = p;
        else electron << p;
    }
    // Electron build_dict.py 按文件名字典序合并（b1, b10, b11 … b19, b2, b20 …），后面的覆盖前面的
    std::sort(electron.begin(), electron.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).fileName() < QFileInfo(b).fileName();
    });
    const auto readAll = [](const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
    for (const QString &p : electron) {
        const auto m = parse(readAll(p), false);
        for (auto i = m.cbegin(); i != m.cend(); ++i) m_map.insert(i.key(), i.value());
    }
    if (!native.isEmpty()) {
        const auto m = parse(readAll(native), true);
        for (auto i = m.cbegin(); i != m.cend(); ++i) m_map.insert(i.key(), i.value());
    }
    return int(m_map.size());
}

QString DictTranslator::translate(const char *, const char *sourceText, const char *, int) const
{
    if (!sourceText) return {};
    const auto it = m_map.constFind(QString::fromUtf8(sourceText));
    return it == m_map.cend() ? QString() : it.value();
}

} // namespace app
} // namespace tp
