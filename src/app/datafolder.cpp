#include "datafolder.h"

#include "atomicfile.h"
#include "jsonio.h"
#include "nativeformat.h"
#include "trdata.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace tp::app::DataFolder {

using json::Value;

bool isValid(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo(QDir(dir).filePath(data::nativefmt::dataFileName())).isFile();
}

bool isElectron(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo(QDir(dir).filePath(data::nativefmt::electronDataFileName())).isFile();
}

bool samePath(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty()) return false;
    const QString ca = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(a).absoluteFilePath()));
    const QString cb = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(b).absoluteFilePath()));
#ifdef Q_OS_WIN
    return ca.compare(cb, Qt::CaseInsensitive) == 0;
#else
    return ca == cb;
#endif
}

Startup decide(const StartupInput &in)
{
    if (!in.savedDir.isEmpty()) {
        if (isValid(in.savedDir)) return Startup::UseSaved;
        return isElectron(in.savedDir) ? Startup::AskFirstRun : Startup::AskMissing;
    }
    if (isValid(in.defaultDir)) return Startup::UseDefault;
    if (isElectron(in.electronDir)) return Startup::AskFirstRun;
    return Startup::CreateDefault;
}

QString readElectronDataDir(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray bytes = f.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    json::ParseError pe;
    const Value v = json::parse(QString::fromUtf8(bytes), &pe);
    if (!pe.ok || !v.isObject()) return {};
    const QString dir = v[QStringLiteral("dataDir")].toString();
    return isElectron(dir) ? QDir::toNativeSeparators(dir) : QString();
}

bool createEmpty(const QString &dir, const QString &appVersion, QString *error)
{
    if (isElectron(dir) && !isValid(dir)) {
        if (error) *error = data::trd("这是 Electron 版的数据文件夹。原生版不在这里写东西，可以把它导入到原生版的数据文件夹。");
        return false;
    }
    if (!QDir().mkpath(dir)) {
        if (error) *error = data::trd("不能在这里建文件夹（可能没有写入权限）：%1").arg(QDir::toNativeSeparators(dir));
        return false;
    }
    if (!isValid(dir)) {
        // 示例计划和季节表模板在阶段 ⑥ 随欢迎页加
        const QString path = QDir(dir).filePath(data::nativefmt::dataFileName());
        const auto w = platform::writeViaTemp(path, QDir(dir).filePath(data::nativefmt::tmpFileName()),
                                              data::nativefmt::serialize(data::nativefmt::emptyDoc(appVersion, QDateTime::currentDateTime())));
        if (!w.ok) {
            if (error) *error = data::trd("写不了 travel.json：%1").arg(w.error);
            return false;
        }
    }
    QDir().mkpath(QDir(dir).filePath(QStringLiteral("photos")));
    return true;
}

} // namespace tp::app::DataFolder
