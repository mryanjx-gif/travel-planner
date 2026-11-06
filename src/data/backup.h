// 手动 / 自动备份（功能清单 D-17 / D-18，0.5.0）。规则照搬 Electron main.js `data:backup`：
//   - 在备份文件夹里新建"旅行计划备份-YYYYMMDD-HHMM"（本地时间），复制 travel.json、travel.backup.json（没有就跳过；0.6.0 前是 data.js、data.backup.js）
//     和整个 photos（不含任何一层名为 .thumbs 的缩略图缓存；.originals 等其他都复制）；
//   - 备份文件夹不能在数据文件夹里面（也不能就是数据文件夹）；
//   - 同一分钟里再备份：名字后面加 -2、-3……
//     （Electron 的写法会先把末尾的"-时分"去掉再加 -2，得到"旅行计划备份-20261103-2"，像是 bug；原生版保留时分：
//      "旅行计划备份-20261103-0102-2"。只影响同一分钟里第二次备份的文件夹名。）
// run() 是同步的（在后台线程里调用），每复制一个文件回调一次进度，回调返回 false = 取消；
// 取消或出错时删掉这次新建的那个备份文件夹（里面只有刚复制的副本），不碰别的东西。
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <functional>

namespace tp::data {

class Backup
{
public:
    struct Result
    {
        bool ok = false;
        bool cancelled = false;
        QString dir;      // 新建的备份文件夹（本地路径）
        int files = 0;    // 复制了几个文件
        QString error;
    };

    // done / total：已复制 / 总文件数；返回 false = 取消
    using Progress = std::function<bool(int done, int total)>;

    static QString folderName(const QDateTime &t);                       // 旅行计划备份-20261103-0102
    static QString uniqueDir(const QString &root, const QString &name);  // 已存在就加 -2、-3…
    static bool isInside(const QString &path, const QString &dataDir);   // path 是 dataDir 本身或在它里面
    // 要复制的文件（相对数据文件夹的路径，"/" 分隔）：travel.json、travel.backup.json、photos/…（不含 .thumbs）
    static QStringList collect(const QString &dataDir);

    static Result run(const QString &dataDir, const QString &destRoot, const QDateTime &now, const Progress &progress = {});
};

} // namespace tp::data
