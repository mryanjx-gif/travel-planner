// 上次崩溃的记号（0.5.1）：崩溃那一刻 src/platform/crashhandler.cpp 在 logs 文件夹写 last-crash.txt（不用 Qt），
// 下次启动时这里读出来、删掉，界面弹"上次旅行计划意外退出了"。同时只留最近 3 个 crash-*.dmp。
// 文件格式（UTF-8）：开头几行 "键: 值"（code、dump、gl），一个空行，后面是完整的错误报告。
#pragma once

#include <QString>

namespace tp::app::CrashInfo {

inline constexpr const char *kMarker = "last-crash.txt";
inline constexpr int kKeepDumps = 3;

struct Last {
    bool found = false;
    QString code;      // TP-CRASH-xxxx
    QString dump;      // crash-YYYYMMDD-HHMMSS.dmp（文件名；没写成时为空）
    QString report;    // 完整的错误报告（复制给 Jason 的那段）
};

Last parse(const QByteArray &bytes);
// 读出并删掉记号；没有记号时 found = false
Last takeLast(const QString &logDir);
// 只留最近 keep 个 crash-*.dmp（按文件名里的时间），返回删掉的个数
int pruneDumps(const QString &logDir, int keep = kKeepDumps);

} // namespace tp::app::CrashInfo
