// 旅行计划 原生版：程序入口。
// 阶段 ⓪ 原型（平面地图 + 地球 + 液态玻璃）+ 0.3.0 数据层 + 0.4.0 设置 / 数据文件夹 / 单实例 / 窗口 / 托盘 / 翻译 / 第一批界面。
//
// 命令行：
//   --backend=auto|vulkan|opengl     图形后端（默认 auto：Vulkan 优先，不行退回 OpenGL）
//   --data <路径>                    临时用这个数据文件夹（或其中的 travel.json；不记进设置；截图、测试用）
//   --lang zh|en                     临时用这个界面语言（截图检查英文用）
//   --screenshot <页面> <输出.png>    页面：street / sat / unlock / globe / globe-night / globe-near（地图），ui-*（界面，见 Main.qml）；截完自动退出
//   --memtest <分钟> [--memlog <csv>] [--label <名字>]   自动循环操作，到时间退出
//   --memsteps <csv>                 内存测试：每一步开始前记一行内存和显存（0.8.6 分步测试）
//   --memtest-skip globe,unlock,sat  内存测试：跳过进地球 / 开已解锁 / 切卫星图这几步（0.8.6）
//   --plot <输出.png> <csv>...        画内存曲线后退出（不开窗口）
//   --test-error q|m|hang             开发版：启动 3 秒后故意制造 QML 错误 / C++ 异常 / 卡住 12 秒（实测出错提示）
//   --wait-pid <进程号>               内部用："结束并重新打开"时等旧的那一份退出
//   --hide-before-exit                测试用（配合 --screenshot）：退出前先把窗口藏起来（等于从托盘退出，0.8.0）
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QScreen>
#include <algorithm>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <rhi/qrhi.h>
#include <QEventLoop>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

#include "appcontroller.h"
#include "markerimages.h"
#include "crashhandler.h"
#include "crashinfo.h"
#include "errorreporter.h"
#include "datastore.h"
#include "devtools.h"
#include "graphicsbackend.h"
#include "glasssurface.h"
#include "logger.h"
#include "nativedialog.h"
#include "paths.h"
#include "photocontroller.h"
#include "plancontroller.h"
#include "exportcontroller.h"
#include "tripcontroller.h"
#include "calendarcontroller.h"
#include "journalcontroller.h"
#include "planpagecontroller.h"
#include "netcontroller.h"
#include "ratescontroller.h"
#include "aicontroller.h"
#include "weathercontroller.h"
#include "planeditor.h"
#include "locator.h"
#include "geocoder.h"
#include "commands.h"
#include "navhistory.h"
#include "thumbcache.h"
#include "thumbprovider.h"
#include "mapcontroller.h"
#include "netclient.h"
#include "roadrouter.h"
#include "tilecache.h"
#include "tileserver.h"
#include "selftest.h"
#include "framestats.h"
#include "perf.h"
#include "settings.h"
#include "singleinstance.h"
#include "tpapplication.h"
#include "translator.h"
#include "version.h"
#include "watchdog.h"
#include "windowstate.h"

