// 自检"性能（0.9.1）"：先量再改。只量、不判断快慢（每项都是"ℹ"信息行，最后一项检查只看这一节跑完了）。
// 量的东西：
// 1. 启动各步（从进程开始的毫秒数）；
// 2. 拖地图时每一帧：街道图上默认几块玻璃 / 透明效果关掉 / 标记藏起来 / 打开我的旅程（多几块玻璃）/ 已解锁底图；
//    两帧间隔（流畅度）、同步耗时（主线程等着，地图库画图和改数据都在这一步）、显卡耗时；
// 3. 四种模式互相切换：过渡全程和每一段、C++ 里换模式 / 换底图 / 数据重建各多久、期间最长一帧；
// 4. 数据重建（筛选 / 自动保存后都会整份重建）、保存一次数据、写一次设置（用 settings.json 的副本）。
// 云端 llvmpipe 上的数只看相对大小；PO 电脑（UHD 620）上的数才是要的。
#include "selftest_util.h"

#include "appcontroller.h"
#include "framestats.h"
#include "mapcontroller.h"
#include "markerlayer.h"
#include "perf.h"
#include "plancontroller.h"
#include "datastore.h"
#include "jsonvalue.h"
#include "settings.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>

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

// 现在看得见的玻璃面板（每块都在实时截背后的地图再模糊）
int visibleGlass(QQuickItem *root)
{
    if (!root || !root->isVisible()) return 0;
    int n = 0;
    if (QString::fromLatin1(root->metaObject()->className()).startsWith(QLatin1String("GlassPanel")) && root->opacity() > 0.01
        && root->width() > 0 && root->height() > 0)
        ++n;
    for (QQuickItem *c : root->childItems()) n += visibleGlass(c);
    return n;
}

QString fmtEntries(const QList<perf::Entry> &list, const QString &what)
{
    QStringList out;
    for (const perf::Entry &e : list)
        if (e.what == what) out << QStringLiteral("%1 ms").arg(e.ms, 0, 'f', 1) + (e.detail.isEmpty() ? QString() : QStringLiteral("（%1）").arg(e.detail));
    return out.join(QStringLiteral("；"));
}

QString fmtPrefixed(const QList<perf::Entry> &list, const QString &prefix)
{
    QStringList out;
    for (const perf::Entry &e : list)
        if (e.what.startsWith(prefix))
            out << QStringLiteral("%1 %2 ms").arg(e.what).arg(e.ms, 0, 'f', 0) + (e.detail.isEmpty() ? QString() : QStringLiteral("（%1）").arg(e.detail));
    return out.join(QStringLiteral("；"));
}

} // namespace

