// 自检：0.10.1 交通住宿、消费、汇率——标签、弹出表单、费用同步和连带删除、不算两遍、退款、缺汇率、主货币、J1 没订住宿、J2 站点卡片、J3 一行快记、汇率的两个来源。
// 0.10.2 分账和账本（runSplit）：一个人时都不显示、平摊和按金额、结算、K1 复制、账本的数字和跳转。
// 0.11.5 同行人窗口（加、改名、删、一步撤销）、加人时自动平摊、单笔窗口的开关和每人金额：都用鼠标真点。
// 都在数据副本上做，最后把数据和主货币改回去。汇率不联网（setFetchForTest）。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "navhistory.h"
#include "plancontroller.h"
#include "jsonio.h"
#include "planmoney.h"
#include "planpagecontroller.h"
#include "plans.h"
#include "ratescontroller.h"
#include "translator.h"

#include <QClipboard>
#include <QDateTime>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
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
bool isShown(QQuickItem *it) { return it && it->isVisible() && it->opacity() > 0.5; }
void click(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return;
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    const QPointF g = win->mapToGlobal(p);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    pause(80);
}
QString text(QQuickItem *it) { return it ? it->property("text").toString() : QString(); }
// 往有焦点的输入框打字（选中的内容会被换掉）
void typeText(QQuickWindow *win, const QString &t)
{
    for (const QChar c : t) {
        QKeyEvent down(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        QCoreApplication::sendEvent(win, &down);
        QKeyEvent up(QEvent::KeyRelease, 0, Qt::NoModifier, QString(c));
        QCoreApplication::sendEvent(win, &up);
    }
    pause(30);
}
void fill(QQuickWindow *win, QQuickItem *field, const QString &t)
{
    click(win, field);
    if (field) QMetaObject::invokeMethod(field, "focusAll");
    typeText(win, t);
}
const Value *curPlan(data::DataStore *s, const QString &id) { return plans::planById(s->data(), id); }
void showExpense(QQuickItem *dlg, const QString &title)
{
    QVariantMap draft;
    draft.insert(QStringLiteral("title"), title);
    QMetaObject::invokeMethod(dlg, "show", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant(draft)));
}
// 弹框淡出的 0.22 秒里还挡着下面的点击
void waitGone(QQuickItem *dlg) { waitFor([&] { return !dlg->isVisible(); }, 1000); }
QVariantMap money(PlanPageController *page) { return page->info().value(QStringLiteral("money")).toMap(); }
} // namespace

