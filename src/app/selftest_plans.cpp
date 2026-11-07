// 自检：0.8.7 计划排序、筛选、"此刻"、简易计划面板、换底图盖一帧。只用 C++ 对象（界面靠截图）。
// 会改数据（勾待办、置顶、标记为已完成、删除），每一步都撤销回去；验证.bat 用的是数据副本。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "jsonio.h"
#include "mapcontroller.h"
#include "plancontroller.h"
#include "plans.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <qpa/qwindowsysteminterface.h>

namespace tp::app::selftest {

using json::Value;
namespace plans = map::plans;

namespace {
bool mapHas(map::MapController *mapc, const QString &id)
{
    for (const map::TripPlan &p : mapc->trips().plans())
        if (p.id == id) return true;
    return false;
}
const Value *planOf(const Value &root, const QString &id)
{
    for (const Value &p : root[QStringLiteral("plans")].asArray())
        if (p[QStringLiteral("id")].toString() == id) return &p;
    return nullptr;
}
bool waitSaved(AppController *app) { return waitFor([app] { return !app->dirty(); }, 3000); }
} // namespace

void runPlans(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("计划：排序、筛选、此刻、简易计划面板（0.8.7）"));
    PlanController *pc = plansCtl();
    if (!r.check(pc != nullptr, QStringLiteral("计划控制器在"))) return;
    data::DataStore *store = app->store();
    mapc->setBase(QStringLiteral("street"));
    waitMapIdle(win, mapc);

    // ---- 排序和 Electron 一样（tests/golden/plans.json；Windows 上中文名的排序走系统的排序规则，看是否一致） ----
#ifdef TP_SOURCE_DIR
    {
        QFile f(QStringLiteral(TP_SOURCE_DIR "/tests/golden/plans.json"));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject g = QJsonDocument::fromJson(f.readAll()).object();
            const Value in = json::parse(QString::fromUtf8(QJsonDocument(g.value(QStringLiteral("input")).toObject()).toJson(QJsonDocument::Compact)));
            const json::Array &arr = in[QStringLiteral("plans")].asArray();
            int bad = 0;
            QString first;
            for (bool pin : { true, false }) {
                const QVector<int> idx = plans::sortedIndexes(in, pin);
                const QJsonArray want = g.value(pin ? QStringLiteral("sortPin") : QStringLiteral("sortNoPin")).toArray();
                for (int i = 0; i < idx.size() && i < want.size(); ++i)
                    if (arr[idx[i]][QStringLiteral("id")].toString() != want[i].toString() && !bad++)
                        first = QStringLiteral("第 %1 个是 %2，Electron 是 %3").arg(i).arg(arr[idx[i]][QStringLiteral("id")].toString(), want[i].toString());
            }
            r.check(bad == 0, QStringLiteral("计划排序和 Electron 一样（含中文名）"),
                    bad ? QStringLiteral("%1 处不同；%2").arg(bad).arg(first) : QStringLiteral("%1 个计划").arg(arr.size()));
        } else {
            r.info(QStringLiteral("没有 tests/golden/plans.json，跳过排序对照"));
        }
    }
