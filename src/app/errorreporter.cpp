#include "errorreporter.h"
#include "errhash.h"
#include "logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>

namespace tp::app {
namespace {
QPointer<ErrorReporter> g_instance;

// errors.js 的 IGNORE 和 main.js 的 NET_ERR 合在一起（断网、服务器连不上这类不算 app 的错误）
const QRegularExpression &ignoreRe()
{
    static const QRegularExpression re(
        QStringLiteral("ResizeObserver loop|Failed to fetch|fetch failed|NetworkError|Load failed|network ?error|AbortError|aborted"
                       "|net::ERR_|ERR_INTERNET|ERR_NAME_NOT_RESOLVED|ERR_CONNECTION|ENOTFOUND|ECONNRESET|ECONNREFUSED"
                       "|ETIMEDOUT|EAI_AGAIN|^Script error\\.?$"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}
} // namespace

ErrorReporter::ErrorReporter(QObject *parent) : QObject(parent) { g_instance = this; }
ErrorReporter::~ErrorReporter() = default;
ErrorReporter *ErrorReporter::instance() { return g_instance.data(); }

void ErrorReporter::setSystem(const QString &s)
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_system == s) return;
        m_system = s;
    }
    emit systemChanged();
}

QString ErrorReporter::system() const
{
    QMutexLocker lock(&m_mutex);
    return m_system;
}

QString ErrorReporter::hash(const QString &s)
{
    char out[5];
    tp::errhash::hash4(reinterpret_cast<const char16_t *>(s.utf16()), std::size_t(s.size()), out);
    return QString::fromLatin1(out);
}

bool ErrorReporter::ignored(const QString &message)
{
    return message.trimmed().isEmpty() || ignoreRe().match(message).hasMatch();
}

QString ErrorReporter::cleanStack(const QString &stack)
{
    // errors.js cleanStack：前 10 行；file:/// 、http(s)://主机/、自定义协议（qrc:/、tripdata://local/）后面的路径只留文件名；
    // Windows 路径 D:\a\b\c.js → c.js。另外 Linux 路径 /a/b/c.cpp → c.cpp（main.js 的写法）
    static const QRegularExpression url(QStringLiteral("(?:file:///?|https?://[^/\\s]+/|qrc:/*|tripdata://local/)[^\\s)]*/([^/\\s)]+)"));
    static const QRegularExpression win(QStringLiteral("[A-Za-z]:[\\\\/][^\\s)]*[\\\\/]([^\\\\/\\s)]+)"));
    static const QRegularExpression nix(QStringLiteral("(?<![\\w.:])/[^\\s)]*/([^/\\s)]+)"));
    QStringList lines = stack.split(QLatin1Char('\n'));
    if (lines.size() > 10) lines = lines.mid(0, 10);
    for (QString &l : lines) {
        l.replace(url, QStringLiteral("\\1"));
        l.replace(win, QStringLiteral("\\1"));
        l.replace(nix, QStringLiteral("\\1"));
    }
    return lines.join(QLatin1Char('\n'));
}

QString ErrorReporter::codeOf(const QString &kind, const QString &message, const QString &stack)
{
    // errors.js codeOf：堆栈里第一行带 "xxx.js:行" 的位置，去掉开头的 "at "、去掉最后一个 ":数字"（列号）
    static const QRegularExpression hasPos(QStringLiteral("\\.(?:js|mjs|qml):\\d+"));
    static const QRegularExpression lead(QStringLiteral("^\\s*at\\s+"));
    static const QRegularExpression tail(QStringLiteral(":\\d+\\)?\\s*$"));
    QString at;
    for (const QString &l : cleanStack(stack).split(QLatin1Char('\n')))
        if (hasPos.match(l).hasMatch()) { at = l; break; }
    at.remove(lead).remove(tail);
    return QStringLiteral("TP-%1-%2").arg(kind, hash(message.section(QLatin1Char('\n'), 0, 0) + QLatin1Char('|') + at));
}

