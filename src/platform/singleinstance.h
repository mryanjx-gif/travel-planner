// 单实例（W-01，0.4.0）：同一个程序目录只开一个窗口。再开一次时把已有窗口调到前面（含从托盘恢复），新开的那个直接退出。
// 用 QLocalServer（Qt Network，LGPL-3，已核实）。名字含程序目录的哈希：开发版和安装版可以各开一个
// （两个版本同时开着同一个数据文件夹时，靠冲突检测和外部修改提示保护数据）。
#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

namespace tp::platform {

class SingleInstance : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstance(QObject *parent = nullptr);
    // true = 我是第一个（已开始监听）；false = 已有一个在运行，已经通知它显示窗口，调用方应退出
    // waitForPrevious = 自己是上一个进程重启出来的（Vulkan 失败后用 OpenGL 重启）：等上一个退出后再接手，最多 5 秒，不发"显示"
    bool start(const QString &key, bool waitForPrevious = false);
    static QString keyFor(const QString &appDir);

signals:
    void activateRequested();   // 另一个进程请求显示窗口

private:
    QLocalServer *m_server = nullptr;
};

} // namespace tp::platform
