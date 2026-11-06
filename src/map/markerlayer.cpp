#include "markerlayer.h"

#include "markerimages.h"
#include "msicons.h"
#include "perf.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace tp::map {

// ---------------- MarkerModel ----------------

MarkerModel::MarkerModel(MarkerLayer *layer, bool clusters, QObject *parent)
    : QAbstractListModel(parent), m_layer(layer), m_clusters(clusters) {}

int MarkerModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_clusters ? int(m_layer->m_clusters.size()) : int(m_layer->m_stops.size());
}

QVariant MarkerModel::data(const QModelIndex &index, int role) const
{
    const int r = index.row();
    if (m_clusters) {
        if (r < 0 || r >= m_layer->m_clusters.size()) return {};
        const MarkerLayer::Cluster &c = m_layer->m_clusters[r];
        switch (role) {
        case X: return c.x;
        case Y: return c.y;
        case Img: return c.img;
        case W: case H: return c.size;
        case Ax: case Ay: return c.size / 2.0;
        case Op: return c.op;
        case Z: return 1;
        case Shown: return c.limb > 0.001;
        case Focus: return false;
        case Glow: return QString();
        case Tip: return c.tip;
        case TipY: return -c.size / 2.0;
        case PlanId: return QString();
        case StopIdx: return -1;
        case Sc: return c.sc;
        case Hit: return c.limb >= 0.5;
        default: return {};
        }
    }
    if (r < 0 || r >= m_layer->m_stops.size()) return {};
    const MarkerLayer::Stop &s = m_layer->m_stops[r];
    const bool sel = !m_layer->m_selected.isEmpty() && s.planId == m_layer->m_selected;
    const bool focus = sel && m_layer->m_focus == s.stop;
    switch (role) {
    case X: return s.x;
    case Y: return s.y;
    case Img: return s.img;
    case W: return s.geom.w;
    case H: return s.geom.h;
    case Ax: return s.geom.ax;
    case Ay: return s.geom.ay;
    case Op: return s.op;
    case Z: return focus ? 3000 : sel ? 1000 : 0;
    case Shown: return !s.clustered && s.limb > 0.001;
    case Focus: return focus;
    case Glow: return focus ? MarkerImages::glowId(s.geom.w, s.geom.h, s.style.shape == QLatin1String("pin"), m_layer->m_dpr) : QString();
    case Tip: return s.tip;
    case TipY: return -s.geom.ay;   // 提示框底边 = 标记顶边（再往上 4 px 在 QML）
    case PlanId: return s.planId;
    case StopIdx: return s.stop;
    case Sc: return s.sc;
    case Hit: return s.limb >= 0.5;   // 0.9.5：快转到背面（透明到一半以下）的不能点
    default: return {};
    }
}

QHash<int, QByteArray> MarkerModel::roleNames() const
{
    return { { X, "mx" }, { Y, "my" }, { Img, "img" }, { W, "mw" }, { H, "mh" }, { Ax, "ax" }, { Ay, "ay" }, { Op, "op" }, { Z, "mz" },
             { Shown, "shown" }, { Focus, "focused" }, { Glow, "glow" }, { Tip, "tip" }, { TipY, "tipY" }, { PlanId, "planId" },
             { StopIdx, "stopIdx" }, { Sc, "sc" }, { Hit, "hit" } };
}

void MarkerModel::changed(const QList<int> &roles)
{
    const int n = rowCount();
    if (n > 0) emit dataChanged(index(0), index(n - 1), roles);
}

void MarkerModel::replaceRows(int oldN, int newN, const std::function<void()> &swap)
{
    if (newN > oldN) {
        beginInsertRows({}, oldN, newN - 1);
        swap();
        endInsertRows();
    } else if (newN < oldN) {
        beginRemoveRows({}, newN, oldN - 1);
        swap();
        endRemoveRows();
    } else {
        swap();
    }
    const int common = std::min(oldN, newN);
    if (common > 0) emit dataChanged(index(0), index(common - 1));
}

