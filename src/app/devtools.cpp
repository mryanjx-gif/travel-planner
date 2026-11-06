#include "devtools.h"
#include "procmem.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <cmath>

MemLogger::MemLogger(const QString &csvPath, const QString &label, int minutes, QObject *parent)
    : QObject(parent), m_path(csvPath), m_label(label), m_t0(QDateTime::currentMSecsSinceEpoch())
{
    if (!m_path.isEmpty()) {
        QFile f(m_path);
        if (!QFileInfo::exists(m_path) && f.open(QIODevice::WriteOnly))
            f.write("elapsed_s,label,working_set_mb,private_mb,gpu_dedicated_mb,gpu_shared_mb\n");
        auto *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, &MemLogger::sample);
        t->start(30000);
        QTimer::singleShot(2000, this, &MemLogger::sample);
    }
    if (minutes > 0) QTimer::singleShot(qEnvironmentVariableIsSet("TP_MEMTEST_SECONDS") ? qEnvironmentVariableIntValue("TP_MEMTEST_SECONDS") * 1000 : minutes * 60000, qApp, [] { QCoreApplication::exit(0); });
}

void MemLogger::setStepsPath(const QString &path)
{
    m_steps = path;
    QFile f(m_steps);
    if (!m_steps.isEmpty() && f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write("elapsed_s,label,loop,after_step,working_set_mb,private_mb,gpu_local_mb,gpu_nonlocal_mb\n");
}

void MemLogger::mark(const QString &afterStep, int loop)
{
    if (m_steps.isEmpty()) return;
    const ProcMem m = readProcMem();
    QFile f(m_steps);
    if (!f.open(QIODevice::Append)) return;
    const double s = (QDateTime::currentMSecsSinceEpoch() - m_t0) / 1000.0;
    const auto gpu = [](double v) { return v < 0 ? QString() : QString::number(v, 'f', 1); };
    f.write(QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8\n").arg(s, 0, 'f', 1).arg(m_label).arg(loop).arg(afterStep)
                .arg(m.workingSetMB, 0, 'f', 1).arg(m.privateMB, 0, 'f', 1).arg(gpu(m.gpuLocalMB), gpu(m.gpuNonLocalMB)).toUtf8());
}

void MemLogger::sample()
{
    const ProcMem m = readProcMem();
    QFile f(m_path);
    if (!f.open(QIODevice::Append)) return;
    const double s = (QDateTime::currentMSecsSinceEpoch() - m_t0) / 1000.0;
    f.write(QStringLiteral("%1,%2,%3,%4,,\n").arg(s, 0, 'f', 0).arg(m_label).arg(m.workingSetMB, 0, 'f', 1).arg(m.privateMB, 0, 'f', 1).toUtf8());
}

namespace {
struct Series { QString label; QVector<double> t, ws, priv, gpu, gpuShared; };

// 最小二乘斜率（MB / 小时），只用后一半数据（前面是启动和预热）
double slopePerHour(const QVector<double> &t, const QVector<double> &y)
{
    const int n = int(t.size()), s0 = n / 2;
    if (n - s0 < 3) return qQNaN();
    double mt = 0, my = 0;
    int k = 0;
    for (int i = s0; i < n; ++i) if (!std::isnan(y[i])) { mt += t[i]; my += y[i]; ++k; }
    if (k < 3) return qQNaN();
    mt /= k; my /= k;
    double num = 0, den = 0;
    for (int i = s0; i < n; ++i) if (!std::isnan(y[i])) { num += (t[i] - mt) * (y[i] - my); den += (t[i] - mt) * (t[i] - mt); }
    return den > 0 ? num / den * 3600.0 : qQNaN();
}
} // namespace

namespace DevTools {
int plot(const QString &outPng, const QStringList &csvFiles)
{
    QMap<QString, Series> by;
    QStringList order;
    for (const QString &fn : csvFiles) {
        QFile f(fn);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&f);
        in.readLine();
        while (!in.atEnd()) {
            const QStringList c = in.readLine().split(',');
            if (c.size() < 4) continue;
            const QString label = c[1];
            if (!by.contains(label)) { by[label].label = label; order << label; }
            Series &s = by[label];
            const auto num = [&c](int i) { bool ok = false; const double v = c.value(i).toDouble(&ok); return ok ? v : qQNaN(); };
            s.t << num(0); s.ws << num(2); s.priv << num(3); s.gpu << num(4); s.gpuShared << num(5);
        }
    }
    if (order.isEmpty()) return 2;

    const int W = 1400, H = 980, L = 90, R = 340, T = 60;
    const int panelH = 360, gap = 90;
    QImage img(W, H, QImage::Format_RGB32);
    img.fill(QColor("#f8fafc"));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font;
    font.setFamilies({ "Noto Serif SC", "Noto Serif CJK SC", "Microsoft YaHei", "SimSun" });
    font.setPixelSize(15);
    p.setFont(font);
    const QColor ink("#16202a"), muted("#5d6d7b"), line("#dbe3e9");
    const QList<QColor> pal{ QColor("#2d5f8b"), QColor("#c2560f"), QColor("#2b7a3c"), QColor("#5b4a8b"), QColor("#8b2d5f") };

    double tMax = 60;
    for (const Series &s : by) for (double t : s.t) tMax = std::max(tMax, t);
    const auto drawPanel = [&](int top, const QString &title, auto pick) {
        double yMax = 10;
        for (const Series &s : by) for (double v : pick(s)) if (!std::isnan(v)) yMax = std::max(yMax, v);
        yMax = std::ceil(yMax * 1.15 / 50.0) * 50.0;
        const QRectF plotR(L, top, W - L - R, panelH);
        p.setPen(ink);
        QFont tf = font; tf.setPixelSize(17); tf.setBold(true); p.setFont(tf);
        p.drawText(QPointF(L, top - 14), title);
        p.setFont(font);
        for (int i = 0; i <= 5; ++i) {   // 横网格 + 纵轴刻度
            const double y = plotR.bottom() - plotR.height() * i / 5;
            p.setPen(QPen(line, 1));
            p.drawLine(QPointF(plotR.left(), y), QPointF(plotR.right(), y));
            p.setPen(muted);
            p.drawText(QRectF(0, y - 10, L - 10, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(yMax * i / 5, 'f', 0));
        }
        const int stepMin = tMax > 5400 ? 15 : tMax > 1800 ? 10 : 2;
        for (int m = 0; m * 60 <= tMax; m += stepMin) {
            const double x = plotR.left() + plotR.width() * m * 60 / tMax;
            p.setPen(muted);
            p.drawText(QRectF(x - 30, plotR.bottom() + 6, 60, 20), Qt::AlignCenter, QString::number(m));
        }
        p.drawText(QRectF(plotR.right() - 120, plotR.bottom() + 26, 120, 20), Qt::AlignRight, QStringLiteral("分钟"));
        p.drawText(QRectF(8, top - 34, L - 14, 20), Qt::AlignRight, QStringLiteral("MB"));
        int k = 0;
        for (const QString &lab : order) {
            const Series &s = by[lab];
            const QVector<double> y = pick(s);
            QPainterPath path; bool started = false;
            for (int i = 0; i < s.t.size(); ++i) {
                if (std::isnan(y[i])) continue;
                const QPointF pt(plotR.left() + plotR.width() * s.t[i] / tMax, plotR.bottom() - plotR.height() * y[i] / yMax);
                if (!started) { path.moveTo(pt); started = true; } else path.lineTo(pt);
            }
            p.setPen(QPen(pal[k % pal.size()], 2.2));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            ++k;
        }
    };
    drawPanel(T + 20, QStringLiteral("私有内存（Private Bytes）"), [](const Series &s) { return s.priv; });
    // 独立显卡看专用显存；集成显卡专用显存一直是 0，改看共享显存（0.2.3：之前画出来是空的）
    bool anyDedicated = false, anyShared = false;
    for (const Series &s : by) {
        for (double v : s.gpu) if (!std::isnan(v) && v > 0) anyDedicated = true;
        for (double v : s.gpuShared) if (!std::isnan(v) && v > 0) anyShared = true;
    }
    const bool useShared = !anyDedicated && anyShared;
    if (anyDedicated) drawPanel(T + 20 + panelH + gap, QStringLiteral("GPU 专用显存"), [](const Series &s) { return s.gpu; });
    else if (anyShared) drawPanel(T + 20 + panelH + gap, QStringLiteral("GPU 共享显存（集成显卡，专用显存为 0）"), [](const Series &s) { return s.gpuShared; });
    else drawPanel(T + 20 + panelH + gap, QStringLiteral("工作集（Working Set）"), [](const Series &s) { return s.ws; });

    // 右侧图例 + 数字
    int y = T + 20, k = 0;
    for (const QString &lab : order) {
        const Series &s = by[lab];
        const auto first = [](const QVector<double> &v) { for (double x : v) if (!std::isnan(x)) return x; return qQNaN(); };
        const auto last = [](const QVector<double> &v) { for (int i = v.size() - 1; i >= 0; --i) if (!std::isnan(v[i])) return v[i]; return qQNaN(); };
        const auto mx = [](const QVector<double> &v) { double m = qQNaN(); for (double x : v) if (!std::isnan(x)) m = std::isnan(m) ? x : std::max(m, x); return m; };
        const QColor c = pal[k % pal.size()];
        p.setPen(Qt::NoPen); p.setBrush(c);
        p.drawRoundedRect(QRectF(W - R + 24, y + 4, 14, 14), 3, 3);
        p.setPen(ink);
        QFont bf = font; bf.setBold(true); p.setFont(bf);
        p.drawText(QPointF(W - R + 46, y + 17), lab);
        p.setFont(font); p.setPen(muted);
        const auto fmt = [](double v) { return std::isnan(v) ? QStringLiteral("—") : QString::number(v, 'f', 0); };
        const double sl = slopePerHour(s.t, s.priv);
        QStringList lines{
            QStringLiteral("时长 %1 分钟，%2 个点").arg(s.t.isEmpty() ? 0 : int(s.t.last() / 60)).arg(s.t.size()),
            QStringLiteral("私有：起 %1 → 末 %2，峰 %3 MB").arg(fmt(first(s.priv)), fmt(last(s.priv)), fmt(mx(s.priv))),
            QStringLiteral("后半段斜率 %1").arg(std::isnan(sl) || s.t.last() < 1200 ? QStringLiteral("—（时长不到 20 分钟不算）") : QString::number(sl, 'f', 1) + QStringLiteral(" MB/小时")),
            QStringLiteral("工作集 峰 %1 MB").arg(fmt(mx(s.ws))) };
        const QVector<double> &g = useShared ? s.gpuShared : s.gpu;
        if (!std::isnan(mx(g))) lines << QStringLiteral("%1 起 %2 → 末 %3，峰 %4 MB").arg(useShared ? QStringLiteral("共享显存") : QStringLiteral("专用显存"), fmt(first(g)), fmt(last(g)), fmt(mx(g)));
        int ly = y + 40;
        for (const QString &l : lines) { p.drawText(QPointF(W - R + 46, ly), l); ly += 22; }
        y = ly + 18; ++k;
    }
    p.setPen(muted);
    p.drawText(QPointF(L, H - 20), QStringLiteral("旅行计划 原生版 %1 · 内存测试 · %2").arg(QCoreApplication::applicationVersion(),
                                                                                  QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")));
    p.end();
    return img.save(outPng) ? 0 : 3;
}
}
