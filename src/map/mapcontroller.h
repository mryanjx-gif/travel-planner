// 平面地图的控制器（0.8.0，替换阶段 ⓪ 的 ProtoMap）。
//
// - 相机自己管（geo::Camera）：拖动（带惯性）、滚轮缩放、飞行、按范围缩放、动态最小缩放都在这里算，
//   每一帧把中心和缩放交给 QML 的 MapLibre 项；QML 覆盖层用 pixelFor() 定位，和地图画面是同一份数据。
// - 样式：只有一份骨架样式文件（背景 + 三个 Esri 栅格底图，内容固定、只有在线网址），
//   路线、站点、已解锁、昼夜阴影都放在内存里，经 QML 的 SourceParameter / LayerParameter 交给地图；
//   换底图只切图层的显示 / 隐藏，不重新加载样式。
// - 数据：跟着 DataStore::contentChanged 重算（读取、修改、撤销、导入），不再在保存后重读文件。
//
// 为什么这样做见 claude/v0.8.0记录.md §2（maplibre-native-qt QML 项的限制）。
#pragma once

#include "geo.h"
#include "globegeom.h"
#include "globeops.h"
#include "globeroutes.h"
#include "markerlayer.h"
#include "photolayer.h"
#include "night.h"
#include "tripdata.h"
#include "unlock.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>

class QThread;

namespace tp::data { class DataStore; }
namespace tp::app { class Settings; }
namespace tp::services { class TileServer; }