void MarkerModel::changedRow(int row, const QList<int> &roles)
{
    if (row >= 0 && row < rowCount()) emit dataChanged(index(row), index(row), roles);
}

// ---------------- MarkerLayer ----------------

MarkerLayer::MarkerLayer(QObject *parent)
    : QObject(parent), m_stopsModel(this, false, this), m_clustersModel(this, true, this)
{
    m_spreadJson = QStringLiteral("{\"type\":\"FeatureCollection\",\"features\":[]}");
    m_lingerTimer.setSingleShot(true);
    m_lingerTimer.setInterval(kHoverLingerMs);
    connect(&m_lingerTimer, &QTimer::timeout, this, [this] {
        // Electron unhoverPlan 的计时到了：这个计划渐出（hvHide），其余恢复（syncDim）
        if (m_hoverPlan >= 0) m_hTarget[m_hoverPlan] = 0;
        m_hoverId.clear();
        m_hoverPlan = -1;
        startAnim();
        updateOpacity();
    });
    m_hoverTimer.setInterval(16);
    m_hoverTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_hoverTimer, &QTimer::timeout, this, &MarkerLayer::hoverTick);
}

void MarkerLayer::setDpr(double d)
{
    d = std::clamp(d > 0 ? d : 1.0, 0.5, 4.0);
    if (std::abs(d - m_dpr) < 1e-6) return;
    m_dpr = d;
    restyle();
    emit dprChanged();
}

bool MarkerLayer::canStep() const
{
    for (const TripPlan &p : m_plans)
        if (p.id == m_selected) return p.isRoute();
    return false;
}

void MarkerLayer::restyle()
{
    for (Stop &s : m_stops) s.img = MarkerImages::stopId(s.style, s.wish, m_dpr);
    for (Cluster &c : m_clusters) {
        QStringList colors;
        for (int i : c.members) colors << m_stops[i].style.color;
        const QString bg = markers::averageColor(colors);
        c.img = MarkerImages::clusterId(c.size, bg, markers::isLightColor(bg) ? QStringLiteral("#1f2328") : QStringLiteral("#fff"),
                                        int(c.members.size()), m_dpr);
    }
    m_stopsModel.changed({ MarkerModel::Img, MarkerModel::Glow });
    m_clustersModel.changed({ MarkerModel::Img });
}

void MarkerLayer::rebuild(const TripData &trips)
{
    m_plans = trips.plans();
    // 选中的计划没了（删掉 / 撤销）就取消选中；站点少了就去掉聚焦
    bool selOk = false;
    for (const TripPlan &p : m_plans)
        if (p.id == m_selected && !p.stops.isEmpty()) { selOk = true; if (m_focus >= p.stops.size()) m_focus = -1; }
    const bool selChanged = !selOk && !m_selected.isEmpty();
    if (!selOk) { m_selected.clear(); m_focus = -1; }
    // Electron renderMap → hoverReset：悬停全部清掉（高亮层马上清空，变淡按新的状态渐变）
    m_hoverId.clear();
    m_hoverPlan = -1;
    m_h.clear();
    m_hTarget.clear();
    m_lingerTimer.stop();
    m_hoverTimer.stop();
    m_hoverCluster = -1;
    m_hoverRow = -1;
    setHv(QString(), -1);

    QVector<Stop> fresh;
    fresh.reserve(m_stops.size());
    const bool iconsOk = MarkerImages::iconReady();
    for (int pi = 0; pi < m_plans.size(); ++pi) {
        const TripPlan &p = m_plans[pi];
        const int n = int(p.stops.size());
        if (!n) continue;
        const QString name = p.name.isEmpty() ? trd("未命名计划") : p.name;
        const bool route = p.isRoute();
        for (int i = 0; i < n; ++i) {
            Stop s;
            s.plan = pi;
            s.stop = i;
            s.planId = p.id;
            s.base = i < p.markerPos.size() ? p.markerPos[i] : geo::LatLng{ p.stops[i].lat, geo::normLng(p.stops[i].lng) };
            s.style = markers::stopStyle(p, i, n);
            const bool iconShown = iconsOk && !s.style.icon.isEmpty() && msicons::codepoint(s.style.icon.toUtf8().constData()) != 0;
            s.geom = markers::geomFor(s.style, iconShown);
            s.route = route;
            s.wish = p.status == QLatin1String("wish");
            s.clusterable = p.id != m_selected;
            // Electron：路线 "计划名 · 1. 站名"；单一目的地有几个点时 "计划名 · 站名"，只有一个点时只有计划名
            // 提示框是 StyledText：和 Electron esc() 一样先转义
            const QString nm = name.toHtmlEscaped(), sn = p.stops[i].name.toHtmlEscaped();
            s.tip = route ? QStringLiteral("%1 · %2. %3").arg(nm).arg(i + 1).arg(sn)
                          : n > 1 ? QStringLiteral("%1 · %2").arg(nm, sn) : nm;
            s.img = MarkerImages::stopId(s.style, s.wish, m_dpr);
            fresh.push_back(s);
        }
    }
    m_stopsModel.replaceRows(int(m_stops.size()), int(fresh.size()), [&] { m_stops = std::move(fresh); });
    m_tier = -2;
    recluster();
    respread();
    updatePositions();
    startAnim();
    updateOpacity();
    if (selChanged) emit selectionChanged();
}

