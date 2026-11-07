#include "weathercontroller.h"

#include "datastore.h"
#include "netclient.h"
#include "plancontroller.h"
#include "planpagecontroller.h"
#include "plans.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace tp::app {

namespace plans = tp::map::plans;
using json::Value;

namespace {
constexpr qint64 kFreshSec = 3 * 3600, kKeepSec = 86400, kRetrySec = 600;
constexpr int kBatch = 50, kTimeoutMs = 25000;
qint64 nowSec() { return QDateTime::currentSecsSinceEpoch(); }
double lat(const Value &s) { return s[QStringLiteral("lat")].jsToNumber(); }
double lng(const Value &s) { return s[QStringLiteral("lng")].jsToNumber(); }
QString keyOf(const Value &s) { return plans::geoKey(lat(s), lng(s)); }
}

WeatherController::WeatherController(PlanPageController *page, PlanController *pc, data::DataStore *store, services::NetClient *net, QObject *parent)
    : QObject(parent), m_page(page), m_plans(pc), m_store(store), m_net(net)
{
    connect(m_page, &PlanPageController::changed, this, &WeatherController::update);
    connect(m_plans, &PlanController::nowChanged, this, &WeatherController::update);
    if (m_net) connect(m_net, &services::NetClient::switchesChanged, this, [this] { emit changed(); update(); });   // 0.12.5：设置里开关一变，气候按钮、预报跟着变
}

bool WeatherController::allowed(const QString &service) const { return m_net && m_net->allows(service); }

void WeatherController::setFetchForTest(Fetch f)
{
    m_fetch = std::move(f);
    m_wx.clear(); m_clim.clear(); m_tried.clear();
    m_id.clear(); m_climOn = false; m_climBusy = false; m_demo = false; m_requests = 0;
    if (!m_fetch) loadCache();
    recompute();
}

void WeatherController::get(const QUrl &url, std::function<void(int, const QByteArray &, int)> done)
{
    ++m_requests;
    if (m_fetch) { m_fetch(url, std::move(done)); return; }
    m_net->get(url, kTimeoutMs, this, [done = std::move(done)](const services::NetClient::Reply &r) { done(r.status, r.body, int(r.error)); });
}

bool WeatherController::fresh(const QString &key) const
{
    const auto it = m_wx.constFind(key);
    return it != m_wx.constEnd() && nowSec() - it->at < kFreshSec;
}

// 此刻卡在路上时今天所在的站
const Value *WeatherController::nowStop() const
{
    const QVariantMap now = m_plans->now();
    if (now.value(QStringLiteral("kind")).toString() != QLatin1String("live")) return nullptr;
    const Value *p = plans::planById(m_store->data(), now.value(QStringLiteral("planId")).toString());
    if (!p) return nullptr;
    const QVector<int> st = plans::validStops(*p);
    const int k = now.value(QStringLiteral("hereIndex"), -1).toInt();
    return k >= 0 && k < st.size() ? &(*p)[QStringLiteral("stops")].asArray()[st[k]] : nullptr;
}

// 打开的计划换了，或者数据、此刻变了：要问的问一下，再重新算
void WeatherController::update()
{
    if (m_page->planId() != m_id) { m_id = m_page->planId(); m_climOn = false; m_climError = false; m_wxFailed = false; }
    QVector<plans::GeoPoint> need;
    const auto add = [&](const Value &s) { need.push_back({ keyOf(s), lat(s), lng(s) }); };
    if (const Value *p = m_id.isEmpty() ? nullptr : plans::planById(m_store->data(), m_id); p && plans::inForecastWindow(*p, m_plans->today())) {
        const json::Array &stops = (*p)[QStringLiteral("stops")].asArray();
        for (int i : plans::validStops(*p)) add(stops[i]);
    }
    if (const Value *s = nowStop()) add(*s);
    requestForecast(need);
    recompute();
}

