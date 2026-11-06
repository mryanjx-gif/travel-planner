// 自检：地图（0.8.0）、标记（0.8.2）。0.8.7 从 selftest.cpp 拆出来，内容没改。
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

void runMap(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("地图（0.8.0）"));
    mapc->markers()->setShown(false);   // 0.8.2 的标记盖在探针上会挡住；这一节只看 maplibre 画的东西
    QQuickItem *item = win ? win->findChild<QQuickItem *>(QStringLiteral("map")) : nullptr;
    if (!r.check(item != nullptr, QStringLiteral("地图库加载了"), item ? QString() : QStringLiteral("没有地图项（maplibre 没装好？）"))) return;
    const bool loaded = waitFor([item] { return item->property("styleLoaded").toBool(); }, 20000);
    r.check(loaded, QStringLiteral("样式加载完成"), mapc->styleUrl());
    if (!loaded) return;
    waitRendered(win, mapc, 1500);

    // 1. 投影：探针（maplibre 画的品红圆点）实际出现的位置 = 我们算的位置
    struct Cam { double lat, lng, zoom, plat, plng; const char *name; };
    const Cam cams[] = {
        { 30, 105, 3, 31.23, 121.47, "缩放 3（上海）" },
        { 60, 8, 6.5, 60.39, 5.32, "缩放 6.5、高纬度（卑尔根）" },
        { 0, 178, 2.5, -14, -171, "跨 180° 经线（萨摩亚，在另一份世界副本上）" },
        { 35.68, 139.76, 12, 35.6812, 139.7671, "缩放 12（东京站）" },
    };
    double worst = 0;
    for (const Cam &c : cams) {
        mapc->setBase(QStringLiteral("street"));
        mapc->jumpTo(c.lat, c.lng, c.zoom);
        mapc->setProbe(c.plat, c.plng);
        waitRendered(win, mapc, 2000);
        const QPointF want = mapc->pixelFor(c.plat, c.plng);
        double fx = 0, fy = 0;
        const bool found = findProbe(win, want.x(), want.y(), &fx, &fy);
        const double err = found ? std::hypot(fx - want.x(), fy - want.y()) : 999;
        worst = std::max(worst, err);
        r.check(found && err <= 1.5, QStringLiteral("投影：") + QString::fromUtf8(c.name),
                found ? QStringLiteral("算的 (%1, %2)，画出来 (%3, %4)，差 %5 像素").arg(want.x(), 0, 'f', 1).arg(want.y(), 0, 'f', 1)
                            .arg(fx, 0, 'f', 1).arg(fy, 0, 'f', 1).arg(err, 0, 'f', 2)
                      : QStringLiteral("探针没找到（算的位置 %1, %2）").arg(want.x(), 0, 'f', 1).arg(want.y(), 0, 'f', 1));
    }
    // 飞行途中：截图那一刻的相机就是画出来的那一帧
    {
        // 上海 → 东京，探针在首尔（一路都在窗口里）
        mapc->jumpTo(31.23, 121.47, 4);
        mapc->setProbe(37.57, 126.98);
        waitRendered(win, mapc, 1500);
        mapc->flyTo(35.68, 139.76, 5, 2400);
        pause(1100);
        const QPointF want = mapc->pixelFor(37.57, 126.98);
        double fx = 0, fy = 0;
        const bool inView = want.x() > 30 && want.y() > 30 && want.x() < win->width() - 30 && want.y() < win->height() - 30;
        const bool found = inView && findProbe(win, want.x(), want.y(), &fx, &fy);
        const double err = found ? std::hypot(fx - want.x(), fy - want.y()) : 999;
        r.check(!inView || (found && err <= 2.0), QStringLiteral("投影：飞行途中"),
                !inView ? QStringLiteral("这一刻探针不在窗口里，跳过") : found ? QStringLiteral("差 %1 像素（缩放 %2）").arg(err, 0, 'f', 2).arg(mapc->zoom(), 0, 'f', 2)
                                                                              : QStringLiteral("探针没找到"));
        waitFor([mapc] { return !mapc->moving(); }, 5000);
        r.check(std::abs(mapc->lat() - 35.68) < 0.01 && std::abs(mapc->lng() - 139.76) < 0.01 && std::abs(mapc->zoom() - 5) < 0.01,
                QStringLiteral("飞到终点"), QStringLiteral("%1, %2, 缩放 %3").arg(mapc->lat(), 0, 'f', 3).arg(mapc->lng(), 0, 'f', 3).arg(mapc->zoom(), 0, 'f', 2));
    }

    // 2. 换底图换样式（0.8.6）：样式里只有当前这一种底图（隐藏的底图不再留瓦片），换完路线、探针这些数据层还在
    {
        int ok = 0, n = 0;
        QStringList bad;
        for (const char *b0 : { "sat", "unlock", "street", "unlock", "sat", "street" }) {   // 0.9.0 起没有浅灰
            const QString b = QString::fromLatin1(b0);
            ++n;
            mapc->setBase(b);
            waitFor([item] { return !item->property("styleLoaded").toBool(); }, 1500);
            const bool loadedAgain = waitFor([item] { return item->property("styleLoaded").toBool(); }, 10000);
            QFile f(mapc->styleUrl().mid(7));   // "file://" + 路径（Windows 上是 file://D:/…）
            const QJsonObject src = QJsonDocument::fromJson(f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray()).object().value(QStringLiteral("sources")).toObject();
            QStringList rasters;
            int ts = 0;
            for (auto it = src.begin(); it != src.end(); ++it)
                if (it.value().toObject().value(QStringLiteral("type")).toString() == QLatin1String("raster")) {
                    rasters << it.key();
                    ts = it.value().toObject().value(QStringLiteral("tileSize")).toInt();
                }
            const bool good = loadedAgain && (b == QLatin1String("unlock") ? rasters.isEmpty()
                                                                           : rasters == QStringList{ QStringLiteral("base-") + b } && ts == mapc->tileSizeOf(b));
            if (good) ++ok; else bad << QStringLiteral("%1：%2").arg(b, rasters.join(QLatin1Char(',')));
            waitFrames(win, 2);
        }
        r.check(ok == n, QStringLiteral("换底图换样式，样式里只有当前底图"),
                ok == n ? QStringLiteral("切了 %1 次，每次都只有当前底图").arg(n) : QStringLiteral("不对的：%1").arg(bad.join(QStringLiteral("；"))));
        // 换完样式，数据层（这里用探针）由地图库重新加上了
        mapc->jumpTo(30, 105, 3);
        mapc->setProbe(31.23, 121.47);
        waitRendered(win, mapc, 2500);
        const QPointF want = mapc->pixelFor(31.23, 121.47);
        double fx = 0, fy = 0;
        const bool found = findProbe(win, want.x(), want.y(), &fx, &fy);
        r.check(found, QStringLiteral("换样式以后数据层还在"), found ? QStringLiteral("探针在") : QStringLiteral("探针没出现"));
        // 卫星图瓦片固定 128，和 Electron 一样（PO 2026-11-05：要清楚）
        const int sat = mapc->tileSizeOf(QStringLiteral("sat"));
        r.check(sat == 128, QStringLiteral("卫星图瓦片 128 像素（和 Electron 一样清楚）"),
                QStringLiteral("屏幕缩放比例 %1，瓦片 %2 像素").arg(mapc->dpr(), 0, 'f', 2).arg(sat));
    }

    // 2b. 底图瓦片在最下面、没被同色的垫底多边形盖住（0.8.2 第一版的错：垫底图层后加的，盖住了全部瓦片）
    {
        const QJsonArray layers = QJsonDocument::fromJson(map::MapController::skeletonStyleJson()).object().value(QStringLiteral("layers")).toArray();
        QStringList order;
        for (const QJsonValue &l : layers) order << l.toObject().value(QStringLiteral("id")).toString();
        const bool orderOk = order.indexOf(QStringLiteral("bg")) == 0 && order.indexOf(QStringLiteral("bg-fill")) == 1
                             && order.indexOf(QStringLiteral("base-street")) > 1;
        // 屏幕上：街道图下，陆地上几个点不全是底色（没联网 / 瓦片被拦时这一项会失败，看日志里有没有 "Failed to load tile"）
        mapc->setBase(QStringLiteral("street"));
        mapc->setProbe(0, 0, false);
        mapc->jumpTo(30, 105, 4);
        waitRendered(win, mapc, 4000);
        const QImage shot = win->grabWindow();
        int bg = 0;
        const double dpr = double(shot.width()) / std::max(1, win->width());
        for (const QPointF &p : { QPointF(0.3, 0.3), QPointF(0.5, 0.45), QPointF(0.7, 0.35), QPointF(0.4, 0.6) }) {
            const QRgb c = shot.pixel(int(win->width() * p.x() * dpr), int(win->height() * p.y() * dpr));
            if (std::abs(qRed(c) - 0xe8) < 3 && std::abs(qGreen(c) - 0xe2) < 3 && std::abs(qBlue(c) - 0xd6) < 3) ++bg;
        }
        r.check(orderOk && bg < 4, QStringLiteral("底图瓦片画出来了（没被垫底的图层盖住）"),
                QStringLiteral("图层顺序 %1；中国一带 4 个点里 %2 个是底色").arg(order.mid(0, 3).join(QStringLiteral(" → "))).arg(bg));
    }

    // 3. 骨架样式放在中文文件夹里也能读（程序清单里的 UTF-8 代码页）
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("地图样式测试"));
        QDir().mkpath(dir);
        const QString file = dir + QStringLiteral("/样式.json");
        QFile f(file);
        const bool wrote = f.open(QIODevice::WriteOnly) && f.write(map::MapController::skeletonStyleJson()) > 0;
        f.close();
        mapc->jumpTo(30, 105, 3);
        mapc->setProbe(31.23, 121.47);
        mapc->setTestStyleUrl(QStringLiteral("file://") + QDir::fromNativeSeparators(file));   // 不直接改地图项的 style（会断掉绑定，0.8.6）
        waitFor([item] { return !item->property("styleLoaded").toBool(); }, 1500);
        const bool reloaded = waitFor([item] { return item->property("styleLoaded").toBool(); }, 10000);
        waitRendered(win, mapc, 2000);
        const QPointF want = mapc->pixelFor(31.23, 121.47);
        double fx = 0, fy = 0;
        const bool found = findProbe(win, want.x(), want.y(), &fx, &fy);
        r.check(wrote && reloaded && found, QStringLiteral("中文路径里的样式文件"),
                QStringLiteral("%1；%2").arg(QDir::toNativeSeparators(file), found ? QStringLiteral("读到了，图层都在") : QStringLiteral("没读到（探针没出现）")));
        mapc->setTestStyleUrl(QString());
        waitFor([item] { return !item->property("styleLoaded").toBool(); }, 1500);
        waitFor([item] { return item->property("styleLoaded").toBool(); }, 10000);
        waitRendered(win, mapc, 1500);
    }

    // 4. 昼夜阴影在 180° 经线两边都画了：同一个画面截两次——太阳在 (0, 180)（180° 经线是白天）和 (0, 0)（是半夜），
    //    夜里那张在经线两边都应该明显变暗（Electron 最深 .62，亮度约剩 38%），而且两边暗得一样
    {
        mapc->setBase(QStringLiteral("sat"));
        mapc->setProbe(0, 0, false);
        mapc->jumpTo(5, 180, mapc->minZoom() + 0.05);
        mapc->setNightSun(0, 180);
        waitRendered(win, mapc, 4000);   // 等卫星瓦片
        const QImage day = win->grabWindow();
        mapc->setNightSun(0, 0);
        waitRendered(win, mapc, 1500);
        const QImage night = win->grabWindow();
        QStringList detail;
        bool ok = !day.isNull() && !night.isNull();
        for (double lat : { 0.0, 20.0, -20.0 }) {
            const QPointF a = mapc->pixelFor(lat, 178), b = mapc->pixelFor(lat, -178);
            const double da = luminanceAt(day, win, a.x(), a.y()), db = luminanceAt(day, win, b.x(), b.y());
            const double na = luminanceAt(night, win, a.x(), a.y()), nb = luminanceAt(night, win, b.x(), b.y());
            const double ka = (na + 1) / (da + 1), kb = (nb + 1) / (db + 1);   // 夜里剩下多少亮度
            ok = ok && ka < 0.75 && kb < 0.75 && std::abs(ka - kb) < 0.15;
            detail << QStringLiteral("纬度 %1：东边剩 %2%、西边剩 %3%").arg(lat).arg(ka * 100, 0, 'f', 0).arg(kb * 100, 0, 'f', 0);
        }
        r.check(ok, QStringLiteral("昼夜阴影：180° 经线两边都画了"), QStringLiteral("缩放 %1；").arg(mapc->zoom(), 0, 'f', 2) + detail.join(QStringLiteral("，")));
        mapc->clearNightSun();
    }

    // 5. "已解锁"上色每次一样
    {
        mapc->setBase(QStringLiteral("unlock"));
        mapc->recomputeUnlockForTest();
        const auto a = mapc->unlockResult().colorById;
        mapc->recomputeUnlockForTest();
        const auto b = mapc->unlockResult().colorById;
        r.check(mapc->unlockLoaded() && !a.isEmpty() && a == b, QStringLiteral("已解锁：颜色每次一样"),
                QStringLiteral("%1 个国家 / 地区、%2 个省 / 州").arg(mapc->unlockResult().countries).arg(mapc->unlockResult().provinces));
    }

    // 6. 改数据后地图跟着变，撤销后变回去
    {
        mapc->setBase(QStringLiteral("street"));
        data::DataStore *store = app->store();
        const QString before = mapc->routesJson();
        bool changed = false;
        store->modify([&changed](json::Value &d) {
            json::Value *plans = d.mutObject().find(QStringLiteral("plans"));
            if (!plans) return;
            for (json::Value &p : plans->mutArray()) {
                json::Object &po = p.mutObject();
                if (po.find(QStringLiteral("type")) == nullptr || po.find(QStringLiteral("type"))->toString() != QLatin1String("route")) continue;
                json::Value *stops = po.find(QStringLiteral("stops"));
                if (!stops || stops->asArray().size() < 2) continue;
                json::Object &s0 = stops->mutArray().front().mutObject();
                const json::Value *lat = s0.find(QStringLiteral("lat"));
                if (!lat) continue;
                s0.set(QStringLiteral("lat"), json::Value(lat->jsToNumber() + 0.5));
                changed = true;
                return;
            }
        });
        app->noteChanged(false);
        const bool followed = waitFor([mapc, before] { return mapc->routesJson() != before; }, 2000);
        app->undo();
        const bool back = waitFor([mapc, before] { return mapc->routesJson() == before; }, 3000);
        r.check(changed && followed && back, QStringLiteral("改数据后地图跟着变，撤销后变回去"),
                QStringLiteral("改了第一条路线的第一站：%1；撤销：%2").arg(followed ? QStringLiteral("跟着变了") : QStringLiteral("没变"),
                                                                         back ? QStringLiteral("变回去了") : QStringLiteral("没变回去")));
    }
    mapc->setProbe(0, 0, false);
    mapc->fitAll(false);
}