void MarkerLayer::recluster()
{
    const int tier = (m_cam || m_globe) ? markers::clusterTier(currentZoom()) : -1;
    m_tier = tier;
    m_hoverCluster = -1;
    m_clustersModel.begin();
    m_clusters.clear();
    for (Stop &s : m_stops) s.clustered = false;
    if (tier >= 0) {
        QVector<int> idx;
        QVector<geo::LatLng> pts;
        for (int i = 0; i < m_stops.size(); ++i)
            if (m_stops[i].clusterable) { idx.push_back(i); pts.push_back(m_stops[i].base); }
        const markers::Groups g = markers::cluster(pts, markers::clusterKm(tier));
        for (const QVector<int> &mem : g.members) {
            if (mem.size() < 2) continue;
            Cluster c;
            std::vector<geo::LatLng> centerPts;
            QStringList names;
            QSet<QString> seen;
            for (int k : mem) {
                const int si = idx[k];
                c.members.push_back(si);
                m_stops[si].clustered = true;
                centerPts.push_back(m_stops[si].base);
                if (!m_stops[si].route) c.allRoute = false;
                const TripPlan &p = m_plans[m_stops[si].plan];
                const QString name = p.name.isEmpty() ? trd("未命名计划") : p.name;
                if (!seen.contains(name)) { seen.insert(name); names << name; }   // Electron：new Set(planName)
            }
            c.center = geo::centerOf(centerPts);
            c.size = markers::clusterSize(int(c.members.size()));
            QStringList esc;
            for (const QString &nm : names.mid(0, 8)) esc << nm.toHtmlEscaped();
            c.tip = esc.join(QStringLiteral("<br>"));
            if (names.size() > 8) c.tip += QStringLiteral("<br>") + trd("…等 %1 个计划").arg(names.size());
            m_clusters.push_back(c);
        }
    }
    // 图片描述（restyle 里的那部分，只算圆圈）
    for (Cluster &c : m_clusters) {
        QStringList colors;
        for (int i : c.members) colors << m_stops[i].style.color;
        const QString bg = markers::averageColor(colors);
        c.img = MarkerImages::clusterId(c.size, bg, markers::isLightColor(bg) ? QStringLiteral("#1f2328") : QStringLiteral("#fff"),
                                        int(c.members.size()), m_dpr);
    }
    m_clustersModel.end();
    m_stopsModel.changed({ MarkerModel::Shown });
    updatePositions();
    updateOpacity();
    emit clustersChanged();
}