void runPerf(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win)
{
    r.section(QStringLiteral("性能（0.9.1，先量再改；这一节只记数，不判断快慢）"));
    FrameStats *fs = frameStats();
    PlanController *pc = plansCtl();

    QQuickItem *mapItem = win ? win->findChild<QQuickItem *>(QStringLiteral("map")) : nullptr;
    if (!mapItem || !pc) { r.info(QStringLiteral("没有地图（maplibre 没加载）或计划控制器，跳过其余")); return; }
    if (!waitFor([mapItem] { return mapItem->property("styleLoaded").toBool(); }, 20000)) { r.info(QStringLiteral("地图样式没加载完，跳过")); return; }
    // 1. 启动各步（等地图样式加载完再写，最后一步才在里面）
    {
        QStringList steps;
        for (const perf::Entry &e : perf::startupSteps()) steps << QStringLiteral("%1 %2").arg(e.what).arg(e.atMs, 0, 'f', 0);
        r.info(QStringLiteral("启动（从进程开始的毫秒数）：") + steps.join(QStringLiteral(" → ")));
        // 启动时（到第一帧为止）数据重建了几次：每次都是整份
        double firstFrame = 0;
        for (const perf::Entry &e : perf::startupSteps()) if (e.what == QStringLiteral("第一帧")) firstFrame = e.atMs;
        int n = 0;
        double total = 0;
        QStringList each;
        for (const perf::Entry &e : perf::entriesSince(0)) {
            if (e.what != QStringLiteral("数据重建") || (firstFrame > 0 && e.atMs > firstFrame + 1000)) continue;
            ++n;
            total += e.ms;
            each << QString::number(e.ms, 'f', 0);
        }
        if (n > 0) r.info(QStringLiteral("启动时数据重建了 %1 次，共 %2 ms（每次 %3 ms）").arg(n).arg(total, 0, 'f', 0).arg(each.join(QStringLiteral(" / "))));
    }

    if (!fs) r.info(QStringLiteral("没挂每帧计时（FrameStats），只有 C++ 计时"));
    QQuickItem *cover = findItem(win->contentItem(), QStringLiteral("baseCover"));
    const auto goPage = [win](const QString &p) { QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, p)); };
    const auto setGlass = [win](bool on) { QMetaObject::invokeMethod(win, "setGlassForTest", Q_ARG(QVariant, on)); };
    mapc->markers()->setShown(true);
    mapc->setBase(QStringLiteral("street"));
    waitRendered(win, mapc, 2000);

    // 2. 拖地图：每 16 毫秒拖一下（约 440 像素 / 秒，来回走一圈回到原处），共 2.5 秒
    const auto drag = [&](const QString &label) {
        mapc->jumpTo(31, 112, 4.6);
        waitRendered(win, mapc, 3000);   // 等瓦片（第二次起在缓存里）
        perf::resetStats();
        const int glass = visibleGlass(win->contentItem());
        const double t0 = perf::nowMs();
        mapc->dragStart();
        QElapsedTimer t;
        t.start();
        QTimer timer;
        timer.setTimerType(Qt::PreciseTimer);
        timer.setInterval(16);
        QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
            const double ph = t.elapsed() / 2500.0 * 2 * M_PI;
            mapc->dragBy(7 * std::cos(ph), 2 * std::sin(2 * ph));
        });
        timer.start();
        waitFor([&] { return t.elapsed() >= 2500; }, 5000);
        timer.stop();
        const double t1 = perf::nowMs();
        mapc->dragEnd();
        mapc->jumpTo(mapc->lat(), mapc->lng(), mapc->zoom());   // 不要惯性
        const perf::Stat mk = perf::stat(QStringLiteral("标记摆位"));
        const QString frames = fs ? fs->summary(t0 + 100, t1).text() : QStringLiteral("（没有每帧计时）");
        r.info(QStringLiteral("拖地图 · %1（看得见的玻璃 %2 块）：%3；标记摆位（C++）平均 %4 ms / 次、最长 %5 ms（%6 次）")
                   .arg(label).arg(glass).arg(frames).arg(mk.avg(), 0, 'f', 2).arg(mk.max, 0, 'f', 1).arg(mk.count));
        pause(300);
    };
    drag(QStringLiteral("计划模式、街道图（平时的样子）"));
    setGlass(false);
    waitFrames(win, 2);
    drag(QStringLiteral("透明效果关掉（玻璃换成实底）"));
    setGlass(true);
    mapc->markers()->setShown(false);
    waitFrames(win, 2);
    drag(QStringLiteral("标记藏起来（玻璃开）"));
    mapc->markers()->setShown(true);
    QMetaObject::invokeMethod(win, "toggleList");
    waitStill(win->findChild<QQuickItem *>(QStringLiteral("planList")), 1500);
    drag(QStringLiteral("打开我的旅程列表"));
    setGlass(false);
    waitFrames(win, 2);
    drag(QStringLiteral("打开我的旅程列表、透明效果关掉"));
    setGlass(true);
    QMetaObject::invokeMethod(win, "toggleList");
    waitStill(win->findChild<QQuickItem *>(QStringLiteral("planList")), 1500);

    // 3. 换模式：真的走 ⋯ 泡泡的路（goPage → applyMode）。0.9.2 起四种模式共用一个底图，直接换、不盖帧
    const auto modeName = [](const QString &m) {
        return m == QLatin1String("plan") ? QStringLiteral("计划") : m == QLatin1String("wish") ? QStringLiteral("想去")
             : m == QLatin1String("foot") ? QStringLiteral("足迹") : QStringLiteral("照片");
    };
    for (const QString &m : { QStringLiteral("wish"), QStringLiteral("foot"), QStringLiteral("photo"), QStringLiteral("plan"),
                              QStringLiteral("foot"), QStringLiteral("plan") }) {
        const QString from = pc->mode();
        const QString baseBefore = mapc->base();
        const double t0 = perf::nowMs();
        goPage(m);
        const bool switched = waitFor([&] { return pc->mode() == m && !(cover && cover->property("active").toBool()); }, 6000);
        const double tCover = perf::nowMs();
        // 画面停下来：0.15 秒没有新的帧（最多再等 3 秒）
        if (fs) waitFor([&] { return perf::nowMs() - std::max(fs->lastSwapAt(), tCover) > 150; }, 3000);
        const double tDone = perf::nowMs();
        const QList<perf::Entry> es = perf::entriesSince(t0);
        QString line = QStringLiteral("%1 → %2（底图 %3 → %4）：").arg(modeName(from), modeName(m), baseBefore, mapc->base());
        line += switched ? fmtPrefixed(es, QStringLiteral("换模式（直接换")) : QStringLiteral("6 秒内没换完");
        const QString cm = fmtEntries(es, QStringLiteral("换模式（C++）")), cb = fmtEntries(es, QStringLiteral("换底图（C++）")),
                      rb = fmtEntries(es, QStringLiteral("数据重建")), st = fmtEntries(es, QStringLiteral("地图库加载样式"));
        if (!cm.isEmpty()) line += QStringLiteral("；C++ 换模式 ") + cm;
        if (!cb.isEmpty()) line += QStringLiteral("；C++ 换底图 ") + cb;
        if (!rb.isEmpty()) line += QStringLiteral("；其中数据重建 ") + rb;
        if (!st.isEmpty()) line += QStringLiteral("；地图库加载样式 ") + st;
        if (fs) {
            const FrameStats::Summary s = fs->summary(t0, tDone);
            line += QStringLiteral("；到画面停下 %1 ms，期间最长一帧间隔 %2 ms、最长同步 %3 ms").arg(tDone - t0, 0, 'f', 0)
                        .arg(fs->longestGapSince(t0), 0, 'f', 0).arg(s.syncMax, 0, 'f', 0);
        }
        r.info(line);
        pause(400);
    }

    // 3b. 换底图（0.9.2）：走图层泡泡的路（pickBase → 盖帧 → 新底图画好就淡出）。街道图 → 已解锁 → 街道图 → 卫星图 → 街道图
    for (const QString &b : { QStringLiteral("unlock"), QStringLiteral("street"), QStringLiteral("sat"), QStringLiteral("street") }) {
        const QString from = mapc->base();
        const double t0 = perf::nowMs();
        QMetaObject::invokeMethod(win, "pickBase", Q_ARG(QVariant, b));
        const bool done = waitFor([&] { return mapc->base() == b && !(cover && cover->property("active").toBool()); }, 6000);
        const double tCover = perf::nowMs();
        if (fs) waitFor([&] { return perf::nowMs() - std::max(fs->lastSwapAt(), tCover) > 150; }, 3000);
        const double tDone = perf::nowMs();
        const QList<perf::Entry> es = perf::entriesSince(t0);
        QString line = QStringLiteral("换底图 %1 → %2：").arg(from, b);
        line += done ? fmtPrefixed(es, QStringLiteral("换底图 过渡")) : QStringLiteral("6 秒内没换完");
        const QString cb = fmtEntries(es, QStringLiteral("换底图（C++）")), st = fmtEntries(es, QStringLiteral("地图库加载样式")),
                      wait = fmtEntries(es, QStringLiteral("等后台的省界模型"));
        if (!cb.isEmpty()) line += QStringLiteral("；C++ ") + cb;
        if (!wait.isEmpty()) line += QStringLiteral("；其中等后台的省界模型 ") + wait;
        if (!st.isEmpty()) line += QStringLiteral("；地图库加载样式 ") + st;
        if (fs) {
            const FrameStats::Summary sm = fs->summary(t0, tDone);
            line += QStringLiteral("；到画面停下 %1 ms，期间最长一帧间隔 %2 ms、最长同步 %3 ms").arg(tDone - t0, 0, 'f', 0)
                        .arg(fs->longestGapSince(t0), 0, 'f', 0).arg(sm.syncMax, 0, 'f', 0);
        }
        r.info(line);
        pause(500);
    }

    // 4. 数据重建（不改数据，只按现在的筛选整份重建 5 次）、保存数据 3 次、写设置 10 次（副本）
    {
        perf::resetStats();
        double worstGap = 0;
        for (int i = 0; i < 5; ++i) {
            const double t0 = perf::nowMs();
            mapc->refilter();
            mapc->jumpTo(mapc->lat(), mapc->lng() + 0.01, mapc->zoom());   // 让地图画一帧（新数据在同步时交给地图库）
            pause(350);
            if (fs) worstGap = std::max(worstGap, fs->longestGapSince(t0));
        }
        const perf::Stat rb = perf::stat(QStringLiteral("数据重建"));
        const QList<perf::Entry> last = perf::entriesSince(perf::nowMs() - 2500);
        r.info(QStringLiteral("数据重建（%1）：平均 %2 ms、最长 %3 ms（最后一次：%4）；之后最长一帧间隔 %5 ms")
                   .arg(mapc->property("dataInfo").toString())
                   .arg(rb.avg(), 0, 'f', 1).arg(rb.max, 0, 'f', 1)
                   .arg(fmtEntries(last, QStringLiteral("数据重建")).section(QStringLiteral("；"), -1))
                   .arg(worstGap, 0, 'f', 0));
        // 0.9.2：改一个计划（置顶 / 取消置顶，自动保存后重建）：只重算这一个
        if (!mapc->trips().plans().isEmpty()) {
            const QString pid = mapc->trips().plans().first().id;
            const double t0 = perf::nowMs();
            pc->togglePin(pid);
            pause(500);
            pc->togglePin(pid);
            pause(500);
            const QList<perf::Entry> es = perf::entriesSince(t0);
            r.info(QStringLiteral("改一个计划（置顶再取消）后的数据重建：%1").arg(fmtEntries(es, QStringLiteral("数据重建"))));
        }
        for (int i = 0; i < 3; ++i) { app->saveNow(false); pause(150); }   // 和开发版"原样保存一次"一样：没改动也真的写
        const perf::Stat sv = perf::stat(QStringLiteral("保存数据"));
        r.info(QStringLiteral("保存一次数据（数据副本，%1 KB）：平均 %2 ms、最长 %3 ms（%4 次）")
                   .arg(QFileInfo(app->store()->dataDir() + QStringLiteral("/travel.json")).size() / 1024)
                   .arg(sv.avg(), 0, 'f', 1).arg(sv.max, 0, 'f', 1).arg(sv.count));
        if (Settings *real = settingsObj()) {
            QTemporaryDir dir(QFileInfo(real->path()).absolutePath() + QStringLiteral("/selftest-settings-XXXXXX"));
            const QString copy = dir.path() + QStringLiteral("/settings.json");
            if (dir.isValid() && QFile::copy(real->path(), copy)) {
                Settings s;
                s.load(copy);
                perf::resetStats();
                for (int i = 0; i < 10; ++i) {
                    s.setValue(QStringLiteral("selftestCounter"), json::Value(double(i)));
                    s.save();
                }
                const perf::Stat ws = perf::stat(QStringLiteral("写设置"));
                r.info(QStringLiteral("写一次 settings.json（和正式的同一个文件夹，%1 KB）：平均 %2 ms、最长 %3 ms；平时换一次模式要写 3–4 次")
                           .arg(QFileInfo(copy).size() / 1024.0, 0, 'f', 1).arg(ws.avg(), 0, 'f', 1).arg(ws.max, 0, 'f', 1));
            } else {
                r.info(QStringLiteral("写设置：副本建不起来（%1），跳过").arg(QDir::toNativeSeparators(real->path())));
            }
        }
    }

    goPage(QStringLiteral("plan"));
    waitFor([&] { return pc->mode() == QLatin1String("plan") && !(cover && cover->property("active").toBool()); }, 4000);
    mapc->setBase(QStringLiteral("street"));
    r.check(pc->mode() == QLatin1String("plan"), QStringLiteral("性能一节跑完，回到计划模式"));
}

} // namespace tp::app::selftest
