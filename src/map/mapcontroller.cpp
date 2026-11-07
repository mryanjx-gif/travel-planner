#include "mapcontroller.h"
#include "mapcontroller_p.h"
#include "perf.h"

#include "datastore.h"
#include "settings.h"
#include "tileserver.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QThread>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <cmath>


namespace tp::map {

using detail::compact;
using detail::kUnlockMaxZoom;
[[maybe_unused]] static const QString &kEmptyFc = detail::emptyFc();

namespace {
// Electron BASEMAPS（Esri 的瓦片地址是 /tile/{z}/{y}/{x}）；mapMaxZoom 是地图最多放大到几级。
// 0.9.0：PO 2026-11-06 去掉浅灰（Canvas/World_Light_Gray_Base），旧设置里的 gray 读成 street（migrateBase）
struct Base { const char *key, *url; int tileMaxZoom, tileSize; double mapMaxZoom; };
const Base kBases[] = {
    { "street", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}", 19, 256, 18 },
    { "sat", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}", 18, 128, 18 },
};
constexpr int kStyleVersion = 6;       // 骨架样式改了内容就 +1（换文件名，maplibre 按网址认样式）；v3（0.8.4）加 transition；v4（0.8.5）底图走本机瓦片服务；v5（0.8.6）每种底图一份；v6（0.9.2）省界写进样式
constexpr int kTileFailBanner = 8;     // 连续失败几次出横幅（PO 2026-11-02：连续 8 次，成功一次清零）


bool isBase(const QString &b)
{
    return b == QLatin1String("unlock") || b == QLatin1String("street") || b == QLatin1String("sat");
}
} // namespace

QString MapController::migrateBase(const QString &b)
{
    return b == QLatin1String("gray") ? QStringLiteral("street") : b;
}

void MapController::setGlobeBase(const QString &b)
{
    if (b == m_globeBase) return;
    m_globeBase = b;
    // 换了看的底图：和 setBase 一样从头数失败次数
    m_tileErr = 0;
    if (m_tileBanner) { m_tileBanner = false; emit tileBannerChanged(); }
    emit globeBaseChanged();
}

MapController::MapController(QObject *parent) : QObject(parent)
{
    m_routesSelJson = m_routesHvJson = m_routesOverlayJson = kEmptyFc;
    m_routesJson = m_visitJson = m_dotsJson = m_nightJson = m_probeJson = kEmptyFc;
    m_unlockHoverJson = kEmptyFc;
    // 预取（0.8.5，Electron：moveend 后 700 ms）
    m_prefetchTimer.setSingleShot(true);
    connect(&m_prefetchTimer, &QTimer::timeout, this, [this] { prefetchNow(); });
    connect(this, &MapController::movingChanged, this, [this] {
        if (m_moving) m_prefetchTimer.stop();
        else m_prefetchTimer.start(700);
    });
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(60);   // 连续改几次（撤销连按、导入）只重算一次
    connect(&m_rebuildTimer, &QTimer::timeout, this, &MapController::rebuildData);
    m_nightTimer.setInterval(60000);  // Electron NIGHT.everyMs
    connect(&m_nightTimer, &QTimer::timeout, this, &MapController::refreshNight);
    m_animTimer.setInterval(16);
    m_animTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_animTimer, &QTimer::timeout, this, &MapController::tick);
    // 标记（0.8.2）：和地图同一个相机；点标记平移、点圆圈放大都走这里的动画
    m_markers.setCamera(&m_cam);
    m_photoLayer.setCamera(&m_cam);
    connect(&m_photoLayer, &PhotoLayer::rebuildDataWanted, this, [this] {
        if (!m_store) return;
        m_photoLayer.rebuild(m_store->data());
        if (globeActive()) m_photoLayer.setGlobeView(true, m_globeOps->globeView(), m_globeOps->globeZoom());
    });
    m_photoLayer.setFitBounds([this](double w, double s, double e, double n, double minZoom, int ms) {
        const geo::FitResult r = geo::fitBounds(fitCam(), w, s, e, n, geo::Padding{ 60, 60, 60, 60 }, 30);
        const geo::LatLng c = geo::centerOf({ { s, w }, { n, e } });
        flyTo(r.ok ? r.lat : c.lat, r.ok ? r.lng : c.lng, std::max(r.ok ? r.zoom : 0.0, minZoom), ms);
    });
    // 点标记、← → 切站时平移：放在左右面板之间（0.8.7，Electron panToVisible）
    m_markers.setPan([this](double lat, double lng, double zoom, int ms) {
        if (zoom < 0) panToVisible(lat, lng, ms);
        else easeTo(lat, lng, zoom, ms);
    });
    m_markers.setFitBounds([this](double w, double s, double e, double n, double minZoom, int ms) {
        // Electron：cameraForBounds(padding 60)，缩放至少 minZoom（= 当前级数 + 1），easeTo 0.5 秒
        const geo::FitResult r = geo::fitBounds(fitCam(), w, s, e, n, geo::Padding{ 60, 60, 60, 60 }, 30);
        double lat, lng;
        if (r.ok) { lat = r.lat; lng = r.lng; }
        else { const geo::LatLng c = geo::centerOf({ { s, w }, { n, e } }); lat = c.lat; lng = c.lng; }
        easeTo(lat, lng, std::max(r.ok ? r.zoom : 0.0, minZoom), ms);
    });
    connect(&m_markers, &MarkerLayer::selectionChanged, this, [this] {
        // Electron selectPlan → leaveUnlock：在"已解锁"里点开计划，先切回在线底图
        // 在线底图关着（离线）时留在"已解锁"（Electron 这时会先提示"在线底图已关闭"再提示"已切回"，两句矛盾）
        if (!m_markers.selectedPlan().isEmpty() && m_base == QLatin1String("unlock") && !m_tilesOff) {
            setBase(m_tileBase);
            emit leftUnlock(m_tileBase);
        }
        refreshRoutes();   // 选中的计划挪到"选中"那一份（加粗、不透明、画在最上面）
    });
    // 线条的高亮和变淡（0.8.4）：和标记同一个渐变，标记每变一帧线也跟着变
    connect(&m_markers, &MarkerLayer::hvPlanChanged, this, &MapController::refreshHv);
    connect(&m_markers, &MarkerLayer::highlightChanged, this, &MapController::lineStyleChanged);
    refreshInfo();
}