void MarkerLayer::respread()
{
    if (m_globe) {
        // 地球最近约 7 级，到不了错开的 14 级：偏移全部清掉
        for (Stop &s : m_stops) s.ox = s.oy = 0;
        const QString empty = QStringLiteral("{\"type\":\"FeatureCollection\",\"features\":[]}");
        if (m_spreadJson != empty) { m_spreadJson = empty; emit spreadChanged(); }
        updatePositions();
        return;
    }
    if (!m_cam) return;
    QVector<markers::SpreadItem> items;
    items.reserve(m_stops.size());
    for (const Stop &s : m_stops) items.push_back({ s.base, s.planId, s.stop });
    const markers::SpreadResult r = markers::spread(items, *m_cam);
    bool any = false;
    for (int i = 0; i < m_stops.size(); ++i) {
        m_stops[i].ox = r.offset[i].x;
        m_stops[i].oy = r.offset[i].y;
        any = any || r.offset[i].x != 0 || r.offset[i].y != 0;
    }
    m_spreadZoom = m_cam->zoom;
    QJsonArray feats;
    for (const geo::LatLng &d : r.dots)
        feats.append(QJsonObject{ { "type", "Feature" }, { "properties", QJsonObject{} },
                                  { "geometry", QJsonObject{ { "type", "Point" }, { "coordinates", QJsonArray{ d.lng, d.lat } } } } });
    const QString json = QString::fromUtf8(QJsonDocument(QJsonObject{ { "type", "FeatureCollection" }, { "features", feats } })
                                               .toJson(QJsonDocument::Compact));
    if (json != m_spreadJson) { m_spreadJson = json; emit spreadChanged(); }
    Q_UNUSED(any);
    updatePositions();
}

void MarkerLayer::updatePositions()
{
    if (m_globe) {
        // 0.9.5 地球上：按地球的相机投影；离地平线 8° 内渐隐（C4），25° 内缩到 70%（G3）
        const globe::Camera cam(m_gv);
        auto place = [&](const geo::LatLng &ll, double *x, double *y, double *limb, double *sc) {
            const globe::Limb l = globe::limbOf(m_gv, ll.lat, ll.lng);
            // 0.9.7 变形：背面渐隐、边缘变小跟着淡掉（平面上没有地平线）
            const double t = std::clamp(m_gv.morph, 0.0, 1.0);
            *limb = l.fade + (1 - l.fade) * t;
            *sc = l.scale + (1 - l.scale) * t;
            if (!cam.project(globe::toVec(ll.lat, ll.lng), x, y)) { *limb = 0; *x = *y = -10000; }
        };
        for (Stop &s : m_stops) { place(s.base, &s.x, &s.y, &s.limb, &s.sc); s.op = stopOpacity(s) * s.limb; }
        const double dim = markerDimFactor();
        for (int i = 0; i < m_clusters.size(); ++i) {
            Cluster &c = m_clusters[i];
            place(c.center, &c.x, &c.y, &c.limb, &c.sc);
            c.op = (i == m_hoverCluster ? 1 : dim * (c.allRoute ? kRouteIdle : kSingleIdle)) * c.limb;
        }
        m_stopsModel.changed({ MarkerModel::X, MarkerModel::Y, MarkerModel::Op, MarkerModel::Sc, MarkerModel::Shown, MarkerModel::Hit });
        m_clustersModel.changed({ MarkerModel::X, MarkerModel::Y, MarkerModel::Op, MarkerModel::Sc, MarkerModel::Shown, MarkerModel::Hit });
        return;
    }
    if (!m_cam) return;
    // 错开的偏移按 lnglat 固定（maplibre 的 Marker 是按经纬度摆的）：缩放途中跟着比例变，停下来再重新排
    const double k = std::exp2(m_cam->zoom - m_spreadZoom);
    for (Stop &s : m_stops) {
        const geo::Pt p = m_cam->pixelFor(s.base.lat, s.base.lng);
        s.x = p.x + s.ox * k;
        s.y = p.y + s.oy * k;
        if (s.limb != 1 || s.sc != 1) { s.limb = s.sc = 1; s.op = stopOpacity(s); }
    }
    for (Cluster &c : m_clusters) c.limb = c.sc = 1;
    for (Cluster &c : m_clusters) {
        const geo::Pt p = m_cam->pixelFor(c.center.lat, c.center.lng);
        c.x = p.x;
        c.y = p.y;
    }
    m_stopsModel.changed({ MarkerModel::X, MarkerModel::Y, MarkerModel::Op, MarkerModel::Sc, MarkerModel::Shown, MarkerModel::Hit });
    m_clustersModel.changed({ MarkerModel::X, MarkerModel::Y, MarkerModel::Sc, MarkerModel::Shown, MarkerModel::Hit });
}

