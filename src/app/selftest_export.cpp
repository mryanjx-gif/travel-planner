// 自检：0.14.0 导出（E-01–E-04、E-10）。在数据副本上补一笔订单、消费、紧急信息，写到临时文件夹，最后把数据改回去。
// 导出的第一页、中间一页、信息卡另存成图放在自检报告旁边（export-*.png），给人看排版。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "exportcontroller.h"
#include "jsonio.h"
#include "plans.h"
#include "roadrouter.h"

#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>

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
bool inked(const QImage &img)   // 有不是白色的像素（画出东西了）
{
    if (img.isNull()) return false;
    int n = 0;
    for (int y = 0; y < img.height(); y += 7)
        for (int x = 0; x < img.width(); x += 7)
            if (qGray(img.pixel(x, y)) < 200) ++n;
    return n > 50;
}
QByteArray head(const QString &file, int n)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.read(n) : QByteArray();
}
} // namespace

void runExport(Report &r, AppController *app, QQuickWindow *win, services::RoadRouter *roads)
{
    r.section(QStringLiteral("0.14.0 导出：行程单、长图、信息卡、.ics、导出前算路"));
    ExportController *ex = exporterCtl();
    data::DataStore *store = app->store();
    if (!r.check(ex && roads, QStringLiteral("导出和算路都在"))) return;
    const auto item = [&](const QString &n) { return findItem(win->contentItem(), n); };
    // 站点最多的路线
    QString id;
    int best = 0;
    for (const Value &p : store->data()[QStringLiteral("plans")].asArray())
        if (plans::typeOf(p) == QLatin1String("route") && plans::validStops(p).size() > best) { best = int(plans::validStops(p).size()); id = p[QStringLiteral("id")].toString(); }
    if (!r.check(best >= 3, QStringLiteral("有 3 站以上的路线"), QString::number(best))) return;
    const Value before = store->data();
    store->modify([&](Value &d) {
        Value *p = plans::mutPlan(d, id);
        json::Object &o = p->mutObject();
        Value b = json::parse(QStringLiteral("{\"id\":\"stx-b\",\"kind\":\"flight\",\"title\":\"自检航班 CA123\",\"from\":\"上海\",\"to\":\"东京\",\"start\":\"2026-01-01T09:30\",\"ref\":\"REF-123456\",\"cost\":1880,\"notes\":\"靠窗\"}"));
        Value e = json::parse(QStringLiteral("{\"id\":\"stx-e\",\"date\":\"2026-01-01\",\"cat\":\"food\",\"title\":\"拉面\",\"amount\":1200,\"currency\":\"JPY\"}"));
        if (!o.contains(QStringLiteral("bookings"))) o.set(QStringLiteral("bookings"), Value::array());
        o.find(QStringLiteral("bookings"))->mutArray().push_back(b);
        if (!o.contains(QStringLiteral("expenses"))) o.set(QStringLiteral("expenses"), Value::array());
        o.find(QStringLiteral("expenses"))->mutArray().push_back(e);
        o.set(QStringLiteral("localEmergency"), json::parse(QStringLiteral("[{\"label\":\"报警\",\"phone\":\"110\"},{\"label\":\"急救\",\"phone\":\"119\"}]")));
        d.mutObject().set(QStringLiteral("emergency"), json::parse(QStringLiteral("{\"insurer\":\"自检保险\",\"policyNo\":\"PN-1\",\"insurerPhone\":\"+86 400\",\"contacts\":[{\"name\":\"家人\",\"relation\":\"父母\",\"phone\":\"+86 138\"}]}")));
    });
    QTemporaryDir tmp;
    const QVariantMap none{ { QStringLiteral("refs"), false }, { QStringLiteral("money"), false }, { QStringLiteral("notes"), false }, { QStringLiteral("emergency"), false } };
    const QVariantMap all{ { QStringLiteral("refs"), true }, { QStringLiteral("money"), true }, { QStringLiteral("notes"), true }, { QStringLiteral("emergency"), true } };

    // ---- 控制器：预览、勾选影响输出、各种格式 ----
    const QVariantMap info = ex->info(id);
    r.check(!info.value(QStringLiteral("name")).toString().isEmpty() && info.value(QStringLiteral("hasEmergency")).toBool() && info.value(QStringLiteral("icsCount")).toInt() > 0,
            QStringLiteral("导出的信息：名字、有紧急信息、.ics 有内容"), QStringLiteral("%1 项").arg(info.value(QStringLiteral("icsCount")).toInt()));
    const QImage pv(QUrl(ex->preview(id, QStringLiteral("itinerary"), all, 440)).toLocalFile());
    r.check(pv.width() == 440 && pv.height() > 600 && inked(pv), QStringLiteral("第一页预览出图"), QStringLiteral("%1×%2").arg(pv.width()).arg(pv.height()));
    const QVariantMap a = ex->save(id, QStringLiteral("itinerary"), QStringLiteral("png"), none, tmp.filePath(QStringLiteral("none.png")));
    const QVariantMap b = ex->save(id, QStringLiteral("itinerary"), QStringLiteral("png"), all, tmp.filePath(QStringLiteral("all.png")));
    const int ha = a.value(QStringLiteral("height")).toInt(), hb = b.value(QStringLiteral("height")).toInt();
    r.check(a.value(QStringLiteral("ok")).toBool() && b.value(QStringLiteral("ok")).toBool() && hb > ha + 150 && QImage(tmp.filePath(QStringLiteral("all.png"))).width() == 820,
            QStringLiteral("勾上订单号、消费、备注、紧急信息，长图多出内容（宽 820）"), QStringLiteral("%1 → %2 高").arg(ha).arg(hb));
    {
        int p0 = 0, p1 = 0;
        const QImage i0 = ex->renderPage(id, QStringLiteral("itinerary"), none, 0, 300, &p0), i1 = ex->renderPage(id, QStringLiteral("itinerary"), all, 0, 300, &p1);
        r.check(p1 >= p0 && i0 != i1, QStringLiteral("勾选也改变 PDF 的页面"), QStringLiteral("%1 → %2 页").arg(p0).arg(p1));
    }
    const QVariantMap pdf = ex->save(id, QStringLiteral("itinerary"), QStringLiteral("pdf"), all, QUrl::fromLocalFile(tmp.filePath(QStringLiteral("a.pdf"))).toString());
    r.check(pdf.value(QStringLiteral("ok")).toBool() && head(tmp.filePath(QStringLiteral("a.pdf")), 5) == "%PDF-" && pdf.value(QStringLiteral("pages")).toInt() >= 1,
            QStringLiteral("行程单 PDF 写出来了（file:/// 地址也认）"), QStringLiteral("%1 页，%2 KB").arg(pdf.value(QStringLiteral("pages")).toInt()).arg(QFileInfo(tmp.filePath(QStringLiteral("a.pdf"))).size() / 1024));
    const QVariantMap cp = ex->save(id, QStringLiteral("card"), QStringLiteral("pdf"), {}, tmp.filePath(QStringLiteral("c.pdf")));
    const QVariantMap ci = ex->save(id, QStringLiteral("card"), QStringLiteral("png"), {}, tmp.filePath(QStringLiteral("c.png")));
    r.check(cp.value(QStringLiteral("ok")).toBool() && ci.value(QStringLiteral("ok")).toBool() && inked(QImage(tmp.filePath(QStringLiteral("c.png")))), QStringLiteral("紧急信息卡 PDF 和 PNG"));
    const QVariantMap ic = ex->save(id, QStringLiteral("ics"), {}, {}, tmp.filePath(QStringLiteral("a.ics")));
    r.check(ic.value(QStringLiteral("ok")).toBool() && head(tmp.filePath(QStringLiteral("a.ics")), 17) == "BEGIN:VCALENDAR\r\n" && ic.value(QStringLiteral("count")).toInt() > 0,
            QStringLiteral(".ics 写出来了"), QStringLiteral("%1 项").arg(ic.value(QStringLiteral("count")).toInt()));
    r.check(ex->defaultName(id, QStringLiteral("itinerary"), QStringLiteral("png")).endsWith(QStringLiteral(" 行程单.png")) || ex->defaultName(id, QStringLiteral("itinerary"), QStringLiteral("png")).endsWith(QStringLiteral(".png")),
            QStringLiteral("默认文件名"), ex->defaultName(id, QStringLiteral("itinerary"), QStringLiteral("png")));
    // 给人看的页面图
    if (!reportDir().isEmpty()) {
        int pages = 0;
        ex->renderPage(id, QStringLiteral("itinerary"), all, 0, 900, &pages).save(reportDir() + QStringLiteral("/export-itinerary-p1.png"));
        ex->renderPage(id, QStringLiteral("itinerary"), all, pages / 2, 900).save(reportDir() + QStringLiteral("/export-itinerary-mid.png"));
        ex->renderPage(id, QStringLiteral("card"), {}, 0, 900).save(reportDir() + QStringLiteral("/export-card.png"));
        r.info(QStringLiteral("页面图：export-itinerary-p1.png、export-itinerary-mid.png（第 %1 / %2 页）、export-card.png").arg(pages / 2 + 1).arg(pages));
    }

    // ---- 对话框：入口、勾选刷新预览、E-10 三个按钮 ----
    QQuickItem *rootDlg = item(QStringLiteral("exportRoot"));
    QQuickItem *dlg = item(QStringLiteral("exportDialog"));
    if (!r.check(rootDlg && dlg, QStringLiteral("导出对话框在"))) { store->modify([&](Value &d) { d = before; }); return; }
    const auto isOpen = [&] { return dlg->property("open").toBool(); };
    QMetaObject::invokeMethod(win, "openExport", Q_ARG(QVariant, id), Q_ARG(QVariant, QStringLiteral("itinerary")), Q_ARG(QVariant, QStringLiteral("pdf")));
    r.check(waitFor(isOpen, 1500), QStringLiteral("打开导出对话框"));
    QQuickItem *img = item(QStringLiteral("exportPreview"));
    r.check(img && waitFor([&] { return img->property("status").toInt() == 1 && img->property("sourceSize").toSize().width() > 0; }, 3000), QStringLiteral("对话框里的第一页预览显示了"));
    const QString url0 = rootDlg->property("previewUrl").toString();
    QMetaObject::invokeMethod(rootDlg, "setOpt", Q_ARG(QVariant, QStringLiteral("refs")), Q_ARG(QVariant, true));
    r.check(waitFor([&] { return rootDlg->property("previewUrl").toString() != url0; }, 1500) && rootDlg->property("opts").toMap().value(QStringLiteral("refs")).toBool(),
            QStringLiteral("勾选订单号：预览重画"));
    // E-10：做一段还没算的自驾（第一站到第二站）
    store->modify([&](Value &d) {
        Value *p = plans::mutPlan(d, id);
        const QVector<int> vs = plans::validStops(*p);
        Value &s = p->mutObject().find(QStringLiteral("stops"))->mutArray()[size_t(vs[0])];
        s.mutObject().set(QStringLiteral("leg"), json::parse(QStringLiteral("{\"mode\":\"drive\"}")));
    });
    const int pend = roads->pendingFor(id);
    r.check(pend >= 1, QStringLiteral("这个计划有还没算的段"), QString::number(pend));
    QQuickItem *ask = item(QStringLiteral("exportRoadAsk"));
    const auto askOpen = [&] { return ask && ask->property("open").toBool(); };
    const QString f1 = tmp.filePath(QStringLiteral("e10-skip.pdf")), f2 = tmp.filePath(QStringLiteral("e10-calc.pdf"));
    rootDlg->setProperty("autoPath", f1);
    QMetaObject::invokeMethod(rootDlg, "begin");
    r.check(waitFor(askOpen, 1000), QStringLiteral("导出前问：还有路线没按道路算"));
    if (ask) QMetaObject::invokeMethod(ask, "chosen", Q_ARG(QString, QStringLiteral("cancel")));
    r.check(waitFor([&] { return !askOpen(); }, 1000) && isOpen() && !QFile::exists(f1), QStringLiteral("取消：不导出，导出对话框还开着"));
    QMetaObject::invokeMethod(rootDlg, "begin");
    waitFor(askOpen, 1000);
    if (ask) QMetaObject::invokeMethod(ask, "chosen", Q_ARG(QString, QStringLiteral("skip")));
    r.check(waitFor([&] { return QFile::exists(f1); }, 3000) && !isOpen() && roads->pendingFor(id) == pend, QStringLiteral("直接导出：写出文件，不算路"));
    // 先算好：用模拟的路线服务
    roads->setFetchForTest([](const QUrl &, std::function<void(int, const QByteArray &, int)> done) {
        const QByteArray body = QJsonDocument(QJsonObject{ { "code", "Ok" }, { "routes", QJsonArray{ QJsonObject{ { "geometry", "_p~iF~ps|U_ulLnnqC_mqNvxq`@" }, { "distance", 12000 }, { "duration", 900 } } } } }).toJson(QJsonDocument::Compact);
        QTimer::singleShot(20, [done, body] { done(200, body, 0); });
    });
    QMetaObject::invokeMethod(win, "openExport", Q_ARG(QVariant, id), Q_ARG(QVariant, QStringLiteral("itinerary")), Q_ARG(QVariant, QStringLiteral("pdf")));
    waitFor(isOpen, 1500);
    rootDlg->setProperty("autoPath", f2);
    QMetaObject::invokeMethod(rootDlg, "begin");
    waitFor(askOpen, 1000);
    if (ask) QMetaObject::invokeMethod(ask, "chosen", Q_ARG(QString, QStringLiteral("calc")));
    r.check(waitFor([&] { return QFile::exists(f2); }, 8000) && roads->pendingFor(id) == 0, QStringLiteral("先算好再导出：算完接着写出文件"),
            QStringLiteral("还没算 %1 段").arg(roads->pendingFor(id)));
    roads->setFetchForTest({});
    rootDlg->setProperty("autoPath", QString());
    dlg->setProperty("open", false);
    // 入口：⋯ 菜单的 4 个、简易面板的"导出…"（另两个 .trip、分享版还在做）
    QMetaObject::invokeMethod(win, "openTrip", Q_ARG(QVariant, id), Q_ARG(QVariant, -1));
    waitFor([&] { return item(QStringLiteral("tripExportPdf")) != nullptr; }, 3000);
    QStringList bad;
    for (const auto &[k, kind] : { std::pair{ "Pdf", "itinerary" }, std::pair{ "Image", "itinerary" }, std::pair{ "Ics", "ics" }, std::pair{ "Card", "card" } }) {
        QQuickItem *row = item(QStringLiteral("tripExport") + QLatin1String(k));
        dlg->setProperty("open", false);
        if (row) QMetaObject::invokeMethod(row, "clicked");
        if (!row || !waitFor(isOpen, 800) || rootDlg->property("kind").toString() != QLatin1String(kind)) bad << QLatin1String(k);
    }
    dlg->setProperty("open", false);
    r.check(bad.isEmpty(), QStringLiteral("⋯ 菜单的行程单 PDF / 长图 / .ics / 信息卡打开导出对话框"), bad.join(u' '));
    r.check(item(QStringLiteral("sheetExport")) != nullptr, QStringLiteral("简易面板的 ⋯ 里有“导出…”"));
    QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("map")));
    store->modify([&](Value &d) { d = before; });
    roads->recount();
}

} // namespace tp::app::selftest
