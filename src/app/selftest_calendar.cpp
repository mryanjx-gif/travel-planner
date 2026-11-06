// 自检：0.11.2 日历（年、月、翻页、弹出卡、拖动和撤销的往返、撞期、隐藏、"还没定日期"）、游记（列表、写、读、空游记、R1、计划页的入口）。
// 会改数据，都在数据副本上做；拖动和撤销要逐字节回到原来的样子（只有 savedAt 例外）。
#include "selftest_util.h"

#include "appcontroller.h"
#include "calendarcontroller.h"
#include "datastore.h"
#include "journalcontroller.h"
#include "jsonio.h"
#include "navhistory.h"
#include "photocontroller.h"
#include "plancal.h"
#include "planpagecontroller.h"
#include "plancontroller.h"
#include "plans.h"

#include <QDate>
#include <QFile>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>

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
bool visibleChain(QQuickItem *it) { return it && it->isVisible() && it->opacity() > 0.5; }

struct Ctx
{
    QQuickWindow *win;
    AppController *app;
    data::DataStore *store;
    NavHistory *nav;
    QQuickItem *item(const QString &n) const { return findItem(win->contentItem(), n); }
    bool shown(const QString &n) const { return visibleChain(item(n)); }
    void press(const QPointF &p) const { QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, win->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress); }
    void moveTo(const QPointF &p) const { QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, win->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, QEvent::MouseMove); pause(30); }
    void release(const QPointF &p) const { QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, win->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease); pause(60); }
    QPointF center(QQuickItem *it) const { return it->mapToScene(QPointF(it->width() / 2, it->height() / 2)); }
    void click(QQuickItem *it) const { if (!it) return; const QPointF p = center(it); press(p); release(p); pause(100); }
    void click(const QString &n) const { click(item(n)); }
    // 真的用鼠标拖：按下、分几步走、（可选）途中回调、松手
    void dragTo(const QPointF &from, const QPointF &to, const std::function<void()> &mid = {}) const
    {
        press(from);
        for (int k = 1; k <= 5; ++k) moveTo(from + (to - from) * (k / 5.0));
        if (mid) mid();
        release(to);
    }
    bool saved() const { return waitFor([this] { return !store->isDirty(); }, 3000); }
    // 右边的列表长：把里面的元素滚到看得见（点在裁掉的地方点不中）
    void revealInSide(QQuickItem *it) const
    {
        QQuickItem *flick = item(QStringLiteral("calSideFlick"));
        if (!it || !flick) return;
        QQuickItem *content = flick->property("contentItem").value<QQuickItem *>();
        const double y = it->mapToItem(content, QPointF(0, 0)).y();
        const double top = std::max(0.0, std::min(y - 20, flick->property("contentHeight").toDouble() - flick->height()));
        flick->setProperty("contentY", top);
        pause(60);
    }
    Value plan(const QString &id) const { const Value *p = plans::planById(store->data(), id); return p ? *p : Value(); }
};

// 磁盘上的文件和内存一样（只有保存时间不同）
Value onDisk(data::DataStore *store)
{
    QFile f(store->dataFile());
    if (!f.open(QIODevice::ReadOnly)) return {};
    Value v = json::parse(QString::fromUtf8(f.readAll()));
    if (v.isObject()) { v.mutObject().remove(QStringLiteral("savedAt")); v.mutObject().remove(QStringLiteral("savedBy")); }
    return v;
}
Value noTime(Value v)
{
    if (v.isObject()) { v.mutObject().remove(QStringLiteral("savedAt")); v.mutObject().remove(QStringLiteral("savedBy")); }
    return v;
}
QString str(const Value &o, const char *k) { return o[QString::fromLatin1(k)].toString(); }

template <class T>
T *ctxObject(QQuickWindow *win, const char *name)
{
    QQmlContext *c = qmlContext(win->contentItem()->childItems().isEmpty() ? win->contentItem() : findItem(win->contentItem(), QStringLiteral("calendarPage")));
    return c ? qobject_cast<T *>(c->contextProperty(QString::fromLatin1(name)).value<QObject *>()) : nullptr;
}
} // namespace