void MarkerLayer::setGlobeView(bool on, const globe::View &v, double zoomEq, bool moving)
{
    const bool switched = on != m_globe;
    m_globe = on;
    if (on) { m_gv = v; m_gzoom = zoomEq; }
    if (switched) {
        // 换投影：错开、合并重新算（平面那边的偏移是按墨卡托像素算的）
        m_tier = -2;
        m_moving = moving;
        recluster();
        respread();
        updateOpacity();
        return;
    }
    if (on) cameraChanged(moving);
}

void MarkerLayer::cameraChanged(bool moving)
{
    m_moving = moving;
    if (!m_cam && !m_globe) return;
    tp::app::perf::Scope timing("标记摆位", 30);   // 0.9.1：相机每动一下都跑（只累计，超过 30 ms 才写日志）
    if (markers::clusterTier(currentZoom()) != m_tier) recluster();   // Electron map.on('zoom', applyClusters(false))：跨层才重新分
    else updatePositions();
    if (!moving) respread();   // 跳过去、窗口大小变了：马上重排（动画 / 拖动结束时由 movingChanged 排）
}

void MarkerLayer::movingChanged(bool moving)
{
    m_moving = moving;
    if (!moving) respread();   // Electron zoomend / moveend → spreadMarkers
}

// ---- 透明度 ----

bool MarkerLayer::dimmed() const { return m_forceDim || !m_hoverId.isEmpty() || !m_selected.isEmpty(); }

double MarkerLayer::hoverLevel(int plan) const { return m_h.value(plan, 0.0); }

double MarkerLayer::stopOpacity(const Stop &s) const
{
    if (!m_selected.isEmpty() && s.planId == m_selected) return 1;   // LINE_STYLE.sel.marker / 选中的单一目的地
    const double h = hoverLevel(s.plan);
    // Electron：calc(var(--rt-dim) * .65 * (1 - h) + h)。--rt-dim 在 Electron 里是一下子变的；
    // 0.8.4 起和线条的变淡用同一个渐变（PO 2026-11-04：标记和线要同步变淡）
    return markerDimFactor() * (s.route ? kRouteIdle : kSingleIdle) * (1 - h) + h;
}

double MarkerLayer::hvLevel() const { return m_hvPlan >= 0 ? m_h.value(m_hvPlan, 0.0) : 0.0; }

void MarkerLayer::setMotion(bool on)
{
    if (on == m_motion) return;
    m_motion = on;
    emit motionChanged();
}

void MarkerLayer::setHv(const QString &planId, int plan)
{
    m_hvPlan = plan;
    if (m_hvId == planId) return;
    m_hvId = planId;
    emit hvPlanChanged();
}

void MarkerLayer::updateOpacity()
{
    const double dim = markerDimFactor();
    for (Stop &s : m_stops) s.op = stopOpacity(s) * s.limb;
    for (int i = 0; i < m_clusters.size(); ++i) {
        Cluster &c = m_clusters[i];
        c.op = (i == m_hoverCluster ? 1 : dim * (c.allRoute ? kRouteIdle : kSingleIdle)) * c.limb;
    }
    m_stopsModel.changed({ MarkerModel::Op });
    m_clustersModel.changed({ MarkerModel::Op });
    emit highlightChanged();
}

// ---- 悬停（Electron hoverPlan / unhoverPlan / hoverStep） ----

