#include "windowstate.h"

#include "settings.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <algorithm>

namespace tp::app {

QRect fitToScreens(const QRect &saved, const QList<QRect> &screens, const QRect &primary, const QSize &minSize)
{
    QRect r = saved;
    if (r.width() < minSize.width()) r.setWidth(minSize.width());
    if (r.height() < minSize.height()) r.setHeight(minSize.height());
    // 标题栏那一条（顶上 40 像素）至少有 120×24 落在某个屏幕里，才算"看得见、抓得住"
    const QRect grip(r.x(), r.y(), r.width(), 40);
    const QRect *home = nullptr;
    for (const QRect &s : screens) {
        const QRect in = grip.intersected(s);
        if (in.width() >= 120 && in.height() >= 24) { home = &s; break; }
    }
    QRect target = home ? *home : primary;
    if (target.isEmpty()) return r;
    r.setWidth(std::min(r.width(), target.width()));
    r.setHeight(std::min(r.height(), target.height()));
    if (!home) {
        r.moveCenter(target.center());
    } else {
        // 在屏幕里但伸出去了：往回推
        if (r.right() > target.right()) r.moveRight(target.right());
        if (r.bottom() > target.bottom()) r.moveBottom(target.bottom());
        if (r.left() < target.left()) r.moveLeft(target.left());
        if (r.top() < target.top()) r.moveTop(target.top());
    }
    return r;
}

WindowKeeper::WindowKeeper(QQuickWindow *win, Settings *settings, QObject *parent)
    : QObject(parent), m_win(win), m_settings(settings)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &WindowKeeper::rememberNow);
    for (auto sig : { &QWindow::xChanged, &QWindow::yChanged, &QWindow::widthChanged, &QWindow::heightChanged })
        connect(win, sig, this, &WindowKeeper::schedule);
    connect(win, &QWindow::visibilityChanged, this, &WindowKeeper::schedule);
}

void WindowKeeper::schedule() { if (!m_suspended) m_timer.start(); }

void WindowKeeper::restoreAndShow()
{
    const QSize minSize(Settings::kMinWidth, Settings::kMinHeight);
    m_win->setMinimumSize(minSize);
    QList<QRect> screens;
    for (QScreen *s : QGuiApplication::screens()) screens << s->availableGeometry();
    const QRect primary = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect();
    if (m_settings->hasBounds()) {
        m_win->setGeometry(fitToScreens(m_settings->bounds(), screens, primary, minSize));
    } else {
        QRect r(0, 0, Settings::kDefaultWidth, Settings::kDefaultHeight);
        m_win->setGeometry(fitToScreens(r.translated(primary.center() - r.center()), screens, primary, minSize));
    }
    if (m_settings->maximized()) m_win->showMaximized();
    else {
        m_win->showNormal();
        // QWindow::geometry() 是不含标题栏的客户区：上面按客户区放进屏幕，客户区顶到屏幕最上面时标题栏就在屏幕外了
        // （0.8.2 PO 实测：看不到关闭 / 最小化按钮）。窗口显示后外框（frameMargins）才准，这时再挪一次
        keepTitleBarOnScreen();
        QTimer::singleShot(0, this, &WindowKeeper::keepTitleBarOnScreen);
    }
}

void WindowKeeper::keepTitleBarOnScreen()
{
    if (m_win->visibility() != QWindow::Windowed) return;
    QScreen *scr = m_win->screen();
    if (!scr) return;
    const QRect avail = scr->availableGeometry();
    const QMargins fm = m_win->frameMargins();
    QRect g = m_win->geometry();
    const QRect before = g;
    // 外框高度不超过可用区域
    const int maxH = avail.height() - fm.top() - fm.bottom();
    if (g.height() > maxH) g.setHeight(std::max(m_win->minimumHeight(), maxH));
    // 标题栏顶边不高于可用区域顶边；底边不超出可用区域
    if (g.top() - fm.top() < avail.top()) g.moveTop(avail.top() + fm.top());
    if (g.bottom() + fm.bottom() > avail.bottom()) g.moveBottom(avail.bottom() - fm.bottom());   // 高度上面已经限过，顶边不会因此伸出去
    if (g != before) {
        qInfo("窗口：标题栏伸到了屏幕外，挪回来（%d,%d %dx%d → %d,%d %dx%d，外框上 %d）", before.x(), before.y(), before.width(), before.height(),
              g.x(), g.y(), g.width(), g.height(), fm.top());
        m_win->setGeometry(g);
    }
}

void WindowKeeper::rememberNow()
{
    m_timer.stop();
    if (m_suspended) return;
    const QWindow::Visibility v = m_win->visibility();
    if (v == QWindow::Hidden || v == QWindow::Minimized || v == QWindow::FullScreen) return;   // 不记
    const bool max = v == QWindow::Maximized;
    if (!max) m_settings->setBounds(m_win->geometry());
    m_settings->setMaximized(max);
    m_settings->save();
}

} // namespace tp::app
