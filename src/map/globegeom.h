// 地球的几何计算（0.9.0，阶段 ③）：相机、投影、拾取、按视野选瓦片、和平面地图缩放级别的换算。
// 只依赖 Qt Core（静态库 tp_map），渲染那边（globeitem.cpp）用同样的相机，单元测试 tst_map 直接测。
//
// 约定：地球半径 = 1，世界坐标 x 指向 0°E、z 指向北极；相机在 (lat, lng) 方向、离球心 dist 处，看向球心，
// 上方向 = 地轴（两极附近改用 y）；竖直视角 35°（和 7.0.0-b 原型一样）。
// 瓦片是 Web 墨卡托（和 Esri / maplibre 一样的 z / x / y，y 从北往南），纬度只到 ±85.0511°，两极另外由底球补上。
#pragma once

#include <QtGlobal>
#include <array>
#include <vector>

namespace tp::map::globe {

constexpr double kFovYDeg = 35.0;
constexpr double kMaxMercLat = 85.0511287798;
constexpr double kEarthRadiusM = 6371008.8;
constexpr double kMinDist = 1.06;    // 最近（约相当于平面 7 级；0.9.3 自动摊平以后由平面接手）
constexpr double kMaxDistCap = 8.0;

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3 &o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3 &o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(double k) const { return { x * k, y * k, z * k }; }
    double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3 &o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
    double length() const;
    Vec3 normalized() const;
};

Vec3 toVec(double lat, double lng);
void toLatLng(const Vec3 &v, double *lat, double *lng);

// 墨卡托（归一化：x、y 都在 0..1，y 从北往南）↔ 经纬度
double mercX(double lng);
double mercY(double lat);
double lngOfMercX(double x);
double latOfMercY(double y);

struct View {
    double lat = 30, lng = 105, dist = 4.2;
    double width = 800, height = 600;   // 像素（选瓦片用设备像素，缩放换算用逻辑像素）
    // 0.9.7 变形过渡（C1）：0 = 球，1 = 平面（以屏幕中心为切点、同一比例尺的墨卡托平面，和 maplibre 同一镜头逐像素对齐）；
    // 中间按比例混合两种屏幕位置。选瓦片不看它（瓦片在着色器里混合）
    double morph = 0;
    Vec3 eye() const { return toVec(lat, lng) * dist; }
    double focal() const;   // 竖直方向：屏幕中心每单位切线距离的像素数 = height / (2 tan(fov/2))
};

// 相机：视图 × 投影（双精度，和渲染那边一样，只是没有图形 API 的裁剪空间校正）
class Camera {
public:
    explicit Camera(const View &v);
    // 世界坐标 → 屏幕像素（左上角是原点）；在相机后面时返回 false
    bool project(const Vec3 &p, double *sx, double *sy) const;
    // 屏幕像素 → 球面上的点（射线和单位球求交，取近的那个）；没打到球返回 false
    bool pick(double sx, double sy, Vec3 *out) const;
    // 球面上的点从相机看得见（没被球自己挡住）：dot(p, eye) > 1
    bool facing(const Vec3 &p, double margin = 0.0) const;
    // 0.9.5：任意一点（可以在球面外，例如抬起的飞行弧线）被球挡住：视线在到达它之前先碰到球
    bool occluded(const Vec3 &p) const;
    // 0.9.7：变形到一半以上时不再判断被球挡住（平面上都看得见）
    bool morphVisible() const { return m_v.morph >= 0.5; }
    // 0.9.7：这一点在平面（变形终点）上的屏幕位置：墨卡托坐标相对屏幕中心 × 每单位的像素
    void flatProject(const Vec3 &p, double *sx, double *sy) const;
    // 0.9.7：变形时相邻两点在屏幕上横着跳这么远就是跨了 180° 经线（平面上各自挪到离中心近的那一份世界），线要断开
    double wrapJumpPx() const { return m_v.morph > 0 ? 0.5 * m_worldPx * m_v.morph : 1e300; }
    const View &view() const { return m_v; }

private:
    View m_v;
    Vec3 m_eye, m_fwd, m_right, m_up;
    double m_f = 1, m_aspect = 1;
    double m_mx0 = 0, m_my0 = 0, m_worldPx = 1;   // 0.9.7：屏幕中心的墨卡托坐标、平面上每单位墨卡托多少像素
};