bool ErrorReporter::isJsException(const QString &description)
{
    // 没接住的 JS 异常在 QML 警告里是 "TypeError: …"、"ReferenceError: …"、"Error: …"（throw new Error）等。
    // "Unable to assign [undefined] to QString"、"Binding loop detected" 这类绑定警告不算（只写日志）
    static const QRegularExpression re(QStringLiteral("^(?:Uncaught\\s+)?(?:[A-Z][A-Za-z]*)?Error:\\s"));
    return re.match(description.trimmed()).hasMatch();
}

QString ErrorReporter::buildReport(const QString &code, const QString &message, const QString &stack) const
{
    QString page;
    QString sys;
    {
        QMutexLocker lock(&m_mutex);
        page = m_page;
        sys = m_system;
    }
    QStringList lines {
        QStringLiteral("旅行计划 错误报告 / Travel Planner error report"),
        QStringLiteral("错误码 Code: ") + code,
        QStringLiteral("版本 Version: ") + (m_version.isEmpty() ? QStringLiteral("?") : QStringLiteral("v") + m_version),
        QStringLiteral("系统 System: ") + sys,
        QStringLiteral("时间 Time: ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy/M/d HH:mm:ss")),
        QStringLiteral("页面 Page: ") + page,
        QStringLiteral("错误 Error: ") + message,
    };
    const QString st = cleanStack(stack);
    if (!st.trimmed().isEmpty()) lines << st;
    return lines.join(QLatin1Char('\n'));
}

QString ErrorReporter::report(const QString &kind, const QString &message, const QString &stack, const QString &at)
{
    if (ignored(message)) return QString();
    const QString code = at.isEmpty()
        ? codeOf(kind, message, stack)
        : QStringLiteral("TP-%1-%2").arg(kind, hash(message.section(QLatin1Char('\n'), 0, 0) + QLatin1Char('|') + at));
    const QString text = buildReport(code, message, stack);
    writeLog(code, text);
    qInfo("[TP-ERROR] %s %s", qUtf8Printable(code), qUtf8Printable(message.left(300)));
    bool show = false;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_shown.contains(code) && m_popups < kMaxPopups) {
            m_shown.insert(code);
            ++m_popups;
            show = true;
        }
    }
    // 排队发出：启动时（界面还没加载完）出的错也能在界面建好后显示；别的线程报的错也安全
    if (show)
        QMetaObject::invokeMethod(this, [this, code, message, text] { emit popup(code, message, text); }, Qt::QueuedConnection);
    return code;
}

QString ErrorReporter::logOnly(const QString &code, const QString &message)
{
    const QString text = buildReport(code, message, QString());
    writeLog(code, text);
    qWarning("[TP-ERROR] %s %s", qUtf8Printable(code), qUtf8Printable(message.left(300)));
    return text;
}

QString ErrorReporter::reportQml(const QString &description, const QString &url, int line, int column)
{
    QString file = QUrl(url).fileName();
    if (file.isEmpty()) file = url;
    const QString where = file.isEmpty() ? QString()
                                         : QStringLiteral("%1:%2:%3").arg(file).arg(qMax(line, 0)).arg(qMax(column, 0));
    if (!isJsException(description)) {
        qWarning("QML: %s%s", qUtf8Printable(where.isEmpty() ? QString() : where + QStringLiteral(": ")), qUtf8Printable(description));
        return QString();
    }
    return report(QStringLiteral("Q"), description.trimmed(), where.isEmpty() ? QString() : QStringLiteral("at ") + where);
}

void ErrorReporter::writeLog(const QString &code, const QString &report)
{
    // 和 Electron errLog 一样的格式：===== 时间 错误码 \n 报告 \n\n
    const QString path = !m_logFile.isEmpty() ? m_logFile
                         : Logger::dir().isEmpty() ? QString() : Logger::dir() + QStringLiteral("/errors.log");
    if (path.isEmpty()) return;
    const QByteArray block = "===== " + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + ' '
                             + code.toUtf8() + '\n' + report.toUtf8() + "\n\n";
    Logger::appendRaw(path, block);
}

} // namespace tp::app
