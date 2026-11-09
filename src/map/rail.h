// 火车类交通沿真实线路画（1.3.0，1.1设计预案.md "# 1.3"）：查软件自带的离线线路数据（assets/rail/，
// 取自 OpenStreetMap，© OpenStreetMap 贡献者，ODbL；生成脚本 tools/rail-data.py），两站各自吸附到线路上
// （≤1.5 km），在线路图上找最短路，截出那段几何。查不到就给原因，调用方画直线。
//
// 不写进数据：每次画的时候现算，按 legKeyOf 缓存在内存里（同一段只算一次）。不联网。
#pragma once

#include "geo.h"

#include <QString>
#include <QVector>

namespace tp::map::rail {

constexpr double kSnapM = 1500;   // 车站离线路超过这么远 = 不在线路上

bool isRailMode(const QString &mode);   // train（数据里火车、地铁、轻轨、电车都记成 train）

struct Result {
    QVector<geo::LatLng> pts;   // 沿线路的点（从起点吸附点到终点吸附点）；空 = 画直线
    double distM = 0;           // 沿线路的长度（米）
    // 空 = 成功；nodata 这座城市还没有线路数据；out 超出线路数据范围（跨城市）；far 站点离线路太远；nopath 两站之间不连通
    QString reason;
    bool ok() const { return reason.isEmpty(); }
};
Result route(double aLat, double aLng, double bLat, double bLng);

// 数据所在的文件夹（默认资源 :/rail；单元测试指向源码里的 assets/rail）。换文件夹会清掉缓存
void setDataDir(const QString &dir);

} // namespace tp::map::rail
