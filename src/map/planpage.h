// 完整计划页的计算（0.10.0，P-01–P-06）：只依赖数据层，单元测试 tst_map 共用。界面文字由 QML 翻译，这里只给结构化的数据。
//   - pageInfo：页面要的全部内容（sheetInfo + 预计天数、季节条、证件警告、每站的日出日落、按天分组的日程、今天）；
//   - schedDays / putSchedule / removeSchedule：日程的显示和修改（站点卡片和编辑器共用）；
//   - setDays / startPlanning：页头的两个操作；
//   - 补时区（Electron weather.js fillGeo 的时区那一半）：只补空的，Open-Meteo 预报接口 timezone=auto。
#pragma once

#include "jsonvalue.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

namespace tp::map::plans {

// 按天分组（PO 选 I2，只改显示）：[{ date, items: [{ j（数组里的下标）, date, time, end, text, today }] }]。
// 日期从早到晚，没写日期的放最后；同一天里没写时间的在前，其余按时间排
QVariantList schedDays(const json::Value &stop, const QString &today);

// 新加（j < 0）或修改一条。返回空 = 改好了；否则是原因的键：
//   empty 什么都没填（什么都不改）/ badDate / badTime 时间不对 / endNoStart 填了结束没填开始（Electron 的提示）。
// 时间认 9:30、09:30、0930，一律存成 09:30；这一条里别的键原样保留
QString putSchedule(json::Value &stop, int j, const QString &date, const QString &time, const QString &end, const QString &text);
bool removeSchedule(json::Value &stop, int j);

// 有效站点（validStops 的第 k 个）在 stops 数组里的那个站点（可以改）；没有返回 nullptr
json::Value *mutValidStop(json::Value &root, const QString &planId, int k);

// 页面的全部内容。没有这个计划返回空
QVariantMap pageInfo(const json::Value &root, const QString &planId, const QString &today);

// 证件警告（travelkit.js docWarnings）：[{ kind: passportExpired / passport6m / doc, owner, docKind, label, expires, end }]
QVariantList docWarnings(const json::Value &root, const json::Value &plan);
// 季节条（store.js seasonStrip）：{ name, nameEn, cells: 12 个 { kind, text, textEn } }；表里没有这个地区返回空
QVariantMap seasonInfo(const json::Value &root, const QString &ref);

// 预计天数：空 = 去掉；1–999（四舍五入）。返回空 = 改好了或没变化；badDays = 不在范围里（什么都不改）
QString setDays(json::Value &root, const QString &planId, const QString &text);
// 开始计划（想去 → 计划中）：填了出发才改日期，没填回来时回来 = 出发（Electron 的 start）
bool startPlanning(json::Value &root, const QString &planId, const QString &start, const QString &end);

// ---- 补时区 ----
struct TzStop { QString id; double lat = 0, lng = 0; };
QVector<TzStop> stopsWithoutTz(const json::Value &plan);    // 有效站点里 tz 为空的
QUrl tzUrl(const QVector<TzStop> &part);                    // 一批（Electron 每批 50 个）
// 回复 → 每站的时区名（认不出的留空；只有一个地点时回复不是数组）
QStringList parseTz(const QByteArray &body, int count);
// 只写还空着的（站点可能已经被删或者手动填了）。返回写了几个
int applyTz(json::Value &root, const QString &planId, const QStringList &ids, const QStringList &tz);

} // namespace tp::map::plans
