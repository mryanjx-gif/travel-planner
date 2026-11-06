// 自检：0.8.8 顶栏、⋯ 泡泡、头像菜单、我的旅程列表、Ctrl K、前进 / 后退、Esc 层级。
// 尽量走真的路径：快捷键用窗口系统的按键事件（和真的按键一样经过 Shortcut），鼠标侧键用鼠标事件；
// 界面上的东西按 objectName 找。会改数据副本里的"下次出发"，最后改回去。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "mapcontroller.h"
#include "navhistory.h"
#include "plancontroller.h"
#include "plans.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <qpa/qwindowsysteminterface.h>

namespace tp::app::selftest {

using json::Value;

namespace {
// 按 objectName 找界面元素：沿着显示树找（Repeater / ListView 建的行不是 QObject 子对象，findChild 找不到）
QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findItem(c, name)) return f;
    return nullptr;
}
QQuickItem *item(QQuickWindow *win, const QString &name) { return findItem(win->contentItem(), name); }
QQuickItem *item(QQuickWindow *win, const char *name) { return item(win, QString::fromUtf8(name)); }
bool shown(QQuickItem *it) { return it && it->isVisible() && it->opacity() > 0.5; }

// 按一个键（经过窗口系统，和真的按键一样；Shortcut 只在窗口有焦点时才收到，所以先让窗口拿到焦点）
bool g_focus = false;
void key(QQuickWindow *win, int k, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(win, QEvent::KeyPress, k, mods);
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(win, QEvent::KeyRelease, k, mods);
    pause(80);
}
void mouse(QQuickWindow *win, const QPointF &p, Qt::MouseButton b)
{
    const QPointF g = win->mapToGlobal(p);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, b, b, QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, b, QEvent::MouseButtonRelease);
    pause(80);
}
// 在某个界面元素中间点一下（经过窗口系统，和真的鼠标一样）
void click(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return;
    mouse(win, it->mapToScene(QPointF(it->width() / 2, it->height() / 2)), Qt::LeftButton);
}
// 0.8.10：点不中时写清楚点在哪（0.8.9 PO 电脑上右边的按钮点不中，Windows差异清单 #26）
QString whereStr(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return QStringLiteral("没找到按钮");
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    const QPoint g = win->mapToGlobal(p.toPoint());
    QQuickItem *top = win->contentItem()->childAt(p.x(), p.y());
    return QStringLiteral("点在窗口 %1,%2（屏幕上 %3,%4；窗口 %5,%6 %7×%8）最上面是 %9")
        .arg(p.x()).arg(p.y()).arg(g.x()).arg(g.y()).arg(win->x()).arg(win->y()).arg(win->width()).arg(win->height())
        .arg(top ? (top->objectName().isEmpty() ? QString::fromLatin1(top->metaObject()->className()) : top->objectName()) : QStringLiteral("（空）"));
}
// 鼠标侧键（Qt::BackButton / ForwardButton）：点在窗口正中（地图上）
void sideButton(QQuickWindow *win, Qt::MouseButton b) { mouse(win, QPointF(win->width() / 2.0, win->height() / 2.0), b); }
QVariant call(QObject *o, const char *fn, const QVariant &a = {}, const QVariant &b = {})
{
    QVariant ret;
    if (!b.isValid() && !a.isValid()) QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, ret));
    else if (!b.isValid()) QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a));
    else QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a), Q_ARG(QVariant, b));
    return ret;
}
} // namespace

