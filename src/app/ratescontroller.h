// 汇率和主货币（0.10.1，P-11；QML 里是 rates）。Frankfurter，失败时用 open.er-api.com（Electron main.js rates:fetch）。
// 都存在 settings.json：currency（主货币，PO 选 3A：在消费和账本页头直接选）、rates（上次的表）、ratesAt（毫秒）。键名和 Electron prefs.js 一样。
// 0.12.5：去掉了手动汇率（PO 2026-11-08）。settings.json 里已有的 manualRates 键不读也不删。
// 20 小时内更新过就不再取（汇率源每个工作日更新一次）；离线时用上次的，页面上写明日期。
// 0.12.6（PO 2026-11-08）：Frankfurter 成功但缺数据里用到的货币（它没有 TWD、VND、RUB、AED、EGP 等）时，再问一次 open.er-api.com，
// 把表里没有的补进同一张表（基准相同才补，已有的不动）；只在有缺的时候请求，时间记在 ratesFillAt，20 小时内不重复。
#pragma once

#include "planmoney.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <functional>

namespace tp::services { class NetClient; }

namespace tp::app {

class Settings;

class RatesController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString main READ main WRITE setMain NOTIFY changed)
    Q_PROPERTY(QString date READ date NOTIFY changed)               // 表的日期（空 = 还没有表）
    Q_PROPERTY(bool failed READ failed NOTIFY changed)              // 上次更新失败了（正在用上次的或者没有）
    Q_PROPERTY(QStringList currencies READ currencies CONSTANT)
    Q_PROPERTY(QString src READ src NOTIFY changed)                 // 表的来源
    Q_PROPERTY(double updatedAt READ updatedAt NOTIFY changed)      // 取到的时间（毫秒）
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool netOff READ netOff NOTIFY changed)              // 这项联网关着（0.12.5）

public:
    RatesController(Settings *settings, services::NetClient *net, QObject *parent = nullptr);

    QString main() const { return m_main; }
    void setMain(const QString &cur);
    QString date() const { return m_rates.date; }
    bool failed() const { return m_failed; }
    QStringList currencies() const { return map::plans::currencyCodes(); }
    const map::plans::Rates &table() const { return m_rates; }
    QString src() const { return m_rates.src; }
    double updatedAt() const { return m_at; }
    bool busy() const { return m_busy; }
    bool netOff() const;

    // 需要时更新（用到汇率的页面打开时调）
    Q_INVOKABLE void ensure();
    // "现在更新"：不看 20 小时。这项联网关着时返回 false
    Q_INVOKABLE bool refreshNow();
    // 表是新的但缺用到的货币时补一次（打开计划、账本，数据变了时调）；没缺、刚补过、联网关着都什么也不做
    void fillMissing();
    void setUsedProvider(std::function<QStringList()> f) { m_used = std::move(f); }

    using Fetch = std::function<void(const QUrl &, std::function<void(int httpStatus, const QByteArray &body, int netError)>)>;
    void setFetchForTest(Fetch f) { m_fetch = std::move(f); }
    void setUsedForTest(const QStringList &used) { m_testUsed = used; }   // 假联网时用这个，不看真数据
    void resetForTest(const QString &main, const map::plans::Rates &r, double at);
    int requests() const { return m_requests; }

signals:
    void changed();
    void busyChanged();

private:
    void fetch(int step);
    void request(const QUrl &url, std::function<void(int, const QByteArray &, int)> done);
    QStringList missingUsed() const;
    void store(const map::plans::Rates &r);
    void persist();

    Settings *m_settings;
    services::NetClient *m_net;
    Fetch m_fetch;
    std::function<QStringList()> m_used;
    QStringList m_testUsed;
    double m_fillAt = 0;        // 上次补成功的时间（settings.json 的 ratesFillAt）
    double m_fillTriedAt = 0;   // 上次试着补的时间（只在内存里：失败后 1 小时内不再试，免得每次刷新都打对方）
    QString m_main;
    map::plans::Rates m_rates;
    double m_at = 0;
    bool m_failed = false;
    bool m_busy = false;
    int m_requests = 0;
};

} // namespace tp::app
