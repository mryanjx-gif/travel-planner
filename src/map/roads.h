// 道路路线（0.8.4，功能清单 M-18 / M-19）：自驾 / 徒步段按道路画，结果存进数据（stops[].leg）。
// 照搬 Electron v6.3.0 map-app.js：decodePolyline / encodePolyline / simplifyPts / legKeyOf / legModeOf /
// pendingLegs / allPendingRoadLegs / fetchRoad（URL 和返回值的处理），只依赖 Qt Core 和数据层，tst_map 覆盖。
//
// 数据（原生格式，见 claude/数据格式.md）：起点站点上的 leg = { mode, path, dist, time, key }
//   mode  'flight'（默认，不写）| 'train' | 'ferry' | 'drive' | 'walk'
//   path  Google encoded polyline（精度 1e-5，已抽稀）；key = "方式:起点纬度,起点经度,终点纬度,终点经度"（各 5 位小数）
//   key 和当前坐标对不上 = 站点挪过，按直线画、算作"还没算"
#pragma once

#include "geo.h"
#include "jsonvalue.h"

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVector>

namespace tp::map::roads {

constexpr double kSimplifyDeg = 0.00015;   // Electron ROAD_SIMPLIFY_DEG：抽稀容差约 15 米
constexpr int kMinIntervalMs = 1100;       // 公共服务器要求低频：两次请求至少隔 1.1 秒
constexpr int kTimeoutMs = 20000;          // Electron fetchRoad：20 秒超时
constexpr int kFailKeep = 500;             // 记住"算不出来"的段最多 500 个（Electron rememberRoadFail）

// JS 的 Number.prototype.toFixed（digits ≤ 20；|v| < 1e21）：两个候选一样近时取大的那个（按绝对值，负数先取反）
QString jsToFixed(double v, int digits);

QVector<geo::LatLng> decodePolyline(const QString &str);
QString encodePolyline(const QVector<geo::LatLng> &pts);
// Douglas-Peucker（经度按纬度余弦缩放，近似等距），tol 单位是度
QVector<geo::LatLng> simplify(const QVector<geo::LatLng> &pts, double tolDeg);

// 不认识的方式按飞机
QString legModeOf(const QString &mode);
bool isRoadMode(const QString &mode);   // drive / walk
QString legKeyOf(const QString &mode, double aLat, double aLng, double bLat, double bLng);

// 一段还没按道路算的路（Electron pendingLegs 的一项）
struct Pending {
    QString planId, planName;
    int stopIndex = -1;       // 起点站点在 stops 数组里的下标（写回用）
    int legNo = 0;            // 第几段（有效站点里的序号，从 0 开始；给人看的）
    QString mode, key;
    double aLat = 0, aLng = 0, bLat = 0, bLng = 0;
};
// 所有计划里还没算的段：路线、自驾 / 徒步、坐标有效、没有和当前坐标对得上的折线、不在 failed 里
// （Electron allPendingRoadLegs；"本次编辑里失败过"的那部分由调用方并进 failed）
QVector<Pending> pendingLegs(const json::Value &root, const QSet<QString> &failed);
// 把算好的结果写进 stops[stopIndex].leg（只有方式和坐标还对得上时才写；保留 leg 里其他的键）。返回是否写了
bool applyLeg(json::Value &root, const Pending &leg, const QString &path, double dist, double time);

// OSRM（routing.openstreetmap.de，FOSSGIS 运营的公共服务器）
QUrl osrmUrl(const QString &mode, double aLat, double aLng, double bLat, double bLng);
struct OsrmResult {
    enum Kind { Ok, NoRoute, TooBig, ServiceError, BadReply } kind = BadReply;
    QString path;             // 抽稀后重新编码
    double dist = 0, time = 0;   // 米、秒（取整）
    QString detail;           // ServiceError：服务给的 message / code；BadReply：HTTP 状态码
    bool permanent() const { return kind == NoRoute || kind == TooBig; }   // 再算也一样，记住不再提示
};
OsrmResult parseOsrm(const QByteArray &body, int httpStatus);

} // namespace tp::map::roads
