// 站点编辑器的纯计算部分（0.8.9，功能清单 M-23 / M-24 / M-18 编辑时单段）。只依赖数据层，单元测试 tst_map 覆盖。
// 照搬 Electron v6.3.0 map-app.js：
//   blankPlan / addStop / moveStop / validateEditor / suggestRegion / suggestPlanRegions / legModeOf 的选择（bind LegEvents）、
//   setOpt（样式"不单独设置"就删掉键）、标记样式的默认值（renderPlanStyle / refreshStopStyle）。
// 数据按原生格式（claude/数据格式.md）：站点顺序 = 数组顺序（没有 order）、交通方式在起点站点的 leg{ mode, path, dist, time, key }、
// 封面是 cover（照片 id）。编辑器只动它管的那些键，计划和站点里别的东西（交通住宿、消费、行程安排、照片归属……）原样保留。
#pragma once

#include "jsonvalue.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace tp::map::planedit {

// 新计划（Electron blankPlan；coverPhoto 换成原生的 cover）。createdAt = 毫秒
json::Value blankPlan(const QString &id, double createdAtMs);
// 新站点（Electron addStop：坐标保留 6 位小数、经度先归一到 ±180；原生版没有 order、photos）
json::Value newStop(const QString &id, const QString &name, double lat, double lng);
// JS 的 +(+v).toFixed(6)
double round6(double v);
// 经度归一到 [-180, 180)（Electron normLng）
double normLng(double lng);

// ---- 校验（Electron validateEditor）----
struct Problems {
    bool nameEmpty = false;        // 请填写计划名称
    bool dateOrder = false;        // 结束日期早于开始日期
    bool noStops = false;          // 至少需要一个站点
    QStringList badCoords;         // 坐标无效的站点 id（纬度 -90~90，经度 -180~180）
    bool ok() const { return !nameEmpty && !dateOrder && !noStops && badCoords.isEmpty(); }
};
Problems validate(const json::Value &plan);

// ---- 区域建议（Electron suggestRegion / suggestPlanRegions：只按经纬度粗略判断）----
// 返回区域名（中文键，和 plans::allRegions 一样），认不出返回空
QString suggestRegion(double lat, double lng);
QStringList suggestPlanRegions(const json::Value &plan);

// ---- 改计划（都只改传进来的 plan；返回是否有变化）----
// 计划的普通字段：name / type / status / startDate / endDate / notes / color / markerMode。
// color、markerMode 空 = 删掉键（Electron setOpt；markerMode 'number' 也删）
bool setField(json::Value &plan, const QString &key, const QString &value);
bool setRegions(json::Value &plan, const QStringList &regions);   // 按 REGIONS 的顺序写
// 标记样式：空 = 不单独设置（删掉键）。stopIndex < 0 = 计划本身
bool setStyle(json::Value &plan, int stopIndex, const QString &color, const QString &shape, const QString &icon);
// 站点字段：name / arriveDate / departDate / notes（notes 空 = 删掉，Electron 一样）
bool setStopField(json::Value &plan, int i, const QString &key, const QString &value);
bool setStopLatLng(json::Value &plan, int i, double lat, double lng);   // 拖动 / 手动输入（保留 6 位小数）
// 交通方式（Electron leg-mode onchange）：flight = 删掉 mode；不是自驾 / 徒步时清掉算好的折线；leg 空了就删掉
bool setLegMode(json::Value &plan, int i, const QString &mode);
bool moveStop(json::Value &plan, int from, int to);   // 挪到 to（Electron ↑ ↓ 是相邻交换；拖动排序是任意位置）
bool removeStop(json::Value &plan, int i);
int appendStop(json::Value &plan, const json::Value &stop);   // 返回新下标
// 改进 g（PO 0.8.9 选）：新建的计划加到第二站时类型自动改成路线。返回是否改了
bool autoRouteOnSecondStop(json::Value &plan);

// 写回整份数据：替换同 id 的计划（位置不变），没有就加在最后。返回是否有变化
bool putPlan(json::Value &root, const json::Value &plan);
bool removePlan(json::Value &root, const QString &planId);
const json::Value *findPlan(const json::Value &root, const QString &planId, int *index = nullptr);

// ---- 每段的状态（编辑器里交通方式那一行；Electron refreshLegRows 的判断，"正在算 / 失败"由调用方叠加）----
struct LegRow {
    int from = -1;               // 起点站点下标（stops 数组）
    QString mode;                // flight / train / ferry / drive / walk
    bool road = false;           // 自驾 / 徒步
    bool badCoords = false;      // 坐标无效，按直线显示
    bool computed = false;       // 有和当前坐标对得上的折线
    double dist = 0, time = 0;   // 算好时的米、秒
    QString key;                 // legKeyOf（自驾 / 徒步且坐标有效时）
    double straightKm = 0;       // 两站的大圆距离（坐标有效时）
};
// 路线里相邻两站之间的每一段（只有 route 才有；按 stops 数组相邻，不跳过坐标无效的——和 Electron 编辑器一样）
QVector<LegRow> legRows(const json::Value &plan);

// ---- 改进 e：编辑器顶上的总里程 ----
struct Stats {
    int stops = 0;
    double km = 0;               // 自驾 / 徒步段算好的用道路距离，其余（含没算好的）用大圆距离
    bool approx = false;         // 有自驾 / 徒步段还没算好
    double driveSec = 0, walkSec = 0;
    bool driveApprox = false, walkApprox = false;   // 有自驾 / 徒步段还没算好（时长不全）
    bool hasDrive = false, hasWalk = false;
};
Stats stats(const json::Value &plan);
double haversineKm(double lat1, double lng1, double lat2, double lng2);

// 站点的摘要（Electron stopSummary 的计数部分）：{ schedule: n, photos: n（照片库里归属这一站的）, notes: bool }
QVariantMap stopCounts(const json::Value &root, const QString &planId, const json::Value &stop);

// "此刻"在路上时今天所在的站点（Electron locateFallback：stopOn 的 st[max(0, i)]）。{ name, lat, lng }；不在路上返回空
QVariantMap todayStop(const json::Value &root, const QString &today);

} // namespace tp::map::planedit
