// 自检：0.11.0 计划库（展开、三种视图、搜索筛选排序、置顶隐藏、批量、O1 地图联动）、想去（月份条、分组、O2、开始计划、关联季节、新建）、足迹（统计）。
// 会改数据，每一步都撤销回去；验证.bat 用的是数据副本。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "mapcontroller.h"
#include "markerlayer.h"
#include "planeditor.h"
#include "plancontroller.h"
#include "plans.h"

#include <QDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QVariant>
#include <qpa/qwindowsysteminterface.h>

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
QQuickItem *item(QQuickWindow *win, const QString &name) { return findItem(win->contentItem(), name); }
bool shown(QQuickItem *it) { return it && it->isVisible() && it->opacity() > 0.5; }
void click(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return;
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    const QPointF g = win->mapToGlobal(p);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    pause(120);
}
void moveTo(QQuickWindow *win, const QPointF &p)
{
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, win->mapToGlobal(p), Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
    pause(60);
}
QVariantList libRows(PlanController *pc) { return pc->library().value(QStringLiteral("rows")).toList(); }
QVariantList listRows(PlanController *pc) { return pc->list().value(QStringLiteral("rows")).toList(); }
QString idOf(const QVariant &v) { return v.toMap().value(QStringLiteral("id")).toString(); }
QSet<QString> statusesOnMap(map::MapController *mapc)
{
    QSet<QString> s;
    for (const map::TripPlan &p : mapc->trips().plans()) s.insert(p.status);
    return s;
}
int planCount(data::DataStore *store) { return int(store->data()[QStringLiteral("plans")].asArray().size()); }
QString statusOfPlan(data::DataStore *store, const QString &id)
{
    const Value *p = plans::planById(store->data(), id);
    return p ? plans::statusOf(*p) : QString();
}
const map::TripPlan *tripOf(map::MapController *mapc, const QString &id)
{
    for (const map::TripPlan &p : mapc->trips().plans())
        if (p.id == id) return &p;
    return nullptr;
}
} // namespace

