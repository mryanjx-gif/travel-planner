// 地球（阶段 ③）：QQuickRhiItem（Qt Quick，LGPL-3）+ 自己写的着色器。
// 0.9.0：瓦片按视野加载（globetiles.h）、卫星图 / 已解锁两种底图（PO 2026-11-06）、相机（拖动惯性、滚轮缓动、
// 最远 / 最近、和平面缩放级别对得上）。
// 0.9.3（天空和光照）：星空挪到后面单独的 SkyItem（星星闪烁时只重画天空，不重画地球）；这里背景透明。
// 夜晚城市灯光（NASA Black Marble 2016，灰度，进地球时后台读、离开时还显存）；大气光边跟着太阳（PO 选 Q1 B）；
// 空闲自转（C7）。地球上不再有已解锁（0.9.2 起只有卫星图），base 固定是 "sat"。
// 0.9.5（地球上的内容）：路线（第三遍：屏幕空间的线，白边 + 彩线 + 虚线，飞行段抬起成弧线）、交给 MapController 的相机接口
// （定位、飞过去、点标记平移都转到这里）、交通小图标、城市灯光只在缩到最远时显示（往里放大 1 级渐渐淡掉，PO 11:05）、
// 修极点附近缩放抽搐（缩放按相机高度插值，补偿用最小旋转）。
//
// 画两遍：底球（略小一点的球，纯色 + 两极的颜色；瓦片没到的地方露出来）→ 瓦片
// （一块共用的 33×33 网格，按块实例化，顶点着色器把墨卡托坐标换成球面；纹理是 256×256 的纹理数组）。
#pragma once
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickRhiItem>
#include <QTimer>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <QAtomicInt>
#include <QDateTime>
#include <QImage>
#include <QQuickWindow>
#include <rhi/qrhi.h>

#include "globegeom.h"
#include "globeops.h"
#include "globeroutes.h"

namespace tp::map { class GlobeTileSet; class MapController; }

class GlobeItem : public QQuickRhiItem, public tp::map::GlobeOps {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double centerLat READ centerLat WRITE setCenterLat NOTIFY viewChanged)
    Q_PROPERTY(double centerLng READ centerLng WRITE setCenterLng NOTIFY viewChanged)
    Q_PROPERTY(double distance READ distance WRITE setDistance NOTIFY viewChanged)   // 相机到球心距离（球半径 = 1）
    Q_PROPERTY(double zoom READ zoomLevelEquivalent NOTIFY viewChanged)               // 换算成平面地图的缩放级别
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)          // 不显示时不加载瓦片
    Q_PROPERTY(QString base READ base CONSTANT)                                        // 0.9.2 起只有 "sat"
    Q_PROPERTY(QObject *mapController READ mapController WRITE setMapController NOTIFY mapControllerChanged)
    Q_PROPERTY(QString tileStatus READ tileStatus NOTIFY tilesChanged)
    // 离开地球后还要再同步一帧，让渲染那边释放显卡上的纹理（0.8.6）：QML 在这期间保持 visible（透明度是 0，看不见）
    Q_PROPERTY(bool releasePending READ releasePending NOTIFY releasePendingChanged)
    // ---- 0.9.3 ----
    Q_PROPERTY(bool nightLights READ nightLights WRITE setNightLights NOTIFY nightLightsChanged)   // 画不画城市灯光（自检对比用；默认画）
    Q_PROPERTY(bool lightsReady READ lightsReady NOTIFY lightsReadyChanged)                      // 灯光贴图读好、交给显卡了
    // 空闲自转（C7）：QML 给 spinAllowed（动效开着、在地图页、地球、没在编辑、窗口没最小化），没有操作 idleDelay 毫秒后慢慢转起来
    Q_PROPERTY(bool spinAllowed READ spinAllowed WRITE setSpinAllowed NOTIFY spinChanged)
    Q_PROPERTY(bool spinning READ spinning NOTIFY spinChanged)
    Q_PROPERTY(int idleDelay READ idleDelay WRITE setIdleDelay NOTIFY spinChanged)
    // ---- 0.9.5 ----
    Q_PROPERTY(bool moving READ moving NOTIFY movingChanged)                       // 在拖 / 在动画 / 在自转（提示框这时藏起来）
    // 交通小图标（G1）：QML 给 vehicleAllowed（动效开着、在地球上、在地图页）；选中计划时一个图标按站点顺序走一遍
    Q_PROPERTY(bool vehicleAllowed READ vehicleAllowed WRITE setVehicleAllowed NOTIFY vehicleChanged)
    Q_PROPERTY(bool vehicleShown READ vehicleShown NOTIFY vehicleChanged)
    Q_PROPERTY(double vehicleX READ vehicleX NOTIFY vehicleChanged)
    Q_PROPERTY(double vehicleY READ vehicleY NOTIFY vehicleChanged)
    Q_PROPERTY(double vehicleAngle READ vehicleAngle NOTIFY vehicleChanged)        // 飞机：前进方向（度，顺时针，0 = 朝右）
    Q_PROPERTY(bool vehicleFlip READ vehicleFlip NOTIFY vehicleChanged)            // 圆牌：往左走时左右翻
    Q_PROPERTY(QString vehicleImg READ vehicleImg NOTIFY vehicleChanged)           // image://marker/ 后面的描述
    Q_PROPERTY(double vehicleSize READ vehicleSize NOTIFY vehicleChanged)
    // ---- 0.9.7 变形过渡（C1）、自动摊平（B3，PO 选 B：越过 5.5 级播一段摊平动画）----
    Q_PROPERTY(double morph READ morph NOTIFY morphChanged)                        // 0 球、1 平面
    Q_PROPERTY(bool morphing READ morphing NOTIFY morphChanged)                    // 正在播变形动画
    // QML 给（图层是地球、没在编辑）：滚轮 / 双击 / ＋ 放大到 kFlattenZoom 级以上时不再自己缩放，发 flattenRequested 让 QML 摊平
    Q_PROPERTY(bool autoFlatten READ autoFlatten WRITE setAutoFlatten NOTIFY autoFlattenChanged)
