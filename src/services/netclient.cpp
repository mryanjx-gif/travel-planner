#include "netclient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <algorithm>

namespace tp::services {

namespace {
// Electron v6.3.0 用到的全部联网服务。名字是开关的键（settings.json 的 net；Electron TPPrefs.net 的键名）
const NetClient::Service kServices[] = {
    { QStringLiteral("routing"), QStringLiteral("routing.openstreetmap.de"), 1100 },     // OSRM：≥1.1 秒（map-app.js fetchRoad）
    { QStringLiteral("geocode"), QStringLiteral("nominatim.openstreetmap.org"), 1000 },  // Nominatim：每秒最多 1 次（项目说明第六条）
    { QStringLiteral("weather"), QStringLiteral("api.open-meteo.com"), 0 },              // 天气；海拔和时区也在这个主机，请求时点名 elev
    { QStringLiteral("climate"), QStringLiteral("archive-api.open-meteo.com"), 0 },      // 往年气候
    { QStringLiteral("rates"), QStringLiteral("api.frankfurter.dev"), 0 },               // 汇率（main.js rates:fetch）
    { QStringLiteral("rates"), QStringLiteral("open.er-api.com"), 0 },                   // 汇率（Frankfurter 失败时）
    { QStringLiteral("tiles"), QStringLiteral("server.arcgisonline.com"), 0 },          // Esri 底图瓦片（0.8.5 起经本机瓦片服务）
    // 1.4.0 地点介绍：维基系的 API 礼仪要求一个一个发（API:Etiquette），隔 0.2 秒
    { QStringLiteral("place"), QStringLiteral("zh.wikipedia.org"), 200 },
    { QStringLiteral("place"), QStringLiteral("en.wikipedia.org"), 200 },
    { QStringLiteral("place"), QStringLiteral("www.wikidata.org"), 200 },
    { QStringLiteral("place"), QStringLiteral("commons.wikimedia.org"), 200 },
    { QStringLiteral("place"), QStringLiteral("upload.wikimedia.org"), 200 },
};
} // namespace

NetClient::NetClient(QObject *parent) : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const Service &s : kServices) addService(s);
}

NetClient::~NetClient()
{
    // 还在飞的请求：断开再删，不再回调
    for (QNetworkReply *r : std::as_const(m_inFlight)) { r->disconnect(this); r->abort(); r->deleteLater(); }
}

QString NetClient::userAgentFor(const QString &version)
{
    return QStringLiteral("TravelPlanner/%1 (personal desktop app)").arg(version);
}

void NetClient::addService(const Service &s)
{
    const QString host = s.host.toLower();
    m_byHost.insert(host, s);
    Host &h = m_hosts[host];
    h.minIntervalMs = s.minIntervalMs;
}

QString NetClient::serviceForHost(const QString &host) const
{
    const auto it = m_byHost.constFind(host.toLower());
    return it == m_byHost.constEnd() ? QString() : it->name;
}

void NetClient::setOffline(bool on)
{
    if (on == m_offline) return;
    m_offline = on;
    emit switchesChanged();
}

void NetClient::setServiceEnabled(const QString &service, bool on)
{
    if (on == !m_off.contains(service)) return;
    if (on) m_off.remove(service);
    else m_off.insert(service);
    emit switchesChanged();
}

int NetClient::queued() const
{
    int n = 0;
    for (const Host &h : m_hosts) n += int(h.queue.size());
    return n;
}

