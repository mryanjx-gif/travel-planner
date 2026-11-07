// 自检：0.10.0 完整计划页——入口（简易面板、此刻卡）、地图只画这一条、压暗和让位、站点切换、就地改日程、页头收起、今天的行程、
// 关闭后回简易面板、前进 / 后退、补时区、编辑器里的行程安排、地球上打开。都在数据副本上做，改过的最后撤销或改回去。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "mapcontroller.h"
#include "navhistory.h"
#include "plancontroller.h"
#include "planeditor.h"
#include "planpage.h"
#include "planpagecontroller.h"
#include "plans.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QUrlQuery>
#include <qpa/qwindowsysteminterface.h>

namespace tp::app::selftest {

using json::Value;
namespace plans = tp::map::plans;

namespace {
QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findItem(c, name)) return f;
    return nullptr;
}
QQuickItem *item(QQuickWindow *win, const QString &name) { return findItem(win->contentItem(), name); }
void click(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return;
    waitStill(it, 1000);   // 0.12.11：面板滑进、列表滚动时先等停稳再点，不然点在半路上
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    const QPointF g = win->mapToGlobal(p);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    pause(80);
}
QVariant eval(QQuickItem *it, const QString &js)
{
    QQmlContext *ctx = it ? qmlContext(it) : nullptr;
    if (!ctx) return {};
    QQmlExpression e(ctx, it, js);
    return e.evaluate();
}
QVariant call(QQuickWindow *win, const char *fn, const QVariant &a = {}, const QVariant &b = {})
{
    QVariant ret;
    if (b.isValid()) QMetaObject::invokeMethod(win, fn, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a), Q_ARG(QVariant, b));
    else if (a.isValid()) QMetaObject::invokeMethod(win, fn, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a));
    else QMetaObject::invokeMethod(win, fn, Q_RETURN_ARG(QVariant, ret));
    return ret;
}
bool isShown(QQuickItem *it) { return it && it->isVisible() && it->opacity() > 0.5; }
} // namespace

