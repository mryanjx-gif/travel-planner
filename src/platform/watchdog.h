// 卡住检测（0.5.1，D-23）：主线程每 0.5 秒"心跳"一次，后台看门狗线程每秒看一次（计数逻辑见 src/app/hangdetector.h）。
// 连续 10 秒没有心跳 → 写 errors.log（TP-HANG），弹 Windows 系统对话框：
//   "复制错误信息，继续等" / "继续等" / "结束并重新打开"。主线程恢复时对话框自动关掉，日志记一笔"已恢复"。
// "结束并重新打开"：先用 --wait-pid=<旧进程号> 启动新的一份（新的一份等旧的退出后再做单实例检查），再结束这一份；
// 没保存的改动会丢（对话框里写明；自动保存停手 0.8 秒就保存，一般只差最后几秒）。
// 10 秒是 Claude 定的（Electron 用的是系统的 unresponsive 事件，时长没核实）。
// 调试器挂着时不检查；截图 / 内存测试时只写日志不弹框。
// 0.5.2：看门狗线程顺便每秒检查一次崩溃过滤器是不是被别的模块换掉了（CrashHandler::checkFilter），
// 换掉了就换回来，app.log 记一行"被谁换掉"（同一个模块只记一次）。卡住对话框开着时这个检查也暂停。
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <mutex>
#include <thread>

namespace tp::platform {

class Watchdog : public QObject
{
    Q_OBJECT
public:
    struct Texts {
        QString title, heading, content, report;
        QStringList buttons;   // 复制错误信息，继续等 / 继续等 / 结束并重新打开
    };

    explicit Watchdog(QObject *parent = nullptr);
    ~Watchdog() override;

    // limitSeconds：几秒没心跳算卡住；showDialog = false 时只写日志（截图、内存测试）
    void start(int limitSeconds, bool showDialog);
    void stop();
    // 文字由主线程准备好（翻译只在主线程做）；换语言后再调一次
    void setTexts(const Texts &t);
    // 报告用：errors.log 的路径
    void setLogFile(const QString &path) { m_logFile = path; }

private:
    void run(int limit, bool showDialog);
    void checkCrashFilter();
    void onHung(bool showDialog);

    QTimer m_beatTimer;
    std::atomic<std::uint64_t> m_beat { 0 };
    std::atomic<bool> m_stop { false };
    std::thread m_thread;
    std::mutex m_textMutex;
    Texts m_texts;
    QString m_logFile;
    QStringList m_filterThieves;   // 已经记过日志的"换掉过滤器"的模块（只在看门狗线程里用）
};

// 新进程启动时：等 pid 那个进程退出（最多 ms 毫秒）；"结束并重新打开"用
void waitForProcessExit(qint64 pid, int ms);

} // namespace tp::platform
