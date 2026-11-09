// 自检：地球（0.9.0）——去掉浅灰、按视野加载瓦片、卫星图 / 已解锁两种底图、"连续失败 8 次"按地球的底图算、
// 相机（惯性、滚轮、最远 / 最近、和平面缩放级别对得上）、离开地球释放显存和省界。
// 0.9.3：天空和光照（星空单独一层、星空动画开关、流星、城市灯光、光晕跟着太阳、空闲自转）。
// 0.9.5：地球上的内容（标记、背面渐隐 / 边缘变小、路线和飞行弧线、碰线、点标记、定位换平面、面板、交通小图标、
// 极点缩放不抽搐、城市灯光只在最远时显示）。
#include "selftest_util.h"

#include <cmath>

#include "appcontroller.h"
#include "globeitem.h"
#include "flatvehicle.h"
#include "planeditor.h"
#include "globetiles.h"
#include "skyitem.h"
#include "globegeom.h"
#include "globeroutes.h"
#include "markerlayer.h"
#include "mapcontroller.h"
#include "framestats.h"
#include "perf.h"
#include "procmem.h"
#include "tileserver.h"

#include <QImage>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalBlocker>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <qpa/qwindowsysteminterface.h>
#include <cmath>

namespace tp::app::selftest {

namespace {
QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findItem(c, name)) return f;
    return nullptr;
}
void click(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return;
    waitStill(it, 1000);   // 0.12.11：面板滑进、列表滚动时先等停稳再点，不然点在半路上
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    const QPointF g = win->mapToGlobal(p);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, devPx(win, p), devPx(win, g), Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, devPx(win, p), devPx(win, g), Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    pause(80);
}
// 一块区域的平均颜色（逻辑坐标；grabWindow 是设备像素）
QColor avgColor(const QImage &img, QRectF r, double dpr)
{
    const QRect pr = QRectF(r.x() * dpr, r.y() * dpr, r.width() * dpr, r.height() * dpr).toAlignedRect().intersected(img.rect());
    double R = 0, G = 0, B = 0;
    int n = 0;
    for (int y = pr.top(); y <= pr.bottom(); ++y)
        for (int x = pr.left(); x <= pr.right(); ++x) {
            const QRgb c = img.pixel(x, y);
            R += qRed(c); G += qGreen(c); B += qBlue(c); ++n;
        }
    return n ? QColor(int(R / n), int(G / n), int(B / n)) : QColor();
}
QString rgbText(const QColor &c) { return QStringLiteral("rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue()); }

// 0.9.3 天空和光照
void runSky(Report &r, AppController *app, QQuickWindow *win, GlobeItem *globe)
{
    r.section(QStringLiteral("天空和光照（0.9.3）"));
    auto *sky = win->findChild<SkyItem *>(QStringLiteral("sky"));
    const double dpr = win->effectiveDevicePixelRatio();
    // 0. 0.9.4：星空底图（星云、银河光雾）在后台算好、传上显卡
    {
        const bool ok = sky && waitFor([sky] { return sky->backgroundReady(); }, 5000);
        r.check(ok, QStringLiteral("星空底图（星云、银河光雾）算好、传上显卡"), ok ? QStringLiteral("好了") : QStringLiteral("5 秒还没好"));
    }
    // 1. 星空单独一层在地球后面，在画；一直开着时按 24 帧刷新
    {
        const bool behind = sky && sky->parentItem() == globe->parentItem() && sky->z() <= globe->z()
                            && sky->parentItem()->childItems().indexOf(sky) < sky->parentItem()->childItems().indexOf(globe);
        const int f0 = sky ? sky->frames() : 0;
        pause(1000);
        const int f1 = sky ? sky->frames() : 0;
        if (!r.check(sky && behind && sky->isVisible() && sky->animate() && f1 - f0 >= 3,
                     QStringLiteral("星空在地球后面单独一层，星星闪烁时一直在画"),
                     QStringLiteral("找到 %1，在后面 %2，显示 %3，动 %4，1 秒画了 %5 帧（24 帧，软件渲染会少）").arg(sky != nullptr).arg(behind)
                         .arg(sky && sky->isVisible()).arg(sky && sky->animate()).arg(f1 - f0)))
            return;
    }
    // 2. "星空动画"关掉：不再一帧帧画（星星不闪、没有流星）；打开又画；设置存下来（键名和 Electron 一样 spaceAnim）
    {
        const bool was = app->spaceAnim();
        app->setSpaceAnim(false);
        waitFor([sky] { return !sky->animate(); }, 1000);
        waitFrames(win, 2);
        const int f0 = sky->frames();
        pause(800);
        const int idle = sky->frames() - f0;
        const bool stopped = !sky->animate() && idle <= 1;
        app->setSpaceAnim(true);
        waitFor([sky] { return sky->animate(); }, 1000);
        const int f1 = sky->frames();
        pause(800);
        const int running = sky->frames() - f1;
        r.check(stopped && sky->animate() && running >= 2 && app->spaceAnim(),
                QStringLiteral("星空动画开关：关了不再一帧帧画，开了接着画"),
                QStringLiteral("关：0.8 秒画了 %1 帧；开：0.8 秒画了 %2 帧").arg(idle).arg(running));
        app->setSpaceAnim(was);
    }
    // 3. 流星：来一颗，0.9 秒划过以后没了
    {
        sky->fireMeteor();
        const bool on = sky->meteorActive();
        const bool gone = waitFor([sky] { return !sky->meteorActive(); }, 3000);
        r.check(on && gone, QStringLiteral("流星：出现、0.9 秒划过后消失"), QStringLiteral("出现 %1，消失 %2").arg(on).arg(gone));
    }
    // 4. 城市灯光：进地球后后台读好（8000×4000，一个通道，连 mipmap），传上显卡
    {
        const bool ready = waitFor([globe] { return globe->lightsReady(); }, 10000);
        const QSize sz = globe->lightsSize();
        r.check(ready && sz == QSize(8000, 4000), QStringLiteral("城市灯光贴图读好、传上显卡"),
                QStringLiteral("%1，%2×%3（%4）").arg(ready ? QStringLiteral("好了") : QStringLiteral("10 秒还没好")).arg(sz.width()).arg(sz.height())
                    .arg(GlobeItem::lightsPath()));
    }
    // 5. 夜里有灯光：对准此刻天最黑的大城市，灯光开 / 关各截一张，开着时中间一块明显更亮、更暖（黄）
    {
        const QVariantList c = globe->nightCity();
        globe->stop();
        globe->setCenterLat(c.value(0).toDouble());
        globe->setCenterLng(c.value(1).toDouble());
        globe->setDistance(globe->maxDistance());   // 0.9.5：灯光只在缩到最远时显示（PO 11:05）
        waitGlobeIdle(win, globe, 4000);
        const QPointF mid = globe->mapToScene(QPointF(globe->width() / 2, globe->height() / 2));
        const QRectF box(mid.x() - 60, mid.y() - 60, 120, 120);
        globe->setNightLights(true);
        waitFrames(win, 3);
        const QImage imgOn = win->grabWindow();
        globe->setNightLights(false);
        waitFrames(win, 3);
        const QImage imgOff = win->grabWindow();
        globe->setNightLights(true);
        // 比最亮那个像素，不比平均：平均值看这一块里城市占多大（哪个城市最黑随时间变），墨西哥城只亮 2 级，开罗、上海亮得多
        const QRect pr = QRectF(box.x() * dpr, box.y() * dpr, box.width() * dpr, box.height() * dpr).toAlignedRect().intersected(imgOn.rect());
        QColor on, off;
        int peak = -999;
        for (int y = pr.top(); y <= pr.bottom(); ++y)
            for (int x = pr.left(); x <= pr.right(); ++x) {
                const QColor a = imgOn.pixelColor(x, y), b = imgOff.pixelColor(x, y);
                if (a.red() - b.red() > peak) { peak = a.red() - b.red(); on = a; off = b; }
            }
        r.check(peak >= 10 && peak > on.blue() - off.blue(),
                QStringLiteral("夜里的城市有灯光（暖黄）"),
                QStringLiteral("%1（太阳高度 %2°），中间 120×120 里变化最大的像素：开 %3，关 %4").arg(c.value(2).toString()).arg(c.value(3).toDouble(), 0, 'f', 0)
                    .arg(rgbText(on), rgbText(off)));
    }
    // 6. 大气光晕跟着太阳（PO 选 Q1 B）：晨昏线竖在中间、白天在右边，右边球外的光晕比左边（夜里）蓝得多
    {
        globe->setCenterLat(globe->sunLat() * 0.3);
        globe->setCenterLng(globe->sunLng() - 90);
        globe->setFromZoomLevel(1.2);   // 缩远一点，球完整在窗口里
        waitGlobeIdle(win, globe, 3000);
        const double f = globe->height() / (2 * std::tan(map::globe::kFovYDeg * M_PI / 360));
        const double R = f * std::tan(std::asin(1.0 / globe->distance()));   // 球在屏幕上的半径（逻辑像素）
        const QPointF mid = globe->mapToScene(QPointF(globe->width() / 2, globe->height() / 2));
        const QImage img = win->grabWindow();
        // 0.11.4：路线的绿色弧线可能正好压在量的那一点上（看截图确认过，全量里因此没过一次）。沿球边量 7 点，取最偏蓝的（蓝减绿最大），绿线、白点都不会被选中
        const auto limb = [&](double side) {
            QColor best;
            for (int k = -3; k <= 3; ++k) {
                const double t = k * 0.1;
                const QColor c = avgColor(img, QRectF(mid.x() + side * (R * 1.02 * std::sqrt(1 - t * t) + 3) - 3, mid.y() + t * R * 1.02 - 6, 6, 12), dpr);
                if (k == -3 || c.blue() - c.green() > best.blue() - best.green()) best = c;
            }
            return best;
        };
        const QColor right = limb(1), left = limb(-1);
        r.check(right.blue() - left.blue() >= 20, QStringLiteral("大气光晕跟着太阳：白天那一侧亮，夜里那一侧淡"),
                QStringLiteral("球半径 %1 像素；右（白天）%2，左（夜里）%3").arg(R, 0, 'f', 0).arg(rgbText(right), rgbText(left)));
    }
    // 7. 空闲自转（C7）：截图 / 自检时 QML 不让转；放开以后，没有操作一段时间（这里 0.6 秒，平时 60 秒）开始向东慢慢转；
    //    窗口里一动鼠标马上停；拖动也停
    {
        const bool blockedInTest = !globe->spinAllowed();
        globe->setIdleDelay(600);
        globe->setSpinAllowed(true);
        const bool started = waitFor([globe] { return globe->spinning(); }, 3000);
        const double lng0 = globe->centerLng();
        pause(2500);
        double moved = lng0 - globe->centerLng();
        if (moved < -180) moved += 360;
        if (moved > 180) moved -= 360;
        // 鼠标在窗口里动一下
        const QPointF p(win->width() / 2.0, win->height() / 2.0);
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, devPx(win, p), devPx(win, win->mapToGlobal(p)), Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
        const bool stoppedByMouse = !globe->spinning();
        const double lng1 = globe->centerLng();
        pause(300);
        const bool still = std::abs(globe->centerLng() - lng1) < 1e-9;
        const bool restarted = waitFor([globe] { return globe->spinning(); }, 3000);
        globe->dragStart();
        const bool stoppedByDrag = !globe->spinning();
        globe->dragEnd();
        globe->setSpinAllowed(false);
        globe->setIdleDelay(60000);
        r.check(blockedInTest && started && moved > 0.4 && moved < 3.5 && stoppedByMouse && still && restarted && stoppedByDrag,
                QStringLiteral("空闲自转：没有操作时向东慢慢转，一动鼠标 / 拖动马上停"),
                QStringLiteral("自检时 QML 不让转 %1；开始 %2；2.5 秒转了 %3°（约 1°/秒，头 2 秒加速）；鼠标停 %4、停住 %5；又开始 %6；拖动停 %7")
                    .arg(blockedInTest).arg(started).arg(moved, 0, 'f', 2).arg(stoppedByMouse).arg(still).arg(restarted).arg(stoppedByDrag));
    }
}
} // namespace

// 0.9.5 地球上的内容
// 0.9.7 切换和收尾：放大越过 5.5 级摊平、平面缩到 5 级以下卷回、编辑器关掉回到地球（PO 选 B）、平面上的交通小图标
void runSwitch(Report &r, map::MapController *mapc, QQuickWindow *win, GlobeItem *globe)
{
    r.section(QStringLiteral("地球 ⇄ 平面的变形（0.9.7）"));
    const bool canMorph = QQmlProperty::read(win, QStringLiteral("map")).value<QObject *>() != nullptr;   // 有平面地图才播变形（云端没有 maplibre 时直接淡入淡出）
    win->setProperty("morphTest", true);   // 截图 / 自检平时不自动摊平 / 卷回，这一节要测
    win->setProperty("startDone", true);
    auto onGlobe = [win, globe] { return win->property("globeMode").toBool() && !globe->morphing() && globe->morph() == 0; };
    auto onFlat = [win, globe] { return !win->property("globeMode").toBool() && !globe->morphing(); };
    // 1. 地球上滚轮放大到 5.5 级以上：播摊平（星空跟着淡掉），停下来时平面在同一个中心、同一级
    {
        globe->stop();
        globe->setCenterLat(31.2); globe->setCenterLng(121.5); globe->setFromZoomLevel(5.1);
        waitGlobeIdle(win, globe, 2000);
        double maxMorph = 0, skyAtHalf = -1;
        QQuickItem *sky = findItem(win->contentItem(), QStringLiteral("sky"));
        const QMetaObject::Connection c = QObject::connect(globe, &GlobeItem::morphChanged, globe, [&] {
            maxMorph = std::max(maxMorph, globe->morph());
            if (skyAtHalf < 0 && globe->morph() > 0.45 && sky) skyAtHalf = sky->opacity();
        });
        globe->wheel(120, globe->width() / 2, globe->height() / 2);   // 一格半级 → 5.6 级
        const bool flat = waitFor(onFlat, 4000);
        QObject::disconnect(c);
        waitMapIdle(win, mapc, 2000);
        const bool aligned = std::abs(mapc->lat() - 31.2) < 0.05 && std::abs(mapc->lng() - 121.5) < 0.05 && std::abs(mapc->zoom() - 5.6) < 0.08;
        r.check(flat && aligned && (!canMorph || (maxMorph > 0.99 && skyAtHalf >= 0 && skyAtHalf < 0.7)),
                QStringLiteral("地球上放大越过 5.5 级：播摊平动画，交给同一中心、同一级的平面"),
                QStringLiteral("%1；平面 %2, %3，%4 级（应 5.6）；变形到 %5，摊到一半时星空 %6%7").arg(flat ? QStringLiteral("到了平面") : QStringLiteral("没到平面"))
                    .arg(mapc->lat(), 0, 'f', 3).arg(mapc->lng(), 0, 'f', 3).arg(mapc->zoom(), 0, 'f', 2).arg(maxMorph, 0, 'f', 2).arg(skyAtHalf, 0, 'f', 2)
                    .arg(canMorph ? QString() : QStringLiteral("（没有平面地图，直接切换）")));
    }
    // 2. 平面上缩到 5 级以下（图层是地球）：卷回地球
    {
        mapc->setGlobeLayer(true);
        waitFor([mapc] { return mapc->autoGlobe(); }, 1000);
        double minMorph = 1;
        const QMetaObject::Connection c = QObject::connect(globe, &GlobeItem::morphChanged, globe, [&] { if (globe->morphing()) minMorph = std::min(minMorph, globe->morph()); });
        mapc->wheel(-120, win->width() / 2.0, win->height() / 2.0);   // 5.6 → 5.1
        waitMapIdle(win, mapc, 1500);
        const bool stillFlat = !win->property("globeMode").toBool();
        const bool kept = globe->property("active").toBool() && globe->lightsReady() && globe->morph() == 0;   // 0.9.8：摊平后留着，不用重建
        mapc->wheel(-120, win->width() / 2.0, win->height() / 2.0);   // 5.1 → 4.6：卷回
        const bool back = waitFor(onGlobe, 5000);
        QObject::disconnect(c);
        const double gz = globe->zoomLevelEquivalent();
        r.check(mapc->autoGlobe() || back, QStringLiteral("自动摊平后平面知道\"图层是地球\"（autoGlobe）"));
        r.check(kept, QStringLiteral("摊平后地球的显存留着（城市灯光还在、回到球），卷回不用重建（0.9.8）"));
        r.check(stillFlat && back && std::abs(gz - 4.6) < 0.1 && std::abs(globe->centerLat() - 31.2) < 0.1,
                QStringLiteral("平面上缩到 5 级以下卷回地球（5.0–5.5 之间不来回切）"),
                QStringLiteral("5.1 级时%1；卷回 %2，地球 %3 级（应 4.6），中心 %4, %5%6").arg(stillFlat ? QStringLiteral("还是平面") : QStringLiteral("已经卷回"))
                    .arg(back).arg(gz, 0, 'f', 2).arg(globe->centerLat(), 0, 'f', 2).arg(globe->centerLng(), 0, 'f', 2)
                    .arg(canMorph ? QStringLiteral("，变形从 1 走到 %1").arg(minMorph, 0, 'f', 2) : QString()));
    }
    // 3. 从地球打开编辑器：摊平到平面；放弃以后卷回地球、对着这个计划（PO 选 B）
    PlanEditor *ed = editorCtl();
    QString pid;
    for (const map::TripPlan &p : mapc->trips().plans()) if (p.isRoute() && p.stops.size() >= 2) { pid = p.id; break; }
    if (ed && !pid.isEmpty()) {
        waitGlobeIdle(win, globe, 1500);
        QMetaObject::invokeMethod(win, "editPlan", Q_ARG(QVariant, pid));
        const bool openedFlat = waitFor([&] { return ed->isOpen() && onFlat(); }, 4000);
        waitMapIdle(win, mapc, 2000);
        ed->discard();
        const bool back = waitFor([&] { return !ed->isOpen() && onGlobe(); }, 6000);
        waitGlobeIdle(win, globe, 2000);
        const QVariantList cam = mapc->fitPlanCamera(pid, 5.0);
        const bool facing = cam.size() == 3 && map::globe::angleBetween(globe->centerLat(), globe->centerLng(), cam[0].toDouble(), cam[1].toDouble()) * 180 / M_PI < 1.0;
        r.check(openedFlat && back && facing, QStringLiteral("从地球打开编辑器换到平面，关掉后卷回地球、对着刚编辑的计划"),
                QStringLiteral("打开时%1；关掉后%2，地球中心 %3, %4（计划 %5, %6）").arg(openedFlat ? QStringLiteral("在平面") : QStringLiteral("不在平面"))
                    .arg(back ? QStringLiteral("回到地球") : QStringLiteral("没回地球")).arg(globe->centerLat(), 0, 'f', 2).arg(globe->centerLng(), 0, 'f', 2)
                    .arg(cam.value(0).toDouble(), 0, 'f', 2).arg(cam.value(1).toDouble(), 0, 'f', 2));
    } else {
        r.info(QStringLiteral("编辑器回到地球：找不到编辑器或路线计划，跳过"));
    }
    // 4. 平面上的交通小图标（PO 14:44 定）：选中路线时沿线走；在地球上不出现
    if (auto *fv = win->findChild<FlatVehicle *>(QStringLiteral("flatVehicle")); fv && !pid.isEmpty() && canMorph) {
        QMetaObject::invokeMethod(win, "pickBase", Q_ARG(QVariant, QStringLiteral("sat")));
        waitFor(onFlat, 4000);
        mapc->markers()->selectPlan(pid, -1, false);
        mapc->flyToPlan(pid);
        waitMapIdle(win, mapc, 3000);
        fv->setAllowed(true);   // 截图 / 自检时 QML 里动效可能关着
        const QVariantMap a = fv->atMs(300);
        const QPointF lp = mapc->linePoint(pid, a.value(QStringLiteral("leg")).toInt());
        r.check(a.value(QStringLiteral("shown")).toBool() && a.value(QStringLiteral("legs")).toInt() > 0 && a.value(QStringLiteral("img")).toString().startsWith(QStringLiteral("v/")),
                QStringLiteral("平面上选中路线：交通小图标沿线走"),
                QStringLiteral("%1 段、全程 %2 秒；0.3 秒时在 %3, %4（第 %5 段）").arg(a.value(QStringLiteral("legs")).toInt()).arg(a.value(QStringLiteral("totalMs")).toDouble() / 1000, 0, 'f', 1)
                    .arg(a.value(QStringLiteral("x")).toDouble(), 0, 'f', 0).arg(a.value(QStringLiteral("y")).toDouble(), 0, 'f', 0).arg(a.value(QStringLiteral("leg")).toInt()));
        Q_UNUSED(lp);
        mapc->markers()->clearSelection();
        QMetaObject::invokeMethod(win, "pickBase", Q_ARG(QVariant, QStringLiteral("globe")));
        waitFor(onGlobe, 5000);
    }
    win->setProperty("morphTest", false);
    waitGlobeIdle(win, globe, 1500);
}

void runContent(Report &r, map::MapController *mapc, QQuickWindow *win, GlobeItem *globe)
{
    r.section(QStringLiteral("地球上的内容（0.9.5）"));
    namespace g = map::globe;
    map::MarkerLayer *mk = mapc->markers();
    mk->clearSelection();
    globe->stop();
    const double W = globe->width(), H = globe->height();
    // 一个路线计划的第一站（不在合并圆圈里也没关系，投影一样）
    int row = -1;
    for (int i = 0; i < mk->stops().size(); ++i)
        if (mk->stops()[i].route) { row = i; break; }
    if (!r.check(row >= 0 && mapc->globeOn(), QStringLiteral("在地球上、有路线的站点"), QStringLiteral("globeOn %1").arg(mapc->globeOn()))) return;
    const map::geo::LatLng base = mk->stops()[row].base;
    // 1. 标记按地球的相机摆：转到这一站正上方，它在屏幕正中
    {
        globe->setCenterLat(base.lat);
        globe->setCenterLng(base.lng);
        globe->setFromZoomLevel(3.0);
        waitFrames(win, 1);
        const QVariantMap i = mk->stopInfo(row);
        QQuickItem *gm = findItem(win->contentItem(), QStringLiteral("globeMarkers"));
        const double dx = i.value(QStringLiteral("x")).toDouble() - W / 2, dy = i.value(QStringLiteral("y")).toDouble() - H / 2;
        r.check(std::hypot(dx, dy) < 1.0 && gm && gm->isVisible() && i.value(QStringLiteral("limb")).toDouble() > 0.999,
                QStringLiteral("地球上的标记按地球的相机摆"), QStringLiteral("站点离屏幕中心 %1 像素；地球标记层%2").arg(std::hypot(dx, dy), 0, 'f', 2)
                    .arg(gm && gm->isVisible() ? QStringLiteral("显示") : QStringLiteral("没显示")));
    }
    // 2. 背面看不见；离地平线 4° 时半透明、变小（C4 / G3）
    {
        globe->setCenterLat(-base.lat);
        globe->setCenterLng(base.lng + 180);
        waitFrames(win, 1);
        const QVariantMap back = mk->stopInfo(row);
        const double hz = g::horizonAngle(globe->distance()) * 180 / M_PI;
        const double off = hz - 4.0;
        globe->setCenterLat(std::abs(base.lat - off) <= 85 ? base.lat - off : base.lat + off);
        globe->setCenterLng(base.lng);
        waitFrames(win, 1);
        const QVariantMap edge = mk->stopInfo(row);
        const double fade = edge.value(QStringLiteral("limb")).toDouble(), sc = edge.value(QStringLiteral("sc")).toDouble();
        const QModelIndex mi = mk->stopsModel()->index(row, 0);
        const bool shownBack = back.value(QStringLiteral("limb")).toDouble() > 0.001;
        r.check(!shownBack && fade > 0.25 && fade < 0.75 && sc < 0.8 && sc > 0.69 && mk->stopsModel()->data(mi, map::MarkerModel::Hit).toBool() == (fade >= 0.5),
                QStringLiteral("背面的标记看不见；快到边上时渐隐、变小"),
                QStringLiteral("背面 %1；离地平线 4°：不透明度 ×%2、大小 ×%3").arg(shownBack ? QStringLiteral("还看得见") : QStringLiteral("看不见"))
                    .arg(fade, 0, 'f', 2).arg(sc, 0, 'f', 2));
    }
    // 3. 路线：飞行段抬起、画出来、碰得到；定位一个跨国的行程留在地球上
    const QString demo = mapc->globeDemoPlan();
    {
        int leg = -1;
        double bestAng = 0;
        for (int i = 0; i < 200; ++i) {
            const QVariantMap li = mapc->globeLegInfo(demo, i);
            if (li.isEmpty()) break;
            if (li.value(QStringLiteral("lifted")).toBool() && li.value(QStringLiteral("angle")).toDouble() > bestAng) { bestAng = li.value(QStringLiteral("angle")).toDouble(); leg = i; }
        }
        const QVariantMap li = mapc->globeLegInfo(demo, leg);
        const double h = li.value(QStringLiteral("maxR")).toDouble() - 1;
        const double want = g::liftHeight(li.value(QStringLiteral("angle")).toDouble());
        mk->selectPlan(demo, -1, false);
        mapc->flyToPlan(demo);
        waitGlobeIdle(win, globe, 3000);
        const bool stayed = win->property("globeMode").toBool();
        const QPointF lp = mapc->linePoint(demo, li.value(QStringLiteral("stop")).toInt());
        const QString hit = lp.x() >= 0 ? mapc->lineAt(lp.x(), lp.y()) : QString();
        // 0.9.6：拖着转 40 帧量画线的耗时（平均 / 最慢；一帧的数不稳，0.9.5 PO 电脑上那一帧是 21 ms）
        globe->resetLineStats();
        for (int i = 0; i < 40; ++i) { globe->rotateBy(4, 0); pause(20); }
        globe->rotateBy(-160, 0);
        waitFrames(win, 2);
        const QVariantMap ls = globe->lineStats();
        r.check(!demo.isEmpty() && std::abs(h - want) < 1e-3 && h > 0, QStringLiteral("飞行段抬起成弧线（弧高 = 段长 × 12%，最高 1500 km）"),
                QStringLiteral("最长一段飞行 %1 km，弧高 %2 km（应 %3）").arg(li.value(QStringLiteral("angle")).toDouble() * g::kEarthRadiusM / 1000, 0, 'f', 0)
                    .arg(h * g::kEarthRadiusM / 1000, 0, 'f', 0).arg(want * g::kEarthRadiusM / 1000, 0, 'f', 0));
        r.check(ls.value(QStringLiteral("verts")).toInt() > 0 && hit == demo, QStringLiteral("地球上画出路线，碰得到"),
                QStringLiteral("%1 段 %2 个顶点；拖着转 %3 帧画线平均 %4 ms、最慢 %5 ms；线上一点 (%6, %7) 碰到 %8").arg(ls.value(QStringLiteral("runs")).toInt())
                    .arg(ls.value(QStringLiteral("verts")).toInt()).arg(ls.value(QStringLiteral("frames")).toInt()).arg(ls.value(QStringLiteral("avgMs")).toDouble(), 0, 'f', 2)
                    .arg(ls.value(QStringLiteral("maxMs")).toDouble(), 0, 'f', 2).arg(lp.x(), 0, 'f', 0).arg(lp.y(), 0, 'f', 0).arg(hit == demo ? QStringLiteral("这条") : hit));
        r.check(stayed && globe->zoomLevelEquivalent() <= map::MapController::kGlobeMaxZoom + 0.01, QStringLiteral("定位跨国的行程：留在地球上转过去"),
                QStringLiteral("%1，%2 级").arg(stayed ? QStringLiteral("在地球上") : QStringLiteral("换成了平面")).arg(globe->zoomLevelEquivalent(), 0, 'f', 2));
        // 交通小图标：第一段要走的这时在路上，在屏幕上、用这段交通方式的图片
        const QVariantMap v = globe->vehicleAtMs(700);
        r.check(v.value(QStringLiteral("shown")).toBool() && v.value(QStringLiteral("img")).toString().startsWith(QStringLiteral("v/"))
                    && v.value(QStringLiteral("x")).toDouble() >= 0 && v.value(QStringLiteral("x")).toDouble() <= W,
                QStringLiteral("选中计划时交通小图标沿路线走"),
                QStringLiteral("%1 段要走、一圈 %2 秒；0.7 秒时在第 %3 段（%4），(%5, %6)").arg(v.value(QStringLiteral("legs")).toInt())
                    .arg(v.value(QStringLiteral("totalMs")).toDouble() / 1000, 0, 'f', 1).arg(v.value(QStringLiteral("leg")).toInt() + 1)
                    .arg(v.value(QStringLiteral("img")).toString().section(QLatin1Char('/'), 1, 1)).arg(v.value(QStringLiteral("x")).toDouble(), 0, 'f', 0)
                    .arg(v.value(QStringLiteral("y")).toDouble(), 0, 'f', 0));
    }
    // 4. 点标记：选中那个计划，地球转过去（缩放不变）
    {
        mk->clearSelection();
        int other = -1;
        for (int i = 0; i < mk->stops().size() && other < 0; ++i) {
            const map::MarkerLayer::Stop &s = mk->stops()[i];
            if (s.planId != demo && !s.clustered && s.limb > 0.9 && s.x > 0 && s.x < W && s.y > 0 && s.y < H) other = i;
        }
        if (other >= 0) {
            const map::MarkerLayer::Stop s = mk->stops()[other];
            const double z0 = globe->zoomLevelEquivalent(), d0 = globe->distance();
            const double a0 = g::angleBetween(globe->centerLat(), globe->centerLng(), s.base.lat, s.base.lng);
            mk->clickStop(other);
            waitFor([globe] { return !globe->animating(); }, 2000);
            const double a1 = g::angleBetween(globe->centerLat(), globe->centerLng(), s.base.lat, s.base.lng);
            r.check(mk->selectedPlan() == s.planId && mk->focusStop() == s.stop && a1 < a0 + 1e-6 && std::abs(globe->distance() - d0) < 1e-6,
                    QStringLiteral("点地球上的标记：选中计划、聚焦这一站、地球转过去"),
                    QStringLiteral("离中心 %1° → %2°；缩放 %3 → %4").arg(a0 * 180 / M_PI, 0, 'f', 1).arg(a1 * 180 / M_PI, 0, 'f', 1).arg(z0, 0, 'f', 2)
                        .arg(globe->zoomLevelEquivalent(), 0, 'f', 2));
        } else {
            r.info(QStringLiteral("点标记：这个视角里没有能点的标记，跳过"));
        }
    }
    // 5. 面板在地球上也显示（状态胶囊、比例尺、我的位置）：先取消选中（简易面板开着时胶囊藏起来）
    {
        mk->clearSelection();
        waitFor([win] { QQuickItem *p = findItem(win->contentItem(), QStringLiteral("statusPill")); return p && p->isVisible(); }, 2000);
        QQuickItem *pill = findItem(win->contentItem(), QStringLiteral("statusPill"));
        QQuickItem *scale = findItem(win->contentItem(), QStringLiteral("scaleBar"));
        QQuickItem *loc = findItem(win->contentItem(), QStringLiteral("locBtn"));
        r.check(pill && pill->isVisible() && scale && scale->isVisible() && loc && loc->isVisible() && mapc->scaleWidth() > 0,
                QStringLiteral("地球上也显示状态胶囊、比例尺、我的位置"),
                QStringLiteral("胶囊 %1、比例尺 %2（%3）、我的位置 %4").arg(pill && pill->isVisible()).arg(scale && scale->isVisible()).arg(mapc->scaleLabel())
                    .arg(loc && loc->isVisible()));
    }
    // 6. 框住所有站点（Electron fitAll 的球形）：分布广时整个地球放进可见区域（直径 = 可见区域的 90%）
    {
        mapc->fitAll(false);
        waitGlobeIdle(win, globe, 1500);
        const double R = globe->logicalView().focal() / std::sqrt(globe->distance() * globe->distance() - 1);
        const double avail = std::min(W - mapc->leftInset() - 40, H - 60);
        r.info(QStringLiteral("框住所有站点：地球在屏幕上的半径 %1 像素（可见区域 %2 的 %3%），%4 级").arg(R, 0, 'f', 0).arg(avail, 0, 'f', 0)
                   .arg(R / avail * 100, 0, 'f', 0).arg(globe->zoomLevelEquivalent(), 0, 'f', 2));
    }
    // 7. 定位城市里的行程（一个点的单一目的地，要飞到 8 级）：换成平面，再回地球
    {
        QString single;
        for (const map::TripPlan &p : mapc->trips().plans())
            if (!p.isRoute() && p.stops.size() == 1) { single = p.id; break; }
        if (!single.isEmpty()) {
            // 0.9.7：在地球上转过去拉近到 5.5 级 → 摊平 → 在平面上继续拉近
            bool morphed = false;
            const QMetaObject::Connection mc = QObject::connect(globe, &GlobeItem::morphChanged, globe, [globe, &morphed] { if (globe->morph() > 0.3) morphed = true; });
            mapc->flyToPlan(single);
            const bool flat = waitFor([win, globe] { return !win->property("globeMode").toBool() && !globe->morphing(); }, 5000);
            waitMapIdle(win, mapc, 4000);
            QObject::disconnect(mc);
            const double fz = mapc->zoom();
            r.check(flat && fz > map::MapController::kGlobeMaxZoom, QStringLiteral("定位城市里的行程：换成平面再飞过去（超过 5.5 级；0.9.7 起中间播摊平）"),
                    QStringLiteral("%1，平面 %2 级，%3").arg(flat ? QStringLiteral("换成了平面") : QStringLiteral("还在地球上")).arg(fz, 0, 'f', 2)
                        .arg(morphed ? QStringLiteral("播了摊平") : QStringLiteral("没播摊平（没有平面地图 / 动效关着时直接淡入淡出）")));
            QMetaObject::invokeMethod(win, "pickBase", Q_ARG(QVariant, QStringLiteral("globe")));
            waitFor([win, globe] { return win->property("globeMode").toBool() && !globe->morphing() && globe->morph() == 0; }, 5000);
            waitGlobeIdle(win, globe, 2000);
        } else {
            r.info(QStringLiteral("定位城市里的行程：数据里没有一个点的单一目的地，跳过"));
        }
    }
    // 8. 极点附近缩放不抽搐（PO 11:02）：中心 85°、鼠标在中心上方滚两格，距离一路变小、中心不动
    {
        mk->clearSelection();
        globe->stop();
        globe->setCenterLat(85);
        globe->setCenterLng(10);
        globe->setDistance(2.2);
        const QVariantList steps = globe->zoomSteps(W * 0.6, H * 0.15, 2, 120);
        bool mono = true;
        double prev = 2.2, maxMove = 0;
        for (const QVariant &v : steps) {
            const QVariantList e = v.toList();
            const double d = e.value(0).toDouble();
            if (d > prev + 1e-9) mono = false;
            prev = d;
            maxMove = std::max(maxMove, g::angleBetween(85, 10, e.value(1).toDouble(), e.value(2).toDouble()) * 180 / M_PI);
        }
        r.check(mono && steps.size() == 24 && maxMove < 0.5, QStringLiteral("北极附近滚轮缩放：距离一路变小、地球不打转"),
                QStringLiteral("2 格 24 帧，距离 2.20 → %1；中心最多挪 %2°").arg(prev, 0, 'f', 3).arg(maxMove, 0, 'f', 2));
    }
    // 9. 城市灯光只在缩到最远时显示：最远 1，放大半级（一格）一半左右，放大一级没有（PO 11:05）
    {
        globe->setCenterLat(30);
        globe->setDistance(globe->maxDistance());
        const double f0 = globe->lightsFade();
        globe->zoomSteps(W / 2, H / 2, 1, 120);
        const double f1 = globe->lightsFade();
        globe->zoomSteps(W / 2, H / 2, 1, 120);
        const double f2 = globe->lightsFade();
        r.check(std::abs(f0 - 1) < 1e-6 && f1 > 0.3 && f1 < 0.7 && f2 < 1e-6, QStringLiteral("城市灯光只在缩到最远时显示，放大一级淡掉"),
                QStringLiteral("最远 %1，放大一格 %2，两格 %3").arg(f0, 0, 'f', 2).arg(f1, 0, 'f', 2).arg(f2, 0, 'f', 2));
    }
    mk->clearSelection();
}

void runGlobe(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, services::TileServer *ts)
{
    r.section(QStringLiteral("地球（0.9.0；0.9.2 图层合并、地球只有卫星图）"));
    // 1. 浅灰去掉了（PO 2026-11-06）；0.9.2 图层泡泡四颗：地球、卫星图、街道图、已解锁；旧设置里的 gray 读成街道图
    {
        QStringList keys;
        for (const QVariant &v : win->property("bases").toList()) keys << v.toMap().value(QStringLiteral("key")).toString();
        mapc->setBase(QStringLiteral("gray"));
        const bool migrated = mapc->base() == QLatin1String("street") && map::MapController::migrateBase(QStringLiteral("gray")) == QLatin1String("street");
        r.check(keys == QStringList{ QStringLiteral("globe"), QStringLiteral("sat"), QStringLiteral("street"), QStringLiteral("unlock") } && migrated,
                QStringLiteral("图层四颗（地球 / 卫星图 / 街道图 / 已解锁，没有浅灰），旧的 gray 读成街道图"),
                QStringLiteral("泡泡 %1；setBase(gray) 之后是 %2").arg(keys.join(u' ')).arg(mapc->base()));
    }
    auto *globe = win->findChild<GlobeItem *>(QStringLiteral("globe"));
    if (!r.check(globe != nullptr, QStringLiteral("找到地球"))) return;
    mapc->setBase(QStringLiteral("street"));
    mapc->jumpTo(30, 110, 3.2);
    waitMapIdle(win, mapc);
    const double flatZoom = mapc->zoom();
    const services::TileServer::Stats st0 = ts ? ts->stats() : services::TileServer::Stats{};
    const ProcMem mem0 = readProcMem();
    const RhiMem rhi0 = readRhiMem(win);   // 0.9.1：显卡内存分配器的统计（地球的显存比估计多 256 MB，拆开看）

    // 2. 图层里点"地球"：进地球（卫星图），平面的底图也换成卫星图；缩放按屏幕中心比例尺换算
    QMetaObject::invokeMethod(win, "pickBase", Q_ARG(QVariant, QStringLiteral("globe")));
    // 0.9.7：平面上选地球是卷回地球（有平面地图、动效开着时播变形，先等瓦片）
    waitFor([win, globe] { return win->property("globeMode").toBool() && !globe->morphing() && globe->morph() == 0; }, 4000);
    waitFrames(win, 2);
    {
        const double gz = globe->zoomLevelEquivalent();
        r.check(globe->base() == QLatin1String("sat") && mapc->globeBase() == QLatin1String("sat") && mapc->base() == QLatin1String("sat")
                    && win->property("layerKey").toString() == QLatin1String("globe") && mapc->globeLayer() && std::abs(gz - flatZoom) < 0.05
                    && std::abs(globe->centerLat() - 30) < 0.01 && std::abs(globe->centerLng() - 110) < 0.01,
                QStringLiteral("图层里点地球：进地球（卫星图），平面底图也换成卫星图，中心和缩放对齐"),
                QStringLiteral("地球底图 %1（mapc.globeBase %2），平面底图 %3，图层 %4，缩放 %5，中心 %6, %7").arg(globe->base(), mapc->globeBase(), mapc->base(),
                    win->property("layerKey").toString()).arg(gz, 0, 'f', 2).arg(globe->centerLat(), 0, 'f', 2).arg(globe->centerLng(), 0, 'f', 2));
    }
    // 3. "连续失败 8 次"在地球上按地球的底图（卫星图）算，平面底图（街道图）的结果不算
    {
        const QSignalBlocker block(ts);
        // 0.9.5：先清零（没联网时真的失败可能已经让横幅出来了，0.9.5 云端没接假上游时碰到）
        mapc->tileResultForTest(QStringLiteral("sat"), true);
        mapc->dismissTileBanner();
        for (int i = 0; i < 8; ++i) mapc->tileResultForTest(QStringLiteral("street"), false);
        const bool streetIgnored = !mapc->tileBanner();
        for (int i = 0; i < 8; ++i) mapc->tileResultForTest(QStringLiteral("sat"), false);
        const bool satCounted = mapc->tileBanner();
        mapc->dismissTileBanner();
        mapc->tileResultForTest(QStringLiteral("sat"), true);
        r.check(streetIgnored && satCounted, QStringLiteral("地球上按地球的底图数失败次数"),
                QStringLiteral("街道图 8 次：%1；卫星图 8 次：%2").arg(streetIgnored ? QStringLiteral("不算") : QStringLiteral("算了"), satCounted ? QStringLiteral("出横幅") : QStringLiteral("没出")));
    }
    // 4. 按视野选瓦片：远看块少、级别低；拉近到约 6 级，选到的级别跟着升，块数不超过预算
    {
        waitFor([globe] { return globe->tileStats().value(QStringLiteral("selected")).toInt() > 0; }, 3000);
        const QVariantMap farS = globe->tileStats();
        globe->setFromZoomLevel(6);
        waitGlobeIdle(win, globe, 3000);
        const QVariantMap nearS = globe->tileStats();
        const int maxZ = nearS.value(QStringLiteral("maxZ")).toInt(), sel = nearS.value(QStringLiteral("selected")).toInt();
        r.check(farS.value(QStringLiteral("maxZ")).toInt() <= 5 && maxZ >= 6 && sel > 0 && sel <= map::GlobeTileSet::kLayers * 2 / 3,
                QStringLiteral("按视野选瓦片：拉近升级、块数有上限"),
                QStringLiteral("远：%1 块、最细 z%2；拉近（%3 级）：%4 块、最细 z%5（上限 %6 块）")
                    .arg(farS.value(QStringLiteral("selected")).toInt()).arg(farS.value(QStringLiteral("maxZ")).toInt())
                    .arg(globe->zoomLevelEquivalent(), 0, 'f', 1).arg(sel).arg(maxZ).arg(map::GlobeTileSet::kLayers * 2 / 3));
    }
    // 5. 卫星瓦片走本机瓦片服务（同一份硬盘缓存 / 离线开关）；没到的先用本机画的简图顶着，画面上不留空
    {
        waitFor([globe] { const QVariantMap s = globe->tileStats(); return s.value(QStringLiteral("resident")).toInt() >= 4 && s.value(QStringLiteral("net")).toInt() == 0; }, 12000);
        const QVariantMap s = globe->tileStats();
        const services::TileServer::Stats st1 = ts ? ts->stats() : services::TileServer::Stats{};
        const int req = st1.requests - st0.requests;
        r.check(req > 0 && s.value(QStringLiteral("resident")).toInt() >= 4 && s.value(QStringLiteral("drawn")).toInt() > 0,
                QStringLiteral("卫星瓦片经本机瓦片服务取，没到的用简图顶着"),
                QStringLiteral("瓦片服务收到 %1 次（缓存里有 %2、联网取到 %3、失败 %4）；显卡上 %5 层，画 %6 块（其中顶着的 %7 块）")
                    .arg(req).arg(st1.hits - st0.hits).arg(st1.fetched - st0.fetched).arg(st1.failed - st0.failed)
                    .arg(s.value(QStringLiteral("resident")).toInt()).arg(s.value(QStringLiteral("drawn")).toInt()).arg(s.value(QStringLiteral("fallback")).toInt()));
        const ProcMem m = readProcMem();
        r.info(QStringLiteral("内存：进地球前私有 %1 MB，拉近后 %2 MB%3").arg(mem0.privateMB, 0, 'f', 0).arg(m.privateMB, 0, 'f', 0)
                   .arg(m.gpuLocalMB < 0 ? QString() : QStringLiteral("；显存 %1 → %2 MB").arg(mem0.gpuLocalMB, 0, 'f', 0).arg(m.gpuLocalMB, 0, 'f', 0)));
        // 0.9.1：多出来的显存是什么。自己算得出的几样：纹理数组（256² × 4 字节 × 4/3 mipmap × 层数）、
        // 地球项自己的渲染目标（颜色 + 深度各 4 字节 / 像素）；其余看分配器：块数涨了多少、块里空着多少
        const RhiMem rhi1 = readRhiMem(win);
        const double dpr = win->effectiveDevicePixelRatio();
        const double texMB = 256.0 * 256 * 4 * 4 / 3 * globe->gpuLayers() / 1048576.0;
        const double rtMB = globe->width() * dpr * globe->height() * dpr * 8 / 1048576.0;
        r.info(QStringLiteral("显存拆开（0.9.1）：纹理数组 %1 层 ≈ %2 MB，地球项的渲染目标 ≈ %3 MB；分配器 进地球前 %4 → 拉近后 %5")
                   .arg(globe->gpuLayers()).arg(texMB, 0, 'f', 0).arg(rtMB, 0, 'f', 0).arg(rhi0.text(), rhi1.text()));
        r.info(QStringLiteral("层数够不够（0.9.1）：这时选中 %1 块、显卡上 %2 层、换出过 %3 层、上传过 %4 块")
                   .arg(s.value(QStringLiteral("selected")).toInt()).arg(s.value(QStringLiteral("resident")).toInt())
                   .arg(s.value(QStringLiteral("evicted")).toInt()).arg(s.value(QStringLiteral("uploaded")).toInt()));
        // 0.9.1：在地球上拖 2.5 秒（拉近到 6 级的样子），每一帧多久。
        // 0.9.4：再拖一次、把星空那一层藏起来，看星空占多少（0.9.3 在 PO 电脑上拖地球显卡 25 ms 一帧）
        auto *skyItem = win->findChild<SkyItem *>(QStringLiteral("sky"));
        if (skyItem) waitFor([skyItem] { return skyItem->backgroundReady(); }, 5000);
        for (int pass = 0; pass < 2; ++pass)
        if (FrameStats *fs = frameStats()) {
            if (pass == 1 && !skyItem) break;
            if (pass == 1) { skyItem->setVisible(false); waitFrames(win, 2); }
            const double t0 = perf::nowMs();
            globe->dragStart();
            QElapsedTimer t;
            t.start();
            QTimer timer;
            timer.setTimerType(Qt::PreciseTimer);
            timer.setInterval(16);
            QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
                const double ph = t.elapsed() / 2500.0 * 2 * M_PI;
                globe->dragBy(7 * std::cos(ph), 2 * std::sin(2 * ph));
            });
            timer.start();
            waitFor([&] { return t.elapsed() >= 2500; }, 5000);
            timer.stop();
            const double t1 = perf::nowMs();
            globe->dragEnd();
            globe->stop();
            const QVariantMap after = globe->tileStats();
            r.info(QStringLiteral("拖地球（0.9.1，6 级附近%4）：%1；拖完显卡上 %2 层、换出过 %3 层")
                       .arg(fs->summary(t0 + 100, t1).text()).arg(after.value(QStringLiteral("resident")).toInt())
                       .arg(after.value(QStringLiteral("evicted")).toInt())
                       .arg(pass == 0 ? QStringLiteral("，星空开着") : QStringLiteral("，星空藏起来（0.9.4 对比）")));
            if (pass == 1) skyItem->setVisible(true);   // C++ 改 visible 不会断开 QML 的绑定
            waitFrames(win, 2);
        }
    }
    // 6. 0.9.2：地球只有卫星图（地球上的已解锁不再用）：平面底图换成已解锁，地球照样是卫星图
    {
        mapc->setBase(QStringLiteral("unlock"));
        waitFrames(win, 1);
        r.check(globe->base() == QLatin1String("sat"), QStringLiteral("地球只有卫星图"), QStringLiteral("平面底图 %1 时地球是 %2").arg(mapc->base(), globe->base()));
        mapc->setBase(QStringLiteral("sat"));
    }
    // 7. 相机：拖动松手有惯性；滚轮缓动；最远 / 最近；缩放级别换算来回一致
    {
        globe->stop();
        globe->dragStart();
        // 每 16 毫秒拖一下（干等，不处理事件：软件渲染时处理一次事件就要上百毫秒，间隔会不准）
        for (int i = 0; i < 6; ++i) { globe->dragBy(-25, 0); QThread::msleep(16); }
        globe->dragEnd();
        const double lngAtRelease = globe->centerLng();
        const bool sliding = globe->animating();
        pause(400);
        double moved = globe->centerLng() - lngAtRelease;
        if (moved < -180) moved += 360;
        if (moved > 180) moved -= 360;
        r.check(sliding && moved > 0.2, QStringLiteral("拖动松手后惯性滑一段"), QStringLiteral("松手后又往东转了 %1°").arg(moved, 0, 'f', 2));
        globe->stop();
        for (int i = 0; i < 40; ++i) globe->wheel(120, globe->width() / 2, globe->height() / 2);
        waitFor([globe] { return !globe->animating(); }, 3000);
        const double nearD = globe->distance();
        for (int i = 0; i < 60; ++i) globe->wheel(-120, globe->width() / 2, globe->height() / 2);
        waitFor([globe] { return !globe->animating(); }, 3000);
        const double farD = globe->distance();
        globe->setFromZoomLevel(3.5);
        const double back = globe->zoomLevelEquivalent();
        // 右上的 ± 在地球上也能用（Electron 球形时也有）：放大一级
        QQuickItem *zin = findItem(win->contentItem(), QStringLiteral("zoomIn"));
        click(win, zin);
        waitFor([globe] { return !globe->animating(); }, 2000);
        const double afterPlus = globe->zoomLevelEquivalent();
        r.check(zin && zin->isVisible() && std::abs(afterPlus - 4.5) < 0.05, QStringLiteral("地球上的 ± 放大一级"),
                QStringLiteral("3.5 → %1").arg(afterPlus, 0, 'f', 2));
        r.check(std::abs(nearD - map::globe::kMinDist) < 1e-3 && std::abs(farD - globe->maxDistance()) < 1e-3 && std::abs(back - 3.5) < 0.01,
                QStringLiteral("滚轮缩放到最近 / 最远为止；缩放级别换算来回一致"),
                QStringLiteral("最近 %1（应 %2），最远 %3（应 %4，随窗口高度）；3.5 级 → %5").arg(nearD, 0, 'f', 3).arg(map::globe::kMinDist, 0, 'f', 3)
                    .arg(farD, 0, 'f', 3).arg(globe->maxDistance(), 0, 'f', 3).arg(back, 0, 'f', 3));
    }
    runContent(r, mapc, win, globe);
    runSky(r, app, win, globe);
    runSwitch(r, mapc, win, globe);
    // 8. 地球上在图层里点"已解锁"：回到平面的已解锁；缩放换算回去；显卡上的纹理数组换回 1 层、瓦片和省界都丢掉
    //    （0.9.3：城市灯光的贴图也还回去）。0.9.7：先放大到 5.5 级、摊平，再换底图
    {
        globe->setCenterLat(20); globe->setCenterLng(100); globe->setFromZoomLevel(4.2);
        const double gz = std::max(5.5, globe->zoomLevelEquivalent());
        QMetaObject::invokeMethod(win, "pickBase", Q_ARG(QVariant, QStringLiteral("unlock")));
        waitFor([win, globe] { return !win->property("globeMode").toBool() && !globe->morphing(); }, 4000);
        waitFor([mapc] { return mapc->base() == QLatin1String("unlock"); }, 3000);
        const bool released = waitFor([globe] { return globe->gpuLayers() == 1 && !globe->releasePending(); }, 5000);
        const QVariantMap s = globe->tileStats();
        waitFor([globe] { return !globe->lightsReady(); }, 2000);
        r.check(released && s.value(QStringLiteral("resident")).toInt() == 0 && !s.value(QStringLiteral("geometry")).toBool() && !globe->isVisible() && !globe->lightsReady()
                    && std::abs(mapc->zoom() - gz) < 0.05 && mapc->globeBase().isEmpty() && mapc->base() == QLatin1String("unlock") && !mapc->globeLayer(),
                QStringLiteral("地球上点已解锁：放大到 5.5 级摊平、回到平面的已解锁，缩放对齐，显存、瓦片、省界都放掉"),
                QStringLiteral("平面 %1 级（地球 %2）；纹理数组 %3 层，瓦片 %4 块，省界%5，城市灯光%6").arg(mapc->zoom(), 0, 'f', 2).arg(gz, 0, 'f', 2)
                    .arg(globe->gpuLayers()).arg(s.value(QStringLiteral("resident")).toInt())
                    .arg(s.value(QStringLiteral("geometry")).toBool() ? QStringLiteral("还在") : QStringLiteral("已丢掉"))
                    .arg(globe->lightsReady() ? QStringLiteral("还在") : QStringLiteral("已还")));
        pause(300);
        const ProcMem m = readProcMem();
        r.info(QStringLiteral("回平面以后（0.9.1）：分配器 %1%2").arg(readRhiMem(win).text())
                   .arg(m.gpuLocalMB < 0 ? QString() : QStringLiteral("；显存 %1 MB、私有 %2 MB").arg(m.gpuLocalMB, 0, 'f', 0).arg(m.privateMB, 0, 'f', 0)));
        mapc->setBase(QStringLiteral("street"));
        waitFrames(win, 2);
    }
}

} // namespace tp::app::selftest