namespace tp::map {

class MapController : public QObject {
    Q_OBJECT
    // 相机
    Q_PROPERTY(double lat READ lat NOTIFY cameraChanged)
    Q_PROPERTY(double lng READ lng NOTIFY cameraChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY cameraChanged)
    Q_PROPERTY(QVariantList coordinate READ coordinate NOTIFY cameraChanged)   // [lat, lng]，给 MapLibre 项
    Q_PROPERTY(double minZoom READ minZoom NOTIFY cameraChanged)
    Q_PROPERTY(double maxZoom READ maxZoom NOTIFY cameraChanged)
    Q_PROPERTY(bool moving READ moving NOTIFY movingChanged)
    // 底图："unlock" 已解锁 / "street" 街道图 / "sat" 卫星图（0.9.0 起没有浅灰，PO 2026-11-06）
    Q_PROPERTY(QString base READ base WRITE setBase NOTIFY baseChanged)
    // 骨架样式（0.8.6 起每种底图一份，只含当前底图的栅格源：隐藏的底图在地图库里会一直留着瓦片缓存，换底图就换样式）
    Q_PROPERTY(QString styleUrl READ styleUrl NOTIFY styleUrlChanged)
    // 交给 maplibre 的 GeoJSON（文字）
    // 路线分三份（0.8.4，Electron routes 的 sel=0 / sel=1 和 routes-hv）：平时（除了选中的计划）、选中的计划、鼠标碰到的那条（高亮层）
    Q_PROPERTY(QString routesJson READ routesJson NOTIFY routesChanged)
    Q_PROPERTY(QString routesSelJson READ routesSelJson NOTIFY routesChanged)
    Q_PROPERTY(QString routesHvJson READ routesHvJson NOTIFY routesHvChanged)
    Q_PROPERTY(QString routesOverlayJson READ routesOverlayJson NOTIFY routesChanged)   // 0.9.7：叠上来的其他旅程（淡灰细线）
    // 线条透明度（0.8.4，Electron LINE_STYLE + DIM + hvAlpha）：平时的线 / 白边（其余变淡时 ×0.3）、高亮层（碰到的程度）
    Q_PROPERTY(double routeOpacity READ routeOpacity NOTIFY lineStyleChanged)
    Q_PROPERTY(double edgeOpacity READ edgeOpacity NOTIFY lineStyleChanged)
    Q_PROPERTY(double hvOpacity READ hvOpacity NOTIFY lineStyleChanged)
    Q_PROPERTY(bool lineHot READ lineHot NOTIFY lineHotChanged)   // 鼠标正在一条线上（手形光标）
    Q_PROPERTY(QString stopsJson READ stopsJson NOTIFY dataChanged)
    Q_PROPERTY(QString visitJson READ visitJson NOTIFY unlockChanged)
    Q_PROPERTY(QString dotsJson READ dotsJson NOTIFY unlockChanged)
    // 省界、国界（0.8.6）：只在"已解锁"底图下才交给地图库，别的底图下是空的（地图库那边的省界数据也就释放了）
    Q_PROPERTY(QString nightJson READ nightJson NOTIFY nightChanged)
    Q_PROPERTY(QString probeJson READ probeJson NOTIFY probeChanged)
    // 开发版左下角的测试信息（已翻译）
    Q_PROPERTY(QString dataInfo READ dataInfo NOTIFY infoChanged)
    Q_PROPERTY(QString unlockInfo READ unlockInfo NOTIFY infoChanged)
    Q_PROPERTY(QString error READ error NOTIFY infoChanged)
    // 站点标记、合并、聚焦、悬停（0.8.2）
    Q_PROPERTY(tp::map::MarkerLayer *markers READ markers CONSTANT)
    Q_PROPERTY(tp::map::PhotoLayer *photoLayer READ photoLayer CONSTANT)   // 0.11.1 照片模式的标记
    // 瓦片（0.8.5）：连续失败 8 次的横幅、在线底图被关掉（离线 / 单关瓦片）
    Q_PROPERTY(bool tileBanner READ tileBanner NOTIFY tileBannerChanged)
    // 0.9.0：在地球上时地球用的底图（"sat" / "unlock"；不在地球上是空）。"连续失败 8 次"按它算（平面是街道图、地球是卫星图时）
    Q_PROPERTY(QString globeBase READ globeBase WRITE setGlobeBase NOTIFY globeBaseChanged)
    Q_PROPERTY(bool tilesOff READ tilesOff NOTIFY tilesOffChanged)
    // 0.9.2：图层里选的是不是"地球"（PO 2026-11-06：图层合并，四颗泡泡 街道图 / 卫星图 / 已解锁 / 地球）。
    // 选地球时平面的底图是卫星图；记进 settings.json 的 globeLayer（截图 / 自检 / 内存测试不记），下次打开回到地球
    Q_PROPERTY(bool globeLayer READ globeLayer WRITE setGlobeLayer NOTIFY globeLayerChanged)
    // "已解锁"足迹框和手动标记（0.8.5）
    Q_PROPERTY(int unlockCountries READ unlockCountries NOTIFY infoChanged)
    Q_PROPERTY(int unlockProvinces READ unlockProvinces NOTIFY infoChanged)
    Q_PROPERTY(bool unlockReady READ unlockReady NOTIFY infoChanged)
    Q_PROPERTY(bool unlockEdit READ unlockEdit WRITE setUnlockEdit NOTIFY unlockEditChanged)
    Q_PROPERTY(QString unlockHoverJson READ unlockHoverJson NOTIFY unlockHoverChanged)
    Q_PROPERTY(QString unlockHoverId READ unlockHoverId NOTIFY unlockHoverChanged)
    Q_PROPERTY(int manualCount READ manualCount NOTIFY unlockChanged)
    // 站点编辑器（0.8.9，M-23）：正在编辑的计划不走平时的路线 / 标记，单独画成"草稿"（白边 + 虚线、标记虚线边、可拖动）
    Q_PROPERTY(QString draftId READ draftId NOTIFY draftChanged)
    Q_PROPERTY(QString draftJson READ draftJson NOTIFY draftChanged)
    Q_PROPERTY(QVariantList draftMarkers READ draftMarkers NOTIFY draftChanged)   // [{ stop（stops 下标）, lat, lng, img, w, h, ax, ay, name, num }]
    // 比例尺（0.8.9，M-30；maplibre-gl ScaleControl：公制，最长 100 px）
    Q_PROPERTY(double scaleWidth READ scaleWidth NOTIFY scaleChanged)
    Q_PROPERTY(QString scaleLabel READ scaleLabel NOTIFY scaleChanged)
    // 0.9.5：现在在地球上（QML 绑定 win.globeMode）。在地球上时标记按地球的相机摆、碰线按地球上的线算、定位等转给地球
    Q_PROPERTY(bool globeOn READ globeOn WRITE setGlobeOn NOTIFY globeOnChanged)
    // 0.9.7：QML 给（图层是地球、在平面上、没在编辑）：平面上滚轮 / － 缩到 kGlobeRollZoom 级以下时不再自己缩放，发 globeRollRequested
    Q_PROPERTY(bool autoGlobe READ autoGlobe WRITE setAutoGlobe NOTIFY autoGlobeChanged)

public:
    explicit MapController(QObject *parent = nullptr);
    ~MapController() override;