void runPlanPage(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, PlanPageController *page, NavHistory *nav)
{
    r.section(QStringLiteral("0.10.0 完整计划页"));
    PlanController *pc = plansCtl();
    data::DataStore *store = app->store();
    QQuickItem *panel = item(win, QStringLiteral("planPage"));
    if (!r.check(pc && page && nav && panel, QStringLiteral("计划页在"))) return;

    pc->setTestAllStatuses(false);
    const QVariantMap demo = pc->demo();
    const QString id = demo.value(QStringLiteral("planId")).toString();
    if (!r.check(!id.isEmpty(), QStringLiteral("有带日期的路线"))) return;
    pc->setTodayOverride(demo.value(QStringLiteral("today")).toString());
    const int allPlans = int(store->data()[QStringLiteral("plans")].asArray().size());
    const QSize oldSize = win->size();
    if (win->width() < 1300) win->resize(1400, win->height());

    // ---- 入口：简易面板 ----
    pc->openPlan(id, false);
    QQuickItem *full = item(win, QStringLiteral("sheetFull"));
    waitStill(item(win, QStringLiteral("planSheet")));
    r.check(isShown(full), QStringLiteral("简易面板有\"打开完整计划\""));
    click(win, full);
    waitStill(panel);
    r.check(nav->page() == QLatin1String("trip") && page->planId() == id, QStringLiteral("点了打开计划页"), nav->page());
    r.check(panel->x() + panel->width() <= win->width() && panel->width() <= 820 * 1.3, QStringLiteral("面板在右边、宽不超过 820"), QString::number(panel->width()));

    // ---- S-03 / S-04 ----
    r.check(mapc->trips().plans().size() == 1 && mapc->trips().plans().first().id == id, QStringLiteral("地图只画这一条"), QStringLiteral("%1 条").arg(mapc->trips().plans().size()));
    r.check(!isShown(item(win, QStringLiteral("planSheet"))) && !isShown(item(win, QStringLiteral("statusPill"))) && !isShown(item(win, QStringLiteral("nowCard"))),
            QStringLiteral("简易面板、左下胶囊、此刻卡都收起来了"));
    r.check(isShown(item(win, QStringLiteral("zoomBtns"))) && item(win, QStringLiteral("zoomBtns"))->x() + item(win, QStringLiteral("zoomBtns"))->width() <= panel->x(),
            QStringLiteral("± 让到面板左边"));
    waitMapIdle(win, mapc, 4000);
    {
        const QVariantList stops = page->info().value(QStringLiteral("stops")).toList();
        int inside = 0;
        for (const QVariant &s : stops) {
            const QPointF px = mapc->pixelFor(s.toMap().value(QStringLiteral("lat")).toDouble(), s.toMap().value(QStringLiteral("lng")).toDouble());
            if (px.x() >= 0 && px.x() <= panel->x()) ++inside;
        }
        r.check(inside == stops.size(), QStringLiteral("整条路线框在面板左边露出的地图里"), QStringLiteral("%1 / %2 站").arg(inside).arg(stops.size()));
    }

    // ---- 页头 ----
    r.check(isShown(item(win, QStringLiteral("tripEdit"))) && isShown(item(win, QStringLiteral("tripSimple"))) && isShown(item(win, QStringLiteral("tripPlans"))) && isShown(item(win, QStringLiteral("tripMore"))),
            QStringLiteral("编辑、简易版、计划库、⋯ 都在"));
    r.check(isShown(item(win, QStringLiteral("tripTab-itinerary"))) && panel->property("tab").toString() == QLatin1String("itinerary"), QStringLiteral("标签默认在\"行程\""));
    click(win, item(win, QStringLiteral("tripMore")));
    r.check(isShown(item(win, QStringLiteral("tripMenu"))) && isShown(item(win, QStringLiteral("tripPin"))), QStringLiteral("⋯ 菜单打开了"));
    r.check(call(win, "escOnce").toString() == QLatin1String("tripMenu"), QStringLiteral("Esc 先关 ⋯ 菜单"));

    // ---- 站点切换 ----
    const QVariantList stops = page->info().value(QStringLiteral("stops")).toList();
    click(win, item(win, QStringLiteral("tripNext")));
    r.check(mapc->markers()->focusStop() == 0, QStringLiteral("› 从第 1 站开始"), QString::number(mapc->markers()->focusStop()));
    click(win, item(win, QStringLiteral("tripNext")));
    r.check(mapc->markers()->focusStop() == 1, QStringLiteral("› 到第 2 站"), QString::number(mapc->markers()->focusStop()));
    r.check(item(win, QStringLiteral("tripNavLabel"))->property("text").toString().contains(stops.value(1).toMap().value(QStringLiteral("name")).toString().toHtmlEscaped().left(2)),
            QStringLiteral("切换条显示站名"));
    click(win, item(win, QStringLiteral("tripPrev")));
    r.check(mapc->markers()->focusStop() == 0, QStringLiteral("‹ 回第 1 站"));
    {
        waitMapIdle(win, mapc, 2000);
        QQuickItem *list = item(win, QStringLiteral("tripList"));
        QMetaObject::invokeMethod(panel, "reveal", Q_ARG(QVariant, 2), Q_ARG(QVariant, true));
        waitFor([&] { return findItem(panel, QStringLiteral("tripCard2")) != nullptr; }, 1000);
        pause(350);   // 滚动动画 0.26 秒
        const double y0 = list->property("contentY").toDouble();
        QQuickItem *card = findItem(panel, QStringLiteral("tripCard2"));
        // 点卡片的右边（左边可能是日程行）
        const QPointF p = card->mapToScene(QPointF(card->width() - 6, 8));
        const QPointF g = win->mapToGlobal(p);
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
        pause(120);
        r.check(mapc->markers()->focusStop() == 2, QStringLiteral("点卡片聚焦那一站"), QString::number(mapc->markers()->focusStop()));
        r.check(qAbs(list->property("contentY").toDouble() - y0) < 1, QStringLiteral("点卡片时列表不跳"));
        waitMapIdle(win, mapc, 3000);
        const QVariantMap s2 = stops.value(2).toMap();
        const QPointF px = mapc->pixelFor(s2.value(QStringLiteral("lat")).toDouble(), s2.value(QStringLiteral("lng")).toDouble());
        r.check(px.x() >= 0 && px.x() <= panel->x(), QStringLiteral("主地图把这一站放在露出来的地方"), QStringLiteral("x = %1，面板 x = %2").arg(px.x()).arg(panel->x()));
    }

    // ---- I4 页头收起 + 切换条吸顶 ----
    {
        QQuickItem *list = item(win, QStringLiteral("tripList"));
        QQuickItem *compact = item(win, QStringLiteral("tripCompact"));
        QQuickItem *navBar = item(win, QStringLiteral("tripNav"));
        waitFor([&] { return compact->opacity() > 0.95; }, 600);
        r.check(compact->opacity() > 0.95, QStringLiteral("往下滚后页头收成一行"));
        r.check(qAbs(navBar->y() - panel->property("compactH").toDouble()) < 1, QStringLiteral("切换条贴在页头下面"), QString::number(navBar->y()));
        // 改数据（这里是置顶）不丢滚动位置（P-04）
        const double y = list->property("contentY").toDouble();
        pc->togglePin(id);
        pause(100);
        r.check(qAbs(list->property("contentY").toDouble() - y) < 1, QStringLiteral("数据变了滚动位置不动"));
        pc->togglePin(id);
        QMetaObject::invokeMethod(list, "positionViewAtBeginning");
        waitFor([&] { return compact->opacity() < 0.05; }, 600);
        r.check(compact->opacity() < 0.05, QStringLiteral("滚回顶上页头展开"));
    }

    // ---- 就地改日程 ----
    {
        const Value before = store->data();
        QMetaObject::invokeMethod(panel, "reveal", Q_ARG(QVariant, 0), Q_ARG(QVariant, true));
        pause(350);
        QQuickItem *sched = findItem(panel, QStringLiteral("tripSched0"));
        click(win, findItem(sched, QStringLiteral("schedAdd")));
        waitFor([&] { return findItem(sched, QStringLiteral("schedForm")) != nullptr; }, 500);
        r.check(findItem(sched, QStringLiteral("schedForm")) != nullptr, QStringLiteral("＋ 在卡片里展开表单"));
        r.check(eval(sched, QStringLiteral("ctl.draft.date")).toString() == stops.value(0).toMap().value(QStringLiteral("arriveDate")).toString(), QStringLiteral("新加的一条默认用到达日期"));
        eval(sched, QStringLiteral("ctl.draft.end = '18:00'; ctl.draft.time = ''; ctl.draft.text = '自检'; save()"));
        r.check(!eval(sched, QStringLiteral("ctl.err")).toString().isEmpty() && eval(sched, QStringLiteral("ctl.j")).toInt() == -1, QStringLiteral("填了结束没填开始：不存，说明原因"));
        r.check(store->data() == before, QStringLiteral("有问题时不碰数据"));
        eval(sched, QStringLiteral("ctl.draft.time = '9:05'; save()"));
        r.check(eval(sched, QStringLiteral("ctl.j")).toInt() == -2, QStringLiteral("保存后表单收起"));
        const Value &plan0 = *plans::planById(store->data(), id);
        const json::Array &sch = plan0[QStringLiteral("stops")].asArray()[plans::validStops(plan0).first()][QStringLiteral("schedule")].asArray();
        const Value &last = sch.back();
        r.check(last[QStringLiteral("time")].toString() == QLatin1String("09:05") && last[QStringLiteral("end")].toString() == QLatin1String("18:00") && last[QStringLiteral("text")].toString() == QStringLiteral("自检"),
                QStringLiteral("写进数据，时间统一成 09:05"));
        r.check(app->property("dirty").toBool() || !store->isDirty(), QStringLiteral("标记了未保存（自动保存）"));
        bool shownRow = false;
        for (const QVariant &d : page->info().value(QStringLiteral("stops")).toList().value(0).toMap().value(QStringLiteral("days")).toList())
            for (const QVariant &it : d.toMap().value(QStringLiteral("items")).toList())
                shownRow = shownRow || it.toMap().value(QStringLiteral("text")).toString() == QStringLiteral("自检");
        r.check(shownRow, QStringLiteral("卡片马上显示新的一条"));
        // 再改：Esc 取消
        eval(sched, QStringLiteral("begin(days[days.length - 1].items[0].j, days[days.length - 1].items[0])"));
        r.check(call(win, "escOnce").toString() == QLatin1String("sched"), QStringLiteral("Esc 取消就地修改，计划页不关"));
        r.check(nav->page() == QLatin1String("trip"), QStringLiteral("还在计划页"));
        const int n = int(sch.size());
        r.check(page->deleteSched(0, n - 1), QStringLiteral("能删一条"));
        store->modify([&](Value &d) { d = before; });
        app->noteChanged(false);
    }

    // ---- 关闭 → 简易面板；前进 / 后退 ----
    r.check(call(win, "escOnce").toString() == QLatin1String("page"), QStringLiteral("Esc 关掉计划页"));
    waitStill(item(win, QStringLiteral("planSheet")));
    r.check(nav->page() != QLatin1String("trip") && page->planId().isEmpty(), QStringLiteral("回到地图"), nav->page());
    r.check(isShown(item(win, QStringLiteral("planSheet"))) && mapc->markers()->selectedPlan() == id, QStringLiteral("关掉后打开这个计划的简易面板"));
    r.check(pc->solo().isEmpty() && mapc->trips().plans().size() > 1, QStringLiteral("地图恢复显示其他计划"), QStringLiteral("%1 条（共 %2）").arg(mapc->trips().plans().size()).arg(allPlans));
    nav->back();
    pause(100);
    r.check(nav->page() == QLatin1String("trip") && page->planId() == id, QStringLiteral("后退回到计划页"));
    nav->forward();
    pause(100);
    r.check(nav->page() != QLatin1String("trip"), QStringLiteral("前进回到地图"));
    mapc->markers()->clearSelection();

    // ---- I3 此刻卡"今天的行程" ----
    {
        const QVariantMap now = pc->now();
        if (now.value(QStringLiteral("kind")).toString() != QLatin1String("live")) pc->setNextTrip(id, true);
        win->setProperty("nowOpen", true);
        QQuickItem *openBtn = item(win, QStringLiteral("nowOpenTrip"));
        waitFor([&] { return isShown(openBtn); }, 800);
        const QVariantMap nw = pc->now();
        const QString nowPlan = nw.value(QStringLiteral("planId")).toString();
        const int here = nw.value(QStringLiteral("hereIndex")).toInt();
        r.info(QStringLiteral("此刻：%1，今天第 %2 站").arg(nw.value(QStringLiteral("kind")).toString()).arg(here + 1));
        click(win, openBtn);
        waitStill(panel);
        pause(350);
        r.check(page->planId() == nowPlan, QStringLiteral("此刻卡打开计划页"));
        if (nw.value(QStringLiteral("kind")).toString() == QLatin1String("live")) {
            r.check(mapc->markers()->focusStop() == here, QStringLiteral("聚焦今天这一站"), QString::number(mapc->markers()->focusStop()));
            QQuickItem *card = findItem(panel, QStringLiteral("tripCard%1").arg(here));
            r.check(card && isShown(findItem(card, QStringLiteral("tripToday"))), QStringLiteral("这一站标着\"今天\"（I3）"));
            const double top = card ? card->mapToItem(panel, QPointF(0, 0)).y() : -1;
            r.check(top >= panel->property("compactH").toDouble() && top < panel->height() * 0.5, QStringLiteral("滚到今天这一站"), QString::number(top));
        }
        nav->go(pc->mode());
        waitStill(panel);
        mapc->markers()->clearSelection();
    }

    // ---- 补时区 ----
    {
        QString tzPlan;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
            if (plans::stopsWithoutTz(p).size() >= 2) { tzPlan = p[QStringLiteral("id")].toString(); break; }
        if (r.check(!tzPlan.isEmpty(), QStringLiteral("数据里有没时区的站点"))) {
            const Value before = store->data();
            QStringList urls;
            page->setFetchForTest([&urls](const QUrl &u, std::function<void(int, const QByteArray &, int)> done) {
                urls << u.toString();
                QJsonArray arr;
                const QString la = QUrlQuery(u).queryItemValue(QStringLiteral("latitude"));
                for (qsizetype k = 0; k < la.split(QLatin1Char(',')).size(); ++k) arr.append(QJsonObject{ { QStringLiteral("timezone"), QStringLiteral("Asia/Shanghai") } });
                done(200, QJsonDocument(arr).toJson(), 0);
            });
            const int req0 = page->tzRequests();
            QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, tzPlan), Q_ARG(QVariant, -1));
            const Value *p = plans::planById(store->data(), tzPlan);
            r.check(waitFor([&] { p = plans::planById(store->data(), tzPlan); return p && plans::stopsWithoutTz(*p).isEmpty(); }, 2000), QStringLiteral("打开时给没时区的站点补上"));
            r.check(page->tzRequests() == req0 + 1, QStringLiteral("请求一次"), QString::number(page->tzRequests() - req0));
            bool approx = false;
            for (const QVariant &s : page->info().value(QStringLiteral("stops")).toList())
                approx = approx || s.toMap().value(QStringLiteral("sun")).toMap().value(QStringLiteral("approx")).toBool();
            r.check(!approx, QStringLiteral("补上后日出日落不再是估算"));
            nav->go(pc->mode());
            waitStill(panel);
            mapc->markers()->clearSelection();
            QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, tzPlan), Q_ARG(QVariant, -1));
            pause(100);
            r.check(page->tzRequests() == req0 + 1, QStringLiteral("都有了就不再请求"));
            nav->go(pc->mode());
            waitStill(panel);
            mapc->markers()->clearSelection();
            page->setFetchForTest({});
            store->modify([&](Value &d) { d = before; });
            app->noteChanged(false);
        }
    }

    // ---- 编辑器里的行程安排（同一个组件）----
    {
        PlanEditor *ed = editorCtl();
        const auto firstSchedule = [&] {
            const Value &p = *plans::planById(store->data(), id);
            return p[QStringLiteral("stops")].asArray()[plans::validStops(p).first()][QStringLiteral("schedule")];
        };
        const Value before = firstSchedule();
        QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
        waitStill(panel);
        click(win, item(win, QStringLiteral("tripEdit")));
        waitFor([&] { return ed->isOpen(); }, 1000);
        r.check(ed->isOpen() && nav->page() != QLatin1String("trip"), QStringLiteral("计划页的\"编辑\"打开编辑器"));
        const QString err = ed->saveSched(0, -1, QString(), QStringLiteral("0930"), QString(), QStringLiteral("编辑器自检"));
        const QVariantList days = ed->plan().value(QStringLiteral("stops")).toList().value(0).toMap().value(QStringLiteral("days")).toList();
        bool found = false;
        for (const QVariant &d : days)
            for (const QVariant &it : d.toMap().value(QStringLiteral("items")).toList())
                found = found || (it.toMap().value(QStringLiteral("text")).toString() == QStringLiteral("编辑器自检") && it.toMap().value(QStringLiteral("time")).toString() == QLatin1String("09:30"));
        r.check(err.isEmpty() && found, QStringLiteral("编辑器能加行程安排"), err);
        r.check(ed->saveSched(0, -1, QString(), QString(), QStringLiteral("10:00"), QStringLiteral("x")) == QLatin1String("endNoStart"), QStringLiteral("编辑器里同样检查时间"));
        ed->discard();
        waitFor([&] { return !ed->isOpen(); }, 1000);
        r.check(firstSchedule() == before, QStringLiteral("放弃修改后行程安排恢复"));
        mapc->markers()->clearSelection();
    }

    // ---- 窄窗口：面板盖满时地图不让位，也不出错 ----
    {
        win->resize(500, win->height());
        QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
        waitStill(panel);
        r.check(panel->x() >= 0 && panel->x() + panel->width() <= win->width(), QStringLiteral("窄窗口里面板放得下"), QStringLiteral("x %1 宽 %2").arg(panel->x()).arg(panel->width()));
        r.check(!isShown(item(win, QStringLiteral("zoomBtns"))), QStringLiteral("窄窗口里 ± 藏起来"));
        nav->go(pc->mode());
        waitStill(panel);
        mapc->markers()->clearSelection();
        win->resize(oldSize.width() < 1300 ? 1400 : oldSize.width(), win->height());
        pause(150);
    }

    // ---- 被删掉的计划 ----
    {
        page->show(QStringLiteral("没有这个计划"));
        r.check(page->info().isEmpty(), QStringLiteral("计划不在时 info 为空（页面显示\"找不到这个计划\"）"));
        page->show(QString());
    }

    // ---- 速度：最长的路线 ----
    {
        QString longest;
        qsizetype most = 0;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
            if (p[QStringLiteral("stops")].asArray().size() > size_t(most)) { most = qsizetype(p[QStringLiteral("stops")].asArray().size()); longest = p[QStringLiteral("id")].toString(); }
        QElapsedTimer t;
        t.start();
        const QVariantMap m = plans::pageInfo(store->data(), longest, pc->today());
        r.check(t.elapsed() < 100 && !m.isEmpty(), QStringLiteral("%1 站的路线算一遍页面内容 < 100 ms").arg(most), QStringLiteral("%1 ms").arg(t.elapsed()));
    }

    // ---- 地球上打开（框住整条路线在 5.5 级以内：留在地球上）----
    {
        call(win, "pickBase", QStringLiteral("globe"));
        waitFor([&] { return win->property("globeMode").toBool(); }, 3000);
        if (r.check(win->property("globeMode").toBool(), QStringLiteral("进了地球"))) {
            QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
            waitStill(panel);
            pause(1200);
            r.check(nav->page() == QLatin1String("trip") && mapc->trips().plans().size() == 1, QStringLiteral("地球上也只画这一条"));
            r.check(panel->x() + panel->width() <= win->width(), QStringLiteral("地球上面板也在"));
            nav->go(pc->mode());
            waitStill(panel);
            mapc->markers()->clearSelection();
            call(win, "pickBase", QStringLiteral("street"));
            waitFor([&] { return !win->property("globeMode").toBool(); }, 3000);
        }
    }

    pc->setTodayOverride(QString());
    pc->setTestAllStatuses(true);
    mapc->markers()->clearSelection();
}

} // namespace tp::app::selftest
