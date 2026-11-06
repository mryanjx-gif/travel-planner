// 原生版自己的文件放在哪（PO 2026-11-02，0.4.0 起）：
//   设置   <程序目录>\settings.json        程序目录不可写时 %APPDATA%\TravelPlannerNative\settings.json
//   数据   <程序目录>\data\（默认位置）     程序目录不可写时 %APPDATA%\TravelPlannerNative\data\；设置里可以换成任何文件夹
//   缓存   <程序目录>\cache\               程序目录不可写时 %LOCALAPPDATA%\TravelPlannerNative\cache 文件夹
//   日志   %LOCALAPPDATA%\TravelPlannerNative\logs\（PO：日志单独放，不放进程序目录）
// 开发时程序目录是 D:\TravelPlannerNative-dev\app，安装后默认 D:\TravelPlannerNative。
#pragma once
#include <QString>

namespace Paths {
QString appDir();                 // exe 所在目录
bool appDirWritable();            // 程序目录能不能写（能建文件、能删）
QString settingsFile();
QString defaultDataDir();         // 默认数据位置（不保证已存在）
QString cacheDir();               // 已创建好的缓存目录（结尾不带斜杠）
QString logDir();                 // 已创建好的日志目录
QString electronSettingsFile();   // Electron 版的设置：%APPDATA%\TravelPlanner\settings.json（只读）

// 测试用：把"程序目录"换成别的文件夹（空 = 恢复）
void overrideAppDirForTest(const QString &dir);
}
