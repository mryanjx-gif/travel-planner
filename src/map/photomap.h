// 照片模式和相册的计算（0.11.1，A-01–A-04；PO 第 4、5 题 A）。只依赖数据层，单元测试 tst_map 共用。
// - read：把 photos[] 连上计划和站点。位置先用照片自己的 GPS，没有就用站点的（fromStop），都没有的算"没有位置"；
// - cluster：某一级缩放上按屏幕距离合并（Electron clusterRadius 70），代表图优先精选；
// - rows：相册的行（按旅程 / 按年份 / 精选 / 未归类）。照片按 columns 切成行，界面只建屏幕上的行。
#pragma once

#include "jsonvalue.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace tp::map::photomap {

struct Rec
{
    QString id, file, caption, plan, stop, planName, stopName, takenAt;
    QString day;           // takenAt，没有就用站点到达日、计划开始日（Electron dayOf）；可能是空的
    int stopNo = 0;        // 第几站（1 起）；没归到站点是 0
    bool route = false, fav = false;
    bool hasPos = false, fromStop = false;
    double lat = 0, lng = 0;
};
struct Trip
{
    QString id, name, start, end;
    bool completed = false;
    int count = 0;
    int rep = -1;          // 代表图（recs 下标）：优先精选
};
struct Library
{
    QVector<Rec> recs;
    QVector<Trip> trips;   // 有照片的计划：已完成的在前，再按出发日期新的在前（Electron tripHtml）
    int pinned = 0, noPos = 0, favs = 0, inbox = 0, heic = 0;
};
Library read(const json::Value &root);
bool isHeic(const QString &file);

struct Group
{
    int rep = 0;           // recs 下标（精选优先，其次库里靠前的）
    QVector<int> members;  // recs 下标
    double lat = 0, lng = 0;
    bool samePlace = true; // 全在同一点（同一站的照片）：放大也分不开，点开直接看大图
};
// 有位置的照片按 zoom 级、radius 像素合并。ponytail: 不跨 ±180° 经线合并，那里两边各一个标记，无害
QVector<Group> cluster(const QVector<Rec> &recs, int zoom, double radiusPx = 70);

constexpr const char *kNoPos = "__nopos";   // 按旅程里"没有位置"那一行的 id

struct Row
{
    QString kind;          // trip（窄列表的一趟）/ plan / stop / year / month / photos
    QString title, planId, sub;   // sub：plan 是日期；month 是 01–12
    int stopNo = 0, count = 0, rep = -1;
    int group = -1;        // photos：同一组（看大图翻页的范围）
    QVector<int> recs;     // photos：这一行的照片
    bool open = false;     // trip：展开着
};
// view：trip / year / fav / inbox；plan：只看这一趟（""= 全部，kNoPos = 没有位置）；
// compact：窄列表，按旅程时每趟一行，只展开选中的那趟
QVector<Row> rows(const Library &lib, const QString &view, const QString &plan, bool compact, int columns);

} // namespace tp::map::photomap