    // 启动时调用一次：assetsDir 里有 admin/adm1.geojson、adm0.geojson；cacheDir 放骨架样式
    void setup(const QString &assetsDir, const QString &cacheDir, tp::app::Settings *settings);
    // 在 setup 之前给：这次启动用这个底图，不读设置里上次的（内存测试用，0.8.6）
    void setStartBase(const QString &b) { m_startBase = b; }
    bool globeLayer() const { return m_globeLayer; }
    void setGlobeLayer(bool on);
    // ---- 0.9.5 地球上的内容 ----
    bool globeOn() const { return m_globeOn; }
    void setGlobeOn(bool on);
    void setGlobeOps(GlobeOps *ops) { m_globeOps = ops; }
    bool globeActive() const { return m_globeOn && m_globeOps; }
    // GlobeItem 的镜头变了（拖动、缩放、动画、自转、窗口大小）
    void globeViewChanged();
    // 地球上的路线（三维折线，数据变了才重建；渲染、碰线、小图标共用）
    std::shared_ptr<const globe::Routes> globeRoutes() const;
    // 地球上能拉多近：超过这个级别的定位 / 飞过去换成平面（0.9.7 做了自动摊平以后由它接手）
    static constexpr double kGlobeMaxZoom = 5.5;
    // 0.9.7：自动摊平以后，平面缩到这一级以下卷回地球（和 5.5 之间留半级，免得在边上来回切）
    static constexpr double kGlobeRollZoom = 5.0;
    bool autoGlobe() const { return m_autoGlobe; }
    void setAutoGlobe(bool on);
    // 0.9.7：框住这个计划的镜头 [lat, lng, zoom]（最多 maxZoom 级，让开面板）；没有这个计划 / 没有站点返回空
    Q_INVOKABLE QVariantList fitPlanCamera(const QString &planId, double maxZoom) const;
    // 截图 / 自检：最长一段飞行最长的那个路线计划（没有返回空）
    Q_INVOKABLE QString globeDemoPlan() const;
    // 自检：地球上某个计划某一段的信息 { lifted, mode, angle, maxR（离球心最远的点）, points }
    Q_INVOKABLE QVariantMap globeLegInfo(const QString &planId, int leg) const;
    // 0.9.2：地图库要的瓦片还有几张在路上（换底图时判断新底图画好没有）
    Q_INVOKABLE int tilesBusy() const;
    Q_INVOKABLE double tilesQuietMs() const;   // 离上一次回给地图库一张瓦片过了多少毫秒
    // 本机瓦片服务（0.8.5）：在 setup 之前给；没有时骨架样式直接用 Esri 的地址（单元测试）
    void setTileServer(tp::services::TileServer *server);
    tp::services::TileServer *tileServer() const { return m_tiles; }
    QString globeBase() const { return m_globeBase; }
    void setGlobeBase(const QString &b);
    // 0.9.0：旧设置里的底图名（PO 2026-11-06 去掉了浅灰：gray → street）
    static QString migrateBase(const QString &b);
    // 有改动要保存（0.8.5 手动标记）：main 接到 AppController::noteChanged(false, 延迟)
    void setOnEdited(std::function<void(int delayMs)> f) { m_onEdited = std::move(f); }
    void setStore(tp::data::DataStore *store);
    // 0.8.7 筛选 / 隐藏：返回 false 的计划不画（"已解锁"照样算）。改了以后调 refilter()
    void setPlanFilter(std::function<bool(const json::Value &plan)> f) { m_planFilter = std::move(f); }
    // 0.9.7：叠上来的其他旅程（PlanController::overlayPlan）：画成淡灰细线垫在下面，不能点、没有标记
    void setOverlayFilter(std::function<bool(const json::Value &plan)> f) { m_overlayFilter = std::move(f); }
    QString routesOverlayJson() const { return m_routesOverlayJson; }
    const TripData &overlayTrips() const { return m_overlayTrips; }
    std::shared_ptr<const globe::Routes> globeOverlayRoutes() const;
    void setPlanTone(std::function<int(const json::Value &plan)> f) { m_planTone = std::move(f); }   // 0.11.0 TripPlan::tone
    void refilter() { rebuildData(); }
    // 0.9.2：启动时先攒着（建控制器、读数据、读筛选和模式各要一次重建，PO 电脑上 5 次共 0.7 秒），
    // setDeferRebuild(false) 时只重建一次
    void setDeferRebuild(bool on);
    // 0.9.2：在后台线程先把省界模型建好（PO 电脑上约 0.2 秒），第一次进"已解锁"时就不用等。main 在窗口显示几秒后调用
    void preloadUnlock();
    // 季节表的"去过"（0.11.3，C-07）：复用省界模型，后台算、缓存，数据变了才重算。还在算返回 nullptr，算好发 visitedCodesChanged
    const QSet<QString> *visitedCodes();
    // 换底图时记进设置（截图、自检、内存测试不记）
    void setPersistBase(bool on) { m_persistBase = on; }
    // 翻译（tp::tr）由 main 传进来，库本身不依赖翻译类
    void setTranslate(std::function<QString(const char *)> tr) { m_tr = tr; m_markers.setTranslate(std::move(tr)); refreshInfo(); }
    void retranslate() { refreshInfo(); m_markers.rebuild(m_trips); }   // 提示框里的"未命名计划"等

