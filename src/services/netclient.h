// 统一请求通道（0.8.4，功能清单 N-07；Electron main.js 的 net:getJson + onBeforeSendHeaders）。
// 程序里所有联网请求都经过这里（0.8.5 起地图瓦片也是：maplibre 向本机瓦片服务要，瓦片服务再经这里取，见 tileserver.h）：
//   - 只允许白名单里的主机（https）；不在名单里的直接拒绝，不发出去
//   - User-Agent 一律 "TravelPlanner/<版本> (personal desktop app)"（OpenStreetMap 系服务要求可识别的 UA）
//   - 超时（默认 25 秒，Electron net:getJson；各服务可以自己给）
//   - 同一主机的最小间隔（公共服务器要求低频：OSRM 1.1 秒、Nominatim 1 秒）：排队一个一个发
//   - 离线总开关 + 每项服务单独的开关（设置页在阶段 ⑥；键名见 settings.json 的 offline / netOff）
// 只依赖 Qt Core + Qt Network（LGPL-3，技术选型.md §4 已核实），单元测试 tst_net 用本机的假服务器测。
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <deque>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace tp::services {

class NetClient : public QObject {
    Q_OBJECT
public:
    enum class Error {
        None,
        NotAllowed,   // 主机不在白名单 / 不是 https
        Offline,      // 离线总开关开着
        ServiceOff,   // 这项服务被关掉了
        Timeout,
        Network,      // 连不上（断网、DNS、TLS……）
        Http,         // 服务器回了 4xx / 5xx（body 照样给，有的服务把原因写在 JSON 里）
        Canceled,
    };
    struct Reply {
        Error error = Error::None;
        int status = 0;          // HTTP 状态码（没收到回复时是 0）
        QByteArray body;
        QString detail;          // 出错时 Qt 给的原因（写日志用，不直接给用户看）
        bool ok() const { return error == Error::None; }
    };
    using Callback = std::function<void(const Reply &)>;

    // 一项服务：名字（开关用）、主机、同一主机两次请求的最小间隔
    struct Service { QString name; QString host; int minIntervalMs = 0; };

    explicit NetClient(QObject *parent = nullptr);
    ~NetClient() override;

    // "TravelPlanner/<版本> (personal desktop app)"（项目说明第四条：按 Electron 代码的写法）
    static QString userAgentFor(const QString &version);
    void setUserAgent(const QString &ua) { m_ua = ua; }
    QString userAgent() const { return m_ua; }

    // 白名单（默认已经登记了 Electron 用到的全部服务，见 netclient.cpp 的 kServices）
    void addService(const Service &s);
    QString serviceForHost(const QString &host) const;
    // 测试用：允许 http（本机假服务器）
    void setAllowPlainHttpForTest(bool on) { m_allowHttp = on; }

    void setOffline(bool on);
    bool offline() const { return m_offline; }
    void setServiceEnabled(const QString &service, bool on);
    bool serviceEnabled(const QString &service) const { return !m_off.contains(service); }
    // 这项服务现在能不能用（不在离线模式、也没被单独关掉）
    bool allows(const QString &service) const { return !m_offline && serviceEnabled(service); }

    // GET。回调在主线程上调用；context 没了（被删）就不再回调。返回请求编号（cancel 用），被拒绝时也返回编号（回调已排队）
    // accept：请求头 Accept（默认要 JSON；瓦片要图片）
    quint64 get(const QUrl &url, int timeoutMs, QObject *context, Callback cb, const QByteArray &accept = QByteArrayLiteral("application/json"));
    void cancel(quint64 id);
    int queued() const;   // 排队还没发出去的（测试用）

    static constexpr int kDefaultTimeoutMs = 25000;

signals:
    // 离线总开关或某项服务的开关变了（0.8.5：地图离线时马上切到"已解锁"）
    void switchesChanged();

private:
    struct Job {
        quint64 id = 0;
        QUrl url;
        int timeoutMs = 0;
        QPointer<QObject> context;
        bool hasContext = false;
        Callback cb;
        QByteArray accept;
    };
    struct Host {
        int minIntervalMs = 0;
        std::deque<Job> queue;
        bool busy = false;           // 有间隔要求的主机同一时间只发一个
        QElapsedTimer lastStart;
        QTimer *timer = nullptr;
    };
    void pump(const QString &host);
    void start(const QString &host, Job job);
    void finish(const Job &job, const Reply &r);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_ua;
    QHash<QString, Service> m_byHost;
    QHash<QString, Host> m_hosts;
    QHash<quint64, QNetworkReply *> m_inFlight;
    QSet<QString> m_off;
    bool m_offline = false;
    bool m_allowHttp = false;
    quint64 m_next = 1;
};

} // namespace tp::services