namespace {
// 报告里"系统"那一行：Windows 版本、CPU 架构、Qt 版本、图形后端和显卡
QString systemLine(const QString &api, const QString &device)
{
    QString s = QSysInfo::prettyProductName() + QStringLiteral(" (") + QSysInfo::kernelVersion() + QStringLiteral(") ")
                + QSysInfo::currentCpuArchitecture() + QStringLiteral(", Qt ") + QString::fromLatin1(qVersion())
                + QStringLiteral(", ") + GraphicsBackend::displayName(api);
    if (!device.isEmpty()) s += QStringLiteral(" · ") + device;
    return s;
}

// "卡住"对话框的文字（翻译只在主线程做；换语言后重新准备）
tp::platform::Watchdog::Texts hangTexts(int seconds, const QString &version, tp::app::ErrorReporter *errors)
{
    tp::platform::Watchdog::Texts t;
    t.title = tp::tr("旅行计划");
    t.heading = tp::tr("旅行计划好像卡住了");
    t.content = tp::tr("错误码：%1\n\n请把错误码发给 Jason。").arg(QStringLiteral("TP-HANG")) + QStringLiteral("\n\n")
                + tp::tr("可以再等一会儿；一直不动的话，点“结束并重新打开”（没保存的改动会丢）。");
    t.buttons = { tp::tr("复制错误信息，继续等"), tp::tr("继续等"), tp::tr("结束并重新打开") };
    // %1 留给卡住那一刻的时间
    t.report = QStringList {
        QStringLiteral("旅行计划 错误报告 / Travel Planner error report"),
        QStringLiteral("错误码 Code: TP-HANG"),
        QStringLiteral("窗口没有响应 Window unresponsive（主线程 %1 秒没有响应 main thread not responding）").arg(seconds),
        QStringLiteral("版本 Version: v") + version,
        QStringLiteral("系统 System: ") + errors->system(),
        QStringLiteral("时间 Time: %1"),
        QStringLiteral("页面 Page: ") + errors->page(),
    }.join(QLatin1Char('\n'));
    return t;
}
// 退出保险（0.8.0）：arm() 以后 N 秒还没走完析构（卡在某个析构里）就直接结束进程。
// 放在 main 最前面声明，所以最后析构，能盖住引擎、窗口、各控制器的析构
class ExitGuard
{
public:
    ~ExitGuard()
    {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_done = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }
    void arm(int seconds, int code)
    {
        m_thread = std::thread([this, seconds, code] {
            std::unique_lock<std::mutex> lk(m_mutex);
            if (!m_cv.wait_for(lk, std::chrono::seconds(seconds), [this] { return m_done; })) {
                qWarning("退出时析构超过 %d 秒，直接结束进程", seconds);
                fflush(stdout);
                fflush(stderr);
                std::_Exit(code);
            }
        });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done = false;
    std::thread m_thread;
};
} // namespace

// 截图就绪判断用：还有看得见的图片在加载（Image.Loading = 2）
static bool imagesLoading(const QQuickItem *it)
{
    if (!it->isVisible()) return false;
    if (it->inherits("QQuickImageBase") && it->property("status").toInt() == 2) return true;
    const auto kids = it->childItems();
    return std::any_of(kids.begin(), kids.end(), [](const QQuickItem *c) { return imagesLoading(c); });
}

// 打包的思源宋体（assets/fonts，完整字库，常规和粗体）。文件不在时返回空，用系统字体。
// 只在真要用时加载一次，选了别的字体不占启动时间
static QString bundledSerif()
{
    static const QString family = [] {
        const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/assets/fonts/");
        if (!QFileInfo::exists(dir + QStringLiteral("NotoSerifCJKsc-Regular.otf"))) return QString();   // 0.12.11：不在时不让 Qt 报"Cannot open"
        const int id = QFontDatabase::addApplicationFont(dir + QStringLiteral("NotoSerifCJKsc-Regular.otf"));
        if (id < 0) return QString();
        QFontDatabase::addApplicationFont(dir + QStringLiteral("NotoSerifCJKsc-Bold.otf"));
        return QFontDatabase::applicationFontFamilies(id).value(0);
    }();
    return family;
}
// 设置 font 的取值（Electron prefs.js FONTS）→ 实际字体名；这台电脑上没有时退到宋体
static QString uiFamily(const QString &key)
{
    QStringList want;
    if (key == QLatin1String("yahei")) want = { QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei") };
    else if (key == QLatin1String("dengxian")) want = { QStringLiteral("DengXian") };
    else if (key == QLatin1String("kaiti")) want = { QStringLiteral("KaiTi"), QStringLiteral("STKaiti") };
    else if (const QString b = bundledSerif(); !b.isEmpty()) return b;
    want << QStringLiteral("Noto Serif SC") << QStringLiteral("Noto Serif CJK SC") << QStringLiteral("Source Han Serif SC") << QStringLiteral("Songti SC");
    for (const QString &f : want)
        if (QFontDatabase::hasFamily(f)) return f;
    return QStringLiteral("SimSun");
}

int main(int argc, char *argv[])
{
    QGuiApplication::setApplicationName(QStringLiteral("TravelPlannerNative"));
    QGuiApplication::setOrganizationName(QString());
    QGuiApplication::setApplicationVersion(QStringLiteral(TP_VERSION_STRING));
    // 0.14.2：程序本身不用 Qt Quick Controls，只有选文件框没有系统原生的时候才会用到；打包只带 Basic 样式，这里固定用它
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE")) qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    tp::app::TpApplication app(argc, argv);   // 接住事件处理里没人接的 C++ 异常（0.5.1）
    ExitGuard exitGuard;                       // 退出保险（0.8.0），见上面
    // 收到托盘时窗口是隐藏的：不能因为"最后一个窗口关了"就退出；退出只走 AppController::finishQuit
    QGuiApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser cli;
    cli.addOption({ "backend", "auto / vulkan / opengl", "api", "auto" });
    cli.addOption({ "fallback-reason", "内部用：Vulkan 失败后自动重启时带上原因", "text" });
    cli.addOption({ "data", "数据文件夹或其中 travel.json 的路径（临时用，不记进设置）", "path" });
    cli.addOption({ "lang", "zh / en（临时用）", "lang" });
    cli.addOption({ "screenshot", "截图页面", "page" });
    cli.addOption({ "memtest", "内存测试分钟数", "min" });
    cli.addOption({ "memlog", "内存测试 CSV", "csv" });
    cli.addOption({ "label", "CSV 里的名字", "text" });
    cli.addOption({ "memsteps", "内存测试：每一步记一行的 CSV", "csv" });
    cli.addOption({ "memtest-skip", "内存测试：跳过的步骤 globe,unlock,sat", "list" });
    cli.addOption({ "plot", "画内存曲线", "png" });
    cli.addOption({ "test-error", "开发版：故意制造错误 q / m / hang", "kind" });
    cli.addOption({ "selftest", "开发版：自检，结果写到这个文件（要配合 --data 给一份数据副本）", "report" });
    cli.addOption({ "wait-pid", "内部用：等这个进程退出", "pid" });
    cli.addOption({ "hide-before-exit", "测试用：截图后先把窗口藏起来再退出（等于从托盘退出）" });
    cli.addPositionalArgument("args", "其余参数");
    cli.process(app);

    if (cli.isSet("plot"))
        return DevTools::plot(cli.value("plot"), cli.positionalArguments());

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString shotPage = cli.value("screenshot");
    const int memMinutes = cli.value("memtest").toInt();
    const QString selftestReport = cli.value("selftest");
    const bool testRun = !shotPage.isEmpty() || memMinutes > 0 || !selftestReport.isEmpty();
    const bool hideBeforeExit = testRun && cli.isSet("hide-before-exit");

    tp::app::Logger::install(Paths::logDir());
    qInfo("==== 旅行计划 原生版 %s 启动（%s）", TP_VERSION_STRING, qUtf8Printable(QDir::toNativeSeparators(appDir)));
    tp::app::perf::setLogAll(testRun);
    tp::app::perf::startup("日志就绪");   // 0.9.1：启动的每一步写"[启动] … @ 毫秒"（从进程开始算）

    // 0. 出错提示、崩溃处理（0.5.1）：越早装越好
    tp::app::ErrorReporter errors;
    errors.setVersion(QStringLiteral(TP_VERSION_STRING));
    errors.setSystem(systemLine(QString(), QString()));
    tp::platform::CrashHandler::install(Paths::logDir(), QStringLiteral(TP_VERSION_STRING), errors.system());
    tp::app::Logger::setFatalHook(&tp::platform::CrashHandler::onFatal);
    qInfo("崩溃处理：%s", qUtf8Printable(tp::platform::CrashHandler::status()));
    tp::platform::NativeDialog::prepare(Paths::cacheDir());
    // 上次意外退出了：读出记号（下面窗口建好后提示），只留最近 3 个转储
    tp::app::CrashInfo::Last lastCrash;
    if (!testRun) lastCrash = tp::app::CrashInfo::takeLast(Paths::logDir());
    tp::app::CrashInfo::pruneDumps(Paths::logDir());
    if (lastCrash.found) qWarning("上次意外退出了：%s", qUtf8Printable(lastCrash.code));

    // "结束并重新打开"：等旧的那一份退出后再做单实例检查
    if (cli.isSet("wait-pid")) tp::platform::waitForProcessExit(cli.value("wait-pid").toLongLong(), 10000);

    // 1. 单实例（截图、内存测试不限制）
    tp::platform::SingleInstance single;
    if (!testRun && !single.start(tp::platform::SingleInstance::keyFor(appDir), cli.isSet("fallback-reason"))) {
        qInfo("已经有一个在运行，已通知它显示窗口");
        return 0;
    }
    QGuiApplication::setWindowIcon(QIcon(appDir + QStringLiteral("/assets/app/icon-256.png")));

    // 2. 设置
    tp::app::Settings settings;
    if (!settings.load(Paths::settingsFile()))
        qWarning("设置文件坏了（%s），已改名为 %s，这次用默认设置", qUtf8Printable(settings.lastError()), qUtf8Printable(settings.brokenBackup()));
    if (cli.isSet("lang")) settings.setLangOverride(cli.value("lang"));
    qInfo("设置：%s", qUtf8Printable(QDir::toNativeSeparators(settings.path())));
    tp::app::perf::startup("读设置");

    // 3. 翻译词典（打包在资源里）
    tp::app::DictTranslator translator;
    qInfo("英文词典：%d 条", translator.loadDir(QStringLiteral(":/i18n/tr")));

    // 4. 图形后端（必须在创建窗口之前定）
    BackendChoice backend = GraphicsBackend::choose(cli.value("backend"), appDir, cli.value("fallback-reason"));
    GraphicsBackend::apply(backend);
    errors.setSystem(systemLine(backend.api, backend.deviceInfo));
    tp::platform::CrashHandler::updateSystem(errors.system());
    tp::app::perf::startup("选图形后端");

    // 5. 数据：设置里记住的文件夹（第一次用时按 PO 2026-11-02 的规则选）；--data 临时指定
    QString tempData = cli.value("data");
#ifdef TP_SOURCE_DIR
    if (tempData.isEmpty() && testRun) tempData = QStringLiteral(TP_SOURCE_DIR "/testdata/travel.json");
#endif
    if (!tempData.isEmpty() && !QFileInfo(tempData).isDir()) tempData = QFileInfo(tempData).absolutePath();
    // 字体（0.12.3）：跟设置走，见 uiFamily
    const QString serif = uiFamily(settings.value(QStringLiteral("font")).toString());
    // 地图标记的图标字体（0.8.2）：Material Symbols Rounded 子集（Apache 2.0，assets/fonts），打包在程序里
    {
        const int fid = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/MaterialSymbolsRounded.ttf"));
        const QStringList fam = fid >= 0 ? QFontDatabase::applicationFontFamilies(fid) : QStringList();
        if (fam.isEmpty()) qWarning("图标字体没有加载成功，地图标记的图标退回数字或圆点");
        tp::map::MarkerImages::setFonts(serif, fam.value(0));
    }
    tp::app::perf::startup("字体");
    // 统一请求通道和批量算道路路线（0.8.4）。离线总开关、每项服务的开关由 NetController 读（0.12.5）
    tp::services::NetClient net;
    net.setUserAgent(tp::services::NetClient::userAgentFor(QStringLiteral(TP_VERSION_STRING)));
    // 本机瓦片服务（0.8.5）：maplibre 向它要底图瓦片，它查硬盘缓存（<缓存>\tiles，上限 2 GB）、没有再经上面的通道向 Esri 取
    tp::services::TileCache tileCache(Paths::cacheDir() + QStringLiteral("/tiles"));
    tileCache.startScan();
    tp::app::NetController netc(&settings, &net, &tileCache);
    tp::services::TileServer tileServer(&net, &tileCache);
    tileServer.start();   // 起不来时（极少见）骨架样式直接用 Esri 的地址，只是没有硬盘缓存和失败提示
#ifdef TP_DEV_TOOLS
    // 云端测试（开发版）：环境变量 TP_TILE_UPSTREAM=http://127.0.0.1:<端口> 把 Esri 换成本机的假瓦片服务（云端连不上 Esri）
    if (const QString up = qEnvironmentVariable("TP_TILE_UPSTREAM"); !up.isEmpty()) {
        net.setAllowPlainHttpForTest(true);
        net.addService({ QStringLiteral("tiles"), QUrl(up).host(), 0 });
        tileServer.setUpstreamRewrite([up](const QString &u) { QString t = u; return t.replace(QStringLiteral("https://server.arcgisonline.com"), up); });
        qInfo("瓦片上游换成了 %s（TP_TILE_UPSTREAM，测试用）", qUtf8Printable(up));
    }
#endif
    tp::app::perf::startup("联网和瓦片服务");
    tp::map::MapController mapc;   // 平面地图（0.8.0）：相机、底图、交给 maplibre 的数据
    mapc.setTranslate([](const char *s) { return tp::tr(s); });
    mapc.setTileServer(&tileServer);
    if (QGuiApplication::primaryScreen()) mapc.setDpr(QGuiApplication::primaryScreen()->devicePixelRatio());   // 卫星瓦片大小（0.8.6）；窗口在别的屏幕上时 QML 再改
    if (memMinutes > 0) mapc.setStartBase(QStringLiteral("street"));   // 内存测试从街道图开始，不受上次用的底图影响（0.8.6）
    mapc.setPersistBase(!testRun);   // 0.9.2：放在 setup 前面（setup 里读"上次是不是地球"，截图 / 自检不读）
    mapc.setup(appDir + QStringLiteral("/assets"), Paths::cacheDir(), &settings);
    tp::app::perf::startup("地图控制器");
    // 离线 / 关了在线底图：只用"已解锁"，运行中切换马上生效（Electron tp:prefs）
    mapc.setTilesOff(!net.allows(QStringLiteral("tiles")));
    QObject::connect(&net, &tp::services::NetClient::switchesChanged, &mapc, [&net, &mapc] { mapc.setTilesOff(!net.allows(QStringLiteral("tiles"))); });
    tp::data::DataStore store;
    store.setAppVersion(QStringLiteral(TP_VERSION_STRING));
    mapc.setDeferRebuild(true);   // 0.9.2：到加载界面之前只重建一次地图数据（见下面 setDeferRebuild(false)）
    mapc.setStore(&store);
    tp::app::AppController ctl(&settings, &store);
    ctl.setTranslator(&translator);
    ctl.setAutoBackupAllowed(!testRun);
    QObject::connect(&ctl, &tp::app::AppController::langChanged, &mapc, &tp::map::MapController::retranslate);
    mapc.setOnEdited([&ctl](int delayMs) { ctl.noteChanged(false, delayMs); });   // 手动标记（0.8.5）：停手 0.7 秒自动保存
    tp::services::RoadRouter roads;
    roads.setTranslate([](const char *s) { return tp::tr(s); });
    roads.setNet(&net);
    // "算不出来"的段：和 settings.json 放在一起（截图 / 自检 / 内存测试用缓存里的另一份，不碰正式的）
    roads.setFailFile(testRun ? Paths::cacheDir() + QStringLiteral("/test-road-fail.json")
                              : QFileInfo(settings.path()).absolutePath() + QStringLiteral("/road-fail.json"));
    roads.setSave([&ctl](bool quiet) { return ctl.saveFor(quiet); });
    roads.setStore(&store);
    roads.setAutoEnabled(!testRun);   // 1.1.0 自检只在 v11 分组里打开
    QObject::connect(&ctl, &tp::app::AppController::langChanged, &roads, &tp::services::RoadRouter::changed);
    ctl.startup(tempData);
    tp::app::perf::startup("读数据");
    // 地图页的计划（0.8.7）：筛选、"此刻"、简易计划面板。在读取数据之后建，筛选按这份数据过滤已删除的计划
    tp::app::PlanController plans(&ctl, &settings, &store, &mapc);
    plans.setPersist(!testRun);
    // 0.8.10：截图 / 自检 / 内存测试固定从"计划"模式开始，四种状态都按正常样子画（地图上和 0.8.9 一样画全部计划；0.9.7 起用 setTestAllStatuses，
    // 以前的截图页和自检不受上次用的模式影响）；不写进 settings.json
    if (testRun) { plans.setMode(QStringLiteral("plan")); plans.setTestAllStatuses(true); }
    // 0.8.9：地名搜索（Nominatim，结果缓存在缓存目录；服务地址可以在 settings.json 的 geocodeUrl 换）、站点编辑器、我的位置
    tp::services::Geocoder geocoder;
    geocoder.setNet(&net);
    geocoder.setTranslate([](const char *s) { return tp::tr(s); });
    geocoder.setBase(settings.value(QStringLiteral("geocodeUrl")).toString());
    geocoder.setCacheFile(Paths::cacheDir() + (testRun ? QStringLiteral("/test-geocode.json") : QStringLiteral("/geocode.json")));
    tp::app::RatesController rates(&settings, &net);                    // 0.10.1 汇率和主货币
    tp::app::AiController ai(&settings, &net, &rates);                  // 0.13.0 AI 助手（钥匙、聊天、费用）
    if (testRun) {   // 截图 / 自检不碰真的钥匙和对话
        tp::app::AiController::TestHooks h;
        h.credPrefix = QStringLiteral("TravelPlannerNative-test/ai/");
        h.chatsFile = Paths::cacheDir() + QStringLiteral("/test-ai-chats.json");
        ai.setTestHooks(h);
    }
    rates.setUsedProvider([&store] { return tp::map::plans::usedCurrencies(store.data()); });   // 0.12.6 缺哪种再补
    tp::app::PlanPageController planPage(&ctl, &plans, &store, &net, &rates, &settings);   // 0.10.0 完整计划页
    // 0.14.0 导出：行程单 / 信息卡一律用打包的思源宋体（嵌进 PDF），封面用打包的省界
    tp::app::ExportController exporter(&store, { uiFamily(QString()), appDir + QStringLiteral("/assets/admin"), Paths::cacheDir() + QStringLiteral("/export"),
                                                 [&plans] { return plans.today(); }, [&rates] { return rates.main(); } });
    tp::app::TripController trips(&store, &ctl, [&rates] { return rates.main(); });   // 0.14.1 .trip、分享版、批量导出
    tp::app::WeatherController weather(&planPage, &plans, &store, &net);   // 0.10.3 预报和往年气候
    tp::app::CalendarController calendar(&ctl, &plans, &settings, &store, &mapc);   // 0.11.2 日历
    calendar.setPersist(!testRun);
    tp::app::JournalController journal(&ctl, &store);                        // 0.11.2 游记
    weather.setCacheFile(Paths::cacheDir() + (testRun ? QStringLiteral("/test-weather.json") : QStringLiteral("/weather.json")));
    tp::app::PlanEditor editor(&ctl, &store, &mapc);
    editor.setNet(&net);
    editor.setRoads(&roads);
    editor.setGeocoder(&geocoder);
    roads.setBlocker([&editor] { return editor.isOpen() ? tp::tr("请先完成或放弃编辑器里的改动，再批量计算") : QString(); });
    QObject::connect(&roads, &tp::services::RoadRouter::message, &ctl, &tp::app::AppController::toast);
    QObject::connect(&ai, &tp::app::AiController::routeNeeded, &roads, &tp::services::RoadRouter::autoFor);   // 1.1.0 自动算路
    QObject::connect(&netc, &tp::app::NetController::changed, &roads, &tp::services::RoadRouter::tryQueue);
    QObject::connect(&editor, &tp::app::PlanEditor::openChanged, &roads, &tp::services::RoadRouter::tryQueue);
    tp::app::Locator locator(&settings, &store, &mapc);
    locator.setToday([&plans] { return plans.today(); });
    // 0.8.8：页面的前进 / 后退（S-13）、Ctrl K 的排序（S-12）
    tp::app::NavHistory nav;
    nav.resetTo(plans.mode());   // 0.8.10：从上次的模式开始（地图的四种模式也是"页面"，前进 / 后退按真的模式走）
    tp::app::CommandRanker commands;
    // 0.13.1 AI 改计划：当前页 / 当前计划（详情页打开的，否则地图上选中的）/ 今天；应用时删掉了选中的计划就取消选中
    ai.setPlanContext(&ctl, &store, &geocoder,
        [&nav, &planPage, &mapc, &plans, &editor] {
            const QString page = nav.page();
            // 1.1.0 编辑器开着（AI 面板并排）时说的就是编辑器里那个计划
            const QString pid = editor.isOpen() ? editor.planId() : page == QLatin1String("trip") ? planPage.planId() : mapc.markers()->selectedPlan();
            return QVariantMap{ { QStringLiteral("page"), page }, { QStringLiteral("today"), plans.today() }, { QStringLiteral("planId"), pid } };
        },
        [&mapc](const QStringList &ids) { if (ids.contains(mapc.markers()->selectedPlan())) mapc.markers()->clearSelection(); });
    // 1.1.0 编辑器开着时应用：先完成编辑（存成一步），应用完再打开
    ai.setEditorHooks([&editor]() -> QString {
            QVariantMap r = editor.finish();
            if (r.value(QStringLiteral("ask")).toString() == QLatin1String("single")) { editor.keepSingle(); r = editor.finish(); }
            return r.value(QStringLiteral("ok")).toBool() ? r.value(QStringLiteral("planId")).toString() : QString();
        },
        [&editor](const QString &id) { editor.openPlan(id); });
    tp::app::perf::startup("控制器都建好");

    if (lastCrash.found)
        ctl.setLastCrash({ { "code", lastCrash.code }, { "dump", lastCrash.dump }, { "report", lastCrash.report },
                           { "gl", backend.vulkanCrashFallback } });

    // 引擎放在堆上：退出时先删它（连同窗口和所有 QML 对象），再析构下面这些控制器；
    // 否则控制器先没了，QML 里还活着的绑定会去读空对象（0.8.0 云端实测：一串 TypeError）
    auto enginePtr = std::make_unique<QQmlApplicationEngine>();
    QQmlApplicationEngine &engine = *enginePtr;
    // QML 警告：真正的 JS 异常弹出错小框（TP-Q），其余写进日志（ErrorReporter::reportQml）
    engine.setOutputWarningsToStandardError(false);
    QObject::connect(&engine, &QQmlEngine::warnings, &errors, [&errors](const QList<QQmlError> &list) {
        for (const QQmlError &e : list) errors.reportQml(e.description(), e.url().toString(), e.line(), e.column());
    });
    engine.addImportPath(backend.mlnDir + QStringLiteral("/qml"));
    ctl.setEngine(&engine);
    ctl.applyLanguage();
    // 照片（0.7.0）：缩略图缓存在 <缓存目录>\thumbs，QML 用 image://thumb/…
    tp::data::ThumbCache thumbs(Paths::cacheDir() + QStringLiteral("/thumbs"));
    auto *thumbProvider = new tp::app::ThumbProvider(&thumbs);   // 引擎接管，引擎析构时删除
    engine.addImageProvider(QStringLiteral("thumb"), thumbProvider);
    engine.addImageProvider(QStringLiteral("card"), new CardImages);   // 0.12.2 内容卡片的连续圆角
    engine.addImageProvider(QStringLiteral("marker"), new tp::map::MarkerImages);   // 地图标记（0.8.2），引擎接管
    tp::app::PhotoController photos(&ctl, &thumbs, thumbProvider);
    photos.setSettings(&settings);
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty("tpVersion", QStringLiteral(TP_VERSION_STRING));
    ctx->setContextProperty("serifFamily", serif);
    // 换字体马上生效：改上下文属性，所有绑了 serifFamily 的文字重新取值。地图标记图片上的自定义文字下次生成时才换（图片按内容缓存）
    QObject::connect(&ctl, &tp::app::AppController::uiFontChanged, &engine, [&ctl, ctx] {
        const QString f = uiFamily(ctl.uiFont());
        ctx->setContextProperty("serifFamily", f);
        tp::map::MarkerImages::setFonts(f, tp::map::MarkerImages::iconFamily());
    });
    ctx->setContextProperty("iconFamily", tp::map::MarkerImages::iconFamily());   // 0.8.9 样式选择器的图标格子
    ctx->setContextProperty("mapc", &mapc);
    ctx->setContextProperty("app", &ctl);
    ctx->setContextProperty("photos", &photos);
    ctx->setContextProperty("plans", &plans);
    ctx->setContextProperty("nav", &nav);
    ctx->setContextProperty("commands", &commands);
    ctx->setContextProperty("roads", &roads);
    ctx->setContextProperty("editor", &editor);
    ctx->setContextProperty("planPage", &planPage);
    ctx->setContextProperty("exporter", &exporter);
    ctx->setContextProperty("trips", &trips);
    ctx->setContextProperty("calendar", &calendar);
    ctx->setContextProperty("journal", &journal);
    ctx->setContextProperty("rates", &rates);
    ctx->setContextProperty("netc", &netc);
    ctx->setContextProperty("ai", &ai);
    ctx->setContextProperty("weather", &weather);
    ctx->setContextProperty("locator", &locator);
    // 截图 ui-photo-import / ui-photo-e2e：把 tests/photos.txt 里的测试照片当成"选中的文件"
    QVariantList photoShotFiles;
#ifdef TP_SOURCE_DIR
    if (shotPage == QLatin1String("ui-photo-import") || shotPage == QLatin1String("ui-photo-e2e") || shotPage.startsWith(QLatin1String("heic-"))) {
        QFile pack(QStringLiteral(TP_SOURCE_DIR "/tests/photos.txt"));   // 每行"文件名<Tab>base64"
        const QString dir = Paths::cacheDir() + QStringLiteral("/shot-photos");
        QDir().mkpath(dir);
        if (pack.open(QIODevice::ReadOnly))
            for (const QByteArray &l : pack.readAll().split('\n')) {
                const int tab = l.indexOf('\t');
                if (l.startsWith('#') || tab <= 0) continue;
                QFile f(dir + u'/' + QString::fromUtf8(l.left(tab)));
                if (f.open(QIODevice::WriteOnly)) { f.write(QByteArray::fromBase64(l.mid(tab + 1).trimmed())); photoShotFiles << f.fileName(); }
            }
    }
#endif
    ctx->setContextProperty("photoShotFiles", photoShotFiles);
    ctx->setContextProperty("errors", &errors);
    ctx->setContextProperty("backendApi", GraphicsBackend::displayName(backend.api));
    ctx->setContextProperty("backendReason", backend.reason);
    ctx->setContextProperty("backendDevice", backend.deviceInfo);
    ctx->setContextProperty("shotPage", shotPage);
    ctx->setContextProperty("memtest", memMinutes > 0);
    // 9. 内存测试（0.8.6 起在加载界面之前建好，QML 的测试循环要调 memlog.mark）
    MemLogger *memLogger = nullptr;
    if (memMinutes > 0) {
        memLogger = new MemLogger(cli.value("memlog"), cli.isSet("label") ? cli.value("label") : GraphicsBackend::displayName(backend.api), memMinutes, &app);
        memLogger->setStepsPath(cli.value("memsteps"));
    }
    ctx->setContextProperty("memlog", memLogger);
    ctx->setContextProperty("memSkip", cli.value("memtest-skip").split(QLatin1Char(','), Qt::SkipEmptyParts));
    tp::app::PerfBridge perfBridge;
    ctx->setContextProperty("perf", &perfBridge);   // 0.9.1：QML 里的计时（换模式、换底图）
    ctx->setContextProperty("testRun", testRun);   // 截图 / 自检 / 内存测试：地图上不跟真的鼠标（0.8.4，免得鼠标正好在窗口上时打乱结果）

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    if (backend.api == QLatin1String("vulkan") && backend.requested == QLatin1String("auto"))
        GraphicsBackend::markVulkanStarting();
    mapc.setDeferRebuild(false);   // 数据、筛选、模式都准备好了：重建一次，界面一建好就有路线和标记
    tp::app::perf::startup("开始加载界面");
    engine.loadFromModule("TPApp", "Main");
    if (engine.rootObjects().isEmpty()) return 2;
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    tp::app::perf::startup("界面加载完");
    // 0.9.1：每一帧的时间（自检"性能"一节、内存测试用；平时设环境变量 TP_FRAME_STATS=1 才挂）。显卡时间戳要在窗口显示前打开
    tp::app::FrameStats *frameStats = nullptr;
    tp::app::FrameStats::enableGpuTimestamps(win);   // 0.12.1：玻璃的自动降级要看显卡耗时
    if (testRun || qEnvironmentVariableIntValue("TP_FRAME_STATS") > 0) {
        frameStats = new tp::app::FrameStats(win, &app);
    }
    // 第一帧换上来的时刻（渲染线程上发，只记一次）
    {
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = QObject::connect(win, &QQuickWindow::frameSwapped, win, [conn] {
            tp::app::perf::startup("第一帧");
            QObject::disconnect(*conn);
        }, Qt::DirectConnection);
    }

    // 6. 窗口：按设置摆好再显示（截图、内存测试固定 1400×900）
    tp::app::WindowKeeper keeper(win, &settings);
    ctl.setWindow(win, testRun ? nullptr : &keeper);
    if (testRun) {
        // 0.8.10：摆在屏幕可用区域里面（放不下就缩小）。0.8.9 在 PO 电脑上自检点右边的按钮全都没反应，
        // 疑似窗口被系统摆到右边、右半截在屏幕外面（云端 Xvfb 里窗口总在左上角）
        QSize sz(1400, 900);
        QRect avail;
        if (QScreen *sc = win->screen() ? win->screen() : QGuiApplication::primaryScreen()) avail = sc->availableGeometry();
        const QString at = qEnvironmentVariable("TP_TEST_WINDOW_AT");   // 开发版：故意摆到某处（"x,y"），复现窗口一部分在屏幕外
        if (!at.isEmpty() && at.contains(QLatin1Char(','))) {
            win->setPosition(at.section(QLatin1Char(','), 0, 0).toInt(), at.section(QLatin1Char(','), 1, 1).toInt());
        } else if (avail.isValid()) {
            sz = sz.boundedTo(QSize(avail.width() - 16, avail.height() - 40));   // 留出标题栏和边框
            win->setPosition(avail.x() + (avail.width() - sz.width()) / 2, avail.y() + 32 + std::max(0, (avail.height() - 40 - sz.height()) / 2));
        }
        win->resize(sz);
        win->show();
    }
    else keeper.restoreAndShow();
    tp::app::perf::startup("窗口显示");
#ifdef TP_DEV_TOOLS
    // 0.12.11 开发版：TP_GRAB="毫秒:路径;毫秒:路径" 正常启动后按时截程序自己的窗口（实测首启、升级提示、离线这些真实启动路径用，不截屏幕）
    for (const QString &g : qEnvironmentVariable("TP_GRAB").split(u';', Qt::SkipEmptyParts)) {
        const QString path = g.section(u':', 1);
        QTimer::singleShot(g.section(u':', 0, 0).toInt(), win, [win, path] {
            qInfo("TP_GRAB %s %s", qUtf8Printable(path), win->grabWindow().save(path) ? "ok" : "FAILED");
        });
    }
#endif
    tp::app::perf::startStallWatch();   // 0.9.1：主线程顿了超过 0.25 秒就写一行日志
    QObject::connect(&single, &tp::platform::SingleInstance::activateRequested, &ctl, &tp::app::AppController::activateWindow);

    // 7. 实际后端和显卡名（RHI 初始化之后才知道）；Vulkan 场景图出错 → 用 OpenGL 重启一次
    QObject::connect(win, &QQuickWindow::sceneGraphInitialized, win, [win, backend] {
        if (QRhi *r = win->rhi()) {
            const QString dev = QString::fromUtf8(r->driverInfo().deviceName);
            QMetaObject::invokeMethod(win, "setRhiInfo", Qt::QueuedConnection,
                                      Q_ARG(QVariant, QString::fromLatin1(r->backendName())), Q_ARG(QVariant, dev));
            qInfo("RHI: %s / %s", r->backendName(), qUtf8Printable(dev));
            if (backend.deviceInfo.isEmpty())
                QMetaObject::invokeMethod(qApp, [dev, api = backend.api] {
                    if (auto *e = tp::app::ErrorReporter::instance()) {
                        e->setSystem(systemLine(api, dev));
                        tp::platform::CrashHandler::updateSystem(e->system());
                    }
                }, Qt::QueuedConnection);
        }
    }, Qt::DirectConnection);
    QObject::connect(win, &QQuickWindow::sceneGraphError, &app, [backend, &errors](QQuickWindow::SceneGraphError err, const QString &msg) {
        if (backend.api != QLatin1String("vulkan") || backend.requested != QLatin1String("auto")) {
            // 显卡出错（0.5.1）：窗口已经画不出来，QML 的框显示不了 → 系统对话框（TP-GPU-…）
            QString key = QString::fromLatin1(QMetaEnum::fromType<QQuickWindow::SceneGraphError>().valueToKey(err));
            key = key.toUpper().remove(QRegularExpression(QStringLiteral("[^A-Z]"))).left(8);
            const QString code = QStringLiteral("TP-GPU-") + (key.isEmpty() ? QStringLiteral("ERROR") : key);
            const QString report = errors.logOnly(code, QStringLiteral("显卡出错 Scene graph error: ") + msg);
            const bool vk = backend.api == QLatin1String("vulkan");
            tp::platform::NativeDialog::Spec spec;
            spec.title = tp::tr("旅行计划");
            spec.heading = tp::tr("旅行计划没法用显卡画出窗口");
            spec.content = tp::tr("错误码：%1\n\n请把错误码发给 Jason。").arg(code) + QStringLiteral("\n\n")
                           + tp::tr("显卡驱动报告：") + msg.left(300)
                           + (vk ? QStringLiteral("\n\n") + tp::tr("可以换成 OpenGL 重新打开试试。") : QString());
            spec.buttons << tp::tr("复制错误信息并退出");
            if (vk) spec.buttons << tp::tr("用 OpenGL 重新打开");
            spec.buttons << tp::tr("退出");
            spec.cancelIndex = int(spec.buttons.size()) - 1;
            const int r = tp::platform::NativeDialog::show(spec);
            if (r == 0) tp::platform::NativeDialog::copyText(report);
            if (vk && r == 1) {
                QStringList args = QCoreApplication::arguments().mid(1);
                args.removeIf([](const QString &a) { return a.startsWith(QLatin1String("--backend")) || a.startsWith(QLatin1String("--fallback-reason")); });
                args << QStringLiteral("--backend=opengl") << QStringLiteral("--fallback-reason=") + tp::tr("显卡出错：") + msg.left(200);
                QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
            }
            QCoreApplication::exit(4);   // 退出前照常保存（main 末尾 flushBeforeExit）
            return;
        }
        QStringList args = QCoreApplication::arguments().mid(1);
        args.removeIf([](const QString &a) { return a.startsWith(QLatin1String("--backend")) || a.startsWith(QLatin1String("--fallback-reason")); });
        args << QStringLiteral("--backend=opengl") << QStringLiteral("--fallback-reason=Vulkan 初始化失败：") + msg;
        qWarning("Vulkan 场景图出错，用 OpenGL 重启：%s", qUtf8Printable(msg));
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
        QCoreApplication::exit(4);
    });
    // 卡住检测（0.5.1）：窗口显示以后才开始；截图 / 内存测试只写日志不弹框
    tp::platform::Watchdog watchdog;
    watchdog.setLogFile(Paths::logDir() + QStringLiteral("/errors.log"));
    constexpr int kHangSeconds = 10;
    auto refreshHangTexts = [&watchdog, &errors] { watchdog.setTexts(hangTexts(kHangSeconds, QStringLiteral(TP_VERSION_STRING), &errors)); };
    refreshHangTexts();
    QObject::connect(&ctl, &tp::app::AppController::langChanged, &watchdog, refreshHangTexts);
    QObject::connect(&errors, &tp::app::ErrorReporter::pageChanged, &watchdog, refreshHangTexts);
    QObject::connect(&errors, &tp::app::ErrorReporter::systemChanged, &watchdog, refreshHangTexts);
    watchdog.start(kHangSeconds, !testRun);

    // 开发版：--test-error 启动 3 秒后故意制造错误（云端和 PO 实测出错提示用）
    if (cli.isSet("test-error") && ctl.devTools()) {
        const QString kind = cli.value("test-error");
        QTimer::singleShot(3000, &app, [kind, win, &ctl] {
            if (kind == QLatin1String("q")) QMetaObject::invokeMethod(win, "devThrow");
            else ctl.devTrigger(kind);
        });
    }

    // 稳定画出 5 秒后，清掉"Vulkan 启动中"记号；缩略图缓存超过上限就删旧的（后台）
    QTimer::singleShot(5000, win, [&photos] { GraphicsBackend::clearVulkanStarting(); photos.pruneCacheLater(); });
    // 0.9.2：窗口显示 3 秒后在后台建省界模型（第一次进"已解锁"不用等）
    // （截图不做：页面就绪可能正好在这之后，退出时要等它读完）
    if (shotPage.isEmpty()) QTimer::singleShot(3000, &mapc, [&mapc] { mapc.preloadUnlock(); });

    // 8. 截图模式
    // 0.14.2：ui-locate 启动后真的问一次系统定位（装好的版本里核对定位插件，日志写"系统定位来源"）
    if (shotPage == QLatin1String("ui-locate")) QTimer::singleShot(500, &locator, &tp::app::Locator::locate);
    if (!shotPage.isEmpty()) {
        const QString out = cli.positionalArguments().value(0, QStringLiteral("screenshot.png"));
        const auto shoot = [win, out] {
            const QImage img = win->grabWindow();
            const bool ok = img.save(out);
            qInfo("screenshot %dx%d -> %s %s", img.width(), img.height(), qUtf8Printable(out), ok ? "ok" : "FAILED");
            QCoreApplication::exit(ok ? 0 : 3);
        };
        // TP_SHOT_FIXED_WAIT=毫秒：固定等这么久再截（0.10.4 以前的做法，比对就绪判断有没有截早时用）
        const int fixedWait = qEnvironmentVariableIntValue("TP_SHOT_FIXED_WAIT");
        if (fixedWait > 0 || shotPage.startsWith(QLatin1String("ui-")) || shotPage.startsWith(QLatin1String("globe"))) {
            QTimer::singleShot(fixedWait > 0 ? fixedWait : shotPage == QLatin1String("ui-devtest") || shotPage == QLatin1String("ui-photo-e2e") ? 8000 : shotPage.startsWith(QLatin1String("ui-")) ? 5000 : 9000, &app, shoot);
        } else {
            // 0.10.5 页面就绪就截（原来固定等 12 秒）：步骤走完、地图样式和瓦片都到了、图片都加载完，再加连续 400 ms 没有新帧。
            // 循环动画（聚焦站点的呼吸光晕、交通小图标）在截图时关掉（MapMarkers.qml、Main.qml），否则帧永远不停，这个信号就没用了。
            // 上限 12 秒：超时照常截，写一行日志说卡在哪（verify.ps1 会列出来）
            auto *clock = new QElapsedTimer;
            clock->start();
            auto lastFrame = std::make_shared<std::atomic<qint64>>(-1);
            QObject::connect(win, &QQuickWindow::frameSwapped, win, [clock, lastFrame] { lastFrame->store(clock->elapsed()); }, Qt::DirectConnection);
            auto *poll = new QTimer(&app);
            poll->setInterval(50);
            QObject::connect(poll, &QTimer::timeout, &app, [=, &tileServer, &mapc, readyAt = qint64(-1)]() mutable {
                const qint64 t = clock->elapsed();
                QVariant stepsBusy;
                QMetaObject::invokeMethod(win, "shotBusy", Q_RETURN_ARG(QVariant, stepsBusy));
                const bool tilesExpected = win->property("mapLive").toBool() && (mapc.base() == QLatin1String("street") || mapc.base() == QLatin1String("sat")) && tileServer.online();
                const char *waiting = stepsBusy.toBool() ? "步骤或地图"
                                    : (tilesExpected && tileServer.stats().requests == 0) || tileServer.busyForMap() > 0 || tileServer.msSinceLastResponse() < 300 ? "瓦片"
                                    : imagesLoading(win->contentItem()) ? "图片" : nullptr;
                if (waiting) readyAt = -1;
                else if (readyAt < 0) readyAt = t;
                const qint64 frame = lastFrame->load();
                const bool still = !waiting && t - readyAt >= 400 && frame >= 0 && t - frame >= 400;
                if (!still && t < 12000) return;
                poll->stop();
                if (still) qInfo("截图就绪：%s，%lld ms", qUtf8Printable(shotPage), static_cast<long long>(t));
                else qWarning("截图等到上限：%s，12 秒还在等%s，照常截", qUtf8Printable(shotPage), waiting ? waiting : "画面停下来（一直有新帧）");
                shoot();
            });
            poll->start();
        }
    }
    // 8b. 开发版自检（0.7.2，验证.bat 调用）：会改数据，只许用 --data 给的副本
    if (!selftestReport.isEmpty()) {
        if (!ctl.devTools() || cli.value("data").isEmpty()) {
            qWarning("--selftest 只在开发版、并且用 --data 指定数据副本时可用");
            store.lock().release();
            std::_Exit(6);
        }
        tp::app::SelfTest::setPlans(&plans);
        tp::app::SelfTest::setPerf(frameStats, &settings);
        tp::app::SelfTest::setEditor(&editor, &locator, &geocoder);
        tp::app::SelfTest::setPlanPage(&planPage, &nav, &rates);
        tp::app::SelfTest::setNetCtl(&netc);
        tp::app::SelfTest::setAi(&ai);
        tp::app::SelfTest::setExporter(&exporter);
        tp::app::SelfTest::setTrips(&trips);
        tp::app::SelfTest::setWeather(&weather);
        tp::app::SelfTest::start(QDir::fromNativeSeparators(selftestReport), &ctl, &photos, &mapc, win,
                                 GraphicsBackend::displayName(backend.api) + (backend.deviceInfo.isEmpty() ? QString() : QStringLiteral(" · ") + backend.deviceInfo),
                                 &roads, &net, &tileServer, &tileCache);
    }
    const int rc = app.exec();
    watchdog.stop();
    // 正常退出（含截图、自检这种 5 秒内就退出的运行）说明 Vulkan 没出事：清掉记号，免得下次被误判退回 OpenGL（0.7.3）
    GraphicsBackend::clearVulkanStarting();
    photos.shutdown();      // 正在导入就取消（删掉已经复制的照片）、等清理和缓存线程停下
    ctl.shutdownBackup();   // 正在备份就取消（删掉没做完的备份文件夹）
    // 退出前保证数据已经落盘（万一析构出事也不丢）；冲突或失败时另存 travel.unsaved-*.json，不覆盖 travel.json
    store.stopWatching();
    const QString rescue = store.flushBeforeExit();
    if (!rescue.isEmpty()) qWarning("没能保存到 travel.json，改动另存为 %s", qUtf8Printable(rescue));
    store.lock().release();
    qInfo("==== 退出（%d）", rc);

    // 0.8.0：正式处理 maplibre 退出崩溃（以前直接 _Exit 跳过析构）。
    // maplibre-native-qt 399311e：地图对象（QMapLibre::Map）由 QML 项和它的场景图节点共同持有；最后释放它的那一方
    // 要在有图形上下文的渲染线程上，否则 ~gl::Context() 调 glUseProgram 会崩溃（7.0.0-b 云端 gdb）。
    // 做法：窗口还在的时候先删掉地图项（Loader.active = false），节点随后由渲染线程在下一次同步时删除
    // （窗口隐藏在托盘里时，在窗口析构、渲染线程释放场景图时删除），地图就在渲染线程上释放；然后照常析构。
    // 保险：从这里起 5 秒还没退完就直接结束进程（数据上面已经落盘）。
    exitGuard.arm(5, rc);
    // 测试用（verify.ps1）：先把窗口藏起来再退出，等于从托盘退出的情况
    if (win && hideBeforeExit) { win->hide(); QCoreApplication::processEvents(); }
    if (win) {
        QMetaObject::invokeMethod(win, "releaseMap");
        // Loader 用 deleteLater 删地图项：这里马上删掉（一定要在窗口析构之前；窗口析构时渲染线程先释放场景图，
        // 之后才删子对象——那时地图项才删的话，最后一份引用在 GUI 线程上释放，照样崩溃。0.8.0 云端 gdb 实测，
        // 窗口隐藏在托盘里时就是这样）
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (win->isExposed()) {   // 等渲染线程同步一次（删掉地图的节点）
            QEventLoop wait;
            QObject::connect(win, &QQuickWindow::frameSwapped, &wait, &QEventLoop::quit);
            QTimer::singleShot(1500, &wait, &QEventLoop::quit);
            win->update();
            wait.exec();
        }
    }
    qInfo("地图已释放，开始析构");
    enginePtr.reset();   // 窗口和所有 QML 对象（这时控制器都还在）
    win = nullptr;
    return rc;
}
