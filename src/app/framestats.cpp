#include "framestats.h"
#include "perf.h"

#include <QQuickGraphicsConfiguration>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <rhi/qrhi.h>

#include <algorithm>

namespace tp::app {

namespace {
double pct(QList<double> v, double p)
{
    if (v.isEmpty()) return -1;
    std::sort(v.begin(), v.end());
    const qsizetype i = std::clamp<qsizetype>(qsizetype(p * (v.size() - 1) + 0.5), 0, v.size() - 1);
    return v.at(i);
}
} // namespace

void FrameStats::enableGpuTimestamps(QQuickWindow *win)
{
    if (!win) return;
    QQuickGraphicsConfiguration gc = win->graphicsConfiguration();
    gc.setTimestamps(true);
    win->setGraphicsConfiguration(gc);
}

FrameStats::FrameStats(QQuickWindow *win, QObject *parent) : QObject(parent), m_win(win)
{
    // 下面这些信号都在渲染线程上发（多线程渲染时），直接连、自己加锁
    connect(win, &QQuickWindow::beforeSynchronizing, this, [this] { m_syncStart = perf::nowMs(); }, Qt::DirectConnection);
    connect(win, &QQuickWindow::afterSynchronizing, this, [this] { m_syncMs = perf::nowMs() - m_syncStart; }, Qt::DirectConnection);
    connect(win, &QQuickWindow::beforeRendering, this, [this] { m_renderStart = perf::nowMs(); }, Qt::DirectConnection);
    connect(win, &QQuickWindow::afterRendering, this, [this] {
        m_renderMs = perf::nowMs() - m_renderStart;
        m_gpuMs = -1;
        if (QSGRendererInterface *ri = m_win->rendererInterface()) {
            auto *sc = static_cast<QRhiSwapChain *>(ri->getResource(m_win, QSGRendererInterface::RhiSwapchainResource));
            if (QRhiCommandBuffer *cb = sc ? sc->currentFrameCommandBuffer() : nullptr) {
                const double t = cb->lastCompletedGpuTime();   // 几帧之前那一帧的显卡耗时（秒）；没打开时间戳时是 0
                if (t > 0) m_gpuMs = t * 1000;
            }
        }
    }, Qt::DirectConnection);
    connect(win, &QQuickWindow::frameSwapped, this, [this] {
        const QMutexLocker lock(&m_lock);
        m_frames.append({ perf::nowMs(), m_syncMs, m_renderMs, m_gpuMs });
        if (m_frames.size() > 6000) m_frames.remove(0, 1000);
    }, Qt::DirectConnection);
}

FrameStats::Summary FrameStats::summary(double fromMs, double toMs) const
{
    QList<double> iv, sync, render, gpu;
    double prev = -1;
    {
        const QMutexLocker lock(&m_lock);
        for (const Frame &f : m_frames) {
            if (f.swapAt < fromMs || f.swapAt > toMs) { if (f.swapAt < fromMs) prev = f.swapAt; continue; }
            if (prev >= 0 && prev >= fromMs - 1) iv << f.swapAt - prev;   // 只算两帧都在这段里的间隔
            prev = f.swapAt;
            sync << f.syncMs;
            render << f.renderMs;
            if (f.gpuMs > 0) gpu << f.gpuMs;
        }
    }
    Summary s;
    s.frames = int(sync.size());
    s.intervalMedian = pct(iv, 0.5);
    s.intervalP95 = pct(iv, 0.95);
    s.intervalMax = iv.isEmpty() ? -1 : *std::max_element(iv.cbegin(), iv.cend());
    s.over50 = int(std::count_if(iv.cbegin(), iv.cend(), [](double d) { return d > 50; }));
    s.syncMedian = pct(sync, 0.5);
    s.syncMax = sync.isEmpty() ? -1 : *std::max_element(sync.cbegin(), sync.cend());
    s.renderMedian = pct(render, 0.5);
    s.gpuMedian = pct(gpu, 0.5);
    s.gpuP95 = pct(gpu, 0.95);
    return s;
}

double FrameStats::longestGapSince(double fromMs) const
{
    const QMutexLocker lock(&m_lock);
    double prev = fromMs, best = -1;
    for (const Frame &f : m_frames) {
        if (f.swapAt < fromMs) continue;
        best = std::max(best, f.swapAt - prev);
        prev = f.swapAt;
    }
    return best;
}

double FrameStats::lastSwapAt() const
{
    const QMutexLocker lock(&m_lock);
    return m_frames.isEmpty() ? -1 : m_frames.last().swapAt;
}

QString FrameStats::Summary::text() const
{
    if (frames == 0) return QStringLiteral("没有画新的帧");
    QString t = QStringLiteral("%1 帧；两帧间隔 中位 %2 / 95% %3 / 最长 %4 ms，超过 50 ms 的 %5 次；同步（主线程等着）中位 %6 ms，渲染线程 %7 ms")
                    .arg(frames).arg(intervalMedian, 0, 'f', 1).arg(intervalP95, 0, 'f', 1).arg(intervalMax, 0, 'f', 0).arg(over50)
                    .arg(syncMedian, 0, 'f', 1).arg(renderMedian, 0, 'f', 1);
    t += gpuMedian > 0 ? QStringLiteral("；显卡 中位 %1 / 95% %2 ms").arg(gpuMedian, 0, 'f', 1).arg(gpuP95, 0, 'f', 1)
                       : QStringLiteral("；显卡耗时拿不到");
    return t;
}

double PerfBridge::now() const { return perf::nowMs(); }
void PerfBridge::mark(const QString &what, double ms, const QString &detail) const { perf::mark(what, ms, detail); }
void PerfBridge::startup(const QString &step) const { perf::startup(step.toUtf8().constData()); }

} // namespace tp::app
