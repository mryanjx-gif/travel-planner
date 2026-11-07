// 自检：0.10.4 行李与待办、紧急信息、游记、攻略。都在数据副本上做，最后把数据改回去。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "navhistory.h"
#include "plancontroller.h"
#include "planpagecontroller.h"
#include "plans.h"

#include <QDate>

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

} // namespace tp::app::selftest
