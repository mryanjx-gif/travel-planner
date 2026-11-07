// 地图瓦片的硬盘缓存（0.8.5，功能清单 M-06）。
//
// Electron 版靠 Chromium 自带的 HTTP 磁盘缓存（main.js：disk-cache-size 2 GB）；原生版的 maplibre QML 项把缓存写死在内存里
// （map_quick_item.cpp：setCacheDatabasePath(":memory:")），所以由本机瓦片服务（tileserver.h）自己存：
//   - 一张瓦片一个文件：<缓存目录>\tiles\<底图>\<z>\<x>\<y>.t（写临时文件再改名，QSaveFile）
//   - 总量上限默认 2 GB；超了就从"最久没有重新取过"的删起，删到上限的 90%
//   - 取回来超过 30 天的瓦片算"旧了"：照样先显示，同时在后台重新取一次（取到了就覆盖，文件时间也跟着更新）。
//     所以经常看的地方文件时间不会超过 30 天，删的时候按文件时间从旧到新删，大致就是"最久没看过的先删"
//   - 启动时在后台线程把已有的文件数一遍（几万个文件在 Windows 上要一两秒，不卡界面）；数完之前不删
// 只依赖 Qt Core。单元测试 tst_net 在临时文件夹里测（上限、按时间删、旧瓦片、清空、坏文件名）。
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QThread;

namespace tp::services {

struct TileKey {
    QString base;   // street / sat（0.9.0 起没有浅灰；旧的 gray 文件夹留着，按上限慢慢删掉）
    int z = 0, x = 0, y = 0;
    QString path() const { return QStringLiteral("%1/%2/%3/%4.t").arg(base).arg(z).arg(x).arg(y); }
    bool operator==(const TileKey &o) const { return base == o.base && z == o.z && x == o.x && y == o.y; }
};

class TileCache : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 kDefaultMaxBytes = 2LL * 1024 * 1024 * 1024;   // 2 GB（Electron disk-cache-size）
    static constexpr int kStaleDays = 30;

    explicit TileCache(const QString &dir, QObject *parent = nullptr);
    ~TileCache() override;

    QString dir() const { return m_dir; }
    void setMaxBytes(qint64 b) { m_max = b; evictIfNeeded(); }
    qint64 maxBytes() const { return m_max; }
    // 测试用：现在的时间（毫秒）
    void setClock(std::function<qint64()> nowMs) { m_now = std::move(nowMs); }

    // 在后台数一遍已有的文件（启动时调一次）；scanDone() 之后才会按上限删
    void startScan();
    bool scanned() const { return m_scanned; }
    void waitScan();   // 测试 / 退出用：等后台数完

    struct Hit { QByteArray data; bool stale = false; };
    std::optional<Hit> get(const TileKey &k);
    bool contains(const TileKey &k) const;
    bool put(const TileKey &k, const QByteArray &data);

    qint64 bytes() const { return m_bytes; }
    int count() const { return int(m_index.size()); }
    // 清空（测试用）：直接删掉整个缓存文件夹里的瓦片
    void clear();
    // 设置页的"清空"（0.12.5）：整个文件夹移进回收站，文件多时要很久，放后台线程；完成发 trashDone。移的时候不读不存
    void clearToTrash();
    bool clearing() const { return m_clearing; }
    QString lastTrashPath() const { return m_trashPath; }   // 测试用：回收站里的位置

signals:
    void scanDone();
    void sizeChanged();
    void trashDone(bool ok);

private:
    struct Entry { qint64 size = 0; qint64 mtime = 0; };
    qint64 now() const;
    void evictIfNeeded();
    void mergeScan();

    QString m_dir;
    qint64 m_max = kDefaultMaxBytes;
    std::function<qint64()> m_now;
    QHash<QString, Entry> m_index;   // 相对路径 → 大小、取回来的时间
    qint64 m_bytes = 0;
    bool m_scanned = false;
    QThread *m_scanThread = nullptr;
    QThread *m_trashThread = nullptr;
    bool m_clearing = false;
    QString m_trashPath;
    std::shared_ptr<std::atomic_bool> m_scanCancel;
    std::shared_ptr<QHash<QString, Entry>> m_scanResult;
};

} // namespace tp::services
