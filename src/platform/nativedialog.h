// 系统对话框（0.5.1）：窗口画不出来（显卡出错）或主线程卡住时，QML 的框显示不了，只能用 Windows 自己的对话框。
// Windows 上用 TaskDialogIndirect（可以自定义按钮文字）。它需要通用控件 6.0：这里在运行时建一个"激活上下文"
// （缓存目录里写一个只声明 Common-Controls 6.0 依赖的 .manifest），不改程序本身的 manifest（Qt 6.12 自己会生成一份）。
// 拿不到 TaskDialogIndirect 时退回 MessageBox（按钮只能是 是 / 否 / 取消，正文里写明各对应什么）。
// 可以在任何线程调用（看门狗线程用）；不用 Qt 的界面类。其他系统上只写日志、返回 cancelIndex。
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

namespace tp::platform::NativeDialog {

struct Spec {
    QString title;            // 窗口标题
    QString heading;          // 大字
    QString content;          // 正文
    QStringList buttons;      // 从左到右
    int defaultIndex = 0;
    int cancelIndex = -1;     // 按 Esc / 右上角 × 时当作哪个按钮（-1 = 不能取消）
    bool warning = false;     // 图标：false = 错误，true = 警告
    // 对话框开着时每 0.25 秒问一次，返回 true 就当作按下了 autoIndex（主线程恢复响应时自动关掉"卡住"对话框）
    std::function<bool()> autoClose;
    int autoIndex = -1;
};

// 启动时在主线程调一次：准备激活上下文、预先加载 comctl32 6.0（卡住时再加载 DLL 可能会等加载锁）
void prepare(const QString &cacheDir);
// 显示并等待；返回按下的按钮序号
int show(const Spec &spec);
// 复制文字到剪贴板（任何线程都能用）
bool copyText(const QString &text);

} // namespace tp::platform::NativeDialog
