// 自检："已解锁"手动标记（0.8.5）、内存（0.8.6）。0.8.7 从 selftest.cpp 拆出来，内容没改。
#include "selftest.h"
#include "selftest_util.h"

#include "appcontroller.h"
#include "datastore.h"
#include "exif.h"
#include "globeitem.h"
#include "globetiles.h"
#include "mapcontroller.h"
#include "markerimages.h"
#include "netclient.h"
#include "roadrouter.h"
#include "roads.h"
#include "photocontroller.h"
#include "photolib.h"
#include "procmem.h"
#include "thumbcache.h"
#include "tilecache.h"
#include "tileserver.h"
#include "unlock.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutex>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <cmath>
#include <functional>
#include <memory>

namespace tp::app::selftest {

// ---------- "已解锁"手动标记（0.8.5） ----------
void runUnlockManual(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("已解锁手动标记（0.8.5）"));
    Q_UNUSED(win)
    data::DataStore *store = app->store();
    mapc->setBase(QStringLiteral("unlock"));
    waitFor([mapc] { return mapc->unlockReady(); }, 3000);
    if (!r.check(mapc->unlockReady(), QStringLiteral("足迹框：省界数据加载了"),
                 QStringLiteral("%1 个国家 / 地区 · %2 个省 / 州；手动标记 %3 个").arg(mapc->unlockCountries()).arg(mapc->unlockProvinces()).arg(mapc->manualCount())))
        return;
    mapc->setUnlockEdit(true);
    // 找一个没去过的省（冰岛 / 格陵兰 / 蒙古……第一个没被点亮的）
    QString id, name;
    for (const char *q : { "冰岛", "格陵兰", "蒙古", "智利", "秘鲁", "挪威" }) {
        for (const QVariant &v : mapc->unlockRows(QString::fromUtf8(q))) {
            const QVariantMap m = v.toMap();
            if (!m.value(QStringLiteral("byPlan")).toBool() && !m.value(QStringLiteral("manual")).toBool()) { id = m.value(QStringLiteral("id")).toString(); name = m.value(QStringLiteral("name")).toString(); break; }
        }
        if (!id.isEmpty()) break;
    }
    if (id.isEmpty()) { r.check(true, QStringLiteral("手动标记（跳过）"), QStringLiteral("找不到没去过的省")); mapc->setUnlockEdit(false); return; }
    const int provBefore = mapc->unlockProvinces(), manualBefore = mapc->manualCount();
    // 1. 打勾：上色、写进 visitedRegions、0.7 秒后自动保存
    const bool changed = mapc->setManual(id, true);
    const bool colored = mapc->unlockResult().colorById.contains(id) && mapc->unlockProvinces() == provBefore + 1 && mapc->manualCount() == manualBefore + 1;
    const bool dirty = store->isDirty();
    const bool saved = waitFor([store] { return !store->isDirty(); }, 4000);
    bool inData = false;
    for (const json::Value &v : store->data()[QStringLiteral("visitedRegions")].asArray()) if (v[QStringLiteral("id")].toString() == id) inData = true;
    r.check(changed && colored && dirty && saved && inData, QStringLiteral("打勾：上色、写进数据、停手 0.7 秒自动保存"),
            QStringLiteral("%1（%2）：%3，省 %4 → %5，%6").arg(name, id).arg(colored ? QStringLiteral("上色了") : QStringLiteral("没上色"))
                .arg(provBefore).arg(mapc->unlockProvinces()).arg(saved && inData ? QStringLiteral("已保存") : QStringLiteral("没保存")));
    // 2. 年份：四位数字才记（存成字符串，和 Electron 一样），别的删掉
    {
        const QString a = mapc->setManualYear(id, QStringLiteral(" 2019 "));
        QString stored;
        for (const json::Value &v : store->data()[QStringLiteral("visitedRegions")].asArray())
            if (v[QStringLiteral("id")].toString() == id) stored = v[QStringLiteral("year")].isString() ? v[QStringLiteral("year")].toString() : QStringLiteral("(不是字符串)");
        const QString b = mapc->setManualYear(id, QStringLiteral("19"));
        bool removed = true;
        for (const json::Value &v : store->data()[QStringLiteral("visitedRegions")].asArray())
            if (v[QStringLiteral("id")].toString() == id && v.asObject().contains(QStringLiteral("year"))) removed = false;
        mapc->setManualYear(id, QStringLiteral("2019"));
        r.check(a == QLatin1String("2019") && stored == QLatin1String("2019") && b.isEmpty() && removed, QStringLiteral("年份：四位数字才记"),
                QStringLiteral("\" 2019 \" → %1（数据里 \"%2\"）；\"19\" → %3").arg(a, stored, removed ? QStringLiteral("删掉了") : QStringLiteral("还在")));
    }
    // 3. 在地图上点这个省：悬停描边 + 提示框，点一下取消
    {
        double lat = 0, lng = 0;
        bool hit = false;
        QString tip;
        // 先缩到能看到整个世界，按 10 px 网格扫一遍找属于这个省的点，再放大到那里
        for (double z : { 3.0, 4.5 }) {
            if (hit) break;
            mapc->jumpTo(20, 0, 1.2);
            waitMapIdle(win, mapc);
            for (double y = 20; y < win->height() - 20 && !hit; y += 10)
                for (double x = 20; x < win->width() - 20 && !hit; x += 10)
                    if (mapc->unlockIdAt(x, y) == id) {
                        const QVariantList ll = mapc->coordinateFor(x, y);
                        lat = ll.value(0).toDouble(); lng = ll.value(1).toDouble();
                        mapc->jumpTo(lat, lng, z);
                        waitMapIdle(win, mapc);
                        const QPointF p = mapc->pixelFor(lat, lng);
                        if (mapc->unlockIdAt(p.x(), p.y()) == id) {
                            tip = mapc->unlockEditHover(p.x(), p.y());
                            const bool outline = mapc->unlockHoverId() == id && mapc->unlockHoverJson().contains(QStringLiteral("Polygon"));
                            hit = outline && mapc->unlockEditClick(p.x(), p.y());
                        }
                    }
        }
        const bool off = hit && !mapc->unlockResult().colorById.contains(id) && mapc->unlockProvinces() == provBefore;
        r.check(off && tip.contains(QStringLiteral("手动标记")), QStringLiteral("地图上悬停描边、点省份取消"),
                QStringLiteral("提示框「%1」；点了以后%2").arg(QString(tip).replace(QStringLiteral("<br>"), QStringLiteral(" / ")),
                                                          off ? QStringLiteral("不再上色") : QStringLiteral("还在")));
        waitFor([store] { return !store->isDirty(); }, 4000);
    }
    // 4. 被计划点亮的省：不能手动标记，只提示
    {
        QString planId;
        for (auto it = mapc->unlockResult().plansById.constBegin(); it != mapc->unlockResult().plansById.constEnd(); ++it)
            if (!it.value().isEmpty()) { planId = it.key(); break; }
        if (planId.isEmpty()) r.check(true, QStringLiteral("被计划点亮的省（跳过）"), QStringLiteral("没有已完成的计划"));
        else {
            QStringList toasts;
            const QMetaObject::Connection tc = QObject::connect(mapc, &map::MapController::toast, mapc, [&toasts](const QString &t) { toasts << t; });
            const int m0 = mapc->manualCount();
            mapc->toggleRegion(planId);
            const bool no = !mapc->setManual(planId, true);
            QObject::disconnect(tc);
            r.check(toasts.size() == 1 && no && mapc->manualCount() == m0, QStringLiteral("被计划点亮的省：只提示，不能手动标记"),
                    toasts.isEmpty() ? QStringLiteral("没提示") : toasts.first());
        }
    }
    // 5. 撤销：回到打勾以后的样子（上一步"点省份取消"是一步）
    {
        app->undo();
        pause(200);
        const bool back = mapc->unlockResult().colorById.contains(id);
        app->redo();
        pause(200);
        const bool again = !mapc->unlockResult().colorById.contains(id);
        r.check(back && again && mapc->manualCount() == manualBefore, QStringLiteral("撤销 / 重做"),
                QStringLiteral("撤销后%1，重做后%2").arg(back ? QStringLiteral("又上色了") : QStringLiteral("没变"), again ? QStringLiteral("取消了") : QStringLiteral("没变")));
    }
    // 6. 搜索：同一组只列一行
    {
        int hk = 0;
        for (const QVariant &v : mapc->unlockRows(QStringLiteral("香港"))) if (v.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("香港")) ++hk;
        const int sc = int(mapc->unlockRows(QStringLiteral("四川")).size());
        r.check(hk == 1 && sc >= 1, QStringLiteral("搜索：同一组只列一行"), QStringLiteral("香港 %1 行，四川 %2 行").arg(hk).arg(sc));
    }
    mapc->setUnlockEdit(false);
    mapc->setBase(QStringLiteral("street"));
}