QByteArray MapController::skeletonStyleJson(const QHash<QString, QString> &tiles, const QString &base, int tileSize, const QString &adminDir)
{
    // 背景 + 当前这一种底图（0.8.6 起；"已解锁"没有栅格底图）。没有本地文件网址：
    // 底图地址是本机瓦片服务（0.8.5，tiles 里给；没给时直接用 Esri 的在线地址）。
    // 为什么每种底图一份（0.8.6，v0.8.6记录.md §2）：maplibre-native 里隐藏的栅格源把瓦片挪进自己的缓存一直留着
    // （tile_pyramid.cpp 的 !needsRendering 分支），三种底图各留一份；换底图时整份样式换掉，旧底图的瓦片就释放了。
    // 栅格源要 maxzoom，而 SourceParameter 不传 maxzoom（技术选型.md §8a），所以底图只能写在样式文件里
    QJsonObject sources;
    QJsonArray layers{ QJsonObject{ { "id", "bg" }, { "type", "background" }, { "paint", QJsonObject{ { "background-color", "#e8e2d6" } } } } };
    // 和背景同色的整块多边形（0.8.2，v2）：放在背景之上、底图之下（见 qml/FlatMap.qml 的 lBgFill）
    {
        QJsonArray ring;
        const double corners[5][2] = { { -180, -85.06 }, { 180, -85.06 }, { 180, 85.06 }, { -180, 85.06 }, { -180, -85.06 } };
        for (const auto &c : corners) ring.append(QJsonArray{ c[0], c[1] });
        QJsonArray rings;
        rings.append(ring);   // 不写成 QJsonArray{ ring }（Windows差异清单 #15）
        sources.insert(QStringLiteral("bg-fill"), QJsonObject{ { "type", "geojson" }, { "data", QJsonObject{
            { "type", "Feature" }, { "properties", QJsonObject{} },
            { "geometry", QJsonObject{ { "type", "Polygon" }, { "coordinates", rings } } } } } });
        layers.append(QJsonObject{ { "id", "bg-fill" }, { "type", "fill" }, { "source", "bg-fill" },
                                   { "paint", QJsonObject{ { "fill-color", "#e8e2d6" }, { "fill-antialias", false } } } });
    }
    // 省界（0.9.2）：写进样式，"已解锁"时是文件地址——地图库自己在后台线程读文件、解析（maplibre-native GeoJSONSource::
    // loadDescription）。以前用 SourceParameter 交 5 MB 的文字，解析在同步阶段做，PO 电脑上进已解锁那一帧卡约 220 ms。
    // 其他底图给空的（图层照样引用这两个数据源，只是没东西画）
    {
        const bool unlock = base == QLatin1String("unlock") && !adminDir.isEmpty();
        const QJsonObject empty{ { "type", "FeatureCollection" }, { "features", QJsonArray{} } };
        for (const char *name : { "adm1", "adm0" }) {
            const QString n = QString::fromLatin1(name);
            const QJsonValue data = unlock ? QJsonValue(QStringLiteral("file://") + QDir::fromNativeSeparators(adminDir) + u'/' + n + QStringLiteral(".geojson"))
                                           : QJsonValue(empty);
            sources.insert(QStringLiteral("u-") + n, QJsonObject{ { "type", "geojson" }, { "data", data } });
        }
    }
    for (const Base &b : kBases) {
        if (base != QLatin1String(b.key)) continue;
        QJsonArray urls;
        urls.append(tiles.value(QLatin1String(b.key), QString::fromLatin1(b.url)));
        sources.insert(QStringLiteral("base-%1").arg(QLatin1String(b.key)),
                       QJsonObject{ { "type", "raster" }, { "tiles", urls }, { "tileSize", tileSize > 0 ? tileSize : b.tileSize },
                                    { "maxzoom", b.tileMaxZoom } });
        layers.append(QJsonObject{ { "id", QStringLiteral("base-%1").arg(QLatin1String(b.key)) }, { "type", "raster" },
                                   { "source", QStringLiteral("base-%1").arg(QLatin1String(b.key)) },
                                   { "paint", QJsonObject{ { "raster-fade-duration", 0 } } } });
    }
    // transition 0（0.8.4）：maplibre 改 paint 属性时默认有 0.3 秒过渡；线条的渐变由我们自己一帧一帧算（和标记同步），
    // 不能再叠一层过渡（否则线会比标记慢半拍）
    return QJsonDocument(QJsonObject{ { "version", 8 }, { "name", "travel-planner" },
                                      { "transition", QJsonObject{ { "duration", 0 }, { "delay", 0 } } },
                                      { "sources", sources }, { "layers", layers } })
        .toJson(QJsonDocument::Compact);
}

