#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace Paths {
namespace {
QString g_appDirOverride;

bool probeWritable(const QString &dir)
{
    if (!QDir().mkpath(dir)) return false;
    QFile probe(dir + QStringLiteral("/.write-test"));
    if (!probe.open(QIODevice::WriteOnly)) return false;
    probe.close();
    return probe.remove();
}

// %APPDATA%\TravelPlannerNative（Linux 测试环境是 ~/.local/share/TravelPlannerNative）
QString roamingDir()
{
#ifdef Q_OS_WIN
    const QString base = qEnvironmentVariable("APPDATA");
    if (!base.isEmpty()) return QDir::fromNativeSeparators(base) + QStringLiteral("/TravelPlannerNative");
#endif
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

// %LOCALAPPDATA%\TravelPlannerNative
QString localDir()
{
#ifdef Q_OS_WIN
    const QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (!base.isEmpty()) return QDir::fromNativeSeparators(base) + QStringLiteral("/TravelPlannerNative");
#endif
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}
} // namespace

void overrideAppDirForTest(const QString &dir) { g_appDirOverride = dir; }

QString appDir()
{
    return g_appDirOverride.isEmpty() ? QCoreApplication::applicationDirPath() : g_appDirOverride;
}

bool appDirWritable()
{
    if (!g_appDirOverride.isEmpty()) return probeWritable(g_appDirOverride);
    static const bool w = probeWritable(appDir());
    return w;
}

QString settingsFile()
{
    return (appDirWritable() ? appDir() : roamingDir()) + QStringLiteral("/settings.json");
}

QString defaultDataDir()
{
    return (appDirWritable() ? appDir() : roamingDir()) + QStringLiteral("/data");
}

QString cacheDir()
{
    static QString cached;
    if (!cached.isEmpty() && g_appDirOverride.isEmpty()) return cached;
    const QString local = appDir() + QStringLiteral("/cache");
    QString dir = probeWritable(local) ? local : localDir() + QStringLiteral("/cache");
    QDir().mkpath(dir);
    if (g_appDirOverride.isEmpty()) cached = dir;
    return dir;
}

QString logDir()
{
    const QString dir = localDir() + QStringLiteral("/logs");
    QDir().mkpath(dir);
    return dir;
}

QString electronSettingsFile()
{
    const QString env = qEnvironmentVariable("TP_ELECTRON_SETTINGS");   // 测试用
    if (!env.isEmpty()) return env;
#ifdef Q_OS_WIN
    const QString base = qEnvironmentVariable("APPDATA");
    if (!base.isEmpty()) return QDir::fromNativeSeparators(base) + QStringLiteral("/TravelPlanner/settings.json");
#endif
    return QDir::homePath() + QStringLiteral("/.config/TravelPlanner/settings.json");
}

} // namespace Paths
