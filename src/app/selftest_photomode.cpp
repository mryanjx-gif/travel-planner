// 自检（0.11.1）：照片模式的地图标记、相册、看大图的 ★ / F / 旋转、HEIC、P1、合成 10,000 张的帧时间。
#include "selftest_util.h"

#include "albummodel.h"
#include "appcontroller.h"
#include "datastore.h"
#include "framestats.h"
#include "jsonvalue.h"
#include "mapcontroller.h"
#include "perf.h"
#include "photocontroller.h"
#include "photolayer.h"
#include "photorotate.h"
#include "plancontroller.h"
#include "plans.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTimer>
#include <qpa/qwindowsysteminterface.h>
#include <cmath>

namespace tp::app::selftest {

using json::Value;
namespace plans = map::plans;

namespace {
QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findItem(c, name)) return f;
    return nullptr;
}
bool shown(QQuickItem *it)
{
    if (!it || !it->isVisible()) return false;
    for (QQuickItem *p = it; p; p = p->parentItem()) if (p->opacity() < 0.5) return false;
    return true;
}
void click(QQuickWindow *win, const QPointF &p)
{
    const QPointF g = win->mapToGlobal(p);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
    pause(40);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    pause(120);
}
void click(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return;
    waitStill(it, 1000);   // 0.12.11：面板滑进、列表滚动时先等停稳再点，不然点在半路上
    click(win, it->mapToScene(QPointF(it->width() / 2, it->height() / 2)));
}
void key(QQuickWindow *win, int k)
{
    QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier), release(QEvent::KeyRelease, k, Qt::NoModifier);
    QCoreApplication::sendEvent(win, &press);
    QCoreApplication::sendEvent(win, &release);
    pause(30);
}
} // namespace

