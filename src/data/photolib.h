// 照片库的文件操作（0.7.0，阶段 ① 第 5 部分；只依赖 Qt Core，界面在 src/app/photocontroller.*）。
//
//   - 导入：猜计划和站点（照搬 Electron photos-import.js guess）、查重复、复制进 photos\YYYY-MM\<id>_<原名>、生成照片记录；
//     中途失败或取消时删掉这次已经复制的文件。
//   - 清理没用的照片：扫描 photos\，照片库里没有记录的文件移到回收站。三道保护（PO 2026-11-03 + Claude 建议、PO 同意）：
//       1. 最近 10 分钟加进来的文件先不动（按文件修改时间和照片记录的 addedAt）；
//       2. 移动前再核对一次 travel.json 有没有被别处改过（调用方做：哈希和手上的版本比）；
//       3. 撤销 / 重做记录里还用得到的文件也不动（调用方传进来）。
//     .originals 里"原照片已经不在了"的原图单独列出来，用户勾选了才清理。
//   - 回收站用 QFile::moveToTrash：Qt 6.12 在 Windows 上用 IFileOperation，进不了回收站时直接失败、不会永久删除
//     （qfilesystemengine_win.cpp 的 PreDeleteItem 没有 TSF_DELETE_RECYCLE_IF_POSSIBLE 时返回 E_FAIL，2026-11-03 读源码核实）。
#pragma once

#include "exif.h"
#include "jsonvalue.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

namespace tp::data::photolib {

inline constexpr int kRecentMinutes = 10;     // 清理时最近这么多分钟加进来的先不动
inline constexpr double kNearKm = 2.0;        // 没有日期时按位置猜：2 km 以内最近的站（Electron NEAR_KM）

// 能导入的格式（按文件头判断）：JPEG、PNG、WebP、HEIC / HEIF、GIF、BMP
bool isImportable(const QString &path);
// 能旋转的格式（PO 2026-11-03）：扩展名是 jpg / jpeg / png / webp，而且文件头对得上
bool canRotate(const QString &path);

// 新照片 id："ph_" + 毫秒时间（36 进制）+ 4 个随机字符
QString newPhotoId(qint64 nowMs);
// 照片库里的相对路径 photos/YYYY-MM/<id>_<原名>：有拍摄时间按拍摄月份，没有按 now 的月份；
// 原名按 Electron safeName 清理；磁盘上已经有、或 taken 里已经用了（不分大小写）就加 -2、-3
QString targetFile(const QString &dataDir, const QString &id, const QString &originalName, const QString &takenAt,
                   const QDateTime &now, QSet<QString> *taken = nullptr);

// ---------- 猜计划和站点 ----------
struct Guess
{
    QString plan, stop;   // 都空 = 未归类
    QString why;          // "date"（按拍摄日期）/ "gps"（按拍摄位置）/ ""（没认出来，或调用方指定的）
};
// forcePlan / forceStop：从计划页"添加照片"进来时默认放进这个计划（这一站），只在计划里按日期 / GPS 挑站
Guess guess(const json::Value &doc, const QString &takenAt, double lat, double lng,
            const QString &forcePlan = {}, const QString &forceStop = {});
double distKm(double lat1, double lng1, double lat2, double lng2);

// ---------- 查重复 ----------
// 照片库里有没有和 srcPath 内容完全一样的文件（先比大小，大小一样再比 SHA-1）。返回那张照片的 id，没有返回空。
// hashCache：照片库里文件的哈希（键 = 绝对路径），同一批导入共用，避免重复算
QString findDuplicate(const json::Value &doc, const QString &dataDir, const QString &srcPath, QHash<QString, QByteArray> *hashCache);
QByteArray fileSha1(const QString &path);

// ---------- 导入 ----------
struct ImportItem
{
    QString src;          // 要导入的文件（绝对路径）
    QString name;         // 原文件名
    json::Object exif;    // exif::photoFields 的结果
    QString plan, stop;   // 归属（都空 = 未归类）
    bool skip = false;    // 不导入（重复的默认跳过）
};
struct ImportResult
{
    bool ok = false, cancelled = false;
    QString error;
    QStringList copiedFiles;   // 这次复制进来的文件（绝对路径；失败时已经删掉）
    json::Array records;       // 要加进照片库的记录
    int skipped = 0;
};
// 复制文件、生成记录（不改数据；调用方 addRecords 后保存，保存失败时 rollback(copiedFiles)）。
// progress(done, total) 返回 false = 取消
ImportResult copyIn(const QString &dataDir, const QList<ImportItem> &items, const QDateTime &now,
                    const std::function<bool(int, int)> &progress = {});
void rollback(const QStringList &copiedFiles);
// 记录加到照片库末尾
void addRecords(json::Value &doc, const json::Array &records);

// ---------- 清理没用的照片 ----------
// 数据里用到的照片文件（照片库的 file，小写、正斜杠），可以传多份数据（撤销记录里的各版）
QSet<QString> referencedFiles(const json::Value &doc);
// 最近 kRecentMinutes 分钟加进照片库的文件（按 addedAt）
QSet<QString> recentlyAddedFiles(const json::Value &doc, qint64 nowMs);

struct CleanScan
{
    QStringList unused;            // photos/ 里没有记录的文件（相对路径）
    qint64 unusedBytes = 0;
    QStringList orphanOriginals;   // photos/.originals/ 里原照片已经不在照片库里的原图
    qint64 orphanBytes = 0;
    QStringList keptRecent;        // 没记录、但最近 10 分钟才加进来的（这次不动）
    QStringList keptUndo;          // 没记录、但撤销记录里还用得到的（这次不动）
};
// referenced：现在的数据用到的；undoReferenced：撤销 / 重做记录里用到的；recentAdded：最近加进来的（按记录）
CleanScan scanUnused(const QString &dataDir, const QSet<QString> &referenced, const QSet<QString> &undoReferenced,
                     const QSet<QString> &recentAdded, const QDateTime &now);

struct TrashResult
{
    int moved = 0;
    QStringList failed;   // "相对路径：原因"
};
// 逐个移到回收站。移动前每个文件再按 CleanScan 的规则核对一次（还没记录、不是最近的），不符合的跳过
TrashResult moveToTrash(const QString &dataDir, const QStringList &rels, const QSet<QString> &referenced,
                        const QSet<QString> &undoReferenced, const QSet<QString> &recentAdded, const QDateTime &now,
                        const std::function<bool(const QString &abs, QString *err)> &trashFn = {});

} // namespace tp::data::photolib
