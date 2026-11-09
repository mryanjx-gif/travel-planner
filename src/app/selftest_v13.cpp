// 1.3.0 自检（分组 v13）：火车类交通沿自带的离线线路数据画（rail.h，资源 :/rail/）。
// 悉尼 Central → Town Hall 沿铁路、长度合理（编辑器那一行写"沿铁路"，地图上那一段是沿线路的点）；
// 雷克雅未克（没有数据的城市）回落直线并给原因。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "jsonio.h"
#include "mapcontroller.h"
#include "planeditor.h"
#include "rail.h"

#include <QQuickWindow>

namespace tp::app::selftest {

void runV13(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    Q_UNUSED(win);
    r.section(QStringLiteral("1.3.0 火车沿真实线路画"));
    data::DataStore *store = app->store();
    PlanEditor *ed = editorCtl();
    if (!r.check(ed, QStringLiteral("编辑器在"))) return;
    const json::Value original = store->data();
    store->modify([](json::Value &d) {
        const json::Value fx = json::parse(QStringLiteral(R"([
          {"id":"p_v13syd","name":"V13 悉尼火车","type":"route","status":"planning","stops":[
            {"id":"s_v13a","name":"Central","lat":-33.8832,"lng":151.2063,"leg":{"mode":"train"}},{"id":"s_v13b","name":"Town Hall","lat":-33.8731,"lng":151.2069}]},
          {"id":"p_v13rkv","name":"V13 雷克雅未克","type":"route","status":"planning","stops":[
            {"id":"s_v13c","name":"Hallgrimskirkja","lat":64.1466,"lng":-21.9426,"leg":{"mode":"train"}},{"id":"s_v13d","name":"Laugardalur","lat":64.1355,"lng":-21.8954}]}])"));
        json::Array &ps = d.mutObject().find(QStringLiteral("plans"))->mutArray();
        for (const json::Value &p : fx.asArray()) ps.push_back(p);
    });
    app->commitStep();

    // ---- 线路数据（资源里打包的） ----
    const map::rail::Result rr = map::rail::route(-33.8832, 151.2063, -33.8731, 151.2069);
    r.check(rr.ok() && rr.distM > 900 && rr.distM < 1800, QStringLiteral("悉尼 Central → Town Hall 沿线路，长度 0.9–1.8 km"),
            rr.ok() ? QString::number(rr.distM, 'f', 0) + QStringLiteral(" 米") : rr.reason);

    // ---- 地图：那一段画的是沿线路的点（站 → 吸附点 → … → 吸附点 → 站），不是大圆弧 ----
    const bool drawn = waitFor([mapc, &rr] {
        for (const auto &p : mapc->trips().plans())
            if (p.id == QLatin1String("p_v13syd")) return !p.segments.isEmpty() && p.segments[0].size() == rr.pts.size() + 2;
        return false;
    }, 4000);
    r.check(drawn, QStringLiteral("地图上那一段沿线路画"));

    // ---- 编辑器：沿铁路 · 距离；没数据的城市：直线 + 原因 ----
    auto legOf = [ed] { return ed->plan().value(QStringLiteral("stops")).toList().value(0).toMap().value(QStringLiteral("leg")).toMap(); };
    ed->openPlan(QStringLiteral("p_v13syd"));
    QVariantMap l = legOf();
    r.check(l.value(QStringLiteral("state")).toString() == QLatin1String("rail") && std::abs(l.value(QStringLiteral("dist")).toDouble() - rr.distM) < 1,
            QStringLiteral("编辑器：沿铁路 · 距离"), l.value(QStringLiteral("state")).toString());
    ed->discard();
    ed->openPlan(QStringLiteral("p_v13rkv"));
    l = legOf();
    r.check(l.value(QStringLiteral("state")).toString() == QLatin1String("railLine") && l.value(QStringLiteral("why")).toString() == QLatin1String("nodata"),
            QStringLiteral("编辑器：没数据的城市画直线，原因是还没有线路数据"),
            l.value(QStringLiteral("state")).toString() + u' ' + l.value(QStringLiteral("why")).toString());
    ed->discard();

    store->modify([&original](json::Value &d) { d = original; });
    app->commitStep();
}

} // namespace tp::app::selftest
