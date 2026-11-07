// 自检（0.11.4）：阶段 ⑤ 总检查补测，能用鼠标真点的都真点：计划库的滚动位置和封面丢失、想去的下拉和日期、
// 简易面板的备注和封面、地球上的照片标记、动效关掉时 P1 直接到位、长游记的帧时间、设置页周首、季节表的 chip、2033 年的斋月。
// 只在数据副本上做，最后改回去。thumbs1000 单独点名才跑（第一次生成 1,000 张缩略图，要一两分钟）。
#include "selftest_util.h"

#include "albummodel.h"
#include "appcontroller.h"
#include "calendarcontroller.h"
#include "datastore.h"
#include "framestats.h"
#include "journalcontroller.h"
#include "mapcontroller.h"
#include "navhistory.h"
#include "perf.h"
#include "photocontroller.h"
#include "photolayer.h"
#include "plancontroller.h"
#include "plans.h"
#include "thumbcache.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <qpa/qwindowsysteminterface.h>

#include <atomic>
#include <cmath>
#include <windows.h>
#include <psapi.h>

namespace tp::app::selftest {

using json::Value;
namespace plans = tp::map::plans;

namespace {
bool shownChain(QQuickItem *it)
{
    for (; it; it = it->parentItem())
        if (!it->isVisible() || it->opacity() <= 0.01) return false;
    return true;
}
// 同名的元素可能有几个（下拉、小日历放在窗口最上层）：只要看得见的
QQuickItem *findShown(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name && shownChain(root)) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findShown(c, name)) return f;
    return nullptr;
}
QQuickItem *findPrefix(QQuickItem *root, const QString &prefix)
{
    if (!root) return nullptr;
    if (root->objectName().startsWith(prefix) && shownChain(root)) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findPrefix(c, prefix)) return f;
    return nullptr;
}
struct Mouse
{
    QQuickWindow *win;
    QQuickItem *item(const QString &n) const { return findShown(win->contentItem(), n); }
    void at(const QPointF &p, Qt::MouseButtons b, Qt::MouseButton which, QEvent::Type t) const
    {
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, win->mapToGlobal(p), b, which, t);
    }
    void click(QQuickItem *it) const
    {
        if (!it) return;
        const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
        at(p, Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
        pause(30);
        at(p, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        at(p, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
        pause(120);
    }
    void click(const QString &n) const { click(item(n)); }
    void hover(QQuickItem *it) const
    {
        if (it) at(it->mapToScene(QPointF(it->width() / 2, it->height() / 2)), Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
        pause(60);
    }
};
template <class T>
T *ctxObject(QQuickWindow *win, const char *name)
{
    QQmlContext *ctx = QQmlEngine::contextForObject(win);
    return ctx ? qobject_cast<T *>(ctx->contextProperty(QString::fromLatin1(name)).value<QObject *>()) : nullptr;
}
QString str(const Value &o, const char *k) { return o[QString::fromLatin1(k)].toString(); }
// 让 Flickable 按固定节奏来回滚 2.5 秒，返回这段的帧时间
QString scrollFrames(QQuickWindow *win, QQuickItem *flick)
{
    Q_UNUSED(win)
    FrameStats *fs = frameStats();
    if (!flick || !fs) return QStringLiteral("（没有每帧计时）");
    const double range = std::max(0.0, flick->property("contentHeight").toDouble() - flick->height());
    const double t0 = perf::nowMs();
    QElapsedTimer t;
    t.start();
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(16);
    QObject::connect(&timer, &QTimer::timeout, &timer, [&] { flick->setProperty("contentY", range * (0.5 - 0.5 * std::cos(t.elapsed() / 2500.0 * 2 * M_PI))); });
    timer.start();
    waitFor([&] { return t.elapsed() >= 2500; }, 5000);
    timer.stop();
    const QString s = fs->summary(t0 + 100, perf::nowMs()).text();
    flick->setProperty("contentY", 0);
    return QStringLiteral("内容高 %1 px：%2").arg(flick->property("contentHeight").toDouble(), 0, 'f', 0).arg(s);
}
} // namespace

void runAudit(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, PhotoController *photos)
{
    r.section(QStringLiteral("阶段 ⑤ 总检查补测（0.11.4）"));
    PlanController *pc = plansCtl();
    NavHistory *nav = navCtl();
    auto *cal = ctxObject<CalendarController>(win, "calendar");
    auto *journal = ctxObject<JournalController>(win, "journal");
    data::DataStore *store = app->store();
    if (!r.check(pc && nav && cal && journal && store->dataDir().contains(QLatin1String("selftest")), QStringLiteral("在数据副本上（路径里有 selftest）"), store->dataDir())) return;
    Mouse m{ win };
    const Value doc0 = store->data();
    const auto goPage = [win](const QString &p) { QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, p)); };
    const auto restore = [&] { store->modify([&doc0](Value &d) { d = doc0; }); app->noteChanged(false); };

    // ---- 计划库：置顶、隐藏后滚动位置不变 ----
    goPage(QStringLiteral("plan"));
    waitFor([pc] { return pc->mode() == QLatin1String("plan"); }, 2000);
    win->setProperty("listOpen", true);
    QQuickItem *panel = m.item(QStringLiteral("planList"));
    waitStill(panel);
    pc->setLibraryOpen(true);
    waitStill(panel);
    const QString view0 = pc->library().value(QStringLiteral("view")).toString();
    const auto rows = [pc] { return pc->library().value(QStringLiteral("rows")).toList(); };
    const auto keepCheck = [&](const QString &viewName, const QString &flickName) {
        pc->setLibraryPref(QStringLiteral("view"), viewName);
        waitFrames(win, 4);
        QQuickItem *flick = m.item(flickName);
        if (!r.check(flick, QStringLiteral("计划库 %1：有列表").arg(viewName))) return;
        const double y0 = std::min(flick->property("contentHeight").toDouble() - flick->height(), 420.0);
        flick->setProperty("contentY", y0);
        waitFrames(win, 3);
        // 这一屏中间的一行
        QString id;
        QQuickItem *pin = nullptr;
        for (const QVariant &v : rows()) {
            const QString rid = v.toMap().value(QStringLiteral("id")).toString();
            QQuickItem *row = findShown(flick, (viewName == QLatin1String("table") ? QStringLiteral("libRow-") : QStringLiteral("libCard-")) + rid);
            if (!row) continue;
            const double y = row->mapToItem(flick, QPointF(0, row->height() / 2)).y();
            if (y < 60 || y > flick->height() - 60) continue;
            m.hover(row);
            pin = findShown(flick, QStringLiteral("libPin-") + rid);
            if (pin) { id = rid; break; }
        }
        if (!r.check(pin, QStringLiteral("%1：这一屏找到一行的置顶按钮").arg(viewName))) return;
        const auto same = [&] { return std::abs(flick->property("contentY").toDouble() - y0) < 1; };
        const auto yNow = [&] { return QString::number(flick->property("contentY").toDouble()) + QStringLiteral(" / ") + QString::number(y0); };
        m.click(pin);
        waitFrames(win, 4);
        r.check(same(), QStringLiteral("%1：置顶后滚动位置不变").arg(viewName), yNow());
        flick->setProperty("contentY", y0);
        waitFrames(win, 2);
        // 解开置顶（行现在在最前面，直接调），再隐藏这一屏的一行
        pc->togglePin(id);
        waitFrames(win, 3);
        flick->setProperty("contentY", y0);
        waitFrames(win, 3);
        QString hid;
        QQuickItem *hide = nullptr;
        for (const QVariant &v : rows()) {
            const QString rid = v.toMap().value(QStringLiteral("id")).toString();
            QQuickItem *row = findShown(flick, (viewName == QLatin1String("table") ? QStringLiteral("libRow-") : QStringLiteral("libCard-")) + rid);
            if (!row) continue;
            const double y = row->mapToItem(flick, QPointF(0, row->height() / 2)).y();
            if (y < 60 || y > flick->height() - 60) continue;
            m.hover(row);
            hide = findShown(flick, QStringLiteral("libHide-") + rid);
            if (hide) { hid = rid; break; }
        }
        if (hide) {
            m.click(hide);
            waitFrames(win, 4);
            r.check(same(), QStringLiteral("%1：隐藏后滚动位置不变").arg(viewName), yNow());
            pc->setListHidden({ hid }, false);
            waitFrames(win, 3);
        } else r.check(false, QStringLiteral("%1：这一屏找到一行的隐藏按钮").arg(viewName));
    };
    keepCheck(QStringLiteral("table"), QStringLiteral("libTable"));
    keepCheck(QStringLiteral("gallery"), QStringLiteral("libGallery"));

    // ---- 换状态：点的那一下不顿（列表马上变，地图隔 200 ms）----
    if (FrameStats *fs = frameStats()) {
        pause(400);
        QElapsedTimer t;
        t.start();
        const double t0 = perf::nowMs();
        m.click(QStringLiteral("libStatus-wish"));
        const qint64 clickMs = t.elapsed();
        waitFor([&] { for (const map::TripPlan &p : mapc->trips().plans()) if (p.status != QLatin1String("wish")) return false; return true; }, 1500);
        const double gap1 = fs->longestGapSince(t0);
        pause(300);
        m.click(QStringLiteral("libStatus-all"));
        pause(500);
        r.info(QStringLiteral("换状态：点一下到列表更新 %1 ms（含自检的 150 ms 等待）；这段最长一帧 %2 ms（地图 200 ms 后重建）").arg(clickMs).arg(gap1, 0, 'f', 0));
    }

    // ---- 封面丢失：画廊显示"照片找不到" ----
    {
        pc->setLibraryPref(QStringLiteral("view"), QStringLiteral("gallery"));
        waitFrames(win, 3);
        QQuickItem *flick = m.item(QStringLiteral("libGallery"));
        if (flick) flick->setProperty("contentY", 0);
        QString id, cover;
        for (const QVariant &v : rows()) {
            const QVariantMap row = v.toMap();
            if (row.value(QStringLiteral("coverFile")).toString().startsWith(QLatin1String("photos/"))) {
                id = row.value(QStringLiteral("id")).toString();
                cover = row.value(QStringLiteral("coverFile")).toString();
                break;
            }
        }
        // 丢文件的情形：记录还指着，文件没了（磁盘上的缩略图和内存里的图都没有这个名字）
        const QString gone = QStringLiteral("photos/2026-11/audit_gone.jpg");
        if (r.check(!id.isEmpty(), QStringLiteral("封面丢失：有带封面的计划"), cover)) {
            store->modify([&](Value &d) {
                for (Value &ph : d.mutObject().find(QStringLiteral("photos"))->mutArray())
                    if (str(ph, "file") == cover) ph.mutObject().set(QStringLiteral("file"), Value(gone));
            });
            app->noteChanged(false);
            const bool missing = waitFor([&] { return findShown(win->contentItem(), QStringLiteral("libCoverMissing-") + id) != nullptr; }, 4000);
            r.check(missing, QStringLiteral("封面丢失：卡片上显示\"照片找不到\""), pc->planBasics(id).value(QStringLiteral("name")).toString());
            restore();
        }
    }
    pc->setLibraryPref(QStringLiteral("view"), view0);
    pc->setLibraryOpen(false);
    waitStill(panel);

    // ---- 想去：真点开始计划、小日历；关联季节的下拉 ----
    goPage(QStringLiteral("wish"));
    waitFor([pc] { return pc->mode() == QLatin1String("wish"); }, 2000);
    win->setProperty("listOpen", true);
    waitStill(panel);
    QString wid;
    const auto wishRows = [pc] { return pc->list().value(QStringLiteral("rows")).toList(); };
    for (const QVariant &v : wishRows()) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        if (m.item(QStringLiteral("wishStartBtn-") + id) && !id.isEmpty()) { wid = id; break; }
    }
    if (r.check(!wid.isEmpty(), QStringLiteral("想去：列表上有开始计划按钮"), wid)) {
        QQuickItem *dlg = win->findChild<QQuickItem *>(QStringLiteral("wishDialog"));
        // 一周从周日开始时小日历也跟着（设置页真点，见下面）：先走设置页
        goPage(QStringLiteral("settings"));
        if (QQuickItem *panel = m.item(QStringLiteral("settingsPanel"))) panel->setProperty("section", QStringLiteral("cal"));   // 0.12.3 分组了
        QQuickItem *seg = nullptr;
        waitFor([&] { seg = m.item(QStringLiteral("weekStartSeg-0")); return seg != nullptr; }, 2500);
        waitStill(m.item(QStringLiteral("weekStartSeg")), 1500);
        m.click(seg);
        r.check(cal->weekStart() == 0, QStringLiteral("设置页：鼠标点\"周日\"，一周从周日开始"), QString::number(cal->weekStart()));
        goPage(QStringLiteral("wish"));
        waitFor([&] { return m.item(QStringLiteral("wishStartBtn-") + wid) != nullptr; }, 2500);
        waitStill(panel);
        m.click(QStringLiteral("wishStartBtn-") + wid);
        r.check(waitFor([&] { return dlg && dlg->property("open").toBool() && dlg->opacity() > 0.5; }, 1500), QStringLiteral("开始计划：鼠标点开对话框"));
        QQuickItem *start = m.item(QStringLiteral("wishStart"));
        m.click(start ? findShown(start, QStringLiteral("dateCalBtn")) : nullptr);
        const QString d1 = QStringLiteral("2031-06-15");
        if (start) {   // 翻到这个月（翻月按钮只是改属性，自检直接设），日子用鼠标点
            start->setProperty("viewY", 2031);
            start->setProperty("viewM", 5);
        }
        waitFrames(win, 3);
        // 2031-06-01 是周日：周日开始时前面没有空格
        const QVariantList cells = start ? start->property("cells").toList() : QVariantList();
        r.check(!cells.isEmpty() && cells.first().toInt() == 1, QStringLiteral("小日历：一周从周日开始时，2031 年 6 月 1 日（周日）在第一格"), cells.isEmpty() ? QString() : cells.first().toString());
        m.click(QStringLiteral("dateDay-") + d1);
        r.check(dlg && dlg->property("start").toString() == d1, QStringLiteral("开始计划：鼠标在小日历上点日期"), dlg ? dlg->property("start").toString() : QString());
        m.click(QStringLiteral("wishSave"));
        const Value *wp = plans::planById(store->data(), wid);
        r.check(waitFor([&] { wp = plans::planById(store->data(), wid); return wp && plans::statusOf(*wp) == QLatin1String("planning"); }, 1500)
                    && wp && str(*wp, "startDate") == d1, QStringLiteral("开始计划：鼠标点保存，改成计划中，日期存了"));
        restore();
        waitFrames(win, 3);

        // 周一开始（真点回去）
        goPage(QStringLiteral("settings"));
        waitFor([&] { seg = m.item(QStringLiteral("weekStartSeg-1")); return seg != nullptr; }, 2500);
        waitStill(m.item(QStringLiteral("weekStartSeg")), 1500);
        m.click(seg);
        r.check(cal->weekStart() == 1, QStringLiteral("设置页：鼠标点\"周一\"改回去"));
        goPage(QStringLiteral("wish"));
        waitFor([&] { return m.item(QStringLiteral("wishSeasonBtn-") + wid) != nullptr; }, 2500);
        waitStill(panel);

        m.click(QStringLiteral("wishSeasonBtn-") + wid);
        waitFor([&] { return dlg->property("open").toBool() && dlg->opacity() > 0.5; }, 1500);
        const QVariantList opts = dlg->property("seasonOptions").toList();
        m.click(QStringLiteral("wishSeason"));
        waitFrames(win, 3);
        // 下拉只滚到当前项，点一个完整露在列表里的（其他的被裁掉，点不中）
        QString ref;
        QQuickItem *opt = nullptr;
        for (const QVariant &v : opts) {
            const QString k = v.toMap().value(QStringLiteral("key")).toString();
            QQuickItem *it = m.item(QStringLiteral("selOpt-") + k);
            QQuickItem *lv = it && it->parentItem() ? it->parentItem()->parentItem() : nullptr;
            if (!it || !lv || k == dlg->property("ref").toString()) continue;
            const double y = it->mapToItem(lv, QPointF(0, 0)).y();
            if (y >= 0 && y + it->height() <= lv->height()) { ref = k; opt = it; break; }
        }
        r.check(opt, QStringLiteral("关联季节：鼠标点开下拉"), ref);
        m.click(opt);
        r.check(dlg->property("ref").toString() == ref, QStringLiteral("关联季节：鼠标点选一行"), dlg->property("ref").toString());
        m.click(QStringLiteral("wishSave"));
        r.check(waitFor([&] { return pc->planBasics(wid).value(QStringLiteral("seasonRef")).toString() == ref; }, 1500), QStringLiteral("关联季节：鼠标点保存，存了"));
        restore();
        waitFrames(win, 3);

        // 选中后简易面板：备注和封面
        store->modify([&](Value &d) {
            if (Value *p = plans::mutPlan(d, wid)) p->mutObject().set(QStringLiteral("notes"), Value(QStringLiteral("总检查的备注")));
        });
        app->noteChanged(false);
        waitFor([&] { return m.item(QStringLiteral("planRow-") + wid) != nullptr; }, 1500);
        // 点这一行的名字（中间是按钮）
        if (QQuickItem *row = m.item(QStringLiteral("planRow-") + wid)) {
            waitStill(row, 1500);   // 列表重排、面板动画没停时按原位置点不中（全量里间歇失败过）
            waitFrames(win, 2);
            const QPointF p = row->mapToScene(QPointF(row->width() * 0.4, 14));
            m.at(p, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
            m.at(p, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
        }
        QQuickItem *notes = nullptr, *cover = nullptr;
        r.check(waitFor([&] { notes = m.item(QStringLiteral("sheetNotes")); return notes != nullptr; }, 2500), QStringLiteral("想去：选中后简易面板有备注"), pc->sheet().value(QStringLiteral("name")).toString());
        const bool hasCover = !pc->sheet().value(QStringLiteral("coverThumb")).toString().isEmpty();
        r.check(hasCover && waitFor([&] { cover = m.item(QStringLiteral("sheetCover")); return cover && cover->property("status").toInt() == 1; }, 2500), QStringLiteral("想去：简易面板有封面，读出来了"));
        mapc->markers()->clearSelection();
        restore();
    }

    // ---- 地球上的照片标记 ----
    win->setProperty("listOpen", false);
    goPage(QStringLiteral("photo"));
    waitFor([pc] { return pc->mode() == QLatin1String("photo"); }, 2000);
    map::PhotoLayer *layer = mapc->photoLayer();
    waitFor([&] { return layer->active(); }, 1500);
    if (layer->pinned() > 0) {
        mapc->fitPoints(layer->pointsOf(QString()), 10, 0);
        waitMapIdle(win, mapc, 3000);
        const QString base0 = mapc->base();
        QMetaObject::invokeMethod(win, "pickBase", Q_ARG(QVariant, QStringLiteral("globe")));
        const bool onGlobe = waitFor([&] { return win->property("globeMode").toBool(); }, 4000);
        pause(900);
        QQuickItem *gm = win->findChild<QQuickItem *>(QStringLiteral("globePhotoMarkers"));
        QQuickItem *mk = nullptr;
        const bool hasMk = waitFor([&] { mk = findPrefix(gm, QStringLiteral("photoMk-")); return mk != nullptr; }, 4000);
        r.check(onGlobe && hasMk, QStringLiteral("地球上：照片标记画出来了"), QStringLiteral("画着 %1 个").arg(layer->shownCount()));
        if (mk) {
            QQuickItem *img = findPrefix(mk, QStringLiteral("photoMkImg"));
            r.check(img && waitFor([&] { return img->property("status").toInt() == 1; }, 3000), QStringLiteral("地球上：标记的缩略图读出来了"));
            QQuickItem *lb = win->findChild<QQuickItem *>(QStringLiteral("lightbox"));
            const double z0 = mapc->zoom();
            m.click(mk);
            const bool reacted = waitFor([&] { return (lb && lb->property("open").toBool()) || std::abs(mapc->zoom() - z0) > 0.05 || layer->shownCount() == 0; }, 2500);
            r.check(reacted, QStringLiteral("地球上：鼠标点标记有反应（看大图或拉近）"));
            if (lb && lb->property("open").toBool()) { QMetaObject::invokeMethod(lb, "close"); waitFor([&] { return !lb->isVisible(); }, 1000); }
        }
        QMetaObject::invokeMethod(win, "pickBase", Q_ARG(QVariant, base0));
        waitFor([&] { return !win->property("globeMode").toBool(); }, 4000);
        pause(600);

        // ---- 动效关掉：P1 直接到位 ----
        win->setProperty("listOpen", true);
        waitStill(panel);
        AlbumModel *album = photos->album();
        album->setView(QStringLiteral("trip"));
        QString tripId;
        for (const QVariant &v : album->trips()) {
            const QString id = v.toMap().value(QStringLiteral("id")).toString();
            if (layer->pointsOf(id).size() > 0) tripId = id;
        }
        album->setPlan(QString());
        mapc->jumpTo(0, 0, 3);
        waitMapIdle(win, mapc, 2000);
        QMetaObject::invokeMethod(win, "setMotionForTest", Q_ARG(QVariant, false));
        const double lat0 = mapc->lat(), lng0 = mapc->lng();
        QElapsedTimer t;
        t.start();
        QMetaObject::invokeMethod(win, "flyToPhotos", Q_ARG(QVariant, tripId));
        waitFrames(win, 2);
        const qint64 ms = t.elapsed();
        const double lat1 = mapc->lat(), lng1 = mapc->lng();
        pause(400);
        r.check(std::abs(lat1 - lat0) + std::abs(lng1 - lng0) > 0.5 && std::abs(mapc->lat() - lat1) + std::abs(mapc->lng() - lng1) < 0.001,
                QStringLiteral("动效关掉：P1 直接到位，两帧内到（%1 ms），之后不再动").arg(ms));
        QMetaObject::invokeMethod(win, "setMotionForTest", Q_ARG(QVariant, true));
    } else r.info(QStringLiteral("地图上没有照片，跳过地球和 P1"));
    win->setProperty("listOpen", false);

    // ---- 此刻卡：刚回来时"写游记" ----
    {
        goPage(QStringLiteral("plan"));
        QString did;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
            if (!str(p, "endDate").isEmpty()) { did = str(p, "id"); break; }
        pc->setTodayOverride(QStringLiteral("2099-01-01"));
        pc->setNextTrip(did, true);
        win->setProperty("nowOpen", true);
        QQuickItem *btn = nullptr;
        r.check(waitFor([&] { btn = m.item(QStringLiteral("nowJournal")); return btn != nullptr; }, 2000), QStringLiteral("此刻卡：刚回来时有\"写游记\""), pc->now().value(QStringLiteral("kind")).toString());
        waitStill(btn, 1000);
        m.click(btn);
        r.check(waitFor([&] { return nav->page() == QLatin1String("trip") && nav->params().value(QStringLiteral("id")).toString() == did; }, 2000) && waitFor([&] { QQuickItem *pg = m.item(QStringLiteral("planPage")); return pg && pg->property("tab").toString() == QLatin1String("journal"); }, 2000),
                QStringLiteral("写游记：鼠标点开这个计划的游记标签"), nav->page());
        goPage(QStringLiteral("plan"));
        pc->setNextTrip(did, false);
        pc->setTodayOverride(QString());
        restore();
    }

    // ---- 只缺 HEVC ----
    r.info(QStringLiteral("只缺 HEVC：解一张内置 HEIC 样本才算能用（读到像素），只有 HEIF 没有 HEVC 时走同一个\"解不了\"分支；这台电脑两个都装了，不卸载，没有真实走到"));

    // ---- 长游记 ----
    {
        QString body;
        const QVariantList lib = photos->library();
        int pic = 0;
        for (int i = 0; body.size() < 6000; ++i) {
            body += QStringLiteral("## 第 %1 段\n\n这是总检查写的长游记。早上从营地出发，沿着河谷走了很久，中午在山口停下来吃东西，下午转进森林，傍晚才到下一个住处。路上遇到几拨人，聊了聊前面的路况。\n\n").arg(i + 1);
            if (i % 4 == 1 && pic < 20 && pic < lib.size())
                body += QStringLiteral("![照片](photo:%1)\n\n").arg(lib[pic++].toMap().value(QStringLiteral("id")).toString());
        }
        const QString id = journal->newId();
        journal->save(id, { { QStringLiteral("title"), QStringLiteral("总检查长游记") }, { QStringLiteral("date"), QStringLiteral("2031-01-01") }, { QStringLiteral("body"), body } });
        nav->go(QStringLiteral("journal"), { { QStringLiteral("id"), id } });
        QQuickItem *read = nullptr;
        const bool hasRead = waitFor([&] { read = m.item(QStringLiteral("journalRead")); return read != nullptr; }, 2500);
        pause(1500);
        r.check(hasRead, QStringLiteral("长游记：打开阅读"), QStringLiteral("%1 字，%2 张照片").arg(body.size()).arg(pic));
        r.info(QStringLiteral("长游记阅读滚动，") + scrollFrames(win, read));
        nav->go(QStringLiteral("journal"), { { QStringLiteral("id"), id }, { QStringLiteral("edit"), true } });
        QQuickItem *edit = nullptr, *prev = nullptr;
        waitFor([&] { edit = m.item(QStringLiteral("journalEdit")); prev = m.item(QStringLiteral("journalPreviewFlick")); return edit && prev; }, 2500);
        pause(1500);
        if (r.check(edit && prev, QStringLiteral("长游记：打开编辑"))) {
            r.info(QStringLiteral("长游记预览滚动，") + scrollFrames(win, prev));
            QQuickItem *f = edit->parentItem();
            while (f && f->metaObject()->indexOfProperty("contentY") < 0) f = f->parentItem();
            r.info(QStringLiteral("长游记写的一边滚动，") + scrollFrames(win, f));
            // 打字：改一次正文到下一帧
            FrameStats *fs = frameStats();
            const QString text0 = edit->property("text").toString();
            const double t0 = perf::nowMs();
            for (int i = 0; i < 20; ++i) {
                edit->setProperty("text", text0 + QString(i + 1, QChar(u'字')));
                pause(120);
            }
            if (fs) r.info(QStringLiteral("长游记打字（每 120 ms 改一次，共 20 次）：%1").arg(fs->summary(t0 + 100, perf::nowMs()).text()));
        }
        nav->go(QStringLiteral("journal"));
        pause(300);
        restore();
    }

    // ---- 季节表：chip 真点、2033 年的两段斋月 ----
    {
        goPage(QStringLiteral("calendar"));
        const QString calView0 = cal->view();
        cal->setView(QStringLiteral("season"));
        waitFor([&] { return m.item(QStringLiteral("calSeasonView")) != nullptr && cal->info().value(QStringLiteral("season")).toMap().value(QStringLiteral("ready")).toBool(); }, 6000);
        cal->setAllSeasonGroups(true);
        cal->setSeasonGroupClosed(QStringLiteral("东亚"), false);
        waitStill(m.item(QStringLiteral("calendarPage")), 1500);
        waitFrames(win, 4);
        QQuickItem *chip = findPrefix(m.item(QStringLiteral("calSeasonView")), QStringLiteral("calSeasonPlan-"));
        if (r.check(chip, QStringLiteral("季节表：东亚有相关计划的 chip"))) {
            const QString pid = chip->objectName().mid(QStringLiteral("calSeasonPlan-").size());
            m.click(chip);
            const bool opened = waitFor([&] { return nav->page() == QLatin1String("trip") && nav->params().value(QStringLiteral("id")).toString() == pid; }, 2000);
            r.check(opened, QStringLiteral("季节表：鼠标点 chip 打开那个计划"), nav->page() + u' ' + pid);
            goPage(QStringLiteral("calendar"));
            cal->setView(QStringLiteral("season"));
        }
        pc->setTodayOverride(QStringLiteral("2033-06-15"));
        cal->setSeasonGroupClosed(QStringLiteral("东亚"), true);
        cal->setSeasonGroupClosed(QStringLiteral("东南亚"), false);
        waitFrames(win, 4);
        QQuickItem *seasonView = m.item(QStringLiteral("calSeasonView"));
        QQuickItem *ram0 = nullptr, *ram1 = nullptr;
        const bool two = waitFor([&] {
            ram0 = findPrefix(seasonView, QStringLiteral("calSeasonRam-"));
            if (!ram0) return false;
            const QString name = ram0->objectName();
            ram1 = findShown(seasonView, name.left(name.lastIndexOf(u'-')) + QStringLiteral("-1"));
            return ram1 != nullptr;
        }, 2000);
        r.check(two, QStringLiteral("季节表：2033 年一行画两段斋月"), QStringLiteral("页面 %1，视图 %2，第一段 %3，表 %4").arg(nav->page(), cal->view(), ram0 ? ram0->objectName() : QStringLiteral("没有"), seasonView ? QStringLiteral("有") : QStringLiteral("没有")));
        pc->setTodayOverride(QString());
        cal->setSeasonGroupClosed(QStringLiteral("东南亚"), false);
        cal->setAllSeasonGroups(false);
        cal->setView(calView0);
    }
    restore();
    nav->go(QStringLiteral("map"));
    goPage(QStringLiteral("plan"));
    pause(200);
    const auto noTime = [](Value v) { if (v.isObject()) { v.mutObject().remove(QStringLiteral("savedAt")); v.mutObject().remove(QStringLiteral("savedBy")); } return v; };
    r.check(noTime(store->data()) == noTime(doc0), QStringLiteral("补测之后数据和开始时一样"));
}

void runThumbs1000(Report &r, PhotoController *photos, const QString &dataDir)
{
    r.section(QStringLiteral("第一次生成 1,000 张缩略图（临时副本，只记数）"));
    QTemporaryDir tmp;
    QStringList files;
    QStringList src;
    for (const QVariant &v : photos->library()) {
        const QVariantMap m = v.toMap();
        if (!m.value(QStringLiteral("missing")).toBool() && !m.value(QStringLiteral("heic")).toBool())
            src << dataDir + u'/' + m.value(QStringLiteral("file")).toString();
    }
    if (!r.check(!src.isEmpty() && tmp.isValid(), QStringLiteral("有照片可以复制"))) return;
    qint64 bytes = 0;
    for (int i = 0; i < 1000; ++i) {
        const QString f = src[i % src.size()];
        const QString to = tmp.filePath(QStringLiteral("p%1%2").arg(i).arg(f.mid(f.lastIndexOf(u'.'))));
        if (QFile::copy(f, to)) { files << to; bytes += QFileInfo(to).size(); }
    }
    r.info(QStringLiteral("复制了 %1 张（来源 %2 张不同的文件，共 %3 MB）").arg(files.size()).arg(src.size()).arg(bytes / 1024 / 1024));
    const auto workingMb = [] { PROCESS_MEMORY_COUNTERS_EX c{}; GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&c), sizeof(c)); return std::pair<qint64, qint64>(c.WorkingSetSize / 1048576, c.PrivateUsage / 1048576); };
    const auto before = workingMb();
    data::ThumbCache cache(tmp.filePath(QStringLiteral("thumbs")));
    std::atomic<int> bad { 0 };
    QThreadPool pool;
    pool.setMaxThreadCount(2);   // 和程序里一样最多 2 张同时做
    QElapsedTimer t;
    t.start();
    for (const QString &f : files)
        pool.start([&cache, f, &bad] { if (cache.ensure(f, 320).isEmpty()) ++bad; });
    qint64 peakWs = 0, peakPriv = 0;
    waitFor([&] {
        const auto cur = workingMb();
        peakWs = std::max(peakWs, cur.first);
        peakPriv = std::max(peakPriv, cur.second);
        return pool.waitForDone(50);
    }, 590000);
    const qint64 ms = t.elapsed();
    r.check(bad == 0, QStringLiteral("1,000 张都生成了"), QStringLiteral("失败 %1").arg(bad.load()));
    r.info(QStringLiteral("用时 %1 秒，平均 %2 ms / 张；开始前工作集 %3 MB、私有 %4 MB，最高工作集 %5 MB、私有 %6 MB；缓存 %7 MB")
               .arg(ms / 1000.0, 0, 'f', 1).arg(files.isEmpty() ? 0 : ms / files.size()).arg(before.first).arg(before.second).arg(peakWs).arg(peakPriv).arg(cache.totalBytes() / 1048576));
}

} // namespace tp::app::selftest
