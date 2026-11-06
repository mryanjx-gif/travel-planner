#include "graphicsbackend.h"
#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QtGui/qtguiglobal.h>
#if QT_CONFIG(vulkan)
#include <QVulkanFunctions>
#include <QVulkanInstance>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString markerPath()
{
    return Paths::cacheDir() + QStringLiteral("/vulkan-starting.flag");
}

bool mlnPresent(const QString &dir)
{
    return QFileInfo::exists(dir + QStringLiteral("/qml/MapLibre/qmldir"));
}

// 能创建 Vulkan 实例、至少有一块物理设备，才算"支持"
bool probeVulkan(QString *why, QString *device)
{
#if !QT_CONFIG(vulkan)
    Q_UNUSED(device);
    *why = QStringLiteral("这个 Qt 编译时没有启用 Vulkan");
    return false;
#else
    QVulkanInstance inst;
    inst.setApiVersion(QVersionNumber(1, 1));
    if (!inst.create()) {
        qWarning("无法创建 Vulkan 实例，错误码 %d", int(inst.errorCode()));   // 错误码只写日志，界面上的原因是固定文字（好翻译）
        *why = QStringLiteral("无法创建 Vulkan 实例（显卡驱动可能不支持 Vulkan）");
        return false;
    }
    QVulkanFunctions *f = inst.functions();
    uint32_t n = 0;
    if (f->vkEnumeratePhysicalDevices(inst.vkInstance(), &n, nullptr) != VK_SUCCESS || n == 0) {
        *why = QStringLiteral("Vulkan 没有找到可用的显卡");
        return false;
    }
    QVector<VkPhysicalDevice> devs(qsizetype(n), VK_NULL_HANDLE);
    f->vkEnumeratePhysicalDevices(inst.vkInstance(), &n, devs.data());
    VkPhysicalDeviceProperties best{};
    for (VkPhysicalDevice d : devs) {
        VkPhysicalDeviceProperties p{};
        f->vkGetPhysicalDeviceProperties(d, &p);
        // 优先独立显卡，其次集成显卡
        if (best.deviceName[0] == 0 || (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                                        && best.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
            best = p;
    }
    *device = QStringLiteral("%1（Vulkan %2.%3）").arg(QString::fromUtf8(best.deviceName))
                  .arg(VK_API_VERSION_MAJOR(best.apiVersion)).arg(VK_API_VERSION_MINOR(best.apiVersion));
    return true;
#endif
}
} // namespace

namespace GraphicsBackend {

QString displayName(const QString &api)
{
    return api == QLatin1String("vulkan") ? QStringLiteral("Vulkan") : QStringLiteral("OpenGL");
}

BackendChoice choose(const QString &requested, const QString &appDir, const QString &prevReason)
{
    BackendChoice c;
    c.requested = requested.isEmpty() ? QStringLiteral("auto") : requested.toLower();
    const QString vkDir = appDir + QStringLiteral("/maplibre/vulkan");
    const QString glDir = appDir + QStringLiteral("/maplibre/opengl");

    if (c.requested == QLatin1String("opengl")) {
        c.api = QStringLiteral("opengl");
        c.reason = prevReason.isEmpty() ? QStringLiteral("命令行指定了 OpenGL") : prevReason;
    } else if (c.requested == QLatin1String("vulkan")) {
        c.api = QStringLiteral("vulkan");
        QString why, dev;
        if (probeVulkan(&why, &dev)) c.deviceInfo = dev;
        else c.reason = QStringLiteral("命令行指定了 Vulkan，但探测失败：") + why;
    } else {
        QString why, dev;
        if (!prevReason.isEmpty()) {
            why = prevReason;
        } else if (QFileInfo::exists(markerPath())) {
            why = QStringLiteral("上次用 Vulkan 启动后程序没能正常画出画面");
            c.vulkanCrashFallback = true;
            QFile::remove(markerPath());
        } else if (!mlnPresent(vkDir)) {
            why = QStringLiteral("没有 Vulkan 版地图库（maplibre/vulkan）");
        } else if (probeVulkan(&why, &dev)) {
            c.api = QStringLiteral("vulkan");
            c.deviceInfo = dev;
        }
        if (c.api.isEmpty()) {
            c.api = QStringLiteral("opengl");
            c.reason = why;
        }
    }
    c.mlnDir = c.api == QLatin1String("vulkan") ? vkDir : glDir;
    return c;
}

void apply(const BackendChoice &c)
{
    QQuickWindow::setGraphicsApi(c.api == QLatin1String("vulkan") ? QSGRendererInterface::Vulkan
                                                                   : QSGRendererInterface::OpenGL);
    const QString bin = QDir::toNativeSeparators(c.mlnDir + QStringLiteral("/bin"));
#ifdef Q_OS_WIN
    // Qt 用 LoadLibrary(绝对路径) 加载 QML 插件；插件依赖的 QMapLibre*.dll 按标准顺序查找，
    // SetDllDirectory 把这一套的 bin 目录插在程序目录之后。PATH 再加一次作保险。
    SetDllDirectoryW(reinterpret_cast<LPCWSTR>(bin.utf16()));
#endif
    qputenv("PATH", (bin + QDir::listSeparator() + QString::fromLocal8Bit(qgetenv("PATH"))).toLocal8Bit());
}

void markVulkanStarting()
{
    QFile f(markerPath());
    if (f.open(QIODevice::WriteOnly)) f.write("1");
}

void clearVulkanStarting()
{
    QFile::remove(markerPath());
}

} // namespace GraphicsBackend