void runCalendar(Report &r, AppController *app, QQuickWindow *win)
{
    r.section(QStringLiteral("日历（0.11.2）：年、月、弹出卡、拖动和撤销的往返"));
    data::DataStore *store = app->store();
    NavHistory *nav = navCtl();
    auto *cal = ctxObject<CalendarController>(win, "calendar");
    if (!r.check(cal, QStringLiteral("拿到日历控制器"))) return;
    Ctx c{ win, app, store, nav };
    const auto goPage = [&](const QString &p) { QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, p)); };
    goPage(QStringLiteral("calendar"));
    r.check(waitFor([&] { return c.shown(QStringLiteral("calendarPage")); }, 2000), QStringLiteral("日历换掉了占位页"));
    r.check(cal->view() == QLatin1String("year"), QStringLiteral("默认年视图"), cal->view());

    // 一个有站点日期和交通住宿的计划，下面都用它
    QString pid;
    for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
        if (plans::statusOf(p) == QLatin1String("planning") && plans::okDate(str(p, "startDate")) && plans::datedBookings(p) > 0) { pid = str(p, "id"); break; }
    if (!r.check(!pid.isEmpty(), QStringLiteral("数据里有定了日期、带交通住宿的计划"))) return;
    const Value p0 = c.plan(pid);
    const QString start0 = str(p0, "startDate"), end0 = str(p0, "endDate");
    cal->goTo(start0.left(4).toInt(), start0.mid(5, 2).toInt());
    QQuickItem *bar = nullptr;
    r.check(waitFor([&] { bar = c.item(QStringLiteral("calBar-") + pid); return visibleChain(bar); }, 1500), QStringLiteral("年视图有这个计划的横条"));

    // ---- 翻页和今天 ----
    const int y0 = cal->year(), m0 = cal->month();
    c.click(QStringLiteral("calNext"));
    const QDate shifted = QDate(y0, m0, 1).addMonths(6);
    r.check(cal->year() == shifted.year() && cal->month() == shifted.month(), QStringLiteral("年视图 › 翻 6 个月"), QStringLiteral("%1-%2").arg(cal->year()).arg(cal->month()));
    c.click(QStringLiteral("calPrev"));
    r.check(cal->year() == y0 && cal->month() == m0, QStringLiteral("‹ 翻回来"));
    c.click(QStringLiteral("calToday"));
    const QDate now = QDate::fromString(plansCtl()->today(), Qt::ISODate);
    r.check(cal->year() == now.year() && cal->month() == now.month(), QStringLiteral("点今天：回到这个月"), QStringLiteral("%1-%2").arg(cal->year()).arg(cal->month()));
    QQuickItem *todayLine = c.item(QStringLiteral("calTodayLine"));
    r.check(waitFor([&] { return visibleChain(todayLine); }, 800), QStringLiteral("年视图有今天的竖线"));
    c.click(QStringLiteral("calLabel"));
    r.check(waitFor([&] { return c.shown(QStringLiteral("calPicker")); }, 800), QStringLiteral("点标题弹出选年月"));
    c.click(QStringLiteral("calPickerMonth-") + QString::number(m0 - 1));
    QQuickItem *yearText = c.item(QStringLiteral("calPickerYear"));
    r.check(!c.shown(QStringLiteral("calPicker")), QStringLiteral("选了月份就收起"), yearText ? yearText->property("text").toString() : QString());
    cal->goTo(y0, m0);

    // ---- 拖横条：站点日期平移，交通住宿不动，一步撤销（真的用鼠标） ----
    waitFor([&] { bar = c.item(QStringLiteral("calBar-") + pid); return visibleChain(bar); }, 1000);
    const Value before = store->data();
    QQuickItem *yearBody = c.item(QStringLiteral("calYearBody"));
    const int undo0 = app->undoCount();
    const QPointF from = c.center(bar);
    const double monthPx = yearBody ? yearBody->width() / 12.0 : 80;
    c.dragTo(from, from + QPointF(monthPx * 1.5, 6), [&] {
        r.check(win && c.shown(QStringLiteral("calGhost")), QStringLiteral("拖动时有影子"));
        QQuickItem *when = c.item(QStringLiteral("calGhostWhen"));
        r.check(when && when->property("text").toString().contains(QStringLiteral(" · ")), QStringLiteral("影子上写落点的日期"), when ? when->property("text").toString() : QString());
    });
    const Value p1 = c.plan(pid);
    const int n = plans::days(start0, str(p1, "startDate"));
    r.check(n > 25 && n < 60, QStringLiteral("拖过去：开始日期挪了约一个半月"), QStringLiteral("%1 → %2（%3 天）").arg(start0, str(p1, "startDate")).arg(n));
    r.check(str(p1, "endDate") == plans::addDays(end0, n), QStringLiteral("结束日期同样平移，天数不变"), str(p1, "endDate"));
    {
        bool stopsOk = true, schedOk = true;
        const json::Array &a0 = p0[QStringLiteral("stops")].asArray(), &a1 = p1[QStringLiteral("stops")].asArray();
        for (size_t k = 0; k < a0.size(); ++k) {
            for (const char *key : { "arriveDate", "departDate" })
                if (plans::okDate(str(a0[k], key))) stopsOk = stopsOk && str(a1[k], key) == plans::addDays(str(a0[k], key), n);
            const json::Array &s0 = a0[k][QStringLiteral("schedule")].asArray(), &s1 = a1[k][QStringLiteral("schedule")].asArray();
            for (size_t j = 0; j < s0.size(); ++j)
                if (plans::okDate(str(s0[j], "date"))) schedOk = schedOk && str(s1[j], "date") == plans::addDays(str(s0[j], "date"), n);
        }
        r.check(stopsOk, QStringLiteral("站点的到达、离开都平移"));
        r.check(schedOk, QStringLiteral("日程里写了日期的都平移"));
    }
    r.check(p1[QStringLiteral("bookings")] == p0[QStringLiteral("bookings")], QStringLiteral("交通住宿不跟"));
    r.check(p1[QStringLiteral("expenses")] == p0[QStringLiteral("expenses")] && p1[QStringLiteral("todos")] == p0[QStringLiteral("todos")], QStringLiteral("消费、待办不跟"));
    QQuickItem *bookLine = c.item(QStringLiteral("calNoticeBookings"));
    r.check(waitFor([&] { return visibleChain(bookLine); }, 800) && bookLine->property("text").toString().contains(QString::number(plans::datedBookings(p0))),
            QStringLiteral("提示 N 条交通住宿没跟"), bookLine ? bookLine->property("text").toString() : QString());
    r.check(c.shown(QStringLiteral("calNoticeBookingsBtn")) && c.shown(QStringLiteral("calNoticeOk")) && c.shown(QStringLiteral("calNoticeUndo")), QStringLiteral("提示上有 打开交通住宿 / 知道了 / 撤销"));
    r.check(c.saved() && app->undoCount() == std::min(undo0 + 1, data::UndoStack::kMaxSteps), QStringLiteral("马上保存，撤销里只多一步"), QStringLiteral("%1 → %2").arg(undo0).arg(app->undoCount()));
    r.check(onDisk(store) == noTime(store->data()), QStringLiteral("磁盘上是挪过的"));
    c.click(QStringLiteral("calNoticeUndo"));
    r.check(waitFor([&] { return !store->isDirty() && noTime(store->data()) == noTime(before); }, 3000), QStringLiteral("一步撤销：数据和原来完全一样（含 updatedAt）"));
    r.check(onDisk(store) == noTime(before), QStringLiteral("一步撤销：磁盘上也和原来完全一样"));
    r.check(app->undoCount() == std::min(undo0, data::UndoStack::kMaxSteps - 1), QStringLiteral("撤销栈回到拖之前（全量时已经满 30 步，最早的一步被挤掉）"), QString::number(app->undoCount()));

    // ---- 打开交通住宿 ----
    {
        c.dragTo(c.center(c.item(QStringLiteral("calBar-") + pid)), c.center(c.item(QStringLiteral("calBar-") + pid)) + QPointF(monthPx, 0));
        waitFor([&] { return c.shown(QStringLiteral("calNoticeBookingsBtn")); }, 800);
        c.click(QStringLiteral("calNoticeBookingsBtn"));
        r.check(waitFor([&] { return nav->page() == QLatin1String("trip"); }, 1500), QStringLiteral("点打开交通住宿：到计划页"), nav->page());
        nav->back();
        waitFor([&] { return nav->page() == QLatin1String("calendar"); }, 1000);
        c.saved();
        app->undo();
        c.saved();
        r.check(noTime(store->data()) == noTime(before), QStringLiteral("再撤销回到原来"));
    }

    // ---- 不用鼠标的往返：跨月、跨年、月底、倒回去 ----
    {
        bool ok = true;
        QString bad;
        for (const char *day : { "2027-01-31", "2028-02-29", "2020-03-01", "2026-12-31" }) {
            const QVariantMap res = cal->drop(pid, QString::fromLatin1(day), start0);
            c.saved();
            const Value moved = c.plan(pid);
            ok = ok && res.value(QStringLiteral("ok")).toBool() && str(moved, "startDate") == QString::fromLatin1(day)
                 && str(moved, "endDate") == plans::addDays(QString::fromLatin1(day), plans::days(start0, end0));
            app->undo();
            c.saved();
            if (noTime(store->data()) != noTime(before)) { ok = false; bad = QString::fromLatin1(day); }
            if (onDisk(store) != noTime(before)) { ok = false; bad = QStringLiteral("磁盘 ") + QString::fromLatin1(day); }
        }
        r.check(ok, QStringLiteral("跨月、跨年、闰年二月、月底：拖过去再撤销都完全一样"), bad);
    }

    // ---- Esc 取消 ----
    {
        waitFor([&] { bar = c.item(QStringLiteral("calBar-") + pid); return visibleChain(bar); }, 1000);
        const QPointF a = c.center(bar);
        c.press(a);
        for (int k = 1; k <= 3; ++k) c.moveTo(a + QPointF(monthPx * k / 3, 0));
        QVariant esc;
        QMetaObject::invokeMethod(win, "escOnce", Q_RETURN_ARG(QVariant, esc));
        r.check(esc.toString() == QLatin1String("calDrag"), QStringLiteral("拖动中按 Esc 取消"), esc.toString());
        c.release(a + QPointF(monthPx, 0));
        r.check(noTime(store->data()) == noTime(before), QStringLiteral("取消后没有改动"));
    }

    // ---- 撞期（R2） ----
    {
        QString other;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
            if (str(p, "id") != pid && plans::okDate(str(p, "startDate"))) { other = str(p, "id"); break; }
        const QVariantMap res = cal->drop(pid, str(c.plan(other), "startDate"), start0);
        c.saved();
        r.check(res.value(QStringLiteral("overlaps")).toInt() >= 1, QStringLiteral("R2：拖到别的计划的日子，算出撞期"), QString::number(res.value(QStringLiteral("overlaps")).toInt()));
        QMetaObject::invokeMethod(c.item(QStringLiteral("calendarPage")), "showNotice", Q_ARG(QVariant, res));
        QQuickItem *over = c.item(QStringLiteral("calNoticeOverlap"));
        r.check(waitFor([&] { return visibleChain(over); }, 500) && over->property("text").toString().contains(QStringLiteral("重叠")), QStringLiteral("R2：提示上写和 N 个计划重叠"), over ? over->property("text").toString() : QString());
        c.click(QStringLiteral("calNoticeUndo"));
        c.saved();
        r.check(noTime(store->data()) == noTime(before), QStringLiteral("撤销后完全一样"));
    }

    // ---- 月视图 ----
    cal->setView(QStringLiteral("month"));
    cal->goTo(start0.left(4).toInt(), start0.mid(5, 2).toInt());
    QQuickItem *weekBar = nullptr;
    r.check(waitFor([&] { for (int w = 0; w < 6 && !weekBar; ++w) weekBar = c.item(QStringLiteral("calBar-%1-%2").arg(pid).arg(w)); return visibleChain(weekBar); }, 1500), QStringLiteral("月视图有横条"));
    QQuickItem *cell = c.item(QStringLiteral("calDay-") + start0);
    r.check(visibleChain(cell), QStringLiteral("月视图有这一天的格子"));
    r.check(!c.item(QStringLiteral("calLabelText"))->property("text").toString().isEmpty(), QStringLiteral("标题写着年月"), c.item(QStringLiteral("calLabelText"))->property("text").toString());
    c.click(QStringLiteral("calNext"));
    const QDate oneOn = QDate::fromString(start0.left(7) + QStringLiteral("-01"), Qt::ISODate).addMonths(1);
    r.check(cal->month() == oneOn.month(), QStringLiteral("月视图 › 翻 1 个月"));
    c.click(QStringLiteral("calPrev"));
    // 月视图里拖一天
    {
        QQuickItem *cellNext = nullptr;
        const QString grabDay = start0, toDay = plans::addDays(start0, 2);
        waitFor([&] { cell = c.item(QStringLiteral("calDay-") + grabDay); cellNext = c.item(QStringLiteral("calDay-") + toDay); return visibleChain(cell) && visibleChain(cellNext); }, 1000);
        weekBar = nullptr;
        waitFor([&] { for (int w = 0; w < 6 && !weekBar; ++w) weekBar = c.item(QStringLiteral("calBar-%1-%2").arg(pid).arg(w)); return visibleChain(weekBar); }, 800);
        if (cell && cellNext && weekBar) {
            // 从起始那天的格子里、横条的高度按下；落点在那一天的格子（可能在下一周）
            const double by = weekBar->mapToScene(QPointF(0, weekBar->height() / 2)).y();
            const QPointF from2(cell->mapToScene(QPointF(cell->width() / 2, 0)).x(), by), to2 = c.center(cellNext);
            c.dragTo(from2, to2);
            r.check(str(c.plan(pid), "startDate") == toDay, QStringLiteral("月视图拖两天：按住哪天，那天落到鼠标下"), str(c.plan(pid), "startDate"));
            c.saved();
            app->undo();
            c.saved();
        }
        r.check(noTime(store->data()) == noTime(before), QStringLiteral("月视图拖动撤销后完全一样"));
    }

    // ---- 弹出卡 ----
    weekBar = nullptr;
    waitFor([&] { for (int w = 0; w < 6 && !weekBar; ++w) weekBar = c.item(QStringLiteral("calBar-%1-%2").arg(pid).arg(w)); return visibleChain(weekBar); }, 800);
    waitFrames(win);   // 周的行刚重建时还叠在一起（Column 下一帧才排），别点早了
    c.click(weekBar);
    QQuickItem *cardItem = c.item(QStringLiteral("calCard"));
    r.check(waitFor([&] { return visibleChain(cardItem); }, 800), QStringLiteral("点横条弹出卡片（没拖动）"), weekBar ? weekBar->objectName() : QString());
    r.check(noTime(store->data()) == noTime(before), QStringLiteral("点一下不改数据"));
    QQuickItem *cardLayer = c.item(QStringLiteral("calCardLayer"));
    if (cardLayer) {
        r.check(cardLayer->property("days").toString() == QString::number(plans::days(start0, end0) + 1), QStringLiteral("卡片的天数对"), cardLayer->property("days").toString() + QStringLiteral(" · ") + cardLayer->property("name").toString() + QStringLiteral(" · ") + pid);
        const QString endText = c.item(QStringLiteral("calCardEnd"))->property("text").toString();
        r.check(endText.contains(end0), QStringLiteral("卡片算出结束日期"), endText);
        // 天数不对：不关
        cardLayer->setProperty("days", QStringLiteral("0"));
        c.click(QStringLiteral("calCardSave"));
        r.check(cardLayer->property("open").toBool() && noTime(store->data()) == noTime(before), QStringLiteral("天数不对时不保存"));
        // 改天数和开始、颜色
        cardLayer->setProperty("days", QStringLiteral("3"));
        cardLayer->setProperty("start", plans::addDays(start0, 10));
        c.click(QStringLiteral("calCardColor-2"));
        c.click(QStringLiteral("calCardSave"));
        c.saved();
        const Value pc = c.plan(pid);
        r.check(str(pc, "startDate") == plans::addDays(start0, 10) && str(pc, "endDate") == plans::addDays(start0, 12), QStringLiteral("保存：开始 + 天数"), str(pc, "startDate") + QStringLiteral(" ~ ") + str(pc, "endDate"));
        r.check(str(pc, "color") == QLatin1String("#1baf7a"), QStringLiteral("保存：颜色"), str(pc, "color"));
        r.check(str(pc[QStringLiteral("stops")].asArray().empty() ? Value() : pc[QStringLiteral("stops")].asArray()[0], "arriveDate") != str(p0[QStringLiteral("stops")].asArray()[0], "arriveDate"), QStringLiteral("保存：站点跟着平移"));
        r.check(c.shown(QStringLiteral("calNotice")), QStringLiteral("保存后也有提示"));
        c.click(QStringLiteral("calNoticeUndo"));
        c.saved();
        r.check(noTime(store->data()) == noTime(before), QStringLiteral("撤销后完全一样"));
    }
    // ---- 隐藏 ----
    weekBar = nullptr;
    waitFor([&] { for (int w = 0; w < 6 && !weekBar; ++w) weekBar = c.item(QStringLiteral("calBar-%1-%2").arg(pid).arg(w)); return visibleChain(weekBar); }, 800);
    waitFrames(win);
    c.click(weekBar);
    waitFor([&] { return c.shown(QStringLiteral("calCardHide")); }, 500);
    c.click(QStringLiteral("calCardHide"));
    r.check(cal->isHidden(pid) && waitFor([&] { return c.shown(QStringLiteral("calHiddenToggle")); }, 800), QStringLiteral("隐藏后右边出现\"在日历里隐藏的\""));
    r.check(noTime(store->data()) == noTime(before), QStringLiteral("隐藏不改数据（只记在设置里）"));
    {
        QTemporaryDir tmp;
        const bool sideWas = cal->sideOpen();
        cal->setSideOpen(false);
        cal->reopenForTest(tmp.filePath(QStringLiteral("s.json")));
        r.check(cal->view() == QLatin1String("month") && !cal->sideOpen() && cal->isHidden(pid), QStringLiteral("视图、收起右边、隐藏都记住了"));
        cal->setSideOpen(sideWas);
    }
    c.revealInSide(c.item(QStringLiteral("calHiddenToggle")));
    c.click(QStringLiteral("calHiddenToggle"));
    QQuickItem *unhide = nullptr;
    waitFor([&] { unhide = c.item(QStringLiteral("calUnhide-") + pid); return visibleChain(unhide); }, 800);
    c.revealInSide(unhide);
    c.click(unhide);
    r.check(!cal->isHidden(pid), QStringLiteral("点显示恢复"));
    cal->setHidden(pid, false);   // 上面没点中也别影响后面

    // ---- 还没定日期：按状态分组，拖到某一天；想去的变计划中；站点平移 ----
    cal->setView(QStringLiteral("year"));
    cal->goTo(2027, 1);
    QString wid;
    for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
        if (plans::statusOf(p) == QLatin1String("wish") && str(p, "startDate").isEmpty() && str(p, "endDate").isEmpty()) { wid = str(p, "id"); break; }
    const QVariantMap side = cal->info().value(QStringLiteral("side")).toMap();
    r.check(side.value(QStringLiteral("wish")).toList().size() + side.value(QStringLiteral("planning")).toList().size() > 0, QStringLiteral("右边有还没定日期的"),
            QStringLiteral("计划中 %1，想去 %2").arg(side.value(QStringLiteral("planning")).toList().size()).arg(side.value(QStringLiteral("wish")).toList().size()));
    if (!wid.isEmpty()) {
        QQuickItem *row = nullptr;
        waitFor([&] { row = c.item(QStringLiteral("calUndated-") + wid); return visibleChain(row); }, 1000);
        c.revealInSide(row);
        yearBody = c.item(QStringLiteral("calYearBody"));
        const QPointF to = yearBody->mapToScene(QPointF(yearBody->width() * 0.5, 60));
        const int days = plans::planDays(c.plan(wid));
        c.dragTo(c.center(row), to);
        c.saved();
        const Value w1 = c.plan(wid);
        r.check(plans::okDate(str(w1, "startDate")) && str(w1, "startDate").startsWith(QLatin1String("2027-0")), QStringLiteral("想去的拖到某一天：定了日期"), str(w1, "startDate"));
        r.check(plans::planDays(w1) == days, QStringLiteral("天数按原来的"), QString::number(plans::planDays(w1)));
        r.check(plans::statusOf(w1) == QLatin1String("planning"), QStringLiteral("想去改成计划中"));
        QQuickItem *line = c.item(QStringLiteral("calNoticeLine"));
        r.check(line && line->property("text").toString().contains(QStringLiteral("计划中")), QStringLiteral("提示写了状态变了"), line ? line->property("text").toString() : QString());
        app->undo();
        c.saved();
        r.check(noTime(store->data()) == noTime(before) && onDisk(store) == noTime(before), QStringLiteral("撤销后完全一样（状态、日期）"));
    } else r.info(QStringLiteral("数据里没有没定日期的想去，拖进来没测"));

    // ---- 拖到右边：去掉日期 ----
    cal->goTo(y0, m0);
    {
        waitFor([&] { bar = c.item(QStringLiteral("calBar-") + pid); return visibleChain(bar); }, 1000);
        QQuickItem *sideItem = c.item(QStringLiteral("calSide"));
        if (!r.check(bar && sideItem, QStringLiteral("拖到右边的前提：有横条"))) return;
        c.dragTo(c.center(bar), c.center(sideItem));
        c.saved();
        const Value un = c.plan(pid);
        r.check(str(un, "startDate").isEmpty() && str(un, "endDate").isEmpty(), QStringLiteral("拖到右边：去掉日期"));
        r.check(un[QStringLiteral("days")].toNumber() == plans::days(start0, end0) + 1, QStringLiteral("天数记进 days"), QString::number(un[QStringLiteral("days")].toNumber()));
        c.click(QStringLiteral("calNoticeUndo"));
        c.saved();
        r.check(noTime(store->data()) == noTime(before) && onDisk(store) == noTime(before), QStringLiteral("撤销后完全一样（站点日期也回来了）"));
    }

    // ---- 计划页 ⋯ 在日历里看 ----
    {
        QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, pid), Q_ARG(QVariant, -1));
        waitFor([&] { return nav->page() == QLatin1String("trip"); }, 1000);
        QQuickItem *panel = c.item(QStringLiteral("planPage"));
        if (panel) panel->setProperty("menuOpen", true);
        waitFor([&] { return c.shown(QStringLiteral("tripInCalendar")); }, 800);
        c.click(QStringLiteral("tripInCalendar"));
        r.check(waitFor([&] { return nav->page() == QLatin1String("calendar"); }, 1000) && cal->view() == QLatin1String("month") && cal->month() == start0.mid(5, 2).toInt() && cal->year() == start0.left(4).toInt(),
                QStringLiteral("⋯ 在日历里看：跳到那个月的月视图"), QStringLiteral("%1 %2-%3").arg(cal->view()).arg(cal->year()).arg(cal->month()));
    }
    if (!wid.isEmpty()) {
        QQuickItem *pageIt = c.item(QStringLiteral("calendarPage"));
        QMetaObject::invokeMethod(pageIt, "focusUndated", Q_ARG(QVariant, wid));
        r.check(pageIt->property("flashId").toString() == wid && cal->sideOpen(), QStringLiteral("没定日期的：打开右边、闪一下"));
    }
    cal->setView(QStringLiteral("year"));
    cal->goToday();
    QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("map")));
    waitFor([&] { return !c.shown(QStringLiteral("calendarPage")); }, 1000);
    c.saved();
    r.check(noTime(store->data()) == noTime(before), QStringLiteral("日历这一节结束：数据和开始时完全一样"));
}

