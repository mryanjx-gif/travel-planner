// 数据文件夹的锁文件 .lock（0.6.0）：同一份数据被两份原生版同时打开时（开发版 + 安装版，或同步盘在两台电脑上），
// 后打开的那份只读，不互相覆盖。
//
// .lock 的内容（JSON）：{ app, version, machine, user, pid, startedAt, heartbeat }（时间是毫秒）。
//   - 打开数据时写入；每 30 秒刷新一次 heartbeat；正常关闭时删掉。
//   - 别人的锁"新鲜"（heartbeat 在 2 分钟以内，且如果是同一台电脑、那个进程还在）→ 只读打开；用户可以"仍然编辑"（强制接管）。
//   - 过时的锁（程序崩溃留下的）→ 直接接管。
//   - 写不了锁文件（文件夹只读等）→ 照常打开，不当成被占用（保存时自然会失败）。
// 同步盘上锁文件可能还没同步过来，所以保存前的哈希冲突检测仍然保留，作为第二道保护。
#pragma once

#include <QDateTime>
#include <QString>
#include <functional>

namespace tp::data {

struct LockOwner
{
    bool valid = false;     // 读到了一个格式正确的锁
    QString app, version, machine, user;
    qint64 pid = 0;
    qint64 startedAt = 0;   // 毫秒
    qint64 heartbeat = 0;   // 毫秒

    // 界面上显示："<电脑名> 上的旅行计划 0.6.0"
    QString describe() const;
};

class LockFile
{
public:
    static constexpr qint64 kStaleMs = 120000;      // 2 分钟没刷新算过时
    static constexpr int kHeartbeatMs = 30000;

    enum class Result {
        Acquired,     // 拿到了（或者本来就是自己的）
        HeldByOther,  // 别人正开着（other 里是对方）
        NoLock,       // 写不了锁文件：照常打开，但没有锁保护
    };

    LockFile();
    ~LockFile();

    // 测试用：替换"现在几点"、本机身份、进程是否还在
    void setClock(std::function<qint64()> nowMs) { m_now = std::move(nowMs); }
    void setIdentity(const QString &app, const QString &version, const QString &machine, qint64 pid);
    void setProcessAlive(std::function<bool(qint64)> fn) { m_alive = std::move(fn); }

    // 打开 dir 时调用。force = 用户选了"仍然编辑"
    Result acquire(const QString &dir, bool force, LockOwner *other = nullptr);
    // 定时刷新 heartbeat。返回 false = 锁被别人接管了（other 里是对方），这时不再写锁文件
    bool refresh(LockOwner *other = nullptr);
    // 关闭 / 换文件夹时调用：锁是自己的就删掉
    void release();

    bool held() const { return m_held; }
    QString dir() const { return m_dir; }

    static QString filePath(const QString &dir);
    static LockOwner read(const QString &dir);
    bool isStale(const LockOwner &o) const;
    bool isMine(const LockOwner &o) const;
    static bool processAlive(qint64 pid);

private:
    bool write();

    QString m_dir;
    bool m_held = false;
    qint64 m_startedAt = 0;
    QString m_app, m_version, m_machine, m_user;
    qint64 m_pid = 0;
    std::function<qint64()> m_now;
    std::function<bool(qint64)> m_alive;
};

} // namespace tp::data
