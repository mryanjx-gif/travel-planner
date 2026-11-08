#include "tileserver.h"

#include "netclient.h"

#include <QHostAddress>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace tp::services {

namespace {
constexpr int kMaxRequestBytes = 16 * 1024;

QByteArray reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

// 看前几个字节是不是图片（Esri 出错时可能回一页 HTML 也是 200）
QByteArray imageType(const QByteArray &b)
{
    if (b.startsWith("\xFF\xD8\xFF")) return "image/jpeg";
    if (b.startsWith("\x89PNG")) return "image/png";
    if (b.startsWith("RIFF") && b.mid(8, 4) == "WEBP") return "image/webp";
    if (b.startsWith("GIF8")) return "image/gif";
    return {};
}
} // namespace

TileServer::TileServer(NetClient *net, TileCache *cache, QObject *parent)
    : QObject(parent), m_net(net), m_cache(cache), m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &TileServer::onNewConnection);
}

TileServer::~TileServer()
{
    for (QTcpSocket *s : m_conns.keys()) s->disconnect(this);
}

bool TileServer::start()
{
    if (m_port) return true;
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        qWarning("本机瓦片服务没有起来：%s（底图直接连 Esri，没有硬盘缓存）", qUtf8Printable(m_server->errorString()));
        return false;
    }
    m_port = m_server->serverPort();
    qInfo("本机瓦片服务：127.0.0.1:%d", int(m_port));
    return true;
}

QString TileServer::localTemplate(const QString &key) const
{
    return QStringLiteral("http://127.0.0.1:%1/tile/%2/{z}/{y}/{x}").arg(m_port).arg(key);
}

bool TileServer::online() const { return m_net && m_net->allows(m_service); }

int TileServer::busyForMap() const
{
    int n = 0;
    for (const Pending &p : m_pending) if (p.forMap) ++n;
    for (const Conn &c : m_conns) if (c.busy) ++n;
    return n;
}

QString TileServer::upstreamUrl(const TileKey &k) const
{
    QString u = m_sources.value(k.base).upstream;
    u.replace(QLatin1String("{z}"), QString::number(k.z)).replace(QLatin1String("{y}"), QString::number(k.y)).replace(QLatin1String("{x}"), QString::number(k.x));
    return m_rewrite ? m_rewrite(u) : u;
}

// ---- HTTP ----

void TileServer::onNewConnection()
{
    while (QTcpSocket *s = m_server->nextPendingConnection()) {
        m_conns.insert(s, Conn{});
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] { m_conns.remove(s); s->deleteLater(); });
    }
}

void TileServer::onReadyRead(QTcpSocket *s)
{
    auto it = m_conns.find(s);
    if (it == m_conns.end()) return;
    it->buf += s->readAll();
    if (it->buf.size() > kMaxRequestBytes && !it->buf.contains("\r\n\r\n")) { s->abort(); return; }
    processNext(s);
}

void TileServer::processNext(QTcpSocket *s)
{
    auto it = m_conns.find(s);
    if (it == m_conns.end() || it->busy) return;
    const int end = it->buf.indexOf("\r\n\r\n");
    if (end < 0) return;
    const QByteArray head = it->buf.left(end);
    it->buf.remove(0, end + 4);
    const QList<QByteArray> lines = head.split('\n');
    const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray l = lines[i].trimmed().toLower();
        if (l.startsWith("connection:") && l.contains("close")) it->closeAfter = true;
    }
    if (first.size() < 3) { it->closeAfter = true; respond(s, 400, {}, false); return; }
    const QByteArray method = first[0];
    if (lines.value(0).trimmed().endsWith("HTTP/1.0")) it->closeAfter = true;
    const bool isHead = method == "HEAD";
    if (method != "GET" && !isHead) { respond(s, 405, {}, false); return; }
    it->busy = true;
    QPointer<QTcpSocket> sp(s);
    handle(first[1], [this, sp, isHead](int status, const QByteArray &body) {
        if (sp && m_conns.contains(sp)) respond(sp, status, body, isHead);
    });
}

void TileServer::respond(QTcpSocket *s, int status, const QByteArray &body, bool head)
{
    auto it = m_conns.find(s);
    if (it == m_conns.end()) return;
    m_lastResponse.start();
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status) + "\r\n";
    if (status == 200) {
        const QByteArray type = imageType(body);
        out += "Content-Type: " + (type.isEmpty() ? QByteArray("application/octet-stream") : type) + "\r\n";
        out += "Cache-Control: max-age=86400\r\n";   // maplibre 内存缓存里留一天（硬盘缓存在这边）
    }
    out += "Content-Length: " + QByteArray::number(head ? (status == 200 ? body.size() : 0) : body.size()) + "\r\n";
    out += it->closeAfter ? "Connection: close\r\n\r\n" : "Connection: keep-alive\r\n\r\n";
    if (!head) out += body;
    s->write(out);
    it->busy = false;
    if (it->closeAfter) { s->disconnectFromHost(); return; }
    processNext(s);
}