void runLibrary(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("计划库、想去、足迹（0.11.0）"));
    PlanController *pc = plansCtl();
    data::DataStore *store = app->store();
    if (!r.check(pc != nullptr, QStringLiteral("计划控制器在"))) return;
    pc->setTestAllStatuses(false);
    pc->filterAll();
    pc->showAllHidden();
    mapc->markers()->clearSelection();
    QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("plan")));
    waitFor([pc] { return pc->mode() == QLatin1String("plan"); }, 2000);
    win->setProperty("listOpen", true);
    QQuickItem *panel = item(win, QStringLiteral("planList"));
    waitStill(panel);

    // ---- 展开 ----
    QQuickItem *expand = item(win, QStringLiteral("listExpandBtn"));
    r.check(shown(expand), QStringLiteral("计划模式的列表头有\"展开\""));
    const double narrowW = panel->width();
    click(win, expand);
    waitFor([pc] { return pc->libraryOpen(); }, 2000);
    waitStill(panel);
    const bool sixty = win->width() < 900 || qAbs(panel->width() - win->width() * 0.6) < 2;
    r.check(pc->libraryOpen() && sixty && panel->width() > narrowW, QStringLiteral("点展开：列表占窗口约 60%，右边留着地图"),
            QStringLiteral("%1 / %2").arg(panel->width()).arg(win->width()));
    const int total = planCount(store);
    r.check(libRows(pc).size() == total, QStringLiteral("计划库列出全部计划（不只是计划中）"), QStringLiteral("%1 / %2").arg(libRows(pc).size()).arg(total));
    r.check(pc->library().value(QStringLiteral("view")).toString() == QLatin1String("gallery") && shown(item(win, QStringLiteral("libGallery"))),
            QStringLiteral("默认视图是画廊（PO 第 1 题 A）"));
    r.check(!shown(item(win, QStringLiteral("statusPill"))) && !shown(item(win, QStringLiteral("listFilterBtn"))),
            QStringLiteral("展开时左下胶囊和列表头的筛选让给计划库自己的筛选"));
    const QSet<QString> onMap = statusesOnMap(mapc);
    r.check(onMap.contains(QStringLiteral("planning")) && onMap.size() >= 2, QStringLiteral("展开时地图画的就是列出来的（全部状态）"), QStringList(onMap.values()).join(u','));

    // ---- 卡片：封面或路线简图 ----
    const QString firstId = idOf(libRows(pc).value(0));
    waitFrames(win, 3);
    QQuickItem *firstCard = item(win, QStringLiteral("libCard-") + firstId);
    r.check(shown(firstCard), QStringLiteral("画廊的第一张卡片建好了"), firstId);
    int covers = 0, sketches = 0;
    for (const QVariant &v : libRows(pc)) (v.toMap().value(QStringLiteral("coverFile")).toString().isEmpty() ? sketches : covers)++;
    r.info(QStringLiteral("有封面 %1、没有封面（路线简图）%2").arg(covers).arg(sketches));
    r.check(covers > 0, QStringLiteral("有的计划有封面（没设的用第一张照片）"));

    // ---- O1：鼠标放上去，地图上那条高亮、其余变淡 ----
    {
        QString lineId;   // 地图上有线的
        for (const map::TripPlan &p : mapc->trips().plans())
            if (p.isRoute()) { lineId = p.id; break; }
        mapc->markers()->hoverFromList(QString());
        pc->setLibraryPref(QStringLiteral("q"), lineId.isEmpty() ? QString() : plans::planById(store->data(), lineId)->operator[](QStringLiteral("name")).toString());
        waitFor([mapc, pc] { return mapc->trips().plans().size() <= libRows(pc).size(); }, 1500);   // 等地图跟上搜索（重建会清掉悬停）
        waitFrames(win, 3);
        QQuickItem *c = item(win, QStringLiteral("libCard-") + lineId);
        if (c) moveTo(win, c->mapToScene(QPointF(c->width() / 2, c->height() - 10)));
        const bool hov = waitFor([mapc, &lineId] { return mapc->markers()->hoverPlan() == lineId; }, 1500);
        const bool dim = waitFor([mapc] { return mapc->markers()->dimLevel() > 0.5; }, 1500);
        r.check(c && hov && mapc->markers()->hvPlan() == lineId && dim, QStringLiteral("O1：鼠标放到卡片上，地图上那条路线加粗、其他变淡"),
                QStringLiteral("%1 变淡 %2").arg(mapc->markers()->hoverPlan()).arg(mapc->markers()->dimLevel()));
        moveTo(win, QPointF(win->width() - 5, win->height() - 5));
        mapc->markers()->hoverFromList(QString());
        pc->setLibraryPref(QStringLiteral("q"), QString());
    }

    // ---- 搜索、状态、类型、排序 ----
    {
        // 找一个站点名只有这个计划有（且计划名里没有）的
        QString stopName;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
            for (const Value &s : p[QStringLiteral("stops")].asArray()) {
                const QString n = s[QStringLiteral("name")].toString();
                if (n.size() >= 2 && !p[QStringLiteral("name")].toString().contains(n) && stopName.isEmpty()) stopName = n;
            }
        pc->setLibraryPref(QStringLiteral("q"), stopName);
        const qsizetype nq = libRows(pc).size();
        r.check(nq >= 1 && nq < total, QStringLiteral("搜索：按站点名也找得到"), QStringLiteral("\"%1\" → %2 个").arg(stopName).arg(nq));
        const bool mapFollows = waitFor([mapc, nq] { return mapc->trips().plans().size() <= nq; }, 1500);   // 地图隔 200 ms 才跟（打字时不卡）
        r.check(mapFollows, QStringLiteral("搜索时地图也只画搜到的"));
        pc->setLibraryPref(QStringLiteral("q"), QString());

        click(win, item(win, QStringLiteral("libStatus-wish")));
        bool allWish = !libRows(pc).isEmpty();
        for (const QVariant &v : libRows(pc)) allWish = allWish && v.toMap().value(QStringLiteral("status")).toString() == QLatin1String("wish");
        r.check(allWish && waitFor([&] { return statusesOnMap(mapc) == QSet<QString>{ QStringLiteral("wish") }; }, 1500), QStringLiteral("点状态\"想去\"：列表和地图都只剩想去"));
        click(win, item(win, QStringLiteral("libStatus-all")));

        pc->setLibraryPref(QStringLiteral("type"), QStringLiteral("single"));
        bool allSingle = true;
        for (const QVariant &v : libRows(pc)) allSingle = allSingle && v.toMap().value(QStringLiteral("type")).toString() == QLatin1String("single");
        r.check(allSingle && libRows(pc).size() < total, QStringLiteral("类型筛选"));
        pc->setLibraryPref(QStringLiteral("type"), QStringLiteral("all"));

        pc->setLibraryPref(QStringLiteral("sort"), QStringLiteral("stops"));
        bool desc = true;
        int prev = 1 << 20;
        for (const QVariant &v : libRows(pc)) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("pinned")).toBool()) continue;
            desc = desc && m.value(QStringLiteral("stops")).toInt() <= prev;
            prev = m.value(QStringLiteral("stops")).toInt();
        }
        r.check(desc, QStringLiteral("按站点数排序（置顶的还在最前）"));
        pc->setLibraryPref(QStringLiteral("sort"), QStringLiteral("default"));
    }

    // ---- 三种视图 + 记住 ----
    {
        pc->setLibraryPref(QStringLiteral("view"), QStringLiteral("table"));
        waitFrames(win, 3);
        r.check(shown(item(win, QStringLiteral("libTable"))) && !shown(item(win, QStringLiteral("libGallery"))) && shown(item(win, QStringLiteral("libRow-") + firstId)),
                QStringLiteral("表格视图"));
        pc->setLibraryPref(QStringLiteral("view"), QStringLiteral("region"));
        waitFrames(win, 3);
        QQuickItem *reg = item(win, QStringLiteral("libRegions"));
        r.check(shown(reg), QStringLiteral("按区域视图"));
        // 折叠第一个区域
        const int before = reg ? reg->property("count").toInt() : 0;
        QQuickItem *head = nullptr;
        QString headKey;
        for (const QString &k : plans::allRegions()) if ((head = item(win, QStringLiteral("libReg-") + k)) && shown(head)) { headKey = k; break; }
        const bool hadHead = head != nullptr;
        click(win, head);
        waitFrames(win, 2);
        const int after = reg ? reg->property("count").toInt() : 0;
        r.check(hadHead && after < before, QStringLiteral("按区域：点标题折叠"), QStringLiteral("%1 → %2 行").arg(before).arg(after));
        click(win, item(win, QStringLiteral("libReg-") + headKey));   // 折叠后代理重建了，要重新找
        waitFrames(win, 2);
        QTemporaryDir tmp;
        pc->reopenLibraryForTest(tmp.filePath(QStringLiteral("settings.json")));
        r.check(pc->library().value(QStringLiteral("view")).toString() == QLatin1String("region"), QStringLiteral("写进设置再读回来：记住上次选的视图"));
        pc->setLibraryPref(QStringLiteral("view"), QStringLiteral("table"));
        waitFrames(win, 3);
    }

    // ---- 置顶、隐藏（表格里点真按钮） ----
    {
        QString pid;
        for (const QVariant &v : libRows(pc)) {
            const QVariantMap m = v.toMap();
            if (!m.value(QStringLiteral("pinned")).toBool()) { pid = m.value(QStringLiteral("id")).toString(); break; }
        }
        click(win, item(win, QStringLiteral("libPin-") + pid));
        waitFrames(win, 2);
        r.check(idOf(libRows(pc).value(0)) == pid || libRows(pc).value(0).toMap().value(QStringLiteral("pinned")).toBool(), QStringLiteral("置顶：排到最前（和地图列表的置顶是一套）"));
        app->undo();
        waitFrames(win, 2);
        const QString hid = idOf(libRows(pc).value(1));
        click(win, item(win, QStringLiteral("libHide-") + hid));
        waitFrames(win, 2);
        r.check(idOf(libRows(pc).last()) == hid && libRows(pc).last().toMap().value(QStringLiteral("hidden")).toBool(), QStringLiteral("在列表里隐藏：排到最后"));
        r.check(!pc->filter().hidden.contains(hid), QStringLiteral("列表里隐藏不影响地图（眼睛是另一项）"));
        // 收起的列表也排到最后、一组"已隐藏"
        const QVariantList rowsAll = plans::listInfo(store->data(), plans::Filter::all(), false, { hid }).value(QStringLiteral("rows")).toList();
        r.check(idOf(rowsAll.last()) == hid && rowsAll.last().toMap().value(QStringLiteral("group")).toString() == QLatin1String("listHidden"), QStringLiteral("收起的列表里也在最后一组"));
        pc->setListHidden({ hid }, false);
    }

    // ---- 批量 ----
    {
        click(win, item(win, QStringLiteral("libSelect")));
        waitFrames(win, 2);
        QQuickItem *lv = item(win, QStringLiteral("libraryView"));
        r.check(lv && lv->property("selMode").toBool() && shown(item(win, QStringLiteral("libSelBar"))), QStringLiteral("选择模式：出现批量操作条"));
        const QString a = idOf(libRows(pc).value(0)), b = idOf(libRows(pc).value(1));
        click(win, item(win, QStringLiteral("libRow-") + a));
        pause(700);   // 差异清单 #26：连点隔开
        click(win, item(win, QStringLiteral("libRow-") + b));
        r.check(lv && lv->property("selIds").toStringList().size() == 2 && pc->libraryOpen(), QStringLiteral("选择模式：点行是勾选，不打开"),
                lv ? lv->property("selIds").toStringList().join(u',') : QString());
        click(win, item(win, QStringLiteral("libSelAll")));
        r.check(lv && lv->property("selIds").toStringList().size() == total, QStringLiteral("全选"));
        click(win, item(win, QStringLiteral("libSelAll")));
        r.check(lv && lv->property("selIds").toStringList().isEmpty(), QStringLiteral("全不选"));
        const QStringList ids = { a, b };
        const QString oldA = statusOfPlan(store, a), target = oldA == QLatin1String("wish") ? QStringLiteral("completed") : QStringLiteral("wish");
        r.check(pc->setPlansStatus(ids, target) && statusOfPlan(store, a) == target && statusOfPlan(store, b) == target, QStringLiteral("批量改状态"));
        app->undo();
        r.check(statusOfPlan(store, a) == oldA, QStringLiteral("批量改状态一步撤销"));

        // 删除：先取 10 个，确认框列前 8 个
        QStringList ten;
        for (int i = 0; i < 10 && i < total; ++i) ten << idOf(libRows(pc).value(i));
        lv->setProperty("selIds", ten);
        waitFrames(win, 2);
        click(win, item(win, QStringLiteral("libSelDelete")));
        QQuickItem *dlg = item(win, QStringLiteral("bulkConfirm"));
        const QStringList paras = dlg ? dlg->property("paragraphs").toStringList() : QStringList();
        const QString body = paras.value(0);
        r.check(shown(dlg) && body.count(QStringLiteral("· ")) == 8 && body.contains(QStringLiteral("10")) && paras.value(1).contains(QStringLiteral("照片")),
                QStringLiteral("批量删除：确认框列前 8 个名字，说明照片不删"), body.left(60));
        const int photosBefore = int(store->data()[QStringLiteral("photos")].asArray().size());
        QMetaObject::invokeMethod(dlg, "chosen", Q_ARG(QString, QStringLiteral("ok")));
        waitFrames(win, 2);
        r.check(planCount(store) == total - 10 && int(store->data()[QStringLiteral("photos")].asArray().size()) == photosBefore
                    && !lv->property("selMode").toBool(), QStringLiteral("删除后计划少了 10 个、照片一张没少、退出选择模式"));
        app->undo();
        waitFrames(win, 2);
        r.check(planCount(store) == total, QStringLiteral("批量删除一步撤销"));
    }

    // ---- 打开、Esc、换模式 ----
    {
        const QString id = idOf(libRows(pc).value(0));
        QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
        const bool tripOn = waitFor([win] { return win->property("tripOpen").toBool(); }, 2000);
        r.check(tripOn && pc->libraryOpen(), QStringLiteral("点计划打开完整计划页，计划库还开着"));
        QVariant ret;
        QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("map")));
        waitFor([win] { return !win->property("tripOpen").toBool(); }, 2000);
        waitFrames(win, 3);
        r.check(pc->libraryOpen() && pc->mode() == QLatin1String("plan"), QStringLiteral("回来还在计划库（想去 / 已完成的计划也不换模式）"), pc->mode());
        mapc->markers()->clearSelection();
        QMetaObject::invokeMethod(win, "escOnce", Q_RETURN_ARG(QVariant, ret));
        r.check(ret.toString() == QLatin1String("library") && !pc->libraryOpen() && win->property("listOpen").toBool(), QStringLiteral("Esc：先收起计划库，列表还开着"), ret.toString());
        waitStill(panel);
        r.check(qAbs(panel->width() - narrowW) < 2 && statusesOnMap(mapc) == QSet<QString>{ QStringLiteral("planning") }, QStringLiteral("收起后地图回到计划模式只画计划中的"));
        pc->setLibraryOpen(true);
        pc->setMode(QStringLiteral("foot"));
        r.check(!pc->libraryOpen(), QStringLiteral("换模式时收起计划库"));
        pc->setLibraryOpen(true);
        r.check(!pc->libraryOpen(), QStringLiteral("只有计划模式能展开"));
        pc->setMode(QStringLiteral("plan"));
    }

    // ---- 入口：去计划库 ----
    {
        QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("foot")));
        waitFor([pc] { return pc->mode() == QLatin1String("foot"); }, 2000);
        QMetaObject::invokeMethod(win, "openLibrary");
        const bool ok = waitFor([pc] { return pc->libraryOpen(); }, 2000);
        r.check(ok && pc->mode() == QLatin1String("plan") && win->property("listOpen").toBool(), QStringLiteral("去计划库：从别的模式回到计划模式、列表展开"));
        pc->setLibraryOpen(false);
    }

    // ---- 想去 ----
    {
        QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("wish")));
        waitFor([pc] { return pc->mode() == QLatin1String("wish"); }, 2000);
        win->setProperty("listOpen", true);
        waitFrames(win, 3);
        QQuickItem *bar = item(win, QStringLiteral("wishMonthBar"));
        r.check(shown(bar) && shown(item(win, QStringLiteral("wishNewBtn"))) && !shown(item(win, QStringLiteral("listExpandBtn"))), QStringLiteral("想去：有月份条、新建想去，没有展开"));
        // 准备：3 个想去，第一个关联到季节表一行（找到 best 的月份），第二个关联完换到不合适的月份，第三个不关联
        const QVariantList seasons = pc->seasonRows();
        r.check(!seasons.isEmpty(), QStringLiteral("数据里有季节表"), QString::number(seasons.size()));
        QHash<QString, int> byGroup;
        for (const QVariant &v : listRows(pc)) byGroup[v.toMap().value(QStringLiteral("group")).toString()]++;
        QVariantMap one;
        for (const QVariant &v : listRows(pc)) if (v.toMap().value(QStringLiteral("group")).toString() == QLatin1String("noref")) { one = v.toMap(); break; }
        for (const QVariant &v : listRows(pc)) if (one.isEmpty()) one = v.toMap();
        r.info(QStringLiteral("这个月：合适 %1、其他 %2、没关联 %3").arg(byGroup.value(QStringLiteral("fit"))).arg(byGroup.value(QStringLiteral("other"))).arg(byGroup.value(QStringLiteral("noref"))));
        const int wishTotal = int(listRows(pc).size());
        r.check(wishTotal > 0, QStringLiteral("想去的列表有内容"));

        // 走一遍关联季节：对话框点保存（值直接设，下拉列表另有组件测）
        const QString wid = one.value(QStringLiteral("id")).toString();
        const QString oldRef = pc->planBasics(wid).value(QStringLiteral("seasonRef")).toString();
        waitFrames(win, 3);
        QQuickItem *seasonBtn = item(win, QStringLiteral("wishSeasonBtn-") + wid);
        QQuickItem *dlg = item(win, QStringLiteral("wishDialog"));
        if (seasonBtn) {
            QQuickItem *lv = item(win, QStringLiteral("planListView"));
            if (lv) QMetaObject::invokeMethod(lv, "positionViewAtEnd");
            waitFrames(win, 3);
            seasonBtn = item(win, QStringLiteral("wishSeasonBtn-") + wid);
            click(win, seasonBtn);
        }
        if (!shown(dlg)) QMetaObject::invokeMethod(dlg, "showSeason", Q_ARG(QVariant, wid));
        waitFor([dlg] { return dlg->opacity() > 0.5; }, 1500);
        const QString ref = seasons.first().toMap().value(QStringLiteral("key")).toString();
        r.check(shown(dlg) && dlg->property("kind").toString() == QLatin1String("season") && shown(item(win, QStringLiteral("wishSeason"))), QStringLiteral("关联季节：对话框"));
        dlg->setProperty("ref", ref);
        click(win, item(win, QStringLiteral("wishSave")));
        waitFor([dlg] { return !dlg->isVisible(); }, 1500);   // 淡完，不然后面点在遮罩上
        r.check(pc->planBasics(wid).value(QStringLiteral("seasonRef")).toString() == ref, QStringLiteral("关联季节：写进计划的 seasonRef"), ref);

        // 这一行的 12 格，换到合适的月 / 不合适的月
        QStringList kinds;
        for (const QVariant &v : listRows(pc)) if (idOf(v) == wid) kinds = v.toMap().value(QStringLiteral("kinds")).toStringList();
        int good = 0, bad = 0;
        for (int i = 0; i < kinds.size(); ++i) {
            if (!good && (kinds[i] == QLatin1String("best") || kinds[i] == QLatin1String("nice"))) good = i + 1;
            if (!bad && kinds[i] != QLatin1String("best") && kinds[i] != QLatin1String("nice")) bad = i + 1;
        }
        r.check(kinds.size() == 12 && shown(item(win, QStringLiteral("wishStrip-") + wid)), QStringLiteral("关联后列表这一行有季节条"));
        const auto groupOf = [pc, &wid] { for (const QVariant &v : listRows(pc)) if (idOf(v) == wid) return v.toMap().value(QStringLiteral("group")).toString(); return QString(); };
        if (good) {
            click(win, item(win, QStringLiteral("month-%1").arg(good)));
            waitFrames(win, 2);
            const map::TripPlan *t = tripOf(mapc, wid);
            const bool green = t && t->tone == 1;
            bool greenMark = t && t->stops.isEmpty();
            for (const map::MarkerLayer::Stop &s : mapc->markers()->stops())
                if (s.planId == wid && s.style.color == QLatin1String("#3f8a5c")) greenMark = true;
            r.check(pc->wishMonth() == good && groupOf() == QLatin1String("fit") && listRows(pc).size() == wishTotal, QStringLiteral("点月份：这个月合适的分到最前一组，一个都不少"), QStringLiteral("%1 月（要 %2）%3，%4 / %5 个").arg(pc->wishMonth()).arg(good).arg(groupOf()).arg(listRows(pc).size()).arg(wishTotal));
            r.check(green && greenMark, QStringLiteral("O2：这个月合适的标记是绿色"));
            r.check(idOf(listRows(pc).first()) == wid || listRows(pc).first().toMap().value(QStringLiteral("group")).toString() == QLatin1String("fit"), QStringLiteral("合适的一组在最前"));
        }
        if (bad) {
            pc->setWishMonth(bad);
            const map::TripPlan *t = tripOf(mapc, wid);
            r.check(groupOf() == QLatin1String("other") && t && t->tone == 0, QStringLiteral("不合适的月份：分到\"其他月份\"，标记不变绿"));
        }
        // 没关联的：一直在、地图上淡
        pc->setSeasonRef(wid, QString());
        waitFor([mapc, &wid] { const map::TripPlan *x = tripOf(mapc, wid); return x && x->tone == -1; }, 1500);   // 改数据后地图隔 60 ms 才重算
        const map::TripPlan *t = tripOf(mapc, wid);
        const QString rawColor = plans::planById(store->data(), wid)->operator[](QStringLiteral("color")).toString();
        r.check(groupOf() == QLatin1String("noref") && t && t->tone == -1, QStringLiteral("没关联的：单独一组、地图上画淡一点（PO 第 2 题 A）"));
        r.check(t && (rawColor.isEmpty() || t->color != rawColor) && plans::planById(store->data(), wid)->operator[](QStringLiteral("color")).toString() == rawColor,
                QStringLiteral("画淡只改这次画的颜色，数据里的颜色没变"), rawColor);
        // 撤销这两步，数据回到原样
        app->undo();
        app->undo();
        r.check(pc->planBasics(wid).value(QStringLiteral("seasonRef")).toString() == oldRef, QStringLiteral("撤销后 seasonRef 回到原样"));

        // 开始计划
        QMetaObject::invokeMethod(dlg, "showStart", Q_ARG(QVariant, wid));
        waitFor([dlg] { return dlg->opacity() > 0.5; }, 1500);
        r.check(shown(item(win, QStringLiteral("wishStart"))) && shown(item(win, QStringLiteral("wishEnd"))), QStringLiteral("开始计划：对话框有出发和回来"));
        dlg->setProperty("start", QStringLiteral("2030-05-10"));
        dlg->setProperty("end", QStringLiteral("2030-05-01"));
        waitFrames(win, 1);
        r.check(!dlg->property("open").toBool() == false && dlg->property("badRange").toBool(), QStringLiteral("回来早于出发：不能保存"));
        dlg->setProperty("end", QString());
        click(win, item(win, QStringLiteral("wishSave")));
        waitFor([dlg] { return !dlg->property("open").toBool(); }, 1000);
        const Value *wp = plans::planById(store->data(), wid);
        r.check(wp && plans::statusOf(*wp) == QLatin1String("planning") && (*wp)[QStringLiteral("startDate")].toString() == QLatin1String("2030-05-10")
                    && (*wp)[QStringLiteral("endDate")].toString() == QLatin1String("2030-05-10"), QStringLiteral("开始计划：改成计划中，只填出发时回来等于出发"));
        r.check(listRows(pc).size() == wishTotal - 1, QStringLiteral("开始计划后从想去的列表里走了"));
        app->undo();
        r.check(statusOfPlan(store, wid) == QLatin1String("wish"), QStringLiteral("开始计划一步撤销"));

        // 新建想去（等上一个对话框淡完，不然点在它的遮罩上）
        waitFor([dlg] { return !dlg->isVisible(); }, 1500);
        PlanEditor *ed = editorCtl();
        click(win, item(win, QStringLiteral("wishNewBtn")));
        waitFor([ed] { return ed && ed->isOpen(); }, 2000);
        r.check(ed && ed->isOpen() && ed->property("plan").toMap().value(QStringLiteral("status")).toString() == QLatin1String("wish"), QStringLiteral("新建想去：编辑器打开，状态是想去"));
        if (ed) QMetaObject::invokeMethod(ed, "discard");
        waitFor([ed] { return ed && !ed->isOpen(); }, 2000);
        const QDate today = QDate::currentDate();
        pc->setWishMonth(today.month());
    }

    // ---- 足迹 ----
    {
        QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("foot")));
        waitFor([pc] { return pc->mode() == QLatin1String("foot"); }, 2000);
        win->setProperty("listOpen", true);
        waitFor([mapc] { return mapc->unlockReady(); }, 8000);
        waitFrames(win, 3);
        QQuickItem *stat = item(win, QStringLiteral("footStat"));
        const QString text = stat ? stat->property("text").toString() : QString();
        int done = 0;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray()) if (plans::statusOf(p) == QLatin1String("completed")) ++done;
        r.check(shown(stat) && text.contains(QString::number(done)) && text.contains(QString::number(mapc->unlockProvinces())) && mapc->unlockProvinces() > 0,
                QStringLiteral("足迹：上面一行统计（N 趟 · N 个地区，和足迹框同一个数）"), text);
        bool years = !listRows(pc).isEmpty();
        for (const QVariant &v : listRows(pc)) { const QString g = v.toMap().value(QStringLiteral("group")).toString(); years = years && (g.startsWith(u'y') || g == QLatin1String("undated")); }
        r.check(years, QStringLiteral("足迹：按年份分组"));
        win->setProperty("listOpen", false);
        QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("plan")));
        waitFor([pc] { return pc->mode() == QLatin1String("plan"); }, 2000);
    }
    app->noteChanged(false);
    pc->setTestAllStatuses(true);
}

} // namespace tp::app::selftest