public:
    static constexpr double kFlattenZoom = 5.5;   // 和 MapController::kGlobeMaxZoom 一样
    explicit GlobeItem(QQuickItem *parent = nullptr);
    ~GlobeItem() override;

    // ---- GlobeOps（0.9.5，MapController 调用） ----
    tp::map::globe::View globeView() const override { return logicalView(); }
    double globeZoom() const override { return zoomLevelEquivalent(); }
    bool globeMoving() const override { return moving(); }
    void globeEaseTo(double lat, double lng, double zoom, int ms, double toDist = -1) override;
    bool moving() const { return m_anim != Anim::None || m_dragging || m_spinning || m_morphTimer.isActive(); }
    // 城市灯光的显示程度（0.9.5，PO 11:05）：缩到最远是 1，往里放大 1 级渐渐变成 0
    Q_INVOKABLE double lightsFade() const;
    // 自检：上一帧画了几段线、多少个顶点、花了多少毫秒
    Q_INVOKABLE QVariantMap lineStats() const;
    // 0.9.6：从上次 resetLineStats 起画了几帧、平均 / 最慢多少毫秒（自检拖地球时量，一帧的数不稳）
    Q_INVOKABLE void resetLineStats() { m_lineFrames.storeRelease(0); m_lineSumUs.storeRelease(0); m_lineMaxUs.storeRelease(0); }
    void setLineStats(int runs, int verts, double ms)
    {
        m_lineRuns.storeRelease(runs); m_lineVerts.storeRelease(verts); m_lineMs.storeRelease(int(ms * 1000));
        if (runs > 0) {
            m_lineFrames.fetchAndAddRelaxed(1);
            m_lineSumUs.fetchAndAddRelaxed(int(ms * 1000));
            if (int(ms * 1000) > m_lineMaxUs.loadRelaxed()) m_lineMaxUs.storeRelease(int(ms * 1000));
        }
    }
    bool vehicleAllowed() const { return m_vehAllowed; }
    void setVehicleAllowed(bool on);
    bool vehicleShown() const { return m_vehShown; }
    double vehicleX() const { return m_vehX; }
    double vehicleY() const { return m_vehY; }
    double vehicleAngle() const { return m_vehAngle; }
    bool vehicleFlip() const { return m_vehFlip; }
    QString vehicleImg() const { return m_vehImg; }
    double vehicleSize() const { return m_vehSize; }
    // 自检：小图标跳到第 ms 毫秒（不用等动画）；返回 { shown, x, y, mode, leg }
    Q_INVOKABLE QVariantMap vehicleAtMs(double ms);
    // 自检：在 (x, y) 处按滚轮缩放 steps 格（直接走完动画），返回每一步的相机距离
    Q_INVOKABLE QVariantList zoomSteps(double x, double y, int steps, double notch);
    QQuickRhiItemRenderer *createRenderer() override;

    double centerLat() const { return m_lat; }
    double centerLng() const { return m_lng; }
    double distance() const { return m_dist; }
    bool active() const { return m_active; }
    bool releasePending() const { return m_releasePending; }
    QString base() const { return QStringLiteral("sat"); }
    QObject *mapController() const;
    void setCenterLat(double v);
    void setCenterLng(double v);
    void setDistance(double v);
    void setActive(bool v);
    void setMapController(QObject *o);
    QString tileStatus() const;
    bool nightLights() const { return m_nightLights; }
    void setNightLights(bool on);
    bool lightsReady() const { return m_lightsReady; }
    bool spinAllowed() const { return m_spinAllowed; }
    void setSpinAllowed(bool on);
    bool spinning() const { return m_spinning; }
    int idleDelay() const { return m_idleDelay; }
    void setIdleDelay(int ms);
    Q_INVOKABLE void noteActivity();          // 有操作：停止自转、重新计时（窗口里的鼠标 / 键盘自动算）
    Q_INVOKABLE QSize lightsSize() const { return m_lightsSize; }   // 自检：灯光贴图多大
    // 渲染那边要的灯光贴图（主线程在同步时交出去，交出去以后这边不再留着）
    QVector<QImage> takeLights();
    Q_INVOKABLE void setLightsUploaded(bool on);   // 渲染那边（排队调用）：传上显卡了 / 显卡上的丢掉了（离开地球）
    static QString lightsPath();

    Q_INVOKABLE void releaseDone();   // 渲染那边释放完了（内部用）
    // 自检：显卡上纹理数组现在有几层（离开地球后是 1）
    int gpuLayers() const { return m_gpuLayers.loadAcquire(); }
    void setGpuLayers(int n) { m_gpuLayers.storeRelease(n); }

    // ---- 相机（0.9.0） ----
    Q_INVOKABLE void dragStart();
    Q_INVOKABLE void dragBy(double dx, double dy);    // 像素（逻辑）；抓住的地方跟着鼠标走
    Q_INVOKABLE void dragEnd();                       // 松手：照 maplibre-gl 的惯性滑一段
    Q_INVOKABLE void wheel(double angleDeltaY, double x, double y);   // 一格半级，0.2 秒缓动，以鼠标处为中心
    Q_INVOKABLE void rotateBy(double dx, double dy);  // 直接转（内存测试用，没有惯性）
    Q_INVOKABLE void zoomBy(double factor);           // 相机离地面的高度 ÷ factor
    Q_INVOKABLE void stop();
    Q_INVOKABLE double zoomLevelEquivalent() const;   // 屏幕中心比例尺相同的平面缩放级别
    Q_INVOKABLE void setFromZoomLevel(double z);
    Q_INVOKABLE double maxDistance() const;           // 最远（随窗口高度，Electron updateMinZoom 的球形）
    Q_INVOKABLE double sunLng() const;                // 太阳直射点经度（截图时对准晨昏线用）
    Q_INVOKABLE double sunLat() const;
    // 0.9.3：此刻天最黑的大城市 [lat, lng, 名字, 太阳高度°]（截图 globe-night、自检看城市灯光用）
    Q_INVOKABLE QVariantList nightCity() const;
    Q_INVOKABLE QVariantMap tileStats() const;        // 自检 / 状态栏
    Q_INVOKABLE QVariantList pick(double x, double y) const;   // 屏幕点 → [lat, lng]（没打到球是空）
    bool animating() const { return m_anim != Anim::None; }

    // ---- 0.9.7 变形 ----
    double morph() const { return m_morph; }
    bool morphing() const { return m_morphTimer.isActive(); }
    bool autoFlatten() const { return m_autoFlatten; }
    void setAutoFlatten(bool on);
    Q_INVOKABLE void setMorph(double t);
    // QML 用：转过去并缩放到 zoom（换算成平面的级别，< 0 = 距离不变），ms ≤ 0 直接跳；走完发 flyFinished（globeEaseTo 是给 MapController 的接口，QML 调不到）
    Q_INVOKABLE void flyTo(double lat, double lng, double zoom, int ms) { globeEaseTo(lat, lng, zoom, ms); }
    // 变形到 target（0 / 1），ms 毫秒；同时镜头飞到 (lat, lng) 和 zoom（换算成平面的级别，< 0 = 距离不变）。
    // waitTiles：先等这一屏的瓦片到齐（最多 0.25 秒）再开始（卷回地球时地球刚打开，瓦片还在路上）。播完发 morphFinished
    Q_INVOKABLE void morphTo(double target, int ms, double lat, double lng, double zoom, bool waitTiles = false);
    // 变形期间滚的轮：攒起来（不动镜头），平面接手后由 QML 交给平面；取走后清零。返回 [格数 × 120, x, y]
    Q_INVOKABLE QVariantList takeWheel();
    // 这一屏的瓦片到齐了（没有还在加载的、没有用上一级顶着的）
    Q_INVOKABLE bool tilesSettled() const;

    tp::map::GlobeTileSet *tiles() const { return m_tiles; }
    QDateTime now() const { return QDateTime::currentDateTimeUtc(); }
    tp::map::globe::View logicalView() const;
    tp::map::globe::View deviceView() const;

