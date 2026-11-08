// 批量按道路算路线 + 地图上的提示条"有 N 段还没算"（0.8.4，功能清单 M-18 / M-19；Electron map-app.js
// allPendingRoadLegs / refreshRoadBar / runRoadBatch / rememberRoadFail）。QML 里是 roads（qml/components/RoadBar.qml）。
//
// - 平时不请求任何路线服务；只有点"现在计算"才一段一段算（经 NetClient，同一主机间隔 ≥ 1.1 秒、20 秒超时）
// - 算好一段就写进数据（stops[].leg），地图马上跟着画道路；全部算完自动保存一次（撤销时是一步）
// - 路线服务明确说"找不到路 / 距离太远"的段记在 road-fail.json（和设置放在一起，最多 500 个），以后不再算进"还没算"；
//   断网、超时、服务器出错只在这次运行里记（下次打开还会再试）。站点挪动或换交通方式后 key 变了，自然重新算
// - 站点编辑器（0.8.9）里改动时自动算单段也走这里（computeLeg，留好接口）
#pragma once

#include "roads.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <functional>

namespace tp::data { class DataStore; }

namespace tp::services {

class NetClient;

class RoadRouter : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool shown READ shown NOTIFY changed)          // 提示条要不要显示
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString button READ button NOTIFY changed)     // 主按钮：现在计算 / 停止 / 保存 / 好
    Q_PROPERTY(int pending READ pending NOTIFY changed)       // 还没算的段数
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool done READ done NOTIFY changed)
    Q_PROPERTY(int okCount READ okCount NOTIFY changed)
    Q_PROPERTY(int failCount READ failCount NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)

public:
    explicit RoadRouter(QObject *parent = nullptr);

    void setStore(tp::data::DataStore *store);
    void setNet(NetClient *net) { m_net = net; }
    // 保存：quiet = 自动保存（成功不提示）；返回是否保存成功。由 main 接到 AppController
    void setSave(std::function<bool(bool quiet)> save) { m_save = std::move(save); }
    void setTranslate(std::function<QString(const char *)> tr) { m_tr = std::move(tr); emit changed(); }
    // 记住"算不出来"的段的文件（不设 = 只记在内存里）
    void setFailFile(const QString &path);
    // 测试用：替换真正的请求（拿到 URL，回调给结果）
    using Fetch = std::function<void(const QUrl &, std::function<void(int httpStatus, const QByteArray &body, int netError)>)>;
    void setFetchForTest(Fetch f) { m_fetch = std::move(f); }

    bool shown() const;
    QString text() const;
    QString button() const;
    int pending() const { return m_pending; }
    bool running() const { return m_running; }
    bool done() const { return m_done; }
    int okCount() const { return m_ok; }
    int failCount() const { return m_fail; }
    int total() const { return m_total; }
    QString failReason(const QString &key) const;   // 这一段为什么没算出来（编辑器里显示）
    // ---- 站点编辑器（0.8.9）自动算单段时用 ----
    bool remembered(const QString &key) const { return m_failMsg.contains(key); }   // 以前已经确定算不出来（不再自动算）
    // 编辑器里算一段失败了：permanent（找不到路 / 距离太远）记住，下次不再算；其余只记原因
    void noteEditorFail(const QString &key, const QString &msg, bool permanent);
    // 编辑器里点"重试"：忘掉这一段的失败（Electron forgetRoadFail）
    void forget(const QString &key);
    // 批量计算被挡住时（编辑器开着）给的原因；空 = 不挡（Electron："请先保存或关闭编辑器，再批量计算"）
    void setBlocker(std::function<QString()> f) { m_blocker = std::move(f); }
    int rememberedFails() const { return int(m_failKeys.size()); }

    Q_INVOKABLE void primary();    // 主按钮
    Q_INVOKABLE void dismiss();    // × 先不算（这次运行里不再显示，有新的段也不显示——和 Electron 一样）
    Q_INVOKABLE void start();      // 现在计算
    // 0.14.0 导出前（E-10，Electron ensureRoads / computeRoadsFor）：只数 / 只算这一个计划的段（planId 空 = 全部）
    Q_INVOKABLE int pendingFor(const QString &planId) const;
    Q_INVOKABLE void startFor(const QString &planId);
    // 0.14.1 .trip / 分享版 / 批量导出：几个计划一起数、一起算
    Q_INVOKABLE int pendingForIds(const QStringList &planIds) const;
    Q_INVOKABLE void startForIds(const QStringList &planIds);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void recount();    // 重新数一遍还没算的段
    // 截图用（roadbar 页）：假装有 n 段还没算（不动数据）
    Q_INVOKABLE void demoForShot(int n) { m_pending = n; m_dismissed = false; m_demo = true; emit changed(); }

    // 错误的种类（NetClient::Error 的一部分 + 服务返回的）→ 给人看的原因
    QString errorText(int netError, const tp::map::roads::OsrmResult *osrm, const QString &mode) const;

signals:
    void changed();
    void legDone(const QString &key, bool ok);   // 每算完一段（自检、编辑器用）
    void message(const QString &text, bool warn);   // 给人看的一句提示（0.8.9）
    void finished();

private:
    void next();
    void onResult(const tp::map::roads::Pending &leg, int httpStatus, const QByteArray &body, int netError);
    void finish();
    void remember(const QString &key, const QString &msg);
    void saveFails();
    QSet<QString> excluded() const;
    QString trd(const char *s) const { return m_tr ? m_tr(s) : QString::fromUtf8(s); }

    tp::data::DataStore *m_store = nullptr;
    NetClient *m_net = nullptr;
    std::function<bool(bool)> m_save;
    std::function<QString(const char *)> m_tr;
    std::function<QString()> m_blocker;
    Fetch m_fetch;
    QTimer m_recount;

    int m_pending = 0;
    bool m_running = false, m_stopReq = false, m_done = false, m_dismissed = false, m_demo = false;
    int m_ok = 0, m_fail = 0, m_total = 0, m_index = 0;
    QVector<tp::map::roads::Pending> m_list;
    QStringList m_only;                   // startFor：只算这些计划
    QSet<QString> m_sessionFail;          // 这次运行里没算出来的（断网、超时……）：不算进"还没算"，下次打开再试
    QHash<QString, QString> m_lastMsg;    // key → 最近一次失败的原因
    QString m_failFile;
    QStringList m_failKeys;               // 记住的"算不出来"（按记下的先后，超过 500 个丢最早的）
    QHash<QString, QString> m_failMsg;
};

} // namespace tp::services
