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
#include <QQuickWindow>
#include <QScreen>
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

namespace tp::app {

using namespace selftest;

namespace {

// tests/photos.txt（每行"文件名<Tab>base64"）解到 dir，返回文件列表
QStringList extractFixtures(const QString &dir)
{
    QStringList out;
#ifdef TP_SOURCE_DIR
    QFile pack(QStringLiteral(TP_SOURCE_DIR "/tests/photos.txt"));
    if (!pack.open(QIODevice::ReadOnly)) return out;
    for (const QByteArray &l : pack.readAll().split('\n')) {
        const int tab = l.indexOf('\t');
        if (l.startsWith('#') || tab <= 0) continue;
        QFile f(dir + u'/' + QString::fromUtf8(l.left(tab)));
        if (f.open(QIODevice::WriteOnly)) { f.write(QByteArray::fromBase64(l.mid(tab + 1).trimmed())); out << f.fileName(); }
    }
#else
    Q_UNUSED(dir)
#endif
    return out;
}

QVariantMap findPhoto(PhotoController *photos, const QString &suffix)
{
    const QVariantList l = photos->library();
    for (auto it = l.crbegin(); it != l.crend(); ++it) {
        const QVariantMap m = it->toMap();
        if (m.value(QStringLiteral("file")).toString().endsWith(suffix)) return m;
    }
    return {};
}

void setOld(const QString &abs, int minutesAgo)
{
    QFile f(abs);
    if (!f.open(QIODevice::ReadWrite)) return;
    const QDateTime t = QDateTime::currentDateTime().addSecs(-60 * minutesAgo);
    f.setFileTime(t, QFileDevice::FileModificationTime);
    f.setFileTime(t, QFileDevice::FileBirthTime);   // Windows 有创建时间；Linux 设不了，忽略
}

void runAll(Report &r, AppController *app, PhotoController *photos, map::MapController *mapc, QQuickWindow *win, const QString &backend,
            services::RoadRouter *roads, services::NetClient *net, services::TileServer *ts, services::TileCache *tileCache)
{
    data::DataStore *store = app->store();
    const QString dir = store->dataDir();
    r.lines << QStringLiteral("# 旅行计划 %1 自检  %2").arg(app->version(), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    r.info(QStringLiteral("系统：%1；图形后端：%2").arg(QSysInfo::prettyProductName(), backend));
    r.info(QStringLiteral("数据（副本）：%1，%2 张照片").arg(QDir::toNativeSeparators(dir)).arg(photos->count()));
    // 0.8.10：0.8.9 在 PO 电脑上右边的按钮点不中（疑似窗口右半截在屏幕外，Windows差异清单 #26）：写出窗口和屏幕的位置
    if (QScreen *sc = win ? win->screen() : nullptr) {
        const QRect g = win->geometry(), a = sc->availableGeometry();
        r.info(QStringLiteral("窗口 %1,%2 %3×%4；屏幕可用区域 %5,%6 %7×%8；缩放 %9；%10")
                   .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()).arg(a.x()).arg(a.y()).arg(a.width()).arg(a.height())
                   .arg(sc->devicePixelRatio()).arg(a.contains(g) ? QStringLiteral("窗口全在屏幕里") : QStringLiteral("窗口有一部分在屏幕外")));
    }
    if (!r.check(!store->isReadOnly() && store->state() != data::DataStore::State::None, QStringLiteral("数据能写"), store->loadError())) return;
    // 0.9.3：环境变量 TP_SELFTEST_ONLY=globe,photos…（逗号分隔）只跑其中几节（云端没有 maplibre 时只测地球用；PO 电脑上不设，全跑）
    const QStringList only = qEnvironmentVariable("TP_SELFTEST_ONLY").split(u',', Qt::SkipEmptyParts);
    // 0.12.11：每个分组开始前把上一组留下的改动存掉。store->modify 不安排保存（程序里都接 noteChanged，自检的收尾没有），
    // 留着不存，下一组第一次保存就把"收尾还原"和它的改动并成一步，撤销一下子退回上一组中途的样子（week,kit 的 8 项）
    const auto want = [&only, app](const char *key) { app->commitStep(); return only.isEmpty() || only.contains(QLatin1String(key)); };
    if (!only.isEmpty()) r.info(QStringLiteral("只跑：%1（TP_SELFTEST_ONLY）").arg(only.join(QStringLiteral("、"))));
    if (only.contains(QLatin1String("cachemeasure"))) runCacheMeasure(r, mapc, win, ts, tileCache);   // 0.12.6 要真联网，只有点名才跑
    if (want("glass")) runGlass(r, app, win);   // 0.12.1
    if (want("perf")) runPerf(r, app, mapc, win);   // 0.9.1：放在最前面（数据、筛选都还是刚打开时的样子）
    if (want("map")) runMap(r, app, mapc, win);
    if (want("markers")) runMarkers(r, mapc, win);
    if (want("routes")) runRoutes(r, app, mapc, win, roads, net);
    if (want("tiles")) runTiles(r, mapc, win, net, ts, tileCache);
    if (want("unlock")) runUnlockManual(r, app, mapc, win);
    if (want("memory")) runMemory(r, mapc, win);
    if (want("globe")) runGlobe(r, app, mapc, win, ts);
    if (want("plans")) runPlans(r, app, mapc, win);
    if (want("shell")) runShell(r, app, mapc, win);
    if (want("pages")) runPages(r, app, mapc, win);
    if (want("editor")) runEditor(r, app, mapc, win, roads);
    if (want("planpage")) runPlanPage(r, app, mapc, win, planPageCtl(), navCtl());
    if (want("money")) runMoney(r, app, win, planPageCtl(), navCtl(), ratesCtl());
    if (want("split")) runSplit(r, app, win, planPageCtl(), navCtl(), ratesCtl());
    if (want("weather")) runWeather(r, app, win, planPageCtl(), navCtl(), weatherCtl());
    if (want("kit")) runKit(r, app, win, planPageCtl(), navCtl());
    if (want("docs")) runDocs(r, app, win, planPageCtl(), navCtl(), ratesCtl());   // 0.12.4
    if (want("netset")) runNetAbout(r, app, mapc, win, navCtl(), net, ratesCtl(), weatherCtl(), photos);   // 0.12.5
    if (want("planphotos")) runPlanPhotos(r, app, win, planPageCtl(), navCtl(), photos);
    if (want("library")) runLibrary(r, app, mapc, win);
    if (want("photomode")) runPhotoMode(r, app, mapc, win, photos);
    if (want("calendar")) runCalendar(r, app, win);
    if (want("week")) runCalendarWeek(r, app, mapc, win);
    if (want("journal")) runJournal(r, app, win);
    if (want("audit")) runAudit(r, app, mapc, win, photos);
    if (want("tour")) runTour(r, app, win);   // 0.12.8
    if (want("ai")) runAi(r, app, win, net);   // 0.13.0
    if (want("export")) runExport(r, app, win, roads);   // 0.14.0
    if (want("welcome")) runWelcome(r, app, win);   // 0.12.7（中途换到临时数据，最后换回来）
    if (only.contains(QLatin1String("thumbs1000"))) runThumbs1000(r, photos, dir);
    mapc->fitAll(false);
    if (!want("photos")) return;

    QTemporaryDir tmp;
    // ---------- 缩略图 ----------
    r.section(QStringLiteral("缩略图（另用一个临时缓存，不动你的缓存）"));
    {
        data::ThumbCache cache(tmp.filePath(QStringLiteral("thumbs")));
        QStringList files;
        for (const QVariant &v : photos->library()) {
            const QVariantMap m = v.toMap();
            if (!m.value(QStringLiteral("missing")).toBool() && !m.value(QStringLiteral("heic")).toBool())
                files << dir + u'/' + m.value(QStringLiteral("file")).toString();
        }
        // 0.8.2：失败时记下是哪张、什么错误（0.8.1 验证时偶发 1 / 120 失败，原因待查）
        QMutex failMutex;
        QStringList failures;
        auto pass = [&](qint64 *ms, int *failed) {
            std::atomic<int> bad { 0 };
            QThreadPool pool;
            pool.setMaxThreadCount(2);   // 和程序里一样最多 2 张同时做
            QElapsedTimer t;
            t.start();
            for (const QString &f : files)
                pool.start([&cache, f, &bad, &failMutex, &failures] {
                    QString e;
                    if (cache.ensure(f, 320, &e).isEmpty()) {
                        ++bad;
                        QMutexLocker lk(&failMutex);
                        failures << QStringLiteral("%1（%2）").arg(QDir::toNativeSeparators(f), e.isEmpty() ? QStringLiteral("没有错误信息") : e);
                    }
                });
            waitFor([&pool] { return pool.waitForDone(1); }, 600000);
            *ms = t.elapsed();
            *failed = bad;
        };
        qint64 cold = 0, warm = 0;
        int badCold = 0, badWarm = 0;
        pass(&cold, &badCold);
        const QStringList coldFailures = failures;
        failures.clear();
        pass(&warm, &badWarm);
        r.check(badCold == 0, QStringLiteral("生成缩略图"), QStringLiteral("%1 张，失败 %2 张").arg(files.size()).arg(badCold));
        for (const QString &f : coldFailures.mid(0, 10)) r.info(QStringLiteral("失败：") + f);
        if (badWarm) {
            r.info(QStringLiteral("第二次（读缓存）也失败 %1 张").arg(badWarm));
            for (const QString &f : failures.mid(0, 10)) r.info(QStringLiteral("失败：") + f);
        }
        r.info(QStringLiteral("第一次（生成）：%1 ms，平均 %2 ms/张；第二次（读缓存）：%3 ms")
                   .arg(cold).arg(files.isEmpty() ? 0 : cold / files.size()).arg(warm));
        r.info(QStringLiteral("缓存大小：%1 KB").arg(cache.totalBytes() / 1024));
    }

    // ---------- 导入 ----------
    r.section(QStringLiteral("导入照片（tests/photos.txt 里的 14 张测试照片）"));
    const QString fxDir = tmp.filePath(QStringLiteral("fixtures"));
    QDir().mkpath(fxDir);
    const QStringList fx = extractFixtures(fxDir);
    if (!r.check(fx.size() >= 14, QStringLiteral("测试照片"), QStringLiteral("%1 张").arg(fx.size()))) return;
    QVariantList urls;
    for (const QString &f : fx) urls << QUrl::fromLocalFile(f);
    auto finished = std::make_shared<QVariantMap>();
    auto got = std::make_shared<bool>(false);
    QObject::connect(photos, &PhotoController::importFinished, photos, [finished, got](const QVariantMap &m) { *finished = m; *got = true; });
    const int before = photos->count();
    QElapsedTimer t;
    t.start();
    QVariantMap res = photos->startImport(urls);
    r.check(res.value(QStringLiteral("ok")).toBool(), QStringLiteral("开始导入"), res.value(QStringLiteral("error")).toString());
    const bool reviewed = waitFor([photos] { return photos->importStage() == QLatin1String("review"); }, 60000);
    r.check(reviewed, QStringLiteral("读拍摄信息、出确认表"), QStringLiteral("%1 ms，%2 行").arg(t.elapsed()).arg(photos->importItems().size()));
    if (!reviewed) return;
    {
        int dup = 0, withTz = 0, heic = 0;
        for (const QVariant &v : photos->importItems()) {
            const QVariantMap m = v.toMap();
            dup += m.value(QStringLiteral("dup")).toBool();
            withTz += !m.value(QStringLiteral("tz")).toString().isEmpty();
            heic += m.value(QStringLiteral("heic")).toBool();
        }
        r.check(withTz >= 5 && heic == 2, QStringLiteral("确认表内容"), QStringLiteral("有时区 %1 张、HEIC %2 张、已经有了 %3 张").arg(withTz).arg(heic).arg(dup));
    }
    *got = false;
    t.restart();
    photos->confirmImport();
    waitFor([got] { return *got; }, 120000);
    const int added = finished->value(QStringLiteral("count")).toInt();
    r.check(*got && finished->value(QStringLiteral("ok")).toBool(), QStringLiteral("复制、保存"),
            QStringLiteral("%1 ms，导入 %2 张，跳过 %3 张 %4").arg(t.elapsed()).arg(added).arg(finished->value(QStringLiteral("skipped")).toInt())
                .arg(finished->value(QStringLiteral("error")).toString()));
    r.check(photos->count() == before + added && added > 0, QStringLiteral("照片库多了这些记录"), QStringLiteral("%1 → %2").arg(before).arg(photos->count()));
    const QVariantMap le = findPhoto(photos, QStringLiteral("_full_le.jpg"));
    r.check(QFileInfo::exists(dir + u'/' + le.value(QStringLiteral("file")).toString())
                && le.value(QStringLiteral("tz")).toString() == QLatin1String("+09:00")
                && le.value(QStringLiteral("camera")).toString() == QLatin1String("Apple iPhone 15 Pro"),
            QStringLiteral("文件和拍摄信息"), le.value(QStringLiteral("file")).toString());
    r.check(!store->isDirty(), QStringLiteral("已经存进 travel.json"));

    // 再导入同一批：应该全部标成"已经有了"
    *got = false;
    photos->startImport(urls);
    if (waitFor([photos] { return photos->importStage() == QLatin1String("review"); }, 60000)) {
        int dup = 0;
        const QVariantList items = photos->importItems();
        for (const QVariant &v : items) dup += v.toMap().value(QStringLiteral("dup")).toBool();
        r.check(dup == items.size() && dup > 0, QStringLiteral("查重复"), QStringLiteral("%1 / %2 张标成已经有了").arg(dup).arg(items.size()));
        photos->cancelImport();
    } else {
        r.check(false, QStringLiteral("查重复"), QStringLiteral("确认表没出来"));
    }
    waitFor([photos] { return photos->importStage().isEmpty(); }, 10000);

    // ---------- 撤销 / 重做 ----------
    r.section(QStringLiteral("撤销 / 重做"));
    const int afterImport = photos->count();
    app->undo();
    pause(200);
    r.check(photos->count() == afterImport - added, QStringLiteral("撤销导入"), QStringLiteral("%1 张").arg(photos->count()));
    app->redo();
    pause(200);
    r.check(photos->count() == afterImport, QStringLiteral("重做"), QStringLiteral("%1 张").arg(photos->count()));

    // ---------- 旋转 ----------
    r.section(QStringLiteral("旋转"));
    data::ThumbCache cache2(tmp.filePath(QStringLiteral("thumbs2")));
    struct Case { const char *suffix; const char *name; int wantOrient; };   // wantOrient：转完以后的 EXIF 方向（JPEG 只改标记）
    for (const Case &c : { Case { "_full_le.jpg", "JPG（只改方向标记）", 3 }, Case { "_exif.png", "PNG（重新保存）", 1 },
                           Case { "_lossy.webp", "WebP（重新保存）", 1 } }) {
        const QVariantMap p = findPhoto(photos, QString::fromLatin1(c.suffix));
        const QString abs = dir + u'/' + p.value(QStringLiteral("file")).toString();
        const int w0 = p.value(QStringLiteral("width")).toInt(), h0 = p.value(QStringLiteral("height")).toInt();
        const QVariantMap rr = photos->rotate(p.value(QStringLiteral("id")).toString(), 1);
        const data::exif::Info info = data::exif::readFile(abs);
        const QVariantMap p2 = findPhoto(photos, QString::fromLatin1(c.suffix));
        // 记录原来有尺寸：转 90° 后宽高互换；原来没有（旧记录）：转完写上摆正后的尺寸
        const bool sizeOk = (w0 > 0 && h0 > 0) ? (p2.value(QStringLiteral("width")).toInt() == h0 && p2.value(QStringLiteral("height")).toInt() == w0)
                                               : (p2.value(QStringLiteral("width")).toInt() == info.displayWidth() && p2.value(QStringLiteral("height")).toInt() == info.displayHeight());
        const bool backup = QFileInfo::exists(dir + QStringLiteral("/photos/.originals/") + p.value(QStringLiteral("file")).toString().mid(7));
        QString e;
        const QString thumb = cache2.ensure(abs, 320, &e);
        const QImage ti(thumb);
        r.check(rr.value(QStringLiteral("ok")).toBool() && info.orientation == c.wantOrient && sizeOk && backup && !ti.isNull()
                    && ti.width() == info.displayWidth() && ti.height() == info.displayHeight(),
                QString::fromUtf8(c.name),
                QStringLiteral("%1；方向 %2；记录尺寸 %3×%4 → %5×%6；原图备份%7；缩略图 %8×%9 %10")
                    .arg(rr.value(QStringLiteral("ok")).toBool() ? QStringLiteral("转了") : rr.value(QStringLiteral("error")).toString())
                    .arg(info.orientation).arg(w0).arg(h0).arg(p2.value(QStringLiteral("width")).toInt()).arg(p2.value(QStringLiteral("height")).toInt())
                    .arg(backup ? QStringLiteral("有") : QStringLiteral("没有")).arg(ti.width()).arg(ti.height()).arg(e));
    }
    {
        const QVariantMap g = findPhoto(photos, QStringLiteral("_plain.gif"));
        const QVariantMap rr = photos->rotate(g.value(QStringLiteral("id")).toString(), 1);
        r.check(!rr.value(QStringLiteral("ok")).toBool() && rr.value(QStringLiteral("error")).toString() == QStringLiteral("这种格式不能旋转"),
                QStringLiteral("GIF 不许转"), rr.value(QStringLiteral("error")).toString());
    }
    r.check(!store->isDirty(), QStringLiteral("旋转后已保存"));

    // ---------- 清理 ----------
    r.section(QStringLiteral("清理没用的照片（只移这次放进去的一个测试文件）"));
    const QString strayRel = QStringLiteral("photos/selftest/stray-%1.jpg").arg(QDateTime::currentMSecsSinceEpoch());
    const QString strayAbs = dir + u'/' + strayRel;
    QDir().mkpath(QFileInfo(strayAbs).absolutePath());
    QFile::copy(fx.value(0), strayAbs);
    auto listHas = [](const QVariantMap &scan, const char *key, const QString &rel) {
        for (const QVariant &v : scan.value(QString::fromLatin1(key)).toList()) if (v.toMap().value(QStringLiteral("rel")).toString() == rel) return true;
        return false;
    };
    QVariantMap scan = photos->scanCleanup();
    r.check(scan.value(QStringLiteral("ok")).toBool() && !listHas(scan, "unused", strayRel), QStringLiteral("刚放进来的先不动（10 分钟保护）"),
            QStringLiteral("这次先不动 %1 个 %2").arg(scan.value(QStringLiteral("keptRecent")).toInt()).arg(scan.value(QStringLiteral("error")).toString()));
    setOld(strayAbs, 120);
    scan = photos->scanCleanup();
    r.check(listHas(scan, "unused", strayRel), QStringLiteral("过了保护时间就列出来"),
            QStringLiteral("没用的 %1 个").arg(scan.value(QStringLiteral("unused")).toList().size()));
    bool trashed = false;
    QVariantMap trashRes;
    QObject::connect(photos, &PhotoController::trashFinished, photos, [&trashed, &trashRes](const QVariantMap &m) { trashRes = m; trashed = true; });
    const QVariantMap tr = photos->trashFiles({ strayRel });
    waitFor([&trashed] { return trashed; }, 60000);
    r.check(tr.value(QStringLiteral("ok")).toBool() && trashRes.value(QStringLiteral("moved")).toInt() == 1 && !QFileInfo::exists(strayAbs),
            QStringLiteral("移到回收站"),
            QStringLiteral("移了 %1 个 %2 %3").arg(trashRes.value(QStringLiteral("moved")).toInt())
                .arg(trashRes.value(QStringLiteral("failed")).toStringList().join(QStringLiteral("；")), tr.value(QStringLiteral("error")).toString()));
    QDir().rmdir(QFileInfo(strayAbs).absolutePath());

    // ---------- 重新读取 ----------
    r.section(QStringLiteral("重新读取"));
    const int expect = photos->count();
    app->reloadData();
    pause(300);
    r.check(photos->count() == expect && !store->isReadOnly(), QStringLiteral("重新读取后照片还在"), QStringLiteral("%1 张").arg(photos->count()));
}

} // namespace

namespace selftest {
static PlanController *g_plans = nullptr;
PlanController *plansCtl() { return g_plans; }
} // namespace selftest

void SelfTest::setPlans(PlanController *p) { selftest::g_plans = p; }

namespace selftest {
static FrameStats *g_frames = nullptr;
static Settings *g_settings = nullptr;
FrameStats *frameStats() { return g_frames; }
Settings *settingsObj() { return g_settings; }
} // namespace selftest

void SelfTest::setPerf(FrameStats *f, Settings *s) { selftest::g_frames = f; selftest::g_settings = s; }

namespace selftest {
static PlanEditor *g_editor = nullptr;
static Locator *g_locator = nullptr;
static services::Geocoder *g_geo = nullptr;
PlanEditor *editorCtl() { return g_editor; }
Locator *locatorCtl() { return g_locator; }
services::Geocoder *geocoderCtl() { return g_geo; }
} // namespace selftest

namespace selftest {
static PlanPageController *g_planPage = nullptr;
static NavHistory *g_nav = nullptr;
static RatesController *g_rates = nullptr;
RatesController *ratesCtl() { return g_rates; }
static WeatherController *g_weather = nullptr;
WeatherController *weatherCtl() { return g_weather; }
static NetController *g_netc = nullptr;
NetController *netCtl() { return g_netc; }
static AiController *g_ai = nullptr;
AiController *aiCtl() { return g_ai; }
static ExportController *g_exporter = nullptr;
ExportController *exporterCtl() { return g_exporter; }
static QString g_reportDir;
QString reportDir() { return g_reportDir; }
PlanPageController *planPageCtl() { return g_planPage; }
NavHistory *navCtl() { return g_nav; }
} // namespace selftest
void SelfTest::setPlanPage(PlanPageController *p, NavHistory *n, RatesController *rates) { selftest::g_planPage = p; selftest::g_nav = n; selftest::g_rates = rates; }
void SelfTest::setWeather(WeatherController *w) { selftest::g_weather = w; }
void SelfTest::setNetCtl(NetController *n) { selftest::g_netc = n; }
void SelfTest::setAi(AiController *a) { selftest::g_ai = a; }
void SelfTest::setExporter(ExportController *e) { selftest::g_exporter = e; }

void SelfTest::setEditor(PlanEditor *e, Locator *l, services::Geocoder *g) { selftest::g_editor = e; selftest::g_locator = l; selftest::g_geo = g; }

void SelfTest::start(const QString &reportPath, AppController *app, PhotoController *photos, map::MapController *mapc, QQuickWindow *win,
                     const QString &backendInfo, services::RoadRouter *roads, services::NetClient *net,
                     services::TileServer *tiles, services::TileCache *tileCache)
{
    selftest::g_reportDir = QFileInfo(reportPath).absolutePath();
    QTimer::singleShot(1500, app, [=] {
        Report r;
        QElapsedTimer all;
        all.start();
        runAll(r, app, photos, mapc, win, backendInfo, roads, net, tiles, tileCache);
        r.lines << QString() << QStringLiteral("# 结果：通过 %1 项，失败 %2 项（用时 %3 秒）").arg(r.passes).arg(r.fails).arg(all.elapsed() / 1000.0, 0, 'f', 1);
        QDir().mkpath(QFileInfo(reportPath).absolutePath());
        QFile f(reportPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(r.lines.join(u'\n').toUtf8() + '\n');
        qInfo("自检完成：通过 %d 项，失败 %d 项 -> %s", r.passes, r.fails, qUtf8Printable(QDir::toNativeSeparators(reportPath)));
        QCoreApplication::exit(r.fails);
    });
}

} // namespace tp::app