void WeatherController::requestForecast(const QVector<plans::GeoPoint> &points)
{
    if (m_demo || (!m_fetch && !allowed(QStringLiteral("weather")))) return;
    QVector<plans::GeoPoint> need;
    const qint64 now = nowSec();
    for (const plans::GeoPoint &p : points) {
        if (fresh(p.key) || now - m_tried.value(p.key, now - kRetrySec) < kRetrySec) continue;
        m_tried.insert(p.key, now);
        need.push_back(p);
    }
    for (qsizetype i = 0; i < need.size(); i += kBatch) {
        const QVector<plans::GeoPoint> part = need.mid(i, kBatch);
        get(plans::forecastUrl(part), [this, part](int status, const QByteArray &body, int netError) {
            if (netError != 0) { qInfo("天气预报：没取到（HTTP %d）", status); if (!m_wxFailed) { m_wxFailed = true; emit changed(); } return; }
            m_wxFailed = false;
            const QVector<QJsonObject> daily = plans::parseDaily(body, int(part.size()));
            const qint64 t = nowSec();
            for (int j = 0; j < part.size(); ++j) if (!daily[j].isEmpty()) m_wx.insert(part[j].key, Wx{ t, daily[j] });
            saveCache();
            recompute();
        });
    }
}

void WeatherController::retryForecast()
{
    m_wxFailed = false;
    m_tried.clear();
    emit changed();
    update();
}

void WeatherController::recompute()
{
    const QString today = m_plans->today();
    QVariantList out;
    if (const Value *p = m_id.isEmpty() ? nullptr : plans::planById(m_store->data(), m_id)) {
        const json::Array &stops = (*p)[QStringLiteral("stops")].asArray();
        const QVector<int> st = plans::validStops(*p);
        const bool window = m_demo || plans::inForecastWindow(*p, today);
        const plans::ClimPlan cp = m_climOn ? plans::climPlan(*p, today) : plans::ClimPlan();
        for (int k = 0; k < st.size(); ++k) {
            const Value &s = stops[st[k]];
            QVariantMap m;
            const QString key = keyOf(s);
            if (window && fresh(key)) {
                const QVariantMap d = plans::wxDay(m_wx[key].daily, plans::stopWxDate(s, today));
                if (!d.isEmpty()) m.insert(QStringLiteral("wx"), d);
            }
            if (m_climOn && k < cp.stopKeys.size()) {
                const QJsonObject c = m_clim.value(cp.stopKeys[k]);
                if (!c.isEmpty()) {
                    QVariantMap v = c.toVariantMap();
                    v.insert(QStringLiteral("month"), cp.stopKeys[k].section(QLatin1Char('@'), 1).toInt());
                    m.insert(QStringLiteral("clim"), v);
                }
            }
            out << m;
        }
    }
    QVariantMap todayWx;
    if (const Value *s = nowStop(); s && fresh(keyOf(*s))) todayWx = plans::wxDay(m_wx[keyOf(*s)].daily, today);
    if (out == m_stops && todayWx == m_today) { emit changed(); return; }
    m_stops = out;
    m_today = todayWx;
    emit changed();
}

// ---------------- 气候 ----------------

void WeatherController::fetchClimate()
{
    if (m_climBusy || (!m_fetch && !allowed(QStringLiteral("climate")))) return;
    m_climError = false;
    const Value *p = plans::planById(m_store->data(), m_id);
    if (!p) return;
    const QString today = m_plans->today();
    QHash<int, QVector<plans::ClimPoint>> byMonth;
    for (const plans::ClimPoint &c : plans::climPlan(*p, today).points) if (!m_clim.contains(c.key)) byMonth[c.month].push_back(c);
    m_climTasks.clear();
    m_climAcc.clear();
    for (auto it = byMonth.cbegin(); it != byMonth.cend(); ++it)
        for (int y : plans::climYears(it.key(), today)) m_climTasks.push_back({ y, it.key(), it.value() });
    m_climBusy = true;
    emit changed();
    climNext();
}