void runMoney(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav, RatesController *rates)
{
    r.section(QStringLiteral("0.10.1 交通住宿、消费、汇率"));
    PlanController *pc = plansCtl();
    data::DataStore *store = app->store();
    QQuickItem *panel = item(win, QStringLiteral("planPage"));
    if (!r.check(pc && page && nav && panel && rates, QStringLiteral("计划页和汇率都在"))) return;

    // ---- 汇率：第一个来源 → 失败时第二个 → 都失败留着上次的 ----
    const QString oldMain = rates->main();
    const plans::Rates oldTable = rates->table();
    QStringList asked;
    int mode = 0;   // 0 Frankfurter 好；1 Frankfurter 坏；2 都坏；3 er-api 坏
    rates->setFetchForTest([&](const QUrl &url, std::function<void(int, const QByteArray &, int)> done) {
        asked << url.host();
        const bool first = url.host() == QLatin1String("api.frankfurter.dev");
        if (mode == 2 || (mode == 1 && first) || (mode == 3 && !first)) { done(0, {}, 5); return; }
        done(200, first ? QByteArrayLiteral(R"({"date":"2026-11-05","rates":{"AUD":0.2,"USD":0.14}})")   // 不写 base：换主货币后也照样收
                        : QByteArrayLiteral(R"({"result":"success","time_last_update_unix":1793923351,"rates":{"AUD":0.25,"USD":0.15,"VND":3500}})"), 0);
    });
    rates->resetForTest(QStringLiteral("CNY"), plans::Rates(), 0);
    rates->ensure();
    r.check(asked == QStringList{ QStringLiteral("api.frankfurter.dev") } && rates->date() == QLatin1String("2026-11-05"), QStringLiteral("汇率先问 Frankfurter"), asked.join(u','));
    rates->ensure();
    r.check(asked.size() == 1, QStringLiteral("20 小时内不再取"));
    asked.clear(); mode = 1;
    rates->resetForTest(QStringLiteral("CNY"), plans::Rates(), 0);
    rates->ensure();
    r.check(asked.size() == 2 && rates->table().table.contains(QStringLiteral("VND")) && !rates->failed(), QStringLiteral("Frankfurter 失败时用 open.er-api.com"), asked.join(u','));
    mode = 2;
    const QString keepDate = rates->date();
    rates->resetForTest(QStringLiteral("CNY"), rates->table(), 0);
    rates->ensure();
    r.check(rates->failed() && rates->date() == keepDate && !rates->table().table.isEmpty(), QStringLiteral("都失败时留着上次的表，并标记失败"));
    // 0.12.6 Frankfurter 成功但缺用到的货币：再问一次 open.er-api.com，只补缺的；20 小时内不重复
    mode = 0; asked.clear();
    rates->setUsedForTest({ QStringLiteral("VND") });
    rates->resetForTest(QStringLiteral("CNY"), plans::Rates(), 0);
    rates->ensure();
    r.check(asked.size() == 2 && qAbs(plans::perMain(rates->table(), QStringLiteral("CNY"), QStringLiteral("VND")) - 1.0 / 3500) < 1e-12 && qAbs(rates->table().table.value(QStringLiteral("AUD")) - 0.2) < 1e-9
                && rates->date() == QLatin1String("2026-11-05") && !rates->failed(), QStringLiteral("缺货币时补上，Frankfurter 的不动"), asked.join(u','));
    asked.clear();
    rates->ensure(); rates->fillMissing();
    r.check(asked.isEmpty(), QStringLiteral("补过 20 小时内不再问"), asked.join(u','));
    rates->refreshNow();
    r.check(asked == QStringList{ QStringLiteral("api.frankfurter.dev") } && rates->table().table.contains(QStringLiteral("VND")), QStringLiteral("现在更新：补过的留着，不再问第二个"), asked.join(u','));
    rates->setUsedForTest({ QStringLiteral("AUD") });
    asked.clear();
    rates->resetForTest(QStringLiteral("CNY"), plans::Rates(), 0);
    rates->ensure();
    r.check(asked.size() == 1, QStringLiteral("不缺就不问第二个"), asked.join(u','));
    rates->setUsedForTest({ QStringLiteral("VND") });
    asked.clear();
    mode = 3;
    rates->resetForTest(QStringLiteral("CNY"), plans::Rates(), 0);
    rates->ensure();
    const int triedOnce = asked.size();
    rates->fillMissing();
    r.check(triedOnce == 2 && asked.size() == 2 && !rates->failed() && !rates->table().table.contains(QStringLiteral("VND")) && !rates->table().table.isEmpty(), QStringLiteral("补失败不算更新失败，也不马上再试"), asked.join(u','));
    rates->setUsedForTest({});
    mode = 0;
    rates->resetForTest(QStringLiteral("CNY"), plans::Rates(), 0);
    rates->ensure();

    // ---- 打开计划、标签 ----
    pc->setTestAllStatuses(false);
    const QVariantMap demo = pc->demo();
    const QString id = demo.value(QStringLiteral("planId")).toString();
    if (!r.check(!id.isEmpty(), QStringLiteral("有带日期的路线"))) { rates->setFetchForTest({}); return; }
    pc->setTodayOverride(demo.value(QStringLiteral("today")).toString());
    const Value before = store->data();
    const QSize oldSize = win->size();
    if (win->width() < 1300) win->resize(1400, win->height());
    // 从干净的状态开始：这条计划没有交通住宿和消费、状态是计划中（J1 要）
    store->modify([&](Value &d) {
        Value *p = plans::mutPlan(d, id);
        p->mutObject().remove(QStringLiteral("bookings"));
        p->mutObject().remove(QStringLiteral("expenses"));
        p->mutObject().set(QStringLiteral("status"), Value(QStringLiteral("planning")));
        p->mutObject().set(QStringLiteral("currency"), Value(QStringLiteral("AUD")));
        p->mutObject().set(QStringLiteral("budget"), Value(1000.0));
    });
    QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
    waitStill(panel);
    r.check(nav->page() == QLatin1String("trip") && page->planId() == id, QStringLiteral("打开了计划页"));
    QQuickItem *tBook = item(win, QStringLiteral("tripTab-bookings")), *tBudget = item(win, QStringLiteral("tripTab-budget")), *tIt = item(win, QStringLiteral("tripTab-itinerary"));
    r.check(tBook && tBudget && tIt && panel->property("tab").toString() == QLatin1String("itinerary"), QStringLiteral("三个标签，默认在行程"));
    click(win, tBook);
    QQuickItem *bookTab = item(win, QStringLiteral("tripBookings"));
    r.check(panel->property("tab").toString() == QLatin1String("bookings") && isShown(bookTab) && !isShown(item(win, QStringLiteral("tripNav"))), QStringLiteral("点了到交通住宿，切换条收起"));
    r.check(item(win, QStringLiteral("tripCard0")) == nullptr || !item(win, QStringLiteral("tripCard0"))->isVisible(), QStringLiteral("别的标签里站点卡片不显示"));

    // ---- J1 ----
    const int allNights = page->info().value(QStringLiteral("nights")).toList().size();
    r.check(allNights > 0 && isShown(item(win, QStringLiteral("tripNights"))), QStringLiteral("没订住宿的晚上有提醒"), QStringLiteral("%1 晚").arg(allNights));

    // ---- 弹出表单：加一条住宿 ----
    QQuickItem *bookDlg = item(win, QStringLiteral("bookingDialog"));
    click(win, item(win, QStringLiteral("bookAdd-hotel")));
    r.check(bookDlg && bookDlg->property("open").toBool() && bookDlg->property("kind").toString() == QLatin1String("hotel"), QStringLiteral("点\"住宿\"弹出表单"));
    r.check(win->property("modalOpen").toBool(), QStringLiteral("表单算弹框（Esc、快捷键让开）"));
    pause(300);   // 等进场动画（卡片在缩放时点不准）
    const QVariantMap s0 = page->info().value(QStringLiteral("stops")).toList().first().toMap();
    QString arrive = s0.value(QStringLiteral("arriveDate")).toString(), depart = s0.value(QStringLiteral("departDate")).toString();
    if (arrive.isEmpty()) { arrive = demo.value(QStringLiteral("start")).toString(); depart = plans::addDays(arrive, 1); }   // 站点没写日期：用计划的
    bookDlg->setProperty("titleText", QStringLiteral("自检酒店"));
    bookDlg->setProperty("startDate", arrive);
    bookDlg->setProperty("startTime", QStringLiteral("1500"));
    bookDlg->setProperty("endDate", depart.isEmpty() ? arrive : depart);
    bookDlg->setProperty("cost", QStringLiteral("abc"));
    const Value atStart = store->data();
    click(win, item(win, QStringLiteral("bookOk")));
    r.check(bookDlg->property("open").toBool() && bookDlg->property("err").toString() == QLatin1String("badCost") && store->data() == atStart, QStringLiteral("费用不是数字：不关、不改数据"), QStringLiteral("open %1 err %2 same %3 btn %4").arg(bookDlg->property("open").toBool()).arg(bookDlg->property("err").toString()).arg(store->data() == atStart).arg(item(win, QStringLiteral("bookOk")) != nullptr));
    bookDlg->setProperty("cost", QStringLiteral("1,200"));
    click(win, item(win, QStringLiteral("bookOk")));
    r.check(!bookDlg->property("open").toBool(), QStringLiteral("保存后关掉"));
    waitGone(bookDlg);
    const Value *p = curPlan(store, id);
    const json::Array *bk = &(*p)[QStringLiteral("bookings")].asArray();
    r.check(bk->size() == 1 && (*bk)[0][QStringLiteral("cost")].toNumber() == 1200 && (*bk)[0][QStringLiteral("start")].toString() == arrive + QStringLiteral("T15:00"),
            QStringLiteral("写进了数据（1,200、时间规范成 15:00）"));
    const QString hotelId = bk->empty() ? QString() : (*bk)[0][QStringLiteral("id")].toString();
    pause(100);
    r.check(page->info().value(QStringLiteral("bookCount")).toInt() == 1, QStringLiteral("标签的计数变成 1"));
    r.check(page->info().value(QStringLiteral("nights")).toList().size() < allNights || arrive == depart, QStringLiteral("住了的晚上不再提醒"));
    r.check(bk->size() == 1 && !(*p)[QStringLiteral("expenses")].isArray(), QStringLiteral("没勾\"记到消费\"：不写消费记录"));

    // ---- 不算两遍：一条 linked 的交通 ----
    QVariantMap linked{ { QStringLiteral("kind"), QStringLiteral("flight") }, { QStringLiteral("title"), QStringLiteral("自检航班") }, { QStringLiteral("cost"), QStringLiteral("300") }, { QStringLiteral("toExp"), true } };
    r.check(page->saveBooking(QString(), linked).isEmpty(), QStringLiteral("加一条同时记到消费的机票"));
    QVariantMap m = money(page);
    r.check(qFuzzyCompare(m.value(QStringLiteral("total")).toDouble(), 1500.0) && qFuzzyCompare(m.value(QStringLiteral("bookTotal")).toDouble(), 1500.0) && m.value(QStringLiteral("expTotal")).toDouble() == 0,
            QStringLiteral("预订 1200 + 300 = 1500，记到消费的不算第二遍"), QString::number(m.value(QStringLiteral("total")).toDouble()));
    r.check(m.value(QStringLiteral("over")).toBool(), QStringLiteral("超预算有标记"));

    // ---- 消费标签 ----
    click(win, tBudget);
    r.check(isShown(item(win, QStringLiteral("tripBudget"))), QStringLiteral("点了到消费"));
    r.check(text(item(win, QStringLiteral("moneyTotal"))) == QLatin1String("AUD 1,500"), QStringLiteral("已花写对了"), text(item(win, QStringLiteral("moneyTotal"))));
    r.check(text(item(win, QStringLiteral("moneySplit"))).contains(QStringLiteral("1,500")), QStringLiteral("分开显示预订和另外记账"), text(item(win, QStringLiteral("moneySplit"))));
    r.check(isShown(item(win, QStringLiteral("moneyMainTotal"))) && text(item(win, QStringLiteral("moneyMainTotal"))).contains(QStringLiteral("CNY 7,500")), QStringLiteral("换算成主货币"), text(item(win, QStringLiteral("moneyMainTotal"))));
    r.check(text(item(win, QStringLiteral("moneyRates"))).contains(QStringLiteral("2026-11-05")), QStringLiteral("写明汇率日期"), text(item(win, QStringLiteral("moneyRates"))));

    // ---- 点"来自交通住宿"的一行：回到交通住宿、打开它的表单 ----
    QQuickItem *rowB = item(win, QStringLiteral("moneyRowBooking"));
    click(win, rowB);
    r.check(panel->property("tab").toString() == QLatin1String("bookings") && bookDlg->property("open").toBool() && bookDlg->property("bookingId").toString() == hotelId,
            QStringLiteral("点来自交通住宿的一行：到交通住宿，打开它的表单"), bookDlg->property("bookingId").toString());
    r.check(bookDlg->property("startTime").toString() == QLatin1String("15:00") && bookDlg->property("cost").toString() == QLatin1String("1200"), QStringLiteral("表单里是原来的内容"));
    bookDlg->setProperty("open", false);
    waitGone(bookDlg);

    // ---- 改费用同步消费；别的键保留 ----
    QString flightId;
    for (const Value &b : (*curPlan(store, id))[QStringLiteral("bookings")].asArray())
        if (b[QStringLiteral("kind")].toString() == QLatin1String("flight")) flightId = b[QStringLiteral("id")].toString();
    store->modify([&](Value &d) {
        for (Value &b : plans::mutPlan(d, id)->mutObject().find(QStringLiteral("bookings"))->mutArray())
            if (b[QStringLiteral("id")].toString() == flightId) b.mutObject().set(QStringLiteral("seat"), Value(QStringLiteral("12A")));
    });
    linked.insert(QStringLiteral("cost"), QStringLiteral("450"));
    linked.remove(QStringLiteral("toExp"));
    page->saveBooking(flightId, linked);
    const json::Array &ex1 = (*curPlan(store, id))[QStringLiteral("expenses")].asArray();
    r.check(ex1.size() == 1 && ex1[0][QStringLiteral("amount")].toNumber() == 450, QStringLiteral("改费用同步到对应的消费"));
    bool seatKept = false;
    for (const Value &b : (*curPlan(store, id))[QStringLiteral("bookings")].asArray()) if (b[QStringLiteral("seat")].toString() == QLatin1String("12A")) seatKept = true;
    r.check(seatKept, QStringLiteral("不认识的键原样保留"));

    // ---- 删除连带删消费，有提示 ----
    QQuickItem *dlgMsg = item(win, QStringLiteral("moneyDialog"));
    QVariantMap flightRow;
    for (const QVariant &dv : page->info().value(QStringLiteral("bookings")).toList())
        for (const QVariant &bv : dv.toMap().value(QStringLiteral("rows")).toList())
            if (bv.toMap().value(QStringLiteral("id")).toString() == flightId) flightRow = bv.toMap();
    QMetaObject::invokeMethod(dlgMsg, "ask", Q_ARG(QVariant, QStringLiteral("booking")), Q_ARG(QVariant, QVariant(flightRow)));
    r.check(dlgMsg->property("open").toBool() && dlgMsg->property("paragraphs").toStringList().size() == 2, QStringLiteral("删除时提示消费也会一起删"), dlgMsg->property("paragraphs").toStringList().join(u'|'));
    QMetaObject::invokeMethod(dlgMsg, "chosen", Q_ARG(QString, QStringLiteral("del")));
    r.check((*curPlan(store, id))[QStringLiteral("bookings")].asArray().size() == 1 && (*curPlan(store, id))[QStringLiteral("expenses")].asArray().empty(), QStringLiteral("删掉了，连带的消费也没了"));

    // ---- J2：站点卡片 ----
    waitGone(dlgMsg);
    click(win, tIt);
    int bookStop = -1;
    const QVariantList stopsNow = page->info().value(QStringLiteral("stops")).toList();
    for (int k = 0; k < stopsNow.size(); ++k) if (!stopsNow[k].toMap().value(QStringLiteral("books")).toList().isEmpty()) bookStop = k;
    QMetaObject::invokeMethod(panel, "jumpTo", Q_ARG(QVariant, bookStop));
    const QString cardName = QStringLiteral("tripCard%1").arg(bookStop);
    waitFor([&] { return item(win, cardName) != nullptr; }, 2000);
    r.check(bookStop >= 0 && isShown(findItem(item(win, cardName), QStringLiteral("tripStopBooks"))), QStringLiteral("那一站的卡片上有当天的住宿"), QStringLiteral("第 %1 站").arg(bookStop + 1));

    // ---- J3：此刻卡"记一笔" → 一行快记 ----
    nav->go(pc->mode());
    waitStill(panel);
    pc->setNextTrip(id, true);
    win->setProperty("nowOpen", true);
    waitFor([&] { return isShown(item(win, QStringLiteral("nowExpense"))); }, 2000);
    QQuickItem *nowBtn = item(win, QStringLiteral("nowExpense"));
    if (r.check(isShown(nowBtn), QStringLiteral("在路上时此刻卡有\"记一笔\""))) {
        waitStill(nowBtn);
        pause(300);
        const QPointF at = nowBtn->mapToScene(QPointF(nowBtn->width() / 2, nowBtn->height() / 2));
        click(win, nowBtn);
        waitStill(panel);
        r.check(nav->page() == QLatin1String("trip") && panel->property("tab").toString() == QLatin1String("budget"), QStringLiteral("点了到消费标签"),
                QStringLiteral("%1 %2 @%3,%4 now %5").arg(nav->page(), panel->property("tab").toString()).arg(at.x()).arg(at.y()).arg(win->property("nowOpen").toBool()));
    } else {
        QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
        waitStill(panel);
        click(win, item(win, QStringLiteral("tripTab-budget")));
    }
    QQuickItem *budget = item(win, QStringLiteral("tripBudget"));
    budget->setProperty("qTitle", QStringLiteral("午饭"));
    budget->setProperty("qAmount", QStringLiteral("x"));
    const Value beforeQuick = store->data();
    click(win, item(win, QStringLiteral("quickAdd")));
    r.check(store->data() == beforeQuick && budget->property("qBad").toBool(), QStringLiteral("金额不对：不记、标红"));
    budget->setProperty("qAmount", QStringLiteral("28"));
    click(win, item(win, QStringLiteral("quickAdd")));
    const json::Array &ex2 = (*curPlan(store, id))[QStringLiteral("expenses")].asArray();
    r.check(ex2.size() == 1 && ex2[0][QStringLiteral("amount")].toNumber() == 28 && ex2[0][QStringLiteral("date")].toString() == pc->today() && ex2[0][QStringLiteral("cat")].toString() == QLatin1String("food"),
            QStringLiteral("一行快记记下了，日期是今天，默认餐饮"));
    r.check(budget->property("qTitle").toString().isEmpty() && budget->property("qAmount").toString().isEmpty(), QStringLiteral("记完清空，接着记"));
    m = money(page);
    r.check(qFuzzyCompare(m.value(QStringLiteral("expTotal")).toDouble(), 28.0) && qFuzzyCompare(m.value(QStringLiteral("bookTotal")).toDouble(), 1200.0), QStringLiteral("另外记账 28、预订 1200"));

    // ---- 退款、外币、缺汇率 ----
    page->saveExpense(QString(), { { QStringLiteral("title"), QStringLiteral("退款") }, { QStringLiteral("amount"), QStringLiteral("-40") }, { QStringLiteral("cat"), QStringLiteral("ticket") }, { QStringLiteral("date"), pc->today() } });
    page->saveExpense(QString(), { { QStringLiteral("title"), QStringLiteral("美元") }, { QStringLiteral("amount"), QStringLiteral("14") }, { QStringLiteral("currency"), QStringLiteral("USD") }, { QStringLiteral("cat"), QStringLiteral("shop") }, { QStringLiteral("date"), pc->today() } });
    page->saveExpense(QString(), { { QStringLiteral("title"), QStringLiteral("没有汇率") }, { QStringLiteral("amount"), QStringLiteral("5") }, { QStringLiteral("currency"), QStringLiteral("ISK") }, { QStringLiteral("cat"), QStringLiteral("shop") }, { QStringLiteral("date"), pc->today() } });
    m = money(page);
    // 14 USD = 14 / 0.14 = 100 CNY = 100 × 0.2 = 20 AUD
    r.check(qAbs(m.value(QStringLiteral("total")).toDouble() - (1200 + 28 - 40 + 20)) < 0.01, QStringLiteral("退款减掉，外币折成计划币种"), QString::number(m.value(QStringLiteral("total")).toDouble()));
    r.check(m.value(QStringLiteral("missing")).toStringList() == QStringList{ QStringLiteral("ISK") } && isShown(item(win, QStringLiteral("moneyMissing"))) && text(item(win, QStringLiteral("moneyMissing"))).contains(QStringLiteral("ISK")),
            QStringLiteral("缺汇率时写明哪一种"), text(item(win, QStringLiteral("moneyMissing"))));
    pause(100);
    bool green = false;
    std::function<void(QQuickItem *)> scan = [&](QQuickItem *x) {
        if (x->objectName() == QLatin1String("moneyAmount") && x->property("text").toString().startsWith(QChar(0x2212)))
            green = x->property("color").value<QColor>().green() > x->property("color").value<QColor>().red();
        for (QQuickItem *c : x->childItems()) scan(c);
    };
    scan(budget);
    r.check(green, QStringLiteral("退款是绿色的负数"));

    // ---- 换主货币 ----
    asked.clear();
    rates->setMain(QStringLiteral("USD"));
    pause(50);
    r.check(asked.size() == 1 && rates->main() == QLatin1String("USD"), QStringLiteral("换主货币重新取表"), asked.join(u','));
    r.check(text(item(win, QStringLiteral("moneyMainTotal"))).startsWith(QStringLiteral("≈ USD")), QStringLiteral("页头跟着变"), text(item(win, QStringLiteral("moneyMainTotal"))));

    // ---- 预算表单 ----
    QQuickItem *bd = item(win, QStringLiteral("budgetDialog"));
    click(win, item(win, QStringLiteral("moneyBudgetBtn")));
    r.check(bd && bd->property("open").toBool() && bd->property("budget").toString() == QLatin1String("1000"), QStringLiteral("预算表单里是现在的值"));
    bd->setProperty("budget", QStringLiteral("-5"));
    click(win, item(win, QStringLiteral("budgetOk")));
    r.check(bd->property("open").toBool() && bd->property("bad").toBool(), QStringLiteral("预算不能是负数"));
    bd->setProperty("budget", QStringLiteral("5000"));
    bd->setProperty("cur", QStringLiteral("NZD"));
    click(win, item(win, QStringLiteral("budgetOk")));
    r.check((*curPlan(store, id))[QStringLiteral("budget")].toNumber() == 5000 && (*curPlan(store, id))[QStringLiteral("currency")].toString() == QLatin1String("NZD"), QStringLiteral("改了预算和币种"));

    // ---- 表单重新打开时内容是新的（上次的输入框还留着焦点也一样）----
    QQuickItem *exDlg = item(win, QStringLiteral("expenseDialog"));
    showExpense(exDlg, QStringLiteral("第一次"));
    r.check(text(findItem(exDlg, QStringLiteral("expTitle"))) == QStringLiteral("第一次") && exDlg->property("date").toString() == pc->today(), QStringLiteral("展开更多带过来内容，日期默认今天"));
    exDlg->setProperty("open", false);
    showExpense(exDlg, QStringLiteral("第二次"));
    r.check(text(findItem(exDlg, QStringLiteral("expTitle"))) == QStringLiteral("第二次"), QStringLiteral("再打开是新内容"), text(findItem(exDlg, QStringLiteral("expTitle"))));
    exDlg->setProperty("open", false);

    // ---- 收尾：数据和主货币改回去 ----
    nav->go(pc->mode());
    waitStill(panel);
    pc->setNextTrip(id, false);
    store->modify([&](Value &d) { d = before; });
    rates->setFetchForTest({});
    // 0.12.6 真的请求一次：用了 Frankfurter 没有的货币，补上以后能换算
    store->modify([&](Value &d) {
        plans::mutPlan(d, id)->mutObject().set(QStringLiteral("expenses"), json::parse(QStringLiteral(R"([{"id":"twd1","amount":100,"currency":"TWD","date":"2026-01-01"}])")));
    });
    rates->resetForTest(QStringLiteral("CNY"), plans::Rates(), 0);
    rates->refreshNow();
    const bool gotTwd = waitFor([&] { return !rates->busy() && rates->table().table.contains(QStringLiteral("TWD")); }, 30000);
    const double twd = plans::perMain(rates->table(), QStringLiteral("CNY"), QStringLiteral("TWD"));
    r.check(gotTwd && twd > 0.1 && twd < 0.5 && rates->src().contains(QLatin1String("er-api")), QStringLiteral("真实请求：缺 TWD 时补上了，换算对"), QStringLiteral("1 TWD = %1 CNY，来源 %2").arg(twd).arg(rates->src()));
    store->modify([&](Value &d) { d = before; });
    rates->resetForTest(oldMain, oldTable, 0);
    pc->setTodayOverride(QString());
    pc->setTestAllStatuses(true);
    win->setProperty("nowOpen", false);
    win->resize(oldSize);
    pause(150);
}

