#include "atomicfile.h"

#include "trdata.h"

#include <QDir>
#include <QFile>
#include <QThread>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <string>
#else
#  include <cerrno>
#  include <cstdio>
#  include <cstring>
#  include <unistd.h>
#endif

namespace tp::platform {

namespace {

#ifndef Q_OS_WIN
bool flushToDisk(QFile &f)
{
    return f.flush() && ::fsync(f.handle()) == 0;
}
#endif

QString lastOsError()
{
#ifdef Q_OS_WIN
    const DWORD code = GetLastError();
    wchar_t *msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    QString s = msg ? QString::fromWCharArray(msg).trimmed() : QString();
    if (msg) LocalFree(msg);
    return tp::data::trd("%1（错误码 %2）").arg(s).arg(code);
#else
    return QString::fromLocal8Bit(std::strerror(errno));
#endif
}

bool renameReplace(const QString &from, const QString &to, QString *err)
{
#ifdef Q_OS_WIN
    const std::wstring f = QDir::toNativeSeparators(from).toStdWString();
    const std::wstring t = QDir::toNativeSeparators(to).toStdWString();
    // 被杀毒软件、索引或同步盘短暂占用时会报"拒绝访问 / 共享冲突"：最多重试 10 次（约 1 秒）
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (MoveFileExW(f.c_str(), t.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        const DWORD code = GetLastError();
        if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION && code != ERROR_LOCK_VIOLATION) break;
        QThread::msleep(100);
    }
    *err = lastOsError();
    return false;
#else
    if (std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0) return true;
    *err = lastOsError();
    return false;
#endif
}

} // namespace

WriteResult writeFile(const QString &path, const QByteArray &bytes)
{
#ifdef Q_OS_WIN
    // 直接用 Win32：CreateFileW → WriteFile → FlushFileBuffers（确保改名前内容已经落盘）
    const std::wstring wp = QDir::toNativeSeparators(path).toStdWString();
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 10; ++attempt) {
        // OPEN_ALWAYS + 写完截断：目标是隐藏文件时 CREATE_ALWAYS 会报"拒绝访问"
        h = CreateFileW(wp.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) break;
        const DWORD code = GetLastError();
        if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION && code != ERROR_LOCK_VIOLATION) break;
        QThread::msleep(100);
    }
    if (h == INVALID_HANDLE_VALUE)
        return { false, tp::data::trd("无法写入 %1：%2").arg(QDir::toNativeSeparators(path), lastOsError()) };
    const char *p = bytes.constData();
    qint64 left = bytes.size();
    bool ok = true;
    while (left > 0 && ok) {
        DWORD chunk = DWORD(qMin<qint64>(left, 16 * 1024 * 1024)), written = 0;
        ok = WriteFile(h, p, chunk, &written, nullptr) && written == chunk;
        p += written;
        left -= written;
    }
    if (ok) ok = SetEndOfFile(h) && FlushFileBuffers(h);
    const QString err = ok ? QString() : lastOsError();
    CloseHandle(h);
    if (!ok) return { false, tp::data::trd("写入 %1 失败：%2").arg(QDir::toNativeSeparators(path), err) };
    return { true, {} };
#else
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, tp::data::trd("无法写入 %1：%2").arg(QDir::toNativeSeparators(path), f.errorString()) };
    if (f.write(bytes) != bytes.size() || !flushToDisk(f)) {
        const QString e = f.errorString();
        f.close();
        return { false, tp::data::trd("写入 %1 失败：%2").arg(QDir::toNativeSeparators(path), e) };
    }
    f.close();
    return { true, {} };
#endif
}

WriteResult writeViaTemp(const QString &target, const QString &tmpPath, const QByteArray &bytes)
{
    WriteResult w = writeFile(tmpPath, bytes);
    if (!w.ok) { QFile::remove(tmpPath); return w; }
    QString err;
    if (!renameReplace(tmpPath, target, &err)) {
        QFile::remove(tmpPath);
        return { false, tp::data::trd("把临时文件改名为 %1 失败：%2").arg(QDir::toNativeSeparators(target), err) };
    }
    return { true, {} };
}

WriteResult copyFileOverwrite(const QString &from, const QString &to)
{
    QFile src(from);
    if (!src.open(QIODevice::ReadOnly))
        return { false, tp::data::trd("无法读取 %1：%2").arg(QDir::toNativeSeparators(from), src.errorString()) };
    const QByteArray bytes = src.readAll();
    src.close();
    return writeFile(to, bytes);
}

} // namespace tp::platform
