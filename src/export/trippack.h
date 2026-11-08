// .trip 计划文件和分享版文件夹（0.14.1，E-05–E-08；照 Electron lib/trip-pack.js、export.js tripExport / clean / shareOne）。
//
// .trip 是 zip（miniz 3.1.2，src/third_party/miniz）：
//   plan.json  { format: "travel-planner.trip", formatVersion: 1（一个计划）/ 2（plans: [...] 多个），
//                plan | plans, journal, photoCredits, appVersion, exportedAt, native, photos: [...] }
//              plan / plans / journal / photoCredits 是 Electron 的格式（Electron 能直接导入）；
//              native = { formatVersion: 1, plans, photos, journal }：同样的计划用原生格式原样再放一份
//              （照片的拍摄信息、计划级照片、createdAt 等 Electron 格式放不下的），原生版导入时优先用它。Electron 不认识，忽略。
//   photos/…   用到的照片，路径和数据文件夹里一样。plan.json 压缩级别 6，照片不压缩。
// 解包限制：最多 5000 条、解压后合计 ≤ 2 GB；只认 plan.json 和 photos/ 下的普通路径（不许 ..、空段、反斜杠、冒号）。
#pragma once

#include "jsonvalue.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>

namespace tp::exporter::trip {

inline constexpr int kMaxVersion = 2;
inline constexpr int kMaxEntries = 5000;
inline constexpr qint64 kMaxTotal = 2LL * 1024 * 1024 * 1024;
QString formatName();   // "travel-planner.trip"

// photos/ 下面的普通文件（Electron trip-pack.js safeRel）
bool safeRel(const QString &rel);

// ---------- 导出 ----------
// doc 里这些计划（按数据里的顺序）做成 plan.json 的内容（photos 由 write 按实际打进去的照片填）。
// 没写币种、但有消费或预算的计划写上 defaultCurrency（对方电脑上按对方的默认币种算会错）。
struct Built
{
    json::Value manifest;
    QStringList photos;   // 要打包的照片（相对数据文件夹）
    int plans = 0;
};
Built build(const json::Value &doc, const QStringList &planIds, const QString &defaultCurrency, const QString &appVersion,
            const QDateTime &now);

struct WriteResult
{
    bool ok = false;
    QString error, file;
    int photos = 0;
    QStringList missing;   // 找不到文件、没带的照片
    qint64 size = 0;
};
// 写到 path（先写 path.tmp-<pid> 再改名）
WriteResult write(const QString &path, json::Value manifest, const QStringList &photos, const QString &dataDir);

// 一个文件夹里不重名的文件名：name.ext → name-2.ext …（Electron trip:export 的 dir 方式）
QString uniquePath(const QString &dir, const QString &baseName, const QString &ext);
// 文件名里不能用的字符换成 _，最长 max 个字（Electron：[\\/:*?"<>|]+ → _）
QString safeFileName(const QString &name, int max = 80);

// ---------- 导入 ----------
class Reader
{
public:
    Reader();
    ~Reader();
    // 打开并检查；失败返回 false，error() 是给用户看的话
    bool open(const QString &path);
    QString error() const { return m_error; }
    const json::Value &manifest() const { return m_manifest; }
    QStringList files() const;   // 包里的照片（相对路径）
    bool read(const QString &rel, QByteArray *out);
    void close();

private:
    struct Zip;
    std::unique_ptr<Zip> m_zip;
    QHash<QString, unsigned> m_index;   // 照片路径 → zip 里的序号
    json::Value m_manifest;
    QString m_error;
};

// plan.json → 原生格式的 { plans, photos, journal }；srcOf：原生的照片路径 → 包里的路径。
// 有 native 就用它（照片路径不变）；没有（Electron 导出的）就按 Electron 导入的规则转换。
json::Value toNative(const json::Value &manifest, const QStringList &packageFiles, const QDateTime &now, QHash<QString, QString> *srcOf);

// ---------- 分享版文件夹（E-07）----------
struct ShareOptions
{
    bool noMoney = true, noBook = true, noTodo = false, noJournal = false, noVisited = false;
    bool allPhotos = false;                          // false = 只带封面
    bool unknownCovers = false, unknownStops = false;   // 没有来源记录的也带（确定是自己拍的）
};
struct Shared
{
    json::Value doc;      // 去掉私人内容的原生数据
    QStringList photos;   // 要复制的照片（相对路径）
    int skipped = 0;      // 不能分发、没带的照片
    QString credits;      // 照片署名.txt 的内容（CRLF，不含 BOM）
};
// planIds 为空 = 全部计划（连未归类照片）
Shared share(const json::Value &doc, const QStringList &planIds, const ShareOptions &o, const QString &defaultCurrency, const QDateTime &now);

struct ShareWrite
{
    bool ok = false;
    QString error, dir;
    int photos = 0;
    QStringList missing;
};
// destRoot 下新建"旅行计划-分享版-[名字-]YYYYMMDD-HHMM"（重名加 -2），写 travel.json、照片、照片署名.txt（UTF-8 BOM）
ShareWrite writeShare(const QString &destRoot, const QString &name, const Shared &s, const QString &dataDir, const QString &appVersion,
                      const QDateTime &now);

} // namespace tp::exporter::trip