void MapController::setup(const QString &assetsDir, const QString &cacheDir, tp::app::Settings *settings)
{
    m_assets = assetsDir;
    m_settings = settings;
    // 骨架样式：写到缓存目录（writeStyle，按当前底图）
    m_styleDir = cacheDir + QStringLiteral("/map");
    QDir().mkpath(m_styleDir);
    m_tileTemplates.clear();
    if (m_tiles && m_tiles->running())
        for (const Base &b : kBases) m_tileTemplates.insert(QLatin1String(b.key), m_tiles->localTemplate(QLatin1String(b.key)));
    // 旧版本的样式文件（v4 及以前，一个文件放三种底图）不再用
    for (const QString &old : QDir(m_styleDir).entryList({ QStringLiteral("style-v*.json") }, QDir::Files))
        if (!old.startsWith(QStringLiteral("style-v%1-").arg(kStyleVersion))) QFile::remove(m_styleDir + QLatin1Char('/') + old);
    // 原型留下的 cache\proto 不再用
    QDir(cacheDir + QStringLiteral("/proto")).removeRecursively();
    if (m_settings) {
        const QString b = migrateBase(m_settings->value(QStringLiteral("baseLayer")).toString());
        if (isBase(b)) m_base = b;
        // 0.9.7（B5）：第一次打开（settings.json 里还没有 globeLayer）是地球；以后记住上次
        if (m_persistBase) {
            const json::Value g = m_settings->value(QStringLiteral("globeLayer"));
            m_globeLayer = g.isNull() ? true : g.toBool();
        }
        if (m_base != QLatin1String("unlock")) m_tileBase = m_base;
    }
    if (isBase(migrateBase(m_startBase))) {
        m_startBase = migrateBase(m_startBase);
        m_base = m_startBase;
        if (m_base != QLatin1String("unlock")) m_tileBase = m_base;
    }
    writeStyle();
    if (m_base == QLatin1String("unlock")) ensureUnlock();
    if (m_base == QLatin1String("sat")) { refreshNight(); m_nightTimer.start(); }
}