// 0.8.6 内存：离开"已解锁"收回省界、程序自己读得到显存（离开地球释放显存 0.9.0 起在 selftest_globe.cpp）
void runMemory(Report &r, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("内存（0.8.6）"));
    QQuickItem *item = win ? win->findChild<QQuickItem *>(QStringLiteral("map")) : nullptr;
    const auto waitStyle = [item] {
        if (!item) return;
        waitFor([item] { return !item->property("styleLoaded").toBool(); }, 1500);
        waitFor([item] { return item->property("styleLoaded").toBool(); }, 10000);
    };
    // 1. 省界只在"已解锁"下交给地图库。0.9.2 起写在骨架样式里：已解锁的样式是省界文件的地址（地图库在后台读），
    // 其他底图的样式里是空的数据源；换样式就收回
    {
        const auto styleText = [mapc] {
            QString path = mapc->styleUrl();
            if (path.startsWith(QLatin1String("file://"))) path = path.mid(7);
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        mapc->setBase(QStringLiteral("unlock"));
        waitStyle();
        const QString inUnlock = styleText();
        mapc->setBase(QStringLiteral("street"));
        waitStyle();
        const QString after = styleText();
        r.check(inUnlock.contains(QLatin1String("adm1.geojson")) && inUnlock.contains(QLatin1String("adm0.geojson"))
                    && after.contains(QLatin1String("\"u-adm1\"")) && !after.contains(QLatin1String("adm1.geojson")),
                QStringLiteral("省界只在已解锁时交给地图库（样式里是文件地址，地图库在后台读）"),
                QStringLiteral("已解锁的样式 %1 字节，街道图的样式 %2 字节").arg(inUnlock.size()).arg(after.size()));
    }
    // 2. 地球离开时释放：0.9.0 起在「地球（0.9.0）」一节查（按视野加载以后不再有整张拼图）
    // 3. 程序自己读本进程内存和显存（Windows：DXGI；云端 Linux 读不到显存，只看私有内存）
    {
        const ProcMem m = readProcMem();
#ifdef Q_OS_WIN
        const bool ok = m.privateMB > 0 && m.gpuLocalMB >= 0;
#else
        const bool ok = m.privateMB > 0;
#endif
        r.check(ok, QStringLiteral("读得到本进程的内存和显存"),
                QStringLiteral("私有 %1 MB，显存 %2").arg(m.privateMB, 0, 'f', 0)
                    .arg(m.gpuLocalMB < 0 ? QStringLiteral("读不到") : QStringLiteral("%1 MB（本地）+ %2 MB（借用）").arg(m.gpuLocalMB, 0, 'f', 0).arg(m.gpuNonLocalMB, 0, 'f', 0)));
    }
}

} // namespace tp::app::selftest
