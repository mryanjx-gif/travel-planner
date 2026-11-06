// 原生版自己的数据格式（0.6.0，PO 2026-11-03 定；完整说明见 claude/数据格式.md）。
//
// 数据文件夹里：
//   travel.json                               主数据（纯 JSON：UTF-8 无 BOM、LF、2 空格缩进，末尾一个换行）
//   travel.backup.json                        上一版（保存前复制）
//   travel.before-ai-YYYYMMDD-HHMMSS.json     AI 修改前的备份（保留 10 份）
//   travel.before-import-YYYYMMDD-HHMMSS.json "替换全部"导入前的备份
//   travel.unsaved-YYYYMMDD-HHMMSS.json       关闭时没能保存，另存的一份
//   .history/travel-YYYY-MM-DD.json           按天的历史版本（保留 14 份）
//   .lock                                     正在打开这份数据的程序（lockfile.h）
//   photos/                                   照片；photos/.originals/ 是旋转前的原图
//
// 顶层：{ format: "travelplanner", formatVersion: 1, savedBy, savedAt, plans, photos, journal, documents, visitedRegions, … }
// 版本规则：只加新字段时 formatVersion 不变（旧版本照常编辑，不认识的字段原样保留、位置不变）；
// 结构不兼容时才加 1，旧版本打开更高版本的文件只读。
#pragma once

#include "jsonvalue.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace tp::data::nativefmt {

inline constexpr int kFormatVersion = 1;
inline const char *const kFormatName = "travelplanner";

inline QString dataFileName() { return QStringLiteral("travel.json"); }
inline QString backupFileName() { return QStringLiteral("travel.backup.json"); }
inline QString tmpFileName() { return QStringLiteral("travel.json.tmp"); }
inline QString historyDirName() { return QStringLiteral(".history"); }
inline QString electronDataFileName() { return QStringLiteral("data.js"); }

// 新建空数据（第一次用 / "在这里新建空数据"）
json::Value emptyDoc(const QString &appVersion, const QDateTime &now);

enum class Check {
    Ok,          // 本版本认识
    Newer,       // formatVersion 比本版本高：只读
    NotOurs,     // 是 JSON 对象，但不是旅行计划的数据（format 不对）
};
// formatVersion 读不到时当 0（会被当成 Ok）
Check check(const json::Value &doc, int *formatVersion = nullptr);

// 读进来之后补齐必有的数组（plans、photos、journal；没有时加在末尾）
void normalize(json::Value &doc);

// 保存前写 savedBy / savedAt（已有的键原位替换）
void stamp(json::Value &doc, const QString &appVersion, const QDateTime &now);

// 写：JSON.stringify(doc, null, 2) + "\n"，UTF-8
QByteArray serialize(const json::Value &doc);

// 带时区的 ISO 时间：2026-11-03T04:30:12+11:00
QString isoWithOffset(const QDateTime &t);

} // namespace tp::data::nativefmt