void TileServer::handle(const QByteArray &rawPath, Done done)
{
    static const QRegularExpression re(QStringLiteral("^/tile/([a-z]{1,16})/(\\d{1,2})/(\\d{1,7})/(\\d{1,7})$"));
    QByteArray path = rawPath;
    if (const int q = path.indexOf('?'); q >= 0) path.truncate(q);
    const QRegularExpressionMatch m = re.match(QString::fromLatin1(path));
    if (!m.hasMatch() || !m_sources.contains(m.captured(1))) { done(404, {}); return; }
    TileKey k{ m.captured(1), m.captured(2).toInt(), m.captured(4).toInt(), m.captured(3).toInt() };   // 路径是 z/y/x
    requestTile(k, std::move(done));
}

void TileServer::requestTile(const TileKey &k, TileDone done)
{
    const qint64 n = qint64(1) << k.z;
    if (!m_sources.contains(k.base) || k.z > m_sources.value(k.base).maxZoom || k.x < 0 || k.y < 0 || k.x >= n || k.y >= n) { done(404, {}); return; }
    ++m_stats.requests;
    if (const auto hit = m_cache->get(k)) {
        ++m_stats.hits;
        done(200, hit->data);
        // 旧了：后台再取一次（排在预取队列最前面，同样受"同一时间最多 4 个"限制）
        if (hit->stale && online() && !m_pending.contains(k.path())) { m_prefetchQueue.push_front(k); pumpPrefetch(); }
        return;
    }
    if (!online()) { done(503, {}); return; }   // 离线 / 关了瓦片：不联网，也不算失败（这时地图本来就切到"已解锁"了）
    fetch(k, true, std::move(done));
}

void TileServer::fetch(const TileKey &k, bool forMap, Done done)
{
    const QString rel = k.path();
    auto pit = m_pending.find(rel);
    if (pit != m_pending.end()) {   // 同一张已经在取：等它
        if (done) pit->waiters.push_back(std::move(done));
        pit->forMap = pit->forMap || forMap;
        return;
    }
    Pending p;
    p.forMap = forMap;
    if (done) p.waiters.push_back(std::move(done));
    m_pending.insert(rel, p);
    m_net->get(QUrl(upstreamUrl(k)), kTimeoutMs, this, [this, k, rel](const NetClient::Reply &r) {
        const Pending p = m_pending.take(rel);
        const bool ok = r.ok() && !imageType(r.body).isEmpty();
        QString detail;
        if (ok) {
            ++m_stats.fetched;
            m_cache->put(k, r.body);
        } else {
            ++m_stats.failed;
            detail = r.ok() ? QStringLiteral("不是图片（HTTP %1，%2 字节）").arg(r.status).arg(r.body.size())
                            : QStringLiteral("HTTP %1 %2").arg(r.status).arg(r.detail);
        }
        // 找不到（404）原样告诉 maplibre（它不会再要）；其他失败回 502（maplibre 过一会儿会重试）
        const int status = ok ? 200 : (r.status == 404 ? 404 : (r.error == NetClient::Error::Offline || r.error == NetClient::Error::ServiceOff ? 503 : 502));
        for (const Done &d : p.waiters) d(status, ok ? r.body : QByteArray());
        // 1.2.0 地址带 blankTile=false 的（卫星图）：404 = 那一级没有图，不算失败、不进"连续失败"计数；其他底图的 404 照旧算失败
        if (p.forMap && r.error != NetClient::Error::Offline && r.error != NetClient::Error::ServiceOff) emit fetched(k.base, ok || (r.status == 404 && m_sources.value(k.base).upstream.contains(QLatin1String("blankTile=false"))), detail);
    }, QByteArrayLiteral("image/*"));
}

// ---- 预取 ----

int TileServer::prefetch(const QVector<TileKey> &tiles)
{
    if (!online()) return 0;
    int added = 0;
    for (const TileKey &k : tiles) {
        if (added >= kPrefetchMax) break;
        const QString rel = k.path();
        if (!m_sources.contains(k.base) || m_prefetched.contains(rel)) continue;
        m_prefetched.insert(rel);
        if (m_cache->contains(k) || m_pending.contains(rel)) continue;
        m_prefetchQueue.push_back(k);
        ++added;
    }
    if (m_prefetched.size() > 6000) m_prefetched.clear();   // Electron：prefetched.size > 6000 时清空
    pumpPrefetch();
    return added;
}

void TileServer::pumpPrefetch()
{
    while (m_prefetchInFlight < kPrefetchParallel && !m_prefetchQueue.empty()) {
        if (!online()) { m_prefetchQueue.clear(); return; }
        const TileKey k = m_prefetchQueue.front();
        m_prefetchQueue.pop_front();
        if (m_pending.contains(k.path())) continue;
        ++m_prefetchInFlight;
        fetch(k, false, [this](int status, const QByteArray &) {
            --m_prefetchInFlight;
            if (status == 200) ++m_stats.prefetched;
            QMetaObject::invokeMethod(this, &TileServer::pumpPrefetch, Qt::QueuedConnection);
        });
    }
}

} // namespace tp::services
