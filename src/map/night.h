// 卫星图上的昼夜阴影（0.8.0）。
//
// Electron 是一张 768×768 的阴影图（image 源）。maplibre-native 的 image 源不会在每个世界副本上重复画
// （0.4.1 PO 实测：缩小到出现第二个世界时另一边没有阴影），而且要给文件网址。
// 原生版改成几层 GeoJSON 多边形：第 k 层 = 太阳高度低于某个值的区域（一个以反太阳点为中心的球冠），
// 各层半透明叠起来，逼近 Electron 的平滑过渡（太阳高度 -0.8° 起，12° 暮光带，smoothstep，最深 .62）。
// GeoJSON 会在每个世界副本上画，也不用文件。
// 0.8.7（PO：卫星图上阴影有"断层"）：12 层叠起来每层差约 5% 的亮度，白色的冰盖上看得出台阶。改成互不重叠的细带（stripes）：
// 每条带只覆盖"太阳高度在 [a, b) 之间"的地方，不透明度直接取中点的目标值；带分得很细（48 条，每条 0.25°），每个像素只画一次，
// 比原来 13 层叠起来还省显卡。做法：按经线切成竖列（每条经线和一个球冠的交是一段纬度区间），一条带 = 外圈区间减内圈区间
// （上下两段），相邻列连成多边形。原来的 bands() / stackedAlpha() 只剩单元测试用。
#pragma once

#include "geo.h"

#include <QString>
#include <vector>

namespace tp::map::night {

constexpr double kAlpha = 0.62;        // Electron NIGHT.alpha
constexpr double kDayAlt = -0.8;       // 太阳高度 > -0.8°（含大气折射）算白天
constexpr double kTwilight = 12.0;     // NIGHT.twilight
constexpr int kBands = 12;             // 暮光带分几层（+ 最里面一层全黑）

struct Band {
    double alt = 0;                         // 这一层：太阳高度低于 alt 的区域
    double opacity = 0;                     // 这一层自己的不透明度（叠起来后等于目标值）
    std::vector<geo::LatLng> ring;          // 外环（经度连续展开，可能超出 ±180；首尾相同）
};

// 太阳直射点（和 Electron subsolarPoint 一样；msSinceEpoch 是 UTC 毫秒）
geo::LatLng subsolarPoint(double msSinceEpoch);

// Electron drawNight 的目标不透明度：太阳高度 alt（度）时阴影有多深
double targetAlpha(double alt);
// 叠好 bands 后，太阳高度为 alt 的地方实际有多深（测试用）
double stackedAlpha(const std::vector<Band> &bands, double alt);
// 某地此刻的太阳高度（度）
double sunAltitude(geo::LatLng sun, double lat, double lng);

std::vector<Band> bands(geo::LatLng sun, int n = kBands, int samples = 180);

// ---- 0.8.7：互不重叠的细带 ----
constexpr int kStripes = 48;           // 暮光带分几条（+ 最里面全黑的一块）
constexpr int kColumns = 120;          // 一圈经线切几列（另加每个球冠左右两端的切点）
struct Stripe {
    double hiAlt = 0, loAlt = 0;                      // 太阳高度在 [loAlt, hiAlt) 之间（最里面那块 loAlt = -90）
    double opacity = 0;                               // 不透明度（不和别的叠）
    std::vector<std::vector<geo::LatLng>> polygons;   // 每个是一个闭合外环（经度连续展开）
};
// 经线 lng 和"离 c 不到 r 度"的球冠的交：纬度区间 [lo, hi]（夹到 ±kMaxLat）；没有交返回 false
bool capOnMeridian(geo::LatLng c, double r, double lng, double *lo, double *hi);
std::vector<Stripe> stripes(geo::LatLng sun, int n = kStripes, int columns = kColumns);
// 某点落在哪条带里（测试用；-1 = 不在任何一条里）
int stripeAt(const std::vector<Stripe> &s, double lat, double lng);
QString toGeoJson(const std::vector<Stripe> &s);
// 交给 maplibre 的 GeoJSON（每层一个 Polygon，properties.a = 这一层的不透明度）
QString toGeoJson(const std::vector<Band> &bands);

} // namespace tp::map::night
