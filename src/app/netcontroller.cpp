#include "netcontroller.h"

#include "netclient.h"
#include "settings.h"
#include "tilecache.h"

namespace tp::app {

using json::Value;

namespace {
const QStringList &keys()
{
    static const QStringList k{ QStringLiteral("tiles"), QStringLiteral("geocode"), QStringLiteral("routing"), QStringLiteral("rates"),
                                QStringLiteral("weather"), QStringLiteral("climate"), QStringLiteral("elev"), QStringLiteral("place"), QStringLiteral("ai") };
    return k;
}
}

NetController::NetController(Settings *settings, services::NetClient *net, services::TileCache *tiles, QObject *parent)
    : QObject(parent), m_settings(settings), m_net(net), m_tiles(tiles)
{
    reload();
    connect(m_net, &services::NetClient::switchesChanged, this, &NetController::changed);
    connect(m_tiles, &services::TileCache::sizeChanged, this, &NetController::cacheChanged);
    connect(m_tiles, &services::TileCache::scanDone, this, &NetController::cacheChanged);
    connect(m_tiles, &services::TileCache::trashDone, this, &NetController::tileCacheCleared);
}

void NetController::reload()
{
    m_net->setOffline(m_settings->value(QStringLiteral("offline")).toBool());
    const Value n = m_settings->value(QStringLiteral("net"));
    for (const QString &k : keys()) {
        const Value v = n[k];
        m_net->setServiceEnabled(k, !(v.isBool() && !v.toBool()));   // Electron：P.net[k] === false 才算关
    }
    const double mb = m_settings->value(QStringLiteral("tileCacheMaxMB")).toNumber();
    if (mb > 0) m_tiles->setMaxBytes(qint64(mb) * 1048576);
}

bool NetController::offline() const { return m_net->offline(); }

void NetController::setOffline(bool on)
{
    m_settings->setValue(QStringLiteral("offline"), Value(on));
    m_settings->save();
    m_net->setOffline(on);
}

QVariantMap NetController::switches() const
{
    QVariantMap m;
    for (const QString &k : keys()) m.insert(k, m_net->serviceEnabled(k));
    return m;
}

QVariantMap NetController::allowed() const
{
    QVariantMap m;
    for (const QString &k : keys()) m.insert(k, m_net->allows(k));
    return m;
}

void NetController::setSwitch(const QString &key, bool on)
{
    if (!keys().contains(key)) return;
    const Value old = m_settings->value(QStringLiteral("net"));
    Value n = old.isObject() ? old : Value::object();   // 在原对象上改：别的键原样留着
    if (on) n.mutObject().remove(key);
    else n.mutObject().set(key, Value(false));
    m_settings->setValue(QStringLiteral("net"), std::move(n));
    m_settings->save();
    m_net->setServiceEnabled(key, on);
}

double NetController::tileBytes() const { return double(m_tiles->bytes()); }
bool NetController::tileScanned() const { return m_tiles->scanned(); }
bool NetController::tileClearing() const { return m_tiles->clearing(); }
void NetController::clearTileCache() { m_tiles->clearToTrash(); }
int NetController::tileMaxMB() const { return int(m_tiles->maxBytes() / 1048576); }

void NetController::setTileMaxMB(int mb)
{
    if (mb <= 0 || mb == tileMaxMB()) return;
    m_settings->setValue(QStringLiteral("tileCacheMaxMB"), Value(double(mb)));
    m_settings->save();
    m_tiles->setMaxBytes(qint64(mb) * 1048576);
    emit cacheChanged();
}
void NetController::offlineForShot() { m_net->setOffline(true); }

} // namespace tp::app