// ---- 和平面地图的缩放级别换算（0.9.0：让屏幕中心的比例尺一致，0.9.3 自动摊平要接得上） ----
// 平面：每弧度地面（纬度 φ 处）= kTile·2^z / (2π) / cos φ 像素；地球：屏幕中心每单位地面 = f / (dist - 1) 像素
double zoomForDist(double dist, double focalLogical, double lat);
double distForZoom(double zoom, double focalLogical, double lat);
// 最远能缩到多远（Electron updateMinZoom 的球形：屏幕中心 100 像素最多约 2000 km，留 3% 余量）
double maxDistFor(double focalLogical);
// 0.9.7：这个相机距离下平面上每单位（归一化）墨卡托多少像素 = 2π·cos(中心纬度)·f / (dist − 1)；
// 等于 maplibre 在 zoomForDist 那一级的世界宽度（512·2^z），变形的终点就是那一级的平面地图
double flatWorldPx(double dist, double focal, double lat);

// ---- 0.9.5 地球上的内容 ----
// 地平线：从相机看，球面上离视线中心多少弧度的点正好在边上 = acos(1 / dist)
double horizonAngle(double dist);
// 背面渐隐（C4）：点离地平线 8° 以内渐渐变透明（1 = 完全看得见，0 = 过了地平线）；
// 边缘变小（G3，PO 选）：离地平线 25° 以内从 100% 缩到 70%
constexpr double kLimbFadeDeg = 8.0, kLimbShrinkDeg = 25.0, kLimbMinScale = 0.7;
struct Limb { double fade = 1, scale = 1; };
Limb limbOf(const View &v, double lat, double lng);
// 这一点到地平线还有几度（负数 = 已经在背面）
double degreesInside(const View &v, double lat, double lng);
// 以 (ax, ay) 为中心缩放（0.9.5 修极点抽搐）：换距离前后各拾取一次，把"换距离后鼠标下的点"转回"换距离前的点"
// （最小旋转作用在中心方向上）；中心纬度最多 ±maxLat，转到上限就停在那里（剩下的不转，不绕极点打转）；
// 中心在 |纬度| 70° 以上时补偿渐渐减小，84° 以上完全以画面中心缩放。
// 两次都拾取到才改中心，返回 true
bool zoomAnchorCenter(const View &before, double newDist, double ax, double ay, double maxLat, double *lat, double *lng);
// 相机高度（dist - 1）的对数插值：滚轮缩放的动画按它走，和纬度无关（换算成平面缩放级别的那个数含 1/cos 纬度，0.9.4 以前按它插值，
// 高纬度时中心一动距离就跳）
double lerpDist(double from, double to, double t);
// 中心沿大圆从 a 转到 b（t = 0..1，球面线性插值）
void slerpCenter(double lat0, double lng0, double lat1, double lng1, double t, double *lat, double *lng);
// 两点之间的球心角（弧度）
double angleBetween(double lat0, double lng0, double lat1, double lng1);

// 0.9.3 星空：Electron map-app.js 的 seededRand（mulberry32），同一个种子出同样的数（tst_map 对照）
std::vector<double> mulberry32(quint32 seed, int n);

// ---- 按视野选瓦片 ----
struct TileId {
    int z = 0, x = 0, y = 0;
    bool operator==(const TileId &o) const { return z == o.z && x == o.x && y == o.y; }
    TileId parent() const { return { z - 1, x >> 1, y >> 1 }; }
};

struct SelectOptions {
    int minZ = 2;            // 最粗一级（z2 = 16 块，每块 90° 经度，网格够细，球的轮廓是圆的）
    int maxZ = 12;
    double maxTilePx = 300;  // 一块 256 像素的瓦片在屏幕上超过这么大就换细一级（"清晰度优先"：纹素 ≥ 0.85 个屏幕像素）
    int budget = 150;        // 最多这么多块（显卡上的纹理层有限）；超了就整体放粗一点再选
};

struct Selection {
    std::vector<TileId> tiles;   // 要画的叶子瓦片
    int maxZ = 0;
    int visited = 0;             // 检查过多少块（性能参考）
    double usedTilePx = 0;       // 实际用的阈值（超预算时会放大）
};

Selection selectTiles(const View &v, const SelectOptions &opt = {});

} // namespace tp::map::globe
