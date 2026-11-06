// 数据仓库：读取 / 保存原生版的 travel.json（阶段 ①；0.3.0 起，0.6.0 改成原生版自己的格式，见 nativeformat.h）。
//
// 保存流程：
//   1. 读磁盘上现在的 travel.json，SHA-256 和"我手上的版本"比；不一样 = 被别处改过 → 返回冲突（除非 force）
//   2. 当天第一次保存：把现在的文件原样存进 .history\travel-YYYY-MM-DD.json（本地日期），只留 14 份
//   3. 把现在的文件原样存成 travel.backup.json
//   4. 写临时文件 travel.json.tmp → 刷盘 → 改名覆盖（改名前再读一次：这期间又被别处改了就放弃，返回冲突）
//   5. 回读，和要写的内容逐字节比对
//   6. 记下新内容的哈希
// 另外：每 3 秒在后台线程算一次文件的哈希，和手上的版本不同就发 externallyChanged（同一个哈希只发一次）。
//
// 只读的几种情况（照常显示，禁止保存）：
//   - 读不出来（JSON 格式错误、不是旅行计划的数据）：显示空数据，提示从备份或历史版本恢复；
//   - 更新版本的旅行计划写的（formatVersion 比本版本高）：照常显示，提示先更新软件；
//   - 别的旅行计划正开着这份数据（.lock，见 lockfile.h）：照常显示，可以"仍然编辑"。
#pragma once

#include "jsonvalue.h"
#include "lockfile.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

namespace tp::data {

class DataStore : public QObject
{
    Q_OBJECT
public:
    enum class State {
        None,        // 还没打开
        Loaded,      // 正常读到
        Missing,     // 文件夹里没有 travel.json：按空数据，可以保存（保存时新建）
        Unreadable,  // 有 travel.json 但读不出来：只读
        Newer,       // 更新版本的旅行计划写的：只读
    };
    Q_ENUM(State)

    struct SaveResult
    {
        bool ok = false;
        bool conflict = false;   // 文件被别处改过，要问用户"仍然保存 / 取消"
        QString error;
    };

    struct HistoryEntry
    {
        QString name;   // travel-2026-11-01.json
        QString date;   // 2026-11-01
        qint64 size = 0;
        int plans = 0;
    };

    static constexpr int kHistoryKeep = 14;
    static constexpr int kBeforeAiKeep = 10;

    explicit DataStore(QObject *parent = nullptr);
    ~DataStore() override;

    // 测试用：替换"现在几点"
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }
    // 写进 savedBy 的版本号（main.cpp 设成程序版本）
    void setAppVersion(const QString &v) { m_appVersion = v; }
    QString appVersion() const { return m_appVersion; }
    LockFile &lock() { return m_lock; }

    // 打开一个数据文件夹。返回 true = 能读（Loaded / Missing / 被别人锁住但读到了）
    bool load(const QString &dataDir);
    bool reload() { return load(m_dir); }

    State state() const { return m_state; }
    bool isReadOnly() const { return m_state == State::Unreadable || m_state == State::Newer || m_lockedByOther; }
    QString loadError() const { return m_loadError; }
    bool isNewerFormat() const { return m_state == State::Newer; }
    int fileFormatVersion() const { return m_fileVersion; }
    // 文件夹里没有 travel.json、但有 Electron 版的 data.js（可以导入）
    bool hasElectronData() const { return m_hasElectronData; }
    // 别的旅行计划正开着这份数据（只读）
    bool lockedByOther() const { return m_lockedByOther; }
    LockOwner lockOwner() const { return m_lockOwner; }
    // 用户选了"仍然编辑"：强制接管锁
    void takeOverLock();

    QString dataDir() const { return m_dir; }
    QString dataFile() const;

    const json::Value &data() const { return m_data; }
    // 改数据：在回调里改，改完自动标记为"有未保存的改动"
    void modify(const std::function<void(json::Value &)> &fn);
    void replaceData(json::Value v);   // 整份替换（撤销 / 重做、导入用），同样标记未保存
    bool isDirty() const { return m_dirty; }