quint64 NetClient::get(const QUrl &url, int timeoutMs, QObject *context, Callback cb, const QByteArray &accept, const QString &service, const QByteArray &acceptLanguage)
{
    Job job;
    job.id = m_next++;
    job.url = url;
    job.timeoutMs = timeoutMs > 0 ? timeoutMs : kDefaultTimeoutMs;
    job.context = context;
    job.hasContext = context != nullptr;
    job.cb = std::move(cb);
    job.accept = accept;
    job.acceptLanguage = acceptLanguage;

    const QString scheme = url.scheme().toLower();
    const QString host = url.host().toLower();
    Error reject = Error::None;
    if (!url.isValid() || !(scheme == QLatin1String("https") || (m_allowHttp && scheme == QLatin1String("http"))) || !m_byHost.contains(host))
        reject = Error::NotAllowed;
    else if (m_offline) reject = Error::Offline;
    else if (m_off.contains(service.isEmpty() ? m_byHost.value(host).name : service)) reject = Error::ServiceOff;
    if (reject != Error::None) {
        if (reject == Error::NotAllowed) qWarning("联网请求被拒绝（不在白名单）：%s", qUtf8Printable(url.toString(QUrl::RemoveQuery)));
        Reply r;
        r.error = reject;
        // 回调放到下一轮事件循环，调用方拿到编号以后才收到结果（和正常请求一样是异步的）
        QTimer::singleShot(0, this, [this, job, r] { finish(job, r); });
        return job.id;
    }
    m_hosts[host].queue.push_back(std::move(job));
    const quint64 id = m_hosts[host].queue.back().id;
    pump(host);
    return id;
}

void NetClient::cancel(quint64 id)
{
    for (Host &h : m_hosts) {
        const auto it = std::find_if(h.queue.begin(), h.queue.end(), [id](const Job &j) { return j.id == id; });
        if (it != h.queue.end()) { h.queue.erase(it); return; }
    }
    if (QNetworkReply *r = m_inFlight.value(id)) {
        r->setProperty("tpCanceled", true);
        r->abort();
    }
}

void NetClient::pump(const QString &host)
{
    Host &h = m_hosts[host];
    while (!h.queue.empty()) {
        if (h.minIntervalMs > 0) {
            if (h.busy) return;
            if (h.lastStart.isValid() && h.lastStart.elapsed() < h.minIntervalMs) {
                if (!h.timer) {
                    h.timer = new QTimer(this);
                    h.timer->setSingleShot(true);
                    connect(h.timer, &QTimer::timeout, this, [this, host] { pump(host); });
                }
                if (!h.timer->isActive()) h.timer->start(int(h.minIntervalMs - h.lastStart.elapsed()) + 1);
                return;
            }
        }
        Job job = std::move(h.queue.front());
        h.queue.pop_front();
        if (job.hasContext && !job.context) continue;   // 发请求的那一方已经没了
        start(host, std::move(job));
    }
}

void NetClient::start(const QString &host, Job job)
{
    Host &h = m_hosts[host];
    h.busy = true;
    h.lastStart.start();
    QNetworkRequest req(job.url);
    // 用原始请求头写（Qt 6.12 的 setHeader(UserAgentHeader, QString) 有时报"QVariant of type QString cannot be used"、不生效）
    req.setRawHeader("User-Agent", m_ua.toUtf8());
    req.setRawHeader("Accept", job.accept.isEmpty() ? QByteArrayLiteral("*/*") : job.accept);
    if (!job.acceptLanguage.isEmpty()) req.setRawHeader("Accept-Language", job.acceptLanguage);   // 1.4.0 中文维基按简体给
    QNetworkReply *reply = m_nam->get(req);
    m_inFlight.insert(job.id, reply);
    // 超时自己计（Qt 的 transferTimeout 在不同版本里报的错误码不一样，这里统一成 Timeout）
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply] { reply->setProperty("tpTimedOut", true); reply->abort(); });
    timer->start(job.timeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, host, job, reply] {
        m_inFlight.remove(job.id);
        Reply r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->isOpen()) r.body = reply->readAll();
        if (reply->property("tpTimedOut").toBool()) r.error = Error::Timeout;
        else if (reply->property("tpCanceled").toBool()) r.error = Error::Canceled;
        else if (r.status >= 400) r.error = Error::Http;
        else if (reply->error() != QNetworkReply::NoError) r.error = r.status >= 400 ? Error::Http : Error::Network;
        if (!r.ok()) r.detail = reply->errorString();
        reply->deleteLater();
        Host &hh = m_hosts[host];
        hh.busy = false;
        if (r.error != Error::Canceled) finish(job, r);
        pump(host);
    });
}

void NetClient::finish(const Job &job, const Reply &r)
{
    if (job.hasContext && !job.context) return;
    if (job.cb) job.cb(r);
}

} // namespace tp::services
