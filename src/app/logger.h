// 日志（0.4.0）：%LOCALAPPDATA%\TravelPlannerNative\logs\（PO：单独放，不进程序目录；F12 打开这个文件夹）
//   app.log     所有 qDebug / qInfo / qWarning / qCritical（带时间）
//   errors.log  只有警告和错误
// 每个文件超过 256 KB 时只留后一半（和 Electron errors.log 一样）。线程安全；同时照常输出到 stderr。
#pragma once

#include <QString>

namespace tp::app::Logger {

void install(const QString &logDir);
QString dir();

// 往日志文件末尾追加一段（和日志共用一把锁；出错报告、崩溃提示用）
void appendRaw(const QString &path, const QByteArray &bytes);

// qFatal 时先调这个（崩溃处理借它写报告：Qt 在 Windows 上 qFatal 用 __fastfail 直接结束进程，崩溃过滤器接不到）
using FatalHook = void (*)(const QString &message);
void setFatalHook(FatalHook hook);

// 文件超过 maxBytes 时只留后一半（从一行的开头开始）。返回是否截断了（测试用）
bool trimFile(const QString &path, qint64 maxBytes = 256 * 1024);

} // namespace tp::app::Logger
