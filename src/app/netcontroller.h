// 联网开关和地图缓存（0.12.5，T-09、T-11；QML 里是 netc）。
// 开关存 settings.json，键名和含义同 Electron prefs.js：offline（总开关）、net（{ tiles: false, … }，没写 = 开）。
// 真正拦请求的是 NetClient，这里只负责读写设置、告诉界面。
#pragma once

#include <QObject>
#include <QVariantMap>

namespace tp::services { class NetClient; class TileCache; }

namespace tp::app {

class Settings;

class NetController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool offline READ offline WRITE setOffline NOTIFY changed)
    Q_PROPERTY(QVariantMap switches READ switches NOTIFY changed)   // 每项自己的开关（不看总开关）
    Q_PROPERTY(QVariantMap allowed READ allowed NOTIFY changed)     // 算上总开关，现在能不能用
    Q_PROPERTY(double tileBytes READ tileBytes NOTIFY cacheChanged)
    Q_PROPERTY(bool tileScanned READ tileScanned NOTIFY cacheChanged)
    Q_PROPERTY(bool tileClearing READ tileClearing NOTIFY cacheChanged)
    Q_PROPERTY(int tileMaxMB READ tileMaxMB WRITE setTileMaxMB NOTIFY cacheChanged)   // 0.12.6 硬盘上限；settings.json 的 tileCacheMaxMB，没写 = 2048

public:
    NetController(Settings *settings, services::NetClient *net, services::TileCache *tiles, QObject *parent = nullptr);

    bool offline() const;
    void setOffline(bool on);
    QVariantMap switches() const;
    QVariantMap allowed() const;
    double tileBytes() const;
    bool tileScanned() const;
    bool tileClearing() const;
    int tileMaxMB() const;
    void setTileMaxMB(int mb);

    Q_INVOKABLE void setSwitch(const QString &key, bool on);
    Q_INVOKABLE void clearTileCache();             // 移进回收站；完成发 tileCacheCleared
    Q_INVOKABLE void offlineForShot();             // 截图用：不写 settings.json
    void reload();                                 // 按 settings.json 重新设一遍（程序启动、自检收尾）

signals:
    void changed();
    void cacheChanged();
    void tileCacheCleared(bool ok);

private:
    Settings *m_settings;
    services::NetClient *m_net;
    services::TileCache *m_tiles;
};

} // namespace tp::app
