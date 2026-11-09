// 自检：0.12.7 欢迎页、新建数据带示例计划（往返、删除）、6 页空状态的显示和点动作。
// 中途换到临时文件夹的数据（设置不记住，测试运行不写 dataDir），最后换回原来的副本。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "jsonio.h"
#include "plancontroller.h"
#include "planeditor.h"
#include "ratescontroller.h"

#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QVariant>
#include <qpa/qwindowsysteminterface.h>

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
QQuickItem *item(QQuickWindow *win, const QString &name) { return findItem(win->contentItem(), name); }
// 一路上的父项都看得见（比 isVisible 多查一层透明度：整页淡出时子项 isVisible 还是真的）
bool shown(QQuickItem *it)
{
    if (!it || !it->isVisible()) return false;
    for (QQuickItem *p = it; p; p = p->parentItem())
        if (p->opacity() < 0.5) return false;
    return true;
}
void click(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return;
    waitStill(it, 1000);   // 0.12.11：面板滑进、列表滚动时先等停稳再点，不然点在半路上
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    const QPointF g = win->mapToGlobal(p);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, devPx(win, p), devPx(win, g), Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, devPx(win, p), devPx(win, g), Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    pause(120);
}
void goPage(QQuickWindow *win, const char *page)
{
    QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QString::fromLatin1(page)));
}
} // namespace

