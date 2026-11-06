// 本机瓦片服务（0.8.5，功能清单 M-04、M-05、M-06）。
//
// 为什么要它：maplibre-native-qt 399311e 的 QML 项把瓦片缓存写死在内存（":memory:"），也不把单张瓦片加载失败报出来
// （读源码核实：map_quick_item.cpp 没有暴露 Settings、没有错误信号），不改第三方代码就做不了硬盘缓存和"连续失败 8 次"提示。
// 所以骨架样式里的底图地址改成 http://127.0.0.1:<端口>/tile/<底图>/{z}/{y}/{x}，由这里：
//   - 先查硬盘缓存（tilecache.h），有就直接给；旧了（> 30 天）照样先给，后台再取一次
//   - 没有就经统一请求通道（netclient.h，服务名 tiles：离线 / 单关瓦片时不发）向 Esri 取，存进缓存再给
//   - 每次真的联网取的结果发 fetched(底图, 成功?)：地图按"连续失败 8 次"出横幅（Electron map.on('error')）
//   - 预取（Electron prefetchTiles）：只取回存进缓存，同一时间最多 4 个，不和地图要的抢
// 只监听 127.0.0.1（别的电脑连不上）；路径严格检查（只认登记过的底图和数字），其余一律 404。
// HTTP 只实现 maplibre 用得到的：GET / HEAD，HTTP/1.1 长连接，同一个连接上的请求按顺序回。
#pragma once

#include "tilecache.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>
#include <deque>
#include <functional>

class QTcpServer;
class QTcpSocket;

namespace tp::services {

class NetClient;

class TileServer : public QObject {
    Q_OBJECT
public:
    struct Source {
        QString key;        // street / sat（0.9.0 起没有浅灰）
        QString upstream;   // https://…/tile/{z}/{y}/{x}
        int maxZoom = 19;
    };

    TileServer(NetClient *net, TileCache *cache, QObject *parent = nullptr);
    ~TileServer() override;

    void addSource(const Source &s) { m_sources.insert(s.key, s); }
    bool start();                       // 在 127.0.0.1 上随便找一个空端口
    quint16 port() const { return m_port; }
    bool running() const { return m_port != 0; }
    // 交给 maplibre 的瓦片地址模板
    QString localTemplate(const QString &key) const;
    void setService(const QString &name) { m_service = name; }   // 开关用的服务名（默认 tiles）
    bool online() const;                // 离线总开关、瓦片开关都没关

    // 预取（Electron prefetchTiles：同一张只预取一次，每次最多 48 张；缓存里有的、正在取的不再取）
    static constexpr int kPrefetchMax = 48;
    int prefetch(const QVector<TileKey> &tiles);
    bool prefetchBusy() const { return m_prefetchInFlight > 0 || !m_prefetchQueue.empty(); }   // 自检用

    // 地球用（0.9.0）：不走 HTTP，按同样的规则给一张瓦片（硬盘缓存 → 联网，旧了后台再取），
    // 联网结果同样发 fetched（算进"连续失败 8 次"）。status：200 / 404 / 502 / 503（离线或关了瓦片）
    using TileDone = std::function<void(int status, const QByteArray &body)>;
    void requestTile(const TileKey &k, TileDone done);

    struct Stats {
        int requests = 0;   // maplibre 要了几张
        int hits = 0;       // 其中缓存里有的
        int fetched = 0;    // 联网取成功（含预取）
        int failed = 0;     // 联网取失败（含预取）
        int prefetched = 0; // 预取成功
    };
    Stats stats() const { return m_stats; }
    // 0.9.2：地图库要的瓦片里还在路上的（正在联网取的 + 正在回给地图库的连接）。换底图时判断"新底图画好没有"用
    int busyForMap() const;
    // 离上一次回给地图库一张瓦片过了多少毫秒（还没回过 = 很大）
    qint64 msSinceLastResponse() const { return m_lastResponse.isValid() ? m_lastResponse.elapsed() : (1LL << 40); }
    void resetStats() { m_stats = {}; }
    // 测试用：上游地址里的 {z}/{y}/{x} 换完以后再改一下（指到本机的假服务器）
    void setUpstreamRewrite(std::function<QString(const QString &)> f) { m_rewrite = std::move(f); }

    static constexpr int kTimeoutMs = 20000;
    static constexpr int kPrefetchParallel = 4;

signals:
    // 地图要的瓦片真的联网取了一次：ok = 取到了；detail = 失败原因（写日志用）
    void fetched(const QString &base, bool ok, const QString &detail);

private:
    struct Conn { QByteArray buf; bool busy = false; bool closeAfter = false; };
    using Done = std::function<void(int status, const QByteArray &body)>;
    struct Pending { QVector<Done> waiters; bool forMap = false; };

    void onNewConnection();
    void onReadyRead(QTcpSocket *s);
    void processNext(QTcpSocket *s);
    void respond(QTcpSocket *s, int status, const QByteArray &body, bool head);
    void handle(const QByteArray &path, Done done);
    void fetch(const TileKey &k, bool forMap, Done done);
    void pumpPrefetch();
    QString upstreamUrl(const TileKey &k) const;

    NetClient *m_net = nullptr;
    TileCache *m_cache = nullptr;
    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    QString m_service = QStringLiteral("tiles");
    QHash<QString, Source> m_sources;
    QHash<QTcpSocket *, Conn> m_conns;
    QHash<QString, Pending> m_pending;    // 正在取的（相对路径）
    std::deque<TileKey> m_prefetchQueue;
    QSet<QString> m_prefetched;
    int m_prefetchInFlight = 0;
    Stats m_stats;
    QElapsedTimer m_lastResponse;
    std::function<QString(const QString &)> m_rewrite;
};

} // namespace tp::services
