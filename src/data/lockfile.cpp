#include "lockfile.h"

#include "atomicfile.h"
#include "jsonio.h"
#include "trdata.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSysInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#endif

namespace tp::data {

using json::Value;

QString LockOwner::describe() const
{
    QString who = machine.isEmpty() ? trd("另一台电脑") : machine;
    return trd("%1 上的旅行计划 %2").arg(who, version).trimmed();
}

LockFile::LockFile()
{
    m_app = QStringLiteral("TravelPlannerNative");
    m_version = QCoreApplication::applicationVersion();
    m_machine = QSysInfo::machineHostName();
    m_user = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    m_pid = QCoreApplication::applicationPid();
    m_now = [] { return QDateTime::currentMSecsSinceEpoch(); };
    m_alive = &LockFile::processAlive;
}

LockFile::~LockFile() { release(); }

void LockFile::setIdentity(const QString &app, const QString &version, const QString &machine, qint64 pid)
{
    m_app = app;
    m_version = version;
    m_machine = machine;
    m_pid = pid;
}

QString LockFile::filePath(const QString &dir) { return QDir(dir).filePath(QStringLiteral(".lock")); }

LockOwner LockFile::read(const QString &dir)
{
    LockOwner o;
    QFile f(filePath(dir));
    if (!f.open(QIODevice::ReadOnly)) return o;
    json::ParseError pe;
    const Value v = json::parse(QString::fromUtf8(f.readAll()), &pe);
    if (!pe.ok || !v.isObject()) return o;
    o.app = v[QStringLiteral("app")].toString();
    o.version = v[QStringLiteral("version")].toString();
    o.machine = v[QStringLiteral("machine")].toString();
    o.user = v[QStringLiteral("user")].toString();
    o.pid = qint64(v[QStringLiteral("pid")].toNumber());
    o.startedAt = qint64(v[QStringLiteral("startedAt")].toNumber());
    o.heartbeat = qint64(v[QStringLiteral("heartbeat")].toNumber());
    o.valid = o.heartbeat > 0;
    return o;
}

bool LockFile::processAlive(qint64 pid)
{
    if (pid <= 0) return false;
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;   // 有这个进程，只是没权限看
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

bool LockFile::isMine(const LockOwner &o) const
{
    return o.valid && o.pid == m_pid && o.machine.compare(m_machine, Qt::CaseInsensitive) == 0;
}

bool LockFile::isStale(const LockOwner &o) const
{
    if (!o.valid) return true;
    const qint64 age = m_now() - o.heartbeat;
    if (age > kStaleMs || age < -kStaleMs) return true;   // 太久没刷新（时钟差得离谱也算过时）
    // 同一台电脑：那个进程已经不在了（崩溃留下的）
    if (o.machine.compare(m_machine, Qt::CaseInsensitive) == 0 && o.pid != m_pid && !m_alive(o.pid)) return true;
    return false;
}

bool LockFile::write()
{
    json::Object o;
    o.set(QStringLiteral("app"), Value(m_app));
    o.set(QStringLiteral("version"), Value(m_version));
    o.set(QStringLiteral("machine"), Value(m_machine));
    o.set(QStringLiteral("user"), Value(m_user));
    o.set(QStringLiteral("pid"), Value(double(m_pid)));
    o.set(QStringLiteral("startedAt"), Value(double(m_startedAt)));
    o.set(QStringLiteral("heartbeat"), Value(double(m_now())));
    const QString path = filePath(m_dir);
    return platform::writeViaTemp(path, path + QStringLiteral(".tmp"), json::stringify(Value(std::move(o)), 2).toUtf8() + '\n').ok;
}

LockFile::Result LockFile::acquire(const QString &dir, bool force, LockOwner *other)
{
    if (m_held && m_dir != dir) release();
    m_dir = dir;
    const LockOwner cur = read(dir);
    if (!force && cur.valid && !isMine(cur) && !isStale(cur)) {
        if (other) *other = cur;
        m_held = false;
        return Result::HeldByOther;
    }
    if (!isMine(cur) || !m_held) m_startedAt = m_now();
    m_held = write();
    return m_held ? Result::Acquired : Result::NoLock;
}

bool LockFile::refresh(LockOwner *other)
{
    if (!m_held || m_dir.isEmpty()) return true;
    const LockOwner cur = read(m_dir);
    if (cur.valid && !isMine(cur) && !isStale(cur)) {
        // 别人"仍然编辑"接管了：不再写，交给调用方变成只读
        m_held = false;
        if (other) *other = cur;
        return false;
    }
    write();
    return true;
}

void LockFile::release()
{
    if (!m_held || m_dir.isEmpty()) { m_held = false; return; }
    const LockOwner cur = read(m_dir);
    if (!cur.valid || isMine(cur)) QFile::remove(filePath(m_dir));
    m_held = false;
}

} // namespace tp::data