// 一个一个地问（一个月最多 10 次、每次最多 12 个点），公共服务低频用
void WeatherController::climNext()
{
    if (m_climTasks.isEmpty()) {
        const QString today = m_plans->today();
        for (auto it = m_climAcc.cbegin(); it != m_climAcc.cend(); ++it) {
            const QJsonObject v = plans::climResult(it.value(), plans::climYears(it.key().section(QLatin1Char('@'), 1).toInt(), today));
            if (!v.isEmpty()) m_clim.insert(it.key(), v);
        }
        m_climAcc.clear();
        saveCache();
        m_climBusy = false;
        m_climOn = true;
        recompute();
        return;
    }
    const ClimTask t = m_climTasks.takeFirst();
    get(plans::climateUrl(t.points, t.year, t.month), [this, t](int status, const QByteArray &body, int netError) {
        if (netError != 0) {
            qInfo("往年气候：没取到（HTTP %d）", status);
            m_climTasks.clear();
            m_climAcc.clear();
            m_climBusy = false;
            m_climError = true;
            emit changed();
            emit climateFailed();
            return;
        }
        QVector<plans::ClimAcc> acc;
        for (const plans::ClimPoint &c : t.points) acc << m_climAcc.value(c.key);
        plans::climAdd(acc, body);
        for (int j = 0; j < t.points.size(); ++j) m_climAcc.insert(t.points[j].key, acc[j]);
        climNext();
    });
}

// ---------------- 缓存和截图 ----------------

void WeatherController::loadCache()
{
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject wx = o.value(QStringLiteral("wx")).toObject(), clim = o.value(QStringLiteral("clim")).toObject();
    const qint64 now = nowSec();
    for (auto it = wx.begin(); it != wx.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        const qint64 at = e.value(QStringLiteral("at")).toInteger();
        if (now - at < kKeepSec) m_wx.insert(it.key(), Wx{ at, e.value(QStringLiteral("daily")).toObject() });
    }
    for (auto it = clim.begin(); it != clim.end(); ++it) m_clim.insert(it.key(), it.value().toObject());
}

void WeatherController::saveCache() const
{
    if (m_fetch || m_demo) return;
    QJsonObject wx, clim;
    const qint64 now = nowSec();
    for (auto it = m_wx.cbegin(); it != m_wx.cend(); ++it)
        if (now - it->at < kKeepSec) wx.insert(it.key(), QJsonObject{ { QStringLiteral("at"), it->at }, { QStringLiteral("daily"), it->daily } });
    for (auto it = m_clim.cbegin(); it != m_clim.cend(); ++it) clim.insert(it.key(), it.value());
    QSaveFile f(m_file);   // 只是缓存，写失败下次再取
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{ { QStringLiteral("wx"), wx }, { QStringLiteral("clim"), clim } }).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void WeatherController::fillDemoForShot()
{
    m_demo = true;
    const QString today = m_plans->today();
    const auto addPlan = [&](const QString &id, bool clim) {
        const Value *p = plans::planById(m_store->data(), id);
        if (!p) return;
        const json::Array &stops = (*p)[QStringLiteral("stops")].asArray();
        const QVector<int> codes{ 0, 2, 61, 80, 71, 95, 3 };
        const QVector<int> st = plans::validStops(*p);
        QJsonArray time{ today };
        for (int i : st) time.append(plans::stopWxDate(stops[i], today));
        int n = 0;
        for (int i : st) {
            const int c = codes[n % codes.size()];
            const double hi = 12 + (n++ % 9);
            QJsonArray cs, his, los, pops;
            for (int j = 0; j < time.size(); ++j) { cs.append(c); his.append(hi); los.append(hi - 7); pops.append(60); }
            m_wx.insert(keyOf(stops[i]), Wx{ nowSec(), QJsonObject{
                { QStringLiteral("time"), time }, { QStringLiteral("weather_code"), cs }, { QStringLiteral("temperature_2m_max"), his },
                { QStringLiteral("temperature_2m_min"), los }, { QStringLiteral("precipitation_probability_max"), pops } } });
        }
        if (clim) {
            for (const plans::ClimPoint &c : plans::climPlan(*p, today).points)
                m_clim.insert(c.key, QJsonObject{ { QStringLiteral("hi"), 26.0 }, { QStringLiteral("lo"), 14.0 }, { QStringLiteral("rainDays"), 7.0 }, { QStringLiteral("mm"), 86.0 }, { QStringLiteral("years"), QStringLiteral("2016–2025") } });
            m_climOn = true;
        }
    };
    addPlan(m_page->planId(), true);
    addPlan(m_plans->now().value(QStringLiteral("planId")).toString(), false);
    recompute();
}

} // namespace tp::app
