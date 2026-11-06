// 地图上的站点标记、合并圆圈、聚焦和悬停（0.8.2）。QML 里是 mapc.markers，画在 qml/components/MapMarkers.qml。
//
// 照搬 Electron v6.3.0 map-app.js 的 renderMap / applyClusters / spreadMarkers / applyFocusMarker / hoverPlan / setFocusStop：
// - 每个站点一个标记（样式见 markers::stopStyle），位置每次相机变化时一次算完（和 MapController::pixelFor 同一套投影）；
// - 合并：缩放跨层时重新分组（选中的计划不参与），被合并的标记藏起来，圆圈另放一个模型；
// - 错开：≥14 级、停下来（moveend / zoomend）时重新排；
// - 悬停：碰到某个计划的标记或线，这个计划 0.2 秒渐亮、其余变淡，离开 1 秒后 0.35 秒渐出；
//   0.8.4 起线条也在这里：其余路线的线和白边 ×0.3、碰到的那条画在高亮层（加粗），和标记用同一个渐变；
// - 选中（0.8.6 的简易计划面板之前的过渡做法）：点标记选中计划、聚焦这一站、平移过去；Esc 取消；← → 切站。
#pragma once

#include "geo.h"
#include "globegeom.h"
#include "markers.h"
#include "tripdata.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <functional>

namespace tp::map {

class MarkerLayer;

// 标记 / 圆圈的列表（QML Repeater 用）。数据都在 MarkerLayer 里，这里只是转一手
class MarkerModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { X = Qt::UserRole + 1, Y, Img, W, H, Ax, Ay, Op, Z, Shown, Focus, Glow, Tip, TipY, PlanId, StopIdx, Sc, Hit };
    MarkerModel(MarkerLayer *layer, bool clusters, QObject *parent);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void begin() { beginResetModel(); }
    void end() { endResetModel(); }
    // 0.9.2：行数从 oldN 变成 newN，不整份重置（QML 的 Repeater 整份重置时要把几百个标记元素全部删掉重建，
    // PO 电脑上约 60 ms）：多出来的行插在末尾、少了的从末尾删掉，swap 在插入 / 删除的中间把数据换成新的；
    // 原来就有的行发 dataChanged（所有角色），元素留着只更新内容
    void replaceRows(int oldN, int newN, const std::function<void()> &swap);
    void changed(const QList<int> &roles);   // 所有行的这些角色变了
    void changedRow(int row, const QList<int> &roles);

private:
    MarkerLayer *m_layer;
    bool m_clusters;
};

class MarkerLayer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *stops READ stopsModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *clusters READ clustersModel CONSTANT)
    Q_PROPERTY(QString selectedPlan READ selectedPlan NOTIFY selectionChanged)
    Q_PROPERTY(int focusStop READ focusStop NOTIFY selectionChanged)
    Q_PROPERTY(bool canStep READ canStep NOTIFY selectionChanged)            // 选中的是路线（← → 能切站）
    Q_PROPERTY(double dpr READ dpr WRITE setDpr NOTIFY dprChanged)           // 屏幕倍率（标记图片按它画）
    Q_PROPERTY(QString spreadJson READ spreadJson NOTIFY spreadChanged)      // 错开时真实位置的小实心点（给 maplibre）
    Q_PROPERTY(int clusterTier READ clusterTier NOTIFY clustersChanged)
    // 自检用：0.8.0 的投影检查找 maplibre 画的探针，标记盖在上面会挡住（只藏 QML 覆盖层，不影响数据）
    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY shownChanged)
    // 线条的高亮和变淡（0.8.4，Electron syncDim / hvShow）：
    //   dimLevel 0..1 = "其余变淡"的程度（有悬停 / 选中时渐变到 1：进 200 ms、出 350 ms），标记和线共用
    //   hvPlan = 画在高亮层上的计划（鼠标碰到的那条路线；渐出完才清掉），hvLevel = 它的高亮程度（和它的标记同一个 h）
    Q_PROPERTY(double dimLevel READ dimLevel NOTIFY highlightChanged)
    Q_PROPERTY(QString hvPlan READ hvPlan NOTIFY hvPlanChanged)
    Q_PROPERTY(double hvLevel READ hvLevel NOTIFY highlightChanged)
    // "动效"设置：关掉时高亮 / 变淡直接到位（没有渐变）
    Q_PROPERTY(bool motion READ motion WRITE setMotion NOTIFY motionChanged)
    // 0.9.5：鼠标停在标记 / 圆圈上（地球的空闲自转这时不开始）
    Q_PROPERTY(bool pointerOn READ overMarker NOTIFY pointerChanged)

