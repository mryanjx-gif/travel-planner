// 地球上的路线（0.9.5，阶段 ③ 第 5 批）：从 TripData 的路线几何生成三维折线，渲染（globeitem.cpp）、碰线（MapController::lineAt）、
// 交通小图标（GlobeItem 的 vehicle）共用一份。只依赖 Qt Core（静态库 tp_map），单元测试 tst_map 直接测。
//
// - 每段和平面同一条线（大圆弧每 1.5° 一个点；算过道路的段是存下的道路折线），再细分到相邻两点不超过 1°，球面上不会切到球里面；
// - 飞行段抬起成弧线（C3，PO 选 Q1 A）：弧高 = 段长 × 12%，最高 1500 km，沿段按 sin(π t) 抬（t = 走过的角度 / 全段角度）；
//   没填交通方式的段按飞行算（roads::legModeOf，和 Electron 一样），也会抬起，短的段抬得很低；
// - 屏幕上：投影 → 被球挡住的部分去掉（边界处二分找到正好被挡住的那一点，边缘不会一跳一跳）。
#pragma once

#include "globegeom.h"
#include "tripdata.h"

#include <QString>
#include <memory>
#include <vector>

namespace tp::map::globe {

constexpr double kLiftRatio = 0.12;      // 弧高 / 段长
constexpr double kLiftMaxKm = 1500.0;
constexpr double kStepDeg = 1.0;         // 细分：相邻两点最多 1°

// 弧高（地球半径为 1），angle = 段两端的球心角（弧度）
double liftHeight(double angle);

struct RouteLeg {
    int stop = 0;            // 起点站序号（A→B 的 A）
    QString mode;            // flight / train / ferry / drive / walk
    QString color;           // 线的颜色（跟起点站）
    bool lifted = false;     // 飞行段：抬起
    double angle = 0;        // 段两端的球心角（弧度）
    std::vector<Vec3> pts;   // 三维点（抬起的段在球面外）
    std::vector<double> t;   // 每个点走过的比例 0..1（按角度）
    // 0.9.6：粗筛用（整段都在地平线以外就不投影）：中间方向、各点离它最远的角度、离球心最远的距离
    Vec3 mid;
    double cap = 0, maxR = 1;
};

struct RoutePlan {
    QString id;
    int index = 0;           // TripData::plans() 的下标
    int dash = 0;            // 0 实线 / 1 虚线 / 2 点线（和平面一样）
    std::vector<RouteLeg> legs;
};

struct Routes {
    std::vector<RoutePlan> plans;
    int points = 0;          // 一共多少个三维点（计时 / 自检参考）
};

std::shared_ptr<const Routes> buildRoutes(const TripData &trips);

// 0.9.6：这一段可能看得见吗（粗筛：整段都在地平线以外——抬起的点能多看到 acos(1/r)——就不用投影）
bool legMaybeVisible(const View &v, const RouteLeg &leg);

// 屏幕折线：一段段连续看得见的部分（像素）
struct Pt2 { double x = 0, y = 0; };
using ScreenRun = std::vector<Pt2>;
// pts 投到屏幕上；被球挡住的部分去掉。tOut（可选）：每个屏幕点对应的折线参数（第几个点 + 小数），给小图标找位置用
std::vector<ScreenRun> screenRuns(const Camera &cam, const std::vector<Vec3> &pts, std::vector<std::vector<double>> *tOut = nullptr);
// 点 (x, y) 到这些屏幕折线的最近距离（像素）
double distToRuns(const std::vector<ScreenRun> &runs, double x, double y);
// 一段在屏幕上看得见的长度（像素）
double runsLength(const std::vector<ScreenRun> &runs);

// 交通小图标（G1，PO 选，备注里扩成按交通方式）：选中计划时一个图标按站点顺序把整条行程走一遍。
// 每段时长按屏幕长度（1.5–5 秒）；屏幕上短于 40 像素的段跳过；到站停 0.3 秒，走完停 1 秒重来。
struct VehicleLeg { int leg = 0; double ms = 0; };
struct VehiclePlan { std::vector<VehicleLeg> legs; double totalMs = 0; };
constexpr double kVehicleMinPx = 40, kVehicleMinMs = 1500, kVehicleMaxMs = 5000, kVehiclePxPerMs = 0.16;
constexpr double kVehicleStopMs = 300, kVehicleEndMs = 1000;
VehiclePlan planVehicle(const std::vector<double> &legScreenPx);
// 在时刻 ms（已经对 totalMs 取过余）落在哪一段、段内走到哪（0..1）；停站 / 停在终点时返回 false
bool vehicleAt(const VehiclePlan &vp, double ms, int *leg, double *frac);
// 一段折线上按比例（走过的角度）取点和前进方向（三维）
Vec3 pointAlong(const RouteLeg &leg, double frac, Vec3 *ahead = nullptr);

} // namespace tp::map::globe
