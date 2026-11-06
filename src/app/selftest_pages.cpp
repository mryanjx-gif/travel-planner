// 自检：0.8.10 页面结构——⋯ 里 8 颗泡泡、地图的四种模式（按状态过滤路线、换底图、每种模式记住自己的"也显示"和底图）、
// 左下胶囊和列表标题跟着模式变、Ctrl 1–4、整页占位（日历 / 游记 / 账本）、前进 / 后退 / Esc 按真的模式和页面走、
// Ctrl K 搜旧页面名、窄窗口时 ⋯ 排两行。
// 会改数据副本里的"下次出发"（⋯ 泡泡要有"下次出发"那一颗），最后改回去；模式和底图的记忆不写进 settings.json（截图 / 自检不存）。
#include "selftest_util.h"
#include "perf.h"

#include "appcontroller.h"
#include "datastore.h"
#include "mapcontroller.h"
#include "navhistory.h"
#include "plancontroller.h"
#include "plans.h"
#include "settings.h"
#include "translator.h"

#include <QGuiApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <qpa/qwindowsysteminterface.h>

namespace tp::app::selftest {

using json::Value;

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
bool shown(QQuickItem *it) { return it && it->isVisible() && it->opacity() > 0.5; }
bool g_focus = false;
void key(QQuickWindow *win, int k, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(win, QEvent::KeyPress, k, mods);
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(win, QEvent::KeyRelease, k, mods);
    pause(80);
}
void click(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return;
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    const QPointF g = win->mapToGlobal(p);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    pause(80);
}
QString escOnce(QQuickWindow *win)
{
    QVariant ret;
    QMetaObject::invokeMethod(win, "escOnce", Q_RETURN_ARG(QVariant, ret));
    return ret.toString();
}
void goPage(QQuickWindow *win, const QString &p) { QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, p)); }
// 地图上画了哪些状态
QSet<QString> statusesOnMap(map::MapController *mapc)
{
    QSet<QString> s;
    for (const map::TripPlan &p : mapc->trips().plans()) s.insert(p.status.isEmpty() ? QStringLiteral("planning") : p.status);
    return s;
}
QString join(const QSet<QString> &s) { QStringList l(s.begin(), s.end()); l.sort(); return l.join(u','); }
// 换模式有过渡（盖一帧再换）：等模式真的换过去、盖着的那一帧淡掉
bool waitMode(PlanController *pc, QQuickWindow *win, const QString &m)
{
    const bool ok = waitFor([pc, m] { return pc->mode() == m; }, 2000);
    QQuickItem *cover = win->findChild<QQuickItem *>(QStringLiteral("baseCover"));
    waitFor([cover] { return !cover || !cover->isVisible(); }, 2000);
    return ok;
}
} // namespace

