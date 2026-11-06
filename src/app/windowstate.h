// 记住窗口大小、位置、是否最大化（W-02，0.4.0）。和 Electron 一样：全屏、最小化、收到托盘时不记；默认 1400×900，最小 360×500。
// 原生版的改进：记住的位置不在任何屏幕上（比如拔了外接显示器）时，改到主屏幕居中；大小超过屏幕时缩到屏幕内。
// 位置变化后停 0.5 秒再写设置（Electron 每动一下写一次）。
#pragma once

#include <QList>
#include <QObject>
#include <QRect>
#include <QTimer>

class QQuickWindow;

namespace tp::app {

class Settings;

// 纯函数（测试用）：把记住的窗口位置放进现有屏幕里。screens = 各屏幕的可用区域（不含任务栏），primary 是主屏幕的
QRect fitToScreens(const QRect &saved, const QList<QRect> &screens, const QRect &primary, const QSize &minSize);

class WindowKeeper : public QObject
{
    Q_OBJECT
public:
    WindowKeeper(QQuickWindow *win, Settings *settings, QObject *parent = nullptr);
    // 按设置摆好窗口并显示（最大化的就最大化）
    void restoreAndShow();
    // 马上把当前位置写进设置（关闭、退出前调用）
    void rememberNow();
    void setSuspended(bool s) { m_suspended = s; if (s) m_timer.stop(); }   // F11 全屏期间不记

private:
    void schedule();
    // 显示以后按"含标题栏"的外框再检查一次：标题栏不能伸到屏幕可用区域外面（0.8.2）
    void keepTitleBarOnScreen();
    QQuickWindow *m_win;
    Settings *m_settings;
    QTimer m_timer;
    bool m_suspended = false;
};

} // namespace tp::app