int MapController::satTileSize(double dpr)
{
    return geo::satTileSize(dpr);
}

int MapController::tileSizeOf(const QString &base) const
{
    if (base == QLatin1String("sat")) return satTileSize(m_dpr);
    for (const Base &b : kBases)
        if (base == QLatin1String(b.key)) return b.tileSize;
    return 256;
}

void MapController::setDpr(double dpr)
{
    if (!(dpr > 0) || std::abs(dpr - m_dpr) < 1e-6) return;
    const int before = satTileSize(m_dpr);
    m_dpr = dpr;
    // 窗口拖到缩放比例不同的屏幕上：卫星图瓦片大小跟着换（要换样式）
    if (satTileSize(m_dpr) != before && m_base == QLatin1String("sat") && !m_styleDir.isEmpty()) writeStyle();
}

void MapController::writeStyle()
{
    if (m_styleDir.isEmpty()) return;
    const bool raster = m_base != QLatin1String("unlock");
    const int ts = raster ? tileSizeOf(m_base) : 0;
    // 文件名带底图和瓦片大小：maplibre 按网址认样式，内容不同就要换网址
    const QString file = m_styleDir + (m_base == QLatin1String("sat") ? QStringLiteral("/style-v%1-%2-%3.json").arg(kStyleVersion).arg(m_base).arg(ts)
                                                                       : QStringLiteral("/style-v%1-%2.json").arg(kStyleVersion).arg(m_base));
    // 端口每次启动不一样，所以样式文件每次启动都会重写（几百字节）；内容一样就不重写
    const QByteArray want = skeletonStyleJson(m_tileTemplates, m_base, ts, m_assets + QStringLiteral("/admin"));
    QFile f(file);
    bool same = false;
    if (f.open(QIODevice::ReadOnly)) { same = f.readAll() == want; f.close(); }
    if (!same && f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f.write(want); f.close(); }
    // maplibre-native 的本地文件源把 "file://" 后面的部分直接当路径（Windows 要写 file://C:/…，两个斜杠）。
    // 路径里的中文：程序清单里设了 UTF-8 代码页（见 src/app/app.manifest），窄字符读文件也能认（0.8.0 自检核对）
    const QString url = QStringLiteral("file://") + QDir::fromNativeSeparators(file);
    if (url == m_styleUrl) return;
    m_styleUrl = url;
    emit styleUrlChanged();
}

void MapController::setTileServer(tp::services::TileServer *server)
{
    m_tiles = server;
    if (!server) return;
    for (const Base &b : kBases)
        server->addSource({ QLatin1String(b.key), QString::fromLatin1(b.url), b.tileMaxZoom });
    connect(server, &tp::services::TileServer::fetched, this, &MapController::onTileFetched);
}

