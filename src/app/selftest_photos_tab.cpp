// 自检：0.10.5 照片标签、看大图（含放大用原图的内存）、攻略正文和照片条点开、编辑器的照片和封面。分组名 planphotos。都在数据副本上做，最后把数据改回去。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "navhistory.h"
#include "photocontroller.h"
#include "plancontroller.h"
#include "planeditor.h"
#include "planpagecontroller.h"
#include "plankit.h"
#include "plans.h"
#include "procmem.h"

#include <QFileInfo>
#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QDateTime>
#include <QLineF>
#include <QUrl>
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
bool shown(QQuickItem *it) { return it && it->isVisible() && it->opacity() > 0.5; }
// 0.11.1 诊断"站点卡片的照片条"的间歇失败：记下按下时抓住鼠标的元素、和上一次点击隔多久多远
QString g_lastClick;
qint64 g_lastClickAt = 0;
QPointF g_lastClickPos;
void click(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return;
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    const QPointF g = win->mapToGlobal(p);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    QQuickItem *grab = win->mouseGrabberItem();
    g_lastClick = QStringLiteral("按下时抓住的是 %1；离上一次点击 %2 ms、%3 px").arg(grab ? (grab->objectName().isEmpty() ? QString::fromLatin1(grab->metaObject()->className()) : grab->objectName()) : QStringLiteral("（没有）"))
                      .arg(now - g_lastClickAt).arg(QLineF(p, g_lastClickPos).length(), 0, 'f', 0);
    g_lastClickAt = now;
    g_lastClickPos = p;
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    pause(80);
}
void key(QQuickWindow *win, int k)
{
    QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier), release(QEvent::KeyRelease, k, Qt::NoModifier);
    QCoreApplication::sendEvent(win, &press);
    QCoreApplication::sendEvent(win, &release);
    pause(30);
}
QString memText(const ProcMem &m) { return QStringLiteral("私有 %1 MB、显存 %2 MB").arg(m.privateMB, 0, 'f', 0).arg(m.gpuLocalMB, 0, 'f', 0); }
const Value *photoRec(const Value &root, const QString &id)
{
    for (const Value &ph : root[QStringLiteral("photos")].asArray())
        if (ph[QStringLiteral("id")].toString() == id) return &ph;
    return nullptr;
}
} // namespace

