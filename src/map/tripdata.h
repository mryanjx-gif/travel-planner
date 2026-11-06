// 地图要画的东西（0.8.0 起从内存里的数据生成，不再读文件）：路线、站点、已完成计划的站点（给"已解锁"）、手动标记。
// 路线几何照搬 Electron v6.3.0 map-app.js：buildRoute / greatCircle / roadLegPts / normLng / unwrapTo。
// 数据按原生格式（travel.json，stops 数组顺序就是站点顺序）；也认 Electron 的写法（order、legMode / legPath / legKey），测试方便。
#pragma once

#include "geo.h"
#include "jsonvalue.h"
#include "unlock.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>

namespace tp::map {

struct TripStop {
    double lat = 0, lng = 0, order = 0;
    QString name, color, legMode, legPath, legKey;
    QString icon, shape;       // 标记样式（0.8.2，Stop.icon / Stop.shape；空 = 跟随计划）
};

struct TripPlan {
    QString id, name, status, type, color, startDate;
    QString icon, shape, markerMode;   // 标记样式（0.8.2，Plan.icon / shape / markerMode）
    QVector<TripStop> stops;   // 已按顺序排好、只含有效坐标（Electron validStops）
    // 标记的位置（0.8.2）：路线是 buildRoute 的 pos（道路折线的终点等），单一目的地是站点坐标；经度归一到 ±180
    QVector<geo::LatLng> markerPos;
    // 路线的线（0.8.4，悬停命中用）：每段一条折线（经度连续展开，可能超出 ±180）、线型 0 实线 / 1 虚线 / 2 点线
    QVector<QVector<geo::LatLng>> segments;
    int dash = 0;
    double west = 0, east = 0, south = 0, north = 0;   // 所有段的范围（经度同样是展开后的）
    bool isRoute() const { return type == QLatin1String("route") && stops.size() > 1; }
};

class TripData {
public:
    // visible（0.8.7 筛选 / 隐藏）：返回 false 的计划不画（路线、标记、命中、范围都不含它）；
    // "已解锁"用的已完成计划的站点照样全算（Electron computeUnlocked 用的是全部计划）
    // 0.9.2：每个计划算好的东西（站点、路线几何、给地图的线）按计划 id 缓存；计划的数据没变（和上次是同一份，
    // 或深比较相等）就直接用，只重算改了的。换模式 / 筛选只是换一批"要画的"，全部命中缓存
    void build(const json::Value &root, const std::function<bool(const json::Value &plan)> &visible = {});
    // 上一次 build 里重算了几个计划（其余命中缓存；测试和计时用）
    int recomputed() const { return m_recomputed; }
    // 上一次 build 时"已解锁"要用的输入（全部已完成计划的站点、手动标记）和再上一次比有没有变
    bool unlockInputsChanged() const { return m_unlockChanged; }
    // 现算（只有测试用）
    QJsonObject routesGeoJson() const;
    // 0.8.4：路线分三份交给地图（平时 / 选中 / 鼠标碰到的高亮层，Electron routes 的 sel 和 routes-hv）。
    // except = 除了这个计划以外的全部；only = 只有这个计划（没有线时是空的 FeatureCollection）
    QString routesJsonExcept(const QString &planId) const;
    QString routesJsonOnly(const QString &planId) const;
    QJsonObject stopsGeoJson() const;   // 现算
    const QVector<TripPlan> &plans() const { return m_plans; }
    // 0.9.7：用另一份 TripData 刚算好的缓存（叠上来的旅程和正常画的是同一批计划，不用再算一遍）
    void shareCache(const TripData &o) { m_cache = o.m_cache; }
    QVector<UnlockModel::PlanStops> completedStops() const { return m_completed; }
    QVector<UnlockModel::Manual> manualRegions() const { return m_manual; }
    int legCount() const { return m_legs; }
    int stopCount() const { return m_stopCount; }
    int roadLegCount() const { return m_roadLegs; }
    int routeCount() const { return m_routeCount; }
    // 所有站点（地图上的位置，经度归一到 ±180）
    QVector<geo::LatLng> allStops() const;

    static QString planColor(const TripPlan &p);
    static QString stopColor(const TripPlan &p, const TripStop &s);

private:
    // 一个计划算好的全部东西
    struct Computed {
        json::Value src;              // 算它用的那份数据（判断变没变）
        TripPlan plan;                // 含 markerPos、segments、范围
        QString feats;                // 给地图的线（GeoJSON 要素，逗号连起来的文字）
        QJsonArray lines, points;     // 同样的线和站点（现算整份 GeoJSON 时用）
        int legs = 0, roadLegs = 0;
        bool route = false;
    };
    static Computed compute(const json::Value &pv);
    QVector<Computed> m_shown;      // 要画的计划（顺序同数据），m_plans 和它一一对应
    QVector<TripPlan> m_plans;
    QVector<UnlockModel::Manual> m_manual;
    QVector<UnlockModel::PlanStops> m_completed;
    QVector<QString> m_planFeats;   // 每个计划的线，和 m_plans 一一对应
    QHash<QString, Computed> m_cache;
    QVector<QString> m_completedKey; // 上次参与"已解锁"的计划（id 顺序）
    int m_legs = 0, m_stopCount = 0, m_roadLegs = 0, m_routeCount = 0;
    int m_recomputed = 0;
    bool m_unlockChanged = true;
};

} // namespace tp::map
