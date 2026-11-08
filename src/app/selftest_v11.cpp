// 自检：1.1.0 PO 第一批反馈（自动算路和排队、新建计划泡泡、编辑器顶部 AI 和并排、发送键、头像菜单、模式泡泡白字、问题报告）
#include "selftest.h"
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "netclient.h"
#include "planeditor.h"
#include "roadrouter.h"
#include "roads.h"
#include "translator.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <cmath>

namespace tp::app::selftest {

namespace {
QQuickItem *findV11(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findV11(c, name)) return f;
    return nullptr;
}
} // namespace

void runV11(Report &r, AppController *app, QQuickWindow *win, services::RoadRouter *roads)
{
    r.section(QStringLiteral("1.1.0 反馈改动"));
    const auto item = [&](const char *n) { return findV11(win->contentItem(), QString::fromLatin1(n)); };
    data::DataStore *store = app->store();

    // ---- 第 5 条：头像菜单只留设置、退出（宽窗口） ----
    if (QQuickItem *menu = item("avatarMenu")) {
        QStringList keys;
        for (const QVariant &v : menu->property("items").toList()) { const QVariantMap m = v.toMap(); if (!m.value(QStringLiteral("sep")).toBool()) keys << m.value(QStringLiteral("key")).toString(); }
        keys.removeIf([](const QString &k) { return k.startsWith(QLatin1String("page:")) || k == QLatin1String("now"); });
        r.check(keys == QStringList{ QStringLiteral("settings"), QStringLiteral("quit") }, QStringLiteral("头像菜单只有设置、退出"), keys.join(u','));
    } else r.check(false, QStringLiteral("头像菜单在"));

    // ---- 第 2 条：新建计划泡泡白字、AI 格去掉 ----
    QQuickItem *label = item("newLabel");
    QQuickItem *nb = item("newBtn");
    QQuickItem *glass = nb && !nb->childItems().isEmpty() ? nb->childItems().first() : nullptr;
    const double tintA = glass ? glass->property("tint").value<QColor>().alphaF() : 0;
    r.check(label && label->property("color").value<QColor>() != QColor(Qt::white) && tintA >= 0.85 && !item("newAiBtn"),
            QStringLiteral("新建计划泡泡：深蓝字、玻璃接近实底、旁边没有 AI 格"), QStringLiteral("玻璃不透明度 %1").arg(tintA));

    // ---- 第 6 条：模式泡泡没选中的白字 ----
    if (QQuickItem *bud = item("bud-foot")) r.check(bud->property("whiteText").toBool(), QStringLiteral("模式泡泡没选中时白字"));
    else r.check(item("bud-calendar") && item("bud-calendar")->property("whiteText").toBool(), QStringLiteral("模式泡泡没选中时白字"));

    // ---- 第 4、8 条：AI 输入框提示、发送键灰 ----
    QQuickItem *aiIn = item("aiInput");
    r.check(aiIn && aiIn->property("placeholder").toString() == tp::tr("问点什么"), QStringLiteral("AI 输入框提示没有按键说明"), aiIn ? aiIn->property("placeholder").toString() : QString());

    // ---- 第 1、9 条：自动算路，断网排队，联网补算 ----
    {
        QString planId; int stop = -1; QString path; double dist = 0, time = 0;
        store->modify([&](json::Value &d) {
            for (json::Value &pv : d.mutObject().find(QStringLiteral("plans"))->mutArray()) {
                json::Value *sv = pv.mutObject().find(QStringLiteral("stops"));
                if (!sv || pv[QStringLiteral("type")].toString() != QLatin1String("route")) continue;
                auto &arr = sv->mutArray();
                for (int i = 0; i + 1 < int(arr.size()); ++i) {
                    json::Value *lv = arr[i].mutObject().find(QStringLiteral("leg"));
                    if (!lv || !lv->isObject() || !map::roads::isRoadMode((*lv)[QStringLiteral("mode")].toString()) || (*lv)[QStringLiteral("path")].toString().isEmpty()) continue;
                    planId = pv[QStringLiteral("id")].toString(); stop = i; path = (*lv)[QStringLiteral("path")].toString();
                    dist = (*lv)[QStringLiteral("dist")].toNumber(); time = (*lv)[QStringLiteral("time")].toNumber();
                    json::Object &lo = lv->mutObject();
                    lo.remove(QStringLiteral("path")); lo.remove(QStringLiteral("dist")); lo.remove(QStringLiteral("time")); lo.remove(QStringLiteral("key"));
                    return;
                }
            }
        });
        app->noteChanged(true);
        if (planId.isEmpty()) r.check(true, QStringLiteral("自动算路（跳过）"), QStringLiteral("数据里没有自驾 / 徒步段"));
        else {
            bool online = false;
            int calls = 0;
            roads->setFetchForTest([&](const QUrl &, std::function<void(int, const QByteArray &, int)> done) {
                ++calls;
                if (!online) { QTimer::singleShot(10, [done] { done(0, {}, int(services::NetClient::Error::Network)); }); return; }
                const QByteArray body = QJsonDocument(QJsonObject{ { "code", "Ok" }, { "routes", QJsonArray{ QJsonObject{ { "geometry", path }, { "distance", dist }, { "duration", time } } } } }).toJson(QJsonDocument::Compact);
                QTimer::singleShot(10, [done, body] { done(200, body, 0); });
            });
            roads->setAutoEnabled(true);
            roads->autoFor({ planId });
            waitFor([roads] { return !roads->running(); }, 5000);
            r.check(calls == 1 && roads->queued().contains(planId) && !roads->done(), QStringLiteral("断网：停在第一段、排队、不出提示条"),
                    QStringLiteral("请求 %1 次，队里 %2").arg(calls).arg(roads->queued().join(u',')));
            online = true;
            roads->tryQueue();
            waitFor([roads] { return !roads->running(); }, 5000);
            r.check(roads->queued().isEmpty() && roads->pendingFor(planId) == 0 && !roads->done(), QStringLiteral("联网后自动补算、出队"),
                    QStringLiteral("队里 %1，还没算 %2").arg(roads->queued().join(u',')).arg(roads->pendingFor(planId)));
            roads->setFetchForTest({});
            roads->setAutoEnabled(false);
        }
    }

    // ---- 第 2、7 条：编辑器顶部 AI，有内容时 AI 面板贴在编辑器左边 ----
    {
        PlanEditor *ed = editorCtl();
        QString withStops;
        for (const json::Value &p : store->data()[QStringLiteral("plans")].asArray())
            if (p[QStringLiteral("stops")].asArray().size() > 1) { withStops = p[QStringLiteral("id")].toString(); break; }
        QQuickItem *pe = item("planEditor"), *panel = item("aiPanel");
        QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("map")));
        pause(400);
        if (ed && pe && panel && !withStops.isEmpty() && ed->openPlan(withStops).isEmpty()) {
            waitFor([pe, win] { return pe->x() + pe->width() <= win->width(); }, 2000);
            QQuickItem *goBtn = item("edSearchGo");
            r.check(goBtn && goBtn->property("has").toBool() == false, QStringLiteral("地名搜索框有发送键，没输入时灰"));
            QMetaObject::invokeMethod(pe, "aiRequested");
            waitFor([panel] { return panel->property("open").toBool(); }, 1000);
            pause(500);
            const bool over = panel->property("backEdit").toBool();
            const bool beside = over ? std::abs(panel->x() - pe->x()) < 2 : std::abs(panel->y() - pe->y()) < 2;
            r.check(panel->property("open").toBool() && beside && (over ? panel->property("backEdit").toBool() : panel->x() + panel->width() <= pe->x() + 1),
                    QStringLiteral("有内容：AI 面板和编辑器并排（窄窗口盖在上面、有回到编辑）"),
                    QStringLiteral("面板 x=%1 w=%2，编辑器 x=%3，over=%4").arg(panel->x()).arg(panel->width()).arg(pe->x()).arg(over));
            panel->setProperty("open", false);
            ed->discard();
        } else r.check(false, QStringLiteral("编辑器 AI（没能打开有站点的计划）"));
    }

    // ---- 第 9 条：问题报告 ----
    {
        const QVariantMap rep = app->exportReport(QStringLiteral("selftest"));
        const QString path = rep.value(QStringLiteral("path")).toString();
        QFile f(path);
        const bool ok = rep.value(QStringLiteral("ok")).toBool() && f.open(QIODevice::ReadOnly) && f.read(2) == QByteArrayLiteral("PK");
        f.close();
        r.check(ok, QStringLiteral("导出问题报告：桌面上有 zip"), rep.value(QStringLiteral("name")).toString() + rep.value(QStringLiteral("error")).toString());
        if (!path.isEmpty()) QFile::remove(path);
    }
}

} // namespace tp::app::selftest
