#include "watchdog.h"
#include "crashhandler.h"
#include "hangdetector.h"
#include "logger.h"
#include "nativedialog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QProcess>
#include <chrono>
#include <cstdlib>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace tp::platform {
namespace {
bool debuggerAttached()
{
#ifdef Q_OS_WIN
    return IsDebuggerPresent() != FALSE;
#else
    return false;
#endif
}
} // namespace

Watchdog::Watchdog(QObject *parent) : QObject(parent)
{
    m_beatTimer.setInterval(500);
    connect(&m_beatTimer, &QTimer::timeout, this, [this] { m_beat.fetch_add(1, std::memory_order_relaxed); });
}

Watchdog::~Watchdog() { stop(); }

void Watchdog::start(int limitSeconds, bool showDialog)
{
    if (m_thread.joinable()) return;
    m_stop = false;
    m_beatTimer.start();
    m_thread = std::thread([this, limitSeconds, showDialog] { run(limitSeconds, showDialog); });
}

void Watchdog::stop()
{
    m_stop = true;
    m_beatTimer.stop();
    // 对话框开着时线程停不下来：不等它（进程马上就退出了）
    if (m_thread.joinable()) m_thread.detach();
}

void Watchdog::setTexts(const Texts &t)
{
    std::lock_guard<std::mutex> lock(m_textMutex);
    m_texts = t;
}

void Watchdog::run(int limit, bool showDialog)
{
    tp::app::HangDetector det(limit, 60);
    while (!m_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (m_stop) break;
        checkCrashFilter();
        switch (det.step(m_beat.load(std::memory_order_relaxed), debuggerAttached())) {
        case tp::app::HangDetector::Hung:
            onHung(showDialog);
            break;
        case tp::app::HangDetector::HungQuiet:
            qWarning("TP-HANG：主线程又 %d 秒没有响应（刚恢复不久，不再弹框）", limit);
            break;
        case tp::app::HangDetector::Recovered:
            qWarning("TP-HANG：主线程恢复响应了");
            break;
        default:
            break;
        }
    }
}

void Watchdog::checkCrashFilter()
{
    const QString thief = CrashHandler::checkFilter();
    if (thief.isEmpty()) return;
    // 模块名 + 偏移；同一个模块只记一次（有的模块可能反复换）
    const QString module = thief.section(QStringLiteral("+0x"), 0, 0);
    if (m_filterThieves.contains(module)) return;
    m_filterThieves << module;
    qWarning("崩溃过滤器被换掉了：换成了 %s 里的过滤器，已换回我们的", qUtf8Printable(thief));
}

void Watchdog::onHung(bool showDialog)
{
    Texts t;
    {
        std::lock_guard<std::mutex> lock(m_textMutex);
        t = m_texts;
    }
    // errors.log（和 Electron errLog 一样的格式）；app.log 里也记一行
    const QString report = t.report.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy/M/d HH:mm:ss")));
    if (!m_logFile.isEmpty())
        tp::app::Logger::appendRaw(m_logFile, "===== " + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
                                                  + " TP-HANG\n" + report.toUtf8() + "\n\n");
    qWarning("TP-HANG：主线程没有响应，%s", showDialog ? "弹系统对话框" : "测试运行，不弹框");
    if (!showDialog || t.buttons.size() < 3) return;

    const std::uint64_t at = m_beat.load();
    NativeDialog::Spec spec;
    spec.title = t.title;
    spec.heading = t.heading;
    spec.content = t.content;
    spec.buttons = t.buttons;
    spec.defaultIndex = 1;
    spec.cancelIndex = 1;
    spec.warning = true;
    spec.autoIndex = 1;
    spec.autoClose = [this, at] { return m_beat.load() != at; };
    const int r = NativeDialog::show(spec);
    if (m_beat.load() != at && r == 1) {
        qWarning("TP-HANG：对话框开着时恢复了，已自动关掉");
        return;
    }
    if (r == 0) NativeDialog::copyText(report);
    if (r != 2) return;

    // 结束并重新打开：先启动新的一份（等这一份退出），再结束这一份
    QStringList args = QCoreApplication::arguments().mid(1);
    args.removeIf([](const QString &a) { return a.startsWith(QLatin1String("--wait-pid")); });
    args << QStringLiteral("--wait-pid=%1").arg(QCoreApplication::applicationPid());
    qWarning("TP-HANG：用户选了结束并重新打开");
    const bool ok = QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    if (!ok) qWarning("TP-HANG：没能启动新的一份");
#ifdef Q_OS_WIN
    TerminateProcess(GetCurrentProcess(), 6);
#else
    std::_Exit(6);
#endif
}

void waitForProcessExit(qint64 pid, int ms)
{
#ifdef Q_OS_WIN
    if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid))) {
        WaitForSingleObject(h, DWORD(ms));
        CloseHandle(h);
    }
#else
    Q_UNUSED(pid);
    Q_UNUSED(ms);
#endif
}

} // namespace tp::platform
