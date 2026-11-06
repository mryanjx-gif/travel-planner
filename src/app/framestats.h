// 每一帧的时间（0.9.1 优化批：先量再改）。挂在窗口上，记下渲染线程上每一帧的：
// - 换帧时刻（frameSwapped）：两帧的间隔就是用户看到的流畅度（60 Hz 屏幕上 16.7 ms 是满帧）；
// - 同步耗时（beforeSynchronizing → afterSynchronizing）：这段时间主线程被挡住；
// - 渲染线程录命令的耗时（beforeRendering → afterRendering）；
// - 显卡耗时（QRhi 时间戳，要在窗口显示前打开 QQuickGraphicsConfiguration::setTimestamps；拿不到时是 -1）。
// 只在自检 / 内存测试 / 环境变量 TP_FRAME_STATS=1 时挂上（平时不挂，零开销）。
#pragma once

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>

class QQuickWindow;

namespace tp::app {

class FrameStats : public QObject
{
    Q_OBJECT
public:
    // 要在窗口显示（场景图初始化）之前调用，才能打开显卡时间戳
    static void enableGpuTimestamps(QQuickWindow *win);
    explicit FrameStats(QQuickWindow *win, QObject *parent = nullptr);

    struct Frame { double swapAt = 0, syncMs = 0, renderMs = 0, gpuMs = -1; };
    struct Summary {
        int frames = 0;
        double intervalMedian = 0, intervalP95 = 0, intervalMax = 0;
        int over50 = 0;          // 间隔超过 50 ms 的帧（看得出的顿）
        double syncMedian = 0, syncMax = 0, renderMedian = 0;
        double gpuMedian = -1, gpuP95 = -1;
        QString text() const;    // 一行中文
    };
    // [fromMs, toMs]（perf::nowMs 的时间）里的帧
    Summary summary(double fromMs, double toMs) const;
    // fromMs 之后最长的一次两帧间隔（毫秒），没有帧时返回 -1
    double longestGapSince(double fromMs) const;
    // 最后一帧换上来的时刻（perf::nowMs），还没有帧时 -1
    double lastSwapAt() const;

private:
    QQuickWindow *m_win;
    mutable QMutex m_lock;
    QList<Frame> m_frames;      // 最近 6000 帧（约 100 秒）
    double m_syncStart = 0, m_renderStart = 0, m_syncMs = 0, m_renderMs = 0, m_gpuMs = -1;
};

// QML 里用的计时（context property "perf"）：perf.now() 毫秒；perf.mark("换模式", ms, "细分")
class PerfBridge : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE double now() const;
    Q_INVOKABLE void mark(const QString &what, double ms, const QString &detail = {}) const;
    Q_INVOKABLE void startup(const QString &step) const;
};

} // namespace tp::app
