#include "ratescontroller.h"

#include "netclient.h"
#include "settings.h"

#include <QDateTime>

namespace tp::app {

using json::Value;
namespace plans = tp::map::plans;

namespace {
constexpr double kFreshMs = 20 * 3600e3;
constexpr double kFillRetryMs = 3600e3;
constexpr int kTimeoutMs = 25000;
const QString kService = QStringLiteral("rates");
}

RatesController::RatesController(Settings *settings, services::NetClient *net, QObject *parent)
    : QObject(parent), m_settings(settings), m_net(net)
{
    m_main = settings->value(QStringLiteral("currency")).toString();
    if (m_main.isEmpty()) m_main = QStringLiteral("CNY");
    const Value t = settings->value(QStringLiteral("rates"));
    m_rates.base = t[QStringLiteral("base")].toString();
    m_rates.date = t[QStringLiteral("date")].toString();
    m_rates.src = t[QStringLiteral("src")].toString();
    for (const auto &[k, v] : t[QStringLiteral("rates")].asObject().members())
        if (v.toNumber() > 0) m_rates.table.insert(k, v.toNumber());
    m_at = settings->value(QStringLiteral("ratesAt")).toNumber();
    m_fillAt = settings->value(QStringLiteral("ratesFillAt")).toNumber();
    if (m_net) connect(m_net, &services::NetClient::switchesChanged, this, &RatesController::changed);
}

bool RatesController::netOff() const { return !m_fetch && (!m_net || !m_net->allows(kService)); }

void RatesController::setMain(const QString &cur)
{
    if (cur.isEmpty() || cur == m_main) return;
    m_main = cur;
    m_settings->setValue(QStringLiteral("currency"), Value(cur));
    m_settings->save();
    emit changed();
    ensure();
}

void RatesController::ensure()
{
    if (m_busy) return;
    if (!m_rates.table.isEmpty() && m_rates.base == m_main && QDateTime::currentMSecsSinceEpoch() - m_at < kFreshMs) { fillMissing(); return; }
    refreshNow();
}

QStringList RatesController::missingUsed() const
{
    const QStringList used = m_fetch ? m_testUsed : (m_used ? m_used() : QStringList());
    QStringList out;
    for (const QString &c : used)
        if (c != m_rates.base && !m_rates.table.contains(c)) out << c;
    return out;
}

void RatesController::fillMissing()
{
    if (m_busy || netOff()) return;
    if (m_rates.table.isEmpty() || m_rates.base != m_main || m_rates.src == QLatin1String("er-api")) return;   // 表本来就是它给的，再问也没有
    const double now = double(QDateTime::currentMSecsSinceEpoch());
    if (now - m_fillAt < kFreshMs || now - m_fillTriedAt < kFillRetryMs) return;
    const QStringList missing = missingUsed();
    if (missing.isEmpty()) return;
    m_fillTriedAt = now;
    m_busy = true;
    emit busyChanged();
    const QString base = m_rates.base;
    request(QUrl(QStringLiteral("https://open.er-api.com/v6/latest/") + base), [this, base, missing](int status, const QByteArray &body, int netError) {
        m_busy = false;
        emit busyChanged();
        plans::Rates r;
        if (netError == 0) r = plans::parseRates(body, QStringLiteral("er-api"), base);
        if (r.table.isEmpty()) {
            qWarning("汇率：补缺的货币失败（HTTP %d，网络错误 %d）", status, netError);   // 不算更新失败：Frankfurter 的表还是好的，页面写着缺哪种
            return;
        }
        if (m_rates.base == base) {
            const int added = plans::mergeRates(m_rates, r);
            m_fillAt = double(QDateTime::currentMSecsSinceEpoch());
            persist();
            qInfo("汇率：open.er-api.com 补了 %d 种（要的是 %s）", added, qUtf8Printable(missing.join(u' ')));
            emit changed();
        }
        if (base != m_main) ensure();   // 取的时候又换了主货币
    });
}

bool RatesController::refreshNow()
{
    if (m_busy) return true;
    if (netOff()) return false;
    m_busy = true;
    emit busyChanged();
    fetch(0);
    return true;
}

void RatesController::fetch(int step)
{
    const bool first = step == 0;
    const QUrl url(first ? QStringLiteral("https://api.frankfurter.dev/v1/latest?base=") + m_main
                         : QStringLiteral("https://open.er-api.com/v6/latest/") + m_main);
    const QString base = m_main;
    auto done = [this, first, step, base](int status, const QByteArray &body, int netError) {
        plans::Rates r;
        if (netError == 0) r = plans::parseRates(body, first ? QStringLiteral("frankfurter") : QStringLiteral("er-api"), base);
        if (!r.table.isEmpty()) {
            m_busy = false;
            emit busyChanged();
            m_failed = false;
            const double now = double(QDateTime::currentMSecsSinceEpoch());
            if (m_rates.base != r.base) m_fillAt = 0;   // 换了基准，补过的数不算了
            else if (now - m_fillAt < kFreshMs) plans::mergeRates(r, m_rates);   // 20 小时内补过：留着，不再问一次
            store(r);
            qInfo("汇率：更新了（%s，%s，%d 种）", qUtf8Printable(r.src), qUtf8Printable(r.date), int(r.table.size()));
            emit changed();
            if (base != m_main) ensure();   // 取的时候又换了主货币
            else fillMissing();
            return;
        }
        qWarning("汇率：第 %d 个来源失败（HTTP %d，网络错误 %d）", step + 1, status, netError);
        if (first) { fetch(1); return; }
        m_busy = false;
        emit busyChanged();
        m_failed = true;
        emit changed();
    };
    request(url, done);
}

void RatesController::request(const QUrl &url, std::function<void(int, const QByteArray &, int)> done)
{
    ++m_requests;
    if (m_fetch) { m_fetch(url, done); return; }
    m_net->get(url, kTimeoutMs, this, [done](const services::NetClient::Reply &r) { done(r.status, r.body, int(r.error)); });
}

void RatesController::store(const plans::Rates &r)
{
    m_rates = r;
    m_at = double(QDateTime::currentMSecsSinceEpoch());
    persist();
}

void RatesController::persist()
{
    const plans::Rates &r = m_rates;
    json::Object table, o;
    for (auto it = r.table.begin(); it != r.table.end(); ++it) table.set(it.key(), Value(it.value()));
    o.set(QStringLiteral("base"), Value(r.base));
    o.set(QStringLiteral("date"), Value(r.date));
    o.set(QStringLiteral("src"), Value(r.src));
    o.set(QStringLiteral("rates"), Value(std::move(table)));
    m_settings->setValue(QStringLiteral("rates"), Value(std::move(o)));
    m_settings->setValue(QStringLiteral("ratesAt"), Value(m_at));
    m_settings->setValue(QStringLiteral("ratesFillAt"), Value(m_fillAt));
    m_settings->save();
}

void RatesController::resetForTest(const QString &main, const plans::Rates &r, double at)
{
    m_main = main;
    m_rates = r;
    m_at = at;
    m_fillAt = m_fillTriedAt = 0;
    m_failed = false;
    m_settings->setValue(QStringLiteral("currency"), Value(main));
    persist();   // 自检的假汇率不能留在 settings.json 里
    emit changed();
}

} // namespace tp::app
