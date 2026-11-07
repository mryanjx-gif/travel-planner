// 自检：0.11.2 日历（年、月、翻页、弹出卡、拖动和撤销的往返、撞期、隐藏、"还没定日期"）、游记（列表、写、读、空游记、R1、计划页的入口）。
// 会改数据，都在数据副本上做；拖动和撤销要逐字节回到原来的样子（只有 savedAt 例外）。
#include "selftest_util.h"

#include "appcontroller.h"
#include "calendarcontroller.h"
#include "datastore.h"
#include "framestats.h"
#include "journalcontroller.h"
#include "jsonio.h"
#include "mapcontroller.h"
#include "navhistory.h"
#include "perf.h"
#include "photocontroller.h"
#include "plancal.h"
#include "planpagecontroller.h"
#include "plancontroller.h"
#include "plans.h"
#include "settings.h"

#include <QDate>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <cmath>

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
    void click(QQuickItem *it) const { if (!it) return; waitStill(it, 1000); const QPointF p = center(it); press(p); release(p); pause(100); }
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
// 两份数据第一处不一样的路径，失败时写进报告（查"撤销后不一样"用）
QString firstDiff(const Value &a, const Value &b, const QString &path = QStringLiteral("$"))
{
    if (a == b) return {};
    if (a.isObject() && b.isObject()) {
        for (const auto &m : a.asObject().members()) {
            const Value *o = b.asObject().find(m.first);
            if (!o) return path + QStringLiteral(".") + m.first + QStringLiteral(" 另一边没有");
            const QString d = firstDiff(m.second, *o, path + QStringLiteral(".") + m.first);
            if (!d.isEmpty()) return d;
        }
        return path + QStringLiteral(" 另一边多了键");
    }
    if (a.isArray() && b.isArray()) {
        if (a.asArray().size() != b.asArray().size()) return path + QStringLiteral(" 长度 %1 / %2").arg(a.asArray().size()).arg(b.asArray().size());
        for (size_t k = 0; k < a.asArray().size(); ++k) {
            const QString d = firstDiff(a.asArray()[k], b.asArray()[k], path + QStringLiteral("[%1]").arg(k));
            if (!d.isEmpty()) return d;
        }
    }
    const auto show = [](const Value &v) { return v.isNumber() ? QString::number(v.toNumber(), 'f', 0) : v.toString(); };
    return path + QStringLiteral("：") + show(a) + QStringLiteral(" / ") + show(b);
}

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

    // ---- 0.12.10 页头并进顶栏：宽窗口时切换控件和 ⋯ 同一行、不压着它，整页顶到窗口顶 ----
    {
        QQuickItem *dots = c.item(QStringLiteral("dotsPill")), *prev = c.item(QStringLiteral("calPrev")), *page = c.item(QStringLiteral("calendarPage"));
        const bool sameRow = dots && prev && qAbs(c.center(dots).y() - c.center(prev).y()) < 3;
        const bool clear = dots && prev && prev->mapToScene(QPointF(0, 0)).x() >= dots->mapToScene(QPointF(dots->width(), 0)).x();
        r.check(sameRow && clear, QStringLiteral("日历的切换控件和顶栏 ⋯ 同一行，不重叠"));
        r.check(page && page->mapToScene(QPointF(0, 0)).y() < 1, QStringLiteral("整页从窗口顶部开始"));
    }

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

