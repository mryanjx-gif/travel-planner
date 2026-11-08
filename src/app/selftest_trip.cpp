// 自检：0.14.1 .trip 计划文件、导入冲突（覆盖 / 另存 / 合并 / 跳过）、撤销、批量导出、分享版、拖入。
// 在验证用的数据副本上做，导出写到临时文件夹，最后把数据改回去（导入写进副本 photos\ 的照片留着，副本用完就删）。
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "jsonio.h"
#include "tripcontroller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>

namespace tp::app::selftest {

using json::Value;

namespace {
QQuickItem *findIt(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findIt(c, name)) return f;
    return nullptr;
}
QByteArray head(const QString &file, int n)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.read(n) : QByteArray();
}
} // namespace

void runTrip(Report &r, AppController *app, QQuickWindow *win)
{
    r.section(QStringLiteral("0.14.1 .trip、导入冲突、批量导出、分享版、拖入"));
    TripController *tc = tripsCtl();
    data::DataStore *store = app->store();
    QQuickItem *flow = findIt(win->contentItem(), QStringLiteral("tripFlow"));
    if (!r.check(tc && flow, QStringLiteral("trips 和 TripFlow 都在"))) return;
    const auto item = [&](const char *n) { return findIt(win->contentItem(), QString::fromLatin1(n)); };
    const auto plansN = [store] { return int(store->data()[QStringLiteral("plans")].asArray().size()); };
    const auto isOpen = [](QQuickItem *d) { return d && d->property("open").toBool(); };
    const auto inv = [flow](const char *fn, const QVariant &a = {}, const QVariant &b = {}) {
        if (b.isValid()) QMetaObject::invokeMethod(flow, fn, Q_ARG(QVariant, a), Q_ARG(QVariant, b));
        else QMetaObject::invokeMethod(flow, fn, Q_ARG(QVariant, a));
    };
    // 照片最多的计划 + 另一个计划
    QString id, id2, name;
    {
        int best = -1;
        for (const Value &p : store->data()[QStringLiteral("plans")].asArray()) {
            int n = 0;
            for (const Value &ph : store->data()[QStringLiteral("photos")].asArray()) n += ph[QStringLiteral("plan")].toString() == p[QStringLiteral("id")].toString();
            if (n > best) { best = n; if (!id.isEmpty()) id2 = id; id = p[QStringLiteral("id")].toString(); name = p[QStringLiteral("name")].toString(); }
            else if (id2.isEmpty()) id2 = p[QStringLiteral("id")].toString();
        }
    }
    if (!r.check(!id.isEmpty() && !id2.isEmpty(), QStringLiteral("数据里至少有两个计划"))) return;
    const Value before = store->data();
    QTemporaryDir tmp;

    // 导出
    const QString f = tmp.filePath(QStringLiteral("one.trip"));
    const QVariantMap ex = tc->exportTrip({ id }, f);
    r.check(ex.value(QStringLiteral("ok")).toBool() && head(f, 2) == "PK", QStringLiteral("导出 .trip（zip）"),
            ex.value(QStringLiteral("error")).toString() + QStringLiteral(" 照片 %1 张").arg(ex.value(QStringLiteral("photos")).toInt()));

    // 导入：撞了同一个计划 → 冲突框 → 另存一份；保存了、能撤销
    QQuickItem *conflict = item("tripConflict");
    const int n0 = plansN();
    flow->setProperty("autoPath", f);
    inv("importTrip", QString());
    const bool asked = waitFor([&] { return isOpen(conflict); }, 1500);
    r.check(asked && conflict->property("item").toMap().value(QStringLiteral("name")).toString() == name, QStringLiteral("导入同一个计划：弹出冲突框"));
    inv("decide", QStringLiteral("copy"));
    const bool copied = waitFor([&] { return plansN() == n0 + 1; }, 3000);
    const QString lastName = copied ? store->data()[QStringLiteral("plans")].asArray().back()[QStringLiteral("name")].toString() : QString();
    r.check(copied && lastName.startsWith(name) && lastName != name && !store->isDirty(), QStringLiteral("另存一份：多一个计划、名字加（导入）、已保存"), lastName);
    app->undo();
    r.check(waitFor([&] { return plansN() == n0; }, 1500), QStringLiteral("Ctrl+Z 撤销导入"));

    // 跳过（冲突框的取消）：数据不变
    inv("importTrip", QString());
    waitFor([&] { return isOpen(conflict); }, 1500);
    inv("decide", QStringLiteral("skip"));
    pause(300);
    r.check(plansN() == n0 && flow->property("lastResult").toString() == QLatin1String("cancelled") && !tc->importOpen(), QStringLiteral("冲突框选取消 = 跳过：数据不变"));

    // 覆盖 / 只合并消费和照片（直接调接口）
    QVariantMap o = tc->openImport(f);
    r.check(o.value(QStringLiteral("ok")).toBool() && o.value(QStringLiteral("plans")).toList().value(0).toMap().value(QStringLiteral("conflict")).toBool(),
            QStringLiteral("openImport 认出已经有了"));
    QVariantMap c = tc->commitImport({ { id, QStringLiteral("merge") } });
    r.check(c.value(QStringLiteral("ok")).toBool() && plansN() == n0, QStringLiteral("只合并消费和照片：计划数不变"), c.value(QStringLiteral("message")).toString());
    store->modify([id](Value &d) {
        for (Value &p : d.mutObject().find(QStringLiteral("plans"))->mutArray())
            if (p[QStringLiteral("id")].toString() == id) p.mutObject().set(QStringLiteral("name"), Value(QStringLiteral("自检改过的名字")));
    });
    app->commitStep();
    tc->openImport(f);
    c = tc->commitImport({ { id, QStringLiteral("replace") } });
    QString nm;
    for (const Value &p : store->data()[QStringLiteral("plans")].asArray()) if (p[QStringLiteral("id")].toString() == id) nm = p[QStringLiteral("name")].toString();
    r.check(c.value(QStringLiteral("ok")).toBool() && nm == name, QStringLiteral("覆盖：换回导入的"), nm);

    // 不是 .trip、坏文件
    o = tc->openImport(tmp.filePath(QStringLiteral("x.jpg")));
    r.check(!o.value(QStringLiteral("ok")).toBool() && o.value(QStringLiteral("error")).toString().contains(QStringLiteral(".trip")), QStringLiteral("别的文件拦下"));
    QFile junk(tmp.filePath(QStringLiteral("junk.trip")));
    if (junk.open(QIODevice::WriteOnly)) { junk.write("not a zip"); junk.close(); }
    o = tc->openImport(junk.fileName());
    r.check(!o.value(QStringLiteral("ok")).toBool(), QStringLiteral("坏的 .trip 拦下"), o.value(QStringLiteral("error")).toString());
    r.check(item("tripDrop") && tc->isTripFile(QStringLiteral("file:///C:/a/b.TRIP")) && !tc->isTripFile(QStringLiteral("file:///C:/a/b.jpg")), QStringLiteral("拖入：认 .trip"));

    // 批量导出：对话框、每个计划单独一个
    QQuickItem *bulk = item("tripBulk");
    inv("bulk", QVariantList{ id, id2 });
    r.check(waitFor([&] { return isOpen(bulk); }, 800), QStringLiteral("批量导出对话框"));
    bulk->setProperty("open", false);
    QTemporaryDir eachDir;
    flow->setProperty("autoPath", eachDir.path());
    flow->setProperty("shareEach", false);
    inv("start", QVariantList{ id, id2 }, QStringLiteral("each"));
    QQuickItem *roadAsk = item("tripRoadAsk");
    if (waitFor([&] { return isOpen(roadAsk); }, 300)) QMetaObject::invokeMethod(roadAsk, "chosen", Q_ARG(QString, QStringLiteral("skip")));
    r.check(waitFor([&] { return QDir(eachDir.path()).entryList({ QStringLiteral("*.trip") }, QDir::Files).size() == 2; }, 3000),
            QStringLiteral("每个计划单独一个 .trip"), QString::number(QDir(eachDir.path()).entryList({ QStringLiteral("*.trip") }, QDir::Files).size()));

    // 分享版：对话框 → 选文件夹 → 结果；私人内容去掉
    QTemporaryDir shareDir;
    flow->setProperty("autoPath", shareDir.path());
    QQuickItem *share = item("tripShare"), *done = item("tripShareDone");
    inv("exportShare", QVariantList{});
    if (waitFor([&] { return isOpen(roadAsk); }, 300)) QMetaObject::invokeMethod(roadAsk, "chosen", Q_ARG(QString, QStringLiteral("skip")));
    r.check(waitFor([&] { return isOpen(share); }, 800), QStringLiteral("分享版选项对话框"));
    share->setProperty("open", false);
    QMetaObject::invokeMethod(flow, "pickFolder", Q_ARG(QVariant, QStringLiteral("share")));
    r.check(waitFor([&] { return isOpen(done); }, 5000), QStringLiteral("分享版导出完的提示"));
    const QStringList dirs = QDir(shareDir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const QString sd = dirs.isEmpty() ? QString() : QDir(shareDir.path()).filePath(dirs.first());
    const QString txt = QString::fromUtf8(head(QDir(sd).filePath(QStringLiteral("travel.json")), 1 << 26));
    const Value sv = json::parse(txt);
    bool clean = sv.isObject() && !sv.asObject().contains(QStringLiteral("emergency")) && !sv.asObject().contains(QStringLiteral("documents"));
    for (const Value &p : sv[QStringLiteral("plans")].asArray()) clean = clean && !p.asObject().contains(QStringLiteral("localEmergency")) && !p.asObject().contains(QStringLiteral("expenses"));
    QStringList credits = QDir(sd).entryList({ QStringLiteral("*.txt") }, QDir::Files);
    r.check(clean && credits.size() == 1 && head(QDir(sd).filePath(credits.value(0)), 3) == "\xEF\xBB\xBF", QStringLiteral("分享版：没有紧急信息、证件、消费，署名文件带 BOM"), sd);
    if (done) done->setProperty("open", false);
    flow->setProperty("autoPath", QString());

    // Electron 再导入的冲突列表：读不到时报错
    r.check(!app->electronConflicts(tmp.filePath(QStringLiteral("nope"))).value(QStringLiteral("ok")).toBool(), QStringLiteral("Electron 冲突列表：文件夹不对时报错"));

    store->modify([&](Value &d) { d = before; });
    app->commitStep();
}

} // namespace tp::app::selftest
