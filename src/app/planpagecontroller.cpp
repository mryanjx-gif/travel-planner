#include "planpagecontroller.h"

#include "appcontroller.h"
#include "datastore.h"
#include "netclient.h"
#include "plancontroller.h"
#include "plankit.h"
#include "planmoney.h"
#include "planpage.h"
#include "planweather.h"
#include "plans.h"
#include "ratescontroller.h"
#include "settings.h"
#include "thumbprovider.h"

namespace tp::app {

namespace plans = tp::map::plans;
using json::Value;

namespace {
constexpr int kTzBatch = 50;         // Electron fillGeo 每批 50 个
constexpr int kTzTimeoutMs = 25000;
constexpr int kElevBatch = 100;      // Electron fillGeo 每批 100 个
constexpr int kGuidePhotoWidth = 960;   // 攻略正文最宽约 600，高分屏也够

// file → thumb（正文里的照片、游记首图）
QVariantList withThumb(QVariantList rows, int width)
{
    for (QVariant &v : rows) {
        QVariantMap m = v.toMap();
        const QString f = m.value(QStringLiteral("file")).toString();
        if (!f.startsWith(QLatin1String("photos/"))) continue;
        m.insert(QStringLiteral("thumb"), ThumbProvider::urlFor(f, width, 0));
        v = m;
    }
    return rows;
}
QVariantMap guideWithThumbs(const Value &root, const QString &text)
{
    QVariantMap g = plans::guideBlocks(root, text);
    g.insert(QStringLiteral("blocks"), withThumb(g.value(QStringLiteral("blocks")).toList(), kGuidePhotoWidth));
    return g;
}
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
    if (!planId.isEmpty()) { fillTz(planId); fillElev(planId); m_rates->ensure(); }
}

void PlanPageController::showLedger(bool on)
{
    m_ledgerOn = on;
    if (on) { m_rates->ensure(); refresh(); }
}

void PlanPageController::refresh()
{
    if (m_ledgerOn) {
        const QVariantMap l = plans::ledgerInfo(m_store->data(), m_rates->main(), m_rates->table(), m_plans->today());
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
            m.insert(QStringLiteral("photos"), withThumb(plans::planPhotos(m_store->data(), m_id), 320));   // 0.10.5 照片标签、照片条点开看大图
            const Value &p = *plans::planById(m_store->data(), m_id);
            m.insert(QStringLiteral("money"), plans::spendInfo(p, m_rates->main(), m_rates->table()));
            // J1：去掉点过"这几晚不用"的
            QStringList nights = m.value(QStringLiteral("nights")).toStringList();
            for (const Value &d : m_settings->value(QStringLiteral("noStayNights"))[m_id].asArray()) nights.removeAll(d.toString());
            m.insert(QStringLiteral("nights"), nights);
            // 0.10.4
            m.insert(QStringLiteral("kit"), plans::kitInfo(m_store->data(), p, m_plans->today()));
            m.insert(QStringLiteral("journal"), withThumb(plans::journalList(m_store->data(), m_id), 320));
            const QString guide = p[QStringLiteral("guide")].toString();
            m.insert(QStringLiteral("guide"), guide);
            m.insert(QStringLiteral("guideView"), guideWithThumbs(m_store->data(), guide));
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

QString PlanPageController::applyRoot(const std::function<QString(Value &)> &fn)
{
    if (m_store->isReadOnly()) return QStringLiteral("readOnly");
    Value copy = m_store->data();
    const QString err = fn(copy);
    if (!err.isEmpty() || copy == m_store->data()) return err;
    m_store->modify([&](Value &d) { d = copy; });
    edited();
    return {};
}

QStringList PlanPageController::splitLines(const QString &text) const { return plans::splitLines(text); }

int PlanPageController::addChecks(const QString &key, const QString &text)
{
    int n = -1;
    applyPlan([&](Value &p) { n = plans::addChecks(p, key, text); return QString(); });
    return n;
}

bool PlanPageController::setCheckDone(const QString &key, const QString &id, bool done)
{
    bool ok = false;
    applyPlan([&](Value &p) { ok = plans::setCheckDone(p, key, id, done); return QString(); });
    return ok;
}

bool PlanPageController::setCheckDue(const QString &id, const QString &due)
{
    bool ok = false;
    applyPlan([&](Value &p) { ok = plans::setCheckDue(p, id, due); return QString(); });
    return ok;
}

bool PlanPageController::removeCheck(const QString &key, const QString &id)
{
    bool ok = false;
    applyPlan([&](Value &p) { ok = plans::removeCheck(p, key, id); return QString(); });
    return ok;
}

int PlanPageController::addFromTemplate()
{
    int n = -1;
    applyRoot([&](Value &d) { n = plans::addFromTemplate(d, m_id); return QString(); });
    return n;
}

int PlanPageController::saveTemplate()
{
    int n = 0;
    applyRoot([&](Value &d) { n = plans::saveTemplate(d, m_id); return QString(); });
    return n;
}

QString PlanPageController::saveLocalEmergency(const QVariantMap &phones)
{
    return applyPlan([&](Value &p) { plans::setLocalEmergency(p, phones); return QString(); });
}

QString PlanPageController::saveEmergency(const QVariantMap &fields)
{
    return applyRoot([&](Value &d) { plans::setEmergency(d, fields); return QString(); });
}

QString PlanPageController::setGuide(const QString &planId, const QString &text)
{
    return applyRoot([&](Value &d) {
        Value *p = plans::mutPlan(d, planId);
        if (!p) return QStringLiteral("gone");
        plans::setGuide(*p, text);
        return QString();
    });
}

bool PlanPageController::toggleGuideTask(int line, bool on)
{
    return applyPlan([&](Value &p) {
        plans::setGuide(p, plans::toggleTaskLine(p[QStringLiteral("guide")].toString(), line, on));
        return QString();
    }).isEmpty();
}

QVariantMap PlanPageController::guideBlocks(const QString &text) const { return guideWithThumbs(m_store->data(), text); }

QVariantList PlanPageController::planPhotos() const { return withThumb(plans::planPhotos(m_store->data(), m_id), 320); }

void PlanPageController::seedPhotosForShot(const QString &id)
{
    const QString planId = id.isEmpty() ? m_id : id;
    const Value *p = plans::planById(m_store->data(), planId);
    if (!p || plans::planPhotos(m_store->data(), planId).size() >= 3) return;
    const QVector<int> st = plans::validStops(*p);
    if (st.isEmpty()) return;
    QStringList stops;
    for (int i : st) if (stops.size() < 3) stops << (*p)[QStringLiteral("stops")].asArray()[size_t(i)][QStringLiteral("id")].toString();
    applyRoot([&](Value &d) {
        Value *list = d.mutObject().find(QStringLiteral("photos"));
        if (!list) return QString();
        int n = 0;
        for (Value &ph : list->mutArray()) {
            if (n >= 9 || !ph[QStringLiteral("file")].toString().startsWith(QLatin1String("photos/"))) continue;
            json::Object &o = ph.mutObject();
            o.set(QStringLiteral("plan"), Value(planId));
            o.set(QStringLiteral("stop"), Value(stops[n % stops.size()]));
            if (n == 1) o.set(QStringLiteral("fav"), Value(true));
            if (n == 2) o.set(QStringLiteral("caption"), Value(QStringLiteral("傍晚的海边，风很大")));
            ++n;
        }
        return QString();
    });
}

void PlanPageController::seedKitForShot()
{
    if (m_id.isEmpty()) return;
    // 这个计划没有照片时借库里第一张放到第一站（副本），才截得到正文里的照片和游记首图
    if (plans::planPhotos(m_store->data(), m_id).isEmpty()) {
        const Value *p = plans::planById(m_store->data(), m_id);
        const QVector<int> st = plans::validStops(*p);
        if (!st.isEmpty() && !m_store->data()[QStringLiteral("photos")].asArray().empty()) {
            const QString stop = (*p)[QStringLiteral("stops")].asArray()[size_t(st.first())][QStringLiteral("id")].toString();
            applyRoot([&](Value &d) {
                json::Object &ph = d.mutObject().find(QStringLiteral("photos"))->mutArray()[0].mutObject();
                ph.set(QStringLiteral("plan"), Value(m_id));
                ph.set(QStringLiteral("stop"), Value(stop));
                return QString();
            });
        }
    }
    const QVariantList photos = plans::planPhotos(m_store->data(), m_id);
    const QString pic = photos.isEmpty() ? QString() : photos.first().toMap().value(QStringLiteral("id")).toString();
    applyRoot([&](Value &d) {
        plans::setEmergency(d, { { QStringLiteral("insurer"), QStringLiteral("Allianz") }, { QStringLiteral("policyNo"), QStringLiteral("AZ-2041-77") }, { QStringLiteral("insurerPhone"), QStringLiteral("+61 2 9390 6222") },
                                 { QStringLiteral("n0"), QStringLiteral("小王") }, { QStringLiteral("r0"), QStringLiteral("朋友") }, { QStringLiteral("p0"), QStringLiteral("+86 138 0000 0000") } });
        Value *p = plans::mutPlan(d, m_id);
        plans::setLocalEmergency(*p, { { QStringLiteral("报警"), QStringLiteral("000") }, { QStringLiteral("急救"), QStringLiteral("000") }, { QStringLiteral("使领馆 / 领事保护"), QStringLiteral("+61 2 6228 3999") } });
        // 游记只加一次（几张截图用同一份副本）
        if (plans::journalList(d, m_id).isEmpty()) {
            json::Object j;
            j.set(QStringLiteral("id"), Value("jn_shot"));
            j.set(QStringLiteral("plan"), Value(m_id));
            j.set(QStringLiteral("title"), Value(QStringLiteral("第一天：到了")));
            j.set(QStringLiteral("date"), Value(m_plans->today()));
            j.set(QStringLiteral("body"), Value((pic.isEmpty() ? QString() : QStringLiteral("![](photo:%1)\n\n").arg(pic))
                + QStringLiteral("**中午到**，先去市场吃了东西，下午沿着海边走了一圈。晚上风很大，第二天的船不知道开不开。这里的日落差不多七点半，拍了很多照片，回来慢慢挑。")));
            d.mutObject().find(QStringLiteral("journal"))->mutArray().push_back(Value(std::move(j)));
        }
        return QString();
    });
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

QString PlanPageController::setElev(int k, const QString &text)
{
    const QString err = applyPlan([&](Value &p) { return plans::setStopElev(p, k, text); });
    if (err.isEmpty() && text.trimmed().isEmpty()) fillElev(m_id);   // 清空后重新取
    return err;
}

void PlanPageController::fillElev(const QString &planId)
{
    if (m_fillingElev.contains(planId) || m_store->isReadOnly()) return;
    if (!m_fetch && (!m_net || !m_net->allows(QStringLiteral("weather")))) return;
    const Value *p = plans::planById(m_store->data(), planId);
    QVector<plans::TzStop> need = p ? plans::stopsWithoutElev(*p) : QVector<plans::TzStop>();
    if (need.isEmpty()) return;
    if (need.size() > kElevBatch) need.resize(kElevBatch);
    QStringList ids;
    for (const plans::TzStop &s : need) ids << s.id;
    m_fillingElev.insert(planId);
    auto done = [this, planId, ids](int status, const QByteArray &body, int netError) {
        m_fillingElev.remove(planId);
        const QVector<double> elev = netError == 0 ? plans::parseElev(body, int(ids.size())) : QVector<double>();
        int n = 0;
        if (!elev.isEmpty() && !m_store->isReadOnly()) {
            m_store->modify([&](Value &d) { n = plans::applyElev(d, planId, ids, elev); });
            if (n > 0) edited();
        }
        qInfo("海拔：补了 %d 站（共问 %d 站，HTTP %d）", n, int(ids.size()), status);
        if (n > 0) fillElev(planId);   // 一个都没补上（网络问题、回复认不出）就停
    };
    const QUrl url = plans::elevUrl(need);
    if (m_fetch) { m_fetch(url, done); return; }
    m_net->get(url, kTzTimeoutMs, this, [done](const services::NetClient::Reply &r) { done(r.status, r.body, int(r.error)); });
}

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
