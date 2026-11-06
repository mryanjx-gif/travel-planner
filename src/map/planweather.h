// 天气、往年同月气候、海拔、高反的计算（0.10.3，N-01–N-04；移植 Electron weather.js）。只依赖 Qt Core 和数据层，单元测试 tst_map 共用。
// 联网和缓存在 WeatherController（预报、气候）和 PlanPageController（补海拔）；这里只有 URL、回复解析和判断。
#pragma once

#include "jsonvalue.h"
#include "planpage.h"

#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

namespace tp::map::plans {

// ---- 预报 ----
struct GeoPoint { QString key; double lat = 0, lng = 0; };
QString geoKey(double lat, double lng);                                 // 同一个地点（两位小数，约 1 km）只问一次
bool inForecastWindow(const json::Value &plan, const QString &today);   // 16 天内出发或在途、没完成（inWindow）
QString stopWxDate(const json::Value &stop, const QString &today);      // 到达日期；正在这一站时用今天
QString wmoKind(int code);                                              // clear / partly / cloudy / fog / drizzle / rain / snow / showers / snowShowers / storm / unknown
QUrl forecastUrl(const QVector<GeoPoint> &part);                        // 一批（Electron 每批 50 个）
QVector<QJsonObject> parseDaily(const QByteArray &body, int count);     // 每个地点的 daily；出错的留空。只有一个地点时回复不是数组
QVariantMap wxDay(const QJsonObject &daily, const QString &date);       // { date, kind, lo, hi, pop? }；没有这一天返回空

// ---- 往年同月气候（只在点按钮时用）----
struct ClimPoint { QString key; double lat = 0, lng = 0; int month = 0; };
struct ClimPlan { QVector<QString> stopKeys; QVector<ClimPoint> points; };   // stopKeys 和有效站点一一对应，超出 12 个点的留空
// 按月份分组，每个月里约 30 km 内的站点合并，最多 12 个点（showClimate、clusters）
ClimPlan climPlan(const json::Value &plan, const QString &today);
QVector<int> climYears(int month, const QString &today);                // 最近 10 个完整的同月（ERA5 约晚 5 天）
QUrl climateUrl(const QVector<ClimPoint> &points, int year, int month);
struct ClimAcc { double hi = 0, lo = 0, mm = 0; int nHi = 0, nLo = 0, rainDays = 0, years = 0; };
void climAdd(QVector<ClimAcc> &acc, const QByteArray &body);            // 一年的回复累加进去，每个点一项
QJsonObject climResult(const ClimAcc &a, const QVector<int> &years);    // { hi, lo, rainDays, mm, years }；一年都没有时为空

// ---- 海拔 ----
bool stopElev(const json::Value &stop, double *out);                    // 这一站的海拔（空字符串、null 都算没有）
QVector<TzStop> stopsWithoutElev(const json::Value &plan);              // 只补空的
QUrl elevUrl(const QVector<TzStop> &part);                              // 一批（Electron 每批 100 个）
QVector<double> parseElev(const QByteArray &body, int count);           // 认不出的是 NaN
int applyElev(json::Value &root, const QString &planId, const QStringList &ids, const QVector<double> &elev);   // 只写还空着的
// 手动改：空 = 去掉（之后重新取）；否则四舍五入成整数。badElev = 不是 -500–9000 的数字（什么都不改）
QString setStopElev(json::Value &plan, int k, const QString &text);

// ---- 高反（altitude）----
// { warnings: [{ kind: "rise", from, to, fromE, toE, gain, days } 或 { kind: "run", stop, nights }]（站号从 1 开始），
//   tags: 每个有效站点一项（"" / rise / high），approx: 有的站没写日期，按每站住一晚估算 }
QVariantMap altitudeInfo(const json::Value &plan);

} // namespace tp::map::plans
