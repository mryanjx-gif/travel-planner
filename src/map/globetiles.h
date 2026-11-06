// 地球的瓦片（0.9.0 重写）：按视野加载，取代原型的整张 4096² 墨卡托拼图。
//
// - 选哪些瓦片：globegeom.h 的 selectTiles（四叉树，按屏幕上的大小分级；背面、屏幕外的不要）
// - 卫星图：经本机瓦片服务的 requestTile（同一份硬盘缓存、同一个离线开关、联网失败算进"连续失败 8 次"）
// - 卫星图还没到 / 离线：用本机画的简图（GlobeRaster）（z0–z1，一直留着）顶着；某块没到时先用已有的上一级放大顶着
// - 显卡上：一个 256×256 的纹理数组，kLayers 层（128 层约 43 MB 连 mipmap），按"最近没用到"换出；
//   每层要传的图连 mipmap 在后台线程算好（QImage 缩半），渲染线程只管上传
// - 离开地球（release）：全部丢掉，只剩硬盘缓存
// 0.9.3：地球只有卫星图，0.9.0 的"已解锁"瓦片（Src::Unlock、颜色版本）删掉。
// 只在主线程用；渲染线程在 synchronize（主线程被挡住）时取 takeFrame()。
#pragma once

#include "globegeom.h"
#include "globeraster.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace tp::services { class TileServer; }

namespace tp::map {

class GlobeTileSet : public QObject {
    Q_OBJECT
public:
    // 0.9.2：192 → 128（PO 选"试一下"）。0.9.1 在 PO 电脑上量到：192 层（64 MB）让显卡内存分配器新要了一整块 256 MB、
    // 回平面后不还；拉近到 6 级只用了 106–111 层。128 层看能不能放进原来几块里空着的地方（自检"地球"一节看块数）。
    // 一帧最多选 kLayers × 2/3 ≈ 85 块（以前 128），窗口很大时整体粗一级
    static constexpr int kLayers = 128;
    static constexpr int kMaxNetInFlight = 8;
    static constexpr int kMaxRasterInFlight = 3;
    static constexpr int kUploadsPerFrame = 12;

    enum class Src : int { Sat = 0, SatFallback = 2 };   // 1 是 0.9.0–0.9.2 的已解锁，不再用

    explicit GlobeTileSet(QObject *parent = nullptr);
    ~GlobeTileSet() override;

    void setTileServer(tp::services::TileServer *s) { m_server = s; }
    void setAdminFile(const QString &adm1) { m_raster.setFile(adm1); }
    void setMaxZ(int z) { m_maxZ = z; }

    // 相机变了：重新选瓦片、发请求、排好要画的（每次事件循环最多一次，由 GlobeItem 调）。view 用设备像素
    void update(const globe::View &deviceView);
    void release();          // 离开地球
    bool released() const { return m_released; }

    // ---- 交给渲染那边 ----
    struct Instance { float merc[4]; float uv[4]; float layer; };   // merc：x0 y0 x1 y1（归一化墨卡托）；uv：在那一层里的范围
    struct Upload { int layer; QVector<QImage> mips; };
    struct Frame {
        QVector<Instance> instances;
        QVector<Upload> uploads;
    };
    Frame takeFrame();       // 取走要上传的图（每帧最多 kUploadsPerFrame 张）和要画的块
    void gpuLost();          // 渲染那边重建了纹理数组：所有层都作废

    // ---- 自检 / 状态 ----
    struct Stats {
        int selected = 0, maxZ = 0, drawn = 0, resident = 0, pending = 0, netInFlight = 0, rasterInFlight = 0;
        int fetched = 0, failed = 0, rendered = 0, evicted = 0, uploaded = 0;
        int fallbackDrawn = 0;   // 用上一级 / 简图顶着的块数
        bool geometry = false;
    };
    Stats stats() const;
    bool busy() const { return m_netInFlight > 0 || m_rasterInFlight > 0 || !m_uploadQueue.isEmpty(); }

signals:
    void changed();   // 有新的图可以画了（或者要画的变了）

private:
    static quint64 pack(Src s, int z, int x, int y) { return (quint64(int(s)) << 56) | (quint64(z) << 48) | (quint64(y) << 24) | quint64(x); }
    static Src srcOf(quint64 k) { return Src(int(k >> 56)); }
    static int zOf(quint64 k) { return int((k >> 48) & 0xff); }
    static int yOf(quint64 k) { return int((k >> 24) & 0xffffff); }
    static int xOf(quint64 k) { return int(k & 0xffffff); }

    enum class State { None, Requested, Ready, Resident };
    struct Entry {
        State state = State::None;
        int layer = -1;            // Resident：在哪一层
        bool refreshing = false;   // 层里有旧图，同时在取新的
        quint64 lastUsed = 0;
        QVector<QImage> mips;      // Ready：等着上传
        int failures = 0;
        qint64 retryAt = 0;        // 失败后多久再试（毫秒时钟）
    };

    bool drawable(quint64 k) const;
    void use(quint64 k);
    bool needsRequest(quint64 k, qint64 now) const;
    void request(quint64 k);
    void onImage(quint64 k, int gen, const QImage &img);
    void onFailed(quint64 k, int gen, bool permanent);
    int takeLayer();
    void addInstance(quint64 drawKey, int z, int x, int y);
    void placeInstances(bool markUse);   // 按 m_lastSel 排要画的（没到的用子块 / 上一级 / 垫底顶着）
    static QVector<QImage> makeMips(const QImage &img);
    static qint64 nowMs();

    tp::services::TileServer *m_server = nullptr;
    GlobeRaster m_raster;
    int m_maxZ = 12;
    QHash<quint64, Entry> m_entries;
    QVector<quint64> m_layerOwner;       // 每层现在装的是哪块（~0 = 空）
    QVector<int> m_freeLayers;
    QVector<quint64> m_uploadQueue;      // 等上传的（Ready），按到达顺序
    QVector<Instance> m_instances;
    quint64 m_frame = 0;
    int m_gen = 0;                       // 离开地球 / 显卡纹理重建 +1：之前发出去的结果不要了
    int m_netInFlight = 0, m_rasterInFlight = 0;
    bool m_released = true;
    globe::View m_lastView;
    std::vector<globe::TileId> m_lastSel;
    bool m_haveView = false;
    Stats m_stats;
    QTimer m_retry;
};

} // namespace tp::map