void MarkerLayer::hoverTo(const QString &planId, int plan)
{
    m_lingerTimer.stop();
    if (m_hoverId == planId) return;
    if (m_hoverPlan >= 0) m_hTarget[m_hoverPlan] = 0;
    m_hoverId = planId;
    m_hoverPlan = plan;
    m_hTarget[plan] = 1;
    if (!m_h.contains(plan)) m_h[plan] = 0;
    // 高亮层换成这个计划（Electron hvShow：换数据源，上一条的高亮层马上没了，这条从当前程度渐入）
    if (plan >= 0 && plan < m_plans.size() && m_plans[plan].isRoute()) setHv(planId, plan);
    else setHv(QString(), -1);
    startAnim();
    updateOpacity();
}

void MarkerLayer::startAnim()
{
    if (!m_motion) { hoverTick(); return; }   // 不要动效：一步到位
    if (!m_hoverTimer.isActive()) { m_hoverClock.start(); m_hoverTimer.start(); }
}

void MarkerLayer::unhover()
{
    if (m_hoverId.isEmpty() || m_lingerTimer.isActive()) return;   // 已经在倒计时就不重新计时
    m_lingerTimer.start();
}

void MarkerLayer::hoverTick()
{
    // Electron hoverStep（每个计划的高亮程度 h）+ 线条变淡的过渡（MapLibre line-opacity-transition：进 200 ms、出 350 ms）。
    // 0.8.4 起两者在同一个定时器里走，标记和线同一帧变
    const double dt = m_motion ? double(std::min<qint64>(64, std::max<qint64>(0, m_hoverClock.restart()))) : 1e9;
    bool busy = false;
    for (auto it = m_h.begin(); it != m_h.end();) {
        const double target = m_hTarget.value(it.key(), 0);
        double &h = it.value();
        const double step = dt / (target > h ? kHoverInMs : kHoverOutMs);
        h = target > h ? std::min(target, h + step) : std::max(target, h - step);
        if (h != target) busy = true;
        if (h == target && target == 0) { m_hTarget.remove(it.key()); it = m_h.erase(it); }
        else ++it;
    }
    const double dimTarget = dimmed() ? 1 : 0;
    if (m_dimT != dimTarget) {
        const double step = dt / (dimTarget > m_dimT ? kHoverInMs : kHoverOutMs);
        m_dimT = dimTarget > m_dimT ? std::min(dimTarget, m_dimT + step) : std::max(dimTarget, m_dimT - step);
        if (m_dimT != dimTarget) busy = true;
    }
    // 高亮层渐出完了（不再碰着）：清掉（Electron 是下次 hvShow 时才换数据源，看不出差别）
    if (m_hvPlan >= 0 && m_hoverId != m_hvId && !m_h.contains(m_hvPlan)) setHv(QString(), -1);
    updateOpacity();
    if (!busy) m_hoverTimer.stop();
}

void MarkerLayer::hoverStop(int row)
{
    if (row < 0 || row >= m_stops.size()) return;
    m_hoverRow = row;
    emit pointerChanged();
    const Stop &s = m_stops[row];
    if (!m_selected.isEmpty() && s.planId == m_selected) return;   // 选中的计划不做悬停高亮
    hoverTo(s.planId, s.plan);
}

void MarkerLayer::unhoverStop(int row)
{
    // 鼠标从一个标记直接移到另一个时，Qt 可能先报"进入新的"再报"离开旧的"：只认最后进入的那个
    if (row != m_hoverRow) return;
    m_hoverRow = -1;
    emit pointerChanged();
    unhover();
}

void MarkerLayer::hoverCluster(int row)
{
    if (row < 0 || row >= m_clusters.size() || row == m_hoverCluster) return;
    const int old = m_hoverCluster;
    m_hoverCluster = row;
    const double dim = markerDimFactor();
    if (old >= 0 && old < m_clusters.size()) {
        m_clusters[old].op = dim * (m_clusters[old].allRoute ? kRouteIdle : kSingleIdle) * m_clusters[old].limb;
        m_clustersModel.changedRow(old, { MarkerModel::Op });
    }
    m_clusters[row].op = m_clusters[row].limb;
    m_clustersModel.changedRow(row, { MarkerModel::Op });
    emit pointerChanged();
}