public:
    struct Stop {
        int plan = 0, stop = 0;           // TripData::plans() 的下标、站点下标
        QString planId;
        geo::LatLng base;                 // 真实位置（经度归一）
        markers::Style style;
        markers::Geom geom;
        QString img;
        bool route = false, wish = false, clusterable = true, clustered = false;
        double ox = 0, oy = 0;            // 错开时相对真实位置的偏移（像素，在 m_spreadZoom 时算的）
        double x = 0, y = 0;              // 屏幕位置（锚点，含偏移）
        double op = 1;
        double limb = 1, sc = 1;          // 0.9.5 地球上：背面渐隐（C4）、边缘变小（G3）；平面上都是 1
        QString tip;
    };
    struct Cluster {
        QVector<int> members;             // m_stops 下标
        geo::LatLng center;
        int size = 26;
        QString img, tip;
        bool allRoute = true;
        double x = 0, y = 0, op = 1;
        double limb = 1, sc = 1;          // 0.9.5 地球上（同 Stop）
    };

    explicit MarkerLayer(QObject *parent = nullptr);
    // 由 MapController 调用
    void setCamera(const geo::Camera *cam) { m_cam = cam; }
    // 0.9.5：地球上用地球的相机摆（on = false 回到平面的相机）。zoomEq = 换算成平面的缩放级别（合并按它分层）
    void setGlobeView(bool on, const globe::View &v, double zoomEq, bool moving);
    bool onGlobe() const { return m_globe; }
    double currentZoom() const { return m_globe ? m_gzoom : (m_cam ? m_cam->zoom : 0.0); }
    void setPan(std::function<void(double, double, double, int)> easeTo) { m_easeTo = std::move(easeTo); }
    void setFitBounds(std::function<void(double, double, double, double, double, int)> fit) { m_fit = std::move(fit); }
    void setTranslate(std::function<QString(const char *)> tr) { m_tr = std::move(tr); }
    void rebuild(const TripData &trips);
    void cameraChanged(bool moving);
    void movingChanged(bool moving);

    QAbstractItemModel *stopsModel() { return &m_stopsModel; }
    QAbstractItemModel *clustersModel() { return &m_clustersModel; }
    QString selectedPlan() const { return m_selected; }
    int focusStop() const { return m_focus; }
    bool canStep() const;
    double dpr() const { return m_dpr; }
    void setDpr(double d);
    QString spreadJson() const { return m_spreadJson; }
    int clusterTier() const { return m_tier; }
    bool shown() const { return m_shown; }
    void setShown(bool on) { if (on != m_shown) { m_shown = on; emit shownChanged(); } }

    const QVector<Stop> &stops() const { return m_stops; }
    const QVector<Cluster> &clusters() const { return m_clusters; }
    QString hoverPlan() const { return m_hoverId; }
    double dimLevel() const { return m_dimT; }
    QString hvPlan() const { return m_hvId; }
    double hvLevel() const;
    bool motion() const { return m_motion; }
    void setMotion(bool on);
    bool overMarker() const { return m_hoverRow >= 0 || m_hoverCluster >= 0; }   // 鼠标在标记 / 圆圈上（线不再响应）
    // 线条（0.8.4，由 MapController 的命中检测调用）：碰到某个计划的线 / 离开线 / 点线
    void hoverLine(const QString &planId);
    void unhoverLine();
    void clickLine(const QString &planId);
    // 平时线条的透明度系数（Electron underOf：DIM.line = .3）
    double lineDimFactor() const { return 1 - (1 - kLineDim) * m_dimT; }
    double markerDimFactor() const { return 1 - (1 - kDim) * m_dimT; }
    // 站点编辑器开着时其余计划变淡（0.8.9，Electron renderMap 的 editor.open → 变淡）
    void setForceDim(bool on) { if (on == m_forceDim) return; m_forceDim = on; startAnim(); }
    bool forceDim() const { return m_forceDim; }

    // ---- QML ----
    Q_INVOKABLE void hoverStop(int row);
    Q_INVOKABLE void unhoverStop(int row);
    Q_INVOKABLE void clickStop(int row);
    Q_INVOKABLE void hoverCluster(int row);
    Q_INVOKABLE void unhoverCluster(int row);
    Q_INVOKABLE void clickCluster(int row);
    // 选中一个计划（stopIdx ≥ 0 时聚焦这一站并平移过去）
    Q_INVOKABLE void selectPlan(const QString &planId, int stopIdx = -1, bool pan = true);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void stepStop(int d);
    // 自检 / 截图：第 i 个站点标记（按计划 id + 站点序号找）在 m_stops 里的下标，没有 -1
    Q_INVOKABLE int indexOf(const QString &planId, int stopIdx) const;
    Q_INVOKABLE QVariantMap stopInfo(int row) const;
    // 截图用（markers / spread / pins 页）：找一个合适的地方。kind = "route"（≥4 站的路线的第 3 站）、
    // "spread"（20 m 内重叠的站点）、"pin"（"想去"的单一目的地）、"line"（站点最多的路线的中间一站，0.8.4）。
    // 返回 { planId, stop, lat, lng }，找不到返回空
    Q_INVOKABLE QVariantMap demo(const QString &kind) const;

    // 透明度（Electron routeMarkOpacity / singleMarkOpacity / .cluster-icon.dim）
    static constexpr double kRouteIdle = 0.65, kSingleIdle = 0.85, kDim = 0.6, kLineDim = 0.3;
    static constexpr int kHoverInMs = 200, kHoverOutMs = 350, kHoverLingerMs = 1000;

