// 托盘图标（W-06，0.4.0）：只在"收到托盘"时创建；单击 / 双击恢复窗口；右键菜单"打开 / 退出"；恢复后销毁（和 Electron 一样）。
// 直接用 Win32 Shell_NotifyIcon 写，不用 QSystemTrayIcon：后者要把程序改成 QApplication、多带一个约 6 MB 的 Qt6Widgets.dll。
// 其他系统（云端 Linux 测试）上 show() 返回 false，调用方改成最小化。
#pragma once

#include <QObject>
#include <QString>

namespace tp::platform {

class TrayIcon : public QObject
{
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon() override;

    // 显示托盘图标。iconPath = .ico 文件；openText / quitText = 右键菜单的两项（已翻译）。失败返回 false
    bool show(const QString &iconPath, const QString &tooltip, const QString &openText, const QString &quitText);
    void hide();
    bool isVisible() const { return m_visible; }

signals:
    void openRequested();   // 单击、双击、菜单"打开"
    void quitRequested();   // 菜单"退出"

#ifdef Q_OS_WIN
public:
    // 内部用（窗口过程）
    long long handleMessage(unsigned msg, unsigned long long wParam, long long lParam);
private:
    bool addIcon();
    void *m_hwnd = nullptr;
    void *m_icon = nullptr;
    unsigned m_taskbarCreated = 0;
#endif
private:
    bool m_visible = false;
    QString m_tooltip, m_openText, m_quitText;
};

} // namespace tp::platform