void MapController::onTileFetched(const QString &base, bool ok, const QString &detail)
{
    // Electron map.on('error')：只算当前底图的；PO 2026-11-02 定为"连续"8 次（成功一次清零）。
    // 0.9.0：在地球上时算地球的底图（平面是街道图、地球显示卫星图）
    if (base != (m_globeBase.isEmpty() ? m_base : m_globeBase)) return;
    if (ok) {
        m_tileErr = 0;
        if (m_tileBanner) { m_tileBanner = false; emit tileBannerChanged(); }   // 又能取到了：横幅自己收起
        return;
    }
    if (++m_tileErr == kTileFailBanner) {
        qWarning("底图「%s」连续 %d 张瓦片没取到（最后一次：%s），提示切到已解锁", qUtf8Printable(base), kTileFailBanner, qUtf8Printable(detail));
        m_tileBanner = true;
        emit tileBannerChanged();
    }
}

void MapController::dismissTileBanner()
{
    if (!m_tileBanner) return;
    m_tileBanner = false;
    emit tileBannerChanged();
}

void MapController::setTilesOff(bool off)
{
    if (off == m_tilesOff) return;
    m_tilesOff = off;
    qInfo("在线底图：%s", off ? "关了（离线或单独关掉），只用已解锁" : "可以用了");
    emit tilesOffChanged();
    // Electron：tilesOff() 时 setBase(UNLOCK)。这里不记进设置——以后联网了再打开，还是用户原来选的底图
    if (off && m_base != QLatin1String("unlock")) setBaseInner(QStringLiteral("unlock"), false);
}

int MapController::prefetchNow()
{
    if (!m_tiles || m_tilesOff || m_moving || m_base == QLatin1String("unlock") || m_cam.width <= 0) return 0;
    const Base *b = nullptr;
    for (const Base &x : kBases)
        if (m_base == QLatin1String(x.key)) b = &x;
    if (!b) return 0;
    const geo::LatLng nw = m_cam.coordinateFor(0, 0), se = m_cam.coordinateFor(m_cam.width, m_cam.height);
    QVector<tp::services::TileKey> keys;
    for (const geo::TileXY &t : geo::prefetchTiles(nw.lng, nw.lat, se.lng, se.lat, m_cam.zoom, tileSizeOf(m_base), b->tileMaxZoom))
        keys.push_back({ m_base, t.z, t.x, t.y });
    return m_tiles->prefetch(keys);
}

void MapController::setStore(tp::data::DataStore *store)
{
    m_store = store;
    connect(store, &tp::data::DataStore::contentChanged, this, [this] { m_rebuildTimer.start(); });
    rebuildData();
}

double MapController::maxZoom() const
{
    if (m_base == QLatin1String("unlock")) return kUnlockMaxZoom;
    for (const Base &b : kBases)
        if (m_base == QLatin1String(b.key)) return b.mapMaxZoom;
    return 18;
}

void MapController::setBase(const QString &b00)
{
    const QString b0 = migrateBase(b00);
    QString b = b0;
    if (!isBase(b)) return;
    // Electron setBase：在线底图关了（或离线模式）时只能用"已解锁"
    if (b != QLatin1String("unlock") && m_tilesOff) {
        emit toast(trd("在线底图已关闭（设置 → 联网服务）"), true);
        b = QStringLiteral("unlock");
    }
    setBaseInner(b, m_persistBase && b == b0);   // 被迫切到"已解锁"的不记
}

void MapController::setBaseInner(const QString &b, bool persist)
{
    if (b == m_base) return;
    tp::app::perf::Scope timing("换底图（C++）", 0);   // 0.9.1：不含地图库换样式、画新底图（那些在渲染线程 / 后台）
    m_base = b;
    // Electron setBase：tileErrCount = 0; banner.hidden = true
    m_tileErr = 0;
    if (m_tileBanner) { m_tileBanner = false; emit tileBannerChanged(); }
    if (m_base != QLatin1String("unlock")) setUnlockEdit(false);
    if (m_base != QLatin1String("unlock")) m_tileBase = m_base;
    writeStyle();   // 换样式（0.8.6）：旧底图的瓦片随旧样式释放
    timing.part("样式文件");
    if (m_base == QLatin1String("unlock")) ensureUnlock();
    timing.part("省界模型");
    if (m_base == QLatin1String("sat")) { refreshNight(); m_nightTimer.start(); }
    else m_nightTimer.stop();
    if (m_settings && persist) {
        m_settings->setValue(QStringLiteral("baseLayer"), m_base);
        m_settings->save();
    }
    timing.part("昼夜 + 写设置");
    emit baseChanged();
    timing.part("界面跟着变");
    applyCamera(m_cam);   // 最大缩放跟着底图变（已解锁最多 9 级）
    m_prefetchTimer.start(700);
}

