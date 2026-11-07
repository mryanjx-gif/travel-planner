// 自检：0.10.4 行李与待办、紧急信息、游记、攻略。都在数据副本上做，最后把数据改回去。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "jsonio.h"
#include "mapcontroller.h"
#include "navhistory.h"
#include "plancontroller.h"
#include "planpagecontroller.h"
#include "plans.h"
#include "netclient.h"
#include "netcontroller.h"
#include "photocontroller.h"
#include "ratescontroller.h"
#include "settings.h"
#include "thumbcache.h"
#include "tilecache.h"
#include "weathercontroller.h"

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
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
bool effectivelyShown(QQuickItem *it) { return it && it->isVisible() && it->opacity() > 0.5; }
void click(QQuickWindow *win, QQuickItem *it)
{
    if (!it) return;
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    const QPointF g = win->mapToGlobal(p);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(win, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    pause(80);
}
QVariantList listOf(PlanPageController *page, const char *key) { return page->info().value(QStringLiteral("kit")).toMap().value(QString::fromLatin1(key)).toList(); }
QStringList idsOf(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &v : rows) out << v.toMap().value(QStringLiteral("id")).toString();
    return out;
}
} // namespace

void runKit(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav)
{
    r.section(QStringLiteral("0.10.4 行李与待办、紧急信息、游记、攻略"));
    PlanController *pc = plansCtl();
    data::DataStore *store = app->store();
    const auto item = [&](const QString &name) { return findItem(win->contentItem(), name); };
    QQuickItem *panel = item(QStringLiteral("planPage"));
    if (!r.check(pc && page && nav && panel, QStringLiteral("计划页在"))) return;
    pc->setTestAllStatuses(false);
    const QVariantMap demo = pc->demo();
    const QString id = demo.value(QStringLiteral("planId")).toString();
    if (!r.check(!id.isEmpty(), QStringLiteral("有带日期的路线"))) return;
    const QString today = demo.value(QStringLiteral("today")).toString();
    pc->setTodayOverride(today);
    const Value before = store->data();
    const QString soon = QDate::fromString(today, Qt::ISODate).addDays(2).toString(Qt::ISODate);

    // 干净的起点：计划中、两张清单空、没有模板、没有游记、没有攻略
    store->modify([&](Value &d) {
        d.mutObject().remove(QStringLiteral("packingTemplate"));
        d.mutObject().remove(QStringLiteral("emergency"));
        Value *p = plans::mutPlan(d, id);
        json::Object &o = p->mutObject();
        o.set(QStringLiteral("status"), Value(QStringLiteral("planning")));
        for (const char *k : { "packing", "todos", "localEmergency", "guide" }) o.remove(QString::fromLatin1(k));
        json::Array &j = d.mutObject().find(QStringLiteral("journal"))->mutArray();
        j.erase(std::remove_if(j.begin(), j.end(), [&](const Value &x) { return x[QStringLiteral("plan")].toString() == id; }), j.end());
    });
    page->show(id);
    page->seedKitForShot();   // 借一张照片给这个计划（下面测插入照片）；顺手写的紧急信息和游记再清掉
    page->show(QString());
    store->modify([&](Value &d) {
        d.mutObject().remove(QStringLiteral("emergency"));
        Value *p = plans::mutPlan(d, id);
        p->mutObject().remove(QStringLiteral("localEmergency"));
        json::Array &j = d.mutObject().find(QStringLiteral("journal"))->mutArray();
        j.erase(std::remove_if(j.begin(), j.end(), [&](const Value &x) { return x[QStringLiteral("plan")].toString() == id; }), j.end());
    });
    QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
    waitFor([&] { return nav->page() == QLatin1String("trip") && page->planId() == id; }, 3000);
    waitFor([&] { return panel->property("shown").toBool() && panel->x() < win->width() - panel->width() / 2; }, 2000);
    pause(300);

    // ---- 标签 ----
    for (const char *t : { "itinerary", "bookings", "packing", "budget", "journal", "guide" })
        r.check(item(QStringLiteral("tripTab-") + QLatin1String(t)) != nullptr, QStringLiteral("有标签 %1").arg(QLatin1String(t)));
    click(win, item(QStringLiteral("tripTab-packing")));
    r.check(waitFor([&] { return panel->property("tab").toString() == QLatin1String("packing"); }, 1000), QStringLiteral("点标签打开行李与待办"));
    r.check(effectivelyShown(item(QStringLiteral("tripPacking"))), QStringLiteral("行李与待办显示了"));
    QQuickItem *packCard = item(QStringLiteral("kitCard-packing"));
    QQuickItem *todoCard = item(QStringLiteral("kitCard-todos"));
    r.check(packCard && todoCard && qAbs(packCard->y() - todoCard->y()) < 1 && todoCard->x() > packCard->x() + packCard->width() - 1, QStringLiteral("宽窗口左右两列"));

    // ---- 模板、默认 11 项、去重 ----
    click(win, item(QStringLiteral("kitTplAdd")));
    r.check(waitFor([&] { return listOf(page, "packing").size() == 11; }, 1000), QStringLiteral("从模板添加：默认 11 项"), QString::number(listOf(page, "packing").size()));
    click(win, item(QStringLiteral("kitTplAdd")));
    pause(100);
    r.check(listOf(page, "packing").size() == 11, QStringLiteral("再点一次不重复"));

    // ---- M1：勾了沉到底，数据顺序不变 ----
    const QStringList order = idsOf(listOf(page, "packing"));
    QQuickItem *row0 = item(QStringLiteral("kitRow-") + order[0]);
    QQuickItem *box0 = item(QStringLiteral("kitBox-") + order[0]);
    const qreal y0 = row0 ? row0->y() : -1;
    click(win, box0);
    r.check(waitFor([&] { return idsOf(listOf(page, "packing")).last() == order[0]; }, 1000), QStringLiteral("点勾选框：这一行排到最后"));
    r.check(row0 && item(QStringLiteral("kitRow-") + order[0]) == row0, QStringLiteral("这一行没有重建（挪过去的）"));
    r.check(waitFor([&] { return row0 && row0->y() > y0 + 50; }, 1500), QStringLiteral("画面上也沉到底了"), row0 ? QString::number(row0->y()) : QString());
    {
        const Value *p = plans::planById(store->data(), id);
        const json::Array &a = (*p)[QStringLiteral("packing")].asArray();
        r.check(a[0][QStringLiteral("id")].toString() == order[0] && a[0][QStringLiteral("done")].toBool(), QStringLiteral("数据顺序没变，done 写进去了"));
    }
    r.check(page->info().value(QStringLiteral("kit")).toMap().value(QStringLiteral("packDone")).toInt() == 1, QStringLiteral("进度 1 / 11"));
    if (QQuickItem *c = item(QStringLiteral("kitCount-packing"))) r.check(c->property("text").toString() == QLatin1String("1 / 11"), QStringLiteral("卡片上写着进度"), c->property("text").toString());
    if (QQuickItem *b = item(QStringLiteral("kitBar-packing"))) r.check(b->width() > 0 && b->width() < packCard->width() / 3, QStringLiteral("进度条有长度"), QString::number(b->width()));
    if (QQuickItem *t = item(QStringLiteral("tripTab-packing"))) r.check(t->property("n").toString() == QLatin1String("1/11"), QStringLiteral("标签上的数字"), t->property("n").toString());
    page->setCheckDone(QStringLiteral("packing"), order[0], false);
    r.check(idsOf(listOf(page, "packing")).first() == order[0], QStringLiteral("取消勾选：回到原来的位置"));

    // ---- 删除、存成模板 ----
    r.check(page->removeCheck(QStringLiteral("packing"), order[1]), QStringLiteral("删除一条"));
    r.check(listOf(page, "packing").size() == 10 && !item(QStringLiteral("kitRow-") + order[1]), QStringLiteral("删掉的行不见了"));
    click(win, item(QStringLiteral("kitTplSave")));
    QQuickItem *tplDlg = item(QStringLiteral("tplDialog"));
    r.check(waitFor([&] { return tplDlg && tplDlg->property("open").toBool(); }, 1000), QStringLiteral("存成模板先问一句"));
    if (tplDlg) {
        QMetaObject::invokeMethod(tplDlg, "chosen", Q_ARG(QString, QStringLiteral("ok")));
        waitFor([&] { return !tplDlg->isVisible(); }, 1000);   // 淡出完才点得到下面
    }
    r.check(store->data()[QStringLiteral("packingTemplate")].asArray().size() == 10, QStringLiteral("模板存了 10 项"));
    r.check(page->info().value(QStringLiteral("kit")).toMap().value(QStringLiteral("tplCount")).toInt() == 10, QStringLiteral("模板项数显示跟上了"));

    // ---- Enter 添加、M4 ----
    QQuickItem *cardTodos = todoCard;
    r.check(page->addChecks(QStringLiteral("todos"), QStringLiteral("办签证")) == 1, QStringLiteral("加一条待办"));
    cardTodos->setProperty("draft", QStringLiteral("换汇\n\n- 订租车\n* [ ] 打疫苗"));
    QQuickItem *addBtn = item(QStringLiteral("kitAdd-todos"));
    r.check(addBtn && addBtn->property("text").toString().contains(QLatin1Char('3')), QStringLiteral("粘贴多行：按钮写加 3 条"), addBtn ? addBtn->property("text").toString() : QString());
    QQuickItem *cancelBtn = item(QStringLiteral("kitCancel-todos"));
    r.check(effectivelyShown(cancelBtn), QStringLiteral("有取消"));
    waitFrames(win, 2);   // 按钮刚出现，等排好位置再点
    click(win, cancelBtn);
    const bool cleared = waitFor([&] { return cardTodos->property("draft").toString().isEmpty(); }, 500);
    r.check(cleared && listOf(page, "todos").size() == 1, QStringLiteral("取消：什么都没加"),
            QStringLiteral("清空 %1，%2 条，按钮 %3,%4 %5×%6").arg(cleared).arg(listOf(page, "todos").size()).arg(cancelBtn->mapToScene(QPointF(0, 0)).x()).arg(cancelBtn->mapToScene(QPointF(0, 0)).y()).arg(cancelBtn->width()).arg(cancelBtn->height()));
    cardTodos->setProperty("draft", QStringLiteral("换汇\n\n- 订租车\n* [ ] 打疫苗"));
    click(win, addBtn);
    r.check(waitFor([&] { return listOf(page, "todos").size() == 4; }, 1000), QStringLiteral("一次加了 3 条"), QString::number(listOf(page, "todos").size()));
    r.check(listOf(page, "todos").value(3).toMap().value(QStringLiteral("text")).toString() == QStringLiteral("打疫苗"), QStringLiteral("行首的记号去掉了"));
    r.check(cardTodos->property("draft").toString().isEmpty(), QStringLiteral("加完输入框空了"));

    // ---- 截止日期、M2 ----
    const QStringList todoIds = idsOf(listOf(page, "todos"));
    r.check(page->setCheckDue(todoIds[0], soon), QStringLiteral("写截止日期"));
    r.check(page->setCheckDue(todoIds[1], QStringLiteral("2020-01-01")), QStringLiteral("写一个已过期的"));
    r.check(!page->setCheckDue(todoIds[2], QStringLiteral("下周")), QStringLiteral("认不出的日期不写"));
    QQuickItem *badge = nullptr;
    if (QQuickItem *t = item(QStringLiteral("tripTab-packing"))) badge = findItem(t, QStringLiteral("tripTabDue"));
    r.check(badge && badge->isVisible(), QStringLiteral("标签上出现提醒"));
    r.check(effectivelyShown(item(QStringLiteral("kitDueNotice"))), QStringLiteral("待办卡片上写着几条要到期"));
    QQuickItem *due0 = item(QStringLiteral("kitDue-") + todoIds[0]);
    QQuickItem *due1 = item(QStringLiteral("kitDue-") + todoIds[1]);
    r.check(due0 && due1 && due0->property("color") != due1->property("color"), QStringLiteral("快到期和已过期颜色不同"));
    // 只在计划中的计划上
    store->modify([&](Value &d) { plans::mutPlan(d, id)->mutObject().set(QStringLiteral("status"), Value(QStringLiteral("wish"))); });
    r.check(waitFor([&] { return badge && !badge->isVisible(); }, 1000), QStringLiteral("想去的计划不提醒"));
    store->modify([&](Value &d) { plans::mutPlan(d, id)->mutObject().set(QStringLiteral("status"), Value(QStringLiteral("planning"))); });
    page->setCheckDone(QStringLiteral("todos"), todoIds[0], true);
    r.check(page->info().value(QStringLiteral("kit")).toMap().value(QStringLiteral("dueSoon")).toInt() == 0, QStringLiteral("勾完就不提醒了"));
    page->setCheckDone(QStringLiteral("todos"), todoIds[0], false);

    // ---- 紧急信息 ----
    QQuickItem *emDlg = item(QStringLiteral("emergencyDialog"));
    QQuickItem *tripList = item(QStringLiteral("tripList"));   // 卡片在清单下面，先滚到底
    tripList->setProperty("contentY", tripList->property("originY").toReal() + tripList->property("contentHeight").toReal() - tripList->height());
    waitFrames(win, 2);
    click(win, item(QStringLiteral("kitEditLocal")));
    r.check(waitFor([&] { return emDlg && emDlg->property("open").toBool() && emDlg->property("mode").toString() == QLatin1String("local"); }, 1000), QStringLiteral("改当地电话是弹出表单"));
    QMetaObject::invokeMethod(emDlg, "set", Q_ARG(QVariant, QStringLiteral("报警")), Q_ARG(QVariant, QStringLiteral("000")));
    pause(50);
    click(win, item(QStringLiteral("emOk")));
    r.check(waitFor([&] { return !emDlg->property("open").toBool(); }, 1000), QStringLiteral("保存后关上"));
    waitFor([&] { return !emDlg->isVisible(); }, 1000);   // 淡出完才点得到下面
    {
        const QVariantMap l = listOf(page, "local").value(0).toMap();
        r.check(l.value(QStringLiteral("label")).toString() == QStringLiteral("报警") && l.value(QStringLiteral("phone")).toString() == QLatin1String("000"), QStringLiteral("当地电话写进这个计划"));
    }
    // 卡片多了一行，列表变高：再滚到底，等排好
    tripList->setProperty("contentY", tripList->property("originY").toReal() + tripList->property("contentHeight").toReal() - tripList->height());
    waitFrames(win, 3);
    pause(700);   // 差异清单 #26：同一块地方连点太快，第二下可能不生效
    // 0.12.3 占位：紧急信息卡旁边、⋯ 菜单里的导出（信号直接发，不靠滚动位置）
    {
        QQuickItem *t = item(QStringLiteral("toast"));
        const auto isStub = [t] { return t && t->property("showing").toBool() && t->property("message").toString().size() > 0; };
        t->setProperty("showing", false);
        click(win, item(QStringLiteral("kitExportCard")));
        r.check(waitFor(isStub, 800), QStringLiteral("紧急信息卡旁边的导出信息卡：提示还在做"));
        QStringList missing;
        for (const char *k : { "Pdf", "Image", "Ics", "Trip", "Share", "Card" }) {
            QQuickItem *row = item(QStringLiteral("tripExport") + QLatin1String(k));
            t->setProperty("showing", false);
            if (row) QMetaObject::invokeMethod(row, "clicked");
            if (!row || !waitFor(isStub, 500)) missing << QLatin1String(k);
        }
        r.check(missing.isEmpty(), QStringLiteral("⋯ 菜单 6 个导出入口：提示还在做"), missing.join(QLatin1Char(' ')));
        r.check(item(QStringLiteral("tripInCalendar")) != nullptr, QStringLiteral("⋯ 菜单的在日历里看还在"));
        t->setProperty("showing", false);
        pause(700);
    }
    click(win, item(QStringLiteral("kitEditGlobal")));
    r.check(waitFor([&] { return emDlg->property("open").toBool() && emDlg->property("mode").toString() == QLatin1String("global"); }, 1000), QStringLiteral("改保险和联系人是弹出表单"));
    QMetaObject::invokeMethod(emDlg, "set", Q_ARG(QVariant, QStringLiteral("insurer")), Q_ARG(QVariant, QStringLiteral("Allianz")));
    QMetaObject::invokeMethod(emDlg, "set", Q_ARG(QVariant, QStringLiteral("n0")), Q_ARG(QVariant, QStringLiteral("小王")));
    pause(50);
    click(win, item(QStringLiteral("emOk")));
    r.check(waitFor([&] { return !emDlg->property("open").toBool(); }, 1000), QStringLiteral("保存后关上"));
    waitFor([&] { return !emDlg->isVisible(); }, 1000);
    r.check(store->data()[QStringLiteral("emergency")][QStringLiteral("insurer")].toString() == QLatin1String("Allianz")
            && store->data()[QStringLiteral("emergency")][QStringLiteral("contacts")].asArray().size() == 1, QStringLiteral("保险和联系人写进顶层，所有计划共用"));
    r.check(!(*plans::planById(store->data(), id)).asObject().contains(QStringLiteral("emergency")), QStringLiteral("没写进计划"));

    // ---- 游记 ----
    click(win, item(QStringLiteral("tripTab-journal")));
    r.check(waitFor([&] { return effectivelyShown(item(QStringLiteral("journalEmpty"))); }, 1000), QStringLiteral("没有游记时一句说明"));
    store->modify([&](Value &d) {
        json::Object j;
        j.set(QStringLiteral("id"), Value(QStringLiteral("jn_test")));
        j.set(QStringLiteral("plan"), Value(id));
        j.set(QStringLiteral("title"), Value(QStringLiteral("第一天")));
        j.set(QStringLiteral("date"), Value(today));
        j.set(QStringLiteral("body"), Value(QStringLiteral("**到了**")));
        d.mutObject().find(QStringLiteral("journal"))->mutArray().push_back(Value(std::move(j)));
    });
    r.check(waitFor([&] { return effectivelyShown(item(QStringLiteral("journalRow"))) && !effectivelyShown(item(QStringLiteral("journalEmpty"))); }, 1000), QStringLiteral("有游记时列出来"));
    r.check(page->info().value(QStringLiteral("journal")).toList().value(0).toMap().value(QStringLiteral("text")).toString() == QStringLiteral("到了"), QStringLiteral("摘要去掉了记号"));

    // ---- 攻略：空的直接进编辑 ----
    click(win, item(QStringLiteral("tripTab-guide")));
    QQuickItem *guideTab = item(QStringLiteral("tripGuide"));
    QQuickItem *edit = item(QStringLiteral("guideEdit"));
    r.check(waitFor([&] { return panel->property("tab").toString() == QLatin1String("guide"); }, 1000) && guideTab && guideTab->property("editing").toBool(), QStringLiteral("空攻略直接进编辑"));
    r.check(effectivelyShown(item(QStringLiteral("guideWrite"))) && effectivelyShown(item(QStringLiteral("guidePreview"))), QStringLiteral("宽窗口左写右预览"));
    for (const char *t : { "h1", "h2", "b", "ul", "task", "quote", "link", "pic" })
        r.check(effectivelyShown(item(QStringLiteral("guideTool-") + QLatin1String(t))), QStringLiteral("工具栏有 %1").arg(QLatin1String(t)));
    // 工具栏（用真的点击）
    edit->setProperty("text", QStringLiteral("交通"));
    click(win, item(QStringLiteral("guideTool-h1")));
    r.check(edit->property("text").toString() == QStringLiteral("# 交通"), QStringLiteral("大标题：行首加 #"), edit->property("text").toString());
    QMetaObject::invokeMethod(edit, "select", Q_ARG(int, 2), Q_ARG(int, 4));
    click(win, item(QStringLiteral("guideTool-b")));
    r.check(edit->property("text").toString() == QStringLiteral("# **交通**"), QStringLiteral("加粗：包住选中的字"), edit->property("text").toString());
    // 链接：校验 + 换掉选中
    QQuickItem *linkDlg = item(QStringLiteral("guideLinkDialog"));
    edit->setProperty("text", QStringLiteral("看这里"));
    QMetaObject::invokeMethod(edit, "select", Q_ARG(int, 0), Q_ARG(int, 3));
    click(win, item(QStringLiteral("guideTool-link")));
    r.check(waitFor([&] { return linkDlg && linkDlg->property("open").toBool(); }, 1000), QStringLiteral("链接：弹出表单"));
    r.check(linkDlg->property("linkText").toString() == QStringLiteral("看这里"), QStringLiteral("选中的字填进去了"));
    linkDlg->setProperty("url", QStringLiteral("example.com"));
    click(win, item(QStringLiteral("guideLinkOk")));
    r.check(linkDlg->property("open").toBool() && effectivelyShown(item(QStringLiteral("guideLinkErr"))), QStringLiteral("网址不对：不插入，写明原因"));
    linkDlg->setProperty("url", QStringLiteral("https://example.com"));
    click(win, item(QStringLiteral("guideLinkOk")));
    r.check(waitFor([&] { return edit->property("text").toString() == QStringLiteral("[看这里](https://example.com)"); }, 1000), QStringLiteral("链接插入了"), edit->property("text").toString());
    r.check(waitFor([&] { return !(*plans::planById(store->data(), id))[QStringLiteral("guide")].toString().isEmpty(); }, 1500), QStringLiteral("空攻略写了字：存进数据"));
    pause(50);
    r.check(guideTab->property("editing").toBool(), QStringLiteral("空攻略写了字：还在编辑，不会跳到读"));

    // 插入照片（这个计划有照片时）
    const QVariantList photos = page->planPhotos();
    if (!photos.isEmpty()) {
        QQuickItem *pick = item(QStringLiteral("photoPickDialog"));
        waitFor([&] { return !linkDlg->isVisible(); }, 1000);   // 淡出完才点得到下面
        click(win, item(QStringLiteral("guideTool-pic")));
        r.check(waitFor([&] { return pick && pick->property("open").toBool(); }, 1000), QStringLiteral("插入照片：列出这个计划的照片"), QString::number(photos.size()));
        waitFor([&] { return item(QStringLiteral("guidePick-0")) != nullptr; }, 1000);
        click(win, item(QStringLiteral("guidePick-0")));
        const QString phId = photos.first().toMap().value(QStringLiteral("id")).toString();
        r.check(waitFor([&] { return edit->property("text").toString().contains(QStringLiteral("](photo:") + phId + QLatin1Char(')')); }, 1000), QStringLiteral("照片写成 photo:id"), edit->property("text").toString());
        const bool thumbed = waitFor([&] {
            for (const QVariant &b : guideTab->property("previewBlocks").toList())
                if (b.toMap().value(QStringLiteral("thumb")).toString().startsWith(QLatin1String("image://thumb/"))) return true;
            return false;
        }, 1500);
        r.check(thumbed, QStringLiteral("预览里找到这张照片的缩略图"));
    } else r.info(QStringLiteral("这个计划没有照片，插入照片没测"));

    // 停手后预览 + 自动保存，字数
    const QString md = QStringLiteral("# 交通\n- [ ] 买转换插头\n- [x] 订酒店\n\n## 吃的\n市场\n\n## 住的\n老城区走路方便。这个标题下面多写几行。\n\n# 一个很长很长很长很长很长很长很长很长很长很长很长很长的标题\n结束");
    edit->setProperty("text", md);
    r.check(waitFor([&] { return (*plans::planById(store->data(), id))[QStringLiteral("guide")].toString() == md; }, 1500), QStringLiteral("停手后写进数据"));
    r.check(waitFor([&] { return guideTab->property("previewBlocks").toList().size() >= 6; }, 1000), QStringLiteral("预览跟上了"));
    if (QQuickItem *h = item(QStringLiteral("guideHint"))) r.check(h->property("text").toString().contains(QString::number(md.size())), QStringLiteral("写着字数"), h->property("text").toString());
    r.check(page->info().value(QStringLiteral("guideView")).toMap().value(QStringLiteral("heads")).toList().size() == 4, QStringLiteral("目录 4 项"));

    // ---- 读：完成、打勾写回、目录 ----
    click(win, item(QStringLiteral("guideDone")));
    r.check(waitFor([&] { return !guideTab->property("editing").toBool(); }, 1000), QStringLiteral("完成：切到读"));
    QQuickItem *body = item(QStringLiteral("guideBody"));
    QQuickItem *side = item(QStringLiteral("guideSide"));
    r.check(effectivelyShown(body) && effectivelyShown(side) && side->width() > 0, QStringLiteral("宽窗口：正文 + 右侧目录"));
    r.check(body && side && body->width() + side->width() < guideTab->width() && body->width() > side->width() * 2, QStringLiteral("目录在右边、正文够宽"), body ? QString::number(body->width()) : QString());
    click(win, findItem(body, QStringLiteral("guideTask-1")));
    r.check(waitFor([&] { return (*plans::planById(store->data(), id))[QStringLiteral("guide")].toString().contains(QStringLiteral("- [x] 买转换插头")); }, 1000), QStringLiteral("打勾写回原文"));
    QQuickItem *list = item(QStringLiteral("tripList"));
    const qreal sideY0 = side->y();
    QQuickItem *row3 = findItem(side, QStringLiteral("guideTocRow-3"));
    r.check(row3 != nullptr, QStringLiteral("目录有最后一个标题"));
    // 把正文撑长一点，才能看出目录留在原地
    QString longer = (*plans::planById(store->data(), id))[QStringLiteral("guide")].toString();
    for (int i = 0; i < 60; ++i) longer += QStringLiteral("\n\n第 %1 段，正文正文正文。").arg(i);
    page->setGuide(id, longer);
    pause(100);
    QMetaObject::invokeMethod(panel, "scrollInGuide", Q_ARG(QVariant, 700), Q_ARG(QVariant, false));
    waitFrames(win, 2);
    const qreal dy = list->property("contentY").toReal();
    r.check(side->y() > sideY0 + 100, QStringLiteral("滚动时目录留在原地"), QStringLiteral("contentY %1，目录 y %2").arg(dy).arg(side->y()));
    const QPointF sideScene = side->mapToScene(QPointF(0, 0));
    r.check(sideScene.y() >= panel->mapToScene(QPointF(0, 0)).y() && sideScene.y() < panel->mapToScene(QPointF(0, 0)).y() + panel->height() / 2, QStringLiteral("目录在可见区域的上半部分"), QString::number(sideScene.y()));
    const qreal before3 = list->property("contentY").toReal();
    click(win, findItem(side, QStringLiteral("guideTocRow-0")));   // 窄窗口那份目录也叫这个名字
    r.check(waitFor([&] { return qAbs(list->property("contentY").toReal() - before3) > 50; }, 1500), QStringLiteral("点目录滚过去"), QString::number(list->property("contentY").toReal()));
    // 重新编辑、切计划不串
    click(win, item(QStringLiteral("guideEditBtn")));
    r.check(waitFor([&] { return guideTab->property("editing").toBool() && edit->property("text").toString() == longer; }, 1500), QStringLiteral("编辑攻略：写的是原文"));
    QMetaObject::invokeMethod(guideTab, "done");
    pause(30);

    // ---- 窄窗口 ----
    const int oldW = win->width();
    win->setWidth(500);
    r.check(waitFor([&] { return !guideTab->property("wide").toBool(); }, 1500), QStringLiteral("窄窗口：攻略退成窄样子"), QString::number(guideTab->width()));
    r.check(!effectivelyShown(side), QStringLiteral("窄窗口：没有右侧竖列"));
    QQuickItem *tocBtn = item(QStringLiteral("guideTocToggle"));
    r.check(waitFor([&] { return effectivelyShown(tocBtn); }, 1500), QStringLiteral("窄窗口：目录收到正文上方"), QStringLiteral("editing %1").arg(guideTab->property("editing").toBool()));
    r.check(tocBtn && tocBtn->mapToScene(QPointF(0, 0)).y() < body->mapToScene(QPointF(0, 0)).y(), QStringLiteral("目录按钮在正文上面"));
    QMetaObject::invokeMethod(guideTab, "beginEdit");
    r.check(waitFor([&] { return guideTab->property("editing").toBool(); }, 1000), QStringLiteral("窄窗口进编辑"));
    pause(50);
    r.check(effectivelyShown(item(QStringLiteral("guideWrite"))) && !effectivelyShown(item(QStringLiteral("guidePreview"))), QStringLiteral("窄窗口：只显示写"));
    guideTab->setProperty("pane", QStringLiteral("read"));
    pause(30);
    r.check(!effectivelyShown(item(QStringLiteral("guideWrite"))) && effectivelyShown(item(QStringLiteral("guidePreview"))), QStringLiteral("窄窗口：切到看"));
    QMetaObject::invokeMethod(guideTab, "done");
    QMetaObject::invokeMethod(panel, "openTab", Q_ARG(QVariant, QStringLiteral("packing")));
    QQuickItem *tabs = item(QStringLiteral("tripTabs"));
    r.check(tabs && tabs->property("contentWidth").toReal() > 0, QStringLiteral("标签能左右拖"), tabs ? QString::number(tabs->property("contentWidth").toReal()) : QString());
    r.check(waitFor([&] { return packCard->x() == todoCard->x() && todoCard->y() > packCard->y(); }, 1500), QStringLiteral("窄窗口：两张清单变一列"));
    win->setWidth(oldW);
    waitFor([&] { return guideTab->property("wide").toBool() || guideTab->width() > 620; }, 1500);

    // ---- 写到一半切计划：不写到别的计划上 ----
    const QVariantList rows = pc->list().value(QStringLiteral("rows")).toList();
    QString otherId;
    for (const QVariant &v : rows) if (v.toMap().value(QStringLiteral("id")).toString() != id) { otherId = v.toMap().value(QStringLiteral("id")).toString(); break; }
    if (!otherId.isEmpty()) {
        const QString otherGuide = (*plans::planById(store->data(), otherId))[QStringLiteral("guide")].toString();
        QMetaObject::invokeMethod(panel, "openTab", Q_ARG(QVariant, QStringLiteral("guide")));
        QMetaObject::invokeMethod(guideTab, "beginEdit");
        pause(50);
        edit->setProperty("text", QStringLiteral("写到一半"));
        QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, otherId), Q_ARG(QVariant, -1));
        waitFor([&] { return page->planId() == otherId; }, 2000);
        pause(400);
        r.check((*plans::planById(store->data(), id))[QStringLiteral("guide")].toString() == QStringLiteral("写到一半"), QStringLiteral("切计划前写的字存到原来的计划"));
        r.check((*plans::planById(store->data(), otherId))[QStringLiteral("guide")].toString() == otherGuide, QStringLiteral("另一个计划的攻略没被改"));
    }

    // ---- 此刻卡 ----
    QMetaObject::invokeMethod(panel, "closeRequested");
    waitFor([&] { return page->planId().isEmpty(); }, 3000);
    store->modify([&](Value &d) {
        Value *p = plans::mutPlan(d, id);
        p->mutObject().set(QStringLiteral("startDate"), Value(QDate::fromString(today, Qt::ISODate).addDays(10).toString(Qt::ISODate)));
        p->mutObject().set(QStringLiteral("endDate"), Value(QDate::fromString(today, Qt::ISODate).addDays(20).toString(Qt::ISODate)));
    });
    nav->go(pc->mode());
    pause(350);
    pc->setNextTrip(id, true);
    win->setProperty("nowOpen", true);
    QQuickItem *nowBtn = nullptr;
    const bool hasBtn = waitFor([&] { nowBtn = item(QStringLiteral("nowPacking")); return effectivelyShown(nowBtn); }, 3000);
    r.check(hasBtn, QStringLiteral("此刻卡有行李与待办"));
    if (hasBtn) {
        pause(350);   // 卡片滑入停下再点（Windows差异清单 #26）
        click(win, nowBtn);
        r.check(waitFor([&] { return page->planId() == id && panel->property("tab").toString() == QLatin1String("packing"); }, 3000), QStringLiteral("点了打开这个计划的行李与待办"), panel->property("tab").toString());
    }

    // ---- 收尾 ----
    QMetaObject::invokeMethod(panel, "closeRequested");
    waitFor([&] { return page->planId().isEmpty(); }, 3000);
    win->setProperty("nowOpen", false);
    pause(350);   // 攻略的保存计时器走完，不再改数据
    store->modify([&](Value &d) { d = before; });
    pc->setTodayOverride(QString());
    nav->go(QStringLiteral("map"));
    pause(100);
}