    // 保存。force = 冲突时仍然保存（用户在弹框里选了"仍然保存"）
    SaveResult save(bool force = false);
    // 退出前调用：有未保存的改动就保存；遇到冲突或失败时不覆盖 travel.json，
    // 而是把当前数据另存为 travel.unsaved-YYYYMMDD-HHMMSS.json（保证不丢），返回那个文件名
    QString flushBeforeExit();
    // 不碰 travel.json，把当前数据另存为 travel.unsaved-YYYYMMDD-HHMMSS.json。返回文件名，失败返回空
    QString saveRescueCopy();
    // 放弃窗口里没保存的改动（不再在退出时保存它们；内存里的数据不变）
    void discardUnsaved() { setDirty(false); }

    // AI 修改含删除前：把磁盘上的 travel.json 原样复制成 travel.before-ai-YYYYMMDD-HHMMSS.json，只留最近 10 份
    QString snapshotBeforeAi(QString *error = nullptr);
    // "替换全部"导入前：travel.json 原样复制成 travel.before-import-YYYYMMDD-HHMMSS.json。没有 travel.json 时返回空且不算失败
    QString snapshotBeforeImport(QString *error = nullptr);

    // .history 里的滚动备份，新的在前
    QList<HistoryEntry> historyList() const;
    // 从滚动备份恢复：现在的 travel.json 先存成 travel.backup.json，再原子写入、回读比对，然后重新读取
    SaveResult restoreHistory(const QString &name);

    QByteArray knownHash() const { return m_knownHash; }
    static QByteArray sha256Hex(const QByteArray &bytes);

    // 外部修改检测（默认 3 秒一次，后台线程算哈希）
    void startWatching(int intervalMs = 3000);
    void stopWatching();
    // 立即同步检查一次（测试用）；返回 true = 发了 externallyChanged
    bool checkExternalNow();
    // 立即刷新一次锁（测试用；平时 30 秒一次）
    void refreshLockNow() { onLockTimer(); }

signals:
    void externallyChanged();      // travel.json 被别处改过（同一个版本只发一次）
    void dirtyChanged(bool dirty);
    void reloaded();               // 重新读取了（恢复历史版本之后等）
    void lockChanged();            // 被别人接管 / 自己接管
    void contentChanged();         // 内存里的数据变了（读取、修改、撤销 / 重做、导入；0.8.0 地图跟着重画）

private:
    QDateTime now() const { return m_clock ? m_clock() : QDateTime::currentDateTime(); }
    bool loadInner(const QString &dataDir);
    void setDirty(bool d);
    void snapPlans();
    void stampUpdated();
    void snapshotDaily(const QByteArray &current);
    void onHashResult(quint64 generation, const QByteArray &hash);
    void tick();
    void onLockTimer();
    QString stampName(const QString &prefix) const;   // prefix-YYYYMMDD-HHMMSS.json

    QString m_dir;
    State m_state = State::None;
    QString m_loadError;
    int m_fileVersion = 0;
    bool m_hasElectronData = false;
    json::Value m_data;
    bool m_dirty = false;
    QByteArray m_knownHash;            // 空 = 文件不存在 / 没有手上的版本（不检测冲突）
    QHash<QString, QString> m_planSigs;  // 计划 id → 上次保存时的内容（不含 updatedAt），用来判断要不要写 updatedAt
    std::function<QDateTime()> m_clock;
    QString m_appVersion;

    LockFile m_lock;
    bool m_lockedByOther = false;
    LockOwner m_lockOwner;
    QTimer m_lockTimer;

    QTimer m_watch;
    bool m_hashing = false;
    quint64 m_generation = 0;          // 每次读取 / 保存 +1，丢掉之前发出的哈希任务的结果
    QByteArray m_lastNotified;
};

} // namespace tp::data