signals:
    void viewChanged();
    void activeChanged();
    void mapControllerChanged();
    void tilesChanged();
    void releasePendingChanged();
    void nightLightsChanged();
    void lightsReadyChanged();
    void spinChanged();
    void movingChanged();
    void vehicleChanged();
    void morphChanged();
    void autoFlattenChanged();
    void flattenRequested(double lat, double lng, double zoom);   // 0.9.7：放大越过 kFlattenZoom（QML 摊平）
    void morphFinished(double morph);                             // 0.9.7：变形播完（1 = 平面，0 = 球）
    void flyFinished();                                           // 0.9.7：globeEaseTo 的动画走完了

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Anim { None, Inertia, Zoom, Fly };
    void scheduleTiles();
    void doTiles();
    void loadLights();
    void spinTick();
    void updateSpinTimer();
    void tick();
    void startAnim(Anim a, int ms);
    void applyZoomDist(double dist, double ax, double ay);   // 以 (ax, ay) 为中心换到某个相机距离（0.9.5：最小旋转补偿）
    void updateMoving();
    void pushView();
    void vehicleTick();
    void morphTick();
    void replanVehicle();
    void updateVehicleTimer();
    double clampDist(double dist) const;

    double m_lat = 30, m_lng = 105, m_dist = 4.2;
    double m_pendingZoom = qQNaN();   // setFromZoomLevel 时还没有大小
    bool m_active = false;
    bool m_releasePending = false;
    QAtomicInt m_gpuLayers = 0;
    tp::map::GlobeTileSet *m_tiles = nullptr;
    QPointer<tp::map::MapController> m_mapc;
    QTimer m_tileTimer;     // 合并：一次事件循环只选一次瓦片

    // 城市灯光（0.9.3）
    bool m_nightLights = true;
    bool m_lightsReady = false;
    bool m_lightsLoading = false;
    int m_lightsGen = 0;            // 离开地球 +1：之前在读的结果不要了
    QVector<QImage> m_lights;       // 读好、还没交给渲染那边的（连 mipmap）
    QSize m_lightsSize;

    // 空闲自转（0.9.3，C7）
    bool m_spinAllowed = false;
    bool m_spinning = false;
    bool m_spinStep = false;        // 自转自己在改中心（不算操作）
    int m_idleDelay = 60000;
    QTimer m_idleTimer;
    QTimer m_spinTimer;
    QElapsedTimer m_spinClock;      // 从开始转算起（头 2 秒加速）
    qint64 m_spinLast = 0;
    QPointer<QQuickWindow> m_filterWin;

    // 动画
    Anim m_anim = Anim::None;
    QTimer m_animTimer;
    QElapsedTimer m_animClock;
    int m_animMs = 0;
    double m_inertiaDx = 0, m_inertiaDy = 0, m_inertiaDone = 0;
    double m_distFrom = 0, m_distTo = 0, m_anchorX = 0, m_anchorY = 0;   // 0.9.5：滚轮缩放按相机距离（高度的对数）插值
    double m_flyLat0 = 0, m_flyLng0 = 0, m_flyDist0 = 0, m_flyLat1 = 0, m_flyLng1 = 0, m_flyDist1 = 0, m_flyBump = 0;
    bool m_dragging = false;
    bool m_lastMoving = false;
    // 0.9.5 交通小图标
    bool m_vehAllowed = false, m_vehShown = false, m_vehFlip = false;
    double m_vehX = 0, m_vehY = 0, m_vehAngle = 0, m_vehSize = 26;
    QString m_vehImg, m_vehPlanId;
    QTimer m_vehTimer;
    QElapsedTimer m_vehClock;
    tp::map::globe::VehiclePlan m_vehPlan;
    double m_vehMsOverride = -1;
    // 自检用的线条统计（渲染线程写）
    QAtomicInt m_lineRuns = 0, m_lineVerts = 0, m_lineMs = 0;
    QAtomicInt m_lineFrames = 0, m_lineSumUs = 0, m_lineMaxUs = 0;
    struct Sample { qint64 t; double dx, dy; };
    QVector<Sample> m_samples;
    QElapsedTimer m_dragClock;
    // 0.9.7 变形
    double m_morph = 0, m_morphFrom = 0, m_morphTarget = 0;
    int m_morphMs = 600;
    bool m_morphWaiting = false;
    bool m_autoFlatten = false;
    QTimer m_morphTimer;
    QElapsedTimer m_morphClock;
    double m_pendWheel = 0, m_pendX = 0, m_pendY = 0;
};
