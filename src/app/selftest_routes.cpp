// 自检：路线悬停和道路路线（0.8.4）、瓦片（0.8.5）。0.8.7 从 selftest.cpp 拆出来，内容没改。
#include "selftest.h"
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "exif.h"
#include "globeitem.h"
#include "globetiles.h"
#include "mapcontroller.h"
#include "markerimages.h"
#include "netclient.h"
#include "roadrouter.h"
#include "roads.h"
#include "photocontroller.h"
#include "photolib.h"
#include "procmem.h"
#include "thumbcache.h"
#include "tilecache.h"
#include "tileserver.h"
#include "unlock.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutex>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <cmath>
#include <functional>
#include <memory>

namespace tp::app::selftest {

void runRoutes(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, services::RoadRouter *roads, services::NetClient *net)
{
    r.section(QStringLiteral("路线悬停（0.8.4）"));
    map::MarkerLayer *mk = mapc->markers();
    const auto &trips = mapc->trips();
    mapc->setBase(QStringLiteral("street"));
    mk->clearSelection();
    mk->setShown(true);
    // 找一条路线：第 1 段够长（屏幕上能找到不被标记挡住的中点）
    QString routeId, routeColor;
    QVector<map::geo::LatLng> seg;
    for (const auto &p : trips.plans()) {
        if (!p.isRoute() || p.status == QLatin1String("wish") || p.segments.isEmpty()) continue;
        const auto &s0 = p.segments[0];
        if (s0.size() < 2 || map::geo::distanceKm(s0.first(), s0.last()) < 80) continue;
        routeId = p.id;
        routeColor = map::TripData::stopColor(p, p.stops[0]);
        seg = s0;
        break;
    }
    if (routeId.isEmpty()) { r.check(true, QStringLiteral("路线悬停（跳过）"), QStringLiteral("数据里没有合适的路线")); }
    else {
        // 镜头放在第 1 段中间，缩放到这一段占屏幕一半左右
        const map::geo::LatLng a = seg.first(), b = seg.last();
        const map::geo::FitResult fit = map::geo::fitBounds(mapc->camera(), std::min(a.lng, b.lng), std::min(a.lat, b.lat),
                                                            std::max(a.lng, b.lng), std::max(a.lat, b.lat), { 200, 200, 200, 200 }, 12);
        mapc->jumpTo(fit.lat, fit.lng, fit.zoom);
        waitRendered(win, mapc, 2500);
        const QPointF pt = mapc->linePoint(routeId, 0);
        // 线的方向（屏幕上），用来取"粗线才盖得到、细线盖不到"的点：中心线旁边 2.2 px（细线半宽 1.75，粗线 2.5）
        const int j = int(seg.size()) / 2;
        const QPointF pa = mapc->pixelFor(seg[j - 1].lat, seg[j - 1].lng), pb = mapc->pixelFor(seg[j].lat, seg[j].lng);
        const double len = std::max(1e-6, std::hypot(pb.x() - pa.x(), pb.y() - pa.y()));
        const QPointF nrm(-(pb.y() - pa.y()) / len, (pb.x() - pa.x()) / len);
        const QPointF side = pt + nrm * 2.2;
        const QString hit = mapc->lineAt(pt.x(), pt.y());
        r.check(hit == routeId, QStringLiteral("碰到线：±4 px 内认得出是哪条路线"),
                QStringLiteral("(%1, %2) → %3").arg(pt.x(), 0, 'f', 0).arg(pt.y(), 0, 'f', 0).arg(hit.isEmpty() ? QStringLiteral("没有") : hit == routeId ? QStringLiteral("这条") : QStringLiteral("另一条")));
        const QImage before = win->grabWindow();
        const QColor want(routeColor);
        const double dBefore = colorDiff(colorAt(before, win, side.x(), side.y()), want);
        int otherRow = -1;
        for (int i = 0; i < mk->stops().size(); ++i)
            if (mk->stops()[i].planId != routeId && mk->stops()[i].route) { otherRow = i; break; }
        const QString tip = mapc->lineHover(pt.x(), pt.y());
        pause(450);
        const QImage after = win->grabWindow();
        const double dAfter = colorDiff(colorAt(after, win, side.x(), side.y()), want);
        const double otherOp = otherRow >= 0 ? mk->stops()[otherRow].op : 0.39;
        const bool okState = !tip.isEmpty() && mk->hoverPlan() == routeId && mk->hvPlan() == routeId && mapc->hvOpacity() > 0.99
                             && std::abs(mapc->routeOpacity() - 0.92 * 0.3) < 0.01 && std::abs(mapc->edgeOpacity() - 0.85 * 0.3) < 0.01
                             && std::abs(otherOp - 0.39) < 0.01;
        r.check(okState, QStringLiteral("碰到线：这条加粗、其余的线和白边 ×0.3、标记一起变淡"),
                QStringLiteral("提示框「%1」，高亮层 %2，其余线 %3 / 白边 %4，别的标记 %5")
                    .arg(tip).arg(mapc->hvOpacity(), 0, 'f', 2).arg(mapc->routeOpacity(), 0, 'f', 3).arg(mapc->edgeOpacity(), 0, 'f', 3).arg(otherOp, 0, 'f', 2));
        r.check(dAfter + 30 < dBefore || dAfter <= 40, QStringLiteral("碰到线：屏幕上真的变粗了（取色）"),
                QStringLiteral("中心线旁 2.2 px 和路线颜色的差：碰之前 %1，碰之后 %2").arg(dBefore).arg(dAfter));
        // 同一帧里标记和线是同一个程度（PO：要同步）：渐变途中读一次
        mapc->lineHover(-100, -100);   // 离开：1 秒后渐出
        pause(700);
        const bool lingered = mk->hvPlan() == routeId && mk->dimLevel() > 0.99;   // 还没到 1 秒：保持高亮
        waitFor([mk] { return mk->dimLevel() < 0.9; }, 2000);                       // 渐出途中
        const double dimMid = mk->dimLevel(), lineMid = mapc->routeOpacity(), markMid = otherRow >= 0 ? mk->stops()[otherRow].op : 0;
        const bool sync = std::abs(lineMid - 0.92 * mk->lineDimFactor()) < 1e-9
                          && (otherRow < 0 || std::abs(markMid - mk->markerDimFactor() * 0.65) < 1e-9);
        pause(600);
        const bool back = mk->hvPlan().isEmpty() && std::abs(mapc->routeOpacity() - 0.92) < 1e-6 && mk->dimLevel() == 0
                          && (otherRow < 0 || std::abs(mk->stops()[otherRow].op - 0.65) < 1e-6);
        r.check(lingered && sync && back && dimMid > 0.02, QStringLiteral("离开线：停 1 秒，再和标记一起渐出、恢复"),
                QStringLiteral("0.7 秒时还亮着 %1；渐出途中变淡程度 %2（线 %3、标记 %4，同一帧）；之后线 %5")
                    .arg(lingered).arg(dimMid, 0, 'f', 2).arg(lineMid, 0, 'f', 3).arg(markMid, 0, 'f', 3).arg(mapc->routeOpacity(), 0, 'f', 2));
        // 关掉动效：一步到位，下一帧就是最终的样子（maplibre 自己的 0.3 秒过渡关掉了：骨架样式 transition 0）
        mk->setMotion(false);
        mapc->lineHover(pt.x(), pt.y());
        pause(120);
        const double dInstant = colorDiff(colorAt(win->grabWindow(), win, side.x(), side.y()), want);
        mapc->lineHover(-100, -100);
        pause(1200);
        mk->setMotion(true);
        r.check(dInstant <= dAfter + 25, QStringLiteral("线条透明度立刻生效（没有地图库自带的过渡）"),
                QStringLiteral("0.12 秒后差 %1（渐变完是 %2）").arg(dInstant).arg(dAfter));
        // 点线 = 选中；选中的计划在"选中"那一份里，平时那份里没有
        const bool clicked = mapc->lineClick(pt.x(), pt.y());
        const bool selOk = clicked && mk->selectedPlan() == routeId && mapc->routesSelJson().contains(routeId) && !mapc->routesJson().contains(routeId);
        // 碰到选中的计划的线：有提示框、不做悬停高亮，其余照样变淡
        const QString tip2 = mapc->lineHover(pt.x(), pt.y());
        waitFor([mk] { return mk->dimLevel() > 0.99; }, 1000);
        const bool selHover = !tip2.isEmpty() && mk->hoverPlan().isEmpty() && mk->hvPlan().isEmpty() && mk->dimLevel() > 0.99;
        r.check(selOk && selHover, QStringLiteral("点线选中；碰到选中的计划的线不再高亮"),
                QStringLiteral("选中 %1，选中那份有它 %2、平时那份没它 %3，碰到时提示框「%4」，悬停 %5 / 高亮层 %6，其余变淡 %7")
                    .arg(mk->selectedPlan() == routeId).arg(mapc->routesSelJson().contains(routeId)).arg(!mapc->routesJson().contains(routeId)).arg(tip2)
                    .arg(mk->hoverPlan().isEmpty() ? QStringLiteral("无") : mk->hoverPlan(), mk->hvPlan().isEmpty() ? QStringLiteral("无") : mk->hvPlan())
                    .arg(mk->dimLevel(), 0, 'f', 2));
        mapc->lineLeave();
        mk->clearSelection();
        waitFor([mk] { return mk->dimLevel() == 0 && mk->hvPlan().isEmpty(); }, 2000);
    }

    r.section(QStringLiteral("道路路线（0.8.4）"));
    data::DataStore *store = app->store();
    // "算不出来"的记录用一个临时文件（每次自检从空的开始，不受上一次影响）
    const QString failFile = QDir::temp().filePath(QStringLiteral("tp-selftest-road-fail.json"));
    QFile::remove(failFile);
    roads->setFailFile(failFile);
    r.check(net->userAgent() == services::NetClient::userAgentFor(app->version()) && net->userAgent().startsWith(QStringLiteral("TravelPlanner/")),
            QStringLiteral("User-Agent"), net->userAgent());
    {
        bool got = false;
        services::NetClient::Error e = services::NetClient::Error::None;
        net->get(QUrl(QStringLiteral("https://example.com/")), 5000, nullptr, [&](const services::NetClient::Reply &rep) { got = true; e = rep.error; });
        waitFor([&] { return got; }, 2000);
        r.check(got && e == services::NetClient::Error::NotAllowed, QStringLiteral("白名单：不在名单里的地址不发出去"));
    }
    // 把几段自驾 / 徒步的道路折线去掉 → "还没算"
    struct Removed { QString planId; int stop; QString path; double dist, time; };
    QVector<Removed> removed;
    store->modify([&](json::Value &d) {
        for (json::Value &pv : d.mutObject().find(QStringLiteral("plans"))->mutArray()) {
            if (pv[QStringLiteral("type")].toString() != QLatin1String("route")) continue;
            json::Value *sv = pv.mutObject().find(QStringLiteral("stops"));
            if (!sv) continue;
            auto &arr = sv->mutArray();
            for (int i = 0; i + 1 < int(arr.size()) && removed.size() < 4; ++i) {
                json::Value *lv = arr[i].mutObject().find(QStringLiteral("leg"));
                if (!lv || !lv->isObject() || !map::roads::isRoadMode((*lv)[QStringLiteral("mode")].toString())) continue;
                if ((*lv)[QStringLiteral("path")].toString().isEmpty()) continue;
                removed.push_back({ pv[QStringLiteral("id")].toString(), i, (*lv)[QStringLiteral("path")].toString(),
                                    (*lv)[QStringLiteral("dist")].toNumber(), (*lv)[QStringLiteral("time")].toNumber() });
                json::Object &lo = lv->mutObject();
                lo.remove(QStringLiteral("path")); lo.remove(QStringLiteral("dist")); lo.remove(QStringLiteral("time")); lo.remove(QStringLiteral("key"));
            }
            if (removed.size() >= 4) break;
        }
    });
    app->noteChanged(true);
    waitFor([store] { return !store->isDirty(); }, 3000);
    roads->recount();
    if (removed.size() < 4) { r.check(true, QStringLiteral("道路路线（跳过）"), QStringLiteral("数据里自驾 / 徒步段不够 4 段")); return; }
    r.check(roads->pending() == 4 && roads->shown(), QStringLiteral("有 N 段还没算：提示条出来了"),
            QStringLiteral("去掉 4 段的道路折线后还没算 %1 段：%2").arg(roads->pending()).arg(roads->text()));
    // 批量计算（模拟的路线服务：把原来的折线当成服务返回的；最后 1 段留给下面真的请求一次）
    const QString realKeyPlan = removed.last().planId;
    const int realKeyStop = removed.last().stop;
    int fetched = 0;
    roads->setFetchForTest([&](const QUrl &url, std::function<void(int, const QByteArray &, int)> done) {
        ++fetched;
        const Removed &rm = removed[std::min<int>(fetched - 1, int(removed.size()) - 1)];
        // 最后一段：模拟"找不到路"（记住、不再算进还没算）
        if (fetched == int(removed.size())) {
            QTimer::singleShot(10, [done] { done(400, QByteArrayLiteral("{\"code\":\"NoRoute\",\"message\":\"Impossible route between points\"}"), int(services::NetClient::Error::Http)); });
            return;
        }
        Q_UNUSED(url)
        const QByteArray body = QJsonDocument(QJsonObject{ { "code", "Ok" }, { "routes", QJsonArray{ QJsonObject{
                                                  { "geometry", rm.path }, { "distance", rm.dist }, { "duration", rm.time } } } } })
                                    .toJson(QJsonDocument::Compact);
        QTimer::singleShot(10, [done, body] { done(200, body, 0); });
    });
    roads->start();
    const bool finished = waitFor([roads] { return !roads->running(); }, 10000);
    const int pendAfter = roads->pending();
    r.check(finished && roads->okCount() == 3 && roads->failCount() == 1 && pendAfter == 0 && !store->isDirty() && roads->done(),
            QStringLiteral("批量计算（模拟的路线服务）：写回、自动保存、找不到路的记住"),
            QStringLiteral("好了 %1 段、没算出来 %2 段，之后还没算 %3 段，%4；%5").arg(roads->okCount()).arg(roads->failCount()).arg(pendAfter)
                .arg(store->isDirty() ? QStringLiteral("没保存") : QStringLiteral("已保存"), roads->text()));
    // 撤销是一步：3 段一起回去
    app->undo();
    roads->recount();
    const int afterUndo = roads->pending();
    app->redo();
    roads->recount();
    r.check(afterUndo == 3 && roads->pending() == 0, QStringLiteral("批量计算：撤销是一步"), QStringLiteral("撤销后还没算 %1 段，重做后 %2 段").arg(afterUndo).arg(roads->pending()));
    roads->dismiss();
    roads->setFetchForTest({});
    // 真的请求一次（routing.openstreetmap.de，经统一请求通道）：把刚才"找不到路"的那段换成一段正常的再算
    {
        // 找一段还有折线的自驾段，去掉它的折线，真的算一次
        QString planId; int stop = -1;
        map::roads::Pending want;
        store->modify([&](json::Value &d) {
            for (json::Value &pv : d.mutObject().find(QStringLiteral("plans"))->mutArray()) {
                if (pv[QStringLiteral("type")].toString() != QLatin1String("route")) continue;
                json::Value *sv = pv.mutObject().find(QStringLiteral("stops"));
                if (!sv) continue;
                auto &arr = sv->mutArray();
                for (int i = 0; i + 1 < int(arr.size()); ++i) {
                    json::Value *lv = arr[i].mutObject().find(QStringLiteral("leg"));
                    if (!lv || !lv->isObject() || (*lv)[QStringLiteral("mode")].toString() != QLatin1String("drive")) continue;
                    if ((*lv)[QStringLiteral("path")].toString().isEmpty() || (*lv)[QStringLiteral("dist")].toNumber() > 300000) continue;
                    if (pv[QStringLiteral("id")].toString() == realKeyPlan && i == realKeyStop) continue;
                    json::Object &lo = lv->mutObject();
                    lo.remove(QStringLiteral("path")); lo.remove(QStringLiteral("key"));
                    planId = pv[QStringLiteral("id")].toString();
                    stop = i;
                    return;
                }
            }
        });
        if (stop < 0) { r.check(true, QStringLiteral("真的请求一次路线服务（跳过）"), QStringLiteral("数据里没有合适的自驾段")); return; }
        roads->recount();
        QElapsedTimer t;
        t.start();
        roads->start();
        const bool fin = waitFor([roads] { return !roads->running(); }, 30000);
        // 结果：道路折线的两头离站点不远
        QString detail;
        bool ok = false;
        json::Value pv;
        for (const json::Value &v : store->data()[QStringLiteral("plans")].asArray())
            if (v[QStringLiteral("id")].toString() == planId) pv = v;
        const json::Array &arr = pv[QStringLiteral("stops")].asArray();
        if (stop + 1 < int(arr.size())) {
            const json::Value &leg = arr[stop][QStringLiteral("leg")];
            const auto pts = map::roads::decodePolyline(leg[QStringLiteral("path")].toString());
            const map::geo::LatLng a{ arr[stop][QStringLiteral("lat")].jsToNumber(), arr[stop][QStringLiteral("lng")].jsToNumber() };
            const map::geo::LatLng b{ arr[stop + 1][QStringLiteral("lat")].jsToNumber(), arr[stop + 1][QStringLiteral("lng")].jsToNumber() };
            const double da = pts.size() >= 2 ? map::geo::distanceKm(pts.first(), a) : 1e9, db = pts.size() >= 2 ? map::geo::distanceKm(pts.last(), b) : 1e9;
            ok = fin && roads->okCount() == 1 && pts.size() >= 2 && da < 5 && db < 5 && !leg[QStringLiteral("key")].toString().isEmpty();
            detail = pts.size() < 2 ? QStringLiteral("%1 秒，没有拿到折线").arg(t.elapsed() / 1000.0, 0, 'f', 1)
                                    : QStringLiteral("%1 秒，折线 %2 个点，两头离站点 %3 / %4 km，%5 米").arg(t.elapsed() / 1000.0, 0, 'f', 1).arg(pts.size())
                                          .arg(da, 0, 'f', 2).arg(db, 0, 'f', 2).arg(leg[QStringLiteral("dist")].toNumber(), 0, 'f', 0);
        }
        if (!ok) {
            const QString key = map::roads::legKeyOf(QStringLiteral("drive"), arr.size() > size_t(stop) ? arr[stop][QStringLiteral("lat")].jsToNumber() : 0,
                                                     arr.size() > size_t(stop) ? arr[stop][QStringLiteral("lng")].jsToNumber() : 0,
                                                     arr.size() > size_t(stop + 1) ? arr[stop + 1][QStringLiteral("lat")].jsToNumber() : 0,
                                                     arr.size() > size_t(stop + 1) ? arr[stop + 1][QStringLiteral("lng")].jsToNumber() : 0);
            detail += QStringLiteral("（%1）").arg(roads->failReason(key).isEmpty() ? QStringLiteral("没结果") : roads->failReason(key));
        }
        r.check(ok, QStringLiteral("真的请求一次路线服务（routing.openstreetmap.de）"), detail);
        roads->dismiss();
    }
}

// ---------- 瓦片（0.8.5） ----------

// 直接向本机瓦片服务要一张（不经 maplibre），返回 HTTP 状态码
static int tileGet(services::TileServer *ts, const QString &path, QByteArray *body = nullptr)
{
    QNetworkAccessManager nam;
    QNetworkReply *rep = nam.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(ts->port()).arg(path))));
    waitFor([rep] { return rep->isFinished(); }, 25000);
    const int st = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (body) *body = rep->readAll();
    delete rep;
    return st;
}