    double lat() const { return m_cam.lat; }
    double lng() const { return m_cam.lng; }
    double zoom() const { return m_cam.zoom; }
    QVariantList coordinate() const { return { m_cam.lat, m_cam.lng }; }
    double minZoom() const { return m_minZoom; }
    double maxZoom() const;
    bool moving() const { return m_moving; }
    const geo::Camera &camera() const { return m_cam; }

    QString base() const { return m_base; }
    void setBase(const QString &b);

    // ---- 瓦片（0.8.5） ----
    bool tileBanner() const { return m_tileBanner; }
    Q_INVOKABLE void dismissTileBanner();
    Q_INVOKABLE void retryTiles();      // 0.12.8 横幅的"重试"：换一个样式网址，让地图库重新取这一屏的瓦片
    bool tilesOff() const { return m_tilesOff; }
    // 离线总开关或"在线底图"开关变了（Electron tp:prefs → tilesOff() 时 setBase('已解锁')）：关掉时马上切到"已解锁"
    void setTilesOff(bool off);
    int tileErrors() const { return m_tileErr; }     // 自检用：现在连续失败了几次
    // 自检用：模拟一次瓦片结果（自检时先挡住瓦片服务的真信号，免得没联网时真的失败混进来）
    void tileResultForTest(const QString &base, bool ok) { onTileFetched(base, ok, QStringLiteral("自检模拟")); }
    // 地图停下 700 ms 后预取（Electron prefetchTiles）；返回这次排进去几张（自检用）
    Q_INVOKABLE int prefetchNow();

    // ---- "已解锁"足迹框和手动标记（0.8.5，Electron renderUnlockPanel / setManual / toggleRegion / setUnlockHover） ----
    int unlockCountries() const { return m_unlock.countries; }
    int unlockProvinces() const { return m_unlock.provinces; }
    bool unlockReady() const { return m_unlockModel.isLoaded(); }
    Q_INVOKABLE void needUnlockCount() { ensureUnlock(); }   // 0.11.0 足迹的统计要用省界模型（和足迹框同一个数）
    bool unlockEdit() const { return m_unlockEdit; }
    void setUnlockEdit(bool on);
    QString unlockHoverJson() const { return m_unlockHoverJson; }
    QString unlockHoverId() const { return m_unlockHoverId; }
    int manualCount() const { return int(m_trips.manualRegions().size()); }
    // 面板列表：q 为空 = 已经手动标记的；否则按名字搜索。每行 { id, name, sub, byPlan, plans, manual, year }
    Q_INVOKABLE QVariantList unlockRows(const QString &q) const;
    // 设置"所在地国家"的选项（0.12.3，Electron fillCountries）：[{ key: 三字母代码, text: 国家名 }]，按中文排序。省界没载入时是空的
    Q_INVOKABLE QVariantList countryOptions() const;
    // 打勾 / 取消（Electron setManual）；被计划点亮的不能手动取消。返回是否改了
    Q_INVOKABLE bool setManual(const QString &id, bool on);
    // 年份（Electron .u-year onchange）：四位数字才记，否则删掉；返回最后记下的（空 = 没有）
    Q_INVOKABLE QString setManualYear(const QString &id, const QString &year);
    // 点省切换（Electron toggleRegion）：被计划点亮的只提示
    Q_INVOKABLE void toggleRegion(const QString &id);
    // 手动标记时鼠标在地图上：描边那个省、返回提示框（StyledText）；不在陆地上返回空
    Q_INVOKABLE QString unlockEditHover(double x, double y);
    // 手动标记时点地图：切换那个省；返回是否点在省上
    Q_INVOKABLE bool unlockEditClick(double x, double y);
    // 描边某个省（面板列表悬停）；空 = 去掉
    Q_INVOKABLE void setUnlockHover(const QString &id);
    Q_INVOKABLE QString unlockIdAt(double x, double y) const;   // 自检：屏幕上 (x, y) 是哪个省
    // 截图 / 自检：一个手动标记过的省（没有就取一个去过的），{ id, lat, lng }（外框中心）；没有返回空
    Q_INVOKABLE QVariantMap unlockDemo() const;
    Q_INVOKABLE void demoTileBanner() { m_tileBanner = true; emit tileBannerChanged(); }   // 截图 tilefail
    QString styleUrl() const { return m_testStyleUrl.isEmpty() ? m_styleUrl : m_testStyleUrl; }
    // 自检：临时换成这个样式文件（空 = 恢复）
    void setTestStyleUrl(const QString &url) { if (url == m_testStyleUrl) return; m_testStyleUrl = url; emit styleUrlChanged(); }
    // 屏幕缩放比例（0.8.6，QML 绑定 Screen.devicePixelRatio）：卫星图瓦片按它选大小
    Q_INVOKABLE void setDpr(double dpr);
    double dpr() const { return m_dpr; }
    // 卫星瓦片大小（0.8.6）：见 geo::satTileSize
    static int satTileSize(double dpr);
    // 这种底图现在用的瓦片大小（像素）
    int tileSizeOf(const QString &base) const;

