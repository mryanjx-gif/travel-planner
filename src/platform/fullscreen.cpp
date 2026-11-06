#include "fullscreen.h"

#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace tp::platform {

bool Fullscreen::toggle(QWindow *win)
{
    if (!win) return m_active;
#ifdef Q_OS_WIN
    HWND h = reinterpret_cast<HWND>(win->winId());
    if (!m_active) {
        RECT r;
        GetWindowRect(h, &r);
        m_rect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
        m_wasMaximized = IsZoomed(h) != FALSE;
        m_style = GetWindowLongPtrW(h, GWL_STYLE);
        SetWindowLongPtrW(h, GWL_STYLE, LONG_PTR(m_style) & ~LONG_PTR(WS_CAPTION | WS_THICKFRAME));
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT &m = mi.rcMonitor;
        SetWindowPos(h, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        m_active = true;
    } else {
        SetWindowLongPtrW(h, GWL_STYLE, LONG_PTR(m_style));
        // 最大化的窗口：样式里还带着 WS_MAXIMIZE，放回进入前的外框就是最大化的样子，不经过普通大小
        SetWindowPos(h, nullptr, m_rect.x(), m_rect.y(), m_rect.width(), m_rect.height(),
                     SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        m_active = false;
    }
#else
    if (!m_active) {
        m_wasMaximized = win->visibility() == QWindow::Maximized;
        win->showFullScreen();
        m_active = true;
    } else {
        if (m_wasMaximized) win->showMaximized(); else win->showNormal();
        m_active = false;
    }
#endif
    return m_active;
}

} // namespace tp::platform