// ---- 数据 ----

void MapController::setDeferRebuild(bool on)
{
    if (on == m_deferRebuild) return;
    m_deferRebuild = on;
    if (!on && m_rebuildPending) {
        m_rebuildPending = false;
        m_rebuildTimer.stop();   // 读数据时排的那一次也算在这一次里
        rebuildData();
    }
}

void MapController::rebuildData()
{
    if (!m_store) return;
    if (m_deferRebuild) { m_rebuildPending = true; m_rebuildTimer.stop(); return; }   // 0.9.2：启动时攒着，最后只重建一次
    // 0.9.1 计时；0.9.2：行程按计划缓存（只重算改了的），标记不再整份重建 QML 元素，"已解锁"的输入没变就不重新上色
    tp::app::perf::Scope timing("数据重建", 0);
    if (m_draftId.isEmpty()) m_trips.build(m_store->data(), m_planFilter, m_planTone);
    else
        // 编辑中的计划由草稿画（Electron renderMap：编辑中的由 renderDraft 画）
        m_trips.build(m_store->data(), [this](const json::Value &plan) {
            return plan[QStringLiteral("id")].toString() != m_draftId && (!m_planFilter || m_planFilter(plan));
        }, m_planTone);
    timing.part(QStringLiteral("行程（重算 %1 个）").arg(m_trips.recomputed()).toUtf8().constData());
    // 0.9.7 叠上来的其他旅程（编辑中的那个不算）：只要线
    if (m_overlayFilter) {
        m_overlayTrips.shareCache(m_trips);
        m_overlayTrips.build(m_store->data(), [this](const json::Value &plan) {
            return (m_draftId.isEmpty() || plan[QStringLiteral("id")].toString() != m_draftId) && m_overlayFilter(plan);
        });
    } else {
        m_overlayTrips.build(m_store->data(), [](const json::Value &) { return false; });
    }
    m_routesOverlayJson = compact(m_overlayTrips.routesGeoJson());
    m_globeOverlayDirty = true;
    timing.part("叠上来的旅程");
    m_haveData = m_trips.stopCount() > 0;
    m_globeRoutesDirty = true;   // 0.9.5：地球上的线用到时再重建
    m_markers.rebuild(m_trips);   // 选中的计划没了会取消选中；悬停清掉
    m_photoLayer.rebuild(m_store->data());
    timing.part("标记");
    refreshRoutes();
    timing.part("路线");
    refreshHv();
    timing.part("高亮层");
    emit dataChanged();
    timing.part("界面跟着变");
    if (m_unlockModel.isLoaded() && m_trips.unlockInputsChanged()) recomputeUnlock();
    timing.part("已解锁上色");
    refreshInfo();
    if (!m_fittedOnce && m_haveData && m_viewportKnown) { m_fittedOnce = true; fitAllFlat(); }
}

void MapController::setGlobeLayer(bool on)
{
    if (on == m_globeLayer) return;
    m_globeLayer = on;
    if (m_settings && m_persistBase) {
        m_settings->setValue(QStringLiteral("globeLayer"), on);
        m_settings->save();
    }
    emit globeLayerChanged();
}

int MapController::tilesBusy() const { return m_tiles ? m_tiles->busyForMap() : 0; }
double MapController::tilesQuietMs() const { return m_tiles ? double(m_tiles->msSinceLastResponse()) : 1e9; }