    QString routesJson() const { return m_routesJson; }
    QString routesSelJson() const { return m_routesSelJson; }
    QString routesHvJson() const { return m_routesHvJson; }
    // Electron LINE_STYLE.idle：线 .92、白边 .85；hover / sel：都是 1
    static constexpr double kLineIdle = 0.92, kEdgeIdle = 0.85;
    double routeOpacity() const { return kLineIdle * m_markers.lineDimFactor(); }
    double edgeOpacity() const { return kEdgeIdle * m_markers.lineDimFactor(); }
    // Electron hvAlpha：目标是 1，不管下面那层多淡，叠起来都是 1 → 高亮层的透明度就是这条线的高亮程度
    double hvOpacity() const { return m_markers.hvLevel(); }
    bool lineHot() const { return m_lineHot; }
    QString stopsJson() const;   // 0.9.2：现算（QML 没有用到，留给测试）
    QString visitJson() const { return m_visitJson; }
    QString dotsJson() const { return m_dotsJson; }
    QString nightJson() const { return m_nightJson; }
    QString probeJson() const { return m_probeJson; }
    QString dataInfo() const { return m_dataInfo; }
    QString unlockInfo() const { return m_unlockInfo; }
    QString error() const { return m_error; }
    const TripData &trips() const { return m_trips; }
    MarkerLayer *markers() { return &m_markers; }
    PhotoLayer *photoLayer() { return &m_photoLayer; }
    const UnlockModel::Result &unlockResult() const { return m_unlock; }
    bool unlockLoaded() const { return m_unlockModel.isLoaded(); }

    // ---- 站点编辑器（0.8.9） ----
    QString draftId() const { return m_draftId; }
    QString draftJson() const;
    QVariantList draftMarkers() const { return m_draftMarkers; }
    // 开始 / 更新草稿（planId 变了才整份重算，别的只重算草稿这一条，很快）
    void setDraft(const QString &planId, const json::Value &plan);
    void clearDraft();
    // 在"已解锁"里打开编辑器：切回上次的在线底图（M-09，Electron leaveUnlock；离线时留在"已解锁"）。返回是否切了
    Q_INVOKABLE bool leaveUnlock();
    // 框住一个国家（我的位置退回"所在国家"，Electron locateFallback：maxZoom 6、1.4 秒）。code = ISO 3166 三字母。返回是否框住了
    bool fitCountry(const QString &code);
    // 框住一组点（地名搜索的结果，M-25 方案 B）：[[lat, lng], …]，让开面板；maxZoom 默认 12
    Q_INVOKABLE void fitPoints(const QVariantList &pts, double maxZoom = 12, int durationMs = 800);
    // 0.8.9：这些点里有没有落在没被面板挡住的可见区域里的（[[lat, lng], …]）
    Q_INVOKABLE bool anyVisible(const QVariantList &pts) const;
    // 每像素多少米（我的位置的精度圈，0.8.9）
    Q_INVOKABLE double metersPerPixel(double lat) const;
    double scaleWidth() const;
    QString scaleLabel() const;
    // maplibre-gl ScaleControl 的取整（getRoundNum）：1 / 2 / 3 / 5 / 10 × 10^n，小于 1 时按小数取整
    static double scaleRoundNum(double num);
    // 一段 maxWidth 像素在地图中线上的距离（米）→ { 宽度, "500 km" }
    static void scaleFor(const geo::Camera &cam, double maxWidth, double *width, QString *label);