void runShell(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("顶栏、我的旅程、Ctrl K、快捷键（0.8.8）"));
    PlanController *pc = plansCtl();
    QQmlContext *ctx = QQmlEngine::contextForObject(win) ? QQmlEngine::contextForObject(win) : nullptr;
    NavHistory *nav = ctx ? qobject_cast<NavHistory *>(ctx->contextProperty(QStringLiteral("nav")).value<QObject *>()) : nullptr;
    if (!r.check(pc && nav, QStringLiteral("计划控制器、页面记录在"))) return;
    data::DataStore *store = app->store();
    mapc->setBase(QStringLiteral("street"));
    mapc->markers()->clearSelection();
    win->setProperty("listOpen", false);
    win->setProperty("nowOpen", false);
    // 快捷键要窗口有焦点（你点到别的窗口时自检会改用直接调用，报告里写明）
    win->requestActivate();
    g_focus = waitFor([win] { return QGuiApplication::focusWindow() == win; }, 2000);
    r.info(g_focus ? QStringLiteral("窗口有焦点：快捷键按真的按键测") : QStringLiteral("窗口没拿到焦点：快捷键几项改用直接调用（不算失败）"));
    waitFrames(win, 2);

    // ---- 顶栏 ----
    {
        QQuickItem *dots = item(win, "dotsPill"), *listBtn = item(win, "listBtn"), *me = item(win, "meBtn");
        r.check(shown(dots) && shown(listBtn) && shown(me) && !item(win, "nextPill") && dots->y() + dots->height() <= win->property("topH").toDouble(),
                QStringLiteral("顶栏：⋯、列表按钮、头像都在顶栏里，原型条去掉了"));
        QQuickItem *save = item(win, "saveState");
        r.check(save && !save->isVisible(), QStringLiteral("保存状态：已经存好时不显示"));
        // 0.9.2 图层合并（PO 2026-11-06）：单独的地球 / 平面按钮去掉，地球在图层泡泡里
        QQuickItem *globeBtn = item(win, "globeBtn"), *layers = item(win, "layers"), *zoom = item(win, "zoomBtns");
        r.check(!globeBtn && shown(layers) && layers->y() >= win->property("topH").toDouble() && (!zoom || zoom->y() > layers->y()),
                QStringLiteral("右上一列：图层按钮在顶栏下面（没有单独的地球按钮），± 在它下面"));
    }

    // ---- ⋯ 泡泡 ----
    const QString oldNext = store->data()[QStringLiteral("nextTripId")].toString();
    const QVariantMap demo = pc->demo();
    {
        QObject *top = item(win, "topBar");
        QQuickItem *bubbles = item(win, "pageBubbles");
        if (!demo.isEmpty()) pc->setNextTrip(demo.value(QStringLiteral("planId")).toString(), true);
        waitFrames(win, 2);
        click(win, item(win, "dotsPill"));
        const bool opened = waitFor([bubbles] { return bubbles && bubbles->property("open").toBool(); }, 1000);
        // 0.8.10：8 颗——计划、想去、足迹、照片、日历、游记、账本、下次出发（PO 定的页面结构）
        QQuickItem *budNext = item(win, "bud-next");
        QQuickItem *lastBud = budNext ? budNext : item(win, "bud-ledger");   // 最后一颗出发得最晚
        waitFor([lastBud] { return lastBud && lastBud->opacity() > 0.95; }, 1500);
        int pages = 0, extra = 0;
        for (const char *k : { "bud-plan", "bud-wish", "bud-foot", "bud-photo", "bud-calendar", "bud-journal", "bud-ledger" }) if (shown(item(win, k))) ++pages;
        for (const char *k : { "bud-map", "bud-library", "bud-album", "bud-wishlist" }) if (item(win, k)) ++extra;   // 旧的页面名不该再有
        r.check(opened && pages == 7 && (demo.isEmpty() || shown(budNext)) && extra == 0,
                QStringLiteral("点 ⋯：分裂出 4 种模式、3 个整页和\"下次出发\""),
                QStringLiteral("页面 %1 / 7，下次出发 %2，多余 %3").arg(pages).arg(shown(budNext)).arg(extra));
        if (budNext && !demo.isEmpty()) {
            click(win, budNext);
            const bool nowShown = waitFor([win] { QQuickItem *n = item(win, "nowCard"); return n && n->isVisible(); }, 1500);
            r.check(nowShown && !bubbles->property("open").toBool(), QStringLiteral("点\"下次出发\"：泡泡收回，打开\"此刻\"卡片"));
            win->setProperty("nowOpen", false);
        }
        // Esc 收回 ⋯
        QMetaObject::invokeMethod(top, "setBubbles", Q_ARG(QVariant, true), Q_ARG(QVariant, QVariant()));
        waitFor([top] { return top->property("bubblesOpen").toBool(); }, 1000);
        if (g_focus) key(win, Qt::Key_Escape); else call(win, "escOnce");
        r.check(!bubbles->property("open").toBool(), QStringLiteral("Esc 收回 ⋯ 泡泡"));
    }

    // ---- 头像菜单 ----
    {
        QQuickItem *menu = item(win, "avatarMenu");
        click(win, item(win, "meBtn"));
        const bool open = waitFor([menu] { return menu && menu->property("open").toBool(); }, 800);
        const bool items = item(win, "menu-settings") && item(win, "menu-folder") && item(win, "menu-title") && item(win, "menu-quit");
        r.check(open && items, QStringLiteral("头像菜单：设置、更换数据文件夹、修改窗口标题、退出"), open ? QString() : whereStr(win, item(win, "meBtn")));
        click(win, item(win, "menu-settings"));
        r.check(waitFor([win] { return win->property("settingsOpen").toBool(); }, 800) && !menu->property("open").toBool() && nav->page() == QLatin1String("settings"),
                QStringLiteral("点\"设置\"：菜单收起、打开设置"));
        QQuickItem *back = item(win, "backBtn");
        r.check(shown(back), QStringLiteral("不在地图页时顶栏出现 ↩"));
        click(win, back);
        r.check(waitFor([nav] { return nav->page() == QLatin1String("plan"); }, 800), QStringLiteral("点 ↩ 回到地图"));
    }

    // ---- 前进 / 后退（S-13） ----
    {
        nav->go(QStringLiteral("settings"));
        waitFrames(win, 1);
        if (g_focus) key(win, Qt::Key_Left, Qt::AltModifier); else nav->back();
        const bool back = nav->page() == QLatin1String("plan");
        if (g_focus) key(win, Qt::Key_Right, Qt::AltModifier); else nav->forward();
        const bool fwd = nav->page() == QLatin1String("settings");
        r.check(back && fwd, QStringLiteral("Alt ← / Alt →：后退、前进"));
        sideButton(win, Qt::BackButton);
        const bool sb = nav->page() == QLatin1String("plan");
        sideButton(win, Qt::ForwardButton);
        const bool sf = nav->page() == QLatin1String("settings");
        r.check(sb && sf, QStringLiteral("鼠标侧键：后退、前进"));
        if (g_focus) key(win, Qt::Key_Escape); else call(win, "escOnce");
        r.check(nav->page() == QLatin1String("plan"), QStringLiteral("设置开着时 Esc 回到地图"));
    }

    // ---- 我的旅程（S-08） ----
    QString routeId;
    {
        const QVariantMap rm = mapc->markers()->demo(QStringLiteral("route"));
        routeId = rm.value(QStringLiteral("planId")).toString();
        click(win, item(win, "listBtn"));
        QQuickItem *list = item(win, "planList");
        const bool open = waitFor([list] { return list && list->isVisible() && list->opacity() > 0.95; }, 1000);
        int matches = 0;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray()) if (pc->filter().matches(p)) ++matches;
        const QVariantList rows = pc->list().value(QStringLiteral("rows")).toList();
        r.check(open && rows.size() == matches, QStringLiteral("顶栏列表按钮：打开我的旅程，列出筛选下的全部计划"),
                QStringLiteral("%1 行，筛选下 %2 个").arg(rows.size()).arg(matches) + (open ? QString() : QStringLiteral("；") + whereStr(win, item(win, "listBtn"))));
        QQuickItem *pill = item(win, "statusPill");
        r.check(pill && list && pill->x() >= list->x() + list->width(), QStringLiteral("列表开着时左下状态胶囊挪到列表右边"));
        // 点一行 = 选中、飞过去
        if (!routeId.isEmpty()) {
            QQuickItem *row = item(win, QStringLiteral("planRow-") + routeId);
            if (!row) {
                // 不在可见范围：滚到它
                pc->openPlan(routeId, false);
                waitFrames(win, 2);
                mapc->markers()->clearSelection();
                row = item(win, QStringLiteral("planRow-") + routeId);
            }
            if (row) click(win, row);
            else pc->openPlan(routeId, true);
            r.check(waitFor([mapc, routeId] { return mapc->markers()->selectedPlan() == routeId; }, 1000), QStringLiteral("点列表里的一行：选中这个计划"),
                    row ? QString() : QStringLiteral("那一行不在可见范围，改用 openPlan"));
        }
        // 眼睛
        const QString hid = rows.isEmpty() ? QString() : rows.last().toMap().value(QStringLiteral("id")).toString();
        if (!hid.isEmpty()) {
            QQuickItem *eye = item(win, QStringLiteral("eye-") + hid);
            if (eye && eye->isVisible()) click(win, eye);
            else pc->setHidden(hid, true);
            pause(200);
            bool onMap = false;
            for (const map::TripPlan &p : mapc->trips().plans()) if (p.id == hid) onMap = true;
            bool dim = false;
            for (const QVariant &v : pc->list().value(QStringLiteral("rows")).toList())
                if (v.toMap().value(QStringLiteral("id")).toString() == hid) dim = v.toMap().value(QStringLiteral("hidden")).toBool();
            r.check(pc->filter().hidden.contains(hid) && !onMap && dim, QStringLiteral("眼睛：在地图上隐藏，列表里还在（变淡）"));
            pc->setHidden(hid, false);
        }
        // 列表头的筛选：打开同一个下拉，挂在按钮下面
        QQuickItem *fb = item(win, "listFilterBtn"), *fm = item(win, "filterMenu");
        click(win, fb);
        const bool fOpen = waitFor([fm] { return fm && fm->property("open").toBool(); }, 800);
        r.check(fOpen && fb && fm->y() > fb->mapToScene(QPointF(0, 0)).y(), QStringLiteral("列表头的\"筛选\"：打开筛选下拉（在按钮下面）"));
        // Ctrl B 关掉列表（筛选下拉跟着收起）
        if (g_focus) key(win, Qt::Key_B, Qt::ControlModifier); else QMetaObject::invokeMethod(win, "toggleList");
        r.check(waitFor([list] { return !list->isVisible(); }, 1000) && !fm->property("open").toBool(), QStringLiteral("Ctrl B：关掉我的旅程，筛选下拉跟着收起"));
    }

    // ---- Ctrl K（S-12） ----
    {
        QQuickItem *pal = item(win, "palette");
        mapc->markers()->clearSelection();
        if (g_focus) key(win, Qt::Key_K, Qt::ControlModifier); else call(win, "openPalette", QString());
        const bool open = waitFor([pal] { return pal && pal->property("open").toBool(); }, 800);
        QVariantList res = pal ? pal->property("results").toList() : QVariantList();
        r.check(open && !res.isEmpty() && res.first().toMap().value(QStringLiteral("group")).toString() == QLatin1String("recent")
                && (routeId.isEmpty() || res.first().toMap().value(QStringLiteral("id")).toString() == routeId),
                QStringLiteral("Ctrl K：打开命令面板，没输入时第一组是\"最近\"（刚才选中的计划在最前）"),
                res.isEmpty() ? QStringLiteral("没有结果") : QStringLiteral("第一项 %1 / %2").arg(res.first().toMap().value("group").toString(), res.first().toMap().value("label").toString()));
        // 输入一个计划名的开头 → 第一项就是它；Enter 打开
        QString name, id;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray()) {
            const QString n = p[QStringLiteral("name")].toString();
            if (n.size() >= 2 && pc->filter().matches(p) && p[QStringLiteral("stops")].asArray().size() > 0) { name = n; id = p[QStringLiteral("id")].toString(); break; }
        }
        if (!name.isEmpty()) {
            QMetaObject::invokeMethod(pal, "show", Q_ARG(QVariant, name));
            waitFrames(win, 1);
            res = pal->property("results").toList();
            const QVariantMap first = res.isEmpty() ? QVariantMap() : res.first().toMap();
            r.check(first.value(QStringLiteral("label")).toString().startsWith(name) && first.value(QStringLiteral("score")).toInt() == 0,
                    QStringLiteral("输入计划名：开头匹配的排第一"), QStringLiteral("搜\"%1\" → %2").arg(name, first.value(QStringLiteral("label")).toString()));
            if (g_focus) key(win, Qt::Key_Return); else QMetaObject::invokeMethod(pal, "runAt", Q_ARG(QVariant, 0));
            const QString pick = first.value(QStringLiteral("id")).toString();
            r.check(waitFor([mapc, pick] { return mapc->markers()->selectedPlan() == pick; }, 1500) && !pal->property("open").toBool(),
                    QStringLiteral("Enter：关掉面板，选中这个计划（打开简易面板）"));
        }
        // 没找到
        QMetaObject::invokeMethod(pal, "show", Q_ARG(QVariant, QStringLiteral("zzqqxx没有这个")));
        pause(80);
        r.check(pal->property("results").toList().isEmpty(), QStringLiteral("搜不到时没有结果（显示\"没有找到\"）"));
        if (g_focus) key(win, Qt::Key_Escape); else call(win, "escOnce");
        r.check(!pal->property("open").toBool(), QStringLiteral("Esc 关掉命令面板"));
    }

    // ---- Esc 层级（S-14） ----
    {
        if (!routeId.isEmpty()) pc->openPlan(routeId, false);
        win->setProperty("listOpen", true);
        win->setProperty("filterFrom", QStringLiteral("list"));
        waitStill(item(win, "planList"), 1500);
        if (QQuickItem *fm = item(win, "filterMenu")) fm->setProperty("open", true);
        if (QQuickItem *lay = item(win, "layers")) QMetaObject::invokeMethod(lay, "setOpen", Q_ARG(QVariant, true));
        if (QQuickItem *menu = item(win, "avatarMenu")) menu->setProperty("open", true);
        if (QQuickItem *pal = item(win, "palette")) QMetaObject::invokeMethod(pal, "show", Q_ARG(QVariant, QString()));
        waitFrames(win, 2);
        QStringList seq;
        for (int i = 0; i < 9; ++i) {
            const QString n = call(win, "escOnce").toString();
            if (n.isEmpty()) break;
            seq << n;
            pause(80);
        }
        const QStringList want = routeId.isEmpty() ? QStringList{ QStringLiteral("palette"), QStringLiteral("menu"), QStringLiteral("layers"), QStringLiteral("filter"), QStringLiteral("list") }
                                                   : QStringList{ QStringLiteral("palette"), QStringLiteral("menu"), QStringLiteral("layers"), QStringLiteral("filter"), QStringLiteral("sheet"), QStringLiteral("list") };
        r.check(seq == want, QStringLiteral("Esc 一层一层关：命令面板 → 头像菜单 → 图层泡泡 → 筛选下拉 → 简易面板 → 我的旅程"),
                seq.join(QStringLiteral(" → ")));
    }

    // 收尾：下次出发改回去
    if (!demo.isEmpty()) {
        if (oldNext.isEmpty()) pc->setNextTrip(demo.value(QStringLiteral("planId")).toString(), false);
        else pc->setNextTrip(oldNext, true);
        waitFor([app] { return !app->dirty(); }, 3000);
    }
    win->setProperty("listOpen", false);
    mapc->markers()->clearSelection();
}

} // namespace tp::app::selftest
