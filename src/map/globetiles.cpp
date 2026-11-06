#include "globetiles.h"
#include "tileserver.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace tp::map {

namespace {
constexpr quint64 kNone = ~quint64(0);   // 层是空的
constexpr int kMaxReady = 96;            // 等上传的最多这么多张（转得很快时丢掉最早的，要用再取）

// 后台解码 / 缩 mipmap 的计数（退出时等它们做完）
std::atomic_int g_decoding{ 0 };
} // namespace

GlobeTileSet::GlobeTileSet(QObject *parent) : QObject(parent)
{
    m_layerOwner.fill(kNone, kLayers);
    for (int i = kLayers - 1; i >= 0; --i) m_freeLayers.push_back(i);
    connect(&m_raster, &GlobeRaster::rendered, this, [this](int z, int x, int y, int gen, const QImage &img) {
        --m_rasterInFlight;
        const quint64 k = pack(Src::SatFallback, z, x, y);
        if (img.isNull()) { onFailed(k, gen, true); return; }
        ++m_stats.rendered;
        onImage(k, gen, img);
    });
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &GlobeTileSet::changed);
}

GlobeTileSet::~GlobeTileSet()
{
    ++m_gen;
    for (int i = 0; i < 200 && g_decoding.load() > 0; ++i) QThread::msleep(10);
}

qint64 GlobeTileSet::nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

bool GlobeTileSet::drawable(quint64 k) const
{
    const auto it = m_entries.constFind(k);
    return it != m_entries.constEnd() && it->layer >= 0 && (it->state == State::Resident || it->refreshing);
}

void GlobeTileSet::use(quint64 k)
{
    auto it = m_entries.find(k);
    if (it != m_entries.end()) it->lastUsed = m_frame;
}

bool GlobeTileSet::needsRequest(quint64 k, qint64 now) const
{
    const auto it = m_entries.constFind(k);
    if (it == m_entries.constEnd()) return true;
    if (it->retryAt > now) return false;
    return it->state == State::None;
}

void GlobeTileSet::request(quint64 k)
{
    Entry &e = m_entries[k];
    e.lastUsed = m_frame;
    const Src s = srcOf(k);
    const int z = zOf(k), x = xOf(k), y = yOf(k), gen = m_gen;
    if (e.state == State::Resident) e.refreshing = true;   // 层里的旧图先照样画
    else e.state = State::Requested;
    if (s == Src::Sat) {
        if (!m_server) { e.state = State::None; e.retryAt = nowMs() + 60000; return; }
        ++m_netInFlight;
        QPointer<GlobeTileSet> self(this);
        m_server->requestTile({ QStringLiteral("sat"), z, x, y }, [self, k, gen](int status, const QByteArray &body) {
            if (!self) return;
            --self->m_netInFlight;
            if (status != 200 || body.isEmpty()) { self->onFailed(k, gen, status == 404); return; }
            ++self->m_stats.fetched;
            // 解码 + 缩 mipmap 放到后台（JPEG 解码约 2–4 ms，主线程上连着几十张会卡）
            ++g_decoding;
            QThreadPool::globalInstance()->start([self, k, gen, body] {
                QImage img;
                img.loadFromData(body);
                QVector<QImage> mips;
                if (!img.isNull()) {
                    if (img.size() != QSize(256, 256)) img = img.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    mips = makeMips(img.convertToFormat(QImage::Format_RGBA8888));
                }
                QMetaObject::invokeMethod(QCoreApplication::instance(), [self, k, gen, mips] {
                    if (!self) return;
                    if (mips.isEmpty()) { self->onFailed(k, gen, false); return; }
                    if (gen != self->m_gen) return;
                    Entry &e = self->m_entries[k];
                    e.mips = mips;
                    e.state = State::Ready;
                    e.failures = 0;
                    self->m_uploadQueue.push_back(k);
                    emit self->changed();
                }, Qt::QueuedConnection);
                --g_decoding;
            });
        });
    } else {
        ++m_rasterInFlight;
        m_raster.render(z, x, y, gen);
    }
}

