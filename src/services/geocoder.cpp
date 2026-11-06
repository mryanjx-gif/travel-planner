#include "geocoder.h"

#include "atomicfile.h"
#include "netclient.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>

namespace tp::services {

using Err = NetClient::Error;

Geocoder::Geocoder(QObject *parent) : QObject(parent) {}

void Geocoder::setBase(const QString &b)
{
    QString base = b.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    const QUrl u(base);
    if (base.isEmpty() || !u.isValid() || u.scheme() != QLatin1String("https") || u.host().isEmpty()) {
        if (!base.isEmpty()) qWarning("地名搜索：服务地址 %s 不能用（要 https），改用默认的", qUtf8Printable(base));
        base = defaultBase();
    }
    m_base = base;
    if (m_net && m_net->serviceForHost(QUrl(m_base).host()).isEmpty())
        m_net->addService({ QStringLiteral("geocode"), QUrl(m_base).host(), 1000 });
}

void Geocoder::setCacheFile(const QString &path)
{
    m_cacheFile = path;
    loadCache();
}

QUrl Geocoder::searchUrl(const QString &q) const
{
    // Electron：…/search?format=jsonv2&limit=8&accept-language=en&q=' + encodeURIComponent(q)
    return QUrl::fromEncoded((m_base + QStringLiteral("/search?format=jsonv2&limit=%1&accept-language=en&q=").arg(kLimit)).toUtf8()
                             + QUrl::toPercentEncoding(q));
}

QUrl Geocoder::reverseUrl(double lat, double lng) const
{
    QUrl u(m_base + QStringLiteral("/reverse"));
    QUrlQuery qq;
    qq.addQueryItem(QStringLiteral("format"), QStringLiteral("jsonv2"));
    qq.addQueryItem(QStringLiteral("accept-language"), QStringLiteral("en"));
    qq.addQueryItem(QStringLiteral("lat"), QString::number(lat, 'f', 6));
    qq.addQueryItem(QStringLiteral("lon"), QString::number(lng, 'f', 6));
    u.setQuery(qq);
    return u;
}

QString Geocoder::nameOf(const QString &name, const QString &display)
{
    if (!name.isEmpty()) return name;
    return display.section(QLatin1Char(','), 0, 0);
}

QVariantList Geocoder::parseSearch(const QByteArray &body, bool *ok)
{
    QVariantList out;
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !d.isArray()) { if (ok) *ok = false; return out; }
    for (const QJsonValue &v : d.array()) {
        const QJsonObject o = v.toObject();
        const QString display = o.value(QStringLiteral("display_name")).toString();
        bool la = false, lo = false;
        // jsonv2 的 lat / lon 是字符串
        const double lat = o.value(QStringLiteral("lat")).isString() ? o.value(QStringLiteral("lat")).toString().toDouble(&la) : (la = o.value(QStringLiteral("lat")).isDouble(), o.value(QStringLiteral("lat")).toDouble());
        const double lng = o.value(QStringLiteral("lon")).isString() ? o.value(QStringLiteral("lon")).toString().toDouble(&lo) : (lo = o.value(QStringLiteral("lon")).isDouble(), o.value(QStringLiteral("lon")).toDouble());
        if (!la || !lo || display.isEmpty()) continue;
        out << QVariantMap{ { QStringLiteral("name"), nameOf(o.value(QStringLiteral("name")).toString(), display) },
                            { QStringLiteral("address"), display },
                            { QStringLiteral("lat"), lat },
                            { QStringLiteral("lng"), lng } };
    }
    if (ok) *ok = true;
    return out;
}

QString Geocoder::parseReverse(const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QString display = o.value(QStringLiteral("display_name")).toString();
    if (display.isEmpty()) return {};
    return nameOf(o.value(QStringLiteral("name")).toString(), display);
}

QString Geocoder::errorText(int netError, int status) const
{
    switch (Err(netError)) {
    case Err::Offline: return trd("联网已关闭（离线模式）");
    case Err::ServiceOff: return trd("地名搜索已关闭（设置 → 联网服务）");
    case Err::Timeout: return trd("地名服务超时");
    case Err::NotAllowed: return trd("不允许的地址");
    case Err::Http:
        if (status == 403 || status == 429) return trd("地名服务拒绝了请求（HTTP %1），可能是请求太多被限流，或者服务停用了").arg(status);
        return trd("地名服务出错（HTTP %1）").arg(status);
    default: return trd("连不上地名服务（可能没联网，或者服务暂时不可用）");
    }
}

