// 自检公共部分的实现（0.8.7 从 selftest.cpp 拆出来）。
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

#include <rhi/qrhi.h>
#include <atomic>
#include <memory>

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

// 等到 pred 为真（期间照常处理事件，不卡界面、不触发卡住检测）。超时返回 false
bool waitFor(const std::function<bool()> &pred, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() > timeoutMs) return false;
        QEventLoop loop;
        QTimer::singleShot(30, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

void pause(int ms) { waitFor([] { return false; }, ms); }

bool waitFrames(QQuickWindow *win, int n, int timeoutMs)
{
    if (!win) return false;
    auto count = std::make_shared<int>(0);
    const QMetaObject::Connection c = QObject::connect(win, &QQuickWindow::frameSwapped, win, [count] { ++*count; }, Qt::DirectConnection);
    win->update();
    const bool ok = waitFor([&] { if (*count < n) win->update(); return *count >= n; }, timeoutMs);
    QObject::disconnect(c);
    return ok;
}

bool waitMapIdle(QQuickWindow *win, map::MapController *mapc, int timeoutMs)
{
    const bool idle = waitFor([mapc] { return !mapc || !mapc->moving(); }, timeoutMs);
    return waitFrames(win, 2, 1500) && idle;
}

bool waitRendered(QQuickWindow *win, map::MapController *mapc, int timeoutMs)
{
    waitFrames(win, 1, 1000);   // 先让这一帧的瓦片请求发出去
    const bool ok = waitFor([mapc] { return !mapc || (!mapc->moving() && mapc->tilesBusy() == 0); }, timeoutMs);
    return waitFrames(win, 3, 1500) && ok;
}

bool waitGlobeIdle(QQuickWindow *win, GlobeItem *globe, int timeoutMs)
{
    if (!globe) return waitFrames(win, 2, 1500);
    const bool still = waitFor([globe] { return !globe->animating() && !globe->morphing(); }, timeoutMs);
    waitFor([globe] { return globe->tilesSettled(); }, timeoutMs);
    return waitFrames(win, 2, 1500) && still;
}

bool waitStill(QQuickItem *it, int timeoutMs)
{
    if (!it) return false;
    QRectF last(-1e9, -1e9, 0, 0);
    int still = 0;
    return waitFor([&] {
        const QRectF now(it->mapToScene(QPointF(0, 0)), QSizeF(it->width(), it->height()));
        still = (now == last) ? still + 1 : 0;
        last = now;
        return still >= 3;
    }, timeoutMs);
}

// 0.9.1：显卡内存分配器（Vulkan 的 VMA）的统计。QRhi::statistics 要在渲染线程上读：挂在 afterRendering 上，
// 让窗口画一帧，读到就摘掉。块（block）是分配器向驱动要的一大块显存，用掉的 + 没用的 = 驱动那边记的占用
RhiMem readRhiMem(QQuickWindow *win)
{
    if (!win || !win->rhi()) return {};
    auto res = std::make_shared<RhiMem>();
    auto done = std::make_shared<std::atomic_bool>(false);
    const QMetaObject::Connection c = QObject::connect(win, &QQuickWindow::afterRendering, win, [win, res, done] {
        if (done->load()) return;
        if (QRhi *rhi = win->rhi()) {
            const QRhiStats st = rhi->statistics();
            res->ok = st.blockCount > 0 || st.allocCount > 0;
            res->blocks = st.blockCount;
            res->allocs = st.allocCount;
            res->usedMB = st.usedBytes / 1048576.0;
            res->unusedMB = st.unusedBytes / 1048576.0;
        }
        done->store(true);
    }, Qt::DirectConnection);
    win->update();
    waitFor([done] { return done->load(); }, 2000);
    QObject::disconnect(c);
    return *res;
}

QString RhiMem::text() const
{
    if (!ok) return QStringLiteral("分配器统计拿不到（OpenGL 没有）");
    return QStringLiteral("%1 块共 %2 MB（用掉 %3、空着 %4），%5 个分配").arg(blocks).arg(usedMB + unusedMB, 0, 'f', 0)
        .arg(usedMB, 0, 'f', 0).arg(unusedMB, 0, 'f', 0).arg(allocs);
}

// 截一张窗口图，在 (x, y)（逻辑像素）附近 24 像素内找品红探针的中心；找不到返回 false
bool findProbe(QQuickWindow *win, double x, double y, double *fx, double *fy, QImage *shot)
{
    const QImage img = win->grabWindow();
    if (shot) *shot = img;
    if (img.isNull() || win->width() <= 0) return false;
    const double dpr = double(img.width()) / win->width();
    const int cx = int(std::lround(x * dpr)), cy = int(std::lround(y * dpr)), R = int(24 * dpr);
    double sx = 0, sy = 0;
    int n = 0;
    for (int yy = std::max(0, cy - R); yy < std::min(img.height(), cy + R); ++yy)
        for (int xx = std::max(0, cx - R); xx < std::min(img.width(), cx + R); ++xx) {
            const QRgb c = img.pixel(xx, yy);
            if (qRed(c) > 235 && qGreen(c) < 40 && qBlue(c) > 235) { sx += xx; sy += yy; ++n; }
        }
    if (n < 4) return false;
    *fx = (sx / n + 0.5) / dpr;
    *fy = (sy / n + 0.5) / dpr;
    return true;
}

double luminanceAt(const QImage &img, QQuickWindow *win, double x, double y)
{
    const double dpr = double(img.width()) / std::max(1, win->width());
    double sum = 0;
    int n = 0;
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) {
            const int xx = int(x * dpr) + dx, yy = int(y * dpr) + dy;
            if (xx < 0 || yy < 0 || xx >= img.width() || yy >= img.height()) continue;
            const QRgb c = img.pixel(xx, yy);
            sum += 0.2126 * qRed(c) + 0.7152 * qGreen(c) + 0.0722 * qBlue(c);
            ++n;
        }
    return n ? sum / n : 0;
}

// 窗口截图里 (x, y)（逻辑像素）的颜色
QColor colorAt(const QImage &img, QQuickWindow *win, double x, double y)
{
    const double dpr = double(img.width()) / std::max(1, win->width());
    const int xx = int(std::lround(x * dpr)), yy = int(std::lround(y * dpr));
    if (xx < 0 || yy < 0 || xx >= img.width() || yy >= img.height()) return {};
    return QColor(img.pixel(xx, yy));
}

double colorDiff(const QColor &a, const QColor &b)
{
    return std::max({ std::abs(a.red() - b.red()), std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue()) });
}

} // namespace tp::app::selftest
