// 自检 0.8.9：站点编辑器、地名搜索、我的位置、地图控件、窄窗口（selftest.h 的说明）。
// 路线服务和地名服务用模拟的（不联网）；会在数据副本里新建 / 改 / 删计划，每一步都撤销回去。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "geocoder.h"
#include "locator.h"
#include "mapcontroller.h"
#include "netclient.h"
#include "planedit.h"
#include "planeditor.h"
#include "plancontroller.h"
#include "roadrouter.h"
#include "roads.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <qpa/qwindowsysteminterface.h>
#include <cmath>

namespace tp::app::selftest {

using json::Value;
namespace pe = tp::map::planedit;

namespace {
QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findItem(c, name)) return f;
    return nullptr;
}
QQuickItem *item(QQuickWindow *win, const char *name) { return findItem(win->contentItem(), QString::fromUtf8(name)); }
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
QString callStr(QObject *o, const char *fn)
{
    QVariant ret;
    QMetaObject::invokeMethod(o, fn, Q_RETURN_ARG(QVariant, ret));
    return ret.toString();
}
const Value *plan(data::DataStore *s, const QString &id) { return pe::findPlan(s->data(), id); }
// 一条 OSRM 成功的回复（三个点的折线）
QByteArray osrmOk(double alat, double alng, double blat, double blng)
{
    const QString path = tp::map::roads::encodePolyline({ { alat, alng }, { (alat + blat) / 2 + 0.01, (alng + blng) / 2 }, { blat, blng } });
    return QStringLiteral(R"({"code":"Ok","routes":[{"geometry":"%1","distance":52345.6,"duration":3725.4}]})").arg(path).toUtf8();
}
} // namespace

