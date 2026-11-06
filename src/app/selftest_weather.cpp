// 自检：0.10.3 天气、气候、海拔、高反、此刻卡。都在数据副本上做，最后把数据改回去；请求用假回复（setFetchForTest），最后真的问一次 Open-Meteo（没网时只记一笔）。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "navhistory.h"
#include "plancontroller.h"
#include "planpagecontroller.h"
#include "plans.h"
#include "planweather.h"
#include "weathercontroller.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
int count(const QUrlQuery &q) { return int(q.queryItemValue(QStringLiteral("latitude")).split(QLatin1Char(',')).size()); }
} // namespace

void runWeather(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav, WeatherController *weather)
{
    r.section(QStringLiteral("0.10.3 天气、气候、海拔、高反"));
    PlanController *pc = plansCtl();
    data::DataStore *store = app->store();
    QQuickItem *panel = item(win, QStringLiteral("planPage"));
    if (!r.check(pc && page && nav && panel && weather, QStringLiteral("计划页和天气都在"))) return;
    pc->setTestAllStatuses(false);
    const QVariantMap demo = pc->demo();
    const QString id = demo.value(QStringLiteral("planId")).toString();
    if (!r.check(!id.isEmpty(), QStringLiteral("有带日期的路线"))) return;
    const QString today = demo.value(QStringLiteral("today")).toString();
    pc->setTodayOverride(today);
    const Value before = store->data();

    // ---- 假回复 ----
    int forecastCalls = 0, climCalls = 0, elevCalls = 0;
    bool fail = false;
    const auto daily = [&](const QString &date) {
        return QJsonObject{ { QStringLiteral("daily"), QJsonObject{
            { QStringLiteral("time"), QJsonArray{ date } }, { QStringLiteral("weather_code"), QJsonArray{ 61 } },
            { QStringLiteral("temperature_2m_max"), QJsonArray{ 18.4 } }, { QStringLiteral("temperature_2m_min"), QJsonArray{ 11.6 } },
            { QStringLiteral("precipitation_probability_max"), QJsonArray{ 70 } } } } };
    };
    // 每站的到达日期和今天都给同一个结果：回复按日期找，所以 time 放今天 + 到达日期
    weather->setFetchForTest([&](const QUrl &url, std::function<void(int, const QByteArray &, int)> done) {
        const QUrlQuery q(url);
        if (fail) { done(500, QByteArray(), 6); return; }
        QJsonArray rows;
        if (url.host() == QLatin1String("archive-api.open-meteo.com")) {
            ++climCalls;
            const QJsonObject d{ { QStringLiteral("daily"), QJsonObject{ { QStringLiteral("temperature_2m_max"), QJsonArray{ 30.0, 32.0 } },
                { QStringLiteral("temperature_2m_min"), QJsonArray{ 20.0, 22.0 } }, { QStringLiteral("precipitation_sum"), QJsonArray{ 0.5, 5.0 } } } } };
            for (int i = 0; i < count(q); ++i) rows.append(d);
        } else {
            ++forecastCalls;
            const Value *p = plans::planById(store->data(), id);
            QJsonArray time;
            time.append(today);
            for (const Value &s : (*p)[QStringLiteral("stops")].asArray()) time.append(s[QStringLiteral("arriveDate")].toString());
            QJsonArray codes, hi, lo, pop;
            for (int i = 0; i < time.size(); ++i) { codes.append(61); hi.append(18.4); lo.append(11.6); pop.append(70); }
            QJsonObject d = daily(today);
            d[QStringLiteral("daily")] = QJsonObject{ { QStringLiteral("time"), time }, { QStringLiteral("weather_code"), codes }, { QStringLiteral("temperature_2m_max"), hi },
                                                      { QStringLiteral("temperature_2m_min"), lo }, { QStringLiteral("precipitation_probability_max"), pop } };
            for (int i = 0; i < count(q); ++i) rows.append(d);
        }
        done(200, QJsonDocument(rows).toJson(QJsonDocument::Compact), 0);
    });
    QByteArray elevReply = QByteArrayLiteral(R"({"elevation":[1234.4]})");
    page->setFetchForTest([&](const QUrl &url, std::function<void(int, const QByteArray &, int)> done) {
        if (url.path().endsWith(QLatin1String("/elevation"))) { ++elevCalls; done(200, fail ? QByteArray("oops") : elevReply, 0); return; }
        done(200, QByteArrayLiteral("[]"), 0);   // 补时区：这里不测
    });

    // 这条计划：计划中；前几站海拔升得太快；第 3 站（下标 2）海拔空着
    store->modify([&](Value &d) {
        Value *p = plans::mutPlan(d, id);
        p->mutObject().set(QStringLiteral("status"), Value(QStringLiteral("planning")));
        const QVector<int> st = plans::validStops(*p);
        json::Array &stops = p->mutObject().find(QStringLiteral("stops"))->mutArray();
        for (int k = 0; k < st.size(); ++k) stops[st[k]].mutObject().remove(QStringLiteral("elev"));
        const QVector<int> elev{ 1000, 2900, -1, 3800 };
        const QDate base = QDate::fromString(today, Qt::ISODate).addDays(-2);
        for (int k = 0; k < st.size() && k < elev.size(); ++k) {
            json::Object &s = stops[st[k]].mutObject();
            if (elev[k] >= 0) s.set(QStringLiteral("elev"), Value(elev[k]));
            s.set(QStringLiteral("arriveDate"), Value(base.addDays(k).toString(Qt::ISODate)));
            s.set(QStringLiteral("departDate"), Value(base.addDays(k + 1).toString(Qt::ISODate)));
        }
    });

    // ---- 打开：补海拔、预报 ----
    QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
    waitFor([&] { return nav->page() == QLatin1String("trip") && page->planId() == id; }, 3000);
    pause(600);
    const int stopN = page->info().value(QStringLiteral("stops")).toList().size();
    r.check(elevCalls >= 1, QStringLiteral("打开时问了海拔"), QString::number(elevCalls));
    const auto stopAt = [&](int k) { return page->info().value(QStringLiteral("stops")).toList().value(k).toMap(); };
    r.check(qRound(stopAt(2).value(QStringLiteral("elev")).toDouble()) == 1234 || stopN < 3, QStringLiteral("空的海拔补上了（四舍五入）"), stopAt(2).value(QStringLiteral("elev")).toString());
    r.check(qRound(stopAt(0).value(QStringLiteral("elev")).toDouble()) == 1000, QStringLiteral("已有的海拔不动"));
    r.check(forecastCalls >= 1 && forecastCalls <= 2, QStringLiteral("预报合并成一两个请求"), QString::number(forecastCalls));
    const QVariantMap wx0 = weather->stops().value(0).toMap().value(QStringLiteral("wx")).toMap();
    r.check(wx0.value(QStringLiteral("kind")).toString() == QLatin1String("rain"), QStringLiteral("每站有预报"), wx0.value(QStringLiteral("date")).toString());
    QQuickItem *wxRow = item(win, QStringLiteral("tripWx0"));
    r.check(isShown(wxRow), QStringLiteral("站点卡片上显示预报"), wxRow ? text(findItem(wxRow, QStringLiteral("tripWxText"))) : QString());

    // ---- 高反：第 2 站 2900（不到 3000 不算）；第 3 站 1234；第 4 站 3800 相对 1234 上升太快 ----
    if (stopN >= 4) {
        const QVariantList warns = page->info().value(QStringLiteral("altWarnings")).toList();
        r.check(!warns.isEmpty() && warns[0].toMap().value(QStringLiteral("to")).toInt() == 4, QStringLiteral("页头有高反提示，对到第 4 站"), QString::number(warns.size()));
        r.check(isShown(item(win, QStringLiteral("tripAlt"))), QStringLiteral("页头那一条显示了"));
        r.check(stopAt(3).value(QStringLiteral("altTag")).toString() == QLatin1String("rise"), QStringLiteral("站点上标了上升太快"));
    }
    // 补完了再打开不再问
    const int e0 = elevCalls;
    // ---- 改海拔 ----
    r.check(page->setElev(0, QStringLiteral("abc")) == QLatin1String("badElev"), QStringLiteral("海拔不是数字：不改"));
    r.check(page->setElev(0, QStringLiteral("12000")) == QLatin1String("badElev"), QStringLiteral("海拔超出范围：不改"));
    r.check(page->setElev(0, QStringLiteral("1500.6")).isEmpty() && qRound(stopAt(0).value(QStringLiteral("elev")).toDouble()) == 1501, QStringLiteral("手动改海拔（取整）"));
    elevReply = QByteArrayLiteral(R"({"elevation":[777]})");
    r.check(page->setElev(0, QString()).isEmpty(), QStringLiteral("清空海拔"));
    waitFor([&] { return elevCalls > e0; }, 1000);
    r.check(elevCalls > e0 && qRound(stopAt(0).value(QStringLiteral("elev")).toDouble()) == 777, QStringLiteral("清空后重新取"), stopAt(0).value(QStringLiteral("elev")).toString());
    // 点海拔：就地表单，Esc 层级能关
    panel->setProperty("elevEdit", -1);
    QMetaObject::invokeMethod(panel, "beginElev", Q_ARG(QVariant, 0));
    pause(60);
    QQuickItem *field = item(win, QStringLiteral("tripElevField"));
    r.check(panel->property("scheduleEditing").toBool() && field && field->isVisible(), QStringLiteral("点海拔：就地表单"));
    QMetaObject::invokeMethod(panel, "cancelSchedule");
    pause(40);
    r.check(!panel->property("scheduleEditing").toBool(), QStringLiteral("取消后关上"));

    // ---- 气候：点了才问；失败有提示，成功后写进卡片 ----
    r.check(climCalls == 0 && weather->stops().value(0).toMap().value(QStringLiteral("clim")).isNull(), QStringLiteral("没点时不问气候"));
    fail = true;
    bool failed = false;
    const QMetaObject::Connection c1 = QObject::connect(weather, &WeatherController::climateFailed, weather, [&] { failed = true; });
    weather->fetchClimate();
    QObject::disconnect(c1);
    r.check(failed && !weather->climateBusy() && !weather->climateShown(), QStringLiteral("气候失败：有提示，按钮还在"));
    fail = false;
    QQuickItem *climBtn = item(win, QStringLiteral("tripClimate"));
    r.check(isShown(climBtn), QStringLiteral("有往年同月气候按钮"), text(climBtn));
    weather->fetchClimate();
    const int pointsMonths = climCalls;
    r.check(pointsMonths >= 10 && pointsMonths % 10 == 0, QStringLiteral("一个月问 10 年"), QString::number(pointsMonths));
    const QVariantMap clim0 = weather->stops().value(0).toMap().value(QStringLiteral("clim")).toMap();
    r.check(weather->climateShown() && qRound(clim0.value(QStringLiteral("hi")).toDouble()) == 31 && qRound(clim0.value(QStringLiteral("lo")).toDouble()) == 21, QStringLiteral("平均最高和最低"));
    r.check(qRound(clim0.value(QStringLiteral("mm")).toDouble()) == 6 && qRound(clim0.value(QStringLiteral("rainDays")).toDouble()) == 1, QStringLiteral("每月降水和雨天"));
    pause(60);
    r.check(isShown(item(win, QStringLiteral("tripClim0"))) && !isShown(item(win, QStringLiteral("tripClimate"))), QStringLiteral("卡片显示气候，按钮收起"));
    weather->fetchClimate();
    r.check(climCalls == pointsMonths, QStringLiteral("气候长期缓存，再点不重问"));

    // ---- 换计划再回来：气候要再点；预报用缓存 ----
    const int fc = forecastCalls;
    QMetaObject::invokeMethod(panel, "closeRequested");
    waitFor([&] { return page->planId().isEmpty(); }, 3000);
    QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
    waitFor([&] { return page->planId() == id; }, 3000);
    pause(200);
    r.check(!weather->climateShown(), QStringLiteral("每次打开要再点气候"));
    r.check(forecastCalls == fc && !weather->stops().value(0).toMap().value(QStringLiteral("wx")).isNull(), QStringLiteral("3 小时内不重问预报"));

    // ---- 已完成的计划不问预报 ----
    QMetaObject::invokeMethod(panel, "closeRequested");
    waitFor([&] { return page->planId().isEmpty(); }, 3000);
    store->modify([&](Value &d) { plans::mutPlan(d, id)->mutObject().set(QStringLiteral("status"), Value(QStringLiteral("completed"))); });
    QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
    waitFor([&] { return page->planId() == id; }, 3000);
    pause(200);
    r.check(weather->stops().value(0).toMap().value(QStringLiteral("wx")).isNull(), QStringLiteral("已完成的计划不显示预报"));
    QMetaObject::invokeMethod(panel, "closeRequested");
    waitFor([&] { return page->planId().isEmpty(); }, 3000);
    store->modify([&](Value &d) { plans::mutPlan(d, id)->mutObject().set(QStringLiteral("status"), Value(QStringLiteral("planning"))); });

    // ---- L2 此刻卡 ----
    pc->setNextTrip(id, true);
    pause(100);
    if (pc->now().value(QStringLiteral("kind")).toString() == QLatin1String("live")) {
        QVariantMap w = weather->today();
        r.check(w.value(QStringLiteral("kind")).toString() == QLatin1String("rain") && w.value(QStringLiteral("date")).toString() == today, QStringLiteral("此刻卡有今天的天气"), w.value(QStringLiteral("kind")).toString());
        win->setProperty("nowOpen", true);
        pause(120);
        QQuickItem *t = item(win, QStringLiteral("nowWeather"));
        r.check(t && t->isVisible() && text(t).contains(QStringLiteral("12–18")), QStringLiteral("此刻卡上写着天气"), text(t));
        win->setProperty("nowOpen", false);
    } else {
        r.info(QStringLiteral("此刻不是在路上，跳过 L2"));
    }
    pc->setNextTrip(id, false);

    // ---- 失败时安静：不显示、不崩 ----
    weather->setFetchForTest([](const QUrl &, std::function<void(int, const QByteArray &, int)> done) { done(0, QByteArray(), 5); });
    QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
    waitFor([&] { return page->planId() == id; }, 3000);
    pause(150);
    r.check(weather->stops().value(0).toMap().value(QStringLiteral("wx")).isNull() && !isShown(item(win, QStringLiteral("tripWx0"))), QStringLiteral("没取到预报：什么都不显示"));

    // ---- 真的问一次（顺带实测 URL、白名单、解析；没网不算失败）----
    QMetaObject::invokeMethod(panel, "closeRequested");
    waitFor([&] { return page->planId().isEmpty(); }, 3000);
    page->setFetchForTest({});
    weather->setFetchForTest({});
    store->modify([&](Value &d) {
        Value *p = plans::mutPlan(d, id);
        const QString t = QDate::currentDate().toString(Qt::ISODate);
        const QString e = QDate::currentDate().addDays(3).toString(Qt::ISODate);
        p->mutObject().set(QStringLiteral("startDate"), Value(t));
        p->mutObject().set(QStringLiteral("endDate"), Value(e));
        for (int i : plans::validStops(*p)) {
            json::Object &s = p->mutObject().find(QStringLiteral("stops"))->mutArray()[i].mutObject();
            s.set(QStringLiteral("arriveDate"), Value(t));
            s.set(QStringLiteral("departDate"), Value(e));
        }
    });
    pc->setTodayOverride(QDate::currentDate().toString(Qt::ISODate));
    QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
    waitFor([&] { return page->planId() == id; }, 3000);
    const bool live = waitFor([&] { return !weather->stops().value(0).toMap().value(QStringLiteral("wx")).isNull(); }, 15000);
    if (live) r.check(true, QStringLiteral("真实的 Open-Meteo 预报取到了"), weather->stops().value(0).toMap().value(QStringLiteral("wx")).toMap().value(QStringLiteral("kind")).toString());
    else r.info(QStringLiteral("真实的 Open-Meteo 预报没取到（可能没网）"));
    if (live) {
        weather->fetchClimate();
        const bool got = waitFor([&] { return weather->climateShown() || !weather->climateBusy(); }, 60000) && weather->climateShown();
        const QVariantMap c = weather->stops().value(0).toMap().value(QStringLiteral("clim")).toMap();
        if (got && !c.isEmpty()) r.check(true, QStringLiteral("真实的往年气候取到了"), QStringLiteral("%1–%2°C · %3 mm · %4").arg(qRound(c.value(QStringLiteral("lo")).toDouble())).arg(qRound(c.value(QStringLiteral("hi")).toDouble())).arg(qRound(c.value(QStringLiteral("mm")).toDouble())).arg(c.value(QStringLiteral("years")).toString()));
        else r.info(QStringLiteral("真实的往年气候没取到（可能被限流）"));
    }

    // ---- 收尾 ----
    QMetaObject::invokeMethod(panel, "closeRequested");
    waitFor([&] { return page->planId().isEmpty(); }, 3000);
    store->modify([&](Value &d) { d = before; });
    pc->setTodayOverride(QString());
    nav->go(QStringLiteral("map"));
    pause(100);
}

} // namespace tp::app::selftest