void runSplit(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav, RatesController *rates)
{
    r.section(QStringLiteral("0.10.2 分账、账本"));
    PlanController *pc = plansCtl();
    data::DataStore *store = app->store();
    QQuickItem *panel = item(win, QStringLiteral("planPage"));
    if (!r.check(pc && page && nav && panel && rates, QStringLiteral("计划页和汇率都在"))) return;
    const QString oldMain = rates->main();
    const plans::Rates oldTable = rates->table();
    rates->setFetchForTest([](const QUrl &, std::function<void(int, const QByteArray &, int)> done) {
        done(200, QByteArrayLiteral(R"({"date":"2026-11-05","rates":{"AUD":0.2,"USD":0.14}})"), 0);
    });
    rates->resetForTest(QStringLiteral("AUD"), plans::Rates(), 0);
    rates->ensure();

    pc->setTestAllStatuses(false);
    const QVariantMap demo = pc->demo();
    const QString id = demo.value(QStringLiteral("planId")).toString();
    if (!r.check(!id.isEmpty(), QStringLiteral("有带日期的路线"))) { rates->setFetchForTest({}); return; }
    pc->setTodayOverride(demo.value(QStringLiteral("today")).toString());
    const Value before = store->data();
    const QSize oldSize = win->size();
    if (win->width() < 1300) win->resize(1400, win->height());
    store->modify([&](Value &d) {
        Value *p = plans::mutPlan(d, id);
        for (const char *k : { "bookings", "expenses", "people", "budget" }) p->mutObject().remove(QString::fromLatin1(k));
        p->mutObject().set(QStringLiteral("currency"), Value(QStringLiteral("AUD")));
    });
    win->setProperty("tripTab", QStringLiteral("budget"));
    QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
    waitStill(panel);
    r.check(panel->property("tab").toString() == QLatin1String("budget"), QStringLiteral("打开到消费标签"));

    // ---- 一个人：什么都不显示；旧数据（没有 paidBy）算"我"的 ----
    page->saveExpense(QString(), { { QStringLiteral("title"), QStringLiteral("以前的") }, { QStringLiteral("amount"), QStringLiteral("30") }, { QStringLiteral("cat"), QStringLiteral("food") }, { QStringLiteral("date"), pc->today() } });
    r.check(!(*curPlan(store, id))[QStringLiteral("expenses")].asArray()[0].asObject().contains(QStringLiteral("paidBy")), QStringLiteral("没有同行人时不写 paidBy"));
    pause(60);
    r.check(!isShown(item(win, QStringLiteral("moneySettle"))) && !isShown(item(win, QStringLiteral("moneyPeople"))), QStringLiteral("没有同行人：不显示结算，也没有人名胶囊"));
    r.check(text(item(win, QStringLiteral("moneyPeopleBtn"))) == tp::tr("同行人 · 只有我"), QStringLiteral("按钮写\"只有我\""), text(item(win, QStringLiteral("moneyPeopleBtn"))));
    QQuickItem *dlg = item(win, QStringLiteral("expenseDialog"));
    showExpense(dlg, QStringLiteral("试"));
    pause(60);
    r.check(!isShown(findItem(dlg, QStringLiteral("expSplit"))) && isShown(findItem(dlg, QStringLiteral("expPeople"))), QStringLiteral("没有同行人时表单里没有分摊，有\"管理同行人\"按钮"));
    dlg->setProperty("open", false);
    waitGone(dlg);
    page->saveBooking(QString(), { { QStringLiteral("kind"), QStringLiteral("train") }, { QStringLiteral("title"), QStringLiteral("自检车票") }, { QStringLiteral("cost"), QStringLiteral("60") } });
    r.check((*curPlan(store, id))[QStringLiteral("expenses")].asArray().size() == 1, QStringLiteral("一个人时交通住宿不自动记"));
    pause(60);

    // ---- 同行人窗口：加人（第一次带上"我"），已有的账自动平摊，一步撤销 ----
    QQuickItem *pd = item(win, QStringLiteral("peopleDialog"));
    QQuickItem *addName = findItem(pd, QStringLiteral("peopleAddName"));
    const auto openPeople = [&] {
        click(win, item(win, QStringLiteral("moneyPeopleBtn")));
        waitFor([&] { return pd->property("open").toBool() && pd->opacity() > 0.95; }, 1000);
    };
    openPeople();
    r.check(pd->property("open").toBool(), QStringLiteral("点按钮打开同行人窗口"));
    const QString hint = text(findItem(pd, QStringLiteral("peopleHint")));
    r.check(hint == tp::tr("再加一个人，已有的 %1 笔和交通住宿的 %2 笔，会变成 %3 人平摊。").arg(1).arg(1).arg(2) + QLatin1Char('\n') + tp::tr("单独改过分摊的不变。") + QLatin1Char('\n') + tp::tr("第一次加别人时会自动带上“我”。"), QStringLiteral("写明这次会变几笔"), hint);
    waitFor([&] { return !store->isDirty(); }, 3000);   // 前面的改动先存成自己的一步
    const int undo0 = app->undoCount();
    fill(win, addName, QStringLiteral("小王"));
    click(win, findItem(pd, QStringLiteral("peopleAdd")));
    waitFor([&] { return money(page).value(QStringLiteral("people")).toList().size() == 2; }, 500);
    const auto legacyOk = [&] {
        const Value *pl = curPlan(store, id);
        const json::Array &ex = (*pl)[QStringLiteral("expenses")].asArray();
        return ex.size() == 2 && ex[0][QStringLiteral("paidBy")].toString() == (*pl)[QStringLiteral("people")].asArray()[0][QStringLiteral("id")].toString()
               && !ex[0].asObject().contains(QStringLiteral("split")) && !ex[1][QStringLiteral("bookingId")].toString().isEmpty();
    };
    QVariantList people = money(page).value(QStringLiteral("people")).toList();
    r.check(people.size() == 2 && people.first().toMap().value(QStringLiteral("name")).toString() == tp::tr("我"), QStringLiteral("加别人时\"我\"在第一位"));
    r.check(legacyOk(), QStringLiteral("旧的一笔改成我付、全员平摊，交通住宿记了一笔"));
    waitFor([&] { return !store->isDirty(); }, 3000);
    r.check(app->undoCount() == std::min(undo0 + 1, data::UndoStack::kMaxSteps), QStringLiteral("一次加人算一步撤销"), QStringLiteral("%1 → %2").arg(undo0).arg(app->undoCount()));
    r.check(qAbs(money(page).value(QStringLiteral("total")).toDouble() - 90) < 0.01, QStringLiteral("合计没有重复算"), QString::number(money(page).value(QStringLiteral("total")).toDouble()));
    app->undo();
    waitFor([&] { return money(page).value(QStringLiteral("people")).toList().isEmpty(); }, 500);
    r.check(money(page).value(QStringLiteral("people")).toList().isEmpty() && (*curPlan(store, id))[QStringLiteral("expenses")].asArray().size() == 1
            && !(*curPlan(store, id))[QStringLiteral("expenses")].asArray()[0].asObject().contains(QStringLiteral("paidBy")), QStringLiteral("撤销一步全回去"));
    for (const char *n : { "小王", "小李" }) {
        fill(win, addName, QString::fromUtf8(n));
        click(win, findItem(pd, QStringLiteral("peopleAdd")));
    }
    fill(win, addName, QStringLiteral("小王"));
    click(win, findItem(pd, QStringLiteral("peopleAdd")));
    r.check(money(page).value(QStringLiteral("people")).toList().size() == 3 && isShown(findItem(pd, QStringLiteral("peopleNote"))), QStringLiteral("重名的不加，写明"), text(findItem(pd, QStringLiteral("peopleNote"))));
    pd->setProperty("addName", QString());
    QMetaObject::invokeMethod(addName, "clear");
    click(win, findItem(pd, QStringLiteral("peopleDone")));
    r.check(!pd->property("open").toBool(), QStringLiteral("完成后关掉"));
    waitGone(pd);
    r.check(text(item(win, QStringLiteral("moneyPeopleBtn"))) == tp::tr("同行人 · %1 人").arg(3), QStringLiteral("按钮写 3 人"), text(item(win, QStringLiteral("moneyPeopleBtn"))));

    // ---- 单笔窗口：有同行人时直接显示；开着时每人一行 ----
    showExpense(dlg, QStringLiteral("晚饭"));
    dlg->setProperty("amount", QStringLiteral("240"));
    pause(60);
    r.check(isShown(findItem(dlg, QStringLiteral("expShare"))) && isShown(findItem(dlg, QStringLiteral("expAmt-小王"))), QStringLiteral("分摊直接显示"));
    r.check(findItem(dlg, QStringLiteral("expAmt-小王"))->property("placeholder").toString() == QStringLiteral("80") && isShown(findItem(dlg, QStringLiteral("expAuto-小王"))), QStringLiteral("每人一行，自动算出 80"));
    r.check(text(findItem(dlg, QStringLiteral("expNet"))).contains(QStringLiteral("+160")), QStringLiteral("点开能看到这一笔每人的净额"), text(findItem(dlg, QStringLiteral("expNet"))));
    r.check(!findItem(dlg, QStringLiteral("expCustom")) && !findItem(dlg, QStringLiteral("expAddName")), QStringLiteral("去掉了按金额勾选框和加人输入框"));
    pause(300);
    click(win, item(win, QStringLiteral("expOk")));
    r.check(!dlg->property("open").toBool(), QStringLiteral("保存后关掉"));
    waitGone(dlg);
    const Value *p = curPlan(store, id);
    r.check((*p)[QStringLiteral("people")].asArray().size() == 3, QStringLiteral("同行人写进了 people"));
    pause(60);
    QVariantMap m = money(page);
    r.check(m.value(QStringLiteral("multi")).toBool() && isShown(item(win, QStringLiteral("moneySettle"))), QStringLiteral("两个人以上：显示结算"));
    QQuickItem *settleCard = item(win, QStringLiteral("moneySettle"));
    QQuickItem *totalItem = item(win, QStringLiteral("moneyTotal"));
    r.check(settleCard && totalItem && settleCard->mapToScene(QPointF()).y() < totalItem->mapToScene(QPointF()).y(), QStringLiteral("结算在最上面（5A）"));
    // 我付 240 + 30 + 60，都是三人平摊：我 +220，小王、小李各 −110
    QVariantMap settle = m.value(QStringLiteral("settle")).toMap();
    r.check(settle.value(QStringLiteral("transfers")).toList().size() == 2, QStringLiteral("两笔转账"));
    const auto lineOf = [&](const QString &title) {
        QVariant line;
        for (const QVariant &v : money(page).value(QStringLiteral("rows")).toList())
            if (v.toMap().value(QStringLiteral("title")).toString() == title) QMetaObject::invokeMethod(item(win, QStringLiteral("tripBudget")), "splitText", Q_RETURN_ARG(QVariant, line), Q_ARG(QVariant, v));
        return line.toString();
    };
    r.check(lineOf(QStringLiteral("晚饭")).contains(tp::tr("每人 %1").arg(QStringLiteral("AUD 80"))) && lineOf(QStringLiteral("晚饭")).contains(tp::tr("我")), QStringLiteral("每行写明谁付、每人多少"), lineOf(QStringLiteral("晚饭")));

    // ---- K1 复制 ----
    click(win, item(win, QStringLiteral("settleCopy")));
    const QString clip = QGuiApplication::clipboard()->text();
    r.check(clip.contains(QStringLiteral("小王 → ")) && clip.contains(QStringLiteral("AUD 110")), QStringLiteral("复制结算结果"), clip);
    r.check(text(item(win, QStringLiteral("settleCopy"))) == tp::tr("已复制"), QStringLiteral("按钮提示已复制"));

    // ---- 每人的金额：填了的固定，没填的自动，× 恢复；校验写清差多少 ----
    const auto dinnerRow = [&] {
        for (const QVariant &v : money(page).value(QStringLiteral("rows")).toList())
            if (v.toMap().value(QStringLiteral("title")).toString() == QStringLiteral("晚饭")) return v.toMap();
        return QVariantMap();
    };
    const auto dinnerRec = [&] {
        for (const Value &x : (*curPlan(store, id))[QStringLiteral("expenses")].asArray())
            if (x[QStringLiteral("title")].toString() == QStringLiteral("晚饭")) return x;
        return Value();
    };
    const auto showDinner = [&] {
        QMetaObject::invokeMethod(dlg, "show", Q_ARG(QVariant, QVariant(dinnerRow())), Q_ARG(QVariant, QVariant()));
        waitFor([&] { return dlg->opacity() > 0.95; }, 1000);
    };
    const auto amt = [&](const QString &n) { return findItem(dlg, QStringLiteral("expAmt-") + n); };
    showDinner();
    r.check(dlg->property("among").toList().size() == 3 && dlg->property("share").toBool(), QStringLiteral("改这一笔：三人平摊"));
    const QStringList pids = [&] { QStringList l; for (const QVariant &v : money(page).value(QStringLiteral("people")).toList()) l << v.toMap().value(QStringLiteral("id")).toString(); return l; }();
    fill(win, amt(QStringLiteral("小王")), QStringLiteral("100"));
    r.check(amt(tp::tr("我"))->property("placeholder").toString() == QStringLiteral("70") && isShown(findItem(dlg, QStringLiteral("expClear-小王"))) && !isShown(findItem(dlg, QStringLiteral("expAuto-小王"))), QStringLiteral("填了金额：别人马上重算，有 ×"), amt(tp::tr("我"))->property("placeholder").toString());
    click(win, findItem(dlg, QStringLiteral("expClear-小王")));
    r.check(amt(QStringLiteral("小李"))->property("placeholder").toString() == QStringLiteral("80") && text(amt(QStringLiteral("小王"))).isEmpty() && isShown(findItem(dlg, QStringLiteral("expAuto-小王"))), QStringLiteral("点 × 恢复平摊"));
    click(win, findItem(dlg, QStringLiteral("expAmong-小李")));
    r.check(amt(QStringLiteral("小王"))->property("placeholder").toString() == QStringLiteral("120") && !isShown(amt(QStringLiteral("小李"))), QStringLiteral("不勾的人没有金额，别人重算"));
    click(win, findItem(dlg, QStringLiteral("expAmong-小李")));
    fill(win, amt(QStringLiteral("小王")), QStringLiteral("100"));
    click(win, item(win, QStringLiteral("expOk")));
    waitGone(dlg);
    {
        const json::Array &sp = dinnerRec()[QStringLiteral("split")].asArray();
        int withAmount = 0;
        for (const Value &x : sp) if (x.asObject().contains(QStringLiteral("amount"))) ++withAmount;
        r.check(sp.size() == 3 && withAmount == 1, QStringLiteral("只给填了的人写 amount"), QString::number(withAmount));
    }
    r.check(lineOf(QStringLiteral("晚饭")).contains(tp::tr("按金额")), QStringLiteral("每行写\"按金额\""), lineOf(QStringLiteral("晚饭")));
    showDinner();
    r.check(text(amt(QStringLiteral("小王"))) == QStringLiteral("100") && text(amt(tp::tr("我"))).isEmpty(), QStringLiteral("再打开：填了的还在，没填的还是自动"));
    const Value atBad = dinnerRec();   // 只比这一笔：自动保存会改全局的时间戳
    fill(win, amt(QStringLiteral("小王")), QStringLiteral("300"));
    QQuickItem *expErr = findItem(dlg, QStringLiteral("expErr"));
    r.check(isShown(expErr) && text(expErr).contains(QStringLiteral("60")), QStringLiteral("超过总额：写清多了多少"), text(expErr));
    click(win, item(win, QStringLiteral("expOk")));
    r.check(dlg->property("open").toBool() && dinnerRec() == atBad, QStringLiteral("超过总额：不关、不改数据"));
    fill(win, amt(tp::tr("我")), QStringLiteral("100"));
    fill(win, amt(QStringLiteral("小王")), QStringLiteral("100"));
    fill(win, amt(QStringLiteral("小李")), QStringLiteral("30"));
    r.check(isShown(expErr) && text(expErr).contains(QStringLiteral("10")), QStringLiteral("都填了但对不上：写清差多少"), text(expErr));
    click(win, item(win, QStringLiteral("expOk")));
    r.check(dlg->property("open").toBool() && dinnerRec() == atBad, QStringLiteral("对不上：不关、不改数据"));
    fill(win, findItem(dlg, QStringLiteral("expAmount")), QStringLiteral("230"));
    r.check(!isShown(expErr), QStringLiteral("总金额改了马上重算，错误消失"), text(expErr));
    click(win, item(win, QStringLiteral("expOk")));
    waitGone(dlg);
    r.check(dinnerRow().value(QStringLiteral("amounts")).toMap().size() == 3, QStringLiteral("全部写了金额"));
    showDinner();
    r.check(dlg->property("fixedIds").toList().size() == 3, QStringLiteral("按金额的数据打开显示全部已填"));
    fill(win, findItem(dlg, QStringLiteral("expAmount")), QStringLiteral("240"));
    fill(win, amt(QStringLiteral("小李")), QStringLiteral("40"));
    // ---- 开关：关 = 只有付款人；换付款人跟着换 ----
    click(win, findItem(dlg, QStringLiteral("expShare")));
    r.check(!isShown(amt(QStringLiteral("小王"))) && isShown(findItem(dlg, QStringLiteral("expPayer-小王"))), QStringLiteral("关了：每人那部分藏起来，谁付的还在"));
    click(win, findItem(dlg, QStringLiteral("expPayer-小王")));   // 关着也能换付款人
    click(win, item(win, QStringLiteral("expOk")));
    waitGone(dlg);
    {
        const Value rec = dinnerRec();
        const json::Array &sp = rec[QStringLiteral("split")].asArray();
        r.check(rec[QStringLiteral("paidBy")].toString() == pids[1] && sp.size() == 1 && sp[0][QStringLiteral("who")].toString() == pids[1] && !sp[0].asObject().contains(QStringLiteral("amount")), QStringLiteral("关：split 只有付款人"));
    }
    r.check(lineOf(QStringLiteral("晚饭")).contains(tp::tr("不 A")) && lineOf(QStringLiteral("晚饭")).contains(QStringLiteral("小王")), QStringLiteral("每行写\"不 A\"，保留谁付"), lineOf(QStringLiteral("晚饭")));
    showDinner();
    r.check(!dlg->property("share").toBool(), QStringLiteral("再打开是关的"));
    click(win, findItem(dlg, QStringLiteral("expShare")));
    click(win, findItem(dlg, QStringLiteral("expPayer-") + tp::tr("我")));
    r.check(dlg->property("among").toList().size() == 3, QStringLiteral("打开后默认全员"));
    click(win, item(win, QStringLiteral("expOk")));
    waitGone(dlg);
    r.check(!dinnerRec().asObject().contains(QStringLiteral("split")) && dinnerRec()[QStringLiteral("paidBy")].toString() == pids[0], QStringLiteral("全员且没有固定金额：split 写空"));

    // ---- 单笔窗口里的"管理同行人"：打开同行人窗口，加人后这边跟着变 ----
    showExpense(dlg, QStringLiteral("试"));
    waitFor([&] { return dlg->opacity() > 0.95; }, 1000);
    click(win, findItem(dlg, QStringLiteral("expPeople")));
    QQuickItem *paneInDlg = findItem(dlg, QStringLiteral("expPeoplePane"));
    r.check(waitFor([&] { return isShown(paneInDlg); }, 1000) && !pd->property("open").toBool() && !isShown(findItem(dlg, QStringLiteral("expForm"))),
            QStringLiteral("\"管理同行人\"在同一窗口内翻页，不叠第二个弹窗（0.12.8）"));
    fill(win, findItem(paneInDlg, QStringLiteral("peopleAddName")), QStringLiteral("路人"));
    click(win, findItem(paneInDlg, QStringLiteral("peopleAdd")));
    waitFor([&] { return dlg->property("among").toList().size() == 4; }, 500);
    r.check(dlg->property("among").toList().size() == 4 && findItem(dlg, QStringLiteral("expAmong-路人")), QStringLiteral("单笔窗口跟着多一个人"));
    click(win, findItem(paneInDlg, QStringLiteral("peopleDel-路人")));
    waitFor([&] { return dlg->property("among").toList().size() == 3; }, 500);
    r.check(money(page).value(QStringLiteral("people")).toList().size() == 3 && dlg->property("among").toList().size() == 3, QStringLiteral("只在全员平摊里的人能直接删"));
    click(win, findItem(paneInDlg, QStringLiteral("peopleDone")));
    r.check(waitFor([&] { return isShown(findItem(dlg, QStringLiteral("expForm"))); }, 800) && dlg->property("titleText").toString().contains(QStringLiteral("试")),
            QStringLiteral("返回后表单还在，填的内容没丢"));
    dlg->setProperty("open", false);
    waitGone(dlg);

    // ---- 一行快记：我付、所有人平摊 ----
    QQuickItem *budget = item(win, QStringLiteral("tripBudget"));
    budget->setProperty("qTitle", QStringLiteral("咖啡"));
    budget->setProperty("qAmount", QStringLiteral("9"));
    click(win, item(win, QStringLiteral("quickAdd")));
    bool quickPaid = false;
    for (const Value &x : (*curPlan(store, id))[QStringLiteral("expenses")].asArray())
        if (x[QStringLiteral("title")].toString() == QStringLiteral("咖啡")) quickPaid = x[QStringLiteral("paidBy")].toString() == pids[0] && !x.asObject().contains(QStringLiteral("split"));
    r.check(quickPaid, QStringLiteral("一行快记默认我付、所有人平摊"));

    // ---- 缺汇率、用到的汇率 ----
    page->saveExpense(QString(), { { QStringLiteral("title"), QStringLiteral("没有汇率") }, { QStringLiteral("amount"), QStringLiteral("5") }, { QStringLiteral("currency"), QStringLiteral("ISK") }, { QStringLiteral("cat"), QStringLiteral("shop") }, { QStringLiteral("date"), pc->today() } });
    page->saveExpense(QString(), { { QStringLiteral("title"), QStringLiteral("美元") }, { QStringLiteral("amount"), QStringLiteral("14") }, { QStringLiteral("currency"), QStringLiteral("USD") }, { QStringLiteral("cat"), QStringLiteral("shop") }, { QStringLiteral("date"), pc->today() } });
    pause(60);
    r.check(text(item(win, QStringLiteral("settleMissing"))).contains(QStringLiteral("ISK")) && isShown(item(win, QStringLiteral("settleMissing"))), QStringLiteral("结算写明缺哪种汇率"));
    r.check(text(item(win, QStringLiteral("settleRates"))).contains(QStringLiteral("1 USD")), QStringLiteral("列出用到的汇率"), text(item(win, QStringLiteral("settleRates"))));

    // ---- 有同行人时新存的交通住宿自动记一笔、进结算，再存不重复 ----
    QVariantMap hotel{ { QStringLiteral("kind"), QStringLiteral("hotel") }, { QStringLiteral("title"), QStringLiteral("自检酒店") }, { QStringLiteral("cost"), QStringLiteral("500") } };
    const QVariantList netBefore = money(page).value(QStringLiteral("settle")).toMap().value(QStringLiteral("rows")).toList();
    page->saveBooking(QString(), hotel);
    pause(60);
    r.check(money(page).value(QStringLiteral("settle")).toMap().value(QStringLiteral("rows")).toList() != netBefore && !isShown(item(win, QStringLiteral("settleBookingNote"))), QStringLiteral("新存的交通住宿自动进结算"));
    QString hotelId;
    for (const Value &b : (*curPlan(store, id))[QStringLiteral("bookings")].asArray()) hotelId = b[QStringLiteral("id")].toString();
    const auto hotelExps = [&] { int n = 0; for (const Value &x : (*curPlan(store, id))[QStringLiteral("expenses")].asArray()) if (x[QStringLiteral("bookingId")].toString() == hotelId) ++n; return n; };
    r.check(hotelExps() == 1, QStringLiteral("记了一笔"));
    hotel.insert(QStringLiteral("toExp"), true);
    page->saveBooking(hotelId, hotel);
    r.check(hotelExps() == 1, QStringLiteral("再存不重复"));

    // ---- 同行人改名、删除 ----
    page->saveExpense(QString(), { { QStringLiteral("title"), QStringLiteral("小李付的") }, { QStringLiteral("amount"), QStringLiteral("3") }, { QStringLiteral("cat"), QStringLiteral("other") }, { QStringLiteral("date"), pc->today() }, { QStringLiteral("paidBy"), pids[2] }, { QStringLiteral("split"), QVariantList() } });
    pause(60);
    openPeople();
    click(win, findItem(pd, QStringLiteral("peopleDel-小李")));
    r.check(money(page).value(QStringLiteral("people")).toList().size() == 3 && text(findItem(pd, QStringLiteral("peopleNote"))).contains(QStringLiteral("小李付的")), QStringLiteral("付过钱的人不能删，说清哪几笔"), text(findItem(pd, QStringLiteral("peopleNote"))));
    fill(win, findItem(pd, QStringLiteral("peopleName-小李")), QStringLiteral("老李"));
    click(win, findItem(pd, QStringLiteral("peopleDone")));
    pause(60);
    r.check(!pd->property("open").toBool(), QStringLiteral("完成后关掉"));
    waitGone(pd);
    r.check(money(page).value(QStringLiteral("settle")).toMap().value(QStringLiteral("rows")).toList().at(2).toMap().value(QStringLiteral("name")).toString() == QStringLiteral("老李"), QStringLiteral("改名了，结算跟着变"));
    r.check(page->editPerson(pids[2], QString(), true) == QLatin1String("used"), QStringLiteral("硬删也不让"));

    // ---- 账本 ----
    const double budgetMain = money(page).value(QStringLiteral("mainTotal")).toDouble();   // 离开计划页后 info 就空了
    nav->go(QStringLiteral("ledger"));
    QQuickItem *ledger = item(win, QStringLiteral("ledgerPage"));
    waitFor([&] { return ledger && ledger->opacity() > 0.95; }, 1500);
    pause(60);
    const QVariantMap led = page->ledger();
    r.check(isShown(ledger) && !led.isEmpty(), QStringLiteral("账本打开了"));
    QVariantMap mine;
    for (const QVariant &v : led.value(QStringLiteral("trips")).toList()) if (v.toMap().value(QStringLiteral("id")).toString() == id) mine = v.toMap();
    // 主货币 AUD = 这条计划的币种：账本里这一趟和消费页的合计除掉缺汇率的应该一样
    r.check(!mine.isEmpty() && qAbs(mine.value(QStringLiteral("spent")).toDouble() - budgetMain) < 0.01, QStringLiteral("账本这一趟的数字和消费页一样"),
            QStringLiteral("%1 / %2").arg(mine.value(QStringLiteral("spent")).toDouble()).arg(budgetMain));
    r.check(led.value(QStringLiteral("missing")).toStringList().contains(QStringLiteral("ISK")) && isShown(item(win, QStringLiteral("ledgerMissing"))), QStringLiteral("写明缺哪种汇率"));
    // 0.10.5 第 5 题 A：已花 / 还没去的预计各算各的，今天到了开始日期才算已花
    {
        double sumDone = 0, sumFuture = 0;
        int nDone = 0, nFuture = 0;
        for (const QVariant &v : led.value(QStringLiteral("trips")).toList()) {
            const QVariantMap t = v.toMap();
            if (t.value(QStringLiteral("done")).toBool()) { sumDone += t.value(QStringLiteral("spent")).toDouble(); ++nDone; }
            else { sumFuture += t.value(QStringLiteral("spent")).toDouble(); ++nFuture; }
        }
        r.check(qAbs(led.value(QStringLiteral("total")).toDouble() - sumDone) < 0.01 && qAbs(led.value(QStringLiteral("future")).toDouble() - sumFuture) < 0.01
                    && led.value(QStringLiteral("spentCount")).toInt() == nDone && led.value(QStringLiteral("futureCount")).toInt() == nFuture,
                QStringLiteral("账本：已花和还没去的预计分开算"), QStringLiteral("已花 %1 趟 %2；还没去 %3 趟 %4").arg(nDone).arg(sumDone).arg(nFuture).arg(sumFuture));
        r.check(isShown(item(win, QStringLiteral("ledgerFuture"))) == (nFuture > 0) && isShown(item(win, QStringLiteral("ledgerStats"))) == (nDone > 0)
                    && isShown(item(win, QStringLiteral("ledgerNoSpent"))) == (nDone == 0), QStringLiteral("账本：页头跟着显示"));
        const QString todayWas = pc->today();
        const auto mineDone = [&] {
            for (const QVariant &v : page->ledger().value(QStringLiteral("trips")).toList())
                if (v.toMap().value(QStringLiteral("id")).toString() == id) return v.toMap().value(QStringLiteral("done")).toBool();
            return false;
        };
        const bool doneNow = mine.value(QStringLiteral("done")).toBool();
        pc->setTodayOverride(QStringLiteral("2000-01-01"));
        const bool flipped = waitFor([&] { return !mineDone(); }, 1000);
        pc->setTodayOverride(todayWas);
        waitFor([&] { return mineDone() == doneNow; }, 1000);
        r.check(doneNow && flipped, QStringLiteral("账本：开始日期前算还没去，到了才算已花"), QStringLiteral("doneNow %1 flipped %2 today %3 state %4 start %5").arg(doneNow).arg(flipped).arg(todayWas, mine.value(QStringLiteral("state")).toString(), mine.value(QStringLiteral("start")).toString()));
    }
    r.check(text(item(win, QStringLiteral("ledgerTotal"))).startsWith(QStringLiteral("AUD ")), QStringLiteral("总额按主货币"), text(item(win, QStringLiteral("ledgerTotal"))));
    rates->setMain(QStringLiteral("CNY"));
    pause(60);
    r.check(text(item(win, QStringLiteral("ledgerTotal"))).startsWith(QStringLiteral("CNY ")), QStringLiteral("页头换主货币，总额跟着变"), text(item(win, QStringLiteral("ledgerTotal"))));
    const QVariantList tripsNow = page->ledger().value(QStringLiteral("trips")).toList();
    r.check(ledger->property("sorted").toList().size() == tripsNow.size(), QStringLiteral("每趟一行"), QString::number(tripsNow.size()));
    bool desc = true;
    for (int i = 1; i < tripsNow.size(); ++i) if (tripsNow[i - 1].toMap().value(QStringLiteral("spent")).toDouble() < tripsNow[i].toMap().value(QStringLiteral("spent")).toDouble()) desc = false;
    r.check(desc, QStringLiteral("默认按花费排"));
    ledger->setProperty("sortBy", QStringLiteral("date"));
    pause(60);
    const QVariantList byDate = ledger->property("sorted").toList();
    r.check(byDate.size() == tripsNow.size(), QStringLiteral("能按日期排"));
    ledger->setProperty("sortBy", QStringLiteral("spent"));
    // 点一行：跳到那个计划的消费标签
    ledger->setProperty("sortBy", QStringLiteral("date"));
    QMetaObject::invokeMethod(ledger, "openPlan", Q_ARG(QString, id));
    waitStill(panel);
    r.check(nav->page() == QLatin1String("trip") && page->planId() == id && panel->property("tab").toString() == QLatin1String("budget"), QStringLiteral("点一行到那个计划的消费标签"));
    ledger->setProperty("sortBy", QStringLiteral("spent"));

    // ---- 收尾 ----
    nav->go(pc->mode());
    waitStill(panel);
    store->modify([&](Value &d) { d = before; });
    rates->setFetchForTest({});
    rates->resetForTest(oldMain, oldTable, 0);
    pc->setTodayOverride(QString());
    pc->setTestAllStatuses(true);
    win->resize(oldSize);
    pause(150);
}

} // namespace tp::app::selftest
