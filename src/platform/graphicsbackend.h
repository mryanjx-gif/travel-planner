// 图形后端选择：优先 Vulkan，不行退回 OpenGL（maplibre-native-qt 在 Windows 只支持这两种，不能用 D3D11）。
// 两套 maplibre 编译产物同名（QMapLibre.dll 等），分别放在 <程序目录>/maplibre/vulkan/ 和 /opengl/，
// 程序本身不链接它们，只通过 QML 插件加载；启动时选好后端，再把对应目录加进 DLL 搜索路径和 QML 导入路径。
#pragma once
#include <QString>

struct BackendChoice {
    QString api;          // "vulkan" 或 "opengl"
    QString requested;    // 命令行要求的："auto" / "vulkan" / "opengl"
    QString reason;       // 没用 Vulkan 的原因（中文，空 = 用的就是首选）
    QString deviceInfo;   // Vulkan 探测到的显卡名（OpenGL 的显卡名在窗口初始化后由 RHI 补上）
    QString mlnDir;       // 用哪一套 maplibre：<程序目录>/maplibre/<api>
    bool vulkanCrashFallback = false;   // 上次用 Vulkan 启动后没能正常画出画面（崩溃等），这次自动用 OpenGL
};

namespace GraphicsBackend {
// requested：auto / vulkan / opengl；prevReason：上一次自动退回时带过来的原因（--fallback-reason）
BackendChoice choose(const QString &requested, const QString &appDir, const QString &prevReason);
// 设置 Qt Quick 的图形 API，并把 maplibre 目录加进 DLL 搜索路径（必须在创建窗口之前调用）
void apply(const BackendChoice &c);
// "上次用 Vulkan 启动后异常退出"的记号：启动时写，画出稳定画面后删
void markVulkanStarting();
void clearVulkanStarting();
QString displayName(const QString &api);
}