void runEditor(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, services::RoadRouter *roads)
{
    r.section(QStringLiteral("站点编辑器、地名搜索、我的位置、地图控件、窄窗口（0.8.9）"));
    PlanEditor *ed = editorCtl();
    Locator *loc = locatorCtl();
    services::Geocoder *geo = geocoderCtl();
    PlanController *pc = plansCtl();
    if (!r.check(ed && loc && geo && pc && roads, QStringLiteral("编辑器、定位、地名搜索在"))) return;
    data::DataStore *store = app->store();
    mapc->setBase(QStringLiteral("street"));
    mapc->markers()->clearSelection();
    ed->setDelays(150, 150);
    pause(300);

    // ---------- 新建计划：边改边存 ----------
    const int plans0 = int(store->data()[QStringLiteral("plans")].asArray().size());
    const int undo0 = app->undoCount();
    QMetaObject::invokeMethod(win, "newPlan");
    waitFor([ed] { return ed->isOpen() && ed->saveState() == QLatin1String("waiting"); }, 2000);
    r.check(ed->isOpen() && ed->isNew() && ed->saveState() == QLatin1String("waiting"), QStringLiteral("新建计划：编辑器打开，还没填好时先不存"),
            ed->saveState() + QStringLiteral(" · ") + ed->saveNote());
    QQuickItem *panel = item(win, "planEditor");
    waitFor([panel, win] { return panel && panel->isVisible() && panel->x() + panel->width() <= win->width() + 1; }, 2000);   // 滑进来的动画
    r.check(panel && panel->isVisible() && panel->x() + panel->width() <= win->width() + 1, QStringLiteral("编辑器面板滑进来了"),
            panel ? QStringLiteral("x %1 宽 %2").arg(panel->x()).arg(panel->width()) : QStringLiteral("没找到"));
    const QString newId = ed->planId();
    ed->setField(QStringLiteral("name"), QStringLiteral("自检新计划"));
    ed->addStop(QStringLiteral("自检 A"), 35.0116, 135.7681);
    waitFor([&] { return plan(store, newId) != nullptr && ed->saveState() != QLatin1String("waiting"); }, 2000);
    r.check(plan(store, newId) != nullptr && ed->saveState() != QLatin1String("waiting"), QStringLiteral("填好名称、加了一站就写进数据"),
            QStringLiteral("计划数 %1 → %2").arg(plans0).arg(store->data()[QStringLiteral("plans")].asArray().size()));
    ed->addStop(QStringLiteral("自检 B"), 34.6937, 135.5023);
    waitFor([ed] { return ed->working()[QStringLiteral("type")].toString() == QLatin1String("route"); }, 2000);
    r.check(ed->working()[QStringLiteral("type")].toString() == QLatin1String("route") && !ed->typeNote().isEmpty(),
            QStringLiteral("改进 g：加到第二站自动改成路线（提示一句）"), ed->typeNote());
    r.check(mapc->draftId() == newId && mapc->draftMarkers().size() == 2 && mapc->draftJson().contains(QStringLiteral("\"k\":\"s\"")),
            QStringLiteral("地图上画成草稿（两个可拖动的标记、虚线）"), QStringLiteral("标记 %1 个").arg(mapc->draftMarkers().size()));
    bool inNormal = false;
    for (const auto &s : mapc->markers()->stops()) if (s.planId == newId) inNormal = true;
    r.check(!inNormal && mapc->markers()->forceDim(), QStringLiteral("平时那一层不画它，其余计划变淡"));
    // 拖动标记松手（和 QML 里 DragHandler 松手调用的一样）
    ed->setStopLatLng(1, 34.70, 135.55);
    waitFor([&] {
        const Value *p = plan(store, newId);
        return !store->isDirty() && p && (*p)[QStringLiteral("stops")].asArray()[1][QStringLiteral("lng")].toNumber() == 135.55;
    }, 4000);
    const Value *np = plan(store, newId);
    r.check(np && (*np)[QStringLiteral("stops")].asArray()[1][QStringLiteral("lng")].toNumber() == 135.55 && !store->isDirty(),
            QStringLiteral("拖动后自动保存"), store->isDirty() ? QStringLiteral("还没存") : QString());
    // 编辑器开着时撤销被挡住
    const int undoMid = app->undoCount();
    app->undo();
    r.check(app->undoCount() == undoMid && plan(store, newId), QStringLiteral("编辑器开着时不撤销"));
    const QVariantMap fin = ed->finish();
    waitFor([&] { return !ed->isOpen() && mapc->draftId().isEmpty() && mapc->markers()->selectedPlan() == newId; }, 2000);
    r.check(fin.value(QStringLiteral("ok")).toBool() && !ed->isOpen() && mapc->draftId().isEmpty(), QStringLiteral("完成：关闭、草稿去掉"));
    r.check(app->undoCount() == undo0 + 1, QStringLiteral("整次编辑在撤销里只算一步"), QStringLiteral("撤销栈 %1 → %2").arg(undo0).arg(app->undoCount()));
    r.check(mapc->markers()->selectedPlan() == newId, QStringLiteral("完成后打开它的简易面板"), mapc->markers()->selectedPlan());
    app->undo();
    waitFor([&] { return !plan(store, newId); }, 2000);
    r.check(!plan(store, newId), QStringLiteral("撤销一步：新建的计划没了"));
    app->redo();
    waitFor([&] { return plan(store, newId) != nullptr; }, 2000);
    r.check(plan(store, newId) != nullptr, QStringLiteral("重做：回来了"));
    if (!plan(store, newId)) return;

    // ---------- 编辑已有计划：放弃这次的修改 ----------
    const QString oldName = (*plan(store, newId))[QStringLiteral("name")].toString();
    const int undo1 = app->undoCount();
    r.check(ed->openPlan(newId).isEmpty(), QStringLiteral("打开已有计划"));
    ed->setField(QStringLiteral("name"), QStringLiteral("改了又放弃"));
    ed->removeStop(0);
    waitFor([&] { const Value *v = plan(store, newId); return v && (*v)[QStringLiteral("name")].toString() == QStringLiteral("改了又放弃"); }, 2000);
    r.check((*plan(store, newId))[QStringLiteral("name")].toString() == QStringLiteral("改了又放弃"), QStringLiteral("改名边改边存"),
            (*plan(store, newId))[QStringLiteral("name")].toString() + QStringLiteral(" · ") + ed->property("saveState").toString() + QStringLiteral(" · ") + ed->property("saveNote").toString());
    ed->discard();
    waitFor([ed] { return !ed->isOpen(); }, 2000);
    const Value *rp = plan(store, newId);
    r.check(rp && (*rp)[QStringLiteral("name")].toString() == oldName && (*rp)[QStringLiteral("stops")].asArray().size() == 2 && !store->isDirty(),
            QStringLiteral("放弃：恢复成打开时的样子并保存"));
    r.check(app->undoCount() == undo1, QStringLiteral("放弃后撤销栈不多一步"), QStringLiteral("%1 → %2").arg(undo1).arg(app->undoCount()));

    // ---------- 问题：名称空先不存 ----------
    ed->openPlan(newId);
    ed->setField(QStringLiteral("name"), QString());
    waitFor([ed] { return ed->saveState() == QLatin1String("invalid"); }, 2000);
    r.check(ed->saveState() == QLatin1String("invalid") && (*plan(store, newId))[QStringLiteral("name")].toString() == oldName,
            QStringLiteral("名称空：先不存，说明原因"), ed->saveNote());
    const QVariantMap f2 = ed->finish();
    r.check(!f2.value(QStringLiteral("ok")).toBool() && ed->isOpen(), QStringLiteral("有问题时\"完成\"不关，问要不要放弃"), f2.value(QStringLiteral("reason")).toString());
    ed->setField(QStringLiteral("name"), oldName);

    // ---------- 编辑时自动算单段（模拟的路线服务） ----------
    int fetches = 0;
    QString lastUrl;
    bool noRoute = false;
    ed->setFetchForTest([&](const QUrl &url, std::function<void(int, const QByteArray &, int)> done) {
        ++fetches;
        lastUrl = url.toString();
        const json::Array &st = ed->working()[QStringLiteral("stops")].asArray();
        const QByteArray body = noRoute ? QByteArray(R"({"code":"NoRoute","message":"x"})")
                                        : osrmOk(st[0][QStringLiteral("lat")].toNumber(), st[0][QStringLiteral("lng")].toNumber(),
                                                 st[1][QStringLiteral("lat")].toNumber(), st[1][QStringLiteral("lng")].toNumber());
        const bool nr = noRoute;
        QTimer::singleShot(50, [done, body, nr] { done(nr ? 400 : 200, body, nr ? int(services::NetClient::Error::Http) : 0); });
    });
    ed->setLegMode(0, QStringLiteral("drive"));
    waitFor([&] { return fetches > 0 && !ed->legBusy() && ed->legsPending() == 0; }, 5000);
    auto rows = pe::legRows(ed->working());
    r.check(fetches == 1 && !rows.isEmpty() && rows[0].computed && lastUrl.contains(QStringLiteral("routed-car")),
            QStringLiteral("改成自驾后停手一会儿自动按道路算这一段"), QStringLiteral("请求 %1 次 %2").arg(fetches).arg(lastUrl.left(60)));
    pause(500);   // 算好的这一段由编辑器的自动保存写进数据（看不到单独的状态，留时间）
    const Value *lp = plan(store, newId);
    r.check(lp && !(*lp)[QStringLiteral("stops")].asArray()[0][QStringLiteral("leg")][QStringLiteral("path")].toString().isEmpty(),
            QStringLiteral("算好的折线写进数据"));
    // 挪站点 → key 变了 → 重新算；这次路线服务说找不到路 → 记住、显示原因
    noRoute = true;
    ed->setStopLatLng(1, 34.80, 135.60);
    waitFor([&] { return fetches >= 2 && !ed->legBusy(); }, 5000);
    pause(200);   // 同上
    const QVariantList vs = ed->plan().value(QStringLiteral("stops")).toList();
    const QVariantMap leg0 = vs.isEmpty() ? QVariantMap() : vs[0].toMap().value(QStringLiteral("leg")).toMap();
    const QString key = pe::legRows(ed->working()).value(0).key;
    r.check(leg0.value(QStringLiteral("state")).toString() == QLatin1String("fail") && roads->remembered(key),
            QStringLiteral("找不到路：显示原因、记住不再自动算"), leg0.value(QStringLiteral("msg")).toString());
    noRoute = false;
    ed->retryLeg(0);
    waitFor([&] { return fetches >= 3 && !ed->legBusy() && ed->legsPending() == 0; }, 5000);
    r.check(!roads->remembered(key) && pe::legRows(ed->working()).value(0).computed, QStringLiteral("重试：忘掉失败、重新算好"));
    const QVariantMap st = ed->stats();
    r.check(st.value(QStringLiteral("km")).toDouble() > 50 && st.value(QStringLiteral("driveSec")).toDouble() > 3000 && !st.value(QStringLiteral("approx")).toBool(),
            QStringLiteral("顶上的总里程用算好的道路距离"),
            QStringLiteral("%1 km，自驾 %2 秒").arg(st.value(QStringLiteral("km")).toDouble(), 0, 'f', 1).arg(st.value(QStringLiteral("driveSec")).toDouble()));
    bool blockedMsg = false;
    const auto conn = QObject::connect(roads, &services::RoadRouter::message, [&](const QString &, bool) { blockedMsg = true; });
    roads->start();
    QObject::disconnect(conn);
    r.check(blockedMsg && !roads->running(), QStringLiteral("编辑器开着时不批量计算"));
    ed->setFetchForTest({});

    // ---------- 地名搜索（模拟的服务）、点选反查 ----------
    int geoCalls = 0;
    geo->setFetchForTest([&](const QUrl &url, std::function<void(int, const QByteArray &, int)> done) {
        ++geoCalls;
        const bool rev = url.path().endsWith(QStringLiteral("reverse"));
        const QByteArray body = rev ? QByteArray(R"({"name":"Fushimi Inari","display_name":"Fushimi Inari, Kyoto"})")
                                    : QByteArray(R"([{"name":"Nara","display_name":"Nara, Nara Prefecture, Japan","lat":"34.6851","lon":"135.8048"},
                                                    {"name":"Nara","display_name":"Nara, Mali","lat":"15.17","lon":"-7.29"}])");
        QTimer::singleShot(30, [done, body] { done(200, body, 0); });
    });
    const QString q = QStringLiteral("自检 Nara %1").arg(QDateTime::currentMSecsSinceEpoch());   // 每次不同：不命中上次运行的缓存
    ed->search(q);
    waitFor([&] { return ed->searchState() != QLatin1String("busy"); }, 4000);
    r.check(ed->searchState() == QLatin1String("done") && ed->results().size() == 2, QStringLiteral("搜地名：结果列出来"), ed->searchState());
    waitFrames(win, 2);
    QQuickItem *res = item(win, "edResults");
    r.check(res && res->isVisible(), QStringLiteral("结果列表显示（带署名）"));
    const int nStops = int(ed->working()[QStringLiteral("stops")].asArray().size());
    // 0.12.2：真点结果（点了这一行就被销毁，以前接着清搜索框会报 searchField is not defined）
    QQuickItem *search = item(win, "edSearch");
    if (search) search->setProperty("value", QStringLiteral("Nara"));
    if (QQuickItem *hit = item(win, "edResultHit-0")) click(win, hit); else ed->addResult(0);
    pause(200);
    r.check(search && search->property("text").toString().isEmpty(), QStringLiteral("点搜索结果后清空搜索框"), search ? search->property("text").toString() : QString());
    if (search) search->setProperty("value", QString());
    {
        const json::Array &sa = ed->working()[QStringLiteral("stops")].asArray();
        r.check(int(sa.size()) == nStops + 1 && sa.back()[QStringLiteral("name")].toString() == QLatin1String("Nara") && ed->searchState() == QLatin1String("idle"),
                QStringLiteral("点一条结果加成站点"));
    }
    const int callsBefore = geoCalls;
    ed->search(q);
    r.check(geoCalls == callsBefore && ed->searchState() == QLatin1String("done"), QStringLiteral("同一个词第二次用缓存，不再请求（条款要求）"));
    ed->clearSearch();
    ed->setPickMode(true);
    ed->pickAt(34.9671 + (QDateTime::currentMSecsSinceEpoch() % 1000) * 1e-5, 135.7727);
    waitFor([&] { return ed->working()[QStringLiteral("stops")].asArray().back()[QStringLiteral("name")].toString() == QLatin1String("Fushimi Inari"); }, 4000);
    r.check(ed->working()[QStringLiteral("stops")].asArray().back()[QStringLiteral("name")].toString() == QLatin1String("Fushimi Inari"),
            QStringLiteral("点选：加一站、反查出地名"));
    const QString e1 = callStr(win, "escOnce");
    r.check(e1 == QLatin1String("pick") && !ed->pickMode() && ed->isOpen(), QStringLiteral("Esc 先退出点选，编辑器还开着"), e1);
    geo->setFetchForTest({});

    // ---------- 窄窗口：编辑器变成底部抽屉 ----------
    // 0.8.10：断点从 768 改成 520（PO 选 ⑥ A）：700 宽时还在右边，500 宽时是抽屉
    // 0.9.1：Windows 上窗口改宽度要等系统、面板位置又有动画，固定等 700 毫秒不够（0.9.0 在 PO 电脑上
    // 700 宽时面板还在从 1400 宽的位置滑过来）。改成等窗口真的变成目标宽度、面板连续 3 次取样（约 30 毫秒一次）
    // 不动了再判断，最多 3 秒。
    const auto settleTo = [&](int w) {
        win->setWidth(w);
        QRectF last(-1, -1, -1, -1);
        int still = 0;
        waitFor([&] {
            if (win->width() != w || !panel) { still = 0; return false; }
            const QRectF now(panel->x(), panel->y(), panel->width(), panel->height());
            still = (now == last) ? still + 1 : 0;
            last = now;
            return still >= 3;
        }, 3000);
    };
    const int w0 = win->width();
    settleTo(700);
    const bool side700 = panel && panel->width() < win->width() - 1 && panel->x() + panel->width() <= win->width() + 1;
    const QString at700 = panel ? QStringLiteral("700 宽：x %1 宽 %2").arg(panel->x()).arg(panel->width()) : QString();
    settleTo(500);
    r.check(side700 && panel && std::abs(panel->width() - win->width()) < 1 && panel->y() > win->height() * 0.3,
            QStringLiteral("编辑器：700 宽时还在右边，≤ 520 变成底部抽屉"),
            panel ? at700 + QStringLiteral("；500 宽：宽 %1 y %2（窗口 %3×%4）").arg(panel->width()).arg(panel->y()).arg(win->width()).arg(win->height()) : QString());
    win->setWidth(w0);
    waitFor([win, w0] { return win->width() == w0; }, 2000);
    waitFrames(win, 2);

    // ---------- 删除计划（编辑器里），撤销回来 ----------
    const QString nm = ed->deletePlan();
    waitFor([&] { return !ed->isOpen() && !plan(store, newId); }, 2000);
    r.check(!ed->isOpen() && !plan(store, newId), QStringLiteral("编辑器里删除计划"), nm);
    app->undo();
    waitFor([&] { return plan(store, newId) != nullptr; }, 2000);
    r.check(plan(store, newId) != nullptr, QStringLiteral("撤销删除：回来了"));
    app->undo();   // 再撤销一次 = 回到新建之前
    waitFor([&] { return !plan(store, newId); }, 2000);
    r.check(!plan(store, newId) && int(store->data()[QStringLiteral("plans")].asArray().size()) == plans0, QStringLiteral("自检新建的计划撤销干净了"));

    // ---------- 我的位置 ----------
    r.info(QStringLiteral("系统定位来源：%1").arg(Locator::sources().isEmpty() ? QStringLiteral("（没有）") : Locator::sources().join(QStringLiteral("、"))));
    r.info(QStringLiteral("所在国家（settings.json 没设时按 Windows 区域设置猜）：%1").arg(loc->homeCountry()));
    mapc->markers()->clearSelection();
    pause(300);
    loc->simulateFix(-33.8688, 151.2093, 3000);
    waitFor([&] { return !mapc->moving(); }, 4000);
    r.check(loc->hasFix() && std::abs(mapc->lat() - -33.8688) < 0.5 && mapc->zoom() >= 9.9, QStringLiteral("拿到位置：蓝点 + 飞过去（至少 10 级）"),
            QStringLiteral("%1, %2 @ %3").arg(mapc->lat(), 0, 'f', 3).arg(mapc->lng(), 0, 'f', 3).arg(mapc->zoom(), 0, 'f', 1));
    QQuickItem *dot = item(win, "meDot");
    r.check(dot && dot->isVisible(), QStringLiteral("蓝点显示出来"));
    QString kind;
    const auto c2 = QObject::connect(loc, &Locator::result, [&](const QString &k) { kind = k; });
    loc->simulateError(QStringLiteral("access"));
    QObject::disconnect(c2);
    waitFor([&] { return !mapc->moving(); }, 4000);
    r.check(!loc->hasFix() && (kind == QLatin1String("country") || kind == QLatin1String("today")), QStringLiteral("拿不到系统定位：退回今天所在的站点或所在国家"), kind);
    QQuickItem *lb = item(win, "locBtn");
    r.check(lb && lb->isVisible(), QStringLiteral("右下角有我的位置按钮"));
    // 真的问一次系统定位（不判对错：台式机、云端常常拿不到，报告里写结果）
    QString realKind;
    const auto c3 = QObject::connect(loc, &Locator::result, [&](const QString &k) { realKind = k; });
    loc->locate();
    waitFor([&] { return !loc->busy(); }, 9000);
    QObject::disconnect(c3);
    r.info(QStringLiteral("真的问系统定位：%1").arg(realKind == QLatin1String("fix") ? QStringLiteral("拿到了（精度 %1 米）").arg(loc->accuracy(), 0, 'f', 0)
                                                                                  : QStringLiteral("没拿到，退回 ") + realKind));
    loc->clearFix();

    // ---------- 比例尺、± ----------
    mapc->jumpTo(35, 135, 6);
    waitMapIdle(win, mapc);
    const double w6 = mapc->scaleWidth();
    const QString l6 = mapc->scaleLabel();
    r.check(w6 > 0 && w6 <= 100 && l6.endsWith(QStringLiteral(" km")), QStringLiteral("比例尺（公制，最长 100 px）"), QStringLiteral("%1 px %2").arg(w6, 0, 'f', 1).arg(l6));
    mapc->jumpTo(35, 135, 16);
    waitMapIdle(win, mapc);
    r.check(mapc->scaleLabel().endsWith(QStringLiteral(" m")), QStringLiteral("放大后单位变成米"), mapc->scaleLabel());
    mapc->jumpTo(35, 135, 6);
    waitMapIdle(win, mapc);
    QQuickItem *zin = item(win, "zoomIn");
    click(win, zin);
    waitFor([&] { return mapc->moving() || std::abs(mapc->zoom() - 7) < 0.05; }, 1000);
    waitFor([&] { return !mapc->moving(); }, 2000);
    r.check(zin && std::abs(mapc->zoom() - 7) < 0.05, QStringLiteral("± 放大一级"),
            QStringLiteral("%1").arg(mapc->zoom(), 0, 'f', 2) + (std::abs(mapc->zoom() - 7) < 0.05 || !zin ? QString()
                : QStringLiteral("；点在窗口 %1,%2（窗口 %3,%4 %5×%6）").arg(zin->mapToScene(QPointF(zin->width() / 2, zin->height() / 2)).x())
                      .arg(zin->mapToScene(QPointF(zin->width() / 2, zin->height() / 2)).y()).arg(win->x()).arg(win->y()).arg(win->width()).arg(win->height())));
    // 0.9.7：没有图例了（左下胶囊去重复），比例尺直接在胶囊上面
    QQuickItem *sb = item(win, "scaleBar"), *pill = item(win, "statusPill");
    r.check(sb && sb->isVisible() && !item(win, "legend") && pill && pill->isVisible() && sb->y() + sb->height() <= pill->y(), QStringLiteral("左下：比例尺在胶囊上面（没有图例）"));
    mapc->fitAll(false);
}

} // namespace tp::app::selftest