QVector<QImage> GlobeTileSet::makeMips(const QImage &img)
{
    QVector<QImage> out;
    out.push_back(img);
    QImage cur = img;
    while (cur.width() > 1) {
        cur = cur.scaled(cur.width() / 2, cur.height() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        out.push_back(cur);
    }
    return out;
}

void GlobeTileSet::onImage(quint64 k, int gen, const QImage &img)
{
    if (gen != m_gen) return;
    Entry &e = m_entries[k];
    e.mips = makeMips(img);
    e.state = State::Ready;
    e.failures = 0;
    m_uploadQueue.push_back(k);
    emit changed();
}

void GlobeTileSet::onFailed(quint64 k, int gen, bool permanent)
{
    ++m_stats.failed;
    if (gen != m_gen) return;
    auto it = m_entries.find(k);
    if (it == m_entries.end()) return;
    it->refreshing = false;
    if (it->state == State::Requested) it->state = State::None;
    ++it->failures;
    // 过一会儿再试：2、4、8 … 秒，最多 60 秒；找不到（404）的 10 分钟后再说
    const qint64 wait = permanent ? 600000 : std::min<qint64>(60000, 1000LL << std::min(it->failures, 6));
    it->retryAt = nowMs() + wait;
    if (!m_retry.isActive() || m_retry.remainingTime() > wait) m_retry.start(int(std::min<qint64>(wait, 60000)));
}

int GlobeTileSet::takeLayer()
{
    if (!m_freeLayers.isEmpty()) return m_freeLayers.takeLast();
    // 换出最久没用到的（这一帧在用的不换）
    int best = -1;
    quint64 bestUsed = m_frame;
    for (int i = 0; i < kLayers; ++i) {
        const auto it = m_entries.constFind(m_layerOwner[i]);
        const quint64 used = it == m_entries.constEnd() ? 0 : it->lastUsed;
        if (used < bestUsed) { bestUsed = used; best = i; }
    }
    if (best < 0) return -1;
    auto it = m_entries.find(m_layerOwner[best]);
    if (it != m_entries.end()) {
        if (it->state == State::Ready) it->layer = -1;   // 正等着刷新的：图还在，以后另找一层
        else m_entries.erase(it);
    }
    m_layerOwner[best] = kNone;
    ++m_stats.evicted;
    return best;
}

void GlobeTileSet::addInstance(quint64 drawKey, int z, int x, int y)
{
    const auto eit = m_entries.constFind(drawKey);
    if (eit == m_entries.constEnd() || eit->layer < 0) return;
    const Entry &e = *eit;
    const int dz = z - zOf(drawKey);
    const double n = std::exp2(z);
    Instance in;
    in.merc[0] = float(x / n); in.merc[1] = float(y / n); in.merc[2] = float((x + 1) / n); in.merc[3] = float((y + 1) / n);
    if (dz <= 0) {
        in.uv[0] = 0; in.uv[1] = 0; in.uv[2] = 1; in.uv[3] = 1;
    } else {
        // 用上一级顶着：取那块里对应的一小格
        const double s = std::exp2(-dz);
        const double ox = (x - (xOf(drawKey) << dz)) * s, oy = (y - (yOf(drawKey) << dz)) * s;
        in.uv[0] = float(ox); in.uv[1] = float(oy); in.uv[2] = float(ox + s); in.uv[3] = float(oy + s);
        ++m_stats.fallbackDrawn;
    }
    in.layer = float(e.layer);
    m_instances.push_back(in);
    ++m_stats.drawn;
}

void GlobeTileSet::placeInstances(bool markUse)
{
    m_instances.clear();
    m_stats.drawn = m_stats.fallbackDrawn = 0;
    const Src rootSrc = Src::SatFallback;
    const auto draw = [&](quint64 a, int z, int x, int y) { if (markUse) use(a); addInstance(a, z, x, y); };
    for (const globe::TileId &t : m_lastSel) {
        const quint64 k = pack(Src::Sat, t.z, t.x, t.y);
        if (drawable(k)) { draw(k, t.z, t.x, t.y); continue; }
        // 还没到：四个子块都在就画子块（缩小时不闪）
        bool kids = true;
        for (int c = 0; c < 4 && kids; ++c) kids = drawable(pack(Src::Sat, t.z + 1, t.x * 2 + (c & 1), t.y * 2 + (c >> 1)));
        if (kids) {
            for (int c = 0; c < 4; ++c) {
                const int cx = t.x * 2 + (c & 1), cy = t.y * 2 + (c >> 1);
                draw(pack(Src::Sat, t.z + 1, cx, cy), t.z + 1, cx, cy);
            }
            continue;
        }
        // 否则用已有的上一级放大顶着；都没有时用垫底的 z1
        bool done = false;
        for (int z = t.z - 1; z >= 0 && !done; --z) {
            const int d = t.z - z;
            const quint64 a = pack(Src::Sat, z, t.x >> d, t.y >> d);
            if (drawable(a)) { draw(a, t.z, t.x, t.y); done = true; }
        }
        if (!done && t.z >= 1) {
            const quint64 a = pack(rootSrc, 1, t.x >> (t.z - 1), t.y >> (t.z - 1));
            if (drawable(a)) draw(a, t.z, t.x, t.y);
        }
    }
}

void GlobeTileSet::update(const globe::View &view)
{
    m_released = false;
    ++m_frame;
    m_lastView = view;
    m_haveView = true;
    globe::SelectOptions opt;
    opt.maxZ = m_maxZ;
    opt.budget = kLayers * 2 / 3;
    const globe::Selection sel = globe::selectTiles(view, opt);
    m_stats.selected = int(sel.tiles.size());
    m_stats.maxZ = sel.maxZ;
    const qint64 now = nowMs();
    // 一直留着的垫底：卫星图 → 本机简图 z1（4 块）；已解锁 → z1（4 块）
    const Src rootSrc = Src::SatFallback;
    struct Miss { double pri; quint64 k; };
    QVector<Miss> missing;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            const quint64 k = pack(rootSrc, 1, x, y);
            use(k);
            if (needsRequest(k, now)) missing.push_back({ -1, k });
        }
    const globe::Camera cam(view);
    for (const globe::TileId &t : sel.tiles) {
        const quint64 k = pack(Src::Sat, t.z, t.x, t.y);
        use(k);
        if (!needsRequest(k, now)) continue;
        // 先要粗的、离屏幕中心近的
        const double n = std::exp2(t.z);
        double sx = view.width / 2, sy = view.height / 2;
        cam.project(globe::toVec(globe::latOfMercY((t.y + 0.5) / n), globe::lngOfMercX((t.x + 0.5) / n)), &sx, &sy);
        missing.push_back({ t.z * 1e5 + std::hypot(sx - view.width / 2, sy - view.height / 2), k });
    }
    m_lastSel = sel.tiles;
    placeInstances(true);
    // 发请求：联网的最多同时 8 个，本机画的最多 3 个
    std::sort(missing.begin(), missing.end(), [](const Miss &a, const Miss &b) { return a.pri < b.pri; });
    for (const Miss &m : std::as_const(missing)) {
        const bool net = srcOf(m.k) == Src::Sat;
        if (net && m_netInFlight >= kMaxNetInFlight) continue;
        if (!net && m_rasterInFlight >= kMaxRasterInFlight) continue;
        request(m.k);   // 离线时：缓存里有的照样给（requestTile 先查缓存），没有的回 503，算失败后过一会儿再试
    }
    // 等上传的太多（转得很快）：丢掉最早的、这一帧用不着的
    while (m_uploadQueue.size() > kMaxReady) {
        const quint64 k = m_uploadQueue.takeFirst();
        auto it = m_entries.find(k);
        if (it == m_entries.end() || it->lastUsed == m_frame) continue;
        it->mips.clear();
        if (it->layer >= 0) it->state = State::Resident, it->refreshing = false;
        else m_entries.erase(it);
    }
}

