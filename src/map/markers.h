// 地图标记的纯计算部分（0.8.2）：样式、合并（聚合）、错开、合并圆圈的颜色。只依赖 Qt Core，单元测试 tst_map 覆盖。
// 全部照搬 Electron v6.3.0 map-app.js：
//   stopStyle / markerParts（:1675、:1692）、applyClusters / makeClusterMarker（:1571、:1596）、
//   distKm / averageColor / isLightColor（:1509、:1545）、spreadMarkers（:1961）。
// 画成图片在 src/map/markerimages.*，摆放和交互在 MapController / qml/components/MapMarkers.qml。
#pragma once

#include "geo.h"
#include "tripdata.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tp::map::markers {

// 某个站点在地图上的最终样式（Electron stopStyle）
struct Style {
    QString color;   // #rrggbb（数据里写什么就是什么）
    QString shape;   // circle / square / pin / diamond
    QString icon;    // Material Symbols 图标名；空 = 没有
    QString text;    // 数字（路线、数字顺序）
    bool dot = false;   // 路线、圆点模式
};
Style stopStyle(const TripPlan &plan, int i, int n);

// 标记的尺寸和锚点（Electron markerParts）：iconReady = 图标字体能用（不能用时退回数字或白点）
struct Geom {
    double w = 22, h = 22;
    double ax = 11, ay = 11;     // 锚点（标记图片里对准真实位置的那一点）
    double tipY = -11;           // 提示框底边相对锚点的 y（Electron tip 偏移；showTipFor 用标记顶边 - 4）
};
Geom geomFor(const Style &st, bool iconReady);
bool showsIcon(const Style &st, bool iconReady);

// 合并圆圈的大小（Electron makeClusterMarker：<10 → 26，<50 → 28，否则 32）
int clusterSize(int n);

// ---- 颜色 ----
// #rgb / #rrggbb → 0..1；认不出返回 false
bool hexToRgb(const QString &hex, double rgb[3]);
// 在 OKLab 里求平均（认不出的跳过，全都认不出 = #364152），结果 #rrggbb 小写
QString averageColor(const QStringList &colors);
// 浅色背景配深色字（相对亮度 > 0.45）
bool isLightColor(const QString &hex);

// ---- 合并 ----
// 当前缩放属于哪一层：0–5（6 级起返回 -1 = 全部展开）。层号就是 floor(zoom)，0 级以下算 0
int clusterTier(double zoom);
// 每层的合并距离（km）：0–1 → 1400，2 → 700，3 → 350，4 → 180，5 → 90
double clusterKm(int tier);
// 按顺序逐个看：离它最近、且 ≤ km 的已有组就并进去（和组的"种子" = 第一个点比），否则自己开一组。
// 返回每个点所在的组号（组号按出现顺序），以及每组的成员。只放可参与合并的点（选中计划的点不放进来）
struct Groups {
    QVector<int> groupOf;              // 下标 = 输入点
    QVector<QVector<int>> members;     // 组号 → 输入点下标
};
Groups cluster(const QVector<geo::LatLng> &pts, double km);

// ---- 错开 ----
constexpr double kSpreadMinZoom = 14;
constexpr double kSpreadNearM = 20;
struct SpreadItem {
    geo::LatLng base;
    QString planId;
    int stopIdx = 0;
};
// 返回每个点相对真实位置的像素偏移（不错开 = 0,0），以及每个被错开的组的真实位置（画小实心点）。
// 只看视野（上下左右各再扩一半）里的点；zoom < 14 时全部为 0。
struct SpreadResult {
    QVector<geo::Pt> offset;
    QVector<geo::LatLng> dots;
};
SpreadResult spread(const QVector<SpreadItem> &items, const geo::Camera &cam);

} // namespace tp::map::markers
