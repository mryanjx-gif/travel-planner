// 1.2.0 自检（分组 v12）：AI 面板 / 编辑页上的滚轮不传给地图、点开计划时按站点范围放大（不再写死 8 级）、
// AI 面板和编辑页之间的拖动条调宽度并记住。AI 省钱的部分在 selftest_ai.cpp（分组 ai）。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "jsonio.h"
#include "mapcontroller.h"
#include "planeditor.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>
#include <qpa/qwindowsysteminterface.h>

namespace tp::app::selftest {

namespace {
void wheelAt(QQuickWindow *win, QPointF p)
{
    QWheelEvent e(p, win->mapToGlobal(p), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QGuiApplication::sendEvent(win, &e);
}
void mouse(QQuickWindow *win, QEvent::Type t, QPointF p)
{
    // 和其他自检一样走系统事件（直接 sendEvent 的鼠标事件 MouseArea 收不到按下）
    const Qt::MouseButtons held = t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButton b = t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, devPx(win, p), devPx(win, win->mapToGlobal(p)), held, b, t);
}
} // namespace

void runV12(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("1.2.0 滚轮、点开计划的缩放、拖动条"));
    data::DataStore *store = app->store();
    const json::Value original = store->data();
    store->modify([](json::Value &d) {
        const json::Value fx = json::parse(QStringLiteral(R"([
          {"id":"p_v12city","name":"V12 悉尼市区","type":"route","status":"planning","stops":[
            {"id":"s_v1","name":"Circular Quay","lat":-33.8610,"lng":151.2108},{"id":"s_v2","name":"Darling Harbour","lat":-33.8748,"lng":151.2006}]},
          {"id":"p_v12one","name":"V12 单站","type":"single","status":"planning","stops":[{"id":"s_v3","name":"Bondi","lat":-33.8915,"lng":151.2767}]}])"));
        json::Array &ps = d.mutObject().find(QStringLiteral("plans"))->mutArray();
        for (const json::Value &p : fx.asArray()) ps.push_back(p);
    });
    app->commitStep();

    // ---- 点开计划：市区两站放大到街区级（以前最多 8 级），单站 12 级 ----
    const bool flew = waitFor([mapc] { return mapc->flyToPlan(QStringLiteral("p_v12city")); }, 4000);
    waitMapIdle(win, mapc, 4000);
    const double zCity = mapc->camera().zoom;
    r.check(flew && zCity > 11.5 && zCity <= 15.01, QStringLiteral("点开市区的计划：按站点范围放大（> 11.5 级，不再停在 8 级）"), QString::number(zCity, 'f', 2));
    mapc->flyToPlan(QStringLiteral("p_v12one"));
    waitMapIdle(win, mapc, 4000);
    r.check(std::abs(mapc->camera().zoom - 12) < 0.05, QStringLiteral("单站的计划：12 级"), QString::number(mapc->camera().zoom, 'f', 2));

    // ---- 滚轮：地图上滚会缩放；AI 面板上滚（列表到头）不传给地图 ----
    QQuickItem *panel = win->findChild<QQuickItem *>(QStringLiteral("aiPanel"));
    if (r.check(panel != nullptr, QStringLiteral("找到 AI 面板"))) {
        const double z0 = mapc->camera().zoom;
        wheelAt(win, QPointF(win->width() * 0.3, win->height() * 0.5));
        waitMapIdle(win, mapc, 3000);
        const double z1 = mapc->camera().zoom;
        panel->setProperty("open", true);
        waitFor([panel] { return panel->opacity() > 0.99; }, 3000);
        waitStill(panel);
        const QPointF c = panel->mapToScene(QPointF(panel->width() / 2, panel->height() / 2));
        wheelAt(win, c);
        wheelAt(win, c);
        waitMapIdle(win, mapc, 3000);
        const double z2 = mapc->camera().zoom;
        r.check(std::abs(z1 - z0) > 0.01 && std::abs(z2 - z1) < 0.001, QStringLiteral("AI 面板上的滚轮不传给地图（地图上照常缩放）"), QStringLiteral("%1 → %2 → %3").arg(z0).arg(z1).arg(z2));
        panel->setProperty("open", false);
    }

    // ---- 拖动条：编辑页开着 + AI 面板并排，拖 60 像素 → AI 面板宽 60、编辑页窄 60，记进 list.aiSplit ----
    PlanEditor *ed = editorCtl();
    QQuickItem *edItem = win->findChild<QQuickItem *>(QStringLiteral("planEditor"));
    if (ed && edItem && panel && win->width() >= 1100) {
        app->setListPref(QStringLiteral("aiSplit"), QStringLiteral("0"));
        win->setProperty("aiSplit", 0);
        ed->openPlan(QStringLiteral("p_v12city"));
        panel->setProperty("open", true);
        waitStill(edItem);
        waitStill(panel);
        QQuickItem *bar = win->findChild<QQuickItem *>(QStringLiteral("aiSplitter"));
        const double aw0 = panel->width(), ew0 = edItem->width();
        if (r.check(bar && bar->isVisible(), QStringLiteral("并排时有拖动条"))) {
            const QPointF p = bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2));
            mouse(win, QEvent::MouseButtonPress, p);
            mouse(win, QEvent::MouseMove, p + QPointF(5, 0));
            waitFrames(win, 2);
            mouse(win, QEvent::MouseMove, p + QPointF(60, 0));
            waitFrames(win, 2);
            mouse(win, QEvent::MouseButtonRelease, p + QPointF(60, 0));
            waitStill(panel);
            const double aw1 = panel->width(), ew1 = edItem->width();
            const QString saved = app->listPref(QStringLiteral("aiSplit"));
            r.check(std::abs((aw1 - aw0) - 60) < 2 && std::abs((ew0 - ew1) - 60) < 2 && saved.toDouble() > 0,
                    QStringLiteral("拖动条：AI 面板宽 60、编辑页窄 60，宽度记住"), QStringLiteral("AI %1→%2，编辑页 %3→%4，记的 %5").arg(aw0).arg(aw1).arg(ew0).arg(ew1).arg(saved));
        }
        panel->setProperty("open", false);
        ed->discard();
        app->setListPref(QStringLiteral("aiSplit"), QStringLiteral("0"));
        win->setProperty("aiSplit", 0);
    } else {
        r.info(QStringLiteral("窗口窄于 1100 或找不到编辑页，跳过拖动条"));
    }

    store->modify([&original](json::Value &d) { d = original; });
    app->commitStep();
}

} // namespace tp::app::selftest
