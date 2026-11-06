// 开发 / 测试用开关（阶段 ⓪）：--screenshot、--memtest、--plot。正式版保留 --screenshot 给云端测试用。
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

class QQuickWindow;

namespace DevTools {
// 画内存曲线：csv 每行 elapsed_s,label,working_set_mb,private_mb,gpu_dedicated_mb,gpu_shared_mb
int plot(const QString &outPng, const QStringList &csvFiles);
}

// 内存测试：到时间自动退出。给了 csvPath 就每 30 秒把本进程内存写进去（Windows 上 内存测试.ps1 另外从外面采样，含显存）；
// 给了 stepsPath 就由 QML 的测试循环在每一步开始前调 mark()，记下"上一步做完 6 秒后"的内存和显存（0.8.6 分步测试）
class MemLogger : public QObject {
    Q_OBJECT
public:
    MemLogger(const QString &csvPath, const QString &label, int minutes, QObject *parent = nullptr);
    void setStepsPath(const QString &path);
    // afterStep：刚做完的那一步；loop：第几圈（从 0 起）
    Q_INVOKABLE void mark(const QString &afterStep, int loop);
private:
    void sample();
    QString m_path, m_label, m_steps;
    qint64 m_t0;
};
