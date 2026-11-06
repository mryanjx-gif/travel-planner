#include "logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <cstdio>

namespace tp::app::Logger {
namespace {
QString g_dir;
QMutex g_mutex;
QtMessageHandler g_prev = nullptr;
int g_writes = 0;
FatalHook g_fatalHook = nullptr;
constexpr qint64 kMax = 256 * 1024;

void append(const QString &name, const QByteArray &line)
{
    QFile f(g_dir + QLatin1Char('/') + name);
    if (f.open(QIODevice::Append)) f.write(line);
}

void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const char *lv = "D";
    switch (type) {
    case QtDebugMsg: lv = "D"; break;
    case QtInfoMsg: lv = "I"; break;
    case QtWarningMsg: lv = "W"; break;
    case QtCriticalMsg: lv = "E"; break;
    case QtFatalMsg: lv = "F"; break;
    }
    const QByteArray line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")).toUtf8()
                            + ' ' + lv + ' ' + msg.toUtf8() + '\n';
    {
        QMutexLocker lock(&g_mutex);
        if (!g_dir.isEmpty()) {
            append(QStringLiteral("app.log"), line);
            if (type >= QtWarningMsg && type != QtInfoMsg) append(QStringLiteral("errors.log"), line);
            if (++g_writes % 200 == 0) {
                trimFile(g_dir + QStringLiteral("/app.log"), kMax);
                trimFile(g_dir + QStringLiteral("/errors.log"), kMax);
            }
        }
    }
    if (type == QtFatalMsg && g_fatalHook) g_fatalHook(msg);
    if (g_prev) g_prev(type, ctx, msg);
    else { std::fputs(line.constData(), stderr); std::fflush(stderr); }
}
} // namespace

bool trimFile(const QString &path, qint64 maxBytes)
{
    QFile f(path);
    if (f.size() <= maxBytes || !f.open(QIODevice::ReadOnly)) return false;
    QByteArray all = f.readAll();
    f.close();
    QByteArray tail = all.right(all.size() / 2);
    const int nl = tail.indexOf('\n');
    if (nl >= 0) tail.remove(0, nl + 1);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(tail);
    return true;
}

void install(const QString &logDir)
{
    g_dir = logDir;
    QDir().mkpath(g_dir);
    trimFile(g_dir + QStringLiteral("/app.log"), kMax);
    trimFile(g_dir + QStringLiteral("/errors.log"), kMax);
    g_prev = qInstallMessageHandler(handler);
}

QString dir() { return g_dir; }

void setFatalHook(FatalHook hook) { g_fatalHook = hook; }

void appendRaw(const QString &path, const QByteArray &bytes)
{
    QMutexLocker lock(&g_mutex);
    QFile f(path);
    if (f.open(QIODevice::Append)) f.write(bytes);
}

} // namespace tp::app::Logger