void runMarkers(Report &r, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("地图标记（0.8.2）"));
    mapc->markers()->setShown(true);
    map::MarkerLayer *mk = mapc->markers();
    const auto &trips = mapc->trips();
    r.check(map::MarkerImages::iconReady(), QStringLiteral("图标字体加载了"), map::MarkerImages::iconFamily());
    r.check(mk->stops().size() == trips.stopCount(), QStringLiteral("每个站点一个标记"),
            QStringLiteral("标记 %1 个，站点 %2 个").arg(mk->stops().size()).arg(trips.stopCount()));
    if (mk->stops().isEmpty()) return;
    mapc->setBase(QStringLiteral("street"));
    mk->clearSelection();

    // 找一条站点 ≥ 4 个的路线（后面选中、聚焦、切站都用它）
    QString routeId;
    int routeStops = 0;
    for (const auto &p : trips.plans())
        if (p.isRoute() && p.stops.size() >= 4 && p.status != QLatin1String("wish")) { routeId = p.id; routeStops = int(p.stops.size()); break; }

    // 1. 合并：0–5 级有圆圈、6 级起全部展开；每个被合并的标记恰好在一个圆圈里
    {
        QStringList detail;
        bool ok = true;
        for (double z : { 1.5, 3.5, 5.5, 6.5 }) {
            mapc->jumpTo(30, 100, z);
            const int tier = mk->clusterTier();
            int clustered = 0, inClusters = 0;
            for (const auto &s : mk->stops()) clustered += s.clustered ? 1 : 0;
            for (const auto &c : mk->clusters()) inClusters += int(c.members.size());
            const bool good = (z < 6 ? tier == int(z) : tier == -1) && clustered == inClusters && (z < 6 || mk->clusters().isEmpty());
            ok = ok && good;
            detail << QStringLiteral("%1 级 %2 个圆圈").arg(z, 0, 'f', 1).arg(mk->clusters().size());
        }
        r.check(ok, QStringLiteral("合并：5 层，6 级起展开"), detail.join(QStringLiteral("，")));
    }

    // 2. 位置：没错开时标记的锚点 = pixelFor（和地图同一套投影）；标记真的画在那里（取色）
    if (!routeId.isEmpty()) {
        mk->selectPlan(routeId, 1, false);
        const int row = mk->indexOf(routeId, 1);
        const auto s0 = mk->stops()[row];
        mapc->jumpTo(s0.base.lat, s0.base.lng, 12);
        waitMapIdle(win, mapc);
        const auto &s = mk->stops()[row];
        const QPointF want = mapc->pixelFor(s.base.lat, s.base.lng);
        const double err = std::hypot(s.x - want.x(), s.y - want.y());
        r.check(err < 1e-6 && !s.clustered, QStringLiteral("标记位置 = 地图投影"), QStringLiteral("差 %1 像素").arg(err, 0, 'g', 3));
        // 选中的计划不透明；圆形 / 方形 / 菱形标记中心左边 6 px 是底色（白边以内、数字以外）；水滴取头部左边
        const QImage shot = win->grabWindow();
        const bool pin = s.style.shape == QLatin1String("pin");
        const QColor got = colorAt(shot, win, s.x - (pin ? 8 : 6), s.y - (pin ? 23 : 0));
        const QColor want2(s.style.color);
        const int diff = std::max({ std::abs(got.red() - want2.red()), std::abs(got.green() - want2.green()), std::abs(got.blue() - want2.blue()) });
        r.check(s.op > 0.99 && got.isValid() && diff <= 40, QStringLiteral("标记画出来了（取色）"),
                QStringLiteral("想要 %1，屏幕上 %2（差 %3）").arg(want2.name(), got.name()).arg(diff));
    }

    // 3. 选中的计划不参与合并；聚焦 / ← → 切站
    if (!routeId.isEmpty()) {
        mapc->jumpTo(30, 100, 2.5);
        mk->selectPlan(routeId, 0, false);
        bool anyClustered = false;
        for (const auto &s : mk->stops()) if (s.planId == routeId && s.clustered) anyClustered = true;
        r.check(!anyClustered, QStringLiteral("选中的计划不参与合并"));
        mk->stepStop(1);
        const bool step1 = mk->focusStop() == 1;
        mk->stepStop(-1);
        mk->stepStop(-1);   // 第一站再往前：不动
        const bool stay0 = mk->focusStop() == 0;
        mk->selectPlan(routeId, routeStops - 1, false);
        mk->stepStop(1);    // 最后一站再往后：不动
        const bool stayLast = mk->focusStop() == routeStops - 1;
        const QAbstractItemModel *m = mk->stopsModel();
        const int frow = mk->indexOf(routeId, routeStops - 1);
        const bool z = m->data(m->index(frow, 0), map::MarkerModel::Z).toInt() == 3000
                       && m->data(m->index(frow, 0), map::MarkerModel::Focus).toBool();
        r.check(step1 && stay0 && stayLast && z, QStringLiteral("聚焦站点：← → 切站、放到最上层"),
                QStringLiteral("→ %1，第一站 ← %2，最后一站 → %3，最上层 %4").arg(step1).arg(stay0).arg(stayLast).arg(z));
        mk->clearSelection();
    }

    // 4. 悬停：这个计划渐亮、其余变淡；离开 1 秒后恢复
    if (!routeId.isEmpty()) {
        mapc->jumpTo(30, 100, 7);
        const int row = mk->indexOf(routeId, 0);
        int other = -1;
        for (int i = 0; i < mk->stops().size(); ++i) if (mk->stops()[i].planId != routeId && mk->stops()[i].route) { other = i; break; }
        mk->hoverStop(row);
        pause(400);
        const double hovered = mk->stops()[row].op, dimmed = other >= 0 ? mk->stops()[other].op : 0.39;
        mk->unhoverStop(row);
        pause(1700);
        const double after = mk->stops()[row].op;
        r.check(hovered > 0.99 && std::abs(dimmed - 0.39) < 0.01 && std::abs(after - 0.65) < 0.01, QStringLiteral("悬停：渐亮、其余变淡、离开后恢复"),
                QStringLiteral("悬停 %1，其他路线 %2，离开后 %3").arg(hovered, 0, 'f', 2).arg(dimmed, 0, 'f', 2).arg(after, 0, 'f', 2));
    }

    // 5. 错开：找 20 m 内重叠的站点，放大到 15 级
    {
        int a = -1, b = -1;
        const auto &st = mk->stops();
        for (int i = 0; i < st.size() && a < 0; ++i)
            for (int j = i + 1; j < st.size(); ++j)
                if (map::geo::distanceMeters(st[i].base, st[j].base) <= 20) { a = i; b = j; break; }
        if (a < 0) {
            r.check(true, QStringLiteral("错开（跳过）"), QStringLiteral("数据里没有 20 米内重叠的站点"));
        } else {
            mapc->jumpTo(st[a].base.lat, st[a].base.lng, 15);
            waitMapIdle(win, mapc);
            const auto &s = mk->stops();
            const double d = std::hypot(s[a].x - s[b].x, s[a].y - s[b].y);
            const bool dot = mk->spreadJson().contains(QStringLiteral("Point"));
            mapc->jumpTo(st[a].base.lat, st[a].base.lng, 13);
            waitMapIdle(win, mapc);
            const double d2 = std::hypot(mk->stops()[a].x - mk->stops()[b].x, mk->stops()[a].y - mk->stops()[b].y);
            r.check(d >= 20 && dot && d2 < 5, QStringLiteral("错开：≥14 级绕圈、原位留小点，13 级收回"),
                    QStringLiteral("15 级相距 %1 像素，13 级 %2 像素").arg(d, 0, 'f', 1).arg(d2, 0, 'f', 1));
        }
    }

    // 6. 点圆圈：至少放大一级
    {
        mapc->jumpTo(30, 100, 2.5);
        if (mk->clusters().isEmpty()) {
            r.check(true, QStringLiteral("点圆圈放大（跳过）"), QStringLiteral("2.5 级没有圆圈"));
        } else {
            const double z0 = mapc->zoom();
            mk->clickCluster(0);
            waitFor([mapc] { return !mapc->moving(); }, 3000);
            r.check(mapc->zoom() >= std::floor(z0) + 1 - 1e-6, QStringLiteral("点圆圈放大"), QStringLiteral("%1 → %2 级").arg(z0, 0, 'f', 2).arg(mapc->zoom(), 0, 'f', 2));
        }
    }

    // 7. 视野里没有路线、瓦片也没加载出来时，地图照样画底色（maplibre-native 那时整帧不画、是白的；0.8.2 加了同色的整块多边形）
    {
        mapc->jumpTo(30, -150, 8);   // 太平洋中间，数据里没有东西
        waitRendered(win, mapc, 3000);
        const QImage shot = win->grabWindow();
        int white = 0;
        for (const QPointF &p : { QPointF(0.2, 0.3), QPointF(0.8, 0.4), QPointF(0.5, 0.6) }) {
            const QColor c = colorAt(shot, win, win->width() * p.x(), win->height() * p.y());
            if (c.red() == 255 && c.green() == 255 && c.blue() == 255) ++white;
        }
        r.check(white == 0, QStringLiteral("没有数据的地方也画了底色"), QStringLiteral("3 个点里 %1 个是纯白").arg(white));
    }

    // 8. "已解锁"里悬停去过的省有提示框
    {
        mapc->setBase(QStringLiteral("unlock"));
        waitFor([mapc] { return mapc->unlockLoaded(); }, 5000);
        QString tip;
        for (const auto &p : trips.plans()) {
            if (p.status != QLatin1String("completed") || p.stops.isEmpty()) continue;
            mapc->jumpTo(p.stops[0].lat, p.stops[0].lng, 6.5);
            const QPointF px = mapc->pixelFor(p.stops[0].lat, p.stops[0].lng);
            tip = mapc->unlockTipAt(px.x(), px.y());
            if (!tip.isEmpty()) break;
        }
        r.check(!tip.isEmpty(), QStringLiteral("已解锁：悬停去过的省有提示框"), QString(tip).replace(QStringLiteral("<br>"), QStringLiteral(" / ")));
        mapc->setBase(QStringLiteral("street"));
    }
    mk->clearSelection();
}

} // namespace tp::app::selftest
