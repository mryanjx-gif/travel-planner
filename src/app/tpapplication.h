// 程序对象（0.5.1）：在 QGuiApplication 的基础上接住事件处理里抛出、没人接的 C++ 异常（std::exception 等），
// 交给 ErrorReporter 弹出错小框（错误码 TP-M-xxxx），程序继续运行。
// 说明：Qt 不保证异常穿过事件循环后状态完好，这里只是不让程序因此直接退出，并让用户知道出了问题。
// 0.5.2：MSVC 下 notify 外面再包一层 SEH（__try / __except），访问冲突这类结构化异常在回到 Windows 的窗口回调
// 之前就交给崩溃处理（CrashHandler::sehFilter）。0.5.1 实测时主线程的崩溃绕过了 SetUnhandledExceptionFilter。
#pragma once

#include <QGuiApplication>

namespace tp::app {

class TpApplication : public QGuiApplication
{
public:
    TpApplication(int &argc, char **argv) : QGuiApplication(argc, argv) {}
    bool notify(QObject *receiver, QEvent *event) override;

private:
    // 里层：接 C++ 异常（MSVC 不允许 __try 和需要析构的对象放在同一个函数里，所以分成两层）
    bool notifyCpp(QObject *receiver, QEvent *event);
};

} // namespace tp::app