void runJournal(Report &r, AppController *app, QQuickWindow *win)
{
    r.section(QStringLiteral("游记（0.11.2）：列表、写、读、空游记、R1、计划页入口"));
    data::DataStore *store = app->store();
    NavHistory *nav = navCtl();
    auto *j = ctxObject<JournalController>(win, "journal");
    auto *ph = ctxObject<PhotoController>(win, "photos");
    if (!r.check(j && ph, QStringLiteral("拿到游记控制器"))) return;
    Ctx c{ win, app, store, nav };
    const auto journalCount = [&] { return int(store->data()[QStringLiteral("journal")].asArray().size()); };
    const auto goList = [&] { nav->go(QStringLiteral("journal")); pause(60); };
    goList();
    r.check(waitFor([&] { return c.shown(QStringLiteral("journalPage")) && c.shown(QStringLiteral("journalList")); }, 2000), QStringLiteral("游记换掉了占位页，先看列表"));
    if (journalCount() == 0) r.check(c.shown(QStringLiteral("journalEmptyPage")), QStringLiteral("没有游记时是空状态"));

    // ---- 什么都没写就离开：数据没动 ----
    const Value before = store->data();
    const int undo0 = app->undoCount();
    c.click(QStringLiteral("journalNew"));
    r.check(waitFor([&] { return c.shown(QStringLiteral("journalEditor")); }, 1000) && nav->params().value(QStringLiteral("edit")).toBool(), QStringLiteral("写一篇：进编辑"));
    goList();
    pause(100);
    r.check(store->data() == before && !store->isDirty() && app->undoCount() == undo0, QStringLiteral("什么都没写就离开：数据完全没动，撤销里也没多一步"));

    // ---- 写 ----
    c.click(QStringLiteral("journalNew"));
    waitFor([&] { return c.shown(QStringLiteral("journalEditor")); }, 1000);
    QQuickItem *page = c.item(QStringLiteral("journalPage"));
    QQuickItem *edit = c.item(QStringLiteral("journalEdit"));
    const QString jid = nav->params().value(QStringLiteral("id")).toString();
    edit->setProperty("text", QStringLiteral("第一段。\n\n## 小标题\n**加粗**的字"));
    r.check(waitFor([&] { return journalCount() == int(before[QStringLiteral("journal")].asArray().size()) + 1; }, 2000), QStringLiteral("写了字（停手）才进数据"));
    r.check(!j->entry(jid).isEmpty(), QStringLiteral("按这个 id 存的"));
    QQuickItem *preview = c.item(QStringLiteral("journalPreview"));
    r.check(preview && visibleChain(preview), QStringLiteral("右边有预览"));
    r.check(c.saved(), QStringLiteral("自动保存了"));

    // 标题、日期
    page->setProperty("edTitle", QStringLiteral("自检的游记"));
    page->setProperty("edDate", QStringLiteral("2031-05-06"));
    QMetaObject::invokeMethod(page, "saveNow");
    r.check(j->entry(jid).value(QStringLiteral("title")).toString() == QStringLiteral("自检的游记") && j->entry(jid).value(QStringLiteral("date")).toString() == QLatin1String("2031-05-06"), QStringLiteral("标题、日期存了"));

    // 工具栏
    QMetaObject::invokeMethod(edit, "select", Q_ARG(int, 0), Q_ARG(int, 3));
    c.click(QStringLiteral("journalTool-b"));
    r.check(edit->property("text").toString().startsWith(QStringLiteral("**第一段**")), QStringLiteral("工具栏：加粗选中的字"), edit->property("text").toString().left(10));
    r.check(c.shown(QStringLiteral("journalTool-file")) && !c.shown(QStringLiteral("journalTool-task")), QStringLiteral("工具栏有\"从电脑添加照片\"，没有勾选项"));

    // ---- R1：关联旅程，下面列出这趟的照片，点一下插入 ----
    QString planWithPhotos;
    for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
        if (!ph->planPhotos(str(p, "id")).isEmpty()) { planWithPhotos = str(p, "id"); break; }
    r.check(!c.shown(QStringLiteral("journalStrip")), QStringLiteral("没关联旅程时没有照片条"));
    if (!planWithPhotos.isEmpty()) {
        page->setProperty("edPlan", planWithPhotos);
        QMetaObject::invokeMethod(page, "refreshStrip");
        QMetaObject::invokeMethod(page, "saveNow");
        r.check(j->entry(jid).value(QStringLiteral("plan")).toString() == planWithPhotos, QStringLiteral("关联旅程存了"));
        r.check(waitFor([&] { return c.shown(QStringLiteral("journalStrip")); }, 800), QStringLiteral("R1：关联了旅程就有这趟的照片"));
        QQuickItem *thumb = nullptr;
        waitFor([&] { thumb = c.item(QStringLiteral("journalStripPhoto-0")); return visibleChain(thumb); }, 800);
        waitFrames(win);   // 编辑框变矮、照片条排好以后再点
        pause(100);
        const QString t0 = edit->property("text").toString();
        c.click(thumb);
        r.check(edit->property("text").toString().size() > t0.size() && edit->property("text").toString().contains(QStringLiteral("](photo:")), QStringLiteral("R1：点一下插入正文"));
        QQuickItem *toggle = c.item(QStringLiteral("journalStripToggle"));
        c.click(toggle);
        r.check(!c.shown(QStringLiteral("journalStripList")), QStringLiteral("R1：可以收起"));
        c.click(toggle);
        // 插入照片的对话框：这个旅程 / 全部
        c.click(QStringLiteral("journalTool-pic"));
        QQuickItem *dlg = c.item(QStringLiteral("journalPhotoPick"));
        r.check(waitFor([&] { return dlg && dlg->property("open").toBool(); }, 800), QStringLiteral("插入照片：打开选择框"));
        const int nPlan = dlg->property("shownRows").toList().size();
        dlg->setProperty("scope", QStringLiteral("all"));
        r.check(dlg->property("shownRows").toList().size() >= nPlan, QStringLiteral("可以看全部照片"), QStringLiteral("%1 → %2").arg(nPlan).arg(dlg->property("shownRows").toList().size()));
        dlg->setProperty("open", false);
        waitFor([&] { return !dlg->isVisible(); }, 1000);   // 淡出时遮罩还拦着鼠标
    } else r.info(QStringLiteral("数据里没有带照片的计划，R1 没测"));

    // ---- 完成 → 读 ----
    c.click(QStringLiteral("journalDone"));
    r.check(waitFor([&] { return c.shown(QStringLiteral("journalRead")); }, 1000), QStringLiteral("完成：进阅读"));
    QQuickItem *rt = c.item(QStringLiteral("journalReadTitle"));
    r.check(rt && rt->property("text").toString() == QStringLiteral("自检的游记"), QStringLiteral("阅读有标题"));
    QQuickItem *body = c.item(QStringLiteral("journalBody"));
    r.check(body && body->height() > 40, QStringLiteral("阅读有正文"), body ? QString::number(body->height()) : QString());
    r.check(planWithPhotos.isEmpty() || c.shown(QStringLiteral("journalPlanChip")), QStringLiteral("阅读有计划 chip"));
    c.click(QStringLiteral("journalEditBtn"));
    r.check(waitFor([&] { return c.shown(QStringLiteral("journalEditor")); }, 1000), QStringLiteral("编辑按钮回到编辑"));
    QQuickItem *titleInput = c.item(QStringLiteral("journalTitle"));
    r.check(titleInput && titleInput->property("text").toString() == QStringLiteral("自检的游记"), QStringLiteral("编辑框读回来了"));

    // ---- 列表、筛选 ----
    goList();
    r.check(waitFor([&] { return c.shown(QStringLiteral("journalCard-0")); }, 1000), QStringLiteral("列表有卡片"));
    const QVariantList rows = j->page().value(QStringLiteral("rows")).toList();
    r.check(rows.value(0).toMap().value(QStringLiteral("id")).toString() == jid, QStringLiteral("新的在前"), rows.value(0).toMap().value(QStringLiteral("title")).toString());
    r.check(rows.value(0).toMap().value(QStringLiteral("text")).toString().size() <= 110, QStringLiteral("摘要不超过 110 字"));
    if (!planWithPhotos.isEmpty()) {
        j->setFilter(planWithPhotos);
        r.check(j->page().value(QStringLiteral("rows")).toList().size() >= 1, QStringLiteral("按旅程筛选"));
        c.click(QStringLiteral("journalNew"));
        waitFor([&] { return c.shown(QStringLiteral("journalEditor")); }, 800);
        r.check(page->property("edPlan").toString() == planWithPhotos, QStringLiteral("筛选着写一篇：预关联这个旅程"));
        goList();
        j->setFilter(QStringLiteral("all"));
    }
    c.click(QStringLiteral("journalCard-0"));
    r.check(waitFor([&] { return c.shown(QStringLiteral("journalRead")); }, 1000), QStringLiteral("点卡片进阅读"));
    c.saved();

    // ---- 写了又全删掉：离开时删掉 ----
    nav->go(QStringLiteral("journal"), { { QStringLiteral("id"), QStringLiteral("jn_st_empty") }, { QStringLiteral("edit"), true } });
    waitFor([&] { return c.shown(QStringLiteral("journalEditor")); }, 800);
    const int n1 = journalCount();
    edit->setProperty("text", QStringLiteral("写点东西"));
    waitFor([&] { return journalCount() == n1 + 1; }, 2000);
    edit->setProperty("text", QString());
    waitFor([&] { return j->entry(QStringLiteral("jn_st_empty")).value(QStringLiteral("body")).toString().isEmpty(); }, 2000);
    goList();
    r.check(waitFor([&] { return journalCount() == n1; }, 800), QStringLiteral("写了又全删掉：离开时删掉"));

    // ---- 删除、撤销 ----
    c.saved();
    const Value hasOne = store->data();
    nav->go(QStringLiteral("journal"), { { QStringLiteral("id"), jid }, { QStringLiteral("edit"), true } });
    waitFor([&] { return c.shown(QStringLiteral("journalEditor")); }, 800);
    c.click(QStringLiteral("journalDelete"));
    QQuickItem *delDlg = c.item(QStringLiteral("journalDeleteDialog"));
    r.check(waitFor([&] { return delDlg && delDlg->property("open").toBool(); }, 800), QStringLiteral("删除先问"));
    QMetaObject::invokeMethod(delDlg, "chosen", Q_ARG(QString, QStringLiteral("ok")));
    r.check(waitFor([&] { return j->entry(jid).isEmpty() && nav->params().isEmpty(); }, 800), QStringLiteral("删掉后回列表"));
    pause(120);
    r.check(j->entry(jid).isEmpty(), QStringLiteral("没有被编辑框存回去"));
    c.saved();
    app->undo();
    c.saved();
    r.check(noTime(store->data()) == noTime(hasOne), QStringLiteral("撤销：回来了，完全一样"));

    // ---- 计划页的入口 ----
    if (!planWithPhotos.isEmpty()) {
        QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, planWithPhotos), Q_ARG(QVariant, -1));
        waitFor([&] { return nav->page() == QLatin1String("trip"); }, 1000);
        QQuickItem *panel = c.item(QStringLiteral("planPage"));
        QMetaObject::invokeMethod(panel, "openTab", Q_ARG(QVariant, QStringLiteral("journal")));
        r.check(waitFor([&] { return c.shown(QStringLiteral("journalWrite")); }, 1000), QStringLiteral("游记标签有写一篇"));
        QQuickItem *row = nullptr;
        r.check(waitFor([&] { row = c.item(QStringLiteral("journalRow")); return visibleChain(row); }, 800), QStringLiteral("游记标签列出这一篇"));
        c.click(row);
        r.check(waitFor([&] { return nav->page() == QLatin1String("journal") && nav->params().value(QStringLiteral("id")).toString() == jid; }, 1000), QStringLiteral("点一行打开全文"));
        nav->back();
        waitFor([&] { return nav->page() == QLatin1String("trip"); }, 1000);
        r.check(waitFor([&] { return c.shown(QStringLiteral("journalWrite")); }, 1000), QStringLiteral("退回来还在游记标签"));
        waitStill(panel, 1000);
        c.click(QStringLiteral("journalWrite"));
        r.check(waitFor([&] { return nav->page() == QLatin1String("journal") && nav->params().value(QStringLiteral("plan")).toString() == planWithPhotos; }, 1000), QStringLiteral("写一篇：预关联这个计划"));
        waitFor([&] { return c.shown(QStringLiteral("journalEditor")); }, 800);
        r.check(c.shown(QStringLiteral("journalStrip")), QStringLiteral("写一篇：直接列出这趟的照片"));
        nav->back();
        waitFor([&] { return nav->page() == QLatin1String("trip"); }, 1000);
        nav->go(QStringLiteral("plan"));
    }

    // 收尾：删掉自检写的那篇
    j->remove(jid);
    c.saved();
    QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("map")));
    waitFor([&] { return !c.shown(QStringLiteral("journalPage")); }, 1000);
    r.check(j->entry(jid).isEmpty(), QStringLiteral("收尾：自检的游记删掉了"));
}

} // namespace tp::app::selftest