signals:
    void selectionChanged();
    void dprChanged();
    void spreadChanged();
    void clustersChanged();
    void focusChanged();
    void shownChanged();
    void highlightChanged();
    void hvPlanChanged();
    void motionChanged();
    void pointerChanged();

private:
    friend class MarkerModel;
    void recluster();
    void respread();
    void updatePositions();
    void updateOpacity();
    void restyle();
    double stopOpacity(const Stop &s) const;
    double hoverLevel(int plan) const;
    void hoverTo(const QString &planId, int plan);
    void unhover();
    void hoverTick();
    void startAnim();
    void setHv(const QString &planId, int plan);
    bool dimmed() const;
    bool m_forceDim = false;
    QString trd(const char *s) const { return m_tr ? m_tr(s) : QString::fromUtf8(s); }

    const geo::Camera *m_cam = nullptr;
    bool m_globe = false;             // 0.9.5：在地球上
    globe::View m_gv;
    double m_gzoom = 0;
    std::function<void(double, double, double, int)> m_easeTo;
    std::function<void(double, double, double, double, double, int)> m_fit;
    std::function<QString(const char *)> m_tr;
    QVector<TripPlan> m_plans;
    QVector<Stop> m_stops;
    QVector<Cluster> m_clusters;
    MarkerModel m_stopsModel, m_clustersModel;
    int m_tier = -2;                  // -2 = 还没分过；-1 = 6 级以上（全部展开）
    double m_dpr = 1;
    QString m_selected;
    int m_focus = -1;
    QString m_spreadJson;
    double m_spreadZoom = 0;
    bool m_moving = false;
    int m_hoverCluster = -1;
    int m_hoverRow = -1;
    bool m_shown = true;

    // 悬停：同一时间只亮一个计划；渐入 / 渐出
    QString m_hoverId;
    int m_hoverPlan = -1;
    QTimer m_lingerTimer, m_hoverTimer;
    QElapsedTimer m_hoverClock;
    QHash<int, double> m_h, m_hTarget;   // 计划下标 → 高亮程度 0..1
    double m_dimT = 0;                   // 其余变淡的程度 0..1（0.8.4：标记和线一起渐变）
    QString m_hvId;                      // 高亮层上的计划（0.8.4）
    int m_hvPlan = -1;
    bool m_motion = true;
};

} // namespace tp::map