void Geocoder::request(const QUrl &url, QObject *context, std::function<void(int, const QByteArray &, int)> done)
{
    // 两次请求至少隔 1.1 秒（Electron lastReq）；NetClient 本身对 Nominatim 也排队隔 1 秒
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 start = std::max(nowMs, m_lastStart + kMinIntervalMs);
    m_lastStart = start;
    QPointer<QObject> ctx(context);
    auto go = [this, url, ctx, done] {
        if (!ctx) return;
        ++m_requests;
        if (m_fetch) { m_fetch(url, done); return; }
        if (!m_net) { done(0, {}, int(Err::Network)); return; }
        m_net->get(url, kTimeoutMs, ctx, [done](const NetClient::Reply &r) { done(r.status, r.body, int(r.error)); });
    };
    if (start > nowMs) QTimer::singleShot(int(start - nowMs), this, go);
    else go();
}

void Geocoder::search(const QString &q0, QObject *context, SearchCb cb)
{
    const QString q = q0.trimmed();
    if (q.isEmpty()) { cb({}, {}, true); return; }
    const QString key = QStringLiteral("s|") + m_base + QLatin1Char('|') + q.toLower();
    QVariant hit;
    if (cached(key, &hit)) { cb(hit.toList(), {}, true); return; }
    request(searchUrl(q), context, [this, key, cb](int status, const QByteArray &body, int netError) {
        if (Err(netError) != Err::None) {
            const QString e = errorText(netError, status);
            qInfo("地名搜索失败：%s", qUtf8Printable(e));
            cb({}, e, false);
            return;
        }
        bool ok = false;
        const QVariantList rows = parseSearch(body, &ok);
        if (!ok) { cb({}, trd("地名服务返回异常（HTTP %1）").arg(status), false); return; }
        remember(key, rows);
        cb(rows, {}, false);
    });
}

void Geocoder::reverse(double lat, double lng, QObject *context, ReverseCb cb)
{
    const QString key = QStringLiteral("r|") + m_base + QLatin1Char('|') + QString::number(lat, 'f', 5) + QLatin1Char(',') + QString::number(lng, 'f', 5);
    QVariant hit;
    if (cached(key, &hit)) { cb(hit.toString()); return; }
    request(reverseUrl(lat, lng), context, [this, key, cb](int, const QByteArray &body, int netError) {
        if (Err(netError) != Err::None) { cb({}); return; }
        const QString name = parseReverse(body);
        if (!name.isEmpty()) remember(key, name);
        cb(name);
    });
}

bool Geocoder::cached(const QString &key, QVariant *out)
{
    auto it = m_cache.find(key);
    if (it == m_cache.end()) return false;
    if (now().toMSecsSinceEpoch() - it->t > kCacheDays * 86400000LL) { m_cache.erase(it); return false; }
    *out = it->v;
    return true;
}

void Geocoder::remember(const QString &key, const QVariant &v)
{
    m_cache.insert(key, { now().toMSecsSinceEpoch(), v });
    while (m_cache.size() > kCacheMax) {   // 丢掉最旧的
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->t < oldest->t) oldest = it;
        m_cache.erase(oldest);
    }
    saveCache();
}

void Geocoder::loadCache()
{
    m_cache.clear();
    if (m_cacheFile.isEmpty()) return;
    QFile f(m_cacheFile);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const qint64 nowMs = now().toMSecsSinceEpoch();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        const qint64 t = qint64(e.value(QStringLiteral("t")).toDouble());
        if (nowMs - t > kCacheDays * 86400000LL) continue;
        m_cache.insert(it.key(), { t, e.value(QStringLiteral("v")).toVariant() });
    }
}

void Geocoder::saveCache()
{
    if (m_cacheFile.isEmpty()) return;
    QJsonObject o;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
        o.insert(it.key(), QJsonObject{ { QStringLiteral("t"), double(it->t) }, { QStringLiteral("v"), QJsonValue::fromVariant(it->v) } });
    const tp::platform::WriteResult w = tp::platform::writeViaTemp(m_cacheFile, m_cacheFile + QStringLiteral(".tmp"),
                                                                   QJsonDocument(o).toJson(QJsonDocument::Compact));
    if (!w.ok) qWarning("地名搜索：缓存写不进去（%s）：%s", qUtf8Printable(m_cacheFile), qUtf8Printable(w.error));
}

} // namespace tp::services
