#include "planpagecontroller.h"

#include "appcontroller.h"
#include "datastore.h"
#include "netclient.h"
#include "plancontroller.h"
#include "planmoney.h"
#include "planpage.h"
#include "plans.h"
#include "ratescontroller.h"
#include "settings.h"

namespace tp::app {

namespace plans = tp::map::plans;
using json::Value;

namespace {
constexpr int kTzBatch = 50;         // Electron fillGeo 每批 50 个
constexpr int kTzTimeoutMs = 25000;
}

PlanPageController::PlanPageController(AppController *app, PlanController *pc, data::DataStore *store, services::NetClient *net,
                                       RatesController *rates, Settings *settings, QObject *parent)
    : QObject(parent), m_app(app), m_plans(pc), m_store(store), m_net(net), m_rates(rates), m_settings(settings)
{
    connect(m_rates, &RatesController::changed, this, &PlanPageController::refresh);
    connect(m_store, &data::DataStore::contentChanged, this, &PlanPageController::refresh);
    connect(m_app, &AppController::dataChanged, this, &PlanPageController::refresh);
    connect(m_plans, &PlanController::nowChanged, this, &PlanPageController::refresh);   // 过了零点、测试改"今天"
}

void PlanPageController::show(const QString &planId)
{
    if (planId == m_id) return;
    m_id = planId;
    refresh();
    if (!planId.isEmpty()) { fillTz(planId); m_rates->ensure(); }
}

void PlanPageController::showLedger(bool on)
{
    m_ledgerOn = on;
    if (on) { m_rates->ensure(); refresh(); }
}

void PlanPageController::refresh()
{
    if (m_ledgerOn) {
        const QVariantMap l = plans::ledgerInfo(m_store->data(), m_rates->main(), m_rates->table());
        if (l != m_ledger) { m_ledger = l; emit ledgerChanged(); }
    }
    QVariantMap m;
    if (!m_id.isEmpty()) {
        m = PlanController::withThumbs(plans::pageInfo(m_store->data(), m_id, m_plans->today()));
        if (!m.isEmpty() && m.value(QStringLiteral("coverThumb")).toString().isEmpty())
            for (const QVariant &s : m.value(QStringLiteral("stops")).toList()) {   // 没有封面：用第一张照片（Electron render）
                const QStringList t = s.toMap().value(QStringLiteral("thumbs")).toStringList();
                if (!t.isEmpty()) { m.insert(QStringLiteral("coverThumb"), t.first()); break; }
            }
        if (!m.isEmpty()) {
            m.insert(QStringLiteral("readOnly"), m_store->isReadOnly());
            const Value &p = *plans::planById(m_store->data(), m_id);
            m.insert(QStringLiteral("money"), plans::spendInfo(p, m_rates->main(), m_rates->table()));
            // J1：去掉点过"这几晚不用"的
            QStringList nights = m.value(QStringLiteral("nights")).toStringList();
            for (const Value &d : m_settings->value(QStringLiteral("noStayNights"))[m_id].asArray()) nights.removeAll(d.toString());
            m.insert(QStringLiteral("nights"), nights);
        }
    }
    if (m == m_info) return;
    m_info = m;
    emit changed();
}

void PlanPageController::edited()
{
    m_app->noteChanged(false);   // 停手 0.8 秒自动保存（P-18）
}

QString PlanPageController::saveSched(int k, int j, const QString &date, const QString &time, const QString &end, const QString &text)
{
    if (m_store->isReadOnly()) return QStringLiteral("readOnly");
    // 先在副本上试：有问题或没有变化时不碰数据（不标记未保存、不记撤销）
    Value copy = m_store->data();
    Value *s = plans::mutValidStop(copy, m_id, k);
    if (!s) return QStringLiteral("empty");
    const QString err = plans::putSchedule(*s, j, date, time, end, text);
    if (!err.isEmpty()) return err;
    if (copy == m_store->data()) return {};
    m_store->modify([&](Value &d) { d = copy; });
    edited();
    return {};
}

bool PlanPageController::deleteSched(int k, int j)
{
    if (m_store->isReadOnly()) return false;
    Value copy = m_store->data();
    Value *s = plans::mutValidStop(copy, m_id, k);
    if (!s || !plans::removeSchedule(*s, j)) return false;
    m_store->modify([&](Value &d) { d = copy; });
    edited();
    return true;
}

QString PlanPageController::setDays(const QString &text)
{
    if (m_store->isReadOnly()) return QStringLiteral("readOnly");
    Value copy = m_store->data();
    const QString err = plans::setDays(copy, m_id, text);
    if (!err.isEmpty() || copy == m_store->data()) return err;
    m_store->modify([&](Value &d) { d = copy; });
    edited();
    return {};
}

bool PlanPageController::startPlanning(const QString &start, const QString &end)
{
    if (m_store->isReadOnly() || !plans::planById(m_store->data(), m_id)) return false;
    m_store->modify([&](Value &d) { plans::startPlanning(d, m_id, start, end); });
    m_app->noteChanged(true);
    return true;
}

QString PlanPageController::applyPlan(const std::function<QString(Value &)> &fn)
{
    if (m_store->isReadOnly()) return QStringLiteral("readOnly");
    // 先在副本上试：有问题或没有变化时不碰数据（不标记未保存、不记撤销）
    Value copy = m_store->data();
    Value *p = plans::mutPlan(copy, m_id);
    if (!p) return QStringLiteral("gone");
    const QString err = fn(*p);
    if (!err.isEmpty() || copy == m_store->data()) return err;
    m_store->modify([&](Value &d) { d = copy; });
    edited();
    return {};
}

QString PlanPageController::saveBooking(const QString &id, const QVariantMap &f)
{
    return applyPlan([&](Value &p) { return plans::putBooking(p, id, f); });
}

int PlanPageController::deleteBooking(const QString &id)
{
    int n = -1;
    applyPlan([&](Value &p) { n = plans::removeBooking(p, id); return QString(); });
    return n;
}

QString PlanPageController::saveExpense(const QString &id, const QVariantMap &f)
{
    return applyPlan([&](Value &p) { return plans::putExpense(p, id, f); });
}

bool PlanPageController::deleteExpense(const QString &id)
{
    bool ok = false;
    applyPlan([&](Value &p) { ok = plans::removeExpense(p, id); return QString(); });
    return ok;
}

QString PlanPageController::setBudget(const QString &text, const QString &currency)
{
    return applyPlan([&](Value &p) { return plans::setBudget(p, text, currency); });
}

QString PlanPageController::editPerson(const QString &id, const QString &name, bool remove)
{
    return applyPlan([&](Value &p) { return plans::editPerson(p, id, name, remove); });
}

void PlanPageController::dismissNights()
{
    if (m_id.isEmpty()) return;
    Value all = m_settings->value(QStringLiteral("noStayNights"));
    if (!all.isObject()) all = Value::object();
    Value list = all[m_id].isArray() ? all[m_id] : Value::array();
    for (const QVariant &d : m_info.value(QStringLiteral("nights")).toList()) list.mutArray().push_back(Value(d.toString()));
    all.mutObject().set(m_id, std::move(list));
    m_settings->setValue(QStringLiteral("noStayNights"), std::move(all));
    m_settings->save();
    refresh();
}

// ---------------- 补时区（Electron weather.js fillGeo）：只补空的；失败只记日志，下次打开再试 ----------------

void PlanPageController::fillTz(const QString &planId)
{
    if (m_filling.contains(planId) || m_store->isReadOnly()) return;
    if (!m_fetch && (!m_net || !m_net->allows(QStringLiteral("weather")))) return;
    m_filling.insert(planId);
    fillBatch(planId);
}

void PlanPageController::fillBatch(const QString &planId)
{
    const Value *p = plans::planById(m_store->data(), planId);
    QVector<plans::TzStop> need = p ? plans::stopsWithoutTz(*p) : QVector<plans::TzStop>();
    if (need.isEmpty() || m_store->isReadOnly()) { m_filling.remove(planId); return; }
    if (need.size() > kTzBatch) need.resize(kTzBatch);
    QStringList ids;
    for (const plans::TzStop &s : need) ids << s.id;

    auto done = [this, planId, ids](int status, const QByteArray &body, int netError) {
        const QStringList tz = netError == 0 ? plans::parseTz(body, int(ids.size())) : QStringList();
        int n = 0;
        if (!m_store->isReadOnly() && !tz.join(QString()).isEmpty()) {
            m_store->modify([&](Value &d) { n = plans::applyTz(d, planId, ids, tz); });
            if (n > 0) edited();
        }
        qInfo("时区：补了 %d 站（共问 %d 站，HTTP %d）", n, int(ids.size()), status);
        // 这一批一个都没补上（网络问题、回复认不出）就停，免得反复请求
        if (n > 0) fillBatch(planId);
        else m_filling.remove(planId);
    };
    ++m_tzRequests;
    const QUrl url = plans::tzUrl(need);
    if (m_fetch) { m_fetch(url, done); return; }
    m_net->get(url, kTzTimeoutMs, this, [done](const services::NetClient::Reply &r) { done(r.status, r.body, int(r.error)); });
}

} // namespace tp::app