    // ---- 交互（QML 调用） ----
    Q_INVOKABLE void setViewport(double width, double height);
    Q_INVOKABLE void dragStart();
    Q_INVOKABLE void dragBy(double dx, double dy);
    Q_INVOKABLE void dragEnd();
    // 滚轮：angleDeltaY 是 Qt 的 1/8 度（一格 120）；以 (x, y) 为中心缩放，带 0.2 秒缓动
    Q_INVOKABLE void wheel(double angleDeltaY, double x, double y);
    // 立刻缩放 delta 级（以 (x, y) 为中心）
    Q_INVOKABLE void zoomBy(double delta, double x, double y, int durationMs = 250);
    Q_INVOKABLE void jumpTo(double lat, double lng, double zoom);
    // 飞过去（maplibre-gl flyTo 的曲线）；durationMs < 0 = 按距离自动算
    Q_INVOKABLE void flyTo(double lat, double lng, double zoom, int durationMs = -1);
    // 框住所有站点（Electron fitAll：maxZoom 5，左右留 40）
    Q_INVOKABLE void fitAll(bool animate = false);
    // 平移 / 缩放过去（maplibre-gl easeTo，默认缓动）；zoom < 0 = 缩放不变
    Q_INVOKABLE void easeTo(double lat, double lng, double zoom = -1, int durationMs = 500);
    // ---- 0.8.7：浮在地图上的东西挡住的边距（S-05，Electron mapfirst.js insets）----
    // left = "此刻"卡片右边缘（宽窗口时）、right = 简易计划面板占的宽度、top = 顶栏下沿、bottom = 状态胶囊上沿（逻辑像素，0 = 没有）
    Q_INVOKABLE void setInsets(double left, double top, double right, double bottom);
    // Electron flyToPlan：框住这个计划（maxZoom 8，让开面板）
    Q_INVOKABLE bool flyToPlan(const QString &planId);
    // Electron mapfirst start()：启动时框住正在进行或下一次的旅行（maxZoom 5.5，不动画）
    Q_INVOKABLE bool frameTrip(const QString &planId);
    // 平移到某点、缩放不变，放在左右面板之间（Electron panToVisible）
    Q_INVOKABLE void panToVisible(double lat, double lng, int durationMs = 500);
    geo::Padding fitPad() const;   // Electron fitPadding(leftInset(), panelWidth())
    double leftInset() const;
    double panelWidth() const;
    // "已解锁"里的悬停提示框（0.8.2）：(x, y) 处去过的省 / 小单位圆点，没有返回空字符串（StyledText）
    Q_INVOKABLE QString unlockTipAt(double x, double y) const;
    // 鼠标碰到线（0.8.4，Electron lineAt / onMapMove）：(x, y) 处 ±4 px 内的路线（标记上、拖动中、"已解锁"时不算）。
    // 碰到就高亮那个计划（选中的除外）、返回提示框文字（计划名，StyledText）；没碰到返回空字符串、开始"离开 1 秒后恢复"
    Q_INVOKABLE QString lineHover(double x, double y);
    Q_INVOKABLE void lineLeave();                    // 鼠标离开地图
    Q_INVOKABLE bool lineClick(double x, double y);  // 点线 = 选中那个计划（Electron selectPlan(pid, false)）；返回是否点到了
    // (x, y) 处的路线（计划 id），没有返回空。自检 / 单元测试也用
    Q_INVOKABLE QString lineAt(double x, double y) const;
    // 自检：某个计划的线上一个点的屏幕位置（第 seg 段的中点附近），没有线返回 (-1, -1)
    Q_INVOKABLE QPointF linePoint(const QString &planId, int seg = 0) const;
    Q_INVOKABLE void stop();   // 停下正在进行的动画
    Q_INVOKABLE QPointF pixelFor(double lat, double lng) const;
    Q_INVOKABLE QVariantList coordinateFor(double x, double y) const;
    Q_INVOKABLE QVariantList randomStop() const;   // 内存测试
    Q_INVOKABLE void refreshNight();
    // 自检：在 (lat, lng) 放一个探针圆点（maplibre 画），空字符串 = 去掉
    Q_INVOKABLE void setProbe(double lat, double lng, bool on = true);
    // 自检：把太阳固定在 (lat, lng)（看阴影在 180° 经线两边是否都画了）；clearNightSun() 恢复按当前时间
    void setNightSun(double lat, double lng) { m_fixedSun = true; m_sunLat = lat; m_sunLng = lng; refreshNight(); }
    void clearNightSun() { m_fixedSun = false; refreshNight(); }
    // 自检：重新算一次"已解锁"（看颜色是否每次一样）
    void recomputeUnlockForTest() { ensureUnlock(); if (m_unlockModel.isLoaded()) recomputeUnlock(); }