void runPhotoMode(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, PhotoController *photos)
{
    r.section(QStringLiteral("照片模式、相册、看大图（0.11.1）"));
    PlanController *pc = plansCtl();
    QQuickItem *mapItem = win->findChild<QQuickItem *>(QStringLiteral("map"));
    if (!pc || !mapItem) { r.info(QStringLiteral("没有地图，跳过")); return; }
    data::DataStore *store = app->store();
    const auto item = [win](const QString &n) { return findItem(win->contentItem(), n); };
    const auto goPage = [win](const QString &p) { QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, p)); };
    map::PhotoLayer *layer = mapc->photoLayer();
    AlbumModel *album = photos->album();
    QQuickItem *lb = win->findChild<QQuickItem *>(QStringLiteral("lightbox"));
    const auto lbOpen = [lb] { return lb->property("open").toBool(); };
    const auto closeLb = [&] { QMetaObject::invokeMethod(lb, "close"); waitFor([&] { return !lb->isVisible(); }, 1000); };
    const auto setList = [&](bool on) { win->setProperty("listOpen", on); QQuickItem *l = item(QStringLiteral("planList")); waitStill(l, 1500); };

    const QString routesBefore = mapc->routesJson();
    goPage(QStringLiteral("photo"));
    waitFor([pc] { return pc->mode() == QLatin1String("photo"); }, 2000);
    r.check(waitFor([&] { return layer->active() && album->active(); }, 1000), QStringLiteral("进照片模式：标记层和相册打开"));
    r.check(waitFor([&] { return !mapc->routesJson().contains(QLatin1String("coordinates")) || mapc->routesJson().contains(QLatin1String("\"coordinates\":[]")); }, 2000), QStringLiteral("照片模式不画路线"), mapc->routesJson().left(80));
    const int total = int(store->data()[QStringLiteral("photos")].asArray().size());
    r.check(layer->pinned() + layer->noPos() == total && total > 0, QStringLiteral("每张不是在地图上就是没有位置"), QStringLiteral("%1 + %2 = %3").arg(layer->pinned()).arg(layer->noPos()).arg(total));
    r.info(QStringLiteral("开发数据：%1 张，地图上 %2 张，没有位置 %3 张").arg(total).arg(layer->pinned()).arg(layer->noPos()));
    // 没有 GPS 的照片用站点的位置
    bool fromStopOk = false;
    QString fromStopPlan;
    for (const Value &ph : store->data()[QStringLiteral("photos")].asArray()) {
        if (ph[QStringLiteral("lat")].isNumber() || ph[QStringLiteral("stop")].toString().isEmpty()) continue;
        const QVariantList items = album->itemsFor({ ph[QStringLiteral("id")].toString() });
        fromStopOk = !items.isEmpty() && items[0].toMap().value(QStringLiteral("fromStop")).toBool();
        fromStopPlan = ph[QStringLiteral("plan")].toString();
        break;
    }
    r.check(fromStopOk, QStringLiteral("没有 GPS、有站点的照片：位置来自站点"));

    // ---- 标记 ----
    mapc->fitPoints(layer->pointsOf(QString()), 10, 0);
    waitMapIdle(win, mapc, 3000);
    r.check(waitFor([&] { return layer->shownCount() > 0; }, 2000), QStringLiteral("世界视图有照片标记"), QStringLiteral("画着 %1 个").arg(layer->shownCount()));
    r.check(layer->shownCount() < layer->pinned(), QStringLiteral("相近的合并了"), QStringLiteral("%1 张 → %2 个").arg(layer->pinned()).arg(layer->shownCount()));
    r.check(layer->slotCount() <= 60, QStringLiteral("QML 元素只有看得见的那么多"), QStringLiteral("%1 个元素").arg(layer->slotCount()));
    QQuickItem *mk0 = nullptr;
    int row0 = -1;
    for (int i = 0; i < layer->slotCount() && !mk0; ++i)
        if (layer->rowInfo(i).value(QStringLiteral("shown")).toBool() && layer->rowInfo(i).value(QStringLiteral("count")).toInt() > 1) { mk0 = item(QStringLiteral("photoMk-%1").arg(i)); row0 = i; }
    if (r.check(shown(mk0), QStringLiteral("合并的标记画出来了"))) {
        QQuickItem *img = findItem(mk0, QStringLiteral("photoMkImg"));
        r.check(img && waitFor([img] { return img->property("status").toInt() == 1; }, 8000), QStringLiteral("标记上是缩略图（160 宽）"), img ? img->property("source").toString().left(26) : QString());
        QQuickItem *badge = findItem(mk0, QStringLiteral("photoMkCount"));
        r.check(shown(badge), QStringLiteral("合并的标记有张数"));
        // 代表图优先精选
        const QVariantMap info0 = layer->rowInfo(row0);
        const double zoom0 = mapc->zoom();
        // 把这一组里最后面的一张设成精选：代表图换成它
        QString favId, favFile;
        for (const Value &ph : store->data()[QStringLiteral("photos")].asArray()) {
            if (ph[QStringLiteral("file")].toString() == info0.value(QStringLiteral("file")).toString()) { favFile = ph[QStringLiteral("plan")].toString(); break; }
        }
        for (const Value &ph : store->data()[QStringLiteral("photos")].asArray())
            if (ph[QStringLiteral("plan")].toString() == favFile && ph[QStringLiteral("file")].toString() != info0.value(QStringLiteral("file")).toString()) { favId = ph[QStringLiteral("id")].toString(); favFile = ph[QStringLiteral("file")].toString(); }
        if (!favId.isEmpty()) {
            photos->setFav(favId, true);
            r.check(waitFor([&] {
                for (int i = 0; i < layer->slotCount(); ++i) { const QVariantMap m = layer->rowInfo(i); if (m.value(QStringLiteral("file")).toString() == favFile) return m.value(QStringLiteral("fav")).toBool(); }
                return false;
            }, 2000), QStringLiteral("代表图优先用精选"), favFile);
            photos->setFav(favId, false);
            waitFor([&] { return !layer->rowInfo(row0).value(QStringLiteral("fav")).toBool(); }, 1500);
        }
        // 点合并的标记：放大。挑一个不全在同一点的
        int spread = -1;
        for (int i = 0; i < layer->slotCount() && spread < 0; ++i) {
            const QVariantMap m = layer->rowInfo(i);
            if (m.value(QStringLiteral("shown")).toBool() && m.value(QStringLiteral("count")).toInt() > 1 && !m.value(QStringLiteral("same")).toBool()) spread = i;
        }
        waitFor([&] { return layer->rowInfo(spread).value(QStringLiteral("shown")).toBool(); }, 500);
        if (spread >= 0) {
            const QVariantMap m = layer->rowInfo(spread);
            click(win, QPointF(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()));
            r.check(waitFor([&] { return mapc->zoom() > zoom0 + 0.5; }, 2500), QStringLiteral("点合并的标记放大"), QStringLiteral("%1 → %2").arg(zoom0, 0, 'f', 2).arg(mapc->zoom(), 0, 'f', 2));
            waitMapIdle(win, mapc, 3000);
        } else r.info(QStringLiteral("没有跨地点的合并标记，放大没测"));
    }
    // 同一站的叠在站点上：点开看大图，标明位置来自站点
    {
        const QVariantList pts = layer->pointsOf(fromStopPlan);
        if (!pts.isEmpty()) {
            const QVariantList p0 = pts[0].toList();
            mapc->jumpTo(p0[0].toDouble(), p0[1].toDouble(), 14);
            waitMapIdle(win, mapc, 2000);
            int row = -1;
            for (int i = 0; i < layer->slotCount() && row < 0; ++i) {
                const QVariantMap m = layer->rowInfo(i);
                if (m.value(QStringLiteral("shown")).toBool() && std::abs(m.value(QStringLiteral("x")).toDouble() - win->width() / 2) < 120 && std::abs(m.value(QStringLiteral("y")).toDouble() - win->height() / 2) < 120) row = i;
            }
            if (r.check(row >= 0, QStringLiteral("放大后站点上有标记"))) {
                const QVariantMap m = layer->rowInfo(row);
                click(win, QPointF(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()));
                r.check(waitFor(lbOpen, 1500), QStringLiteral("点同一站的标记打开看大图"), QStringLiteral("%1 张").arg(m.value(QStringLiteral("count")).toInt()));
                QQuickItem *meta = item(QStringLiteral("lightboxMeta"));
                r.check(meta && meta->property("text").toString().contains(QStringLiteral("位置来自站点")), QStringLiteral("看大图标明位置来自站点"), meta ? meta->property("text").toString() : QString());
                closeLb();
            }
        }
    }

    // ---- 窄列表和 P1 ----
    setList(true);
    QQuickItem *albumView = nullptr;
    r.check(waitFor([&] { albumView = item(QStringLiteral("albumView")); return shown(albumView); }, 2000), QStringLiteral("照片模式的列表是相册"));
    QQuickItem *grid = item(QStringLiteral("albumGrid"));
    album->setView(QStringLiteral("trip"));
    const QVariantList trips = album->trips();
    r.check(!trips.isEmpty() && shown(grid), QStringLiteral("按旅程列出有照片的计划"), QStringLiteral("%1 趟").arg(trips.size()));
    QString tripId = trips.isEmpty() ? QString() : trips.first().toMap().value(QStringLiteral("id")).toString();
    for (const QVariant &t : trips) if (t.toMap().value(QStringLiteral("id")).toString() == fromStopPlan) tripId = fromStopPlan;
    mapc->jumpTo(0, 0, 2);
    waitMapIdle(win, mapc, 2000);
    QQuickItem *tripRow = nullptr;
    QMetaObject::invokeMethod(grid, "positionViewAtBeginning");
    waitFor([&] { tripRow = item(QStringLiteral("albumTrip-") + tripId); return shown(tripRow); }, 800);
    for (int y = 0; y < 3000 && !shown(tripRow); y += 200) { grid->setProperty("contentY", y); pause(40); tripRow = item(QStringLiteral("albumTrip-") + tripId); }
    // 代理在缓存区里也算"看得见"：把它滚到列表最上面再点
    if (tripRow) { grid->setProperty("contentY", tripRow->parentItem() ? tripRow->parentItem()->y() : 0.0); pause(80); tripRow = item(QStringLiteral("albumTrip-") + tripId); }
    if (r.check(shown(tripRow), QStringLiteral("列表里有这一趟"))) {
        const double lat0 = mapc->lat(), lng0 = mapc->lng();
        click(win, tripRow);
        r.check(album->plan() == tripId, QStringLiteral("P1：点一趟，展开这趟的照片"), album->plan());
        {   // 0.11.2 修：点一趟后列表不跳回顶上，这一趟还在视野里（0.11.1 重建前就定位，定位用的是旧行）
            QQuickItem *again = item(QStringLiteral("albumTrip-") + tripId);
            const double yy = again ? again->mapToItem(grid, QPointF(0, 0)).y() : -1;
            r.check(again && yy >= -1 && yy < grid->height() && grid->property("contentY").toDouble() >= 0, QStringLiteral("点一趟后这一趟还在视野里"), QStringLiteral("行 y=%1，contentY=%2").arg(yy).arg(grid->property("contentY").toDouble()));
        }
        r.check(waitFor([&] { return std::abs(mapc->lat() - lat0) + std::abs(mapc->lng() - lng0) > 0.5; }, 2500), QStringLiteral("P1：地图飞到这趟的范围"));
        waitMapIdle(win, mapc, 3000);
        r.check(layer->rowOfPlan(tripId) >= 0, QStringLiteral("飞到后这趟的照片在视野里"));
    }

    // ---- 相册整页 ----
    QQuickItem *expandBtn = item(QStringLiteral("listExpandBtn"));
    pause(700);   // Windows差异清单 #26
    click(win, expandBtn);
    QQuickItem *list = item(QStringLiteral("planList"));
    r.check(waitFor([&] { return win->property("albumOpen").toBool(); }, 1000), QStringLiteral("展开成相册"));
    waitStill(list, 1500);
    r.check(std::abs(list->width() - (win->width() - 2 * win->property("gap").toDouble())) < 2, QStringLiteral("相册铺满窗口"), QStringLiteral("%1 / %2").arg(list->width()).arg(win->width()));
    r.check(!album->compact() && album->layoutWidth() > 600, QStringLiteral("整页按宽窗口排行"), QString::number(album->layoutWidth()));
    // 0.12.9 等高行：照片比例对、整行铺满（窗口宽度和 QML 一致）、没裁切
    {
        waitFor([&] { return album->rowOfPhoto(album->anchorPhoto(0)) >= 0; }, 2000);
        waitFor([&] { return std::abs(grid->width() - album->layoutWidth()) < 1; }, 1500);
        int row = album->firstRowOf(QStringLiteral("photos"));
        const QVariantList first = album->data(album->index(row), AlbumModel::Items).toList();
        const double rowH = album->data(album->index(row), AlbumModel::RowH).toDouble();
        double sum = 0;
        for (const QVariant &v : first) sum += v.toMap().value(QStringLiteral("w")).toDouble();
        r.check(rowH > 0 && !first.isEmpty() && sum > 0 && sum <= album->layoutWidth() + 1, QStringLiteral("等高行：有高度，不超出宽度"), QStringLiteral("高 %1，宽合计 %2 / %3，%4 张").arg(rowH).arg(sum).arg(album->layoutWidth()).arg(first.size()));
        r.check(grid->property("contentItem").value<QQuickItem *>() != nullptr, QStringLiteral("等高行：列表还在"));
        QQuickItem *imgItem = findItem(grid, QStringLiteral("albumImg"));
        if (imgItem) r.check(imgItem->property("fillMode").toInt() == 1, QStringLiteral("等高行：照片不裁切（PreserveAspectFit）"), QString::number(imgItem->property("fillMode").toInt()));
    }
    QQuickItem *aside = item(QStringLiteral("albumAside"));
    r.check(shown(aside), QStringLiteral("整页按旅程有左边一列"));
    {   // 铺满时不飞
        const double lat0 = mapc->lat(), lng0 = mapc->lng();
        pause(500);
        QQuickItem *a0 = nullptr;
        waitFor([&] { a0 = item(QStringLiteral("albumAside-") + tripId); return a0 != nullptr; }, 1000);
        if (a0) {
            click(win, a0);
            r.check(album->plan() == tripId, QStringLiteral("整页点一趟：只看这一趟"));
            pause(300);
            r.check(mapc->lat() == lat0 && mapc->lng() == lng0, QStringLiteral("P1：列表铺满时地图不飞"));
        }
        album->setPlan(QString());
    }
    // 只建屏幕上的：行数多时，建出来的行远少于总行数（这份数据不大，10,000 张时再量）
    r.check(grid->property("count").toInt() > 0, QStringLiteral("相册有行"), QStringLiteral("%1 行").arg(grid->property("count").toInt()));

    // 精选：点星星，内容变、滚动位置不动
    waitFor([&] { QQuickItem *c = grid->property("contentItem").value<QQuickItem *>(); return c && !c->childItems().isEmpty(); }, 1000);
    const QStringList favIds = [&] { QStringList ids; for (const Value &ph : store->data()[QStringLiteral("photos")].asArray()) if (!ph[QStringLiteral("plan")].toString().isEmpty() && !ph[QStringLiteral("fav")].toBool()) ids << ph[QStringLiteral("id")].toString(); return ids; }();
    grid->setProperty("contentY", 400);
    pause(150);
    const double yBefore = grid->property("contentY").toDouble();
    QQuickItem *anyPhoto = nullptr;
    QString anyId;
    for (const QString &id : favIds) { anyPhoto = item(QStringLiteral("albumPhoto-") + id); if (shown(anyPhoto) && anyPhoto->mapToScene(QPointF(0, 0)).y() > grid->mapToScene(QPointF(0, 0)).y()) { anyId = id; break; } anyPhoto = nullptr; }
    if (r.check(anyPhoto != nullptr, QStringLiteral("屏幕上有照片"))) {
        const QPointF c = anyPhoto->mapToScene(QPointF(anyPhoto->width() - 16, 16));
        click(win, c);   // 先移上去才有星星；click 里先发了移动
        const auto favOf = [&](const QString &id) { for (const Value &ph : store->data()[QStringLiteral("photos")].asArray()) if (ph[QStringLiteral("id")].toString() == id) return ph[QStringLiteral("fav")].toBool(); return false; };
        r.check(waitFor([&] { return favOf(anyId); }, 1000), QStringLiteral("点星星：设为精选"), anyId);
        r.check(std::abs(grid->property("contentY").toDouble() - yBefore) < 1, QStringLiteral("点星星：列表不动"));
        // 看大图（同一个组件）：点照片打开，有精选和旋转；F 键切精选。这一行的格子刚重建过，重新找
        pause(700);
        anyPhoto = nullptr;
        waitFor([&] { anyPhoto = item(QStringLiteral("albumPhoto-") + anyId); return shown(anyPhoto); }, 1000);
        click(win, anyPhoto->mapToScene(QPointF(anyPhoto->width() / 2, anyPhoto->width() / 2)));
        r.check(waitFor(lbOpen, 1500), QStringLiteral("相册点照片打开看大图"));
        QQuickItem *favBtn = item(QStringLiteral("lightboxFav")), *rotBtn = item(QStringLiteral("lightboxRotate"));
        r.check(shown(favBtn) && shown(rotBtn), QStringLiteral("看大图有 ★ 精选和旋转"));
        r.check(favBtn && favBtn->property("text").toString().contains(QStringLiteral("已精选")), QStringLiteral("看大图显示已精选"), favBtn ? favBtn->property("text").toString() : QString());
        key(win, Qt::Key_F);
        r.check(waitFor([&] { return !favOf(anyId); }, 1000), QStringLiteral("F 键取消精选"));
        r.check(favBtn && favBtn->property("text").toString().contains(QStringLiteral("精选")) && !favBtn->property("text").toString().contains(QStringLiteral("已精选")), QStringLiteral("按钮跟着变"));
        key(win, Qt::Key_F);
        r.check(waitFor([&] { return favOf(anyId); }, 1000), QStringLiteral("F 键设为精选"));
        click(win, favBtn);
        r.check(waitFor([&] { return !favOf(anyId); }, 1000), QStringLiteral("点 ★ 取消精选"));
        // 旋转：转一张（JPEG 或 PNG），文件转了、原图备份、地址变、后台不卡
        QString rotId, rotFile;
        for (const Value &ph : store->data()[QStringLiteral("photos")].asArray()) if (ph[QStringLiteral("id")].toString() == anyId) rotFile = ph[QStringLiteral("file")].toString();
        rotId = anyId;
        const QString cur = lb->property("cur").toMap().value(QStringLiteral("id")).toString();
        r.check(cur == rotId, QStringLiteral("看大图打开的是点的这一张"), cur);
        if (photos->canRotate(rotFile)) {
            QImage before(store->dataDir() + u'/' + rotFile);
            const QSize s0 = photos->imageSize(rotFile);
            QQuickItem *fit = item(QStringLiteral("lightboxFit"));
            const QString src0 = fit->property("source").toString();
            auto done = std::make_shared<bool>(false);
            const QMetaObject::Connection c0 = QObject::connect(photos, &PhotoController::rotateFinished, photos, [done](const QString &, bool ok, const QString &) { *done = ok; });
            click(win, rotBtn);
            r.check(waitFor([&] { return *done; }, 5000), QStringLiteral("点旋转：后台转完"));
            QObject::disconnect(c0);
            const QSize s1 = photos->imageSize(rotFile);
            r.check(s1 == QSize(s0.height(), s0.width()), QStringLiteral("长宽换了"), QStringLiteral("%1x%2 → %3x%4").arg(s0.width()).arg(s0.height()).arg(s1.width()).arg(s1.height()));
            r.check(waitFor([&] { return fit->property("source").toString() != src0; }, 1000), QStringLiteral("看大图换了地址（重新读）"), fit->property("source").toString().right(14));
            r.check(waitFor([&] { return fit->property("status").toInt() == 1; }, 3000), QStringLiteral("转完的图读出来了"));
            r.check(QFileInfo::exists(store->dataDir() + QStringLiteral("/photos/.originals/") + rotFile.mid(7)), QStringLiteral("原图备份进 .originals"), rotFile);
            QQuickItem *note = item(QStringLiteral("lightboxNote"));
            r.check(note && note->property("text").toString().contains(QStringLiteral("旋转")), QStringLiteral("写一句已旋转"), note ? note->property("text").toString() : QString());
            // 转回去（数据副本也不留痕）
            photos->rotate(rotId, 3);
        } else r.info(QStringLiteral("这张不能转，旋转没测：") + rotFile);
        closeLb();
    }

    // 视图：按年份、精选、未归类；记住上次选的
    album->setView(QStringLiteral("year"));
    r.check(album->firstRowOf(QStringLiteral("year")) >= 0, QStringLiteral("按年份有年份的行"));
    album->setView(QStringLiteral("fav"));
    const int favsNow = album->favs();
    r.check(favsNow == 0 ? grid->property("count").toInt() == 0 && shown(item(QStringLiteral("albumEmpty"))) : grid->property("count").toInt() > 0, QStringLiteral("精选视图"), QStringLiteral("%1 张").arg(favsNow));
    // 未归类：把两张移出计划（数据副本），勾选后归到计划，再撤销
    {
        QStringList move;
        for (const Value &ph : store->data()[QStringLiteral("photos")].asArray())
            if (!ph[QStringLiteral("plan")].toString().isEmpty() && move.size() < 2) move << ph[QStringLiteral("id")].toString();
        const Value doc0 = store->data();
        photos->assign(move, QString(), QString());
        album->setView(QStringLiteral("inbox"));
        r.check(waitFor([&] { return album->inboxIds().size() >= 2; }, 1500), QStringLiteral("未归类列出这两张"), QStringLiteral("%1 张").arg(album->inbox()));
        waitFrames(win, 3);
        QQuickItem *sel = nullptr;
        waitFor([&] { sel = item(QStringLiteral("albumSel-") + move[0]); return shown(sel); }, 1500);
        pause(300);
        if (r.check(shown(sel), QStringLiteral("未归类有勾选框"))) {
            click(win, sel);
            r.check(albumView->property("selCount").toInt() == 1, QStringLiteral("勾选一张"), QString::number(albumView->property("selCount").toInt()));
            QQuickItem *assignBtn = item(QStringLiteral("albumAssign"));
            pause(700);
            click(win, assignBtn);
            QQuickItem *dlg = win->findChild<QQuickItem *>(QStringLiteral("photoAssign"));
            r.check(dlg && waitFor([dlg] { return dlg->property("open").toBool(); }, 1000), QStringLiteral("归到计划：弹出选择框"));
            QQuickItem *save = item(QStringLiteral("assignSave"));
            waitStill(save, 800);
            pause(400);
            const QString planPick = dlg ? dlg->property("planId").toString() : QString();
            click(win, save);
            const auto planOf = [&](const QString &id) { for (const Value &ph : store->data()[QStringLiteral("photos")].asArray()) if (ph[QStringLiteral("id")].toString() == id) return ph[QStringLiteral("plan")].toString(); return QString(); };
            r.check(waitFor([&] { return planOf(move[0]) == planPick && !planPick.isEmpty(); }, 1000), QStringLiteral("归到计划：写进这张的 plan"), planPick);
            r.check(planOf(move[1]).isEmpty(), QStringLiteral("没勾的还在未归类"));
            waitFor([&] { return !dlg->isVisible(); }, 1000);
        }
        store->modify([&doc0](Value &d) { d = doc0; });
        app->noteChanged(false);
        photos->album()->refresh();
    }
    album->setView(QStringLiteral("trip"));

    // Esc：先收相册，再关列表
    QVariant esc;
    QMetaObject::invokeMethod(win, "escOnce", Q_RETURN_ARG(QVariant, esc));
    r.check(esc.toString() == QLatin1String("album") && !win->property("albumOpen").toBool(), QStringLiteral("Esc 收起相册"), esc.toString());
    waitStill(list, 1500);
    r.check(album->compact(), QStringLiteral("回到窄列表"));

    // ---- HEIC ----
    {
        QTemporaryDir tmp;
        QStringList files;
#ifdef TP_SOURCE_DIR
        QFile pack(QStringLiteral(TP_SOURCE_DIR "/tests/photos.txt"));
        if (pack.open(QIODevice::ReadOnly))
            for (const QByteArray &l : pack.readAll().split('\n')) {
                const int tab = l.indexOf('\t');
                if (tab <= 0 || !l.left(tab).endsWith(".heic")) continue;
                QFile f(tmp.filePath(QString::fromUtf8(l.left(tab))));
                if (f.open(QIODevice::WriteOnly)) { f.write(QByteArray::fromBase64(l.mid(tab + 1).trimmed())); files << f.fileName(); }
            }
#endif
        const Value doc0 = store->data();
        QVariantList urls;
        for (const QString &f : files) urls << f;
        if (r.check(!files.isEmpty(), QStringLiteral("HEIC 测试文件"), QStringLiteral("%1 个").arg(files.size())) && photos->startImport(urls).value(QStringLiteral("ok")).toBool()) {
            waitFor([&] { return photos->importStage() == QLatin1String("review"); }, 3000);
            QMetaObject::invokeMethod(photos, "confirmImport");
            auto finished = std::make_shared<bool>(false);
            const QMetaObject::Connection c = QObject::connect(photos, &PhotoController::importFinished, photos, [finished](const QVariantMap &) { *finished = true; });
            waitFor([&] { return *finished || photos->importStage().isEmpty(); }, 3000);
            QObject::disconnect(c);
            r.check(waitFor([&] { return album->heic() == files.size(); }, 1500), QStringLiteral("HEIC 导入了"), QString::number(album->heic()));
            r.info(QStringLiteral("这台电脑的 HEIC：%1").arg(photos->heicReady() ? QStringLiteral("能解（HEIF + HEVC 扩展都在）") : QStringLiteral("解不了（没装扩展）")));
            // 这些照片的文件和记录
            QString heicId, heicFile, irotFile;
            for (const Value &ph : store->data()[QStringLiteral("photos")].asArray()) {
                const QString f = ph[QStringLiteral("file")].toString();
                if (f.endsWith(QLatin1String("irot.heic"))) { irotFile = f; }
                else if (f.endsWith(QLatin1String(".heic"))) { heicFile = f; heicId = ph[QStringLiteral("id")].toString(); }
            }
            r.check(!photos->canRotate(heicFile), QStringLiteral("HEIC 不能旋转"));
            const QVariantList items = album->itemsFor({ heicId });
            QMetaObject::invokeMethod(lb, "show", Q_ARG(QVariant, QVariant(items)), Q_ARG(QVariant, 0));
            QQuickItem *fit = item(QStringLiteral("lightboxFit")), *hint = item(QStringLiteral("lightboxHeicHint"));
            if (photos->heicReady()) {
                r.check(fit && waitFor([fit] { return fit->property("status").toInt() == 1; }, 3000), QStringLiteral("看大图真的解出了 HEIC"), fit ? fit->property("source").toString().left(24) : QString());
                r.check(!shown(hint), QStringLiteral("解得了时不提示装扩展"));
                r.check(photos->imageSize(irotFile) == QSize(48, 64), QStringLiteral("带 irot 的长宽按摆正后算"));
                QQuickItem *rotBtn = item(QStringLiteral("lightboxRotate"));
                click(win, rotBtn);
                QQuickItem *note = item(QStringLiteral("lightboxNote"));
                r.check(note && note->property("text").toString().contains(QStringLiteral("不能旋转")), QStringLiteral("点旋转：说明这种格式不能转"), note ? note->property("text").toString() : QString());
            }
            closeLb();
            // 没装扩展这条路（这台电脑装了，用开关假装没有）
            photos->setHeicTestOff(true);
            r.check(!photos->heicReady(), QStringLiteral("假装没装扩展"));
            QMetaObject::invokeMethod(lb, "show", Q_ARG(QVariant, QVariant(items)), Q_ARG(QVariant, 0));
            r.check(waitFor([&] { return shown(hint); }, 1500), QStringLiteral("没装扩展：看大图说去哪装"));
            r.check(shown(item(QStringLiteral("lightboxStoreHeif"))) && shown(item(QStringLiteral("lightboxStoreHevc"))), QStringLiteral("有两个商店按钮（自检不点，会打开商店）"));
            closeLb();
            r.check(waitFor([&] { return shown(item(QStringLiteral("albumHeicHint"))); }, 1500), QStringLiteral("没装扩展：相册上面有提示"));
            photos->setHeicTestOff(false);
        }
        store->modify([&doc0](Value &d) { d = doc0; });
        app->noteChanged(false);
        photos->album()->refresh();
    }

    // ---- 合成 10,000 张的帧时间（只记数）----
    {
        FrameStats *fs = frameStats();
        setList(false);
        mapc->setBase(QStringLiteral("street"));
        const auto drag = [&](const QString &label) {
            mapc->jumpTo(31, 112, 4.6);
            waitRendered(win, mapc, 3000);
            pause(600);   // 缩略图读完
            perf::resetStats();
            const double t0 = perf::nowMs();
            mapc->dragStart();
            QElapsedTimer t;
            t.start();
            QTimer timer;
            timer.setTimerType(Qt::PreciseTimer);
            timer.setInterval(16);
            QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
                const double ph = t.elapsed() / 2500.0 * 2 * M_PI;
                mapc->dragBy(7 * std::cos(ph), 2 * std::sin(2 * ph));
            });
            timer.start();
            waitFor([&] { return t.elapsed() >= 2500; }, 5000);
            timer.stop();
            const double t1 = perf::nowMs();
            mapc->dragEnd();
            mapc->jumpTo(mapc->lat(), mapc->lng(), mapc->zoom());
            const perf::Stat st = perf::stat(QStringLiteral("照片标记摆位"));
            r.info(QStringLiteral("拖地图 · %1：%2；摆位（C++）平均 %3 ms / 次、最长 %4 ms（%5 次）；画着 %6 个标记、%7 个元素")
                       .arg(label, fs ? fs->summary(t0 + 100, t1).text() : QStringLiteral("（没有每帧计时）"))
                       .arg(st.avg(), 0, 'f', 2).arg(st.max, 0, 'f', 1).arg(st.count).arg(layer->shownCount()).arg(layer->slotCount()));
            pause(200);
        };
        const auto scroll = [&](const QString &label) {
            QQuickItem *g = item(QStringLiteral("albumGrid"));
            if (!g) return;
            perf::resetStats();
            const double t0 = perf::nowMs();
            QElapsedTimer t;
            t.start();
            QTimer timer;
            timer.setTimerType(Qt::PreciseTimer);
            timer.setInterval(16);
            double y = 0;
            const double maxY = std::max(0.0, g->property("contentHeight").toDouble() - g->height());
            QObject::connect(&timer, &QTimer::timeout, &timer, [&] { y = std::min(maxY, y + 6); g->setProperty("contentY", y); });
            timer.start();
            waitFor([&] { return t.elapsed() >= 2500; }, 5000);
            timer.stop();
            const double t1 = perf::nowMs();
            int built = 0;
            if (QQuickItem *c = g->property("contentItem").value<QQuickItem *>()) built = int(c->childItems().size());
            r.info(QStringLiteral("滚动相册 · %1：%2；总行数 %3、建出来的行 %4")
                       .arg(label, fs ? fs->summary(t0 + 100, t1).text() : QStringLiteral("（没有每帧计时）")).arg(g->property("count").toInt()).arg(built));
            g->setProperty("contentY", 0);
            pause(200);
        };
        // 0.12.9 再量一次快速滚动（每帧 40 像素，一直在建新行）
        const auto scrollFast = [&](const QString &label) {
            QQuickItem *g = item(QStringLiteral("albumGrid"));
            if (!g) return;
            const double t0 = perf::nowMs();
            QElapsedTimer t;
            t.start();
            QTimer timer;
            timer.setTimerType(Qt::PreciseTimer);
            timer.setInterval(16);
            double y = 0;
            QObject::connect(&timer, &QTimer::timeout, &timer, [&] { y += 40; g->setProperty("contentY", y); });
            timer.start();
            waitFor([&] { return t.elapsed() >= 2500; }, 5000);
            timer.stop();
            r.info(QStringLiteral("快速滚动相册 · %1：%2").arg(label, fs ? fs->summary(t0 + 100, perf::nowMs()).text() : QStringLiteral("（没有每帧计时）")));
            g->setProperty("contentY", 0);
            pause(200);
        };
        drag(QStringLiteral("开发数据 %1 张").arg(total));
        setList(true);
        win->setProperty("albumOpen", true);
        waitStill(item(QStringLiteral("planList")), 1500);
        scroll(QStringLiteral("开发数据 %1 张").arg(total));
        win->setProperty("albumOpen", false);
        setList(false);

        // 合成：把现有照片的记录复制到 10,000 条（文件重复用），GPS 撒在 300 个点周围。数据副本，用完退回
        const Value doc0 = store->data();
        const json::Array src = doc0[QStringLiteral("photos")].asArray();
        QElapsedTimer tb;
        tb.start();
        store->modify([&](Value &d) {
            json::Array out;
            out.reserve(10000);
            auto *rng = QRandomGenerator::global();
            for (int i = 0; i < 10000; ++i) {
                Value ph = src[size_t(i) % src.size()];
                json::Object &o = ph.mutObject();
                o.set(QStringLiteral("id"), Value(QStringLiteral("syn%1").arg(i)));
                const int spot = i % 300;
                o.set(QStringLiteral("lat"), Value(20.0 + (spot % 20) * 2 + rng->bounded(0.3)));
                o.set(QStringLiteral("lng"), Value(90.0 + (spot / 20) * 4 + rng->bounded(0.3)));
                out.push_back(std::move(ph));
            }
            d.mutObject().set(QStringLiteral("photos"), Value(std::move(out)));
        });
        const qint64 modifyMs = tb.restart();
        photos->album()->refresh();
        mapc->refilter();
        r.info(QStringLiteral("10,000 张（合成，文件重复用 %1 张）：改数据 %2 ms，相册 + 标记重建 %3 ms；有位置 %4 张").arg(src.size()).arg(modifyMs).arg(tb.elapsed()).arg(layer->pinned()));
        r.check(layer->pinned() == 10000, QStringLiteral("10,000 张都有位置"), QString::number(layer->pinned()));
        for (const double z : { 3.0, 5.0, 8.0, 12.0 }) {
            mapc->jumpTo(31, 112, z);
            waitMapIdle(win, mapc, 2000);
            r.info(QStringLiteral("10,000 张 · %1 级：画着 %2 个标记，元素 %3 个").arg(z).arg(layer->shownCount()).arg(layer->slotCount()));
        }
        r.check(layer->slotCount() < 400, QStringLiteral("10,000 张时元素个数没有跟着张数涨"), QString::number(layer->slotCount()));
        drag(QStringLiteral("合成 10,000 张，4.6 级"));
        setList(true);
        win->setProperty("albumOpen", true);
        QQuickItem *g = nullptr;
        waitFor([&] { g = item(QStringLiteral("albumGrid")); return g != nullptr; }, 1500);
        waitStill(item(QStringLiteral("planList")), 1500);
        album->setView(QStringLiteral("year"));
        waitFrames(win, 3);
        pause(800);
        scroll(QStringLiteral("合成 10,000 张，按年份"));
        scrollFast(QStringLiteral("合成 10,000 张，按年份"));
        if (g) {
            int built = 0;
            if (QQuickItem *c = g->property("contentItem").value<QQuickItem *>()) built = int(c->childItems().size());
            r.check(built < 80 && g->property("count").toInt() > 400, QStringLiteral("10,000 张：只建屏幕上的行"), QStringLiteral("总 %1 行，建了 %2").arg(g->property("count").toInt()).arg(built));
        }
        album->setView(QStringLiteral("trip"));
        win->setProperty("albumOpen", false);
        setList(false);
        store->modify([&doc0](Value &d) { d = doc0; });
        app->noteChanged(false);
        photos->album()->refresh();
        mapc->refilter();
        waitFrames(win, 3);
    }

    goPage(QStringLiteral("plan"));
    waitFor([pc] { return pc->mode() == QLatin1String("plan"); }, 2000);
    r.check(waitFor([&] { return !layer->active() && !album->active(); }, 1000), QStringLiteral("离开照片模式：标记层和相册关掉"));
    r.check(waitFor([&] { return mapc->routesJson() == routesBefore || mapc->routesJson().contains(QLatin1String("coordinates")); }, 2000), QStringLiteral("回到计划模式：路线回来"));
}

} // namespace tp::app::selftest
