// 自检：0.8.8 顶栏、⋯ 泡泡、头像菜单、我的旅程列表、Ctrl K、前进 / 后退、Esc 层级。
// 尽量走真的路径：快捷键用窗口系统的按键事件（和真的按键一样经过 Shortcut），鼠标侧键用鼠标事件；
// 界面上的东西按 objectName 找。会改数据副本里的"下次出发"，最后改回去。
#include "settings.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "mapcontroller.h"
#include "navhistory.h"
#include "plancontroller.h"
#include "plans.h"
#include "translator.h"

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
    waitStill(it, 1000);   // 0.12.11：面板滑进、列表滚动时先等停稳再点，不然点在半路上
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
        // 0.11.6：横条 5 颗——计划 ▾、照片 ▾、日历、游记、账本；想去、足迹、计划库、相册、下次出发在竖列里（点\"计划\"展开）
        QQuickItem *budNext = item(win, "bud-next");
        QQuickItem *lastBud = item(win, "bud-ledger");   // 最后一颗出发得最晚
        waitFor([lastBud] { return lastBud && lastBud->opacity() > 0.95; }, 1500);
        int pages = 0, extra = 0;
        for (const char *k : { "bud-plan", "bud-photo", "bud-calendar", "bud-journal", "bud-ledger" }) if (shown(item(win, k))) ++pages;
        for (const char *k : { "bud-wish", "bud-foot", "bud-library", "bud-album", "bud-next", "bud-map", "bud-wishlist" }) if (shown(item(win, k))) ++extra;   // 竖列没开时不该看到
        r.check(opened && pages == 5 && extra == 0,
                QStringLiteral("点 ⋯：横条分裂出 计划、照片、日历、游记、账本 5 颗，竖列里的不出现"),
                QStringLiteral("页面 %1 / 5，多余 %2").arg(pages).arg(extra));
        if (budNext && !demo.isEmpty()) {
            pause(700);   // 和 ⋯ 同一处，隔开 0.7 秒（#26）
            click(win, item(win, "bud-plan"));   // 已在计划这一组：展开竖列
            waitFor([budNext] { return budNext->opacity() > 0.95; }, 1500);
            waitStill(budNext, 1500);
            click(win, budNext);
            const bool nowShown = waitFor([win] { QQuickItem *n = item(win, "nowCard"); return n && n->isVisible(); }, 1500);
            r.check(nowShown && !bubbles->property("open").toBool(), QStringLiteral("点\"下次出发\"：泡泡收回，打开\"此刻\"卡片"));
            win->setProperty("nowOpen", false);
        }
        // Esc 收回 ⋯
        QMetaObject::invokeMethod(top, "setBubbles", Q_ARG(QVariant, true), Q_ARG(QVariant, QVariant()));
        waitFor([top] { return top->property("bubblesOpen").toBool(); }, 1000);
        if (ensureFocus(win)) key(win, Qt::Key_Escape); else call(win, "escOnce");
        r.check(!bubbles->property("open").toBool(), QStringLiteral("Esc 收回 ⋯ 泡泡"));
    }

    // ---- 0.11.6 ⋯ 的竖列（计划 ▾ / 照片 ▾ 的点法；全用鼠标真点，同一处连点隔开 0.7 秒，Windows差异清单 #26） ----
    {
        QObject *top = item(win, "topBar");
        auto bar = [&] { return item(win, "pageBubbles")->property("open").toBool(); };
        auto sub = [&] { return top->property("subIsOpen").toBool(); };
        auto page = [&](const char *p) { return nav->page() == QLatin1String(p); };
        auto openBar = [&] {
            click(win, item(win, "dotsPill"));
            waitFor([&] { QQuickItem *b = item(win, "bud-ledger"); return bar() && b && b->opacity() > 0.95; }, 1500);
            waitStill(item(win, "bud-ledger"), 1500);
        };
        auto clickBud = [&](const char *k, bool needSub) {
            QQuickItem *b = item(win, k);
            if (needSub) waitFor([b] { return b && b->opacity() > 0.95; }, 1500);
            waitStill(b, 1500);
            pause(700);
            click(win, b);
        };
        // 规则 1：在日历里点"计划" = 去计划地图，竖列不开，横条收起
        nav->go(QStringLiteral("calendar"));
        waitFor([&] { return page("calendar"); }, 1000);
        openBar();
        clickBud("bud-plan", false);
        r.check(waitFor([&] { return page("plan") && !bar(); }, 1500) && !sub(), QStringLiteral("竖列规则 1：日历里点\"计划\"：去计划地图，竖列不盖在整页上，横条收起"));
        // 规则 2：在想去里点"计划" = 展开竖列，模式不变，想去那一项高亮
        nav->go(QStringLiteral("wish"));
        waitFor([&] { return page("wish") && pc->mode() == QLatin1String("wish"); }, 1500);
        openBar();
        clickBud("bud-plan", false);
        const bool opened = waitFor([&] { return sub(); }, 1500);
        QQuickItem *wishBud = item(win, "bud-wish");
        waitFor([wishBud] { return wishBud && wishBud->opacity() > 0.95; }, 1500);
        r.check(opened && bar() && page("wish") && pc->mode() == QLatin1String("wish") && wishBud && wishBud->property("on").toBool(),
                QStringLiteral("竖列规则 2：在想去里点\"计划\"：展开竖列、模式不变、\"想去\"高亮"));
        // 规则 3：竖列开着再点"计划" = 回计划地图，竖列和横条收起
        clickBud("bud-plan", false);
        r.check(waitFor([&] { return page("plan") && !sub() && !bar(); }, 1500) && pc->mode() == QLatin1String("plan"),
                QStringLiteral("竖列规则 3：竖列开着再点\"计划\"：回计划地图，收起"));
        // 规则 4：选竖列里的足迹 / 计划库
        openBar();
        clickBud("bud-plan", false);
        waitFor([&] { return sub(); }, 1500);
        clickBud("bud-foot", true);
        r.check(waitFor([&] { return page("foot") && !sub() && !bar(); }, 1500) && pc->mode() == QLatin1String("foot"),
                QStringLiteral("竖列规则 4：选\"足迹\"：去足迹，横条和竖列都收起"));
        openBar();
        clickBud("bud-plan", false);
        waitFor([&] { return sub(); }, 1500);
        clickBud("bud-library", true);
        r.check(waitFor([&] { return page("plan") && pc->libraryOpen() && !sub() && !bar(); }, 2000),
                QStringLiteral("竖列规则 4：选\"计划库\"：计划库打开，横条和竖列都收起"));
        QQuickItem *lbl = item(win, "dotsLabel");
        r.check(shown(lbl) && lbl->property("text").toString() == tp::tr("计划库"), QStringLiteral("⋯ 上显示\"计划库\""));
        // Esc：先收竖列，再收横条，页面不动
        openBar();
        clickBud("bud-plan", false);
        waitFor([&] { return sub(); }, 1500);
        call(win, "escOnce");
        const bool e1 = !sub() && bar() && page("plan") && pc->libraryOpen();
        call(win, "escOnce");
        r.check(e1 && !bar() && page("plan") && pc->libraryOpen(), QStringLiteral("Esc：先收竖列，再按一次收横条，地图和计划库不动"));
        // 规则 3 的计划库：竖列开着再点"计划" 关掉计划库
        openBar();
        clickBud("bud-plan", false);
        waitFor([&] { return sub(); }, 1500);
        clickBud("bud-plan", false);
        r.check(waitFor([&] { return !pc->libraryOpen() && !sub() && !bar(); }, 1500), QStringLiteral("竖列开着再点\"计划\"：计划库也关掉"));
        // 照片这一组：不在组里点"照片" = 去照片地图；再点展开；选"相册"
        openBar();
        clickBud("bud-photo", false);
        r.check(waitFor([&] { return page("photo") && !bar() && !sub(); }, 1500), QStringLiteral("点\"照片\"（不在这一组）：去照片地图，横条收起"));
        openBar();
        clickBud("bud-photo", false);
        waitFor([&] { return sub(); }, 1500);
        clickBud("bud-album", true);
        r.check(waitFor([&] { return page("photo") && win->property("albumOpen").toBool() && !sub() && !bar(); }, 2000),
                QStringLiteral("照片的竖列：选\"相册\"：相册铺满，横条和竖列收起"));
        lbl = item(win, "dotsLabel");
        r.check(shown(lbl) && lbl->property("text").toString() == tp::tr("相册"), QStringLiteral("⋯ 上显示\"相册\""));
        win->setProperty("albumOpen", false);
        nav->go(QStringLiteral("plan"));
        waitFor([&] { return page("plan"); }, 1000);
        // 窄窗口：头像菜单里的页面入口还能到想去
        const int w0 = win->width();
        win->setWidth(640);
        waitFor([win] { return win->width() == 640; }, 2000); waitFrames(win, 2);
        pause(700);
        click(win, item(win, "meBtn"));
        QQuickItem *wishItem = nullptr;
        waitFor([&] { wishItem = item(win, "menu-page:wish"); return wishItem && wishItem->isVisible(); }, 1500);
        const bool hasLib = item(win, "menu-page:library") && item(win, "menu-page:album");
        click(win, wishItem);
        r.check(hasLib && waitFor([&] { return page("wish"); }, 1500), QStringLiteral("窄窗口：头像菜单的页面入口能到想去（还有计划库、相册）"));
        win->setWidth(w0);
        waitFor([win, w0] { return win->width() == w0; }, 2000); waitFrames(win, 2);
        nav->go(QStringLiteral("plan"));
        waitFor([&] { return page("plan"); }, 1000);
        win->setProperty("listOpen", false);   // 计划库、相册进来时开了列表：关掉，别影响后面的列表检查
        waitFrames(win, 2);
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
        if (ensureFocus(win)) key(win, Qt::Key_Left, Qt::AltModifier); else nav->back();
        const bool back = nav->page() == QLatin1String("plan");
        if (ensureFocus(win)) key(win, Qt::Key_Right, Qt::AltModifier); else nav->forward();
        const bool fwd = nav->page() == QLatin1String("settings");
        r.check(back && fwd, QStringLiteral("Alt ← / Alt →：后退、前进"));
        sideButton(win, Qt::BackButton);
        const bool sb = nav->page() == QLatin1String("plan");
        sideButton(win, Qt::ForwardButton);
        const bool sf = nav->page() == QLatin1String("settings");
        r.check(sb && sf, QStringLiteral("鼠标侧键：后退、前进"));
        if (ensureFocus(win)) key(win, Qt::Key_Escape); else call(win, "escOnce");
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
        // 胶囊挪过去有动画：等它到位（0.12.11 点之前等停稳后，读的时候动画还没走完）
        r.check(waitFor([&] { return pill && list && pill->x() >= list->x() + list->width(); }, 1000), QStringLiteral("列表开着时左下状态胶囊挪到列表右边"),
                pill && list ? QStringLiteral("%1 / %2").arg(pill->x()).arg(list->x() + list->width()) : QString());
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
        if (ensureFocus(win)) key(win, Qt::Key_B, Qt::ControlModifier); else QMetaObject::invokeMethod(win, "toggleList");
        r.check(waitFor([list] { return !list->isVisible(); }, 1000) && !fm->property("open").toBool(), QStringLiteral("Ctrl B：关掉我的旅程，筛选下拉跟着收起"));
    }

    // ---- Ctrl K（S-12） ----
    {
        QQuickItem *pal = item(win, "palette");
        mapc->markers()->clearSelection();
        if (ensureFocus(win)) key(win, Qt::Key_K, Qt::ControlModifier); else call(win, "openPalette", QString());
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
            if (ensureFocus(win)) key(win, Qt::Key_Return); else QMetaObject::invokeMethod(pal, "runAt", Q_ARG(QVariant, 0));
            const QString pick = first.value(QStringLiteral("id")).toString();
            r.check(waitFor([mapc, pick] { return mapc->markers()->selectedPlan() == pick; }, 1500) && !pal->property("open").toBool(),
                    QStringLiteral("Enter：关掉面板，选中这个计划（打开简易面板）"));
        }
        // 没找到
        QMetaObject::invokeMethod(pal, "show", Q_ARG(QVariant, QStringLiteral("zzqqxx没有这个")));
        pause(80);
        r.check(pal->property("results").toList().isEmpty(), QStringLiteral("搜不到时没有结果（显示\"没有找到\"）"));
        if (ensureFocus(win)) key(win, Qt::Key_Escape); else call(win, "escOnce");
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

    // ---- 0.12.3 占位入口、设置改版 ----
    r.section(QStringLiteral("占位入口、设置改版（0.12.3）"));
    {
        QQuickItem *t = item(win, "toast");
        const auto isStub = [t] {
            const QString m = t ? t->property("message").toString() : QString();
            return t && t->property("showing").toBool() && (m == QStringLiteral("这个功能还在做") || m == QLatin1String("This feature isn't ready yet"));
        };
        const auto stub = [&](QQuickItem *it, const QString &what) {
            if (t) t->setProperty("showing", false);
            click(win, it);
            const bool ok = waitFor(isStub, 800);
            r.check(ok, what, ok ? QString() : whereStr(win, it));
            pause(300);
        };
        nav->go(QStringLiteral("plan"));
        waitFor([win] { return shown(item(win, "aiBtn")); }, 1500);
        click(win, item(win, "aiBtn"));   // 0.13.0 起是真的面板
        r.check(waitFor([win] { QQuickItem *p = item(win, "aiPanel"); return p && p->property("open").toBool(); }, 800), QStringLiteral("AI 泡泡打开 AI 面板"));
        click(win, item(win, "aiBtn"));
        waitFor([win] { QQuickItem *p = item(win, "aiPanel"); return p && !p->isVisible(); }, 1500);
        click(win, item(win, "meBtn"));
        waitFor([win] { QQuickItem *m = item(win, "avatarMenu"); return m && m->property("open").toBool(); }, 800);
        pause(250);
        stub(item(win, "menu-importTrip"), QStringLiteral("头像菜单的导入 .trip"));

        int stubs = 0;
        for (const QVariant &v : call(win, "paletteItems").toList())
            if (v.toMap().value(QStringLiteral("key")) == QLatin1String("notReady")) ++stubs;
        if (t) t->setProperty("showing", false);
        call(win, "runCommand", QVariantMap{ { QStringLiteral("kind"), QStringLiteral("act") }, { QStringLiteral("key"), QStringLiteral("notReady") } });
        r.check(stubs == 8 && waitFor(isStub, 800), QStringLiteral("Ctrl K 搜得到 8 个占位入口（AI 助手 0.13.0 接上了），选了提示还在做"), QString::number(stubs));

        call(win, "pickPage", QStringLiteral("library"));
        waitFor([win] { return shown(item(win, "libImportTrip")); }, 2000);
        pause(400);
        stub(item(win, "libImportTrip"), QStringLiteral("计划库顶部的导入 .trip"));
        click(win, item(win, "libSelect"));
        waitFor([win] { return shown(item(win, "libSelExport")); }, 1000);
        click(win, item(win, "libSelAll"));
        pause(200);
        stub(item(win, "libSelExport"), QStringLiteral("计划库多选后的批量导出"));
        click(win, item(win, "libSelect"));
        for (int i = 0; i < 9 && !call(win, "escOnce").toString().isEmpty(); ++i) pause(80);

        nav->go(QStringLiteral("settings"));
        QQuickItem *panel = item(win, "settingsPanel");
        waitFor([win] { return win->property("settingsOpen").toBool(); }, 800);
        pause(500);
        r.check(panel && !panel->property("narrow").toBool() && panel->width() > 600, QStringLiteral("设置面板放宽到约 760，有侧边栏"), panel ? QString::number(panel->width()) : QString());
        QStringList badNav;
        for (const char *k : { "me", "map", "cal", "win", "money", "docs", "ai", "net", "data", "keys", "about", "look" }) {
            click(win, item(win, QString(QStringLiteral("setNav-") + QLatin1String(k))));
            if (!waitFor([&] { return panel->property("section").toString() == QLatin1String(k); }, 500)) badNav << QString::fromLatin1(k);
            pause(120);
        }
        r.check(badNav.isEmpty() && shown(item(win, "setCard-look")), QStringLiteral("侧边栏 12 组都点得到"), badNav.join(QLatin1Char(' ')));

        click(win, item(win, "setNav-ai"));   // 0.13.0：AI 分组的功能在自检 ai 分组里测，这里只看在不在
        r.check(waitFor([win] { return shown(item(win, "aiProvider")) && shown(item(win, "aiKeySave")) && shown(item(win, "aiCapInput")); }, 800), QStringLiteral("AI 分组有服务商、钥匙、月上限"));

        // 新设置项写进 settings.json 并读回；字体真生效；头像第一个字
        const QString oFont = app->uiFont(), oName = app->greetName(), oHome = app->homeCountry(), oBase = app->startBase(), oProj = app->startProj();
        QQmlContext *rootCtx = QQmlEngine::contextForObject(win);
        app->setUiFont(QStringLiteral("kaiti"));
        app->setGreetName(QStringLiteral("小明") + QString(30, QLatin1Char('x')));
        app->setHomeCountry(QStringLiteral("JPN"));
        app->setStartBase(QStringLiteral("街道图"));
        app->setStartProj(QStringLiteral("mercator"));
        Settings back;
        back.load(settingsObj()->path());
        r.check(back.value(QStringLiteral("font")).toString() == QLatin1String("kaiti") && back.value(QStringLiteral("greetName")).toString().size() == 20
                    && back.value(QStringLiteral("homeCountry")).toString() == QLatin1String("JPN") && back.value(QStringLiteral("startBase")).toString() == QStringLiteral("街道图")
                    && back.value(QStringLiteral("startProj")).toString() == QLatin1String("mercator"),
                QStringLiteral("字体、名字（截到 20 字）、国家、打开时的底图和形状写进 settings.json 并读回"));
        waitFrames(win, 2);
        QQuickItem *gs = item(win, "glassStatus");
        const QString fam = rootCtx ? rootCtx->contextProperty(QStringLiteral("serifFamily")).toString() : QString();
        r.check(fam == QLatin1String("KaiTi") && gs && gs->property("font").value<QFont>().family() == fam, QStringLiteral("选楷体后界面文字真的换了字体"), fam);
        QQuickItem *ini = item(win, "meInitial");
        r.check(ini && ini->isVisible() && ini->property("text").toString() == QStringLiteral("小"), QStringLiteral("头像显示名字第一个字"));
        app->setUiFont(oFont); app->setGreetName(oName); app->setHomeCountry(oHome); app->setStartBase(oBase); app->setStartProj(oProj);
        waitFrames(win, 2);
        r.check(!item(win, "meInitial")->isVisible() || !oName.isEmpty(), QStringLiteral("没名字时还是人像图标"));

        // 等宽数字：实测（每种字体 1111 和 0000 一样宽 = 生效）
        for (const QString &f : { QStringLiteral("Noto Serif SC"), QStringLiteral("Noto Serif CJK SC"), QStringLiteral("Microsoft YaHei UI"), QStringLiteral("DengXian"), QStringLiteral("KaiTi") }) {
            QFont a(f); a.setPixelSize(40);
            QFont b = a; b.setFeature(QFont::Tag("tnum"), 1); b.setFeature(QFont::Tag("hwid"), 1);
            const QFontMetricsF ma(a), mb(b);
            r.info(QStringLiteral("等宽数字实测 %1%2：普通 1111=%3 0000=%4；tnum + hwid 1111=%5 0000=%6").arg(f, QFontDatabase::hasFamily(f) ? QString() : QStringLiteral("（这台电脑没有）"))
                       .arg(ma.horizontalAdvance(QStringLiteral("1111"))).arg(ma.horizontalAdvance(QStringLiteral("0000")))
                       .arg(mb.horizontalAdvance(QStringLiteral("1111"))).arg(mb.horizontalAdvance(QStringLiteral("0000"))));
        }

        {
            QFont b(rootCtx ? rootCtx->contextProperty(QStringLiteral("serifFamily")).toString() : QString());
            b.setPixelSize(40); b.setFeature(QFont::Tag("tnum"), 1); b.setFeature(QFont::Tag("hwid"), 1);
            const QFontMetricsF mb(b);
            r.check(mb.horizontalAdvance(QStringLiteral("1111")) == mb.horizontalAdvance(QStringLiteral("0000")), QStringLiteral("现在的字体上金额数字等宽"), b.family());
        }

        // 窄窗口：顶部横向标签
        const int oldW = win->width();
        win->setWidth(620);
        const bool nar = waitFor([panel] { return panel->property("narrow").toBool(); }, 1000);
        waitFrames(win, 2);
        r.check(nar && shown(item(win, "setTab-look")), QStringLiteral("窄窗口改成顶部横向标签"));
        click(win, item(win, "setTab-me"));
        r.check(waitFor([panel] { return panel->property("section").toString() == QLatin1String("me"); }, 500), QStringLiteral("顶部标签点得到"));
        win->setWidth(oldW);
        waitFor([panel] { return !panel->property("narrow").toBool(); }, 1000);
        panel->setProperty("section", QStringLiteral("look"));
        waitFrames(win, 2);
        click(win, item(win, "settingsClose"));
        r.check(waitFor([win] { return !win->property("settingsOpen").toBool(); }, 800), QStringLiteral("× 关闭设置面板"));
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