    // 静态内容（单元测试用）
    // tiles：底图 → 瓦片地址模板（0.8.5 本机瓦片服务）；没给的用 Esri 的在线地址
    // base：只放这一种底图的栅格源和图层（"unlock" 时不放）；tileSize 0 = 默认（卫星图 128）
    // adminDir（0.9.2）：省界文件所在的文件夹；"已解锁"的样式里省界是这里的文件地址（空 = 两个空的数据源）
    static QByteArray skeletonStyleJson(const QHash<QString, QString> &tiles = {}, const QString &base = QStringLiteral("street"), int tileSize = 0,
                                        const QString &adminDir = {});
    static QString nightGeoJson(const std::vector<night::Band> &bands);

signals:
    void cameraChanged();
    void movingChanged();
    void baseChanged();
    void dataChanged();
    void unlockChanged();
    void visitedCodesChanged();
    void styleUrlChanged();
    void nightChanged();
    void probeChanged();
    void infoChanged();
    void routesChanged();
    void routesHvChanged();
    void lineStyleChanged();
    void lineHotChanged();
    // 在"已解锁"里选中了计划：切回了在线底图（Electron leaveUnlock，QML 弹提示）
    void leftUnlock(const QString &base);
    void tileBannerChanged();
    void globeBaseChanged();
    void globeLayerChanged();
    void tilesOffChanged();
    void unlockEditChanged();
    void unlockHoverChanged();
    // 地图要 QML 弹的提示（已经翻译好）
    void toast(const QString &text, bool warn);
    void draftChanged();
    void scaleChanged();
    void globeOnChanged();
    // 0.9.5：在地球上要飞到超过 kGlobeMaxZoom 的地方（城市里的行程、我的位置）：QML 换成平面后在平面上飞过去
    void globeExitRequested(double lat, double lng, double zoom, int ms);
    void autoGlobeChanged();
    // 0.9.7：平面上缩到 kGlobeRollZoom 以下（图层是地球）：QML 卷回地球，终点 (lat, lng, zoom)（缩放后鼠标下的点不动）
    void globeRollRequested(double lat, double lng, double zoom);

private:
    enum class Anim { None, Fly, Ease, Inertia, Pan };
    void applyCamera(geo::Camera c);   // 约束 + 发信号
    bool rollToGlobe(double target, double x, double y);   // 0.9.7
    void setMoving(bool m);
    void startAnim(Anim kind, int durationMs);
    void tick();
    void rebuildData();
    void rebuildDraft();
    void ensureUnlock();
    void writeStyle();      // 按当前底图（和卫星瓦片大小）写骨架样式文件、换 styleUrl（0.8.6）
    void recomputeUnlock();
    void refreshInfo();
    void refreshRoutes();
    void refreshHv();
    void setLineHot(bool on);
    geo::Camera fitCam() const;   // 0.9.5：框住时用的相机（在地球上时窗口大小用地球那一项的）
    void fitAllFlat();   // 0.9.6：自动框住只动平面
public:
    static double planMaxZoom(double w, double s, double e, double n);   // 1.2.0 框住一个计划时最多放大到几级（一个点 12，否则 15）
private:
    bool globeGo(double lat, double lng, double zoom, int ms);   // 在地球上时转给地球（zoom 太大就换平面）；不在地球上返回 false
    QString globeLineAt(double x, double y) const;
    bool planBounds(const QString &planId, double *w, double *s, double *e, double *n) const;
    void onTileFetched(const QString &base, bool ok, const QString &detail);
    void setBaseInner(const QString &b, bool persist);
    int manualIndexOf(int unit) const;   // visitedRegions 里和这个单位同一块的那条（没有 -1）
    QString unitSubText(int i) const;
    bool editable();
    // 叫 trd：缺英文检查（tst_app allStringsTranslated）认 qsTr / tp::tr / trd 三种写法
    std::function<bool(const json::Value &plan)> m_planFilter;
    std::function<int(const json::Value &plan)> m_planTone;
    std::function<bool(const json::Value &plan)> m_overlayFilter;   // 0.9.7
    TripData m_overlayTrips;
    QString m_routesOverlayJson;
    mutable std::shared_ptr<const globe::Routes> m_globeOverlayRoutes;
    mutable bool m_globeOverlayDirty = true;
    double m_insL = 0, m_insT = 0, m_insR = 0, m_insB = 0;
    QString trd(const char *s) const { return m_tr ? m_tr(s) : QString::fromUtf8(s); }
    // 省名、国家名（Natural Earth 的中文名）：英文界面时按词典翻译（Electron 的 i18n 对页面文字也查同一本词典），查不到原样显示
    QString placeName(const QString &zh) const { if (!m_tr || zh.isEmpty()) return zh; const QByteArray u = zh.toUtf8(); return m_tr(u.constData()); }

