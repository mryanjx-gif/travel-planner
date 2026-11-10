// 1.4.0 自检（分组 v14）：简易计划面板里的地点介绍（placeinfo.h）。
// 自检不联网（main.cpp 给 testRun 设了 setNoNet，缓存用 cache\test-place）：
// 聚焦有缓存的一站 → 横杠和卡片出来、显示缓存的简介；面板内容能往下滑；切到没缓存的一站 → "没联网"，不报错。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "jsonio.h"
#include "mapcontroller.h"
#include "markerlayer.h"
#include "navhistory.h"
#include "placeinfo.h"
#include "plancontroller.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

namespace tp::app::selftest {

void runV14(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("1.4.0 地点介绍"));
    data::DataStore *store = app->store();
    PlanController *pc = plansCtl();
    QQuickItem *sheet = win->findChild<QQuickItem *>(QStringLiteral("planSheet"));
    QQmlContext *ctx = sheet ? QQmlEngine::contextForObject(sheet) : nullptr;
    auto *place = ctx ? qobject_cast<services::PlaceInfo *>(ctx->contextProperty(QStringLiteral("place")).value<QObject *>()) : nullptr;
    if (!r.check(pc && sheet && place, QStringLiteral("简易面板和 place 在"))) return;

    const json::Value original = store->data();
    // 第一站日程排得长一点，面板内容一定超出高度（测能滑）
    QString sched;
    for (int i = 0; i < 24; ++i) sched += QStringLiteral(R"({"time":"%1:00","text":"V14 安排 %2"},)").arg(8 + i % 12).arg(i + 1);
    sched.chop(1);
    store->modify([&sched](json::Value &d) {
        const json::Value fx = json::parse(QStringLiteral(R"({"id":"p_v14","name":"V14 悉尼","type":"route","status":"planning","stops":[
            {"id":"s_v14a","name":"Sydney Opera House","lat":-33.8568,"lng":151.2153,"arriveDate":"2030-01-05","tz":"Australia/Sydney","schedule":[%1]},
            {"id":"s_v14b","name":"V14 Nowhere Lookout","lat":-33.80,"lng":151.10,"arriveDate":"2030-01-06"}]})").arg(sched));
        d.mutObject().find(QStringLiteral("plans"))->mutArray().push_back(fx);
    });
    app->commitStep();
    place->putCacheForTest(QStringLiteral("Sydney Opera House"), -33.8568, 151.2153, QVariantMap{
        { QStringLiteral("osm"), QVariantMap{ { QStringLiteral("hours"), QStringLiteral("Mo-Su 09:00-17:00") }, { QStringLiteral("fee"), QStringLiteral("yes") } } },
        { QStringLiteral("qid"), QStringLiteral("Q45178") },
        { QStringLiteral("ent"), QVariantMap{ { QStringLiteral("qid"), QStringLiteral("Q45178") }, { QStringLiteral("label"), QVariantMap{ { QStringLiteral("en"), QStringLiteral("Sydney Opera House") } } } } },
        { QStringLiteral("sum"), QVariantMap{ { app->lang(), QVariantMap{ { QStringLiteral("title"), QStringLiteral("V14 歌剧院") }, { QStringLiteral("text"), QStringLiteral("V14 缓存里的简介") },
                                                                     { QStringLiteral("url"), QStringLiteral("https://en.wikipedia.org/wiki/Sydney_Opera_House") } } } } } });

    // 地图上有了这个计划再选中（数据改了以后地图是下一轮才更新的）
    waitFor([mapc] { for (const auto &p : mapc->trips().plans()) if (p.id == QLatin1String("p_v14")) return true; return false; }, 3000);
    if (NavHistory *nav = navCtl()) nav->go(pc->mode());   // 前面的分组可能停在别的页面（简易面板只在地图页出来）
    pc->openPlan(QStringLiteral("p_v14"), false);
    waitFor([pc] { return pc->sheet().value(QStringLiteral("id")).toString() == QLatin1String("p_v14"); }, 2000);
    mapc->markers()->selectPlan(QStringLiteral("p_v14"), 0, false);
    r.check(waitFor([sheet, win] { return sheet->isVisible() && sheet->x() + sheet->width() <= win->width(); }, 3000), QStringLiteral("简易面板打开了"),
            QStringLiteral("计划 %1，x = %2").arg(pc->sheet().value(QStringLiteral("id")).toString()).arg(sheet->x()));
    auto item = [win](const char *n) { return win->findChild<QQuickItem *>(QString::fromLatin1(n)); };
    auto shown = [&](const char *n) { QQuickItem *it = item(n); return it && it->isVisible(); };
    const bool card = waitFor([&] { return shown("sheetIntroLine") && shown("sheetIntro"); }, 3000);
    r.check(card, QStringLiteral("聚焦一站时面板里出现横杠和介绍卡片"));
    QQuickItem *txt = item("sheetIntroText");
    const bool cached = waitFor([txt] { return txt && txt->isVisible() && txt->property("text").toString() == QStringLiteral("V14 缓存里的简介"); }, 2000);
    r.check(cached, QStringLiteral("缓存命中：显示缓存里的简介，不联网"), txt ? txt->property("text").toString() : QStringLiteral("找不到"));
    r.check(place->info().value(QStringLiteral("state")).toString() == QLatin1String("ok") && place->info().contains(QStringLiteral("sun")),
            QStringLiteral("有缓存时状态正常，日出日落本机算出来了"), place->info().value(QStringLiteral("state")).toString());

    // 面板内容能往下滑，滑到底能看到卡片
    QQuickItem *body = item("sheetBody");
    bool scrolls = false;
    if (body) {
        const double maxY = body->property("contentHeight").toDouble() - body->height();
        body->setProperty("contentY", maxY);
        waitFrames(win, 2);
        QQuickItem *intro = item("sheetIntro");
        const double y = intro ? intro->mapToItem(body, QPointF(0, 0)).y() : -1;
        scrolls = maxY > 0 && body->property("contentY").toDouble() > 0 && y < body->height();
        r.check(scrolls, QStringLiteral("面板内容超出时能往下滑，滑下去看得到卡片"), QStringLiteral("可滑 %1 px，卡片在 %2").arg(maxY, 0, 'f', 0).arg(y, 0, 'f', 0));
    } else r.check(false, QStringLiteral("面板内容超出时能往下滑"), QStringLiteral("找不到 sheetBody"));

    // 切到没缓存的一站：不联网，说"没联网"
    mapc->markers()->selectPlan(QStringLiteral("p_v14"), 1, false);
    const bool offline = waitFor([place] { return place->info().value(QStringLiteral("state")).toString() == QLatin1String("offline"); }, 2000);
    QQuickItem *st = item("sheetIntroState");
    r.check(offline && st && st->isVisible() && !st->property("text").toString().isEmpty(), QStringLiteral("切站跟着换；没缓存、不联网时显示一句\"没联网\""),
            st ? st->property("text").toString() : QStringLiteral("找不到"));
    r.check(!item("sheetIntroText")->isVisible(), QStringLiteral("没有简介时那一项不占位"));
    r.check(place->requests() == 0, QStringLiteral("自检里没有真的发请求"), QString::number(place->requests()));

    mapc->markers()->clearSelection();
    store->modify([&original](json::Value &d) { d = original; });
    app->commitStep();
}

} // namespace tp::app::selftest