void MarkerLayer::unhoverCluster(int row)
{
    if (row != m_hoverCluster || row < 0 || row >= m_clusters.size()) return;
    m_hoverCluster = -1;
    const double dim = markerDimFactor();
    m_clusters[row].op = dim * (m_clusters[row].allRoute ? kRouteIdle : kSingleIdle) * m_clusters[row].limb;
    m_clustersModel.changedRow(row, { MarkerModel::Op });
    emit pointerChanged();
}

// ---- 线条（0.8.4，Electron onMapMove / map.on('click')） ----

void MarkerLayer::hoverLine(const QString &planId)
{
    // 选中的计划不做悬停高亮（提示框照样显示）；碰到别的计划就高亮它
    if (!m_selected.isEmpty() && planId == m_selected) { unhover(); return; }
    for (int i = 0; i < m_plans.size(); ++i)
        if (m_plans[i].id == planId) { hoverTo(planId, i); return; }
}

void MarkerLayer::unhoverLine() { unhover(); }

void MarkerLayer::clickLine(const QString &planId)
{
    // Electron selectPlan(pid, false)：只选中，不飞、不聚焦站点；同一个计划已经选中时什么都不变
    if (planId == m_selected) return;
    selectPlan(planId, -1, false);
}

// ---- 选中 / 聚焦 ----

void MarkerLayer::clickStop(int row)
{
    if (row < 0 || row >= m_stops.size() || (!m_cam && !m_globe)) return;
    const Stop s = m_stops[row];
    // Electron：selectPlan(plan.id, false, si, m.mk.getLngLat()) —— 平移到标记现在的位置（错开时是错开后的位置；地球上不错开）
    const geo::LatLng at = m_globe ? s.base : m_cam->coordinateFor(s.x, s.y);
    selectPlan(s.planId, s.stop, false);
    if (m_easeTo) m_easeTo(at.lat, geo::normLng(at.lng), -1, 500);
}

void MarkerLayer::clickCluster(int row)
{
    if (row < 0 || row >= m_clusters.size() || (!m_cam && !m_globe)) return;
    const Cluster &c = m_clusters[row];
    // 点圆圈：放大到能看清里面这些点（至少放大一级）
    double w = 1e9, s = 1e9, e = -1e9, n = -1e9;
    const double ref = m_stops[c.members.first()].base.lng;
    for (int i : c.members) {
        const geo::LatLng &b = m_stops[i].base;
        const double lng = geo::unwrapTo(b.lng, ref);
        w = std::min(w, lng); e = std::max(e, lng); s = std::min(s, b.lat); n = std::max(n, b.lat);
    }
    m_hoverCluster = -1;
    if (m_fit) m_fit(w, s, e, n, std::floor(currentZoom()) + 1, 500);
}

void MarkerLayer::selectPlan(const QString &planId, int stopIdx, bool pan)
{
    int pi = -1;
    for (int i = 0; i < m_plans.size(); ++i)
        if (m_plans[i].id == planId) { pi = i; break; }
    if (pi < 0 || m_plans[pi].stops.isEmpty()) return;
    const TripPlan &p = m_plans[pi];
    if (stopIdx >= p.stops.size()) stopIdx = -1;
    const bool samePlan = planId == m_selected;
    m_selected = planId;
    m_focus = stopIdx;
    if (!samePlan) {
        // 选中的计划不参与合并、不做悬停高亮（Electron renderMap 会清掉悬停）
        m_lingerTimer.stop();
        m_hoverTimer.stop();
        m_hoverId.clear();
        m_hoverPlan = -1;
        m_h.clear();
        m_hTarget.clear();
        setHv(QString(), -1);
        for (Stop &s : m_stops) s.clusterable = s.planId != m_selected;
        recluster();
    }
    m_stopsModel.changed({ MarkerModel::Z, MarkerModel::Focus, MarkerModel::Glow });
    startAnim();
    updateOpacity();
    emit selectionChanged();
    emit focusChanged();
    if (pan && stopIdx >= 0 && m_easeTo) {
        const int row = indexOf(planId, stopIdx);
        const geo::LatLng b = row >= 0 ? m_stops[row].base : geo::LatLng{ p.stops[stopIdx].lat, geo::normLng(p.stops[stopIdx].lng) };
        m_easeTo(b.lat, b.lng, -1, 500);
    }
}