GlobeTileSet::Frame GlobeTileSet::takeFrame()
{
    Frame f;
    int n = 0;
    bool placed = false;
    while (n < kUploadsPerFrame && !m_uploadQueue.isEmpty()) {
        const quint64 k = m_uploadQueue.first();
        auto it = m_entries.find(k);
        if (it == m_entries.end() || it->state != State::Ready || it->mips.isEmpty()) { m_uploadQueue.removeFirst(); continue; }
        if (it->layer < 0) {
            const int L = takeLayer();
            if (L < 0) break;   // 层都在用：等下一帧
            it = m_entries.find(k);   // takeLayer 可能删过别的项，迭代器要重新取
            if (it == m_entries.end()) { m_freeLayers.push_back(L); m_uploadQueue.removeFirst(); continue; }
            it->layer = L;
            m_layerOwner[L] = k;
            placed = true;
        } else if (!it->refreshing) {
            placed = true;
        }
        m_uploadQueue.removeFirst();
        f.uploads.push_back({ it->layer, std::move(it->mips) });
        it->mips.clear();
        it->state = State::Resident;
        it->refreshing = false;
        ++m_stats.uploaded;
        ++n;
    }
    // 这一帧新放进去的块马上画（不用等下一次 update）：按上次选的重新排一遍要画的（只排，不发请求）
    // 这一帧新放进去的块马上画（不用等下一次 update）：按上次选的重新排一遍要画的。
    // 这里在渲染线程上（主线程被挡住），只动内存里的数据，不联网、不发信号
    if (placed && m_haveView) placeInstances(false);
    f.instances = m_instances;
    if (!m_uploadQueue.isEmpty()) QMetaObject::invokeMethod(this, &GlobeTileSet::changed, Qt::QueuedConnection);   // 还有没传完的：再来一帧
    return f;
}

void GlobeTileSet::gpuLost()
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->layer >= 0 && it->state != State::Ready) { it = m_entries.erase(it); continue; }   // 要重新取（硬盘缓存 / 本机画）
        it->layer = -1;
        it->refreshing = false;
        ++it;
    }
    m_layerOwner.fill(kNone, kLayers);
    m_freeLayers.clear();
    for (int i = kLayers - 1; i >= 0; --i) m_freeLayers.push_back(i);
    m_instances.clear();
    QMetaObject::invokeMethod(this, &GlobeTileSet::changed, Qt::QueuedConnection);
}

void GlobeTileSet::release()
{
    ++m_gen;   // 正在取 / 正在画的结果回来以后不要
    m_entries.clear();
    m_entries.squeeze();
    m_uploadQueue.clear();
    m_instances.clear();
    m_layerOwner.fill(kNone, kLayers);
    m_freeLayers.clear();
    for (int i = kLayers - 1; i >= 0; --i) m_freeLayers.push_back(i);
    m_raster.release();
    m_released = true;
    m_haveView = false;
    m_lastSel.clear();
    m_retry.stop();
}

GlobeTileSet::Stats GlobeTileSet::stats() const
{
    Stats s = m_stats;
    s.resident = 0;
    for (quint64 o : m_layerOwner) if (o != kNone) ++s.resident;
    s.pending = int(m_uploadQueue.size());
    s.netInFlight = m_netInFlight;
    s.rasterInFlight = m_rasterInFlight;
    s.geometry = m_raster.geometryLoaded();
    return s;
}

} // namespace tp::map
