// 地图的几何计算（0.8.0）：墨卡托投影、相机（中心 + 缩放 + 窗口大小）、像素和经纬度互换、
// 按范围缩放（fitBounds）、动态最小缩放、飞行路径。只依赖标准库，单元测试 tst_map 覆盖。
//
// 为什么自己算：maplibre-native-qt 399311e 的 QML 地图项不提供像素换算，飞行时也不更新中心和缩放
// （见 claude/v0.8.0记录.md §2）。原生版自己管相机，把中心和缩放每一帧交给地图，QML 覆盖层用同一份数据定位。
// 约定和 maplibre-native 一致：瓦片 512 逻辑像素，缩放 z 时整个世界宽 512·2^z 像素，中心在窗口正中，北朝上、不旋转。
#pragma once

#include <cmath>
#include <vector>

namespace tp::map::geo {

constexpr double kPi = 3.14159265358979323846;
constexpr double D2R = kPi / 180.0;
constexpr double R2D = 180.0 / kPi;
constexpr double kTile = 512.0;                    // maplibre-native 的瓦片大小（逻辑像素）
constexpr double kMaxLat = 85.0511287798066;       // 墨卡托能画到的纬度
constexpr double kEarthRadius = 6371008.8;         // maplibre-gl LngLat.distanceTo 用的地球半径（米）

struct LatLng { double lat = 0, lng = 0; };
struct Pt { double x = 0, y = 0; };

// 经度归一到 [-180, 180)；把 lng 挪到离 ref 最近的那一份（±360）
double normLng(double lng);
double unwrapTo(double lng, double ref);

// 墨卡托：经纬度 → [0,1]² 的世界坐标（x 向东，y 向南）。纬度先夹到 ±kMaxLat
Pt project(double lat, double lng);
LatLng unproject(double x, double y);

// 两点间的大圆距离（米，haversine，和 maplibre-gl distanceTo 一样）
double distanceMeters(LatLng a, LatLng b);
double distanceKm(LatLng a, LatLng b);   // Electron distKm（地球半径 6371 km）

// 大圆弧：≥3° 时每 1.5° 取一个点（Electron greatCircle）
std::vector<LatLng> greatCircle(LatLng a, LatLng b);
// 一组点的球面平均中心（Electron centerOf）
LatLng centerOf(const std::vector<LatLng> &pts);

struct Padding { double top = 0, right = 0, bottom = 0, left = 0; };

struct Camera {
    double lat = 25, lng = 100, zoom = 1.2;   // Electron 初始 center [100, 25]、zoom 1.2
    double width = 800, height = 600;          // 地图项的逻辑像素大小

    double worldSize() const { return kTile * std::exp2(zoom); }
    // 经纬度 → 窗口像素。lng 自动挪到离中心最近的那一份世界副本（标记跟着最近的副本走，和 maplibre-gl 一样）
    Pt pixelFor(double lat, double lng) const;
    // 不挪副本的版本（画线、算范围用）
    Pt pixelForRaw(double lat, double lng) const;
    LatLng coordinateFor(double px, double py) const;
    // 把窗口上的 (px,py) 这一点固定不动、缩放到 newZoom
    void zoomAround(double newZoom, double px, double py);
    // 拖动：地图内容跟着鼠标移动 (dx, dy) 像素
    void panBy(double dx, double dy);
    // 每像素多少米（窗口正中左右各 50 像素量的，和 Electron updateMinZoom 一样）
    double metersPerPixelAtCenter() const;
};

// maplibre-native 默认的"只限高度"约束：世界的上下边不能露出来。
// zoom 至少 log2(高度/512)，中心纬度夹到上下刚好贴边。返回约束后的相机
Camera constrain(Camera c, double minZoom, double maxZoom);

// Electron updateMinZoom（平面）：比例尺最多到 1000 km——在窗口正中量"100 像素是多少米"，反推出最小缩放，夹到 [0, 6]
double minZoomForScale(const Camera &c, double minScaleKm = 1000.0);

// maplibre-gl fitBounds（不旋转）：把 [west,south]–[east,north] 放进去掉 padding 后的区域，缩放不超过 maxZoom。
// east 可以 > 180（跨 180° 经线时先把经度展开）
struct FitResult { double lat = 0, lng = 0, zoom = 0; bool ok = false; };
FitResult fitBounds(const Camera &c, double west, double south, double east, double north, Padding pad, double maxZoom);
// Electron fitPadding：左右按 left / right，再按窗口大小缩小；上下 min(40, (H-80)/2)
Padding fitPadding(double width, double height, double left, double right);

// maplibre-gl flyTo（van Wijk & Nuij 2003，curve ρ = 1.42）：返回飞行时间 t∈[0,1] 时的相机中心和缩放。
// 只在起点算一次参数，之后每一帧按 t 取值
class FlyPath {
public:
    FlyPath(const Camera &from, double toLat, double toLng, double toZoom, double curve = 1.42);
    void at(double t, double *lat, double *lng, double *zoom) const;
    // maplibre-gl：duration = 1000 · S / speed（speed 1.2）；S 是路径长度（ρ 单位）
    double naturalDurationMs(double speed = 1.2) const;

private:
    double m_startZoom = 0, m_rho = 1.42;
    Pt m_from, m_delta;             // 世界坐标（0..1），终点经度已挪到离起点最近
    double m_w0 = 1, m_w1 = 1, m_u1 = 0, m_r0 = 0, m_S = 0, m_scale = 1;
    bool m_simple = false;          // 起点终点几乎重合：只改缩放
    double w(double s) const;
    double u(double s) const;
};

// 瓦片编号（0.8.5，Electron tileXY：x 按世界副本折回，y 夹在 0..n-1）
struct TileXY { int z = 0, x = 0, y = 0; };
TileXY tileXY(double lng, double lat, int z);
// Electron prefetchTiles 取哪些瓦片（不含"只预取一次"和 48 张上限）：当前视野上下各一级（先 +1 级、再 -1 级），
// zoomBase = round(缩放 + log2(256 / 瓦片像素))（128 时 = Electron 的 +1）；级数 < 1 或 > tileMaxZoom 的不要；一级超过 200 张就跳过这一级
std::vector<TileXY> prefetchTiles(double west, double north, double east, double south, double zoom, int tileSize, int tileMaxZoom);
// 卫星瓦片大小：固定 128（和 Electron 一样，最清楚）。0.8.6 曾按屏幕缩放比例取 128–256 省内存，
// PO 2026-11-05 实际看了觉得"有点糊"、"要清楚，内存多占一点没关系"，改回 128；参数留着，以后要做成设置项时用
int satTileSize(double dpr);

double smoothstep(double t);   // t·t·(3-2t)
// maplibre-gl 的默认缓动 bezier(.25, .1, .25, 1)（easeTo / flyTo 不给 easing 时用这个）
double easeDefault(double t);
double easeCubicInOut(double t);

} // namespace tp::map::geo