// 0.11.3：周视图（拖动和撤销的往返，一周从周一、周日开始都走一遍）、一周起始、季节表、去过、斋月，开头修的两项
void runCalendarWeek(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("日历（0.11.3）：周视图、一周起始、季节表、去过、斋月"));
    data::DataStore *store = app->store();
    NavHistory *nav = navCtl();
    PlanController *pc = plansCtl();
    auto *cal = ctxObject<CalendarController>(win, "calendar");
    if (!r.check(cal, QStringLiteral("拿到日历控制器"))) return;
    Ctx c{ win, app, store, nav };
    const auto goPage = [&](const QString &p) { QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, p)); };
    goPage(QStringLiteral("calendar"));
    waitFor([&] { return c.shown(QStringLiteral("calendarPage")); }, 2000);
    QString pid;
    for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
        if (plans::statusOf(p) == QLatin1String("planning") && plans::okDate(str(p, "startDate")) && plans::datedBookings(p) > 0) { pid = str(p, "id"); break; }
    if (!r.check(!pid.isEmpty(), QStringLiteral("数据里有定了日期、带交通住宿的计划"))) return;
    const Value before = store->data();
    const Value p0 = c.plan(pid);
    const QString start0 = str(p0, "startDate"), end0 = str(p0, "endDate");
    const auto weekMap = [&] { return cal->info().value(QStringLiteral("week")).toMap(); };

    // ---- 月视图点日期跳到那周 ----
    cal->setView(QStringLiteral("month"));
    cal->goTo(start0.left(4).toInt(), start0.mid(5, 2).toInt());
    QQuickItem *num = nullptr;
    waitFor([&] { num = c.item(QStringLiteral("calDayNum-") + start0); return visibleChain(num); }, 1500);
    waitFrames(win);
    c.click(num);
    r.check(waitFor([&] { return cal->view() == QLatin1String("week"); }, 800) && cal->weekFirst() == plans::weekStartOf(start0, 1), QStringLiteral("月视图点日期：跳到那一周"), cal->weekFirst());
    r.check(noTime(store->data()) == noTime(before), QStringLiteral("跳过去不改数据"));
    QQuickItem *flick = c.item(QStringLiteral("calWeekFlick"));
    r.check(waitFor([&] { return flick && flick->property("contentY").toDouble() > 0; }, 800), QStringLiteral("时间轴滚到最早的事件或 7 点附近"), flick ? flick->property("contentY").toString() : QString());
    QQuickItem *weekTop = c.item(QStringLiteral("calWeekTop")), *weekView = c.item(QStringLiteral("calWeekView"));
    r.check(weekTop && weekView && weekTop->height() <= weekView->height() * 0.4 + 1 && flick->height() >= weekView->height() * 0.4,
            QStringLiteral("全天区不超过周视图的 40%，时间轴留得够"), weekTop && weekView ? QStringLiteral("%1 / %2 / %3").arg(weekTop->height()).arg(flick->height()).arg(weekView->height()) : QString());
    // 0.12.10 事件多时全天区被裁一半：滚轮要能滚到下面。开发数据里 2026-12-12 那周放不下
    const QString weekBack = cal->weekFirst();
    cal->showWeek(QStringLiteral("2026-12-12"));
    if (weekTop && waitFor([&] { return weekTop->property("contentHeight").toDouble() > weekTop->height(); }, 1000)) {
        const QPointF at = weekTop->mapToScene(QPointF(weekTop->width() / 2, weekTop->height() / 2));
        weekTop->setProperty("contentY", 0);
        QWindowSystemInterface::handleWheelEvent(win, at, win->mapToGlobal(at), QPoint(), QPoint(0, -120));
        r.check(waitFor([&] { return weekTop->property("contentY").toDouble() > 0; }, 1000), QStringLiteral("全天区放不下时滚轮能滚"), weekTop->property("contentY").toString());
        weekTop->setProperty("contentY", 0);
    } else r.info(QStringLiteral("这一周全天区放得下，滚轮没测"));
    cal->showWeek(weekBack);
    r.info(QStringLiteral("一小时 %1 px（44 px 乘字号比例）").arg(flick ? flick->property("contentHeight").toDouble() / 24 : 0));

    // ---- 翻页：一周一周 ----
    const QString ws0 = cal->weekFirst();
    c.click(QStringLiteral("calNext"));
    r.check(cal->weekFirst() == plans::addDays(ws0, 7), QStringLiteral("周视图 › 翻 7 天"), cal->weekFirst());
    c.click(QStringLiteral("calPrev"));
    r.check(cal->weekFirst() == ws0, QStringLiteral("‹ 翻回来"));

    // ---- 拖动和撤销：周一、周日开始各走一遍（必须过）----
    for (int ws : { 1, 0 }) {
        cal->setWeekStart(ws);
        const QString tag = ws == 1 ? QStringLiteral("周一开始") : QStringLiteral("周日开始");
        cal->showWeek(start0);
        r.check(cal->weekFirst() == plans::weekStartOf(start0, ws) && weekMap().value(QStringLiteral("days")).toList().value(0).toMap().value(QStringLiteral("wd")).toInt() == ws % 7,
                tag + QStringLiteral("：这一周的第一天对"), cal->weekFirst());
        QQuickItem *bar = nullptr;
        waitFor([&] { bar = c.item(QStringLiteral("calWeekBar-") + pid); return visibleChain(bar); }, 1500);
        waitFrames(win);
        QQuickItem *cols = c.item(QStringLiteral("calWeekCols"));
        if (!r.check(bar && cols, tag + QStringLiteral("：有横条"))) continue;
        const double colW = cols->width() / 7;
        const int undo0 = app->undoCount();
        // 按住横条最左边一格（那天 = 计划开始或这周第一天），往右 2 列
        const QPointF from = bar->mapToScene(QPointF(colW / 2 - 3, bar->height() / 2));
        // 横条在周末那两列时往左拖（往右就拖到右边的列表上了）
        const int step = bar->mapToItem(cols, QPointF(0, 0)).x() > colW * 4 ? -2 : 2;
        c.dragTo(from, from + QPointF(colW * step, 200), [&] {
            QQuickItem *when = c.item(QStringLiteral("calGhostWhen"));
            r.check(c.shown(QStringLiteral("calGhost")) && when && !when->property("text").toString().isEmpty(), tag + QStringLiteral("：拖到时间轴上也有影子和落点日期"), when ? when->property("text").toString() : QString());
        });
        const Value moved = c.plan(pid);
        r.check(str(moved, "startDate") == plans::addDays(start0, step) && str(moved, "endDate") == plans::addDays(end0, step), tag + QStringLiteral("：拖 2 列，开始和结束都挪 2 天"), str(moved, "startDate") + QStringLiteral(" ~ ") + str(moved, "endDate"));
        r.check(moved[QStringLiteral("bookings")] == p0[QStringLiteral("bookings")], tag + QStringLiteral("：交通住宿不跟"));
        r.check(c.saved() && app->undoCount() == std::min(undo0 + 1, data::UndoStack::kMaxSteps), tag + QStringLiteral("：马上保存，撤销里只多一步"));
        r.check(onDisk(store) == noTime(store->data()), tag + QStringLiteral("：磁盘上是挪过的"));
        c.click(QStringLiteral("calNoticeUndo"));
        r.check(waitFor([&] { return !store->isDirty() && noTime(store->data()) == noTime(before); }, 3000), tag + QStringLiteral("：一步撤销，数据和原来完全一样（含 updatedAt）"), firstDiff(noTime(store->data()), noTime(before)));
        r.check(onDisk(store) == noTime(before), tag + QStringLiteral("：磁盘上也和原来完全一样"));
        // 月视图：这一格的日期跟着周首走，拖过去落点对，撤销也回来
        cal->setView(QStringLiteral("month"));
        cal->goTo(start0.left(4).toInt(), start0.mid(5, 2).toInt());
        const QVariantList weeks = cal->info().value(QStringLiteral("month")).toMap().value(QStringLiteral("weeks")).toList();
        r.check(!weeks.isEmpty() && QDate::fromString(weeks[0].toMap().value(QStringLiteral("start")).toString(), Qt::ISODate).dayOfWeek() % 7 == ws % 7, tag + QStringLiteral("：月视图每周从这天开始"));
        QQuickItem *cellA = nullptr, *cellB = nullptr, *mbar = nullptr;
        const QString toDay = plans::addDays(start0, 3);
        waitFor([&] { cellA = c.item(QStringLiteral("calDay-") + start0); cellB = c.item(QStringLiteral("calDay-") + toDay); return visibleChain(cellA) && visibleChain(cellB); }, 1000);
        waitFor([&] { mbar = nullptr; for (int w = 0; w < 6 && !mbar; ++w) mbar = c.item(QStringLiteral("calBar-%1-%2").arg(pid).arg(w)); return visibleChain(mbar); }, 800);
        waitFrames(win);
        if (cellA && cellB && mbar) {
            c.dragTo(QPointF(cellA->mapToScene(QPointF(cellA->width() / 2, 0)).x(), mbar->mapToScene(QPointF(0, mbar->height() / 2)).y()), c.center(cellB));
            r.check(str(c.plan(pid), "startDate") == toDay, tag + QStringLiteral("：月视图拖 3 天，按住哪天，那天落到鼠标下"), str(c.plan(pid), "startDate"));
            c.saved();
            app->undo();
            c.saved();
            r.check(noTime(store->data()) == noTime(before) && onDisk(store) == noTime(before), tag + QStringLiteral("：月视图拖动撤销后内存和磁盘都完全一样"));
        }
    }
    // 没定日期的拖进周视图（从周日开始时）：放好、状态回得来
    {
        cal->setWeekStart(0);
        cal->setSideOpen(true);
        cal->showWeek(start0);
        QString wid;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
            if (plans::statusOf(p) == QLatin1String("wish") && str(p, "startDate").isEmpty() && str(p, "endDate").isEmpty()) { wid = str(p, "id"); break; }
        QQuickItem *row = wid.isEmpty() ? nullptr : c.item(QStringLiteral("calUndated-") + wid);
        waitFor([&] { row = c.item(QStringLiteral("calUndated-") + wid); return visibleChain(row); }, 800);
        c.revealInSide(row);
        QQuickItem *col = c.item(QStringLiteral("calWeekCol-") + plans::addDays(cal->weekFirst(), 4));
        QQuickItem *flick2 = c.item(QStringLiteral("calWeekFlick"));
        if (row && col && flick2) {
            const QPointF to(col->mapToScene(QPointF(col->width() / 2, 0)).x(), flick2->mapToScene(QPointF(0, flick2->height() / 2)).y());
            c.dragTo(c.center(row), to);
            r.check(str(c.plan(wid), "startDate") == plans::addDays(cal->weekFirst(), 4), QStringLiteral("没定日期的拖到周视图某一列：从那天开始"), str(c.plan(wid), "startDate"));
            c.saved();
            app->undo();
            c.saved();
            r.check(noTime(store->data()) == noTime(before) && onDisk(store) == noTime(before), QStringLiteral("撤销后内存和磁盘都完全一样（状态也回来）"));
        }
        // 改周首：这一周对齐到新的周首，不改数据
        const QString sun = cal->weekFirst();
        cal->setWeekStart(1);
        r.check(cal->weekFirst() == plans::weekStartOf(sun, 1), QStringLiteral("设置里改周首：对齐到新的周首"), cal->weekFirst());
        r.check(noTime(store->data()) == noTime(before), QStringLiteral("改周首不改 travel.json 的内容"));
        cal->setWeekStart(0);
        QTemporaryDir tmp;
        cal->reopenForTest(tmp.filePath(QStringLiteral("s.json")));
        r.check(cal->weekStart() == 0, QStringLiteral("一周起始记在设置里，读回来还是周日"));
        Settings back;
        back.load(tmp.filePath(QStringLiteral("s.json")));
        r.check(back.value(QStringLiteral("weekStart")).toNumber() == 0, QStringLiteral("设置的新键 weekStart"));
        cal->setWeekStart(1);
    }

    // ---- 周视图的内容：路线的第 3 天那一周 ----
    {
        const QVariantMap demo = pc->demo();
        const QString today = demo.value(QStringLiteral("today")).toString();
        pc->setTodayOverride(today);
        cal->showWeek(today);
        waitFor([&] { return weekMap().value(QStringLiteral("start")).toString() == cal->weekFirst(); }, 800);
        const QVariantMap w = weekMap();
        r.check(!w.value(QStringLiteral("bars")).toList().isEmpty(), QStringLiteral("全天区有横条"), QString::number(w.value(QStringLiteral("bars")).toList().size()));
        int stops = 0, events = 0;
        for (const QVariant &v : w.value(QStringLiteral("stops")).toList()) stops += int(v.toList().size());
        for (const QVariant &v : w.value(QStringLiteral("events")).toList()) events += int(v.toList().size());
        r.check(stops > 0, QStringLiteral("每天写着在哪一站"), QString::number(stops));
        r.info(QStringLiteral("这一周 %1 个站、%2 个有时间的事件").arg(stops).arg(events));
        QQuickItem *todayCol = c.item(QStringLiteral("calWeekCol-") + today);
        bool nowLine = false;
        if (todayCol) for (QQuickItem *k : todayCol->childItems()) nowLine = nowLine || (k->objectName() == QLatin1String("calWeekNow") && k->isVisible());
        r.check(nowLine, QStringLiteral("今天那一列有当前时间线"));
        pc->setTodayOverride(QString());
    }

    // ---- 季节表 ----
    cal->setAllSeasonGroups(false);
    cal->setView(QStringLiteral("season"));
    r.check(waitFor([&] { return c.shown(QStringLiteral("calSeasonList")); }, 1500), QStringLiteral("季节表是日历第 4 个视图"));
    r.check(!c.shown(QStringLiteral("calPrev")) && !c.shown(QStringLiteral("calSide")), QStringLiteral("季节表没有翻页和右边的列表"));
    const auto seasonRows = [&] { return cal->info().value(QStringLiteral("season")).toMap().value(QStringLiteral("rows")).toList(); };
    r.check(waitFor([&] { return cal->info().value(QStringLiteral("season")).toMap().value(QStringLiteral("ready")).toBool(); }, 15000), QStringLiteral("去过：后台算好了"));
    const QVariantList all = seasonRows();
    int nRows = 0, nHead = 0, nVisited = 0;
    QStringList visitedNames;
    for (const QVariant &v : all) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("head")).toBool()) { ++nHead; continue; }
        ++nRows;
        if (m.value(QStringLiteral("visited")).toBool()) { ++nVisited; visitedNames << m.value(QStringLiteral("key")).toString(); }
    }
    r.check(nRows >= 92 && nHead >= 9, QStringLiteral("第一次全部展开：区域分组、92 行都在（俄罗斯可能在两组）"), QStringLiteral("%1 行，%2 组").arg(nRows).arg(nHead));
    r.check(nVisited > 0, QStringLiteral("有去过的国家打勾"), visitedNames.join(QStringLiteral("、")));
    QQuickItem *list = c.item(QStringLiteral("calSeasonList"));
    QQuickItem *content = list ? list->property("contentItem").value<QQuickItem *>() : nullptr;
    const int built = content ? int(content->childItems().size()) : -1;
    r.check(built > 0 && built < 60, QStringLiteral("只建屏幕上的行"), QString::number(built));
    // 滚动的帧时间（UHD 620 上量，写进版本记录）
    if (list) {
        FrameStats *fs = frameStats();
        const double maxY = list->property("contentHeight").toDouble() - list->height();
        const double t0 = perf::nowMs();
        QElapsedTimer t;
        t.start();
        QTimer timer;
        timer.setTimerType(Qt::PreciseTimer);
        timer.setInterval(16);
        QObject::connect(&timer, &QTimer::timeout, &timer, [&] { list->setProperty("contentY", maxY * (0.5 - 0.5 * std::cos(t.elapsed() / 3000.0 * 2 * M_PI))); });
        timer.start();
        waitFor([&] { return t.elapsed() >= 3000; }, 6000);
        timer.stop();
        const double t1 = perf::nowMs();
        r.info(QStringLiteral("滚季节表 %1 行：%2").arg(nRows).arg(fs ? fs->summary(t0 + 100, t1).text() : QStringLiteral("（没有每帧计时）")));
        list->setProperty("contentY", 0);
        waitFrames(win);
    }
    // 折叠：记住
    {
        const QString g = all[0].toMap().value(QStringLiteral("group")).toString();
        QQuickItem *head = nullptr;
        waitFor([&] { head = c.item(QStringLiteral("calSeasonGroup-") + g); return visibleChain(head); }, 800);
        c.click(head);
        r.check(waitFor([&] { return seasonRows().size() < all.size(); }, 800), QStringLiteral("点区域：折叠"), QString::number(seasonRows().size()));
        QTemporaryDir tmp;
        cal->reopenForTest(tmp.filePath(QStringLiteral("s.json")));
        r.check(cal->view() == QLatin1String("season") && seasonRows().size() < all.size(), QStringLiteral("折叠和视图都记住了"));
        cal->setAllSeasonGroups(false);
        r.check(seasonRows().size() == all.size(), QStringLiteral("全部展开"));
        cal->setAllSeasonGroups(true);
        r.check(seasonRows().size() == nHead, QStringLiteral("全部收起"));
        cal->setAllSeasonGroups(false);
    }
    // 相关计划：点开
    {
        QString chipPlan;
        for (const QVariant &v : all) { const QVariantList ps = v.toMap().value(QStringLiteral("plans")).toList(); if (!ps.isEmpty()) { chipPlan = ps[0].toMap().value(QStringLiteral("id")).toString(); break; } }
        r.check(!chipPlan.isEmpty(), QStringLiteral("有行写着相关计划"), chipPlan);
    }
    // 去过：完成一个计划就重算，只改名不重算（缓存）
    {
        QString wishId;
        QString rowKey;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray()) {
            const QString ref = str(p, "seasonRef");
            if (ref.isEmpty() || plans::statusOf(p) == QLatin1String("completed") || visitedNames.contains(ref) || plans::validStops(p).isEmpty()) continue;
            wishId = str(p, "id"); rowKey = ref; break;
        }
        const auto rowVisited = [&](const QString &key) {
            for (const QVariant &v : seasonRows()) if (v.toMap().value(QStringLiteral("key")).toString() == key) return v.toMap().value(QStringLiteral("visited")).toBool();
            return false;
        };
        store->modify([&](Value &d) { plans::mutPlan(d, pid)->mutObject().set(QStringLiteral("name"), Value(QStringLiteral("改个名字"))); });
        app->noteChanged(false);
        pause(150);
        r.check(mapc->visitedCodes() != nullptr, QStringLiteral("只改计划名：去过不重算，马上有"));
        store->modify([&before](Value &d) { d = before; });
        app->noteChanged(false);
        pause(150);
        if (!wishId.isEmpty()) {
            store->modify([&](Value &d) { plans::mutPlan(d, wishId)->mutObject().set(QStringLiteral("status"), Value(QStringLiteral("completed"))); });
            app->noteChanged(false);
            const bool on = waitFor([&] { return rowVisited(rowKey); }, 5000);
            r.info(QStringLiteral("把「%1」（%2）标成已完成：%3").arg(str(c.plan(wishId), "name"), rowKey, on ? QStringLiteral("这一行打勾了") : QStringLiteral("这一行没打勾（站点不在这个国家？）")));
            store->modify([&before](Value &d) { d = before; });
            app->noteChanged(false);
            r.check(waitFor([&] { return !rowVisited(rowKey); }, 5000), QStringLiteral("改回去：勾也去掉"));
        }
    }
    // 斋月：今年一段；2030 年画两段（T1）
    {
        pc->setTodayOverride(QStringLiteral("2030-06-15"));
        const auto ramOf = [&] {
            for (const QVariant &v : seasonRows()) if (!v.toMap().value(QStringLiteral("ram")).toList().isEmpty()) return v.toMap();
            return QVariantMap();
        };
        r.check(waitFor([&] { return ramOf().value(QStringLiteral("ram")).toList().size() == 2; }, 1500), QStringLiteral("2030 年：标了斋月的行有两段"), ramOf().value(QStringLiteral("key")).toString());
        const QVariantList ram = ramOf().value(QStringLiteral("ram")).toList();
        if (ram.size() == 2) {
            r.check(ram[0].toMap().value(QStringLiteral("a")).toString() == QLatin1String("2030-01-05") && ram[1].toMap().value(QStringLiteral("b")).toString() == QLatin1String("2031-01-23"), QStringLiteral("日期对，跨年的写完整"), ram[1].toMap().value(QStringLiteral("a")).toString() + QStringLiteral(" ~ ") + ram[1].toMap().value(QStringLiteral("b")).toString());
            r.check(ram[1].toMap().value(QStringLiteral("r")).toDouble() == 1.0, QStringLiteral("跨年的画到这一年底"));
            const QString key = ramOf().value(QStringLiteral("key")).toString();
            QQuickItem *ramItem = nullptr;
            list->setProperty("contentY", 0);
            for (double y = 0; y < list->property("contentHeight").toDouble() && !ramItem; y += list->height() / 2) {
                list->setProperty("contentY", y);
                waitFrames(win);
                ramItem = c.item(QStringLiteral("calSeasonRam-%1-1").arg(key));
            }
            QQuickItem *txt = nullptr;
            if (ramItem) for (QQuickItem *k : ramItem->childItems()) if (k->objectName() == QLatin1String("calSeasonRamText")) txt = k;
            r.check(txt && txt->property("text").toString().contains(QStringLiteral("约")), QStringLiteral("界面写\"约\""), txt ? txt->property("text").toString() : QString());
            list->setProperty("contentY", 0);
        }
        pc->setTodayOverride(QString());
        r.check(c.shown(QStringLiteral("calSeasonRamNote")), QStringLiteral("图例说明按各国看月可能差一两天"));
    }
    cal->setView(QStringLiteral("year"));
    cal->goToday();

    // ---- 开头修的两项 ----
    {
        QQuickItem *panel = c.item(QStringLiteral("planPage"));
        QString other;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
            if (str(p, "id") != pid && !plans::validStops(p).isEmpty()) { other = str(p, "id"); break; }
        // 计划页游记标签打开全文（发同一个信号）→ 去地图 → 打开另一个计划：不留在游记标签
        QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, pid), Q_ARG(QVariant, -1));
        waitFor([&] { return nav->page() == QLatin1String("trip"); }, 1500);
        win->setProperty("tripTab", QStringLiteral("journal"));
        win->setProperty("tripTabPlan", pid);
        nav->go(QStringLiteral("journal"), {});
        waitFor([&] { return nav->page() == QLatin1String("journal"); }, 1000);
        goPage(QStringLiteral("map"));
        waitFor([&] { return nav->page() != QLatin1String("journal"); }, 1000);
        r.check(win->property("tripTab").toString().isEmpty(), QStringLiteral("从游记去了别处：tripTab 清掉"));
        win->setProperty("tripTab", QStringLiteral("journal"));
        win->setProperty("tripTabPlan", pid);
        QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, other), Q_ARG(QVariant, -1));
        r.check(waitFor([&] { return nav->page() == QLatin1String("trip") && panel->property("tab").toString() == QLatin1String("itinerary"); }, 1500), QStringLiteral("打开另一个计划：不跳到游记标签"), panel->property("tab").toString());
        // 攻略写的时候：标签那一行不被收起的页头压住
        QMetaObject::invokeMethod(panel, "openTab", Q_ARG(QVariant, QStringLiteral("guide")));
        QMetaObject::invokeMethod(panel, "guideBeginEdit");
        pause(500);
        waitFrames(win);
        QQuickItem *tabs = c.item(QStringLiteral("tripTabs")), *compact = c.item(QStringLiteral("tripCompact"));
        if (tabs && compact) {
            const double tabsTop = tabs->mapToScene(QPointF(0, 0)).y(), compactBottom = compact->mapToScene(QPointF(0, compact->height())).y();
            r.check(!compact->isVisible() || tabsTop >= compactBottom - 1, QStringLiteral("攻略写的时候：标签整行露在页头下面"), QStringLiteral("标签顶 %1，页头底 %2").arg(tabsTop).arg(compactBottom));
        }
        QMetaObject::invokeMethod(panel, "openTab", Q_ARG(QVariant, QStringLiteral("itinerary")));
        nav->back();
    }
    goPage(QStringLiteral("map"));
    waitFor([&] { return !c.shown(QStringLiteral("calendarPage")); }, 1000);
    c.saved();
    r.check(noTime(store->data()) == noTime(before), QStringLiteral("这一节结束：数据和开始时完全一样"));
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