void runWelcome(Report &r, AppController *app, QQuickWindow *win)
{
    r.section(QStringLiteral("欢迎页、示例计划、空状态（0.12.7）"));
    PlanController *pc = plansCtl();
    data::DataStore *store = app->store();
    const QString original = store->dataDir();
    const QString currencyWas = ratesCtl()->main();   // 点"开始"会按系统地区猜主货币，最后还原
    QTemporaryDir tmp;
    if (!r.check(pc && tmp.isValid(), QStringLiteral("准备临时文件夹"))) return;

    pc->setTestAllStatuses(false);   // 和真实使用一样：每个模式只有自己状态的计划

    // ---- 欢迎页 ----
    QQuickItem *page = item(win, QStringLiteral("welcomePage"));
    if (!r.check(page != nullptr, QStringLiteral("欢迎页在"))) return;
    r.check(!page->isVisible(), QStringLiteral("已经有数据时不显示欢迎页"));
    page->setProperty("forced", true);
    waitFor([&] { return page->opacity() > 0.99; }, 2000);
    QQuickItem *dirText = item(win, QStringLiteral("welcomeDir"));
    r.check(shown(dirText) && dirText->property("text").toString() == app->defaultDataDir(), QStringLiteral("数据位置卡片默认是建议的文件夹"),
            dirText ? dirText->property("text").toString() : QString());
    r.check(win->property("modalOpen").toBool(), QStringLiteral("欢迎页开着时快捷键不触发（算弹框）"));
    QQuickItem *title = item(win, QStringLiteral("welcomeTitle"));
    const QString langWas = app->lang();
    app->setLang(QStringLiteral("en"));
    r.check(title && title->property("text").toString() == QLatin1String("Travel Planner"), QStringLiteral("切到英文：标题跟着变"), title ? title->property("text").toString() : QString());
    app->setLang(QStringLiteral("zh"));
    r.check(title && title->property("text").toString() == QStringLiteral("旅行计划"), QStringLiteral("切回中文"));

    // "开始"：在选的文件夹里建数据（真点击）
    const QString dirZh = tmp.filePath(QStringLiteral("新建数据"));
    page->setProperty("dir", dirZh);
    click(win, item(win, QStringLiteral("welcomeStart")));
    const bool created = waitFor([&] { return store->dataDir() == dirZh; }, 3000);
    r.check(created && QFile::exists(dirZh + QStringLiteral("/travel.json")) && QFile::exists(dirZh + QStringLiteral("/photos")), QStringLiteral("点开始：建好 travel.json 和 photos，打开这份数据"));
    page->setProperty("forced", false);
    waitFor([&] { return !page->isVisible(); }, 2000);
    r.check(!page->isVisible(), QStringLiteral("开始之后欢迎页消失"));

    // ---- 示例计划 ----
    const auto planArr = [&] { return store->data()[QStringLiteral("plans")].asArray(); };
    r.check(planArr().size() == 1 && planArr()[0][QStringLiteral("name")].toString().contains(QStringLiteral("示例")), QStringLiteral("新数据里有一个中文示例计划"));
    r.check(store->data()[QStringLiteral("seasonGuide")][QStringLiteral("rows")].asArray().size() > 10, QStringLiteral("新数据里有季节表"));
    const QString planId = planArr().empty() ? QString() : planArr()[0][QStringLiteral("id")].toString();
    goPage(win, "plan");
    waitFor([pc] { return pc->mode() == QLatin1String("plan"); }, 2000);
    win->setProperty("listOpen", true);
    r.check(waitFor([pc] { return pc->list().value(QStringLiteral("rows")).toList().size() == 1; }, 2000), QStringLiteral("计划列表里有示例计划"));
    const QString before = json::stringify(store->data()[QStringLiteral("plans")], 2);
    r.check(store->save(false).ok, QStringLiteral("保存一次"));
    data::DataStore again;
    r.check(again.load(dirZh) && json::stringify(again.data()[QStringLiteral("plans")], 2) == before, QStringLiteral("往返不变：写到硬盘再读回来，计划一样"));

    // 想去和足迹：示例是计划中，这两页也算空（不是"筛选下没有"）
    win->setProperty("listOpen", false);
    goPage(win, "wish");
    r.check(waitFor([&] { return shown(item(win, QStringLiteral("emptyWish"))); }, 2000), QStringLiteral("只有示例计划时：进想去自动打开列表，显示空状态"));
    goPage(win, "foot");
    r.check(waitFor([&] { return shown(item(win, QStringLiteral("emptyFoot"))); }, 2000), QStringLiteral("只有示例计划时：足迹显示空状态"));

    // 日历：示例没定日期，算空，但账本要等有花费
    goPage(win, "calendar");
    QQuickItem *cal = item(win, QStringLiteral("calendarPage"));
    r.check(waitFor([&] { return shown(item(win, QStringLiteral("emptyCalendar"))); }, 3000), QStringLiteral("示例计划没定日期：日历显示空状态"));

    // 删掉示例计划和普通计划一样
    goPage(win, "plan");
    waitFor([pc] { return pc->mode() == QLatin1String("plan"); }, 2000);
    r.check(pc->deletePlan(planId) && planArr().empty(), QStringLiteral("删掉示例计划"));

    // ---- 6 页空状态：看得见、点了有动作 ----
    PlanEditor *ed = editorCtl();
    const auto closeEditor = [&] { if (ed) ed->discard(); pause(150); };

    win->setProperty("listOpen", true);
    r.check(waitFor([&] { return shown(item(win, QStringLiteral("emptyPlanList"))); }, 2000), QStringLiteral("计划列表：空状态"));
    QQuickItem *panel = item(win, QStringLiteral("planList"));
    waitStill(panel);
    QMetaObject::invokeMethod(win, "openLibrary");
    r.check(waitFor([&] { return pc->libraryOpen() && shown(item(win, QStringLiteral("emptyLibrary"))); }, 3000), QStringLiteral("计划库：空状态"));
    waitStill(panel);
    click(win, item(win, QStringLiteral("emptyLibraryBtn")));
    r.check(waitFor([&] { return ed && ed->isOpen(); }, 2000), QStringLiteral("计划库：点\"新建计划\"打开编辑器"));
    closeEditor();
    pc->setLibraryOpen(false);

    goPage(win, "wish");
    waitFor([&] { return shown(item(win, QStringLiteral("emptyWishBtn"))); }, 2000);
    waitStill(panel);
    click(win, item(win, QStringLiteral("emptyWishBtn")));
    r.check(waitFor([&] { return ed && ed->isOpen() && ed->property("plan").toMap().value(QStringLiteral("status")).toString() == QLatin1String("wish"); }, 2000), QStringLiteral("想去：点动作新建想去"));
    closeEditor();

    goPage(win, "foot");
    waitFor([&] { return shown(item(win, QStringLiteral("emptyFootBtn"))); }, 2000);
    waitStill(panel);
    click(win, item(win, QStringLiteral("emptyFootBtn")));
    r.check(waitFor([&] { return ed && ed->isOpen() && ed->property("plan").toMap().value(QStringLiteral("status")).toString() == QLatin1String("completed"); }, 2000), QStringLiteral("足迹：点动作新建已完成的计划"));
    closeEditor();

    goPage(win, "calendar");
    waitFor([&] { return shown(item(win, QStringLiteral("emptyCalendarBtn"))); }, 3000);
    if (cal) waitStill(cal);
    click(win, item(win, QStringLiteral("emptyCalendarBtn")));
    r.check(waitFor([&] { return ed && ed->isOpen(); }, 2000), QStringLiteral("日历：点\"新建计划\"打开编辑器"));
    closeEditor();

    goPage(win, "ledger");
    r.check(waitFor([&] { return shown(item(win, QStringLiteral("ledgerEmptyAll"))); }, 3000), QStringLiteral("账本：空状态"));
    waitStill(item(win, QStringLiteral("ledgerPage")));
    click(win, item(win, QStringLiteral("ledgerEmptyAllBtn")));
    r.check(waitFor([&] { return pc->mode() == QLatin1String("plan") && pc->libraryOpen(); }, 3000), QStringLiteral("账本：点\"打开计划库\"到计划库"));
    pc->setLibraryOpen(false);

    goPage(win, "photo");
    win->setProperty("listOpen", true);
    win->setProperty("albumOpen", true);
    r.check(waitFor([&] { return shown(item(win, QStringLiteral("emptyAlbum"))); }, 3000), QStringLiteral("相册：空状态"));
    QQuickItem *albumBtn = item(win, QStringLiteral("emptyAlbumBtn"));
    r.check(shown(albumBtn) && albumBtn->property("text").toString() == QStringLiteral("导入照片"), QStringLiteral("相册：动作是导入照片（真的选文件框不在自检里点）"));
    win->setProperty("albumOpen", false);

    // ---- 英文示例 ----
    const QString dirEn = tmp.filePath(QStringLiteral("en"));
    app->setLang(QStringLiteral("en"));
    const QVariantMap en = app->welcomeStart(dirEn);
    r.check(en.value(QStringLiteral("ok")).toBool() && planArr().size() == 1 && !planArr()[0][QStringLiteral("name")].toString().contains(QStringLiteral("示例")), QStringLiteral("英文界面新建：示例计划是英文的"));
    app->setLang(langWas);

    ratesCtl()->setMain(currencyWas);

    // 换回原来的副本
    const QVariantMap back = app->switchDataFolder(original, false);
    r.check(back.value(QStringLiteral("ok")).toBool() && store->dataDir() == original, QStringLiteral("换回原来的数据"));
    goPage(win, "plan");
    waitFor([pc] { return pc->mode() == QLatin1String("plan"); }, 2000);
    win->setProperty("listOpen", false);
    pause(150);
}