QString MapController::stopsJson() const { return compact(m_trips.stopsGeoJson()); }

MapController::~MapController()
{
    if (m_visitedThread) { m_visitedThread->wait(); delete m_visitedThread; }
    if (m_unlockThread) { m_unlockThread->wait(); delete m_unlockThread; }
}

const QSet<QString> *MapController::visitedCodes()
{
    // 只有已完成计划的站点和手动标记变了才重算
    const QVector<UnlockModel::PlanStops> completed = m_trips.completedStops();
    const QVector<UnlockModel::Manual> manual = m_trips.manualRegions();
    QString key;
    for (const UnlockModel::PlanStops &p : completed)
        for (const geo::LatLng &s : p.stops) key += QStringLiteral("%1,%2|").arg(s.lat, 0, 'f', 6).arg(s.lng, 0, 'f', 6);
    for (const UnlockModel::Manual &m : manual) key += m.id + QLatin1Char('|');
    if (m_visitedValid && key == m_visitedKey) return &m_visited;
    if (m_visitedThread) return nullptr;   // 算完回来再对一次 key
    // 省界模型还在后台建：建好了再来（没人建过就现在开始建）
    if (!m_unlockModel.isLoaded() && !m_unlockThread && !m_unlockTried) preloadUnlock();
    if (m_unlockThread && !m_unlockThread->isFinished()) {
        connect(m_unlockThread, &QThread::finished, this, &MapController::visitedCodesChanged, Qt::UniqueConnection);
        return nullptr;
    }
    const std::shared_ptr<UnlockModel> model = m_unlockModel.isLoaded() ? std::make_shared<UnlockModel>(m_unlockModel) : m_unlockPre;
    if (!model || !model->isLoaded()) {   // 省界读不出来：都算没去过（Electron 同样），错误在"已解锁"那边报
        m_visited.clear();
        m_visitedKey = key;
        m_visitedValid = true;
        return &m_visited;
    }
    auto result = std::make_shared<QSet<QString>>();
    m_visitedThread = QThread::create([model, completed, manual, result] { *result = model->visitedCodes(completed, manual); });
    connect(m_visitedThread, &QThread::finished, this, [this, key, result] {
        m_visitedThread->wait();
        delete m_visitedThread;
        m_visitedThread = nullptr;
        m_visited = *result;
        m_visitedKey = key;
        m_visitedValid = true;
        emit visitedCodesChanged();
    });
    m_visitedThread->start(QThread::LowPriority);
    return nullptr;
}

void MapController::preloadUnlock()
{
    if (m_unlockTried || m_unlockThread || m_assets.isEmpty()) return;
    auto model = std::make_shared<UnlockModel>();
    const QString file = m_assets + QStringLiteral("/admin/adm1.geojson");
    m_unlockThread = QThread::create([model, file] {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly)) model->load(f.readAll());
    });
    m_unlockPre = model;
    m_unlockThread->start(QThread::LowPriority);
}