#endif

    // ---- 筛选 → 地图 ----
    pc->filterAll();
    pc->showAllHidden();
    int total = 0, completed = 0;
    QString aCompleted, aRoute;
    for (const Value &p : store->data()[QStringLiteral("plans")].asArray()) {
        bool hasStop = false;
        for (const Value &s : p[QStringLiteral("stops")].asArray()) hasStop = hasStop || (s[QStringLiteral("lat")].isNumber() && s[QStringLiteral("lng")].isNumber());
        if (!hasStop) continue;
        ++total;
        if (plans::statusOf(p) == QLatin1String("completed")) { ++completed; if (aCompleted.isEmpty()) aCompleted = p[QStringLiteral("id")].toString(); }
        if (aRoute.isEmpty() && plans::typeOf(p) == QLatin1String("route") && plans::statusOf(p) != QLatin1String("completed")) aRoute = p[QStringLiteral("id")].toString();
    }
    r.check(int(mapc->trips().plans().size()) == total, QStringLiteral("全选时地图上画全部计划"), QStringLiteral("%1 / %2").arg(mapc->trips().plans().size()).arg(total));
    const int provincesAll = mapc->unlockResult().provinces;
    // 0.9.7：计划模式只画计划中的（截图 / 自检平时四种状态都画，这里关掉）
    pc->setTestAllStatuses(false);
    bool noCompleted = true;
    for (const map::TripPlan &p : mapc->trips().plans()) noCompleted = noCompleted && p.status != QLatin1String("completed");
    r.check(noCompleted && pc->excludedCount() == 0,
            QStringLiteral("计划模式：地图上不画已完成的计划，筛选角标不算状态（0.8.10 状态归模式）"),
            QStringLiteral("剩 %1 个，角标 %2").arg(mapc->trips().plans().size()).arg(pc->excludedCount()));
    if (mapc->unlockLoaded()) {
        mapc->recomputeUnlockForTest();
        r.check(mapc->unlockResult().provinces == provincesAll, QStringLiteral("\"已解锁\"不受筛选影响"), QStringLiteral("%1 个省 / 州").arg(mapc->unlockResult().provinces));
    }
    // 定位一个不在这个模式里的计划：切到它的模式（0.9.7 起没有"也显示"，已完成的切到足迹）、选中、面板出来
    if (!aCompleted.isEmpty()) {
        const QVariantMap rv = pc->openPlan(aCompleted, false);
        waitFor([mapc, &aCompleted] { return mapHas(mapc, aCompleted) && mapc->markers()->selectedPlan() == aCompleted; }, 1500);
        r.check(rv.value(QStringLiteral("mode")).toString() == QLatin1String("foot") && mapHas(mapc, aCompleted)
                    && mapc->markers()->selectedPlan() == aCompleted && pc->sheet().value(QStringLiteral("id")).toString() == aCompleted,
                QStringLiteral("定位不在这个模式里的计划时切到它的模式、选中、出简易面板"),
                QStringLiteral("切到 %1，选中 %2").arg(rv.value(QStringLiteral("mode")).toString(), mapc->markers()->selectedPlan()));
        QQuickItem *sheet = win->findChild<QQuickItem *>(QStringLiteral("planSheet"));
        const bool shown = waitFor([sheet, win] { return sheet && sheet->isVisible() && sheet->x() + sheet->width() <= win->width() - 10; }, 2000);
        r.check(shown, QStringLiteral("简易计划面板滑进来了"),
                sheet ? QStringLiteral("x = %1（窗口宽 %2）").arg(sheet->x()).arg(win->width()) : QStringLiteral("找不到 planSheet"));
        QQuickItem *pill = win->findChild<QQuickItem *>(QStringLiteral("statusPill"));
        r.check(pill && !pill->isVisible(), QStringLiteral("面板开着时左下的状态胶囊藏起来（Electron 同）"));
        // 让开面板：飞过去以后计划的站点在面板左边
        mapc->flyToPlan(aCompleted);
        waitFor([mapc] { return !mapc->moving(); }, 3000);
        waitFrames(win, 2);
        double cx = 0;
        int n = 0;
        for (const map::TripPlan &p : mapc->trips().plans())
            if (p.id == aCompleted)
                for (const map::TripStop &s : p.stops) { cx += mapc->pixelFor(s.lat, s.lng).x(); ++n; }
        if (n) cx /= n;
        const double right = sheet ? sheet->x() : win->width();
        r.check(n && cx > 0 && cx < right, QStringLiteral("飞过去时让开面板（计划在面板左边）"),
                QStringLiteral("站点中心 x = %1，面板左边 x = %2").arg(cx, 0, 'f', 0).arg(right, 0, 'f', 0));
        // 点进某一站以后回到整条路线（0.8.7 PO；0.11.6 改成这一排最左边的 ↩ 全部路线）：看某一站时才出现，点了回概览；全部路线时不显示
        if (mapc->markers()->canStep()) {
            auto ov = [&] { return win->findChild<QQuickItem *>(QStringLiteral("sheetOverview")); };
            auto vis = [&] { QQuickItem *a = ov(); return a && a->isVisible() && a->opacity() > 0.5; };
            mapc->markers()->selectPlan(aCompleted, 1, true);
            const bool onStop = waitFor(vis, 1500);
            bool back = false;
            if (onStop) {
                waitStill(ov(), 1500);
                QQuickItem *a = ov();
                const QPointF p = a->mapToScene(QPointF(a->width() / 2, a->height() / 2));
                const QPointF g = win->mapToGlobal(p);
                QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
                QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
                back = waitFor([&] { return mapc->markers()->focusStop() < 0 && !vis(); }, 1500);
            }
            r.check(onStop && back, QStringLiteral("简易面板：看某一站时有 ↩ 全部路线，点了回到整条路线，全部路线时图标不显示"),
                    QStringLiteral("出现 %1，点后回概览 %2，focusStop %3，").arg(onStop).arg(back).arg(mapc->markers()->focusStop()) + (ov() ? QStringLiteral("图标 %1").arg(vis()) : QStringLiteral("找不到 sheetOverview")));
        }
        mapc->markers()->clearSelection();
        waitFrames(win, 2);
    }
    // 回到截图 / 自检的默认（计划模式、四种状态都画）
    pc->setMode(QStringLiteral("plan"));
    pc->setTestAllStatuses(true);
    // 隐藏
    if (!aRoute.isEmpty()) {
        pc->setHidden(aRoute, true);
        const bool gone = !mapHas(mapc, aRoute) && pc->hiddenCount() == 1;
        pc->showAllHidden();
        r.check(gone && mapHas(mapc, aRoute), QStringLiteral("隐藏一个计划 → 地图上没有；全部显示 → 回来"));
    }
    pc->filterNone();
    r.check(mapc->trips().plans().isEmpty() && pc->excludedCount() == 12, QStringLiteral("清空（类型、区域）：地图上一个都不画，角标 12"), QStringLiteral("角标 %1").arg(pc->excludedCount()));
    pc->filterAll();
    pc->onlyRegion(QStringLiteral("中国"));
    bool onlyChina = true;
    for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
        if (mapHas(mapc, p[QStringLiteral("id")].toString())) onlyChina = onlyChina && plans::regionsOf(p).contains(QStringLiteral("中国"));
    r.check(onlyChina && pc->filterRegion() == QStringList{ QStringLiteral("中国") }, QStringLiteral("只看中国"), QStringLiteral("地图上 %1 个").arg(mapc->trips().plans().size()));
    pc->filterAll();
    r.check(int(mapc->trips().plans().size()) == total, QStringLiteral("全选恢复"));

    // ---- 此刻 ----
    const QVariantMap demo = pc->demo();
    if (demo.isEmpty()) {
        r.info(QStringLiteral("数据里没有带日期的路线，跳过\"此刻\""));
    } else {
        const QString id = demo.value(QStringLiteral("planId")).toString();
        const QString start = demo.value(QStringLiteral("start")).toString(), end = demo.value(QStringLiteral("end")).toString();
        const QString oldNext = store->data()[QStringLiteral("nextTripId")].toString();
        pc->setNextTrip(id, true);
        waitSaved(app);
        pc->setTodayOverride(plans::addDays(start, 1));
        QVariantMap n = pc->now();
        r.check(n.value(QStringLiteral("kind")).toString() == QLatin1String("live") && n.value(QStringLiteral("dayN")).toInt() == 2,
                QStringLiteral("此刻：开始后第二天 → 在路上 · 第 2 天"),
                QStringLiteral("%1 第 %2 / %3 天，今天在 %4").arg(n.value("kind").toString()).arg(n.value("dayN").toInt()).arg(n.value("dayTotal").toInt()).arg(n.value("here").toString()));
        pc->setTodayOverride(plans::addDays(end, 3));
        n = pc->now();
        r.check(n.value(QStringLiteral("kind")).toString() == QLatin1String("back") && n.value(QStringLiteral("daysAgo")).toInt() == 3, QStringLiteral("此刻：结束后 3 天 → 刚回来 · 3 天前"));
        pc->setTodayOverride(plans::addDays(start, -5));
        n = pc->now();
        r.check(n.value(QStringLiteral("kind")).toString() == QLatin1String("next") && n.value(QStringLiteral("daysUntil")).toInt() == 5, QStringLiteral("此刻：开始前 5 天 → 下一次出发 · 5 天后"));
        // 0.8.8：入口从原型条挪进了顶栏 ⋯ 泡泡；⋯ 上带倒计时角标（PO 选的顶栏方案 C）
        QQuickItem *badges = win->findChild<QQuickItem *>(QStringLiteral("dotsBadges"));
        r.check(badges && badges->isVisible(), QStringLiteral("顶栏 ⋯ 上出现\"下次出发\"的角标"));

        // 勾待办：先给这个计划加一条待办（副本数据），勾上 → 自动保存、角标减一；撤销回来
        store->modify([&](Value &d) {
            for (Value &p : d.mutObject().find(QStringLiteral("plans"))->mutArray()) {
                if (std::as_const(p)[QStringLiteral("id")].toString() != id) continue;
                json::Array todos = std::as_const(p)[QStringLiteral("todos")].asArray();
                json::Object t;
                t.set(QStringLiteral("id"), Value(QStringLiteral("td_selftest")));
                t.set(QStringLiteral("text"), Value(QStringLiteral("自检加的待办")));
                t.set(QStringLiteral("done"), Value(false));
                todos.push_back(Value(std::move(t)));
                p.mutObject().set(QStringLiteral("todos"), Value(std::move(todos)));
            }
        });
        app->noteChanged(true);
        waitSaved(app);
        const int openBefore = pc->now().value(QStringLiteral("todoOpen")).toInt();
        const bool ok = pc->setTodoDone(id, QStringLiteral("td_selftest"), true);
        const bool saved = waitSaved(app);
        r.check(ok && saved && pc->now().value(QStringLiteral("todoOpen")).toInt() == openBefore - 1, QStringLiteral("勾待办：改数据、自动保存、角标减一"),
                QStringLiteral("待办 %1 → %2").arg(openBefore).arg(pc->now().value(QStringLiteral("todoOpen")).toInt()));
        app->undo();
        waitSaved(app);
        r.check(pc->now().value(QStringLiteral("todoOpen")).toInt() == openBefore, QStringLiteral("撤销：待办回来"));
        app->undo();   // 去掉自检加的待办
        waitSaved(app);
        if (oldNext != id) {
            if (oldNext.isEmpty()) pc->setNextTrip(id, false);
            else pc->setNextTrip(oldNext, true);
            waitSaved(app);
        }
        pc->setTodayOverride(QString());
    }

    // ---- 置顶 / 标记为已完成 / 删除（各撤销一次） ----
    QString victim;
    for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
        if (plans::statusOf(p) == QLatin1String("planning") && plans::typeOf(p) == QLatin1String("route") && !p.asObject().contains(QStringLiteral("pinnedAt")))
            victim = p[QStringLiteral("id")].toString();
    if (!victim.isEmpty()) {
        pc->togglePin(victim);
        waitSaved(app);
        {
            const QVector<int> idx = plans::sortedIndexes(store->data(), true);
            const json::Array &arr = store->data()[QStringLiteral("plans")].asArray();
            r.check(!idx.isEmpty() && arr[idx.first()][QStringLiteral("id")].toString() == victim, QStringLiteral("置顶 → 排在最前"));
        }
        app->undo();
        waitSaved(app);
        const Value *vp = planOf(store->data(), victim);
        r.check(vp && !vp->asObject().contains(QStringLiteral("pinnedAt")), QStringLiteral("撤销置顶"));

        pc->markCompleted(victim);
        waitSaved(app);
        vp = planOf(store->data(), victim);
        r.check(vp && (*vp)[QStringLiteral("status")].toString() == QLatin1String("completed"), QStringLiteral("标记为已完成"));
        app->undo();
        waitSaved(app);

        const int before = int(store->data()[QStringLiteral("plans")].asArray().size());
        pc->openPlan(victim, false);
        waitFor([&] { return mapc->markers()->selectedPlan() == victim; }, 1000);
        const bool del = pc->deletePlan(victim);
        const bool saved = waitSaved(app);
        // 地图跟着数据重画有 60 ms 的防抖；电脑忙的时候会更久（0.8.7 PO 第二次验证时 0.3 秒不够），最多等 3 秒
        const bool goneFromMap = waitFor([mapc, &victim] { return !mapHas(mapc, victim) && mapc->markers()->selectedPlan().isEmpty(); }, 3000);
        const int after = int(store->data()[QStringLiteral("plans")].asArray().size());
        r.check(del && after == before - 1 && goneFromMap, QStringLiteral("删除计划：数据里没了、地图上没了、面板关掉"),
                QStringLiteral("计划 %1 → %2，保存了 %3，地图上 %4，选中「%5」")
                    .arg(before).arg(after).arg(saved ? "是" : "否", mapHas(mapc, victim) ? "还在" : "没了", mapc->markers()->selectedPlan()));
        app->undo();
        waitSaved(app);
        r.check(int(store->data()[QStringLiteral("plans")].asArray().size()) == before && planOf(store->data(), victim), QStringLiteral("撤销删除：计划回来"));
    }

    // ---- 换底图：先盖一帧再淡出（"动效"开着时） ----
    {
        QQuickItem *cover = win->findChild<QQuickItem *>(QStringLiteral("baseCover"));
        bool covered = false;
        if (cover) {
            QMetaObject::invokeMethod(win, "switchBase", Q_ARG(QVariant, QVariant(QStringLiteral("sat"))));
            covered = waitFor([cover] { return cover->property("active").toBool(); }, 300);
        }
        const bool switched = waitFor([mapc] { return mapc->base() == QLatin1String("sat"); }, 2000);
        const bool released = cover && waitFor([cover] { return !cover->property("active").toBool(); }, 3000);
        r.check(cover && covered && switched && released, QStringLiteral("换底图：先盖住换之前的一帧，换好后淡出、放掉"),
                QStringLiteral("盖住 %1，换了 %2，放掉 %3").arg(covered ? "是" : "否", switched ? "是" : "否", released ? "是" : "否"));
        mapc->setBase(QStringLiteral("street"));
    }
}

} // namespace tp::app::selftest
