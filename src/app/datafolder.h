// 数据文件夹：启动时用哪个、换文件夹、在某处新建空数据（D-14 / D-15 / D-19，0.4.0）。
//
// 启动时（PO 2026-11-02；0.6.0 起原生版不再和 Electron 版共用数据文件夹，改成"从 Electron 版导入"）：
//   1. 设置里记住的文件夹有 travel.json → 用它
//   2. 记住的是 Electron 版的数据文件夹（只有 data.js，0.5.x 留下的设置）→ 问"从 Electron 版导入 / 新建空数据 / 选择…"
//   3. 记住了但找不到了 → 问用户（重新选择 / 在软件文件夹里新建）
//   4. 没记住（第一次用）：
//      a. 软件文件夹里的 data\ 已经有 travel.json → 用它
//      b. 电脑上有 Electron 版、它记住的数据文件夹有 data.js → 问"从 Electron 版导入 / 新建空数据 / 选择…"
//      c. 都没有 → 直接在软件文件夹里的 data\ 新建空数据
// Electron 版的设置和数据文件夹只读，不改。
#pragma once

#include <QString>

namespace tp::app {

namespace DataFolder {

bool isValid(const QString &dir);          // 里面有 travel.json（原生版的数据）
bool isElectron(const QString &dir);       // 里面有 data.js（Electron 版的数据）

enum class Startup {
    UseSaved,          // 用设置里记住的
    UseDefault,        // 用软件文件夹里已有的 data\（记进设置）
    AskFirstRun,       // 第一次用（或记住的是 Electron 版的文件夹），电脑上有 Electron 版的数据：问要不要导入
    AskMissing,        // 记住的找不到了：问
    CreateDefault,     // 直接在软件文件夹里新建
};

struct StartupInput
{
    QString savedDir;      // 设置里记住的（可能为空）
    QString defaultDir;    // 软件文件夹里的 data 文件夹
    QString electronDir;   // Electron 版记住的（可能为空）
};

Startup decide(const StartupInput &in);

// 读 Electron 版 settings.json 里的 dataDir；文件不存在、读不出来或那个文件夹里没有 data.js 时返回空
QString readElectronDataDir(const QString &electronSettingsFile);

// 在 dir 里新建空数据：travel.json（nativeformat emptyDoc）和 photos\。
// dir 里已经有 travel.json 时不动它，直接返回 true。dir 是 Electron 版的数据文件夹时不写，返回 false。失败时 error 是原因。
bool createEmpty(const QString &dir, const QString &appVersion, QString *error = nullptr);

// 两个路径是否指同一个文件夹（Windows 不分大小写）
bool samePath(const QString &a, const QString &b);

} // namespace DataFolder

} // namespace tp::app
