// 把别处来的计划放进现有数据（0.14.1）：.trip 导入（E-08）和从 Electron 再导入时逐个计划选（待办 2026-11-03）共用。
// 都在原生格式上做（来源先转成原生格式）；照 Electron export.js applyImport：
//   New      本地没有这个 id：加进来（照片、游记跟着来）
//   Replace  覆盖：本地这个计划、它的照片记录和游记换成导入的（照片文件不删）
//   Copy     另存一份：新计划 id，名字后面加"（导入）"，照片和游记都用新 id、照片文件另起名字（不和原来的共用）
//   Merge    只合并消费和照片：同行人（同 id 或同名算一个人）、消费（按 id 不重复）、站点照片（站点 id 对得上、同一文件不重复）、
//            封面（本地没有时）、游记（按 id 不重复）
//   Skip     跳过
// 照片 id 撞了别的照片：换新 id，封面、攻略、游记正文里的 photo:<id> 跟着改。
#pragma once

#include "jsonvalue.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>

namespace tp::data::planmerge {

enum class Mode { New, Replace, Copy, Merge, Skip };
Mode modeFromKey(const QString &key);   // "replace" / "copy" / "merge" / "skip"，其他 = New

struct FileJob
{
    QString photoId;      // 结果数据里这条照片记录的 id
    QString from;         // 来源里的相对路径（导入的照片记录原来的 file）
    bool fresh = false;   // 另存一份：目标已经有文件时一律另起名字
};

struct Result
{
    json::Value doc;
    QList<FileJob> files;
    QStringList messages;               // 每个导入的计划一句
    QHash<QString, QString> planIds;    // 来源计划 id → 结果里的 id（跳过的没有）
    int plans = 0, photos = 0, journal = 0, expenses = 0;
};

struct Options
{
    std::function<QString(const QString &prefix)> uid;   // 新 id（"p" / "ph" / "j"）
    QString copySuffix;                                    // 另存一份时名字后面加的
    QString defaultCurrency;                               // 合并的消费没写币种、来源计划也没写时用
};

// incoming：原生格式的 { plans, photos, journal }（photos / journal 按 plan 归到各个计划）。
// modes：来源计划 id → 怎么处理；没写的：本地没有 = New，本地有 = Skip。本地没有时 Replace / Merge 也按 New
Result apply(const json::Value &existing, const json::Value &incoming, const QHash<QString, Mode> &modes, const Options &opt);

struct WriteResult
{
    bool ok = false;
    QString error;
    QStringList copied;   // 这次写进数据文件夹的文件（绝对路径）
    int written = 0, same = 0, missing = 0;
};
// 把照片文件写进数据文件夹，目标是 doc 里这条照片的 file：已经有内容一样的文件就直接用（fresh 时不用），
// 内容不同另起名字 -2、-3（doc 里的 file 跟着改）。read(from, &bytes) 读来源，读不到算 missing（记录保留，显示为缺图）。
// readOriginal（可空）：有旋转前原图时给出内容，写到 photos/.originals/ 对应位置。写文件：临时文件 → 核对大小 → 改名。
// 出错时删掉这次写进去的文件。
WriteResult writeFiles(json::Value &doc, const QString &dataDir, const QList<FileJob> &jobs,
                       const std::function<bool(const QString &from, QByteArray *bytes)> &read,
                       const std::function<bool(const QString &from, QByteArray *bytes)> &readOriginal = {});

// 冲突框里的对比：{ id, name, updatedAt（毫秒，0 = 不知道）, stops, expenses, photos }；doc 是原生格式
QVariantMap summary(const json::Value &doc, const QString &planId);

// 正文里的 ](photo:旧id) 换成新的
QString remapPhotoRefs(const QString &text, const QHash<QString, QString> &map);
// 正文里引用的照片 id
QStringList photoRefs(const QString &text);

} // namespace tp::data::planmerge
