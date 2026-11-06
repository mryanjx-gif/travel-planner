// 原子写入：先写临时文件、刷到磁盘，再改名覆盖目标（阶段 ①，0.3.0）。
// Windows 上用 MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)，和 Node 的 fs.rename（libuv）一样；
// 被杀毒软件 / 同步盘短暂锁住时重试几次。
#pragma once

#include <QByteArray>
#include <QString>

namespace tp::platform {

struct WriteResult
{
    bool ok = false;
    QString error;
};

// 写 bytes 到 tmpPath（覆盖），刷盘，然后改名为 target。失败时尽量删掉临时文件。
WriteResult writeViaTemp(const QString &target, const QString &tmpPath, const QByteArray &bytes);

// 原样复制文件（覆盖目标）。用于 data.backup.js、.history、data.before-ai-*.js
WriteResult copyFileOverwrite(const QString &from, const QString &to);

// 直接写一个文件（覆盖），刷盘
WriteResult writeFile(const QString &path, const QByteArray &bytes);

} // namespace tp::platform