void runPlanPhotos(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav, PhotoController *photos)
{
    r.section(QStringLiteral("0.10.5 照片标签、看大图、编辑器的照片和封面"));
    PlanController *pc = plansCtl();
    data::DataStore *store = app->store();
    const auto item = [&](const QString &name) { return findItem(win->contentItem(), name); };
    QQuickItem *panel = item(QStringLiteral("planPage"));
    if (!r.check(pc && page && nav && panel && photos, QStringLiteral("计划页在"))) return;
    pc->setTestAllStatuses(false);
    const QVariantMap demo = pc->demo();
    const QString id = demo.value(QStringLiteral("planId")).toString();
    if (!r.check(!id.isEmpty(), QStringLiteral("有带日期的路线"))) return;
    pc->setTodayOverride(demo.value(QStringLiteral("today")).toString());
    const Value before = store->data();
    QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
    waitFor([&] { return page->planId() == id && panel->x() < win->width() - 10; }, 2000);
    page->seedPhotosForShot();
    QVariantList rows = page->info().value(QStringLiteral("photos")).toList();
    if (!r.check(rows.size() >= 3, QStringLiteral("这个计划（副本）有照片"), QString::number(rows.size()))) { store->modify([&](Value &d) { d = before; }); return; }

    // ---- 标签 ----
    QQuickItem *tabP = item(QStringLiteral("tripTab-photos")), *tabB = item(QStringLiteral("tripTab-budget")), *tabJ = item(QStringLiteral("tripTab-journal"));
    r.check(tabP && tabB && tabJ && tabB->x() < tabP->x() && tabP->x() < tabJ->x(), QStringLiteral("照片标签在消费和游记之间"));
    int tabs = 0;
    for (const char *t : { "itinerary", "bookings", "packing", "budget", "photos", "journal", "guide" }) if (item(QStringLiteral("tripTab-") + QLatin1String(t))) ++tabs;
    r.check(tabs == 7, QStringLiteral("一共 7 个标签"), QString::number(tabs));
    QMetaObject::invokeMethod(panel, "openTab", Q_ARG(QVariant, QStringLiteral("photos")));
    QQuickItem *tab = item(QStringLiteral("tripPhotosTab"));
    r.check(waitFor([&] { return shown(tab); }, 1000), QStringLiteral("照片标签打开了"));
    QQuickItem *count = findItem(tab, QStringLiteral("photoCount"));
    r.check(count && count->property("text").toString().contains(QString::number(rows.size())), QStringLiteral("写着张数"), count ? count->property("text").toString() : QString());
    r.check(findItem(tab, QStringLiteral("photoGroupTitle")) != nullptr, QStringLiteral("按站分组"));
    waitFor([&] { return findItem(tab, QStringLiteral("photoTile-0")) != nullptr; }, 1000);
    pause(150);   // 列表排好再点

    // ---- 精选、设封面 ----
    const auto tile0 = [&] { return findItem(tab, QStringLiteral("photoTile-0")); };   // 照片一变标签就重建，指针不能留
    const QString ph0 = rows[0].toMap().value(QStringLiteral("id")).toString();
    const QString file0 = rows[0].toMap().value(QStringLiteral("file")).toString();
    const bool favWas = photoRec(store->data(), ph0)->operator[](QStringLiteral("fav")).toBool();
    click(win, findItem(tile0(), QStringLiteral("photoFav")));
    r.check(waitFor([&] { return photoRec(store->data(), ph0)->operator[](QStringLiteral("fav")).toBool() != favWas; }, 1000), QStringLiteral("精选：点一下写进数据"));
    r.check(waitFor([&] { return count->property("text").toString().contains(QStringLiteral("精选")) != favWas; }, 1000) || favWas, QStringLiteral("精选数跟着变"), count->property("text").toString());
    page->info();
    click(win, findItem(tile0(), QStringLiteral("photoSetCover")));
    r.check(waitFor([&] { return (*plans::planById(store->data(), id))[QStringLiteral("cover")].toString() == ph0; }, 1000), QStringLiteral("设封面：写进数据"));
    r.check(waitFor([&] { return shown(findItem(tab, QStringLiteral("photoCover"))); }, 1000), QStringLiteral("封面区出现"));

    // ---- 写说明 ----
    pause(200);
    QQuickItem *capDlg = item(QStringLiteral("photoCaptionDialog"));
    click(win, findItem(tile0(), QStringLiteral("photoCaption")));
    r.check(waitFor([&] { return capDlg && capDlg->property("open").toBool(); }, 1000), QStringLiteral("写说明：弹出对话框"));
    if (capDlg) {
        QMetaObject::invokeMethod(capDlg, "accepted", Q_ARG(QString, QStringLiteral("海边的傍晚")));
        r.check(waitFor([&] { return (*photoRec(store->data(), ph0))[QStringLiteral("caption")].toString() == QStringLiteral("海边的傍晚"); }, 1000), QStringLiteral("说明写进数据"));
        waitFor([&] { return !capDlg->isVisible(); }, 1000);   // 淡出完才点得到下面
    }

    // ---- 看大图 ----
    QQuickItem *lb = item(QStringLiteral("lightbox"));
    if (!r.check(lb != nullptr, QStringLiteral("看大图组件在"))) { store->modify([&](Value &d) { d = before; }); return; }
    click(win, findItem(tile0(), QStringLiteral("photoOpen")));
    r.check(waitFor([&] { return lb->property("open").toBool() && lb->opacity() > 0.9; }, 1000), QStringLiteral("点照片打开看大图"));
    r.check(lb->width() == win->contentItem()->width() && lb->height() == win->contentItem()->height(), QStringLiteral("盖住整个窗口"));
    QQuickItem *fit = findItem(lb, QStringLiteral("lightboxFit")), *full = findItem(lb, QStringLiteral("lightboxFull"));
    const bool fitOk = waitFor([&] { return fit->property("status").toInt() == 1; }, 5000);   // Image.Ready
    r.check(fitOk, QStringLiteral("大图加载出来了"), fit->property("source").toString());
    QQuickItem *meta = findItem(lb, QStringLiteral("lightboxMeta")), *capT = findItem(lb, QStringLiteral("lightboxCaption"));
    const QString stopName = rows[0].toMap().value(QStringLiteral("stop")).toString();
    r.check(meta && meta->property("text").toString().contains(stopName), QStringLiteral("N2：写着站点"), meta ? meta->property("text").toString() : QString());
    r.check(meta && meta->property("text").toString().contains(QStringLiteral("第 ")) == !app->property("lang").toString().startsWith(u'e'), QStringLiteral("N2：路线写第几站"), meta ? meta->property("text").toString() : QString());
    const QString taken = (*photoRec(store->data(), ph0))[QStringLiteral("takenAt")].toString();
    if (!taken.isEmpty()) r.check(meta->property("text").toString().contains(taken.left(10)), QStringLiteral("N2：写着拍摄时间"), meta->property("text").toString());
    r.check(capT && capT->property("text").toString() == QStringLiteral("海边的傍晚"), QStringLiteral("显示说明"));
    QQuickItem *cnt = findItem(lb, QStringLiteral("lightboxCount"));
    r.check(cnt && cnt->property("text").toString() == QStringLiteral("1 / %1").arg(rows.size()), QStringLiteral("显示序号"), cnt ? cnt->property("text").toString() : QString());
    // 换一张、Esc（真按键事件；窗口没拿到焦点时直接调，差异清单 #22）
    win->requestActivate();
    key(win, Qt::Key_Right);
    const bool keyOk = lb->property("index").toInt() == 1;
    if (!keyOk) { r.info(QStringLiteral("→ 没收到（窗口没有焦点？），改直接调用")); QMetaObject::invokeMethod(lb, "step", Q_ARG(QVariant, 1)); }
    r.check(lb->property("index").toInt() == 1, QStringLiteral("→ 下一张"));
    click(win, findItem(lb, QStringLiteral("lightboxPrev")));
    r.check(lb->property("index").toInt() == 0, QStringLiteral("‹ 上一张"));
    QMetaObject::invokeMethod(lb, "step", Q_ARG(QVariant, -1));
    r.check(lb->property("index").toInt() == rows.size() - 1, QStringLiteral("第一张再往前到最后一张"));
    QMetaObject::invokeMethod(lb, "step", Q_ARG(QVariant, 1));

    // ---- N1 放大：滚轮、原图、百分比、拖动、双击还原、内存 ----
    waitFor([&] { return fit->property("status").toInt() == 1; }, 5000);
    pause(300);
    const ProcMem m0 = readProcMem();
    QQuickItem *stage = findItem(lb, QStringLiteral("lightboxStage")), *pic = findItem(lb, QStringLiteral("lightboxPic")), *pct = findItem(lb, QStringLiteral("lightboxPercent"));
    const QPointF c = stage->mapToScene(QPointF(stage->width() / 2 + 40, stage->height() / 2 + 20));
    const QString pct0 = pct->property("text").toString();
    const qreal picW0 = pic->width();
    for (int i = 0; i < 6; ++i) {
        QWindowSystemInterface::handleWheelEvent(win, c, win->mapToGlobal(c), QPoint(), QPoint(0, 120));
        pause(16);
    }
    r.check(waitFor([&] { return lb->property("zoom").toDouble() > 2; }, 1000), QStringLiteral("滚轮放大"), QString::number(lb->property("zoom").toDouble()));
    r.check(pic->width() > picW0 * 2 && pct->property("text").toString() != pct0, QStringLiteral("显示百分比"), pct0 + QStringLiteral(" → ") + pct->property("text").toString());
    const bool fullOk = waitFor([&] { return full->property("status").toInt() == 1; }, 15000);
    r.check(fullOk, QStringLiteral("放大时加载原图"), full->property("source").toString());
    pause(300);
    const ProcMem m1 = readProcMem();
    const QSize orig(int(full->implicitWidth()), int(full->implicitHeight()));
    r.info(QStringLiteral("看大图内存：平时 %1；放大用原图（%2×%3）%4；多占 私有 %5 MB、显存 %6 MB").arg(memText(m0)).arg(orig.width()).arg(orig.height()).arg(memText(m1))
               .arg(m1.privateMB - m0.privateMB, 0, 'f', 0).arg(m1.gpuLocalMB - m0.gpuLocalMB, 0, 'f', 0));
    // 百分比按原图算：不管库里记没记长宽，放大前后都对得上
    const auto pctNum = [&] { return pct->property("text").toString().chopped(1).toDouble(); };
    const qreal dpr = win->devicePixelRatio();
    r.check(orig.width() > 0 && qAbs(pct0.chopped(1).toDouble() - picW0 * dpr / orig.width() * 100) <= 1 && qAbs(pctNum() - pic->width() * dpr / orig.width() * 100) <= 1,
            QStringLiteral("百分比按原图算"), pct0 + QStringLiteral(" → ") + pct->property("text").toString());
    const qreal panX0 = lb->property("panX").toDouble();
    const QPointF g = win->mapToGlobal(c);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, c, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    const QPointF c2 = c + QPointF(-60, -30);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, c2, win->mapToGlobal(c2), Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, c2, win->mapToGlobal(c2), Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    r.check(qAbs(lb->property("panX").toDouble() - panX0) > 10, QStringLiteral("拖动"), QString::number(lb->property("panX").toDouble() - panX0));
    r.check(lb->property("open").toBool(), QStringLiteral("放大时拖动、点击不关"));
    pause(500);   // 和上一下隔开，不然算成双击的第二下不生效（差异清单 #26）
    click(win, stage);
    pause(60);
    click(win, stage);
    r.check(waitFor([&] { return lb->property("zoom").toDouble() == 1; }, 800), QStringLiteral("双击还原"), QString::number(lb->property("zoom").toDouble()));
    if (lb->property("zoom").toDouble() != 1) QMetaObject::invokeMethod(lb, "resetZoom");
    r.check(waitFor([&] { return full->property("source").toString().isEmpty(); }, 500), QStringLiteral("还原后放掉原图"));
    pause(600);
    const ProcMem m2 = readProcMem();
    r.info(QStringLiteral("还原后：%1").arg(memText(m2)));
    QMetaObject::invokeMethod(lb, "close");
    r.check(waitFor([&] { return !lb->isVisible(); }, 1000), QStringLiteral("关闭：回到原处"));
    r.check(shown(tab), QStringLiteral("关掉后还在照片标签"));

    // 点空白处关闭
    click(win, findItem(findItem(tab, QStringLiteral("photoTile-0")), QStringLiteral("photoOpen")));
    waitFor([&] { return lb->opacity() > 0.9; }, 1000);
    pause(500);   // 和打开那一下隔开
    {
        const QPointF blank(win->width() / 2.0, 20);
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, blank, win->mapToGlobal(blank), Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, blank, win->mapToGlobal(blank), Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    }
    r.check(waitFor([&] { return !lb->property("open").toBool(); }, 1000), QStringLiteral("点空白处关闭"));
    waitFor([&] { return !lb->isVisible(); }, 1000);
    // Esc 关闭
    click(win, findItem(findItem(tab, QStringLiteral("photoTile-0")), QStringLiteral("photoOpen")));
    waitFor([&] { return lb->opacity() > 0.9; }, 1000);
    key(win, Qt::Key_Escape);
    if (lb->property("open").toBool()) { r.info(QStringLiteral("Esc 没收到（窗口没有焦点？），改直接调用")); QMetaObject::invokeMethod(lb, "close"); }
    r.check(!lb->property("open").toBool(), QStringLiteral("Esc 关闭"));
    waitFor([&] { return !lb->isVisible(); }, 1000);

    // ---- 攻略正文和照片条 ----
    const QString pid = rows[1].toMap().value(QStringLiteral("id")).toString();
    page->setGuide(id, QStringLiteral("开头\n\n![正文说明](photo:%1)\n\n结尾").arg(pid));
    QMetaObject::invokeMethod(panel, "openTab", Q_ARG(QVariant, QStringLiteral("guide")));
    QQuickItem *guideTab = item(QStringLiteral("tripGuide"));
    waitFor([&] { return shown(guideTab) && findItem(guideTab, QStringLiteral("guidePhoto-1")) && findItem(guideTab, QStringLiteral("guidePhoto-1"))->isVisible(); }, 1500);
    pause(200);
    QQuickItem *gp = nullptr;
    for (QQuickItem *body = item(QStringLiteral("guideBody")); body && !gp; body = nullptr) gp = findItem(body, QStringLiteral("guidePhoto-1"));
    if (r.check(gp != nullptr, QStringLiteral("攻略正文里有照片"))) {
        click(win, gp);
        r.check(waitFor([&] { return lb->property("open").toBool(); }, 1000), QStringLiteral("攻略里点照片打开看大图"));
        r.check(capT->property("text").toString() == QStringLiteral("正文说明") && meta->property("text").toString().contains(rows[1].toMap().value(QStringLiteral("stop")).toString()), QStringLiteral("写正文的说明和站点"), capT->property("text").toString());
        QMetaObject::invokeMethod(lb, "close");
        waitFor([&] { return !lb->isVisible(); }, 1000);
    }
    QMetaObject::invokeMethod(panel, "openTab", Q_ARG(QVariant, QStringLiteral("itinerary")));
    QQuickItem *strip = nullptr;
    waitFor([&] { strip = item(QStringLiteral("tripPhoto-0")); return strip && strip->isVisible(); }, 1500);
    if (strip) {
        pause(200);
        QMetaObject::invokeMethod(panel, "reveal", Q_ARG(QVariant, 0), Q_ARG(QVariant, true));
        // 等滚动停下（固定等 500 ms 时全量里偶尔点空）
        QQuickItem *thumb = item(QStringLiteral("tripPhoto-0"));
        if (thumb) waitStill(thumb);
        pause(700);   // Windows差异清单 #26：和上一次点击隔开
        QString where;
        if (thumb) {
            const QPointF p = thumb->mapToScene(QPointF(thumb->width() / 2, thumb->height() / 2));
            QStringList path;
            for (QQuickItem *c = win->contentItem(); c;) {
                const QPointF q = c->mapFromScene(p);
                c = c->childAt(q.x(), q.y());
                if (c) path << (c->objectName().isEmpty() ? QString::fromLatin1(c->metaObject()->className()).left(18) : c->objectName());
            }
            where = QStringLiteral("点在 %1,%2 经过 %3；照片 %4 张").arg(p.x()).arg(p.y()).arg(path.join(QStringLiteral(" > "))).arg(page->property("info").toMap().value(QStringLiteral("photos")).toList().size());
        }
        click(win, thumb);
        const QString grabbed = g_lastClick;
        const bool stripOk = waitFor([&] { return lb->property("open").toBool(); }, 1000);
        r.check(stripOk, QStringLiteral("站点卡片的照片条点开看大图"), stripOk ? QString() : where + QStringLiteral("；") + grabbed);
        r.info(QStringLiteral("照片条这一下：") + where + QStringLiteral("；") + grabbed);
        QMetaObject::invokeMethod(lb, "close");
        waitFor([&] { return !lb->isVisible(); }, 1000);
    } else r.info(QStringLiteral("第一屏没有照片条，没测"));

    // ---- 移除：只从计划里拿掉，文件还在；封面跟着去掉；能撤销 ----
    QMetaObject::invokeMethod(panel, "openTab", Q_ARG(QVariant, QStringLiteral("photos")));
    waitFor([&] { return shown(tab); }, 1000);
    pause(300);
    QQuickItem *rmDlg = item(QStringLiteral("photoRemoveDialog"));
    click(win, findItem(findItem(tab, QStringLiteral("photoTile-0")), QStringLiteral("photoRemove")));
    r.check(waitFor([&] { return rmDlg && rmDlg->property("open").toBool(); }, 1000), QStringLiteral("移除：先问一句"));
    if (rmDlg) QMetaObject::invokeMethod(rmDlg, "chosen", Q_ARG(QString, QStringLiteral("ok")));
    r.check(waitFor([&] { return (*photoRec(store->data(), ph0))[QStringLiteral("plan")].toString().isEmpty(); }, 1000), QStringLiteral("移除：这张不再属于计划"));
    r.check((*photoRec(store->data(), ph0))[QStringLiteral("file")].toString() == file0 && QFileInfo::exists(store->dataDir() + u'/' + file0), QStringLiteral("移除：记录和文件都还在"), file0);
    r.check((*plans::planById(store->data(), id))[QStringLiteral("cover")].toString().isEmpty(), QStringLiteral("移除的是封面：封面去掉"));
    r.check(waitFor([&] { return page->info().value(QStringLiteral("photos")).toList().size() == rows.size() - 1; }, 1000), QStringLiteral("标签里少了一张"));
    app->undo();
    r.check(waitFor([&] { return (*photoRec(store->data(), ph0))[QStringLiteral("plan")].toString() == id; }, 1000), QStringLiteral("撤销：回来了"));
    r.check(photos->removeFromPlan(QStringLiteral("没有这个计划"), ph0) == false, QStringLiteral("不是这个计划的照片不动"));
    if (rmDlg) waitFor([&] { return !rmDlg->isVisible(); }, 1000);

    // ---- 添加照片：归到指定的站点；顺便量一张 1200 万像素的图放大时多占多少内存（开发数据的照片都只有 1080 宽） ----
    {
        QTemporaryDir tmp;
        QImage big(4000, 3000, QImage::Format_RGB32);
        big.fill(QColor(90, 120, 150));
        const QString bigFile = tmp.path() + QStringLiteral("/selftest-big.jpg");
        big.save(bigFile, "JPG", 90);
        const QString stopId = rows[1].toMap().value(QStringLiteral("stopId")).toString();
        QVariantMap fin;
        bool got = false;
        const auto conn = QObject::connect(photos, &PhotoController::importFinished, photos, [&](const QVariantMap &m) { fin = m; got = true; });
        const QVariantMap st = photos->startImport(QVariantList{ QUrl::fromLocalFile(bigFile) }, id, stopId);
        if (r.check(st.value(QStringLiteral("ok")).toBool() && waitFor([&] { return photos->importStage() == QLatin1String("review"); }, 20000), QStringLiteral("添加照片：出确认表"), st.value(QStringLiteral("error")).toString())) {
            photos->confirmImport();
            waitFor([&] { return got; }, 30000);
        }
        QObject::disconnect(conn);
        const QStringList ids = fin.value(QStringLiteral("ids")).toStringList();
        const Value *rec = ids.isEmpty() ? nullptr : photoRec(store->data(), ids.first());
        r.check(rec && (*rec)[QStringLiteral("plan")].toString() == id && (*rec)[QStringLiteral("stop")].toString() == stopId, QStringLiteral("添加照片：归到当前站点"));
        if (rec) {
            const QString bigRel = (*rec)[QStringLiteral("file")].toString();
            const QVariant bigList = QVariantList{ QVariantMap{ { QStringLiteral("file"), bigRel }, { QStringLiteral("width"), 4000 }, { QStringLiteral("height"), 3000 } } };
            QMetaObject::invokeMethod(lb, "show", Q_ARG(QVariant, bigList), Q_ARG(QVariant, 0));
            waitFor([&] { return fit->property("status").toInt() == 1 && lb->opacity() > 0.9; }, 8000);
            pause(300);
            const ProcMem b0 = readProcMem();
            for (int i = 0; i < 6; ++i) {
                QWindowSystemInterface::handleWheelEvent(win, c, win->mapToGlobal(c), QPoint(), QPoint(0, 120));
                pause(16);
            }
            const bool bigOk = waitFor([&] { return full->property("status").toInt() == 1; }, 15000);
            pause(300);
            const ProcMem b1 = readProcMem();
            r.check(bigOk, QStringLiteral("大图放大时读到原图"), pct->property("text").toString());
            r.info(QStringLiteral("看大图内存（4000×3000）：平时 %1；放大 %2；多占 私有 %3 MB、显存 %4 MB").arg(memText(b0)).arg(memText(b1))
                       .arg(b1.privateMB - b0.privateMB, 0, 'f', 0).arg(b1.gpuLocalMB - b0.gpuLocalMB, 0, 'f', 0));
            QMetaObject::invokeMethod(lb, "close");
            pause(600);
            const ProcMem b2 = readProcMem();
            r.info(QStringLiteral("关掉后：%1").arg(memText(b2)));
            // 再放大、关掉两次：内存不该一次比一次多
            waitFor([&] { return !lb->isVisible(); }, 1000);
            for (int round = 2; round <= 3; ++round) {
                QMetaObject::invokeMethod(lb, "show", Q_ARG(QVariant, bigList), Q_ARG(QVariant, 0));
                waitFor([&] { return fit->property("status").toInt() == 1 && lb->opacity() > 0.9; }, 8000);
                for (int i = 0; i < 6; ++i) {
                    QWindowSystemInterface::handleWheelEvent(win, c, win->mapToGlobal(c), QPoint(), QPoint(0, 120));
                    pause(16);
                }
                waitFor([&] { return full->property("status").toInt() == 1; }, 15000);
                pause(300);
                QMetaObject::invokeMethod(lb, "close");
                waitFor([&] { return !lb->isVisible(); }, 1000);
                pause(600);
                r.info(QStringLiteral("第 %1 次放大再关掉后：%2").arg(round).arg(memText(readProcMem())));
            }
            waitFor([&] { return !lb->isVisible(); }, 1000);
        }
    }
    // N2 的格式：有拍摄时间（开发数据多数没有）
    const QVariant n2List = QVariantList{ QVariantMap{ { QStringLiteral("stop"), QStringLiteral("兴义") }, { QStringLiteral("stopNo"), 13 }, { QStringLiteral("route"), true }, { QStringLiteral("takenAt"), QStringLiteral("2026-05-03T07:12:30") } } };
    QMetaObject::invokeMethod(lb, "show", Q_ARG(QVariant, n2List), Q_ARG(QVariant, 0));
    const QString n2 = meta->property("text").toString();
    r.check(n2.contains(QStringLiteral("13")) && n2.contains(QStringLiteral("兴义")) && n2.endsWith(QStringLiteral("2026-05-03 07:12")), QStringLiteral("N2：站点和拍摄时间的格式"), n2);
    QMetaObject::invokeMethod(lb, "close");
    waitFor([&] { return !lb->isVisible(); }, 1000);

    // ---- 编辑器：站点的照片、封面 ----
    PlanEditor *ed = editorCtl();
    QMetaObject::invokeMethod(win, "editPlan", Q_ARG(QVariant, id));
    QQuickItem *edPanel = item(QStringLiteral("planEditor"));
    r.check(waitFor([&] { return ed->isOpen() && shown(edPanel); }, 1500), QStringLiteral("编辑器打开了"));
    if (ed->isOpen()) {
        edPanel->setProperty("infoOpen", true);
        ed->setActiveStop(0);
        QQuickItem *ep = nullptr;
        waitFor([&] { ep = findItem(edPanel, QStringLiteral("edPhoto-0")); return ep != nullptr; }, 1500);
        r.check(ep != nullptr, QStringLiteral("编辑器：站点里列出照片"));
        r.check(findItem(edPanel, QStringLiteral("edPhotoAdd")) != nullptr, QStringLiteral("编辑器：能添加照片"));
        r.check(shown(findItem(edPanel, QStringLiteral("edCoverPick"))), QStringLiteral("编辑器：能选封面"));
        ed->setField(QStringLiteral("cover"), pid);
        QQuickItem *cover = findItem(edPanel, QStringLiteral("edCover"));
        r.check(waitFor([&] { return cover && cover->isVisible(); }, 1000), QStringLiteral("选了封面：显示缩略图"));
        r.check(waitFor([&] { return (*plans::planById(store->data(), id))[QStringLiteral("cover")].toString() == pid; }, 2000), QStringLiteral("封面存进数据（边改边存）"));
        ed->setField(QStringLiteral("cover"), QString());
        r.check(waitFor([&] { return (*plans::planById(store->data(), id))[QStringLiteral("cover")].toString().isEmpty(); }, 2000), QStringLiteral("去掉封面"));
        // 放弃这次的修改：编辑时改的照片也退回
        const QString capBefore = (*photoRec(store->data(), ph0))[QStringLiteral("caption")].toString();
        photos->setCaption(ph0, QStringLiteral("编辑时改的"));
        ed->discard();
        r.check((*photoRec(store->data(), ph0))[QStringLiteral("caption")].toString() == capBefore, QStringLiteral("放弃修改：照片的说明也退回"));
    }
    waitFor([&] { return !ed->isOpen(); }, 500);
    pause(800);

    pc->setTodayOverride(QString());
    store->modify([&](Value &d) { d = before; });
    app->noteChanged(false);   // 撤销的基准也跟上，不然后面的撤销会退回到还带着这里导入的照片的样子
    nav->go(QStringLiteral("plan"));
    pause(500);
}

} // namespace tp::app::selftest
