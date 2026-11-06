// 崩溃处理（0.5.1，D-23）。Windows 上：
//   - SetUnhandledExceptionFilter 接住没处理的结构化异常（访问冲突、栈溢出等）；std::terminate、纯虚函数调用、
//     CRT 非法参数、abort 这几种绕过它的，在各自的处理函数里抛一个自定义异常，统一走同一个过滤器；
//     qFatal（Qt 在 Windows 上用 __fastfail，过滤器接不到）由日志的 fatal 钩子先调 onFatal。
//   - 崩溃那一刻只写三样东西（不另存数据，那一刻不可靠）：errors.log 里一段报告、logs\crash-时间.dmp（小转储）、
//     logs\last-crash.txt（"上次崩溃"记号，下次启动时由 src/app/crashinfo.cpp 读出、弹提示）。
//   - 写这些的是启动时就建好的一个线程（崩溃的线程栈可能已经用完）；dbghelp.dll 启动时就加载（Windows 自带）。
//   - 错误码 TP-CRASH-xxxx：异常码 + 第一个不是系统 / 运行库的位置（模块名 + 偏移）算哈希，同一个崩溃每次一样。
// 0.5.2（0.5.1 实测崩溃没接住：Windows 错误报告收到了 0xC0000005，我们的过滤器没运行）：
//   - TpApplication::notify 外面包一层 SEH，主线程事件处理里的崩溃在回到 Windows 的窗口回调之前就由 sehFilter 接住；
//   - 看门狗每秒调一次 checkFilter：过滤器被别的模块（显卡驱动、注入的图形钩子等）换掉了就记日志、换回来；
//   - status() 给启动时、开发版故意崩溃前写日志用。
// 其他系统上这些函数什么都不做（云端测试环境）。
#pragma once

#include <QString>

namespace tp::platform::CrashHandler {

// 启动时调一次（日志目录已建好之后）
void install(const QString &logDir, const QString &version, const QString &system);
// 显卡名等在窗口初始化后才知道：更新报告里的"系统"那一行
void updateSystem(const QString &system);
// qFatal：写报告后结束进程（Logger 的 fatal 钩子）
void onFatal(const QString &message);
// SEH 的过滤表达式（MSVC 的 __except(...) 里调，参数是 GetExceptionInformation()）：
// 调试器挂着、C++ 异常、不会让程序停下的异常码 → 返回 0（EXCEPTION_CONTINUE_SEARCH，交给别人）；
// 其他 → 和过滤器一样写报告并结束进程（不会返回）。规则见 src/app/crashcodes.h。
long sehFilter(void *exceptionPointers);
// 检查过滤器是不是还是我们的；被换掉了就换回来，返回换掉它的那个过滤器所在的模块名（"(空)" = 被设成了空），
// 没被换掉返回空字符串。看门狗线程每秒调一次。
QString checkFilter();
// 一行状态（dbghelp、写报告的线程、过滤器），写日志用
QString status();
// 开发版测试：故意制造一次访问冲突（在调用它的线程里）
[[noreturn]] void crashForTest();
// 开发版测试：新建一个后台线程，在那个线程里制造访问冲突（不经过 notify，只靠过滤器）
void crashOnBackgroundThreadForTest();

} // namespace tp::platform::CrashHandler