void runTiles(Report &r, map::MapController *mapc, QQuickWindow *win, services::NetClient *net, services::TileServer *ts, services::TileCache *cache)
{
    r.section(QStringLiteral("瓦片（0.8.5）"));
    Q_UNUSED(win)
    // 1. 服务起来了，样式里的底图地址指向它
    {
        // styleUrl 是 "file://" + 路径（Windows 上是 file://D:/…，不能交给 QUrl 解析，D: 会被当成主机名）
        mapc->setBase(QStringLiteral("street"));   // 0.8.6 起样式里只有当前底图
        QFile f(mapc->styleUrl().mid(7));
        const QByteArray style = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        const QString want = QStringLiteral("127.0.0.1:%1/tile/street/").arg(ts->port());
        r.check(ts->running() && style.contains(want.toUtf8()), QStringLiteral("本机瓦片服务起来了，底图走它"),
                QStringLiteral("端口 %1；样式文件里%2").arg(ts->port()).arg(style.contains(want.toUtf8()) ? QStringLiteral("是本机地址") : QStringLiteral("没有本机地址")));
    }
    // 2. 地图要的瓦片经过本机服务、存进了硬盘缓存（前面"底图瓦片画出来了"那一项已经看过街道图）
    const services::TileServer::Stats st = ts->stats();
    r.check(st.requests > 0 && cache->count() > 0 && st.fetched + st.hits > 0, QStringLiteral("地图的瓦片经过本机服务、存进硬盘缓存"),
            QStringLiteral("地图要了 %1 张，缓存里有的 %2 张，联网取到 %3 张、没取到 %4 张；缓存一共 %5 张 %6 MB（%7）")
                .arg(st.requests).arg(st.hits).arg(st.fetched).arg(st.failed).arg(cache->count())
                .arg(cache->bytes() / 1048576.0, 0, 'f', 1).arg(QDir::toNativeSeparators(cache->dir())));
    // 3. 缓存里有的直接给（不联网）；路径不对的 404
    {
        // 找一张缓存里有的：从样式里能看到的街道图 4 级瓦片挨个试
        QString hitPath;
        for (int x = 0; x < 16 && hitPath.isEmpty(); ++x)
            for (int y = 0; y < 16 && hitPath.isEmpty(); ++y)
                if (cache->contains({ QStringLiteral("street"), 4, x, y })) hitPath = QStringLiteral("/tile/street/4/%1/%2").arg(y).arg(x);
        if (hitPath.isEmpty())
            for (int z = 3; z <= 6 && hitPath.isEmpty(); ++z)
                for (int x = 0; x < (1 << z) && hitPath.isEmpty(); ++x)
                    for (int y = 0; y < (1 << z) && hitPath.isEmpty(); ++y)
                        if (cache->contains({ QStringLiteral("street"), z, x, y })) hitPath = QStringLiteral("/tile/street/%1/%2/%3").arg(z).arg(y).arg(x);
        const int fetchedBefore = ts->stats().fetched + ts->stats().failed, hitsBefore = ts->stats().hits;
        QByteArray body;
        const int code = hitPath.isEmpty() ? 0 : tileGet(ts, hitPath, &body);
        const bool fromCache = code == 200 && ts->stats().hits == hitsBefore + 1 && ts->stats().fetched + ts->stats().failed == fetchedBefore;
        const int bad = tileGet(ts, QStringLiteral("/tile/../settings.json"));
        r.check(fromCache && bad == 404, QStringLiteral("缓存里有的直接给（不联网），路径不对的不给"),
                QStringLiteral("%1 → %2（%3 字节）；/tile/../settings.json → %4").arg(hitPath.isEmpty() ? QStringLiteral("（缓存里没有街道图的瓦片）") : hitPath)
                    .arg(code).arg(body.size()).arg(bad));
    }
    // 4. 预取：停下以后把上下各一级先取回来（最多 48 张）
    {
        mapc->setBase(QStringLiteral("street"));
        mapc->jumpTo(48.86, 2.35, 9.3);   // 巴黎附近（之前没看过）
        waitMapIdle(win, mapc);
        // 这次视野要预取的瓦片里还有几张不在缓存里（上次验证取过的会在缓存里）
        const map::geo::Camera &cam = mapc->camera();
        const map::geo::LatLng nw = cam.coordinateFor(0, 0), se = cam.coordinateFor(cam.width, cam.height);
        int candidates = 0, missing = 0;
        for (const map::geo::TileXY &t : map::geo::prefetchTiles(nw.lng, nw.lat, se.lng, se.lat, cam.zoom, 256, 19)) {
            ++candidates;
            if (!cache->contains({ QStringLiteral("street"), t.z, t.x, t.y })) ++missing;
        }
        const int before = ts->stats().prefetched;
        const int queued = mapc->prefetchNow();
        pause(50);
        waitFor([ts] { return !ts->prefetchBusy(); }, 20000);   // 地图同时在要的那几张不再重复取（算在地图那边）
        const int got = ts->stats().prefetched - before;
        const bool allCached = candidates > 0 && missing == 0 && queued == 0;
        r.check(allCached || (queued > 0 && queued <= services::TileServer::kPrefetchMax && got >= queued * 3 / 4), QStringLiteral("预取：停下后取上下各一级"),
                allCached ? QStringLiteral("这里的 %1 张都已经在缓存里（之前取过），不用再取").arg(candidates)
                          : QStringLiteral("该取 %1 张、缓存里没有 %2 张；排了 %3 张（上限 %4），取回 %5 张").arg(candidates).arg(missing).arg(queued)
                                .arg(services::TileServer::kPrefetchMax).arg(got));
    }
    // 5. 连续 8 次失败出横幅；成功一次清零；换底图清零（用模拟的结果，不真的断网）
    {
        const QSignalBlocker block(ts);   // 这期间瓦片服务的真结果不算（没联网时真的失败会混进来）
        mapc->setBase(QStringLiteral("sat"));
        mapc->setBase(QStringLiteral("street"));   // 换一次底图：从 0 开始数
        for (int i = 0; i < 7; ++i) mapc->tileResultForTest(QStringLiteral("street"), false);
        const bool at7 = !mapc->tileBanner() && mapc->tileErrors() == 7;
        mapc->tileResultForTest(QStringLiteral("street"), true);
        const bool reset = mapc->tileErrors() == 0;
        for (int i = 0; i < 8; ++i) mapc->tileResultForTest(QStringLiteral("street"), false);
        const bool banner = mapc->tileBanner();
        mapc->tileResultForTest(QStringLiteral("sat"), true);   // 别的底图的结果不算
        const bool otherIgnored = mapc->tileBanner();
        mapc->setBase(QStringLiteral("sat"));
        const bool baseReset = !mapc->tileBanner() && mapc->tileErrors() == 0;
        mapc->setBase(QStringLiteral("street"));
        r.check(at7 && reset && banner && otherIgnored && baseReset, QStringLiteral("连续 8 次没取到出横幅；成功一次、换底图都清零"),
                QStringLiteral("7 次：%1；成功一次后：%2；8 次：%3；换底图后：%4").arg(at7 ? QStringLiteral("还没出") : QStringLiteral("不对"))
                    .arg(reset ? QStringLiteral("清零") : QStringLiteral("没清零")).arg(banner ? QStringLiteral("出横幅") : QStringLiteral("没出"))
                    .arg(baseReset ? QStringLiteral("清零、横幅收起") : QStringLiteral("没清零")));
    }
    // 6. 离线：马上切到"已解锁"、选在线底图被拦；恢复后能选
    {
        mapc->setBase(QStringLiteral("sat"));
        int toasts = 0;
        const QMetaObject::Connection tc = QObject::connect(mapc, &map::MapController::toast, mapc, [&toasts] { ++toasts; });
        net->setOffline(true);
        const bool forced = mapc->base() == QLatin1String("unlock") && mapc->tilesOff();
        mapc->setBase(QStringLiteral("street"));
        const bool blocked = mapc->base() == QLatin1String("unlock") && toasts == 1;
        QObject::disconnect(tc);
        const int offCode = tileGet(ts, QStringLiteral("/tile/street/17/1/1"));   // 缓存里没有：不联网，503
        net->setOffline(false);
        mapc->setBase(QStringLiteral("street"));
        const bool back = mapc->base() == QLatin1String("street") && !mapc->tilesOff();
        net->setServiceEnabled(QStringLiteral("tiles"), false);
        const bool tilesOnly = mapc->base() == QLatin1String("unlock");
        net->setServiceEnabled(QStringLiteral("tiles"), true);
        mapc->setBase(QStringLiteral("street"));
        r.check(forced && blocked && offCode == 503 && back && tilesOnly, QStringLiteral("离线：切到已解锁、在线底图选不了；联网后恢复"),
                QStringLiteral("离线 → %1；再选街道图 → %2（提示 %3 次）；缓存里没有的瓦片 → %4；联网后 → %5；只关瓦片 → %6")
                    .arg(forced ? QStringLiteral("已解锁") : QStringLiteral("没切"))
                    .arg(blocked ? QStringLiteral("还是已解锁") : QStringLiteral("切过去了")).arg(toasts).arg(offCode)
                    .arg(back ? QStringLiteral("能选街道图") : QStringLiteral("选不了")).arg(tilesOnly ? QStringLiteral("已解锁") : QStringLiteral("没切")));
    }
}

} // namespace tp::app::selftest