void MarkerLayer::clearSelection()
{
    if (m_selected.isEmpty()) return;
    m_selected.clear();
    m_focus = -1;
    for (Stop &s : m_stops) s.clusterable = true;
    recluster();
    m_stopsModel.changed({ MarkerModel::Z, MarkerModel::Focus, MarkerModel::Glow });
    startAnim();
    updateOpacity();
    emit selectionChanged();
    emit focusChanged();
}

void MarkerLayer::stepStop(int d)
{
    if (!canStep()) return;
    int n = 0;
    for (const TripPlan &p : m_plans)
        if (p.id == m_selected) n = int(p.stops.size());
    if (!n) return;
    const int cur = m_focus;
    const int next = cur < 0 ? (d > 0 ? 0 : n - 1) : std::clamp(cur + d, 0, n - 1);
    if (next == cur) return;
    selectPlan(m_selected, next, true);
}

int MarkerLayer::indexOf(const QString &planId, int stopIdx) const
{
    for (int i = 0; i < m_stops.size(); ++i)
        if (m_stops[i].planId == planId && m_stops[i].stop == stopIdx) return i;
    return -1;
}

QVariantMap MarkerLayer::stopInfo(int row) const
{
    if (row < 0 || row >= m_stops.size()) return {};
    const Stop &s = m_stops[row];
    return { { "x", s.x }, { "y", s.y }, { "lat", s.base.lat }, { "lng", s.base.lng }, { "planId", s.planId }, { "stop", s.stop },
             { "clustered", s.clustered }, { "op", s.op }, { "shape", s.style.shape }, { "text", s.style.text }, { "w", s.geom.w },
             { "h", s.geom.h }, { "ax", s.geom.ax }, { "ay", s.geom.ay }, { "ox", s.ox }, { "oy", s.oy }, { "tip", s.tip },
             { "limb", s.limb }, { "sc", s.sc } };
}

QVariantMap MarkerLayer::demo(const QString &kind) const
{
    const auto out = [this](int i) {
        const Stop &s = m_stops[i];
        return QVariantMap{ { "planId", s.planId }, { "stop", s.stop }, { "lat", s.base.lat }, { "lng", s.base.lng } };
    };
    if (kind == QLatin1String("line")) {   // 0.8.4 lines 页：站点最多的一条路线（不是"想去"），返回它中间那一站
        int best = -1, bestN = 0;
        for (int pi = 0; pi < m_plans.size(); ++pi)
            if (m_plans[pi].isRoute() && m_plans[pi].status != QLatin1String("wish") && m_plans[pi].stops.size() > bestN) { best = pi; bestN = int(m_plans[pi].stops.size()); }
        const int row = best >= 0 ? indexOf(m_plans[best].id, bestN / 2) : -1;
        return row >= 0 ? out(row) : QVariantMap{};
    }
    for (int i = 0; i < m_stops.size(); ++i) {
        const Stop &s = m_stops[i];
        if (kind == QLatin1String("route") && s.route && s.stop == 2 && m_plans[s.plan].stops.size() >= 4) return out(i);
        if (kind == QLatin1String("pin") && !s.route && s.wish && s.style.shape == QLatin1String("pin")) return out(i);
        if (kind == QLatin1String("spread"))
            for (int j = i + 1; j < m_stops.size(); ++j)
                if (geo::distanceMeters(s.base, m_stops[j].base) <= markers::kSpreadNearM) return out(i);
    }
    return {};
}

} // namespace tp::map