void runPages(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("页面结构：⋯ 的 8 颗泡泡、地图的四种模式、整页占位（0.8.10）"));
    PlanController *pc = plansCtl();
    QQmlContext *ctx = QQmlEngine::contextForObject(win);
    NavHistory *nav = ctx ? qobject_cast<NavHistory *>(ctx->contextProperty(QStringLiteral("nav")).value<QObject *>()) : nullptr;
    if (!r.check(pc && nav, QStringLiteral("计划控制器、页面记录在"))) return;
    data::DataStore *store = app->store();
    mapc->markers()->clearSelection();
    mapc->setBase(QStringLiteral("street"));
    win->setProperty("listOpen", false);
    win->setProperty("nowOpen", false);
    nav->resetTo(QStringLiteral("plan"));
    pc->setMode(QStringLiteral("plan"));
    pc->setTestAllStatuses(false);   // 0.9.7：这一节看每种模式自己的样子
    pc->setOverlayForTest(QStringLiteral("plan"), false);
    pc->setOverlayForTest(QStringLiteral("wish"), false);
    pc->setOverlayForTest(QStringLiteral("foot"), false);
    pc->filterAll();
    pc->showAllHidden();
    waitMapIdle(win, mapc);
    win->requestActivate();
    g_focus = waitFor([win] { return QGuiApplication::focusWindow() == win; }, 2000);

    // 数据里各状态有几个计划（有坐标的）
    QHash<QString, int> byStatus;
    for (const Value &p : store->data()[QStringLiteral("plans")].asArray()) {
        bool has = false;
        for (const Value &s : p[QStringLiteral("stops")].asArray()) has = has || (s[QStringLiteral("lat")].isNumber() && s[QStringLiteral("lng")].isNumber());
        if (has) byStatus[map::plans::statusOf(p)]++;
    }
    r.info(QStringLiteral("有坐标的计划：计划中 %1、想去 %2、已完成 %3").arg(byStatus.value(QStringLiteral("planning")))
               .arg(byStatus.value(QStringLiteral("wish"))).arg(byStatus.value(QStringLiteral("completed"))));

    // ---- 计划模式：只画计划中 ----
    {
        const QSet<QString> s = statusesOnMap(mapc);
        r.check(pc->mode() == QLatin1String("plan") && s == QSet<QString>{ QStringLiteral("planning") }
                    && int(mapc->trips().plans().size()) == byStatus.value(QStringLiteral("planning")),
                QStringLiteral("计划模式：地图上只画计划中的路线"), QStringLiteral("状态 %1，%2 个").arg(join(s)).arg(mapc->trips().plans().size()));
        // 0.9.7（PO 15:15 选方案 A）：左下只有"筛选"，没有"也显示"和图例；"叠上其他旅程"在筛选弹窗最下面
        QQuickItem *pill = item(win, "statusPill");
        r.check(shown(pill) && !item(win, "status-completed") && !item(win, "status-wish") && !item(win, "pillNote") && !item(win, "legend")
                    && shown(item(win, "filterButton")) && !shown(item(win, "overlayDot")) && pill->x() < win->width() / 2.0,
                QStringLiteral("左下胶囊只有\"筛选\"（没有\"也显示\"、图例）"));
        QQuickItem *fb = item(win, "filterButton");
        QQuickItem *menu = item(win, "filterMenu");
        waitStill(pill, 1500);   // 上面关了列表 / 此刻卡片，胶囊正往左滑（0.25 秒）；滑到位再点，不然点空
        click(win, fb);
        waitFor([menu] { return menu && menu->property("open").toBool() && menu->opacity() > 0.95; }, 1500);
        QQuickItem *ov = item(win, "overlayCheck");
        const bool ovShown = shown(ov);
        int others = 0;
        for (auto it = byStatus.constBegin(); it != byStatus.constEnd(); ++it) if (it.key() != QLatin1String("planning")) others += it.value();
        if (ov) click(win, ov);
        waitFor([pc, mapc] { return pc->overlay() && !mapc->overlayTrips().plans().isEmpty(); }, 1500);
        const int ovN = int(mapc->overlayTrips().plans().size());
        const QSet<QString> sOn = statusesOnMap(mapc);
        const bool dot = waitFor([win] { return shown(item(win, "overlayDot")); }, 1000);
        const int badge = pc->excludedCount();
        pause(700);
        if (ov) click(win, ov);
        waitFor([pc, mapc] { return !pc->overlay() && mapc->overlayTrips().plans().isEmpty(); }, 1500);
        r.check(ovShown && ovN == others && sOn == QSet<QString>{ QStringLiteral("planning") } && dot && badge == 0 && !pc->overlay()
                    && mapc->overlayTrips().plans().isEmpty() && !shown(item(win, "overlayDot")),
                QStringLiteral("筛选弹窗里\"叠上其他旅程\"：其他状态的旅程画成淡灰细线垫在下面（正常的还是只有计划中），按钮上出小圆点、不算进角标；再点去掉"),
                QStringLiteral("开关%1；叠上 %2 个（应 %3），正常画的 %4，圆点 %5，角标 %6").arg(ovShown ? QStringLiteral("在") : QStringLiteral("不在"))
                    .arg(ovN).arg(others).arg(join(sOn)).arg(dot).arg(badge));
        escOnce(win);
        waitFor([menu] { return menu && !menu->property("open").toBool(); }, 1000);
        pc->setOverlayForTest(QStringLiteral("plan"), false);   // 不管上面过没过，后面几项从"不叠"开始
    }

    // ---- ⋯ 泡泡：8 颗，点"足迹" ----
    const QString oldNext = store->data()[QStringLiteral("nextTripId")].toString();
    const QVariantMap demo = pc->demo();
    if (!demo.isEmpty()) pc->setNextTrip(demo.value(QStringLiteral("planId")).toString(), true);
    waitFrames(win, 2);
    QQuickItem *bubbles = item(win, "pageBubbles");
    {
        click(win, item(win, "dotsPill"));
        waitFor([bubbles] { return bubbles && bubbles->property("open").toBool(); }, 1000);
        QQuickItem *last = item(win, demo.isEmpty() ? "bud-ledger" : "bud-next");
        waitFor([last] { return last && last->opacity() > 0.95; }, 1500);
        QStringList keys;
        double prevRight = -1;
        bool inOrder = true;
        for (const char *k : { "bud-plan", "bud-wish", "bud-foot", "bud-photo", "bud-calendar", "bud-journal", "bud-ledger", "bud-next" }) {
            QQuickItem *b = item(win, k);
            if (!shown(b)) continue;
            keys << QString::fromLatin1(k + 4);
            const double left = b->mapToScene(QPointF(0, 0)).x();
            if (left < prevRight) inOrder = false;
            prevRight = left + b->width();
        }
        const int want = demo.isEmpty() ? 7 : 8;
        r.check(keys.size() == want && inOrder, QStringLiteral("⋯：%1 颗泡泡，从左到右 计划 想去 足迹 照片 ｜ 日历 游记 账本 ｜ 下次出发").arg(want), keys.join(u' '));
        click(win, item(win, "bud-foot"));
        const bool ok = waitMode(pc, win, QStringLiteral("foot"));
        const QSet<QString> s = statusesOnMap(mapc);
        // 0.9.2：底图不跟着换（PO 07:21：只有在图层里点已解锁才显示已解锁；PO 选 A 四种模式共用一个底图）；换模式不截帧盖住
        QQuickItem *cover = item(win, "baseCover");
        r.check(ok && nav->page() == QLatin1String("foot") && s == QSet<QString>{ QStringLiteral("completed") } && mapc->base() == QLatin1String("street")
                    && !(cover && cover->property("active").toBool()),
                QStringLiteral("点\"足迹\"：只画已完成的路线，底图还是街道图，不盖帧"),
                QStringLiteral("页面 %1，状态 %2，底图 %3").arg(nav->page(), join(s), mapc->base()));
        QQuickItem *label = item(win, "dotsLabel");
        r.check(shown(label) && label->property("text").toString() == tp::tr("足迹"), QStringLiteral("⋯ 上显示\"足迹\""));
        r.check(!item(win, "pillNote") && pc->overlayAvailable() && !shown(item(win, "unlockPanel")),
                QStringLiteral("足迹模式：左下只有\"筛选\"，筛选里有\"叠上其他旅程\"；足迹框跟着已解锁底图，这时不出现"));
        QQuickItem *back = item(win, "backBtn");
        r.check(!shown(back), QStringLiteral("模式里不出现 ↩（还在地图上）"));
    }
    // 列表：足迹按年份
    {
        win->setProperty("listOpen", true);
        waitStill(item(win, "planList"), 1500);
        QQuickItem *title = item(win, "listTitle");
        const QVariantList rows = pc->list().value(QStringLiteral("rows")).toList();
        bool years = !rows.isEmpty(), allDone = true;
        QString prevYear = QStringLiteral("9999");
        for (const QVariant &v : rows) {
            const QVariantMap m = v.toMap();
            const QString g = m.value(QStringLiteral("group")).toString();
            allDone = allDone && m.value(QStringLiteral("status")).toString() == QLatin1String("completed");
            if (g == QLatin1String("undated")) { prevYear = QString(); continue; }
            if (!g.startsWith(u'y') || prevYear.isNull() || g.mid(1) > prevYear) years = false;
            prevYear = g.mid(1);
        }
        r.check(shown(title) && title->property("text").toString().contains(QString::number(rows.size())) && years && allDone,
                QStringLiteral("足迹的列表：\"去过的 · N 趟\"，按年份从新到旧分组，没有日期的放最后"),
                QStringLiteral("%1；%2 行").arg(title ? title->property("text").toString() : QString()).arg(rows.size()));
        win->setProperty("listOpen", false);
    }
    // 0.9.2（PO 选 A）：四种模式共用一个底图：足迹里换成卫星图 → 回计划还是卫星图 → 再进足迹也是卫星图
    {
        QMetaObject::invokeMethod(win, "pickBase", Q_ARG(QVariant, QStringLiteral("sat")));
        waitFor([mapc] { return mapc->base() == QLatin1String("sat"); }, 2000);
        const QString e = escOnce(win);
        const bool toPlan = waitMode(pc, win, QStringLiteral("plan"));
        const QString planBase = mapc->base();
        if (g_focus) key(win, Qt::Key_3, Qt::ControlModifier); else goPage(win, QStringLiteral("foot"));
        const bool toFoot = waitMode(pc, win, QStringLiteral("foot"));
        r.check(e == QLatin1String("page") && toPlan && planBase == QLatin1String("sat") && toFoot && mapc->base() == QLatin1String("sat"),
                QStringLiteral("四种模式共用一个底图：足迹里换成卫星图，Esc 回计划还是卫星图，Ctrl 3 再进足迹也是"),
                QStringLiteral("Esc → %1，计划的底图 %2，足迹的底图 %3").arg(e, planBase, mapc->base()));
        QMetaObject::invokeMethod(win, "pickBase", Q_ARG(QVariant, QStringLiteral("street")));
        waitFor([mapc] { return mapc->base() == QLatin1String("street"); }, 2000);
        waitFor([win] { QQuickItem *c = item(win, "baseCover"); return !c || !c->property("active").toBool(); }, 2000);   // 换底图的盖帧淡完
    }
    // 想去（PO 选改进 c：只显示想去）、照片（全部）
    {
        const double swT0 = perf::nowMs();
        if (g_focus) key(win, Qt::Key_2, Qt::ControlModifier); else goPage(win, QStringLiteral("wish"));
        const bool w = waitMode(pc, win, QStringLiteral("wish"));
        double swMs = -1;
        for (const perf::Entry &e : perf::entriesSince(swT0)) if (e.what == QStringLiteral("换模式（直接换）")) swMs = e.ms;
        QQuickItem *cover = item(win, "baseCover");
        r.check(w && !(cover && cover->property("active").toBool()) && swMs >= 0 && swMs < 500,
                QStringLiteral("换模式直接换（不截帧盖住、不停顿）"), QStringLiteral("足迹 → 想去：换的那一下 %1 ms").arg(swMs, 0, 'f', 0));
        const QSet<QString> s = statusesOnMap(mapc);
        QQuickItem *title = item(win, "listTitle");
        r.check(w && (s.isEmpty() ? byStatus.value(QStringLiteral("wish")) == 0 : s == QSet<QString>{ QStringLiteral("wish") }) && mapc->base() == QLatin1String("street")
                    && pc->overlayAvailable() && title && title->property("text").toString() == tp::tr("想去"),
                QStringLiteral("Ctrl 2 想去：只画想去的，筛选里有\"叠上其他旅程\"，列表标题\"想去\""),
                QStringLiteral("状态 %1，底图 %2").arg(join(s), mapc->base()));
        if (g_focus) key(win, Qt::Key_4, Qt::ControlModifier); else goPage(win, QStringLiteral("photo"));
        const bool ph = waitMode(pc, win, QStringLiteral("photo"));
        const QSet<QString> s2 = statusesOnMap(mapc);
        int total = 0;
        for (int n : byStatus) total += n;
        r.check(ph && int(mapc->trips().plans().size()) == total && !pc->overlayAvailable() && mapc->overlayTrips().plans().isEmpty(),
                QStringLiteral("Ctrl 4 照片：画全部计划，没有\"叠上其他旅程\""), QStringLiteral("%1 / %2 个").arg(mapc->trips().plans().size()).arg(total));
        // "叠上其他旅程"记在各自的模式里：想去模式打开，回计划模式不受影响
        goPage(win, QStringLiteral("wish"));
        waitMode(pc, win, QStringLiteral("wish"));
        pc->setOverlay(true);
        goPage(win, QStringLiteral("plan"));
        waitMode(pc, win, QStringLiteral("plan"));
        const bool op = pc->overlay();
        const int np = int(mapc->overlayTrips().plans().size());
        goPage(win, QStringLiteral("wish"));
        waitMode(pc, win, QStringLiteral("wish"));
        const bool ow = pc->overlay();
        const int nw = int(mapc->overlayTrips().plans().size());
        pc->setOverlay(false);
        // 0.10.5 子选项：叠哪些。overlayStatuses = 淡灰细线实际画了哪些状态
        const auto overlayStatuses = [mapc] {
            QSet<QString> st;
            for (const map::TripPlan &p : mapc->overlayTrips().plans()) st.insert(p.status.isEmpty() ? QStringLiteral("planning") : p.status);
            return st;
        };
        const auto n = [mapc] { return int(mapc->overlayTrips().plans().size()); };
        const int nPlan = byStatus.value(QStringLiteral("planning")), nWish = byStatus.value(QStringLiteral("wish")), nDone = byStatus.value(QStringLiteral("completed"));
        goPage(win, QStringLiteral("wish"));
        waitMode(pc, win, QStringLiteral("wish"));
        pc->setOverlay(true);
        waitFor([&] { return n() == nPlan + nDone; }, 1500);
        r.check(n() == nPlan + nDone && pc->overlayParts().size() == 2 && pc->overlayParts().contains(QStringLiteral("plan")) && pc->overlayParts().contains(QStringLiteral("foot")),
                QStringLiteral("子选项：想去模式列出计划、足迹，默认都开，和以前一样"), QStringLiteral("%1 个").arg(n()));
        pc->setOverlayPart(QStringLiteral("foot"), false);
        waitFor([&] { return n() == nPlan; }, 1500);
        r.check(n() == nPlan && (nPlan == 0 || overlayStatuses() == QSet<QString>{ QStringLiteral("planning") }), QStringLiteral("子选项：只开\"叠加计划\"只画计划中的"), join(overlayStatuses()));
        pc->setOverlayPart(QStringLiteral("plan"), false);
        waitFor([&] { return n() == 0; }, 1500);
        const bool dotAllOff = waitFor([win] { return !shown(item(win, "overlayDot")); }, 1000);
        r.check(n() == 0 && pc->overlay() && !pc->overlayLit() && dotAllOff, QStringLiteral("子选项：两项都关不画线、小圆点不亮，总开关还开着"));
        pc->setOverlayPart(QStringLiteral("foot"), true);
        waitFor([&] { return n() == nDone; }, 1500);
        r.check(n() == nDone && (nDone == 0 || overlayStatuses() == QSet<QString>{ QStringLiteral("completed") }) && pc->overlayLit(), QStringLiteral("子选项：只开\"叠加足迹\"只画已完成的"), join(overlayStatuses()));
        goPage(win, QStringLiteral("plan"));
        waitMode(pc, win, QStringLiteral("plan"));
        pc->setOverlay(true);
        waitFor([&] { return n() == nWish + nDone; }, 1500);
        const int planAll = n();
        pc->setOverlay(false);
        goPage(win, QStringLiteral("wish"));
        waitMode(pc, win, QStringLiteral("wish"));
        waitFor([&] { return n() == nDone; }, 1500);
        r.check(planAll == nWish + nDone && n() == nDone && pc->overlayParts().value(QStringLiteral("plan")).toBool() == false, QStringLiteral("子选项：换模式各记各的"), QStringLiteral("计划模式 %1 个；回想去 %2 个").arg(planAll).arg(n()));
        // 重开：写进真的设置文件（副本）再读回来；文件里别的内容不丢
        {
            QTemporaryDir dir;
            const QString f = dir.path() + QStringLiteral("/settings.json");
            Settings seed;
            seed.load(f);
            seed.setValue(QStringLiteral("selftestUnknown"), Value(QStringLiteral("留着")));
            seed.save();
            pc->reopenPartsForTest(f);
            waitFor([&] { return n() == nDone; }, 1500);
            Settings back;
            back.load(f);
            r.check(n() == nDone && pc->overlayParts().value(QStringLiteral("plan")).toBool() == false && pc->overlayParts().value(QStringLiteral("foot")).toBool() == true
                        && back.value(QStringLiteral("selftestUnknown")).toString() == QStringLiteral("留着"),
                    QStringLiteral("子选项：重开后还在，设置文件里别的内容保留"), QStringLiteral("%1 个").arg(n()));
        }
        // 真点一下：弹窗里只列另外两种，点了能关能开；总开关关了就藏起来
        pc->setOverlayPart(QStringLiteral("plan"), true);
        pc->setOverlayPart(QStringLiteral("foot"), true);
        QQuickItem *fm = item(win, "filterMenu");
        if (fm) fm->setProperty("open", true);
        waitFor([fm] { return fm && fm->opacity() > 0.95; }, 1500);
        waitStill(fm, 1500);   // 胶囊和弹窗滑到位再点，不然点空
        QQuickItem *partFoot = item(win, "overlayPart-foot");
        const bool partsShown = shown(item(win, "overlayPart-plan")) && shown(partFoot) && !shown(item(win, "overlayPart-wish"));
        click(win, partFoot);
        const bool clickedOff = waitFor([pc] { return !pc->overlayParts().value(QStringLiteral("foot")).toBool(); }, 1000);
        pause(700);
        click(win, partFoot);
        const bool clickedOn = waitFor([pc] { return pc->overlayParts().value(QStringLiteral("foot")).toBool(); }, 1000);
        r.check(partsShown && clickedOff && clickedOn, QStringLiteral("子选项：弹窗里只列另外两种，真点能关能开"),
                QStringLiteral("显示 %1，关 %2，开 %3；弹窗 open %4、y %5").arg(partsShown).arg(clickedOff).arg(clickedOn).arg(fm && fm->property("open").toBool()).arg(fm ? fm->y() : -1));
        pc->setOverlay(false);
        r.check(waitFor([win] { return !shown(item(win, "overlayPart-plan")); }, 1000) && waitFor([&] { return n() == 0; }, 1500), QStringLiteral("子选项：总开关关着时不显示"));
        if (fm) fm->setProperty("open", false);
        goPage(win, QStringLiteral("foot"));
        waitMode(pc, win, QStringLiteral("foot"));
        const QVariantMap footParts = pc->overlayParts();
        goPage(win, QStringLiteral("photo"));
        waitMode(pc, win, QStringLiteral("photo"));
        r.check(footParts.contains(QStringLiteral("plan")) && footParts.contains(QStringLiteral("wish")) && footParts.size() == 2 && pc->overlayParts().isEmpty(), QStringLiteral("子选项：足迹列计划、想去；照片没有"));
        goPage(win, QStringLiteral("plan"));
        waitMode(pc, win, QStringLiteral("plan"));
        r.check(!op && np == 0 && ow && (nw > 0 || total == byStatus.value(QStringLiteral("wish"))),
                QStringLiteral("\"叠上其他旅程\"每种模式各记各的（想去里打开的不影响计划）"), QStringLiteral("计划 %1（%2 个）；想去 %3（%4 个）").arg(op).arg(np).arg(ow).arg(nw));
    }
    // 整页占位：账本
    {
        goPage(win, QStringLiteral("plan"));
        waitMode(pc, win, QStringLiteral("plan"));
        goPage(win, QStringLiteral("foot"));
        waitMode(pc, win, QStringLiteral("foot"));
        click(win, item(win, "dotsPill"));
        QQuickItem *bud = item(win, "bud-ledger");
        waitFor([bud] { return bud && bud->opacity() > 0.95; }, 1500);
        click(win, bud);
        QQuickItem *page = item(win, "ledgerPage");
        const bool open = waitFor([page] { return page && page->opacity() > 0.95; }, 1500);
        QQuickItem *title = item(win, "placeholderTitle");
        QQuickItem *label = item(win, "dotsLabel");
        r.check(open && nav->page() == QLatin1String("ledger")
                    && shown(item(win, "backBtn")) && !shown(item(win, "statusPill")) && label && label->property("text").toString() == tp::tr("账本"),
                QStringLiteral("点\"账本\"：账本盖住地图，⋯ 上显示\"账本\"，出现 ↩，左下胶囊藏起来"));
        const QString e = escOnce(win);
        const bool back = waitFor([nav] { return nav->page() == QLatin1String("foot"); }, 800);
        r.check(e == QLatin1String("page") && back && pc->mode() == QLatin1String("foot") && waitFor([page] { return page && !page->isVisible(); }, 1500),
                QStringLiteral("账本里按 Esc：回到进来之前的模式（足迹）"), nav->page());
        // 前进 / 后退：足迹 → 日历 → 游记，后退两步回到足迹，再前进到日历
        goPage(win, QStringLiteral("calendar"));
        goPage(win, QStringLiteral("journal"));
        const bool j = nav->page() == QLatin1String("journal") && waitFor([title] { return title && title->property("text").toString() == tp::tr("游记"); }, 800);
        if (g_focus) { key(win, Qt::Key_Left, Qt::AltModifier); key(win, Qt::Key_Left, Qt::AltModifier); } else { nav->back(); nav->back(); }
        const bool b2 = nav->page() == QLatin1String("foot") && pc->mode() == QLatin1String("foot");
        if (g_focus) key(win, Qt::Key_Right, Qt::AltModifier); else nav->forward();
        r.check(j && b2 && nav->page() == QLatin1String("calendar"), QStringLiteral("Alt ← / → 在整页和模式之间按顺序走"), nav->page());
        // 模式之间也算一步：计划 → 足迹，后退回到计划
        goPage(win, QStringLiteral("plan"));
        waitMode(pc, win, QStringLiteral("plan"));
        goPage(win, QStringLiteral("foot"));
        waitMode(pc, win, QStringLiteral("foot"));
        nav->back();
        const bool bp = waitMode(pc, win, QStringLiteral("plan"));
        r.check(bp && nav->page() == QLatin1String("plan") && mapc->base() == QLatin1String("street"), QStringLiteral("后退也会换模式（足迹 → 计划，底图不变）"),
                QStringLiteral("页面 %1，底图 %2").arg(nav->page(), mapc->base()));
    }
    // 足迹里定位一个计划中的计划：切到计划模式
    {
        QString planning;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
            if (map::plans::statusOf(p) == QLatin1String("planning") && !p[QStringLiteral("stops")].asArray().empty()) { planning = p[QStringLiteral("id")].toString(); break; }
        if (!planning.isEmpty()) {
            goPage(win, QStringLiteral("foot"));
            waitMode(pc, win, QStringLiteral("foot"));
            const QVariantMap rv = pc->openPlan(planning, false);
            QMetaObject::invokeMethod(win, "revealToast", Q_ARG(QVariant, rv));
            waitMapIdle(win, mapc);
            bool onMap = false;
            for (const map::TripPlan &p : mapc->trips().plans()) onMap = onMap || p.id == planning;
            r.check(rv.value(QStringLiteral("mode")).toString() == QLatin1String("plan") && pc->mode() == QLatin1String("plan") && nav->page() == QLatin1String("plan")
                        && onMap && mapc->markers()->selectedPlan() == planning,
                    QStringLiteral("足迹模式里打开一个计划中的计划：切到\"计划\"、选中它"), QStringLiteral("模式 %1，页面 %2").arg(pc->mode(), nav->page()));
            mapc->markers()->clearSelection();
            mapc->setBase(QStringLiteral("street"));
        }
    }
    // Ctrl K：搜旧页面名
    {
        QObject *pal = win->findChild<QObject *>(QStringLiteral("palette"));
        QStringList got;
        bool ok = pal != nullptr;
        for (const auto &[q, want] : { std::pair{ "计划库", "plan" }, std::pair{ "愿望清单", "wish" }, std::pair{ "相册", "photo" }, std::pair{ "账本", "ledger" } }) {
            if (!pal) break;
            QMetaObject::invokeMethod(pal, "show", Q_ARG(QVariant, tp::tr(q)));
            waitFrames(win, 1);
            QString key;
            for (const QVariant &v : pal->property("results").toList()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("kind")).toString() == QLatin1String("page")) { key = m.value(QStringLiteral("key")).toString(); break; }
            }
            got << QStringLiteral("%1 → %2").arg(tp::tr(q), key);
            ok = ok && key == QLatin1String(want);
        }
        if (pal) QMetaObject::invokeMethod(pal, "hide");
        r.check(ok, QStringLiteral("Ctrl K：搜\"计划库\"\"愿望清单\"\"相册\"\"账本\"找到对应的模式和页面"), got.join(QStringLiteral("；")));
    }
    // 窄窗口：⋯ 排不下时换行，不压到右边的胶囊
    {
        const int w0 = win->width();
        win->setWidth(560);
        waitFor([win] { return win->width() == 560; }, 2000); waitFrames(win, 2);
        QMetaObject::invokeMethod(item(win, "topBar"), "setBubbles", Q_ARG(QVariant, true), Q_ARG(QVariant, QVariant()));
        QQuickItem *last = item(win, demo.isEmpty() ? "bud-ledger" : "bud-next");
        waitFor([last] { return last && last->opacity() > 0.95; }, 1500);
        waitStill(last, 1500);
        QQuickItem *cap = item(win, "topCapsule");
        const double capLeft = cap ? cap->mapToScene(QPointF(0, 0)).x() : win->width();
        double maxRight = 0, maxBottom = 0;
        for (const char *k : { "bud-plan", "bud-wish", "bud-foot", "bud-photo", "bud-calendar", "bud-journal", "bud-ledger", "bud-next" }) {
            QQuickItem *b = item(win, k);
            if (!shown(b)) continue;
            const QPointF tl = b->mapToScene(QPointF(0, 0));
            maxRight = std::max(maxRight, tl.x() + b->width());
            maxBottom = std::max(maxBottom, tl.y() + b->height());
        }
        r.check(maxRight <= capLeft && maxBottom > win->property("topH").toDouble(),
                QStringLiteral("560 宽：⋯ 的泡泡排成两行，不压到右边的胶囊"), QStringLiteral("最右 %1，胶囊左边 %2，最下 %3").arg(maxRight).arg(capLeft).arg(maxBottom));
        escOnce(win);
        win->setWidth(w0);
        waitFor([win, w0] { return win->width() == w0; }, 2000); waitFrames(win, 2);
    }
    // 收尾：回到开始时的样子（截图 / 自检默认：计划模式，四种状态都画）
    goPage(win, QStringLiteral("plan"));
    waitMode(pc, win, QStringLiteral("plan"));
    pc->setTestAllStatuses(true);
    mapc->setBase(QStringLiteral("street"));
    pc->setNextTrip(oldNext.isEmpty() ? demo.value(QStringLiteral("planId")).toString() : oldNext, !oldNext.isEmpty());
    waitFrames(win, 2);
}

} // namespace tp::app::selftest
