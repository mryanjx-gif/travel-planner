// 从 Electron 版导入（0.6.0，PO 2026-11-03 定；对应关系见 claude/数据格式.md §导入）。
//
// 只读 Electron 的数据文件夹（data.js + photos\），转换成原生格式：
//   - 站点按 order 排好（数组顺序就是站点顺序，去掉 order）；去下一站的 legMode / legPath / legKey / legDist / legTime 合成 leg 对象；
//   - schedule、localEmergency 的旧字符串形式转成数组；
//   - 照片统一进顶层 photos（站点照片、未归类、封面、照片署名、游记 / 攻略正文里的 ![…](photos/…)），其他地方改用照片 id；
//     文件复制到 photos\YYYY-MM\<id>_<原名>（有拍摄时间按拍摄月份，没有按导入月份），.originals 里的原图一起复制；
//   - Electron 以后加的、这里不认识的字段原样保留在对应的对象里（报告里列出来）。
// 两种方式：替换全部（调用方先把现有数据备份成 travel.before-import-*.json）/ 只加新的（按 id，已经有的计划跳过）。
// 中途失败或取消时，删掉这次已经复制的照片，数据不变。
#pragma once

#include "jsonvalue.h"
#include "planmerge.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <functional>

namespace tp::data {

struct ImportCopy
{
    QString src;   // 相对 Electron 数据文件夹（"photos/…"）
    QString dst;   // 相对原生数据文件夹（"photos/…"）
};

struct ImportReport
{
    bool ok = false;
    bool cancelled = false;
    QString error;

    int plans = 0, plansSkipped = 0, stops = 0, photos = 0, journal = 0;
    int photosCopied = 0, originalsCopied = 0;
    QStringList missingFiles;     // 引用了、但 Electron 文件夹里找不到的照片（相对路径）
    QStringList unusedFiles;      // Electron photos\ 里没有被任何地方引用的文件（不导入）
    QStringList unknownFields;    // 不认识、原样保留的字段（"计划.xxx" 这种）
    QStringList duplicatePhotos;  // 同一个文件被多处当成不同照片引用（只留第一处的归属）
    QStringList externalPhotos;   // 不在 photos\ 下的照片地址（网址等），原样保留、不复制
    QStringList copiedFiles;      // 这次复制进原生数据文件夹的文件（绝对路径；保存失败时调用方用来回滚）
    QString backupName;           // 替换全部前的备份（调用方填）
};

class ElectronImport
{
public:
    enum class Mode { Replace, AddNew };

    // 第一步：纯转换（不碰文件）。electron = data.js 里的 window.TRAVEL_DATA。
    // fileExists(rel)：Electron 文件夹里这个相对路径的文件在不在（决定要不要复制 / 算缺失）
    struct Converted
    {
        json::Value doc;              // 原生格式（完整的一份）
        QList<ImportCopy> copies;     // 要复制的照片
        QList<ImportCopy> originals;  // 要复制的旋转前原图（Electron 有才复制）
        QStringList referenced;       // 被引用的本地照片路径（Electron 的相对路径，含缺失的）
        ImportReport report;
    };
    static Converted convert(const json::Value &electron, const QDateTime &now, const QString &appVersion,
                             const std::function<bool(const QString &rel)> &fileExists);

    // 第二步：和已有的原生数据合并。AddNew：只加原生版里还没有的计划（按 id）；只保留这些计划的照片 / 游记要复制的文件
    static json::Value merge(const json::Value &existing, Converted &conv, Mode mode);

    // 整个流程（在后台线程跑）：读 data.js → 转换 → 合并 → 复制照片。成功时 result 是要保存的新数据。
    // existing 为空（null）时按"替换全部"。progress(done, total) 返回 false = 取消。
    // picks（0.14.1）：AddNew 时本地已经有的计划逐个怎么处理（计划 id → Replace / Copy / Skip，见 planmerge.h）；没写的跳过
    static ImportReport run(const QString &electronDir, const QString &nativeDir, const json::Value &existing, Mode mode,
                            const QDateTime &now, const QString &appVersion, json::Value *result,
                            const std::function<bool(int, int)> &progress = {},
                            const QHash<QString, planmerge::Mode> &picks = {});

    // 0.14.1：再导入前看看哪些计划这里已经有了（冲突框用）。[{ id, name, local: summary, incoming: summary }]；error 非空 = 读不出来
    static QVariantList conflicts(const QString &electronDir, const json::Value &existing, const QDateTime &now, QString *error);

    // 删掉 run 复制进去的文件（保存失败时回滚）
    static void rollback(const QStringList &copiedFiles);

    // 文件夹里有没有 Electron 版的数据（data.js）
    static bool isElectronFolder(const QString &dir);

    // Electron store.js safeName：[\\/:*?"<>|#%\s]+ → _，只留最后 60 个字符（\s 按 JS 的字符集）
    static QString safeName(const QString &name);
};

} // namespace tp::data