    geo::Camera m_cam;
    double m_minZoom = 0;
    QString m_base = QStringLiteral("street");
    QString m_tileBase = QStringLiteral("street");   // 最近用的在线底图（Electron tileBase）
    QString m_styleUrl, m_assets, m_styleDir, m_testStyleUrl;
    QHash<QString, QString> m_tileTemplates;   // 底图 → 本机瓦片服务的地址模板
    double m_dpr = 1.0;
    tp::app::Settings *m_settings = nullptr;
    QString m_startBase;
    bool m_globeLayer = false;
    bool m_autoGlobe = false;   // 0.9.7
    bool m_globeOn = false;
    GlobeOps *m_globeOps = nullptr;
    mutable std::shared_ptr<const globe::Routes> m_globeRoutes;
    mutable bool m_globeRoutesDirty = true;
    // 0.9.6：碰线用的屏幕折线（逻辑像素），镜头或线变了才重算（鼠标每动一下都要碰线，0.9.5 每次都把所有线投影一遍）
    struct GlobeHitPlan { QString id; int index = 0, dash = 0; std::vector<std::pair<int, std::vector<globe::ScreenRun>>> legs; };
    mutable std::vector<GlobeHitPlan> m_globeHit;
    mutable globe::View m_globeHitView;
    mutable const globe::Routes *m_globeHitRoutes = nullptr;
    const std::vector<GlobeHitPlan> &globeHitRuns() const;
    // 后台建的省界模型（preloadUnlock）：建好前进"已解锁"就等它
    QThread *m_unlockThread = nullptr;
    std::shared_ptr<UnlockModel> m_unlockPre;
    QThread *m_visitedThread = nullptr;
    QString m_visitedKey;
    QSet<QString> m_visited;
    bool m_visitedValid = false;
    tp::data::DataStore *m_store = nullptr;
    std::function<QString(const char *)> m_tr;

    TripData m_trips;
    MarkerLayer m_markers;
    PhotoLayer m_photoLayer;
    UnlockModel m_unlockModel;
    UnlockModel::Result m_unlock;
    bool m_unlockTried = false;
    QString m_routesSelJson, m_routesHvJson;
    bool m_lineHot = false;
    QString m_draftId, m_draftJson;      // 站点编辑器的草稿（0.8.9）
    json::Value m_draftPlan;
    QVariantList m_draftMarkers;
    QString m_routesJson, m_visitJson, m_dotsJson, m_nightJson, m_probeJson;
    QString m_dataInfo, m_unlockInfo, m_error;
    bool m_haveData = false;
    bool m_persistBase = false;
    bool m_viewportKnown = false;
    bool m_fixedSun = false;
    double m_sunLat = 0, m_sunLng = 0;
    bool m_fittedOnce = false;
    QTimer m_rebuildTimer, m_nightTimer, m_animTimer;
    bool m_deferRebuild = false, m_rebuildPending = false;
    // 瓦片（0.8.5）
    tp::services::TileServer *m_tiles = nullptr;
    QString m_globeBase;
    QTimer m_prefetchTimer;
    int m_tileErr = 0;
    bool m_tileBanner = false;
    int m_styleRetry = 0;
    bool m_tilesOff = false;
    // 手动标记（0.8.5）
    std::function<void(int)> m_onEdited;
    bool m_unlockEdit = false;
    QString m_unlockHoverId, m_unlockHoverJson;

    // 动画
    Anim m_anim = Anim::None;
    QElapsedTimer m_animClock;
    double m_animMs = 0;
    std::unique_ptr<geo::FlyPath> m_fly;
    geo::Camera m_from;                 // Ease / Pan：起点
    double m_toLat = 0, m_toLng = 0, m_toZoom = 0;
    double m_easeAnchorX = -1, m_easeAnchorY = -1;   // Ease 缩放时固定不动的点（-1 = 中心）
    double m_wheelTarget = 0;           // 滚轮缓动的目标缩放
    double m_inertiaDx = 0, m_inertiaDy = 0, m_inertiaDone = 0;
    bool m_moving = false;
    // 拖动的速度采样（最近 160 毫秒）
    struct Sample { qint64 t; double dx, dy; };
    QVector<Sample> m_samples;
    QElapsedTimer m_dragClock;
};

} // namespace tp::map