void runDocs(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav, RatesController *rates)
{
    r.section(QStringLiteral("0.12.4 设置：货币、证件、紧急信息"));
    PlanController *pc = plansCtl();
    data::DataStore *store = app->store();
    Settings *settings = settingsObj();
    const auto item = [&](const QString &name) { return findItem(win->contentItem(), name); };
    QQuickItem *panel = item(QStringLiteral("settingsPanel"));
    QQuickItem *planPanel = item(QStringLiteral("planPage"));
    if (!r.check(pc && page && nav && rates && settings && panel && planPanel, QStringLiteral("设置面板在"))) return;
    pc->setTestAllStatuses(false);
    const QVariantMap demo = pc->demo();
    const QString id = demo.value(QStringLiteral("planId")).toString();
    if (!r.check(!id.isEmpty(), QStringLiteral("有带日期的路线"))) return;
    const QString today = demo.value(QStringLiteral("today")).toString();
    pc->setTodayOverride(today);
    const QDate day = QDate::fromString(today, Qt::ISODate);
    const Value before = store->data();
    const QString oldMain = rates->main();
    const plans::Rates oldTable = rates->table();
    const Value oldManual = settings->value(QStringLiteral("manualRates"));
    const auto gotoSection = [&](const char *key) {
        nav->go(QStringLiteral("settings"));
        panel->setProperty("section", QString::fromLatin1(key));
        pause(150);
    };

    // ---- 货币 ----
    QStringList asked;
    rates->setFetchForTest([&](const QUrl &url, std::function<void(int, const QByteArray &, int)> done) {
        asked << url.host();
        done(200, QByteArrayLiteral(R"({"base":"CNY","date":"2026-11-05","rates":{"AUD":0.2,"USD":0.14}})"), 0);
    });
    rates->resetForTest(QStringLiteral("CNY"), plans::Rates(), double(QDateTime::currentMSecsSinceEpoch()));
    settings->setValue(QStringLiteral("manualRates"), json::parse(QStringLiteral(R"({"AUD":4.65,"ZZZ":"留着"})")));   // 旧版留下的
    r.check(rates->refreshNow() && asked.size() == 1 && rates->date() == QLatin1String("2026-11-05"), QStringLiteral("现在更新：20 小时内也会取"), asked.join(u','));
    r.check(rates->src() == QLatin1String("frankfurter") && rates->updatedAt() > 0 && !rates->busy(), QStringLiteral("汇率状态有来源和时间"));
    store->modify([&](Value &d) {
        Value *p = plans::mutPlan(d, id);
        json::Object &o = p->mutObject();
        o.set(QStringLiteral("currency"), Value(QStringLiteral("AUD")));
        o.remove(QStringLiteral("bookings"));
        o.set(QStringLiteral("expenses"), json::parse(QStringLiteral(R"([{"id":"e1","amount":100,"date":"2026-01-01"}])")));
    });
    page->show(id);
    const auto mainTotal = [&] { return page->info().value(QStringLiteral("money")).toMap().value(QStringLiteral("mainTotal")).toDouble(); };
    r.check(qAbs(mainTotal() - 500) < 1e-6, QStringLiteral("settings.json 里旧的手动汇率不再起作用"), QString::number(mainTotal()));
    {
        RatesController again(settings, nullptr);   // 等于重新打开程序
        r.check(qAbs(plans::perMain(again.table(), QStringLiteral("CNY"), QStringLiteral("AUD")) - 5) < 1e-9, QStringLiteral("重新读 settings.json 也不认手动汇率"));
    }
    rates->refreshNow();
    r.check(settings->value(QStringLiteral("manualRates"))[QStringLiteral("ZZZ")].toString() == QStringLiteral("留着"), QStringLiteral("settings.json 里的 manualRates 键原样留着"));
    page->show(QString());
    gotoSection("money");
    r.check(effectivelyShown(item(QStringLiteral("setCard-money"))) && !item(QStringLiteral("setCard-rates")) && !item(QStringLiteral("rateField-AUD")), QStringLiteral("货币分组只有主货币和联网汇率"));

    // ---- 证件：此刻卡跳过去 ----
    const QString soon = day.addDays(10).toString(Qt::ISODate), past = day.addDays(-1).toString(Qt::ISODate), far = day.addYears(5).toString(Qt::ISODate);
    store->modify([&](Value &d) {
        // Electron 存的样子，带一个不认识的键
        d.mutObject().set(QStringLiteral("documents"), json::parse(QStringLiteral(R"([{"kind":"visa","owner":"妈妈","label":"单次","expires":"%1","planId":"%2","id":"doc_a","future":1},{"kind":"passport","label":"旧护照","expires":"%3","id":"doc_b"}])").arg(soon, id, past)));
        d.mutObject().remove(QStringLiteral("emergency"));
        plans::mutPlan(d, id)->mutObject().set(QStringLiteral("status"), Value(QStringLiteral("planning")));
    });
    nav->go(pc->mode());
    pause(350);
    pc->setNextTrip(id, true);
    win->setProperty("nowOpen", true);
    panel->setProperty("section", QStringLiteral("look"));
    QQuickItem *nowDocs = nullptr;
    const bool hasLink = waitFor([&] { nowDocs = item(QStringLiteral("nowDocs")); return effectivelyShown(nowDocs); }, 3000);
    r.check(hasLink, QStringLiteral("此刻卡的证件提醒有 看证件"));
    if (hasLink) {
        pause(350);   // 卡片滑入停下再点（Windows差异清单 #26）
        click(win, nowDocs);
        r.check(waitFor([&] { return nav->page() == QLatin1String("settings") && panel->property("section").toString() == QLatin1String("docs"); }, 3000), QStringLiteral("点了跳到设置的证件分组"), nav->page());
        QQuickItem *docsCard = item(QStringLiteral("setCard-docs"));
        r.check(waitFor([&] { return effectivelyShown(docsCard) && docsCard->isVisible() && effectivelyShown(panel); }, 3000), QStringLiteral("证件分组显示出来"));
        QQuickItem *flick = item(QStringLiteral("settingsFlick"));
        r.check(flick && qFuzzyIsNull(flick->property("contentY").toDouble()), QStringLiteral("滚到证件那一组的开头"));
    }
    win->setProperty("nowOpen", false);
    gotoSection("docs");
    const auto docs = [&] { return page->documents(); };
    r.check(docs().size() == 2 && docs()[0].toMap().value(QStringLiteral("id")).toString() == QLatin1String("doc_b"), QStringLiteral("证件按到期日排"));
    QQuickItem *date0 = nullptr;
    const bool rowsUp = waitFor([&] { date0 = item(QStringLiteral("docDate-doc_b")); return date0 != nullptr; }, 2000);
    r.check(rowsUp, QStringLiteral("证件列表显示出来"));
    QQuickItem *date1 = item(QStringLiteral("docDate-doc_a"));
    if (rowsUp && date1)
        r.check(date0->property("color").value<QColor>() != date1->property("color").value<QColor>(), QStringLiteral("过期的日期标红"));
    QQuickItem *dlg = item(QStringLiteral("docDialog"));
    QQuickItem *add = item(QStringLiteral("docAdd"));
    if (r.check(dlg && add, QStringLiteral("有添加按钮和对话框"))) {
        click(win, add);
        r.check(waitFor([&] { return dlg->property("open").toBool(); }, 2000), QStringLiteral("点添加证件打开对话框"));
        dlg->setProperty("owner", QStringLiteral("我"));
        dlg->setProperty("expires", far);
        dlg->setProperty("planId", id);
        QQuickItem *ok = item(QStringLiteral("docOk"));
        pause(250);
        click(win, ok);
        r.check(waitFor([&] { return docs().size() == 3 && !dlg->property("open").toBool(); }, 2000), QStringLiteral("保存后多一条，对话框关上"));
    }
    QVariantMap added, visa;
    for (const QVariant &v : docs()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")) == QStringLiteral("doc_a")) visa = m;
        else if (m.value(QStringLiteral("id")) != QStringLiteral("doc_b")) added = m;
    }
    r.check(added.value(QStringLiteral("id")).toString().startsWith(QLatin1String("doc_")) && added.value(QStringLiteral("kind")) == QStringLiteral("passport")
            && added.value(QStringLiteral("owner")) == QStringLiteral("我") && added.value(QStringLiteral("expires")).toString() == far && added.value(QStringLiteral("planId")).toString() == id,
            QStringLiteral("新证件的字段和 Electron 一样"));
    visa.insert(QStringLiteral("label"), QStringLiteral("多次"));
    r.check(page->saveDocument(visa.value(QStringLiteral("index")).toInt(), QStringLiteral("doc_a"), visa).isEmpty(), QStringLiteral("改证件"));
    const Value &va = store->data()[QStringLiteral("documents")].asArray()[0];
    r.check(va[QStringLiteral("label")].toString() == QStringLiteral("多次") && va[QStringLiteral("future")].toNumber() == 1 && va[QStringLiteral("owner")].toString() == QStringLiteral("妈妈"), QStringLiteral("改证件保留不认识的键"));
    r.check(page->deleteDocument(2, added.value(QStringLiteral("id")).toString()).isEmpty() && docs().size() == 2, QStringLiteral("删证件"));
    r.check(page->deleteDocument(0, QStringLiteral("对不上")) == QLatin1String("gone") && docs().size() == 2, QStringLiteral("列表变过时不会删错"));

    // ---- 紧急信息 ----
    QQuickItem *emBtn = item(QStringLiteral("setEmergencyEdit"));
    QQuickItem *emDlg = item(QStringLiteral("emergencyDialog"));
    if (r.check(emBtn && emDlg && effectivelyShown(emBtn), QStringLiteral("证件分组有紧急信息"))) {
        pause(250);   // 刚删了一行，列表要重新排好，按钮才在算出来的位置上
        click(win, emBtn);
        r.check(waitFor([&] { return emDlg->property("open").toBool() && emDlg->property("mode").toString() == QLatin1String("global"); }, 2000), QStringLiteral("点编辑打开紧急信息（不用先打开计划）"));
        emDlg->setProperty("open", false);
        waitFor([&] { return !emDlg->property("open").toBool(); }, 1000);
    }
    r.check(page->saveEmergency({ { QStringLiteral("insurer"), QStringLiteral("Allianz") }, { QStringLiteral("n0"), QStringLiteral("甲") } }).isEmpty()
            && store->data()[QStringLiteral("emergency")][QStringLiteral("insurer")].toString() == QLatin1String("Allianz"), QStringLiteral("没打开计划时也能存紧急信息"));
    r.check(page->emergencyInfo().value(QStringLiteral("contacts")).toList().size() == 1, QStringLiteral("读回紧急信息"));
    QQuickItem *emSub = item(QStringLiteral("setEmergencyRow"));
    r.check(waitFor([&] { return emSub && emSub->property("sub").toString().contains(QLatin1String("Allianz")); }, 2000), QStringLiteral("紧急信息一行显示摘要"));

    // ---- 计划页的"管理证件" ----
    panel->setProperty("section", QStringLiteral("look"));
    QMetaObject::invokeMethod(planPanel, "docsRequested");
    r.check(waitFor([&] { return nav->page() == QLatin1String("settings") && panel->property("section").toString() == QLatin1String("docs"); }, 2000), QStringLiteral("计划页的管理证件跳到证件分组"));

    // ---- 收尾 ----
    pc->setNextTrip(id, false);
    pause(300);
    store->modify([&](Value &d) { d = before; });
    rates->setFetchForTest({});
    settings->setValue(QStringLiteral("manualRates"), oldManual);
    rates->resetForTest(oldMain, oldTable, 0);
    pc->setTodayOverride(QString());
    pc->setTestAllStatuses(true);
    panel->setProperty("section", QStringLiteral("look"));
    nav->go(QStringLiteral("map"));
    pause(100);
}

