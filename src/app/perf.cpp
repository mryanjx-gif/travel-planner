#include "perf.h"

#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QTimer>

namespace tp::app::perf {

namespace {

// 静态初始化时开始计时：比 main() 还早一点，当作"进程开始"
struct Clock { QElapsedTimer t; Clock() { t.start(); } };
Clock &clock()
{
    static Clock c;
    return c;
}
[[maybe_unused]] const bool kClockStarted = (clock(), true);

QMutex g_lock;
QList<Entry> g_recent;          // 最近 400 条
QList<Entry> g_startup;         // 启动的每一步（只记第一次启动，最多 40 条）
QHash<QString, Stat> g_stats;
QString g_last;                 // 最近记的一件事（主线程卡住时写进日志）
constexpr int kRecent = 400;
bool g_logAll = false;
constexpr double kQuietMs = 50;   // 平时只写超过这么久的

// 统计总是累计；最近记录只放写进日志的那些（每帧都跑、只累计的不放，免得把别的挤掉）
void record(const QString &what, double ms, const QString &detail, bool keep)
{
    const QMutexLocker lock(&g_lock);
    if (keep) {
        g_recent.append(Entry{ nowMs(), what, ms, detail });
        if (g_recent.size() > kRecent) g_recent.remove(0, g_recent.size() - kRecent);
        g_last = what;
    }
    Stat &s = g_stats[what];
    ++s.count;
    s.total += ms;
    s.max = std::max(s.max, ms);
}

} // namespace

double nowMs() { return clock().t.nsecsElapsed() / 1e6; }

void setLogAll(bool on) { g_logAll = on; }

void startup(const char *step)
{
    const double at = nowMs();
    {
        const QMutexLocker lock(&g_lock);
        if (g_startup.size() < 40) g_startup.append({ at, QString::fromUtf8(step), 0, {} });
        g_last = QString::fromUtf8(step);
    }
    qInfo("[启动] %s @ %.0f ms", step, at);
}

void mark(const QString &what, double ms, const QString &detail, bool log)
{
    if (!g_logAll && ms < kQuietMs) log = false;
    record(what, ms, detail, log);
    if (log) {
        if (detail.isEmpty()) qInfo("[计时] %s %.1f ms", qUtf8Printable(what), ms);
        else qInfo("[计时] %s %.1f ms（%s）", qUtf8Printable(what), ms, qUtf8Printable(detail));
    }
}

QList<Entry> entriesSince(double atMs)
{
    const QMutexLocker lock(&g_lock);
    QList<Entry> out;
    for (const Entry &e : std::as_const(g_recent))
        if (e.atMs >= atMs) out.append(e);
    return out;
}

QList<Entry> startupSteps()
{
    const QMutexLocker lock(&g_lock);
    return g_startup;
}

Stat stat(const QString &what)
{
    const QMutexLocker lock(&g_lock);
    return g_stats.value(what);
}

void resetStats()
{
    const QMutexLocker lock(&g_lock);
    g_stats.clear();
}

Scope::Scope(const char *what, double logAboveMs) : m_what(what), m_logAbove(logAboveMs) { m_t.start(); }

void Scope::part(const char *name)
{
    const double now = elapsedMs();
    if (!m_detail.isEmpty()) m_detail += QStringLiteral("、");
    m_detail += QStringLiteral("%1 %2").arg(QString::fromUtf8(name)).arg(now - m_lastPart, 0, 'f', 1);
    m_lastPart = now;
}

Scope::~Scope()
{
    const double ms = elapsedMs();
    mark(QString::fromUtf8(m_what), ms, m_detail, ms >= m_logAbove);
}

void startStallWatch(int thresholdMs)
{
    if (!QCoreApplication::instance()) return;
    auto *timer = new QTimer(QCoreApplication::instance());
    timer->setInterval(50);
    timer->setTimerType(Qt::PreciseTimer);
    auto *last = new QElapsedTimer;   // 跟着程序走到结束，不释放
    last->start();
    QObject::connect(timer, &QTimer::timeout, timer, [last, thresholdMs] {
        const double gap = last->nsecsElapsed() / 1e6;
        last->restart();
        if (gap < thresholdMs || gap > 60000) return;   // 超过 1 分钟多半是电脑睡眠，不算
        QString before;
        {
            const QMutexLocker lock(&g_lock);
            before = g_last;
        }
        // 不进统计（免得自检里的"卡了"把别的数搅乱），只写日志
        qInfo("[计时] 主线程卡了 %.0f ms（之前最后记的一件事：%s）", gap - 50, qUtf8Printable(before.isEmpty() ? QStringLiteral("无") : before));
    });
    timer->start();
}

} // namespace tp::app::perf
