// 计时（0.9.1 优化批：先量再改）。只记数、不改行为。
//
// - perf::Scope：量一段代码。超过 logAboveMs 写一行日志"[计时] 名字 12.3 ms（细分…）"，不管多少都累计进统计；
//   每帧都跑的代码（标记摆位等）把 logAboveMs 设大，只看累计（自检"性能"一节读）。
// - perf::startup("一步")：启动的每一步写"[启动] 一步 @ 123 ms"（从进程开始算），自检读出来。
// - perf::mark(名字, 毫秒)：外面量好的数（QML 里用 Date.now() 量的）写进日志和统计。
// - 最近 400 条（写进日志的）记在内存里（自检按时间段取）；统计按名字累计次数、总和、最大（自检开头清零）。
// 线程安全（一把锁）；全部在 tp_core，只依赖 Qt Core。
#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QString>

namespace tp::app::perf {

// 从进程开始（这个模块被加载）到现在的毫秒数
double nowMs();

// 0.9.2（PO 选 8，计时留在正式程序里）：平时只写超过 50 ms 的（启动各步、"主线程卡了"照常写）；
// 截图 / 自检 / 内存测试时 setLogAll(true) 全部写
void setLogAll(bool on);
void startup(const char *step);
void mark(const QString &what, double ms, const QString &detail = {}, bool log = true);

struct Entry { double atMs = 0; QString what; double ms = 0; QString detail; };
QList<Entry> entriesSince(double atMs);
QList<Entry> startupSteps();

struct Stat { int count = 0; double total = 0, max = 0; double avg() const { return count ? total / count : 0; } };
Stat stat(const QString &what);
void resetStats();

// 量一段代码；析构时记一笔。细分（sub）在析构前用 part() 加，日志里写成"（读文件 3.1、解析 8.0）"
class Scope
{
public:
    explicit Scope(const char *what, double logAboveMs = 0);
    ~Scope();
    void part(const char *name);   // 记下从上一个 part（或开始）到现在这一段
    double elapsedMs() const { return m_t.nsecsElapsed() / 1e6; }
private:
    const char *m_what;
    double m_logAbove;
    QElapsedTimer m_t;
    double m_lastPart = 0;
    QString m_detail;
};

// 主线程卡住的记录（0.9.1）：每 50 毫秒醒一次，两次之间隔了超过 thresholdMs 就写一行"[计时] 主线程卡了 X ms（之前最后一件事：…）"。
// 和卡住检测（10 秒弹框）不同，这个只写日志，用来在日常使用里找出"哪里顿了一下"
void startStallWatch(int thresholdMs = 250);

} // namespace tp::app::perf