void MapController::ensureUnlock()
{
    if (m_unlockTried) return;
    m_unlockTried = true;
    if (m_unlockThread) {
        // 后台在建（或已建好）：等它，用它的结果（建坏了就照旧在这里再读一次，报错照旧）
        tp::app::perf::Scope timing("等后台的省界模型", 0);
        m_unlockThread->wait();
        delete m_unlockThread;
        m_unlockThread = nullptr;
        const std::shared_ptr<UnlockModel> pre = std::move(m_unlockPre);
        if (pre && pre->isLoaded() && QFile::exists(m_assets + QStringLiteral("/admin/adm0.geojson"))) {
            m_unlockModel = *pre;
            recomputeUnlock();
            return;
        }
    }
    const auto readAll = [](const QString &p) {
        QFile f(p);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    const QByteArray adm1 = readAll(m_assets + QStringLiteral("/admin/adm1.geojson"));
    const QByteArray adm0 = readAll(m_assets + QStringLiteral("/admin/adm0.geojson"));
    QString err;
    if (adm1.isEmpty() || adm0.isEmpty() || !m_unlockModel.load(adm1, &err)) {
        m_error = trd("省界数据没有加载成功");
        qWarning("省界数据没有加载成功（%s）：%s", qUtf8Printable(QDir::toNativeSeparators(m_assets)), qUtf8Printable(err));
        refreshInfo();
        return;
    }
    // 省界原文不留着（0.8.6）；0.9.2 起地图库按样式里的文件地址自己读，离开"已解锁"（换样式）就收回
    recomputeUnlock();
}

void MapController::recomputeUnlock()
{
    m_unlock = m_unlockModel.compute(m_trips.completedStops(), m_trips.manualRegions());
    m_visitJson = compact(m_unlock.visit);
    m_dotsJson = compact(m_unlock.dots);
    emit unlockChanged();
    refreshInfo();
}

QString MapController::unlockTipAt(double x, double y) const
{
    if (m_base != QLatin1String("unlock") || !m_unlockModel.isLoaded()) return {};
    const auto tipOf = [this](const QJsonObject &p) {
        QString plans = p.value(QLatin1String("plans")).toString();
        if (plans.startsWith(QStringLiteral("手动标记"))) plans = trd("手动标记") + plans.mid(4);
        return QStringLiteral("<b>%1</b> · %2<br>%3").arg(placeName(p.value(QLatin1String("name")).toString()).toHtmlEscaped(),
                                                       placeName(p.value(QLatin1String("country")).toString()).toHtmlEscaped(),
                                                       plans.toHtmlEscaped());
    };
    // 小单位的圆点（半径 5 + 白边 1.5，6 级以下才显示）：Electron map.on('mousemove', 'u-dot')
    if (m_cam.zoom < 6) {
        for (const QJsonValue &fv : m_unlock.dots.value(QLatin1String("features")).toArray()) {
            const QJsonArray c = fv.toObject().value(QLatin1String("geometry")).toObject().value(QLatin1String("coordinates")).toArray();
            const geo::Pt p = m_cam.pixelFor(c.at(1).toDouble(), c.at(0).toDouble());
            if (std::hypot(p.x - x, p.y - y) <= 6.5) return tipOf(fv.toObject().value(QLatin1String("properties")).toObject());
        }
    }
    // 去过的省：鼠标所在的多边形（Electron map.on('mousemove', 'u-visit')）
    const geo::LatLng ll = m_cam.coordinateFor(x, y);
    const QString id = m_unlockModel.unitId(m_unlockModel.unitAt(geo::normLng(ll.lng), ll.lat));
    const auto it = m_unlock.propsById.constFind(id);
    return it == m_unlock.propsById.constEnd() ? QString() : tipOf(*it);
}

QString MapController::nightGeoJson(const std::vector<night::Band> &bands) { return night::toGeoJson(bands); }

void MapController::refreshNight()
{
    const geo::LatLng sun = m_fixedSun ? geo::LatLng{ m_sunLat, m_sunLng } : night::subsolarPoint(double(QDateTime::currentMSecsSinceEpoch()));
    m_nightJson = night::toGeoJson(night::stripes(sun));   // 0.8.7：互不重叠的细带（原来 13 层叠起来有台阶）
    emit nightChanged();
}

void MapController::refreshInfo()
{
    if (!m_store || m_trips.plans().isEmpty() && !m_haveData)
        m_dataInfo = trd("还没有数据");
    else
        m_dataInfo = trd("%1 个计划 · %2 条路线 · %3 段（其中道路 %4 段）· %5 个站点")
                         .arg(m_trips.plans().size()).arg(m_trips.routeCount()).arg(m_trips.legCount())
                         .arg(m_trips.roadLegCount()).arg(m_trips.stopCount());
    m_unlockInfo = m_unlockModel.isLoaded() ? trd("已解锁 %1 个国家 / 地区 · %2 个省 / 州").arg(m_unlock.countries).arg(m_unlock.provinces)
                                            : QString();
    emit infoChanged();
}


} // namespace tp::map
