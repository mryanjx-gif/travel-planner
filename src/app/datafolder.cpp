#include "datafolder.h"

#include "atomicfile.h"
#include "electronimport.h"
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

// 打包的 Electron 模板（assets/templates，资源 :/templates/）
static Value readTemplate(const QString &name)
{
    QFile f(QStringLiteral(":/templates/") + name);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("读不了模板 %s", qUtf8Printable(name));
        return {};
    }
    json::ParseError pe;
    const Value v = json::parse(QString::fromUtf8(f.readAll()), &pe);
    return pe.ok ? v : Value();
}

// 新数据（D-19）：季节表；sampleLang 不空时加一个示例计划。
// 模板是 Electron 的格式，示例计划走和导入同一个转换（站点去 order、去下一站合成 leg 等）。
static Value newDoc(const QString &appVersion, const QString &sampleLang)
{
    const QDateTime now = QDateTime::currentDateTime();
    Value doc = data::nativefmt::emptyDoc(appVersion, now);
    const Value season = readTemplate(QStringLiteral("season-guide.json"));
    if (season.isObject()) doc.mutObject().set(QStringLiteral("seasonGuide"), season);
    if (sampleLang.isEmpty()) return doc;
    const Value sample = readTemplate(sampleLang == QLatin1String("en") ? QStringLiteral("sample-plan-en.json") : QStringLiteral("sample-plan.json"));
    if (!sample.isObject()) return doc;
    json::Object electron;
    electron.set(QStringLiteral("version"), Value(4));
    electron.set(QStringLiteral("plans"), Value(json::Array{ sample }));
    const auto conv = data::ElectronImport::convert(Value(std::move(electron)), now, appVersion, [](const QString &) { return false; });
    doc.mutObject().set(QStringLiteral("plans"), conv.doc[QStringLiteral("plans")]);
    return doc;
}

bool createEmpty(const QString &dir, const QString &appVersion, QString *error, const QString &sampleLang)
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
        const QString path = QDir(dir).filePath(data::nativefmt::dataFileName());
        const auto w = platform::writeViaTemp(path, QDir(dir).filePath(data::nativefmt::tmpFileName()),
                                              data::nativefmt::serialize(newDoc(appVersion, sampleLang)));
        if (!w.ok) {
            if (error) *error = data::trd("写不了 travel.json：%1").arg(w.error);
            return false;
        }
    }
    QDir().mkpath(QDir(dir).filePath(QStringLiteral("photos")));
    return true;
}

} // namespace tp::app::DataFolder