namespace {
// 一个项下面所有看得见的文字，拼在一起
QString allText(QQuickItem *it)
{
    if (!it) return {};
    QString s = it->property("text").toString();
    for (QQuickItem *c : it->childItems()) s += u'\n' + allText(c);
    return s;
}
}

void runNetAbout(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, NavHistory *nav, services::NetClient *net, RatesController *rates, WeatherController *weather, PhotoController *photos)
{
    r.section(QStringLiteral("0.12.5 设置：联网开关、缓存、关于、更新日志"));
    using services::NetClient;
    NetController *netc = netCtl();
    Settings *settings = settingsObj();
    const auto item = [&](const QString &name) { return findItem(win->contentItem(), name); };
    QQuickItem *panel = item(QStringLiteral("settingsPanel"));
    if (!r.check(netc && net && rates && weather && settings && panel, QStringLiteral("联网开关和设置面板在"))) return;
    const bool oldOffline = netc->offline();
    const QString oldBase = mapc->base();
    const Value oldNet = settings->value(QStringLiteral("net"));
    rates->setFetchForTest({});
    weather->setFetchForTest({});

    // ---- 每项开关都拦住对应的请求（没有真的发出去：被拒绝的回调里 status 是 0、错误是 ServiceOff / Offline）----
    struct Svc { const char *key; const char *url; const char *named; };
    const Svc svcs[] = {
        { "tiles", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/1/0/0", "" },
        { "geocode", "https://nominatim.openstreetmap.org/search?format=jsonv2&q=x", "" },
        { "routing", "https://routing.openstreetmap.de/routed-car/route/v1/driving/1,1;2,2", "" },
        { "rates", "https://api.frankfurter.dev/v1/latest?base=CNY", "" },
        { "rates", "https://open.er-api.com/v6/latest/CNY", "" },
        { "weather", "https://api.open-meteo.com/v1/forecast?latitude=1&longitude=1", "" },
        { "climate", "https://archive-api.open-meteo.com/v1/archive?latitude=1&longitude=1", "" },
        { "elev", "https://api.open-meteo.com/v1/elevation?latitude=1&longitude=1", "elev" },
    };
    const auto ask = [&](const Svc &s) {
        NetClient::Error e = NetClient::Error::None;
        bool done = false;
        const quint64 id = net->get(QUrl(QString::fromLatin1(s.url)), 3000, nullptr, [&](const NetClient::Reply &rep) { e = rep.error; done = true; },
                                    QByteArrayLiteral("application/json"), QString::fromLatin1(s.named));
        if (!waitFor([&] { return done; }, 500)) { net->cancel(id); return NetClient::Error::None; }   // 没被拦：不等它真的联网
        return e;
    };
    netc->setOffline(false);
    QStringList bad;
    for (const Svc &s : svcs) {
        const QString k = QString::fromLatin1(s.key);
        netc->setSwitch(k, false);
        if (ask(s) != NetClient::Error::ServiceOff) bad << k;
        if (settings->value(QStringLiteral("net"))[k].isBool() == false) bad << k + QStringLiteral("没存");
        netc->setSwitch(k, true);
        if (settings->value(QStringLiteral("net")).asObject().contains(k)) bad << k + QStringLiteral("没去掉");
    }
    r.check(bad.isEmpty(), QStringLiteral("每项开关关掉后拦住对应请求，存进 settings.json 的 net（同 Electron）"), bad.join(u','));
    netc->setSwitch(QStringLiteral("weather"), false);
    const bool elevStill = net->allows(QStringLiteral("elev")) && net->allows(QStringLiteral("climate"));
    netc->setSwitch(QStringLiteral("weather"), true);
    netc->setSwitch(QStringLiteral("climate"), false);
    const bool climOff = !weather->climateAvailable();
    netc->setSwitch(QStringLiteral("climate"), true);
    r.check(elevStill && climOff && weather->climateAvailable(), QStringLiteral("天气、气候、海拔各管各的；气候关了气候按钮不显示"));
    netc->setSwitch(QStringLiteral("rates"), false);
    const bool ratesOff = rates->netOff() && !rates->refreshNow() && !rates->busy();
    netc->setSwitch(QStringLiteral("rates"), true);
    r.check(ratesOff && !rates->netOff(), QStringLiteral("汇率关了：现在更新不发请求，界面能知道是关着"));
    netc->setOffline(true);
    bad.clear();
    for (const Svc &s : svcs) if (ask(s) != NetClient::Error::Offline) bad << QString::fromLatin1(s.key);
    r.check(bad.isEmpty() && settings->value(QStringLiteral("offline")).toBool() && !netc->allowed().value(QStringLiteral("ai")).toBool(), QStringLiteral("离线总开关拦住所有请求，AI 也算关"), bad.join(u','));
    panel->setProperty("section", QStringLiteral("net"));
    nav->go(QStringLiteral("settings"));
    pause(200);
    QQuickItem *aiBtn = item(QStringLiteral("aiBtn"));
    r.check(aiBtn && !aiBtn->isVisible(), QStringLiteral("离线时 AI 泡泡不显示（Electron 同样）"));
    QQuickItem *netCard = item(QStringLiteral("setCard-net"));
    const QString netTxt = allText(netCard);
    r.check(effectivelyShown(netCard) && item(QStringLiteral("netSw-ai")) && item(QStringLiteral("netSw-tiles")) && netTxt.contains(QStringLiteral("都不用")), QStringLiteral("联网分组有 8 项开关，离线时说人话"));
    netc->setOffline(oldOffline);
    settings->setValue(QStringLiteral("net"), oldNet);
    settings->save();
    netc->reload();
    mapc->setBase(oldBase);   // 关瓦片时被切到了"已解锁"

    // ---- 缓存清空走回收站（临时文件夹里的假缓存；验证完从回收站拿掉）----
    {
        QTemporaryDir tmp;
        services::TileCache cache(tmp.path() + QStringLiteral("/tiles"));
        cache.startScan();
        cache.waitScan();
        cache.put({ QStringLiteral("gray"), 1, 0, 0 }, QByteArrayLiteral("old"));
        cache.put({ QStringLiteral("street"), 1, 0, 0 }, QByteArrayLiteral("new"));
        bool done = false, ok = false;
        QObject::connect(&cache, &services::TileCache::trashDone, &cache, [&](bool o) { ok = o; done = true; });
        cache.clearToTrash();
        const bool blocked = cache.clearing() && !cache.put({ QStringLiteral("street"), 1, 0, 1 }, QByteArrayLiteral("x"));
        r.check(blocked && waitFor([&] { return done; }, 15000) && ok, QStringLiteral("瓦片缓存：清空时不存新的，完成后报成功"));
        const QString where = cache.lastTrashPath();
        const bool inTrash = !where.isEmpty() && QFileInfo::exists(where + QStringLiteral("/gray/1/0/0.t"));
        const bool gone = !QFileInfo::exists(tmp.path() + QStringLiteral("/tiles/street/1/0/0.t")) && cache.bytes() == 0;
        r.check(inTrash && gone, QStringLiteral("瓦片缓存移进回收站（含旧的浅灰），不是删掉"), where);
        if (inTrash) QDir(where).removeRecursively();

        data::ThumbCache thumbs(tmp.path() + QStringLiteral("/thumbs"));
        QDir().mkpath(thumbs.root() + QStringLiteral("/a"));
        { QFile f(thumbs.root() + QStringLiteral("/a/t.jpg")); (void)f.open(QIODevice::WriteOnly); f.write("x"); }
        QString tw;
        const bool tok = thumbs.clearToTrash(&tw);
        const bool tin = !tw.isEmpty() && QFileInfo::exists(tw + QStringLiteral("/a/t.jpg"));
        r.check(tok && tin && thumbs.totalBytes() == 0 && QFileInfo(thumbs.root()).isDir(), QStringLiteral("缩略图缓存移进回收站，文件夹重新建好"), tw);
        if (tin) QDir(tw).removeRecursively();
    }
    panel->setProperty("section", QStringLiteral("data"));
    pause(150);
    QQuickItem *tileRow = item(QStringLiteral("cacheTiles"));
    r.check(effectivelyShown(tileRow) && effectivelyShown(item(QStringLiteral("cacheThumbs"))), QStringLiteral("数据分组显示地图和缩略图的缓存用量"), tileRow ? tileRow->property("sub").toString() : QString());
    {   // 0.12.6 硬盘上限：存进 settings.json，读回来是新值；0 不收
        const int keep = netc->tileMaxMB();
        netc->setTileMaxMB(5120);
        const bool saved = int(settings->value(QStringLiteral("tileCacheMaxMB")).toNumber()) == 5120 && netc->tileMaxMB() == 5120;
        netc->setTileMaxMB(0);
        r.check(saved && netc->tileMaxMB() == 5120 && effectivelyShown(item(QStringLiteral("cacheTilesMax"))), QStringLiteral("地图缓存上限存进 settings.json，缓存真的用它"));
        netc->setTileMaxMB(keep);
    }

    // ---- 关于 ----
    panel->setProperty("section", QStringLiteral("about"));
    pause(150);
    QQuickItem *aboutCard = item(QStringLiteral("aboutSection"));
    const QString about = allText(aboutCard);
    QStringList lack;
    for (const char *w : { "不用于商业", "Windows 10", "Black Marble", "Esri", "OpenStreetMap", "Nominatim", "OSRM", "Open-Meteo", "CC BY", "Frankfurter", "er-api", "exchangerate-api.com", "Natural Earth",
                           "LGPL-3", "maplibre-native-qt", "suncalc", "OFL", "Apache 2.0", "Material Symbols", "思源宋体", "原创" })
        if (!about.contains(QString::fromUtf8(w))) lack << QString::fromUtf8(w);
    r.check(effectivelyShown(aboutCard) && lack.isEmpty(), QStringLiteral("关于写全了作者、系统要求、署名、授权、字体、素材"), lack.join(u','));
    const QString lic = app->licensesDir();
    bool licOk = true;
    for (const QString &f : { QStringLiteral("LICENSE-suncalc.txt"), QStringLiteral("LICENSE-NotoSerifCJK.txt"), QStringLiteral("LICENSE-Material-Symbols.txt"), QStringLiteral("maplibre-native-qt/LGPL-3.0-only.txt"), QStringLiteral("maplibre-native-qt/BSD-2-Clause.txt") })
        licOk = licOk && QFileInfo::exists(lic + u'/' + f);
    r.check(licOk, QStringLiteral("授权全文在程序目录的 LICENSES"), lic);
    QQuickItem *tour = item(QStringLiteral("aboutTour"));
    if (r.check(effectivelyShown(tour), QStringLiteral("有 再看一次引导"))) {
        click(win, tour);
        QQuickItem *t = item(QStringLiteral("toast"));
        r.check(t && waitFor([&] { return t->property("message").toString().contains(QStringLiteral("还在做")); }, 1500), QStringLiteral("引导还没做：点了提示还在做"));
    }
    // 照片署名：种一条看列表
    data::DataStore *store = app->store();
    const Value before = store->data();
    store->modify([&](Value &d) {
        d.mutObject().set(QStringLiteral("photos"), json::parse(QStringLiteral(R"([{"id":"ph_c","file":"photos/a.jpg","credit":{"src":"wikimedia","title":"湖","author":"某人","license":"CC BY 4.0"}},{"id":"ph_d","file":"photos/b.jpg"}])")));
    });
    const QVariantList credits = photos->creditRows();
    r.check(credits.size() == 1 && credits[0].toMap().value(QStringLiteral("author")).toString() == QStringLiteral("某人"), QStringLiteral("照片署名只列有署名的"));
    store->modify([&](Value &d) { d = before; });

    // ---- 更新日志 ----
    const QString log = app->changelogText();
    r.check(log.startsWith(QLatin1String("## v")) && log.contains(QStringLiteral("## v") + app->version()), QStringLiteral("CHANGELOG.md 打包进资源，顶上是这个版本"), log.left(40));
    QQuickItem *logBtn = item(QStringLiteral("aboutLog"));
    if (r.check(effectivelyShown(logBtn), QStringLiteral("关于里有更新日志入口"))) {
        QElapsedTimer t;
        t.start();
        click(win, logBtn);
        QQuickItem *logText = nullptr;
        const bool shown = waitFor([&] { logText = item(QStringLiteral("logText")); return effectivelyShown(logText) && logText->property("text").toString().contains(app->version()); }, 3000);
        r.check(shown && logText->height() > 2000, QStringLiteral("点了显示更新日志"), QStringLiteral("%1 ms，高 %2").arg(t.elapsed()).arg(logText ? logText->height() : 0));
        QQuickItem *back = item(QStringLiteral("aboutLogBack"));
        if (back) click(win, back);
        r.check(waitFor([&] { return panel->property("section").toString() == QLatin1String("about"); }, 1500), QStringLiteral("从更新日志回到关于"));
    }
    panel->setProperty("section", QStringLiteral("look"));
    nav->go(QStringLiteral("map"));
    pause(100);
}

} // namespace tp::app::selftest
