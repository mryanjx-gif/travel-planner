// F11 全屏（W-04，0.4.1 改）。
// 不用 Qt 的 FullScreen 窗口状态：Qt 在 Windows 上退出全屏时，先把窗口放回"普通大小"的位置、再最大化
// （qtbase 6.12 qwindowswindow.cpp setWindowState_sys），原来是最大化的窗口会先变小再变大（PO 实测 0.4.0）。
// 改成和浏览器一样的做法：窗口保持原来的状态（最大化就一直是最大化），只去掉标题栏和边框、铺满所在的显示器；
// 退出时把样式和位置原样放回。Qt 通过 WM_NCCALCSIZE 自动更新边框尺寸。
// 其他系统（云端 Linux 测试）上退回 Qt 的 showFullScreen。
#pragma once

#include <QRect>

class QWindow;

namespace tp::platform {

class Fullscreen
{
public:
    bool isActive() const { return m_active; }
    // 进入 / 退出；返回进入后是否处于全屏
    bool toggle(QWindow *win);
    void exit(QWindow *win) { if (m_active) toggle(win); }

private:
    bool m_active = false;
    bool m_wasMaximized = false;
    long long m_style = 0;
    QRect m_rect;      // 进入前的窗口外框（含边框，屏幕坐标）
};

} // namespace tp::platform