// 0.12.8 新手引导（问一句 → 4 步 → 记下来）、Esc 层级、报错走横幅不走 Toast、升级提示、不见了的数据文件
void runTour(Report &r, AppController *app, QQuickWindow *win)
{
    r.section(QStringLiteral("新手引导和提示（0.12.8）"));
    goPage(win, "plan");
    pause(300);
    QQuickItem *tour = item(win, QStringLiteral("tour"));
    QQuickItem *offer = findItem(tour, QStringLiteral("tourOffer"));
    QQuickItem *toast = item(win, QStringLiteral("toast"));
    QQuickItem *err = item(win, QStringLiteral("errorBanner"));
    const bool wasDone = app->tourDone();
    const auto esc = [&] { QVariant v; QMetaObject::invokeMethod(win, "escOnce", Q_RETURN_ARG(QVariant, v)); return v.toString(); };

    // 问一句：Esc 算跳过，记下来
    app->setTourDone(false);
    QMetaObject::invokeMethod(tour, "offer");
    r.check(waitFor([&] { return shown(offer); }, 800) && !win->property("modalOpen").toBool(), QStringLiteral("小卡片问要不要看，不挡地图"));
    r.check(esc() == QLatin1String("tourOffer") && !tour->property("offering").toBool() && app->tourDone(), QStringLiteral("Esc = 跳过，记下来"));

    // 点"看看"开始；4 步都在，洞对准按钮
    app->setTourDone(false);
    QMetaObject::invokeMethod(tour, "offer");
    pause(200);
    click(win, findItem(tour, QStringLiteral("tourOfferGo")));
    r.check(waitFor([&] { return tour->property("active").toBool(); }, 800) && win->property("modalOpen").toBool(), QStringLiteral("点看看开始，算弹框"));
    r.check(!app->tourDone(), QStringLiteral("还没看完不记"));
    pause(500);
    const auto holeOn = [&](const char *name) {
        QQuickItem *t = item(win, QString::fromLatin1(name));
        const QRectF rc = tour->property("holeRect").toRectF();
        const QPointF p = t->mapToItem(tour, QPointF(t->width() / 2, t->height() / 2));
        return rc.contains(p);
    };
    r.check(holeOn("dotsPill"), QStringLiteral("第 1 步指 ⋯"));
    click(win, findItem(tour, QStringLiteral("tourNext")));
    r.check(waitFor([&] { return tour->property("index").toInt() == 1; }, 500) && holeOn("newBtn"), QStringLiteral("第 2 步指新建计划"));
    click(win, findItem(tour, QStringLiteral("tourNext")));
    r.check(waitFor([&] { return tour->property("index").toInt() == 2; }, 500) && holeOn("layers"), QStringLiteral("第 3 步指图层"));
    click(win, findItem(tour, QStringLiteral("tourNext")));
    r.check(waitFor([&] { return tour->property("index").toInt() == 3; }, 500) && holeOn("locBtn") && tour->property("last").toBool(), QStringLiteral("第 4 步指我的位置，是最后一步"));
    click(win, findItem(tour, QStringLiteral("tourNext")));
    r.check(waitFor([&] { return !tour->property("active").toBool(); }, 500) && app->tourDone(), QStringLiteral("看完记下来"));

    // 进行中 Esc 结束
    QMetaObject::invokeMethod(tour, "start");
    pause(200);
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(win, QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(win, QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
    r.check(waitFor([&] { return !tour->property("active").toBool(); }, 500), QStringLiteral("进行中 Esc 结束"));

    // 报错走横幅，Toast 不显示
    toast->setProperty("showing", false);
    QMetaObject::invokeMethod(toast, "show", Q_ARG(QVariant, QStringLiteral("试试报错")), Q_ARG(QVariant, true), Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
    r.check(waitFor([&] { return shown(err); }, 500) && err->property("title").toString() == QStringLiteral("试试报错") && !toast->property("showing").toBool(), QStringLiteral("报错进横幅，不走 Toast"));
    QMetaObject::invokeMethod(win, "clearError");
    r.check(waitFor([&] { return !shown(err); }, 500), QStringLiteral("横幅能关"));

    // 升级提示：12 秒，点了打开更新日志
    QMetaObject::invokeMethod(win, "showUpdated");
    r.check(toast->property("showing").toBool() && toast->property("message").toString().contains(app->version()), QStringLiteral("升级提示写版本号"));
    r.check(toast->property("stayMs").toInt() == 12000, QStringLiteral("停 12 秒"));
    pause(300);
    click(win, findItem(toast, QStringLiteral("toastAction")));
    QQuickItem *settings = item(win, QStringLiteral("settingsPanel"));
    r.check(waitFor([&] { return settings->property("section").toString() == QLatin1String("log") && shown(settings); }, 1500), QStringLiteral("点了打开更新日志"));
    settings->setProperty("section", QStringLiteral("about"));
    goPage(win, "plan");
    pause(300);

    // 图标按钮的提示：鼠标停上去出现，画在窗口最上层
    {
        QQuickItem *loc = item(win, QStringLiteral("locBtn"));
        const QPointF p = loc->mapToScene(QPointF(loc->width() / 2, loc->height() / 2));
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, devPx(win, p), devPx(win, win->mapToGlobal(p)), Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
        const auto tipShown = [&] {
            for (QQuickItem *c : win->contentItem()->childItems())
                if (c->objectName() == QLatin1String("tpTip") && c->isVisible()) return true;
            return false;
        };
        r.check(waitFor(tipShown, 1500), QStringLiteral("我的位置：鼠标提示出现"));
        const QPointF away(win->width() / 2.0, win->height() / 2.0);
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, devPx(win, away), devPx(win, win->mapToGlobal(away)), Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
        r.check(waitFor([&] { return !tipShown(); }, 800), QStringLiteral("移开就消失"));
    }

    // 账本：滚过表头以后上面钉一份
    goPage(win, "ledger");
    QQuickItem *flick = item(win, QStringLiteral("ledgerFlick"));
    QQuickItem *pin = item(win, QStringLiteral("ledgerPinnedHead"));
    waitFor([&] { return shown(flick); }, 1500);
    r.check(pin && !pin->isVisible(), QStringLiteral("账本在最上面时不钉表头"));
    if (flick) flick->setProperty("contentY", flick->property("contentHeight").toDouble());
    pause(200);
    r.check(flick && flick->property("contentHeight").toDouble() <= flick->height() || (pin && pin->isVisible()), QStringLiteral("账本滚下去后表头钉在上面"));
    if (flick) flick->setProperty("contentY", 0);
    goPage(win, "plan");
    pause(200);

    if (!wasDone) app->setTourDone(false);
}

} // namespace tp::app::selftest
