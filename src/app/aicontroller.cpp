#include "aicontroller.h"

#include "aiprompt.h"
#include "aitools.h"
#include "appcontroller.h"
#include "datastore.h"
#include "geocoder.h"
#include "planedit.h"
#include "credentials.h"
#include "jsonio.h"
#include "netclient.h"
#include "paths.h"
#include "planmoney.h"
#include "ratescontroller.h"
#include "settings.h"
#include "translator.h"

#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>
#include <QUrl>
#include <QHash>
#include <QtMath>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace tp::app {

using namespace tp::ai;

namespace {

constexpr int kMaxChats = 8;
constexpr qint64 kMaxChatBytes = 2000000;

QString monthKey() { return QDate::currentDate().toString(QStringLiteral("yyyy-MM")); }
qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QJsonObject toQ(const json::Value &v)
{
    if (!v.isObject()) return {};
    return QJsonDocument::fromJson(json::stringify(v, 0).toUtf8()).object();
}
json::Value fromQ(const QJsonValue &v)
{
    if (v.isObject()) return json::parse(QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact)));
    if (v.isArray()) return json::parse(QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)));
    if (v.isBool()) return json::Value(v.toBool());
    if (v.isDouble()) return json::Value(v.toDouble());
    if (v.isString()) return json::Value(v.toString());
    return {};
}

QString providerLabel(const QString &id)
{
    if (id == QLatin1String("qwen")) return tp::tr("通义千问");
    if (id == QLatin1String("doubao")) return tp::tr("豆包");
    if (id == QLatin1String("custom")) return tp::tr("自定义");
    const Provider *p = ai::provider(id);
    return p ? p->name : id;
}

// 离线又从没取到过汇率时的粗略换算（ponytail：只有美元和人民币；有汇率表后不用它）
double fallbackPerUsd(const QString &cur) { return cur == QLatin1String("USD") ? 1 : cur == QLatin1String("CNY") ? 7.1 : -1; }

} // namespace

AiController::AiController(Settings *settings, services::NetClient *net, RatesController *rates, QObject *parent)
    : QObject(parent), m_settings(settings), m_net(net), m_rates(rates)
{
    m_client.setUserAgent(net->userAgent().toUtf8());
    const QJsonObject c = conf();
    m_provider = ai::provider(c.value(QStringLiteral("provider")).toString()) ? c.value(QStringLiteral("provider")).toString() : QStringLiteral("claude");
    m_tier = tierKey(tierIndex(c.value(QStringLiteral("tier")).toString()));
    m_chatsFile = QFileInfo(settings->path().isEmpty() ? Paths::settingsFile() : settings->path()).absolutePath() + QStringLiteral("/ai-chats.json");
    loadKeys();
    loadChats();
    connect(&m_client, &AiClient::updated, this, [this] {
        m_lastEventMs = nowMs();
        drainTools();
        if (!m_liveThrottle.isActive()) m_liveThrottle.start();
    });
    connect(&m_client, &AiClient::finished, this, &AiController::onFinished);
    m_liveThrottle.setSingleShot(true);
    m_liveThrottle.setInterval(50);
    connect(&m_liveThrottle, &QTimer::timeout, this, &AiController::liveChanged);
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        const int guard = m_hooks.noEventMs > 0 ? m_hooks.noEventMs : 150000;
        if (busy() && !m_guardStalled && nowMs() - m_lastEventMs > guard) { m_guardStalled = true; m_client.abort(); }
        emit liveChanged();
    });
    connect(rates, &RatesController::changed, this, &AiController::usageChanged);
}

// ---------------- 设置 ----------------

QJsonObject AiController::conf() const { return toQ(m_settings->value(QStringLiteral("ai"))); }

void AiController::setConf(const QString &key, const QJsonValue &v)
{
    QJsonObject c = conf();
    if (v.isUndefined() || v.isNull()) c.remove(key);
    else c.insert(key, v);
    m_settings->setValue(QStringLiteral("ai"), fromQ(c));
    m_settings->save();
}

QVariantList AiController::providers() const
{
    QVariantList out;
    for (const Provider &p : ai::providers())
        out << QVariantMap{ { QStringLiteral("id"), p.id }, { QStringLiteral("name"), providerLabel(p.id) }, { QStringLiteral("currency"), p.currency },
                            { QStringLiteral("signup"), p.signup }, { QStringLiteral("untested"), p.untested }, { QStringLiteral("chinaBlocked"), p.chinaBlocked } };
    return out;
}

void AiController::setProvider(const QString &id)
{
    if (id == m_provider || !ai::provider(id) || busy()) return;
    m_provider = id;
    setConf(QStringLiteral("provider"), id);
    m_models.clear();
    m_modelsState.clear();
    emit modelsChanged();
    emit changed();
    emit keysChanged();
    emit tellChanged();
}

QString AiController::providerName() const { return providerLabel(m_provider); }

void AiController::setTier(const QString &t)
{
    const QString k = tierKey(tierIndex(t));
    if (k == m_tier) return;
    m_tier = k;
    setConf(QStringLiteral("tier"), k);
    emit changed();
}

QString AiController::manualModel() const
{
    if (m_provider == QLatin1String("custom")) return conf().value(QStringLiteral("custom")).toObject().value(QStringLiteral("model")).toString();
    return conf().value(QStringLiteral("models")).toObject().value(m_provider).toString();
}

void AiController::setManualModel(const QString &m)
{
    const QString v = m.trimmed();
    if (v == manualModel()) return;
    if (m_provider == QLatin1String("custom")) { setCustom(QStringLiteral("model"), v); return; }
    QJsonObject ms = conf().value(QStringLiteral("models")).toObject();
    if (v.isEmpty()) ms.remove(m_provider); else ms.insert(m_provider, v);
    setConf(QStringLiteral("models"), ms);
    emit changed();
}

QString AiController::model() const
{
    const QString m = manualModel();
    if (!m.isEmpty() || m_provider == QLatin1String("custom")) return m;
    return ai::provider(m_provider)->tiers[tierIndex(m_tier)].model;
}

bool AiController::think() const
{
    const Provider *p = ai::provider(m_provider);
    const int i = tierIndex(m_tier);
    if (m_provider == QLatin1String("kimi")) return true;
    if (p->tiers[i].model.isEmpty()) return i == 2;   // 自定义：档位只用来估价
    return p->tiers[i].think;   // 手填模型时也按档位定思考（PO 选 4）
}

QString AiController::effort() const { return ai::provider(m_provider)->tiers[tierIndex(m_tier)].effort; }

QString AiController::base() const
{
    if (m_provider == QLatin1String("custom") && !m_hooks.baseOpenAi.isEmpty()) return m_hooks.baseOpenAi;
    if (!m_hooks.baseAnthropic.isEmpty() && ai::provider(m_provider)->kind == Kind::Anthropic) return m_hooks.baseAnthropic;
    if (!m_hooks.baseOpenAi.isEmpty() && ai::provider(m_provider)->kind == Kind::OpenAi) return m_hooks.baseOpenAi;
    return conf().value(QStringLiteral("bases")).toObject().value(m_provider).toString();
}

void AiController::setBase(const QString &b)
{
    QJsonObject bs = conf().value(QStringLiteral("bases")).toObject();
    const QString v = b.trimmed();
    if (v == bs.value(m_provider).toString()) return;
    if (v.isEmpty()) bs.remove(m_provider); else bs.insert(m_provider, v);
    setConf(QStringLiteral("bases"), bs);
    emit changed();
}

QString AiController::defaultBase() const { return ai::provider(m_provider)->base; }

QVariantMap AiController::custom() const
{
    QVariantMap m = conf().value(QStringLiteral("custom")).toObject().toVariantMap();
    if (!m.contains(QStringLiteral("cur"))) m.insert(QStringLiteral("cur"), mainCur());
    return m;
}

void AiController::setCustom(const QString &field, const QString &value)
{
    QJsonObject c = conf().value(QStringLiteral("custom")).toObject();
    const QString v = value.trimmed();
    if (v.isEmpty()) c.remove(field); else c.insert(field, v);
    if ((field == QLatin1String("pin") || field == QLatin1String("pout")) && !c.contains(QStringLiteral("cur"))) c.insert(QStringLiteral("cur"), mainCur());
    setConf(QStringLiteral("custom"), c);
    emit changed();
}

const Price *AiController::price(Price *buf) const
{
    if (m_provider != QLatin1String("custom")) return priceFor(*ai::provider(m_provider), model());
    const QVariantMap c = custom();
    bool a = false, b = false;
    const double pin = c.value(QStringLiteral("pin")).toString().toDouble(&a), pout = c.value(QStringLiteral("pout")).toString().toDouble(&b);
    if (!a || !b || pin < 0 || pout < 0) return nullptr;
    *buf = Price{ QString(), pin, pout };
    return buf;
}

QVariantList AiController::tiers() const
{
    QVariantList out;
    const Provider *p = ai::provider(m_provider);
    const QString cur = m_provider == QLatin1String("custom") ? custom().value(QStringLiteral("cur")).toString() : p->currency;
    for (int i = 0; i < 3; ++i) {
        const Tier &t = p->tiers[i];
        const QString m = manualModel().isEmpty() ? t.model : manualModel();
        const bool th = m_provider == QLatin1String("custom") ? i == 2 : t.think;
        Price buf;
        const Price *pr = m_provider == QLatin1String("custom") ? price(&buf) : priceFor(*p, m);
        const double est = estimate(pr, th);
        out << QVariantMap{ { QStringLiteral("key"), tierKey(i) }, { QStringLiteral("model"), m }, { QStringLiteral("think"), th },
                            { QStringLiteral("est"), est >= 0 ? money(est, cur) : QString() } };
    }
    return out;
}

bool AiController::web() const { return conf().value(QStringLiteral("web")).toBool(true); }
void AiController::setWeb(bool on) { if (on == web()) return; setConf(QStringLiteral("web"), on); emit changed(); }
bool AiController::canWeb() const { return tools::canSearch(m_provider); }
bool AiController::noMoney() const { return conf().value(QStringLiteral("noMoney")).toBool(true); }
void AiController::setNoMoney(bool on) { if (on == noMoney()) return; setConf(QStringLiteral("noMoney"), on); emit changed(); }

void AiController::setPlanContext(AppController *app, data::DataStore *store, services::Geocoder *geocoder,
                                  std::function<QVariantMap()> where, std::function<void(const QStringList &)> onDeleted)
{
    m_app = app;
    m_store = store;
    m_geocoder = geocoder;
    m_where = std::move(where);
    m_onDeleted = std::move(onDeleted);
    if (app) connect(app, &AppController::undoApplied, this, &AiController::onUndo);
    emit messagesChanged();
}

// ---------------- 钥匙 ----------------

void AiController::loadKeys()
{
    m_tails.clear();
    for (const Provider &p : ai::providers()) {
        const QString k = platform::credentials::read(credTarget(p.id));
        if (!k.isEmpty()) m_tails.insert(p.id, k.right(4));
    }
    emit keysChanged();
}

QString AiController::keyFor(const QString &id) const { return platform::credentials::read(credTarget(id)); }

QString AiController::setKey(const QString &key)
{
    const QString err = checkKey(m_provider, key);
    if (!err.isEmpty()) return err;
    QString why;
    if (!platform::credentials::write(credTarget(m_provider), key.trimmed(), &why)) {
        qWarning("AI 钥匙没存进凭据管理器：%s", qUtf8Printable(why));
        return tp::tr("没能存进 Windows 凭据管理器：%1").arg(why);
    }
    m_tails.insert(m_provider, key.trimmed().right(4));
    qInfo("AI 钥匙已存（%s，末 4 位 %s）", qUtf8Printable(m_provider), qUtf8Printable(key.trimmed().right(4)));
    emit keysChanged();
    return {};
}

void AiController::clearKey()
{
    platform::credentials::remove(credTarget(m_provider));
    m_tails.remove(m_provider);
    emit keysChanged();
}

void AiController::refreshModels()
{
    if (!m_net->allows(QStringLiteral("ai"))) { m_modelsState = tp::tr("联网关着（设置 → 联网）"); emit modelsChanged(); return; }
    const QString key = keyFor(m_provider);
    if (key.isEmpty()) { m_modelsState = tp::tr("先填钥匙"); emit modelsChanged(); return; }
    const Provider *p = ai::provider(m_provider);
    if (m_provider == QLatin1String("custom") && base().isEmpty()) { m_modelsState = tp::tr("先填地址"); emit modelsChanged(); return; }
    m_modelsState = QStringLiteral("busy");
    emit modelsChanged();
    const QString pid = m_provider;
    m_client.get(buildModels(*p, base(), key), [this, pid](int status, const QByteArray &body, const QString &netErr) {
        if (pid != m_provider) return;
        const QStringList ms = status >= 200 && status < 300 ? parseModels(body) : QStringList();
        m_models = ms;
        m_modelsState = !ms.isEmpty() ? QStringLiteral("ok")
                      : status >= 400 ? tp::tr("拉不到列表（%1），可以手填模型名").arg(status)
                      : status == 0 ? tp::tr("连不上（%1），可以手填模型名").arg(netErr)
                                    : tp::tr("这家没给列表，可以手填模型名");
        emit modelsChanged();
    });
}

// ---------------- 用量和上限 ----------------

QString AiController::mainCur() const { return m_rates->main(); }

double AiController::toMain(double amount, const QString &cur) const
{
    const QString main = mainCur();
    if (cur == main || amount == 0) return amount;
    const double k = map::plans::perMain(m_rates->table(), main, cur);
    if (k > 0) return amount * k;
    const double a = fallbackPerUsd(cur), b = fallbackPerUsd(main);
    return a > 0 && b > 0 ? amount / a * b : -1;
}

double AiController::cap() const
{
    const QJsonObject c = conf();
    if (c.contains(QStringLiteral("cap"))) {
        const double v = toMain(c.value(QStringLiteral("cap")).toDouble(), c.value(QStringLiteral("capCur")).toString(mainCur()));
        if (v >= 0) return v;
    }
    const double d = toMain(5, QStringLiteral("USD"));   // 默认约 $5，按主币种取整
    return d > 0 ? std::max(1.0, std::round(d)) : 5;
}

void AiController::setCap(double v)
{
    QJsonObject c = conf();
    c.insert(QStringLiteral("cap"), std::max(0.0, v));
    c.insert(QStringLiteral("capCur"), mainCur());
    m_settings->setValue(QStringLiteral("ai"), fromQ(c));
    m_settings->save();
    emit usageChanged();
}

void AiController::raiseCap()
{
    const double step = std::max(1.0, std::round(toMain(5, QStringLiteral("USD"))));
    setCap(std::max(cap(), used()) + step);
    emit toast(tp::tr("本月上限提高到 %1").arg(money(cap(), mainCur())), false);
}

QJsonObject AiController::usageObj() const
{
    const QJsonObject u = toQ(m_settings->value(QStringLiteral("aiUsage")));
    if (u.value(QStringLiteral("m")).toString() == monthKey()) return u;
    QJsonObject fresh{ { QStringLiteral("m"), monthKey() } };
    if (u.contains(QStringLiteral("plans"))) fresh.insert(QStringLiteral("plans"), u.value(QStringLiteral("plans")));   // 方案用量跨月留着
    return fresh;
}

// 0.13.2 每份方案的用量（向导一次、从订单生成一次、之后每次出修改的一问）：一问里几轮（含自动续轮）的 token 和花费加起来，
// 记在 aiUsage.plans（最新的在前，留 30 条）：t 时间、p 服务商、m 模型、tok、amt + cur（没单价时没有）、k（wizard / order / edit）
void AiController::recordPlan()
{
    const Run &r = m_run;
    if (r.tok <= 0 || (r.kind.isEmpty() && !r.props)) return;
    QJsonObject u = usageObj();
    QJsonArray a = u.value(QStringLiteral("plans")).toArray();
    QJsonObject e{ { QStringLiteral("t"), QDateTime::currentDateTime().toString(Qt::ISODate) }, { QStringLiteral("p"), r.pid }, { QStringLiteral("m"), r.mdl },
                   { QStringLiteral("tok"), r.tok }, { QStringLiteral("k"), r.kind.isEmpty() ? QStringLiteral("edit") : r.kind } };
    if (r.priced) { e.insert(QStringLiteral("amt"), r.amt); e.insert(QStringLiteral("cur"), r.cur); }
    a.prepend(e);
    while (a.size() > 30) a.removeLast();
    u.insert(QStringLiteral("plans"), a);
    m_settings->setValue(QStringLiteral("aiUsage"), fromQ(u));
    m_settings->save();
    emit usageChanged();
}

QVariantList AiController::planUsage() const
{
    QVariantList out;
    for (const auto &v : usageObj().value(QStringLiteral("plans")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString k = o.value(QStringLiteral("k")).toString();
        const QDateTime t = QDateTime::fromString(o.value(QStringLiteral("t")).toString(), Qt::ISODate);
        out << QVariantMap{ { QStringLiteral("time"), t.toString(QStringLiteral("yyyy-MM-dd HH:mm")) },
                            { QStringLiteral("kind"), k == QLatin1String("wizard") ? tp::tr("向导规划") : k == QLatin1String("order") ? tp::tr("从订单生成") : tp::tr("修改") },
                            { QStringLiteral("provider"), providerLabel(o.value(QStringLiteral("p")).toString()) }, { QStringLiteral("model"), o.value(QStringLiteral("m")).toString() },
                            { QStringLiteral("tok"), tp::tr("%1 token").arg(QLocale().toString(o.value(QStringLiteral("tok")).toInteger())) },
                            { QStringLiteral("cost"), o.contains(QStringLiteral("amt")) ? money(o.value(QStringLiteral("amt")).toDouble(), o.value(QStringLiteral("cur")).toString()) : tp::tr("没填单价") } };
    }
    return out;
}

double AiController::used() const
{
    double sum = 0;
    const QJsonObject by = usageObj().value(QStringLiteral("by")).toObject();
    for (auto it = by.begin(); it != by.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        const double v = toMain(o.value(QStringLiteral("amt")).toDouble(), o.value(QStringLiteral("cur")).toString());
        if (v > 0) sum += v;
    }
    return sum;
}

QVariantList AiController::usageRows() const
{
    QVariantList out;
    const QJsonObject by = usageObj().value(QStringLiteral("by")).toObject();
    for (auto it = by.begin(); it != by.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        const QString cur = o.value(QStringLiteral("cur")).toString();
        const double amt = o.value(QStringLiteral("amt")).toDouble();
        QString t = money(amt, cur);
        if (cur != mainCur() && amt > 0) { const double m = toMain(amt, cur); if (m >= 0) t += QStringLiteral("（≈ %1）").arg(money(m, mainCur())); }
        t += QStringLiteral(" · ") + tp::tr("%1 次 · %2 token").arg(o.value(QStringLiteral("n")).toInt()).arg(QLocale().toString(o.value(QStringLiteral("tok")).toInteger()));
        out << QVariantMap{ { QStringLiteral("name"), providerLabel(it.key()) }, { QStringLiteral("text"), t } };
    }
    return out;
}

void AiController::record(const QString &pid, double amount, const QString &cur, qint64 tokens)
{
    const double before = used();
    QJsonObject u = usageObj();
    QJsonObject by = u.value(QStringLiteral("by")).toObject();
    QJsonObject o = by.value(pid).toObject();
    o.insert(QStringLiteral("amt"), o.value(QStringLiteral("amt")).toDouble() + std::max(0.0, amount));
    o.insert(QStringLiteral("cur"), cur);
    o.insert(QStringLiteral("n"), o.value(QStringLiteral("n")).toInt() + 1);
    o.insert(QStringLiteral("tok"), o.value(QStringLiteral("tok")).toInteger() + tokens);
    by.insert(pid, o);
    u.insert(QStringLiteral("by"), by);
    m_settings->setValue(QStringLiteral("aiUsage"), fromQ(u));
    m_settings->save();
    emit usageChanged();
    const double c = cap(), now = used();
    if (c > 0 && now >= 0.8 * c && before < 0.8 * c && now < c) {
        note(tp::tr("这个月已经用了约 %1，到了上限 %2 的 80%。").arg(money(now, mainCur()), money(c, mainCur())));
        emit capWarned(now, c);
    }
}

QString AiController::money(double amount, const QString &cur) const
{
    const QString sym = cur == QLatin1String("USD") ? QStringLiteral("$") : cur == QLatin1String("CNY") ? QStringLiteral("¥")
                      : cur == QLatin1String("EUR") ? QStringLiteral("€") : cur + QLatin1Char(' ');
    const int dp = amount != 0 && std::abs(amount) < 0.1 ? 3 : 2;
    return sym + QString::number(amount, 'f', dp);
}

// ---------------- 对话 ----------------

namespace {
QString pageZh(const QString &p)
{
    static const QHash<QString, QString> m{ { QStringLiteral("plan"), QStringLiteral("地图（计划中）") }, { QStringLiteral("wish"), QStringLiteral("地图（想去）") },
                                            { QStringLiteral("foot"), QStringLiteral("地图（足迹）") }, { QStringLiteral("photo"), QStringLiteral("照片") },
                                            { QStringLiteral("trip"), QStringLiteral("计划详情") }, { QStringLiteral("calendar"), QStringLiteral("日历") },
                                            { QStringLiteral("journal"), QStringLiteral("游记") }, { QStringLiteral("ledger"), QStringLiteral("账本") },
                                            { QStringLiteral("settings"), QStringLiteral("设置") } };
    return m.value(p, QStringLiteral("地图"));
}
QString fmtDur(qint64 ms) { const qint64 s = std::max<qint64>(0, (ms + 500) / 1000); return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0')); }
const json::Value &emptyRoot() { static const json::Value v; return v; }
const char kPending[] = "已显示给用户预览，用户还没有决定。";
} // namespace

void AiController::note(const QString &text, const QString &kind)
{
    m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("note") }, { QStringLiteral("text"), text }, { QStringLiteral("kind"), kind } });
    emit messagesChanged();
}

const json::Value &AiController::root() const { return m_store ? m_store->data() : emptyRoot(); }

QVariantList AiController::messages() const
{
    QVariantList out;
    for (int i = 0; i < m_msgs.size(); ++i) {
        const QJsonObject o = m_msgs.at(i).toObject();
        const QString role = o.value(QStringLiteral("role")).toString();
        QVariantMap m{ { QStringLiteral("role"), role }, { QStringLiteral("kind"), o.value(QStringLiteral("kind")).toString() },
                       { QStringLiteral("text"), o.value(QStringLiteral("shown")).toString(o.value(QStringLiteral("text")).toString()) },
                       { QStringLiteral("hidden"), o.value(QStringLiteral("auto")).toBool() }, { QStringLiteral("live"), busy() && i == m_msgs.size() - 1 } };
        if (role == QLatin1String("assistant")) {
            m.insert(QStringLiteral("html"), mdLite(o.value(QStringLiteral("text")).toString()));
            for (const char *k : { "thinking", "meta", "error", "cur", "provider", "model" }) if (o.contains(QLatin1String(k))) m.insert(QLatin1String(k), o.value(QLatin1String(k)).toString());
            if (o.contains(QStringLiteral("cost"))) m.insert(QStringLiteral("cost"), o.value(QStringLiteral("cost")).toDouble());
            m.insert(QStringLiteral("act"), o.value(QStringLiteral("act")).toVariant());
            // 1.1.0：Anthropic 要求原样显示时带上来源（技术选型.md §11），只给 Claude 的；国内几家不显示
            if (o.value(QStringLiteral("api")).toString() == QLatin1String("anthropic")) m.insert(QStringLiteral("sources"), o.value(QStringLiteral("sources")).toVariant());
            m.insert(QStringLiteral("noWeb"), o.value(QStringLiteral("noWeb")).toBool() && !o.value(QStringLiteral("text")).toString().isEmpty());
            QVariantList props;
            const QJsonArray ps = o.value(QStringLiteral("props")).toArray();
            for (int pi = 0; pi < ps.size(); ++pi) {
                QVariantMap v = tools::view(root(), ps.at(pi).toObject());
                v.insert(QStringLiteral("mi"), i);
                v.insert(QStringLiteral("pi"), pi);
                props << v;
            }
            m.insert(QStringLiteral("props"), props);
            if (o.contains(QStringLiteral("research"))) {
                QVariantMap r = o.value(QStringLiteral("research")).toObject().toVariantMap();
                r.insert(QStringLiteral("usedText"), fmtDur(qint64(r.value(QStringLiteral("used")).toDouble())));
                const int secs = r.value(QStringLiteral("secs")).toInt();
                r.insert(QStringLiteral("secsText"), secs < 90 ? tp::tr("约 %1 秒").arg(secs) : tp::tr("约 %1 分钟").arg((secs + 30) / 60));
                r.insert(QStringLiteral("mi"), i);
                m.insert(QStringLiteral("research"), r);
            }
        }
        out << m;
    }
    return out;
}

QString AiController::liveText() const { return mdLite(m_client.state().text); }

QString AiController::runText() const
{
    if (!m_shotRun.isEmpty()) return m_shotRun;
    if (!busy()) return {};
    const qint64 now = nowMs();
    QStringList parts;
    if (m_research.on && m_research.budget > 0) {
        const qint64 used = now - m_research.start, left = m_research.budget - used;
        parts << tp::tr("已用 %1 / 预计 %2").arg(fmtDur(used), fmtDur(m_research.budget));
        if (left > 0 && left < 45000) parts << tp::tr("还剩 %1 秒").arg(left / 1000);
        if (!m_research.todo.isEmpty()) parts << tp::tr("还剩 %1 项").arg(m_research.todo.size());
    } else {
        parts << tp::tr("已用 %1 秒").arg((now - m_run.t0) / 1000);
    }
    const StreamState &s = m_client.state();
    qsizetype chars = s.text.size();
    for (const QString &j : s.toolJson) chars += j.size();
    if (chars) parts << tp::tr("已写 %1 字").arg(QLocale().toString(chars));
    else if (!s.thinking.isEmpty()) parts << tp::tr("正在思考");
    if (now - m_lastEventMs > 30000) parts << tp::tr("%1 秒没有新内容").arg((now - m_lastEventMs) / 1000);
    return parts.join(QStringLiteral(" · "));
}

QString AiController::pendingTell() const { return m_pending.isEmpty() ? QString() : providerName(); }

QString AiController::convCost() const
{
    double sum = 0;
    for (const auto &v : m_msgs) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("cost"))) { const double m = toMain(o.value(QStringLiteral("cost")).toDouble(), o.value(QStringLiteral("cur")).toString()); if (m > 0) sum += m; }
    }
    return sum > 0 ? tp::tr("这段对话约 %1").arg(money(sum, mainCur())) : QString();
}

bool AiController::send(const QString &text) { return sendWith(text, QString(), false, false); }

QString AiController::blockReason() const { return blockReason(nullptr); }

QString AiController::blockReason(QString *kind) const
{
    if (kind) kind->clear();
    if (!m_net->allows(QStringLiteral("ai"))) return tp::tr("AI 助手现在不能用：离线模式开着，或者设置 → 联网里关掉了“AI 助手”。");
    if (!hasKey()) { if (kind) *kind = QStringLiteral("nokey"); return tp::tr("还没有填 %1 的钥匙：设置 → AI。").arg(providerName()); }
    if (m_provider == QLatin1String("custom") && (base().isEmpty() || model().isEmpty())) return tp::tr("自定义服务要先在设置 → AI 里填地址和模型名。");
    if (capReached()) { if (kind) *kind = QStringLiteral("cap"); return tp::tr("这个月已经用了约 %1，到了上限 %2，先停在这里。").arg(money(used(), mainCur()), money(cap(), mainCur())); }
    return {};
}

bool AiController::sendWith(const QString &text, const QString &shown, bool gapFill, bool keepResearch, const QString &runKind)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || busy()) return false;
    QString kind;
    const QString why = blockReason(&kind);
    if (!why.isEmpty()) { note(why, kind); return false; }
    if (!conf().value(QStringLiteral("told")).toArray().contains(m_provider)) {
        m_pending = t;
        m_pendingShown = shown;
        m_pendingKind = runKind;
        emit tellChanged();
        return false;
    }
    doSend(t, shown, gapFill, keepResearch, runKind);
    return true;
}

bool AiController::planWizard(const QVariantMap &form)
{
    if (busy() || !blockReason().isEmpty()) return false;
    bool empty = true;   // 当前这段还没说过话就接着用，否则开一段新的
    for (const auto &v : m_msgs) if (v.toObject().value(QStringLiteral("role")) != QLatin1String("note")) empty = false;
    if (!empty) newChat();
    const WizardMsg w = wizardMessage(form, mainCur());
    return sendWith(w.text, w.shown, false, false, QStringLiteral("wizard"));
}

bool AiController::sendOrder(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || busy()) return false;
    if (currentPlanId().isEmpty()) { note(tp::tr("先在地图上点开要加交通住宿的那个计划，再粘贴订单文字。")); return false; }
    return sendWith(orderMessage(t), t, false, false, QStringLiteral("order"));
}

void AiController::confirmTell()
{
    if (m_pending.isEmpty()) return;
    QJsonArray told = conf().value(QStringLiteral("told")).toArray();
    if (!told.contains(m_provider)) told.append(m_provider);
    setConf(QStringLiteral("told"), told);
    const QString t = m_pending, shown = m_pendingShown, kind = m_pendingKind;
    m_pending.clear();
    m_pendingShown.clear();
    m_pendingKind.clear();
    emit tellChanged();
    doSend(t, shown, false, false, kind);
}

void AiController::cancelTell() { m_pending.clear(); m_pendingShown.clear(); m_pendingKind.clear(); emit tellChanged(); }

QString AiController::currentPlanId() const
{
    const QVariantMap w = m_where ? m_where() : QVariantMap();
    const QString id = w.value(QStringLiteral("planId")).toString();
    if (!id.isEmpty() && tp::map::planedit::findPlan(root(), id)) return id;
    QString live;
    tools::liveLine(root(), (w.value(QStringLiteral("today")).toString().isEmpty() ? QDate::currentDate().toString(Qt::ISODate) : w.value(QStringLiteral("today")).toString()), &live);
    return live;
}

void AiController::doSend(const QString &text, const QString &shown, bool gapFill, bool keepResearch, const QString &kind)
{
    if (!keepResearch) m_research = Research();
    QJsonObject u{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("text"), text } };
    if (!shown.isEmpty()) u.insert(QStringLiteral("shown"), shown);
    m_msgs.append(u);
    m_run = Run();
    m_run.t0 = nowMs();
    m_run.gapFill = gapFill;
    m_run.kind = kind;
    m_run.planId = kind == QLatin1String("wizard") ? QString() : currentPlanId();   // 向导是新计划，不带地图上选中的那个
    run();
}

// 对话 → 这种接法的 messages（Electron apiMessages）：
// 助手的话有同一种接法的原样内容（思考块含 signature、工具调用、reasoning_content）就原样回传，否则只带文字（0.13.0 的旧历史、换了服务商）；
// 上一轮调了工具的，下一条用户消息前补上工具结果；出错、一个字没写的回答和它前面那一问不带
QJsonArray AiController::apiHistory(Kind kind) const
{
    const bool anth = kind == Kind::Anthropic;
    const QString api = anth ? QStringLiteral("anthropic") : QStringLiteral("openai");
    QJsonArray out;
    QList<std::pair<QString, QString>> pending;   // 还没回传结果的工具调用：id、名字
    QJsonObject pendingResults;
    auto dead = [](const QJsonObject &a) { return a.value(QStringLiteral("role")) == QLatin1String("assistant") && a.contains(QStringLiteral("error")) && a.value(QStringLiteral("text")).toString().isEmpty(); };
    auto push = [&out](QJsonObject m) {
        if (!out.isEmpty()) {
            QJsonObject l = out.last().toObject();
            if (l.value(QStringLiteral("role")) == m.value(QStringLiteral("role")) && m.value(QStringLiteral("role")) != QLatin1String("tool")
                && l.value(QStringLiteral("content")).isString() && m.value(QStringLiteral("content")).isString() && !l.contains(QStringLiteral("tool_calls"))) {
                l.insert(QStringLiteral("content"), l.value(QStringLiteral("content")).toString() + QStringLiteral("\n\n") + m.value(QStringLiteral("content")).toString());
                out.replace(out.size() - 1, l);
                return;
            }
        }
        out.append(m);
    };
    for (qsizetype i = 0; i < m_msgs.size(); ++i) {
        const QJsonObject o = m_msgs.at(i).toObject();
        const QString role = o.value(QStringLiteral("role")).toString();
        if (role == QLatin1String("user")) {
            if (!o.value(QStringLiteral("auto")).toBool() && i + 1 < m_msgs.size() && dead(m_msgs.at(i + 1).toObject())) continue;
            const QString text = o.value(QStringLiteral("text")).toString();
            if (anth) {
                QJsonArray content;
                for (const auto &p : pending)
                    content.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("tool_result") }, { QStringLiteral("tool_use_id"), p.first },
                                                { QStringLiteral("content"), pendingResults.value(p.first).toString(p.second == QLatin1String("propose_changes") ? QString::fromUtf8(kPending) : QStringLiteral("好的。")) } });
                if (content.isEmpty()) { if (!text.isEmpty()) push(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("content"), text } }); }
                else {
                    if (!text.isEmpty()) content.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("text") }, { QStringLiteral("text"), text } });
                    push(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("content"), content } });
                }
            } else {
                for (const auto &p : pending) {
                    QJsonObject t{ { QStringLiteral("role"), QStringLiteral("tool") }, { QStringLiteral("tool_call_id"), p.first },
                                   { QStringLiteral("content"), pendingResults.value(p.first).toString(p.second == QLatin1String("propose_changes") ? QString::fromUtf8(kPending) : QStringLiteral("好的。")) } };
                    if (p.second.startsWith(u'$')) t.insert(QStringLiteral("name"), p.second);   // Kimi 内置工具要带名字
                    push(t);
                }
                if (!text.isEmpty()) push(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("content"), text } });
            }
            pending.clear();
        } else if (role == QLatin1String("assistant")) {
            if (dead(o)) { pending.clear(); continue; }
            pending.clear();
            pendingResults = o.value(QStringLiteral("results")).toObject();
            const QString text = o.value(QStringLiteral("text")).toString();
            const bool raw = o.value(QStringLiteral("api")).toString() == api && !o.contains(QStringLiteral("error"));
            if (raw && anth) {
                QJsonArray content;
                for (const auto &v : o.value(QStringLiteral("content")).toArray()) {
                    const QJsonObject b = v.toObject();
                    const QString bt = b.value(QStringLiteral("type")).toString();
                    if (bt.isEmpty() || (bt == QLatin1String("thinking") && b.value(QStringLiteral("signature")).toString().isEmpty()) || (bt == QLatin1String("text") && b.value(QStringLiteral("text")).toString().isEmpty())) continue;
                    content.append(b);
                    if (bt == QLatin1String("tool_use")) pending << std::pair{ b.value(QStringLiteral("id")).toString(), b.value(QStringLiteral("name")).toString() };
                }
                if (!content.isEmpty()) out.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") }, { QStringLiteral("content"), content } });
            } else if (raw) {
                const QJsonObject oa = o.value(QStringLiteral("oa")).toObject();
                QJsonObject m{ { QStringLiteral("role"), QStringLiteral("assistant") }, { QStringLiteral("content"), text } };
                const QJsonArray calls = oa.value(QStringLiteral("tool_calls")).toArray();
                if (!calls.isEmpty()) {
                    m.insert(QStringLiteral("tool_calls"), calls);
                    // DeepSeek / Kimi 带工具时要回传 reasoning_content，否则 400（技术选型.md §11）
                    if (!oa.value(QStringLiteral("reasoning_content")).toString().isEmpty()) m.insert(QStringLiteral("reasoning_content"), oa.value(QStringLiteral("reasoning_content")).toString());
                    for (const auto &c : calls) pending << std::pair{ c.toObject().value(QStringLiteral("id")).toString(), c.toObject().value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString() };
                }
                if (!text.isEmpty() || !calls.isEmpty()) out.append(m);
            } else if (!text.isEmpty()) {
                push(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") }, { QStringLiteral("content"), text } });
            }
        }
    }
    while (!out.isEmpty() && out.first().toObject().value(QStringLiteral("role")).toString() != QLatin1String("user")) out.removeFirst();
    return out;
}

void AiController::run()
{
    const Provider *p = ai::provider(m_provider);
    const bool order = m_run.kind == QLatin1String("order");
    const bool canWeb = tools::canSearch(m_provider);
    const bool web = canWeb && !order && (this->web() || m_run.gapFill);
    if (!m_run.planId.isEmpty() && !tp::map::planedit::findPlan(root(), m_run.planId)) m_run.planId = m_run.kind == QLatin1String("wizard") ? QString() : currentPlanId();
    const json::Value *plan = m_run.planId.isEmpty() ? nullptr : tp::map::planedit::findPlan(root(), m_run.planId);
    const QVariantMap w = m_where ? m_where() : QVariantMap();
    const QString today = (w.value(QStringLiteral("today")).toString().isEmpty() ? QDate::currentDate().toString(Qt::ISODate) : w.value(QStringLiteral("today")).toString());
    const QString live = tools::liveLine(root(), today, nullptr);
    const QString head = systemHead(tools::plansSummary(root()));
    ChatExtra x;
    x.systemTail = systemTail(QDateTime::currentDateTime(), pageZh(w.value(QStringLiteral("page")).toString()), plan ? (*plan)[QStringLiteral("name")].toString() : QString(),
                              m_run.planId, live, web, canWeb, plan ? tools::planForAi(root(), *plan, noMoney()) : QString());
    x.tools = p->kind == Kind::Anthropic ? tools::anthropicTools(web) : tools::openAiTools(m_provider, web);
    x.search = web && m_provider == QLatin1String("qwen");
    // 从订单生成（I-12）：Electron 固定 Haiku → 这里用当前服务商"快"档的模型、不思考（Kimi 关不掉）；只给 propose_changes，能强制就强制；4000 token
    QString mdl = model();
    m_run.pid = m_provider;
    bool th = think();
    QString eff = effort();
    if (order) {
        if (m_provider != QLatin1String("custom")) { mdl = p->tiers[0].model; eff = p->tiers[0].effort; }
        th = m_provider == QLatin1String("kimi");   // 千问思考模式下不能强制 tool_choice，所以关掉思考（快档本来也不思考）
        x.tools = tools::orderTools(p->kind == Kind::Anthropic, m_provider);
        x.toolChoice = tools::orderChoice(m_provider);
        x.maxTokens = 4000;
        x.systemTail += QStringLiteral("\n\n") + orderTask(m_run.planId);
    }
    m_run.mdl = mdl;
    const QJsonArray hist = apiHistory(p->kind);
    m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") }, { QStringLiteral("text"), QString() },
                               { QStringLiteral("provider"), m_provider }, { QStringLiteral("model"), mdl },
                               { QStringLiteral("api"), p->kind == Kind::Anthropic ? QStringLiteral("anthropic") : QStringLiteral("openai") },
                               { QStringLiteral("noWeb"), !web }, { QStringLiteral("planId"), m_run.planId } });
    const Request r = buildChat(*p, base(), keyFor(m_provider), mdl, th, eff, head, hist, x);
    m_client.setNoByteMs(m_hooks.noByteMs > 0 ? m_hooks.noByteMs : 90000);
    m_lastEventMs = nowMs();
    m_guardStalled = false;
    m_doneSeen = 0;
    m_client.start(r, p->kind);
    m_tick.start();
    qInfo("AI 发送：%s %s 思考=%d 联网=%d 自动第 %d 轮%s", qUtf8Printable(m_provider), qUtf8Printable(mdl), int(th), int(web), m_run.autoN, order ? "（从订单生成）" : "");
    emit busyChanged();
    emit messagesChanged();
    emit liveChanged();
}

void AiController::stop() { m_client.abort(); }

// 流式中：propose_changes 一写完就出预览（Electron content_block_stop），不等整条回答
void AiController::drainTools()
{
    const StreamState &s = m_client.state();
    while (m_doneSeen < s.doneTools.size()) {
        const int idx = s.doneTools.at(m_doneSeen++);
        const QJsonObject b = s.blocks.at(idx).toObject();
        if (b.value(QStringLiteral("name")).toString() == QLatin1String("propose_changes"))
            prepareProposal(int(m_msgs.size()) - 1, b.value(QStringLiteral("id")).toString(), b.value(QStringLiteral("input")).toObject(), s.badTools.contains(idx));
    }
}

void AiController::prepareProposal(int mi, const QString &toolId, const QJsonObject &input, bool truncated)
{
    if (mi < 0 || mi >= m_msgs.size()) return;
    QJsonObject am = m_msgs.at(mi).toObject();
    QJsonArray props = am.value(QStringLiteral("props")).toArray();
    for (const auto &v : props) if (v.toObject().value(QStringLiteral("toolId")).toString() == toolId) return;
    const QJsonObject P = tools::prepare(root(), input, am.value(QStringLiteral("planId")).toString(), toolId, truncated);
    props.append(P);
    m_run.props = true;
    am.insert(QStringLiteral("props"), props);
    m_msgs.replace(mi, am);
    // add_stop 用 Nominatim 核坐标（Geocoder 自己守每秒最多 1 次和缓存）
    const QJsonArray chs = P.value(QStringLiteral("changes")).toArray();
    for (int k = 0; k < chs.size(); ++k)
        if (chs.at(k).toObject().contains(QStringLiteral("geo"))) m_geoQueue << GeoJob{ m_chatGen, mi, toolId, k };
    emit messagesChanged();
    nextGeo();
}

QJsonObject *AiController::propRef(int mi, int pi, QJsonObject &am, QJsonArray &props)
{
    if (mi < 0 || mi >= m_msgs.size()) return nullptr;
    am = m_msgs.at(mi).toObject();
    props = am.value(QStringLiteral("props")).toArray();
    if (pi < 0 || pi >= props.size()) return nullptr;
    m_propBuf = props.at(pi).toObject();
    return &m_propBuf;
}

void AiController::putProp(int mi, int pi, QJsonObject am, QJsonArray props, const QJsonObject &P, bool save)
{
    props.replace(pi, P);
    am.insert(QStringLiteral("props"), props);
    m_msgs.replace(mi, am);
    emit messagesChanged();
    if (save) saveChat();
}

void AiController::nextGeo()
{
    if (m_geoBusy || m_geoQueue.isEmpty()) return;
    const GeoJob j = m_geoQueue.takeFirst();
    if (j.gen != m_chatGen || j.mi >= m_msgs.size()) { nextGeo(); return; }
    QJsonArray props = m_msgs.at(j.mi).toObject().value(QStringLiteral("props")).toArray();
    int pi = -1;
    for (int i = 0; i < props.size(); ++i) if (props.at(i).toObject().value(QStringLiteral("toolId")).toString() == j.toolId) pi = i;
    if (pi < 0) { nextGeo(); return; }
    const QJsonObject c = props.at(pi).toObject().value(QStringLiteral("changes")).toArray().at(j.k).toObject().value(QStringLiteral("c")).toObject();
    const double lat = c.value(QStringLiteral("lat")).toDouble(qQNaN()), lng = c.value(QStringLiteral("lng")).toDouble(qQNaN());
    auto finish = [this, j](const QJsonObject &geo) {
        if (j.gen == m_chatGen && j.mi < m_msgs.size()) {
            QJsonObject am = m_msgs.at(j.mi).toObject();
            QJsonArray ps = am.value(QStringLiteral("props")).toArray();
            for (int i = 0; i < ps.size(); ++i) {
                QJsonObject P = ps.at(i).toObject();
                if (P.value(QStringLiteral("toolId")).toString() != j.toolId) continue;
                QJsonArray chs = P.value(QStringLiteral("changes")).toArray();
                QJsonObject ch = chs.at(j.k).toObject();
                ch.insert(QStringLiteral("geo"), geo);
                chs.replace(j.k, ch);
                P.insert(QStringLiteral("changes"), chs);
                ps.replace(i, P);
            }
            am.insert(QStringLiteral("props"), ps);
            m_msgs.replace(j.mi, am);
            emit messagesChanged();
        }
        m_geoBusy = false;
        nextGeo();
    };
    if (!m_geocoder || !m_net->allows(QStringLiteral("geocode"))) { finish(QJsonObject{ { QStringLiteral("state"), QStringLiteral("off") }, { QStringLiteral("pick"), -1 } }); return; }
    m_geoBusy = true;
    m_geocoder->search(c.value(QStringLiteral("name")).toString(), this, [finish, lat, lng](const QVariantList &rows, const QString &err, bool) {
        QJsonArray cands;
        int nearIdx = -1;
        for (const QVariant &v : rows) {
            const QVariantMap r = v.toMap();
            const double la = r.value(QStringLiteral("lat")).toDouble(), ln = r.value(QStringLiteral("lng")).toDouble();
            const double km = std::isfinite(lat) && std::isfinite(lng) ? std::hypot(la - lat, (ln - lng) * std::cos(lat * M_PI / 180)) * 111 : 1e9;
            if (nearIdx < 0 && km < 5) nearIdx = int(cands.size());
            cands.append(QJsonObject{ { QStringLiteral("name"), r.value(QStringLiteral("name")).toString() }, { QStringLiteral("addr"), r.value(QStringLiteral("address")).toString() },
                                      { QStringLiteral("lat"), la }, { QStringLiteral("lng"), ln } });
            if (cands.size() >= 5) break;
        }
        Q_UNUSED(err);
        finish(QJsonObject{ { QStringLiteral("state"), nearIdx >= 0 ? QStringLiteral("ok") : cands.isEmpty() ? QStringLiteral("none") : QStringLiteral("pick") },
                            { QStringLiteral("cands"), cands }, { QStringLiteral("pick"), nearIdx } });
    });
}

QString AiController::onResearch(QJsonObject &am, const QString &id, const QJsonObject &x, QJsonObject &results)
{
    const qint64 now = nowMs();
    QStringList todo, done;
    for (const auto &v : x.value(QStringLiteral("todo")).toArray()) todo << v.toString();
    for (const auto &v : x.value(QStringLiteral("done")).toArray()) done << v.toString();
    const int secs = std::clamp(int(x.value(QStringLiteral("remaining_seconds")).toDouble(60)), 10, 1800);   // 预算 10–1800 秒（不简化）
    const double mul = m_hooks.budgetMul > 0 ? m_hooks.budgetMul : 1;
    if (!m_research.on) { m_research.on = true; m_research.start = m_run.t0 ? m_run.t0 : now; }
    if (!m_research.budget) m_research.budget = (now - m_research.start) + qint64(secs * 1000 * mul);   // 第一次报的预计时间就是预算
    m_research.todo = todo;
    m_research.secs = secs;
    const qint64 used = now - m_research.start, left = m_research.budget - used;
    QJsonObject view{ { QStringLiteral("todo"), QJsonArray::fromStringList(todo) }, { QStringLiteral("done"), QJsonArray::fromStringList(done) }, { QStringLiteral("secs"), secs },
                      { QStringLiteral("used"), double(used) }, { QStringLiteral("finished"), x.value(QStringLiteral("finished")).toBool() }, { QStringLiteral("note"), x.value(QStringLiteral("note")).toString() } };
    QString decision;
    if (x.value(QStringLiteral("finished")).toBool()) { m_research = Research(); decision = QStringLiteral("stop"); }
    else if (left <= 5000) {   // 到预计时间了：停下来汇报，等用户决定
        view.insert(QStringLiteral("paused"), true);
        m_research.paused = true;
        results.insert(id, QStringLiteral("已经到预计时间，暂停等用户决定（用户会说\"继续查\"或\"先到这里\"）。"));
        decision = QStringLiteral("pause");
    } else {
        const bool soon = left < 45000;
        results.insert(id, QStringLiteral("继续。已用 %1 秒，离预计时间还剩 %2 秒。这一轮最多搜 3 次%3。").arg(used / 1000).arg(left / 1000)
                               .arg(soon ? QStringLiteral("；剩下的时间不多，先查最重要的一项，然后调用 research_status 汇报") : QStringLiteral("，结束时调用 research_status 汇报")));
        decision = QStringLiteral("go");
    }
    am.insert(QStringLiteral("research"), view);
    return decision;
}

void AiController::onFinished(const QString &error, bool aborted, bool stalled)
{
    m_tick.stop();
    m_liveThrottle.stop();
    drainTools();
    const StreamState &s = m_client.state();
    const int mi = int(m_msgs.size()) - 1;
    QJsonObject am = m_msgs.isEmpty() ? QJsonObject() : m_msgs.last().toObject();
    const bool anth = am.value(QStringLiteral("api")).toString() == QLatin1String("anthropic");
    am.insert(QStringLiteral("text"), s.text);
    if (!s.thinking.isEmpty()) am.insert(QStringLiteral("thinking"), s.thinking);
    if (!s.citations.isEmpty()) am.insert(QStringLiteral("sources"), QJsonArray::fromStringList(s.citations));
    QString err = error;
    if (m_guardStalled) err = tp::tr("连接卡住了（很久没有收到内容），已自动停下。可以再说一次。");
    else if (aborted && !stalled) err = tp::tr("已停止");
    if (!err.isEmpty()) am.insert(QStringLiteral("error"), err);

    // 工具调用：两种接法统一成 [id, 名字, 参数, 参数断了]
    struct Call { QString id, name; QJsonObject input; bool bad; };
    QList<Call> calls;
    QStringList act;
    if (anth) {
        QJsonArray content;
        for (int i = 0; i < s.blocks.size(); ++i) {
            const QJsonObject b = s.blocks.at(i).toObject();
            if (b.isEmpty()) continue;
            content.append(b);
            const QString bt = b.value(QStringLiteral("type")).toString();
            if (bt == QLatin1String("tool_use")) calls << Call{ b.value(QStringLiteral("id")).toString(), b.value(QStringLiteral("name")).toString(), b.value(QStringLiteral("input")).toObject(), s.badTools.contains(i) };
        }
        am.insert(QStringLiteral("content"), content);
    } else {
        QJsonArray tcs;
        for (auto it = s.toolJson.begin(); it != s.toolJson.end(); ++it) {
            const QString id = s.callIds.value(it.key(), QStringLiteral("call_%1").arg(it.key()));
            const QString name = s.callNames.value(it.key());
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson((it.value().trimmed().isEmpty() ? QStringLiteral("{}") : it.value()).toUtf8(), &pe);
            calls << Call{ id, name, doc.object(), pe.error != QJsonParseError::NoError || !doc.isObject() };
            tcs.append(QJsonObject{ { QStringLiteral("id"), id }, { QStringLiteral("type"), QStringLiteral("function") },
                                    { QStringLiteral("function"), QJsonObject{ { QStringLiteral("name"), name }, { QStringLiteral("arguments"), it.value() } } } });
        }
        QJsonObject oa{ { QStringLiteral("content"), s.text } };
        if (!s.thinking.isEmpty()) oa.insert(QStringLiteral("reasoning_content"), s.thinking);
        if (!tcs.isEmpty()) oa.insert(QStringLiteral("tool_calls"), tcs);
        am.insert(QStringLiteral("oa"), oa);
    }
    m_msgs.replace(mi, am);

    // 处理工具（出错、停下的不处理）
    QString go;
    int kimiSearches = 0;
    if (err.isEmpty()) {
        QJsonObject results = am.value(QStringLiteral("results")).toObject();
        QStringList read;
        QString rs;
        for (const Call &c : calls) {
            if (c.name == QLatin1String("propose_changes")) prepareProposal(mi, c.id, c.input, c.bad);
            else if (c.name == QLatin1String("get_plans")) { results.insert(c.id, tools::readPlans(root(), c.input, noMoney(), &read)); go = QStringLiteral("go"); }
            else if (c.name == QLatin1String("research_status")) { am = m_msgs.at(mi).toObject(); rs = onResearch(am, c.id, c.input, results); m_msgs.replace(mi, am); }
            else if (c.name == QLatin1String("$web_search")) {   // Kimi：参数原样回传它就去搜；每次请求最多 3 次
                ++kimiSearches;
                results.insert(c.id, kimiSearches <= tools::kMaxSearch ? QString::fromUtf8(QJsonDocument(c.input).toJson(QJsonDocument::Compact))
                                                                       : QStringLiteral("这一轮已经搜了 3 次，先用已有的结果。"));
                go = QStringLiteral("go");
            }
        }
        if (rs == QLatin1String("go")) go = QStringLiteral("go");
        if (rs == QLatin1String("pause")) go.clear();
        if (calls.isEmpty() && s.stop == QLatin1String("pause_turn")) go = QStringLiteral("pause_turn");
        am = m_msgs.at(mi).toObject();
        if (!read.isEmpty()) act.prepend(tp::tr("读了「%1」").arg(read.join(QStringLiteral("」「"))));
        am.insert(QStringLiteral("results"), results);
    }
    if (!act.isEmpty()) am.insert(QStringLiteral("act"), QJsonArray::fromStringList(act));

    // 计费：有用量就记（停下、卡住时服务器也可能已经算了输入）
    Usage usage = s.usage;
    usage.searches += std::min(kimiSearches, tools::kMaxSearch);
    const QString pid = am.value(QStringLiteral("provider")).toString(m_provider);
    const Provider *p = ai::provider(pid);
    if (usage.any() && p) {
        Price buf;
        const Price *pr = pid == QLatin1String("custom") ? price(&buf) : priceFor(*p, am.value(QStringLiteral("model")).toString());   // 按这条实际用的模型（从订单生成用快档）
        const QString cur = pid == QLatin1String("custom") ? custom().value(QStringLiteral("cur")).toString() : p->currency;
        const double c = ai::cost(pr, p->searchPrice, usage);
        const qint64 tok = usage.inTotal() + usage.output;
        QString meta = tp::tr("输入 %1 · 输出 %2 token").arg(QLocale().toString(usage.inTotal()), QLocale().toString(usage.output));
        if (usage.cacheRead) meta += tp::tr("（缓存 %1）").arg(QLocale().toString(usage.cacheRead));
        if (usage.searches) meta += QStringLiteral(" · ") + tp::tr("搜索 %1 次").arg(usage.searches);
        if (c >= 0) { meta += QStringLiteral(" · ") + money(c, cur); am.insert(QStringLiteral("cost"), c); am.insert(QStringLiteral("cur"), cur); }
        else meta += QStringLiteral(" · ") + tp::tr("没填单价，只记 token");
        am.insert(QStringLiteral("meta"), meta);
        m_msgs.replace(mi, am);
        record(pid, c, cur, tok);
        m_run.tok += tok;
        if (c >= 0) m_run.amt += c; else m_run.priced = false;
        m_run.cur = cur;
    } else {
        m_msgs.replace(mi, am);
    }
    if (s.truncated()) note(tp::tr("这次回答太长，被截断了。可以说“一次只改一个计划，攻略写短一点”再试。"));
    // 自动续轮：读了计划、查找还没到预计时间、Kimi 搜索、长搜索被暂停（pause_turn）
    if (!go.isEmpty() && m_run.autoN < tools::kMaxAuto) {
        ++m_run.autoN;
        if (go != QLatin1String("pause_turn")) m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("auto"), true }, { QStringLiteral("text"), QString() } });
        emit messagesChanged();
        run();
        return;
    }
    if (!go.isEmpty()) note(tp::tr("自动续轮次数到上限了，停在这里。可以接着说“继续”。"));
    // 从订单生成：Kimi / 自定义不强制调用，没给修改时说一句
    if (m_run.kind == QLatin1String("order") && err.isEmpty() && m_msgs.at(mi).toObject().value(QStringLiteral("props")).toArray().isEmpty())
        note(tp::tr("这次没整理出交通住宿。可以把订单文字删短一点再试，或者在设置 → AI 换一家服务商。"));
    recordPlan();
    emit busyChanged();
    emit messagesChanged();
    emit liveChanged();
    saveChat();
}

// ---------------- 预览：勾选、核对坐标、应用、撤销联动（I-04 / I-05） ----------------

void AiController::toggleChange(int mi, int pi, int k)
{
    QJsonObject am; QJsonArray props;
    QJsonObject *P = propRef(mi, pi, am, props);
    if (!P || P->value(QStringLiteral("state")).toString() != QLatin1String("open")) return;
    QJsonArray chs = P->value(QStringLiteral("changes")).toArray();
    if (k < 0 || k >= chs.size() || chs.at(k).toObject().contains(QStringLiteral("bad"))) return;
    QJsonObject ch = chs.at(k).toObject();
    ch.insert(QStringLiteral("on"), !ch.value(QStringLiteral("on")).toBool());
    chs.replace(k, ch);
    P->insert(QStringLiteral("changes"), chs);
    putProp(mi, pi, am, props, *P, false);
}

void AiController::toggleSched(int mi, int pi, int k, int j)
{
    QJsonObject am; QJsonArray props;
    QJsonObject *P = propRef(mi, pi, am, props);
    if (!P || P->value(QStringLiteral("state")).toString() != QLatin1String("open")) return;
    QJsonArray chs = P->value(QStringLiteral("changes")).toArray();
    if (k < 0 || k >= chs.size()) return;
    QJsonObject ch = chs.at(k).toObject();
    QJsonArray off = ch.value(QStringLiteral("schOff")).toArray();
    qsizetype at = off.size();
    while (--at >= 0 && off.at(at).toInt() != j) {}
    if (at >= 0) off.removeAt(at); else off.append(j);
    ch.insert(QStringLiteral("schOff"), off);
    chs.replace(k, ch);
    P->insert(QStringLiteral("changes"), chs);
    putProp(mi, pi, am, props, *P, false);
}

void AiController::toggleAll(int mi, int pi)
{
    QJsonObject am; QJsonArray props;
    QJsonObject *P = propRef(mi, pi, am, props);
    if (!P || P->value(QStringLiteral("state")).toString() != QLatin1String("open")) return;
    QJsonArray chs = P->value(QStringLiteral("changes")).toArray();
    bool all = true;
    for (const auto &v : chs) if (!v.toObject().contains(QStringLiteral("bad")) && !v.toObject().value(QStringLiteral("on")).toBool()) all = false;
    for (int k = 0; k < chs.size(); ++k) {
        QJsonObject ch = chs.at(k).toObject();
        if (ch.contains(QStringLiteral("bad"))) continue;
        ch.insert(QStringLiteral("on"), !all);
        chs.replace(k, ch);
    }
    P->insert(QStringLiteral("changes"), chs);
    putProp(mi, pi, am, props, *P, false);
}

void AiController::pickGeo(int mi, int pi, int k, int j)
{
    QJsonObject am; QJsonArray props;
    QJsonObject *P = propRef(mi, pi, am, props);
    if (!P || P->value(QStringLiteral("state")).toString() != QLatin1String("open")) return;
    QJsonArray chs = P->value(QStringLiteral("changes")).toArray();
    if (k < 0 || k >= chs.size()) return;
    QJsonObject ch = chs.at(k).toObject();
    QJsonObject g = ch.value(QStringLiteral("geo")).toObject();
    if (j >= g.value(QStringLiteral("cands")).toArray().size()) return;
    g.insert(QStringLiteral("pick"), j);
    ch.insert(QStringLiteral("geo"), g);
    chs.replace(k, ch);
    P->insert(QStringLiteral("changes"), chs);
    putProp(mi, pi, am, props, *P, false);
}

void AiController::applyProposal(int mi, int pi, bool force)
{
    QJsonObject am; QJsonArray props;
    QJsonObject *Pp = propRef(mi, pi, am, props);
    if (!Pp || !m_store || !m_app || Pp->value(QStringLiteral("state")).toString() != QLatin1String("open") || tools::geoBusy(*Pp)) return;
    QJsonObject P = *Pp;
    QString reopen;
    if (m_app->undoGrouped() && m_editorClose) reopen = m_editorClose();
    if (m_app->undoGrouped()) { emit toast(tp::tr("先关掉地图上的计划编辑器，再应用（不然编辑器保存时会把这些修改盖掉）"), true); return; }
    if (m_store->isReadOnly()) { emit toast(tp::tr("数据现在是只读的，改不了。"), true); return; }
    // 工作规则五：AI 修改前单独备份（travel.before-ai-*.json，留 10 份）。手上没存的改动先存成一步
    m_app->commitStep();
    QString why;
    if (m_store->snapshotBeforeAi(&why).isEmpty() && !force) {
        P.insert(QStringLiteral("warn"), tp::tr("应用前没能先存备份（%1）。还是要应用吗？应用后仍然可以 Ctrl+Z 撤销。").arg(why));
        putProp(mi, pi, am, props, P, false);
        return;
    }
    tools::Applied a;
    m_store->modify([&](json::Value &d) { a = tools::apply(d, P, [](const QString &pre) { return tools::uid(pre); }); });
    m_app->commitStep();   // 一次应用在撤销里是一步
    P.remove(QStringLiteral("warn"));
    P.insert(QStringLiteral("state"), QStringLiteral("applied"));
    P.insert(QStringLiteral("applied"), a.n);
    const QString result = tools::resultText(P, a);
    QJsonObject results = am.value(QStringLiteral("results")).toObject();
    results.insert(P.value(QStringLiteral("toolId")).toString(), result);
    am.insert(QStringLiteral("results"), results);
    m_last = Last{ true, m_chatId, mi, pi, m_app->undoStateId(), result };
    qInfo("AI 修改已应用：%d 项", a.n);
    if (m_onDeleted && !a.deleted.isEmpty()) m_onDeleted(a.deleted);
    putProp(mi, pi, am, props, P, true);
    if (!reopen.isEmpty() && m_editorReopen) m_editorReopen(reopen);   // 编辑器打开后自己会算它那个计划的段
    if (a.needRoute) emit routeNeeded(a.touched);   // 1.1.0 直接算，不问（编辑器开着就排队，关了再算）
}

void AiController::skipProposal(int mi, int pi)
{
    QJsonObject am; QJsonArray props;
    QJsonObject *P = propRef(mi, pi, am, props);
    if (!P || P->value(QStringLiteral("state")).toString() != QLatin1String("open")) return;
    P->insert(QStringLiteral("state"), QStringLiteral("skipped"));
    QJsonObject results = am.value(QStringLiteral("results")).toObject();
    results.insert(P->value(QStringLiteral("toolId")).toString(), QStringLiteral("用户没有应用这些修改。"));
    am.insert(QStringLiteral("results"), results);
    putProp(mi, pi, am, props, *P, true);
}

// 撤销 / 重做：撤到这次应用之前 → "已撤销"并告诉 AI；重做回来 → "已应用"（Electron tp:undo）。
// 按应用后那一版数据的编号认（0.13.3）：在重做栈里 = 撤销了，是现在或在撤销栈里 = 还在；挤出栈 / 换了分支就不动
void AiController::onUndo(int dir)
{
    if (!m_last.valid || m_last.chat != m_chatId || !m_app) return;
    QJsonObject am; QJsonArray props;
    QJsonObject *P = propRef(m_last.mi, m_last.pi, am, props);
    if (!P) return;
    const QString st = P->value(QStringLiteral("state")).toString();
    QString result;
    const int w = m_app->undoWhere(m_last.mark);
    if (dir < 0 && st == QLatin1String("applied") && w < 0) {
        P->insert(QStringLiteral("state"), QStringLiteral("undone"));
        result = m_last.result + QStringLiteral("（之后用户按 Ctrl+Z 撤销了，数据已经回到应用前。）");
    } else if (dir > 0 && st == QLatin1String("undone") && w > 0) {
        P->insert(QStringLiteral("state"), QStringLiteral("applied"));
        result = m_last.result;
    } else return;
    QJsonObject results = am.value(QStringLiteral("results")).toObject();
    results.insert(P->value(QStringLiteral("toolId")).toString(), result);
    am.insert(QStringLiteral("results"), results);
    putProp(m_last.mi, m_last.pi, am, props, *P, true);
}

// ---- 还缺 / 没查（gaps）----
void AiController::toggleGap(int mi, int pi, int j)
{
    QJsonObject am; QJsonArray props;
    QJsonObject *P = propRef(mi, pi, am, props);
    if (!P || P->value(QStringLiteral("gapState")).toString() != QLatin1String("open")) return;
    QJsonArray gs = P->value(QStringLiteral("gaps")).toArray();
    if (j < 0 || j >= gs.size()) return;
    QJsonObject g = gs.at(j).toObject();
    g.insert(QStringLiteral("on"), !g.value(QStringLiteral("on")).toBool());
    gs.replace(j, g);
    P->insert(QStringLiteral("gaps"), gs);
    putProp(mi, pi, am, props, *P, false);
}

void AiController::gapsSkip(int mi, int pi)
{
    QJsonObject am; QJsonArray props;
    QJsonObject *P = propRef(mi, pi, am, props);
    if (!P || P->value(QStringLiteral("gapState")).toString() != QLatin1String("open")) return;
    P->insert(QStringLiteral("gapState"), QStringLiteral("closed"));
    putProp(mi, pi, am, props, *P, true);
}

void AiController::gapsFill(int mi, int pi)
{
    QJsonObject am; QJsonArray props;
    QJsonObject *P = propRef(mi, pi, am, props);
    if (!P || busy() || P->value(QStringLiteral("gapState")).toString() != QLatin1String("open")) return;
    QStringList sel;
    for (const auto &v : P->value(QStringLiteral("gaps")).toArray()) if (v.toObject().value(QStringLiteral("on")).toBool()) sel << v.toObject().value(QStringLiteral("text")).toString();
    if (sel.isEmpty()) return;
    P->insert(QStringLiteral("gapState"), QStringLiteral("sent"));
    putProp(mi, pi, am, props, *P, false);
    QStringList lines;
    for (const QString &s : sel) lines << QStringLiteral("- ") + s;
    sendWith(QStringLiteral("请补充下面这几项（需要就联网查；要搜 3 次以上时先用 research_status 报预计时间），把结果写进计划，做成修改预览：\n") + lines.join(u'\n'),
             tp::tr("补充：%1").arg(sel.join(QStringLiteral("；"))), true, false);
}

// ---- 查找的时间预算（I-06）：到预计时间停下来问"继续查 / 先到这里" ----
void AiController::researchAnswer(int mi, bool more)
{
    if (busy() || mi < 0 || mi >= m_msgs.size()) return;
    QJsonObject am = m_msgs.at(mi).toObject();
    QJsonObject r = am.value(QStringLiteral("research")).toObject();
    if (!r.value(QStringLiteral("paused")).toBool() || r.value(QStringLiteral("answered")).toBool()) return;
    r.insert(QStringLiteral("answered"), true);
    am.insert(QStringLiteral("research"), r);
    m_msgs.replace(mi, am);
    emit messagesChanged();
    if (more && m_research.on) {
        const double mul = m_hooks.budgetMul > 0 ? m_hooks.budgetMul : 1;
        m_research.budget += qint64(r.value(QStringLiteral("secs")).toInt() * 1000 * mul);
        m_research.paused = false;
        sendWith(QStringLiteral("继续查。"), tp::tr("继续查"), true, true);
    } else {
        m_research = Research();
        sendWith(QStringLiteral("先到这里：把已经查到的整理成修改预览，没查完的放进 gaps。"), tp::tr("先到这里"), false, false);
    }
}

// ---------------- 历史 ----------------

QJsonArray AiController::readChats() const
{
    QFile f(m_chatsFile);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}

// 从文件读回来的对话：没应用的预览标"已过期"（站点等目标要重新认领，不能再直接应用；Electron packMsg）
QJsonArray AiController::restored(const QJsonArray &msgs)
{
    QJsonArray out;
    for (const auto &v : msgs) {
        QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("props"))) {
            QJsonArray ps = o.value(QStringLiteral("props")).toArray();
            for (int i = 0; i < ps.size(); ++i) {
                QJsonObject P = ps.at(i).toObject();
                if (P.value(QStringLiteral("state")).toString() == QLatin1String("open")) P.insert(QStringLiteral("state"), QStringLiteral("expired"));
                if (P.value(QStringLiteral("gapState")).toString() == QLatin1String("open")) P.insert(QStringLiteral("gapState"), QStringLiteral("closed"));
                ps.replace(i, P);
            }
            o.insert(QStringLiteral("props"), ps);
        }
        if (o.contains(QStringLiteral("research"))) { QJsonObject r = o.value(QStringLiteral("research")).toObject(); r.insert(QStringLiteral("answered"), true); o.insert(QStringLiteral("research"), r); }
        out.append(o);
    }
    return out;
}

void AiController::loadChats()
{
    const QJsonArray all = readChats();   // 启动接着最近一段（Electron 同样）
    const QJsonObject c = all.isEmpty() ? QJsonObject() : all.first().toObject();
    m_chatId = c.value(QStringLiteral("id")).toString();
    m_msgs = restored(c.value(QStringLiteral("msgs")).toArray());
    ++m_chatGen;
    m_last = Last();
    m_research = Research();
    emit chatsChanged();
    emit messagesChanged();
}

void AiController::setChatsFile(const QString &f)
{
    m_chatsFile = f;
    loadChats();
}

void AiController::saveChat()
{
    QJsonArray keep;
    QString title;
    for (const auto &v : m_msgs) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("role")) == QLatin1String("note")) continue;
        if (title.isEmpty() && o.value(QStringLiteral("role")) == QLatin1String("user") && !o.value(QStringLiteral("auto")).toBool())
            title = o.value(QStringLiteral("shown")).toString(o.value(QStringLiteral("text")).toString()).simplified().left(40);
        keep.append(o);
    }
    if (title.isEmpty()) return;
    if (m_chatId.isEmpty()) m_chatId = QStringLiteral("c%1").arg(nowMs());
    QJsonArray all{ QJsonObject{ { QStringLiteral("id"), m_chatId }, { QStringLiteral("title"), title }, { QStringLiteral("at"), double(nowMs()) }, { QStringLiteral("msgs"), keep } } };
    for (const auto &v : readChats())
        if (v.toObject().value(QStringLiteral("id")).toString() != m_chatId && all.size() < kMaxChats) all.append(v);
    QByteArray bytes = QJsonDocument(all).toJson(QJsonDocument::Compact);
    while (bytes.size() > kMaxChatBytes && all.size() > 1) { all.removeLast(); bytes = QJsonDocument(all).toJson(QJsonDocument::Compact); }
    QSaveFile f(m_chatsFile);
    if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit()) emit chatsChanged();
    else qWarning("AI 对话没存上：%s", qUtf8Printable(f.errorString()));
}

QVariantList AiController::chats() const
{
    QVariantList out;
    for (const auto &v : readChats()) {
        const QJsonObject o = v.toObject();
        out << QVariantMap{ { QStringLiteral("id"), o.value(QStringLiteral("id")).toString() }, { QStringLiteral("title"), o.value(QStringLiteral("title")).toString() },
                            { QStringLiteral("at"), QDateTime::fromMSecsSinceEpoch(qint64(o.value(QStringLiteral("at")).toDouble())).toString(QStringLiteral("M/d HH:mm")) } };
    }
    return out;
}

void AiController::newChat()
{
    if (busy()) return;
    saveChat();
    m_msgs = QJsonArray();
    m_chatId.clear();
    m_pending.clear();
    ++m_chatGen;
    m_last = Last();
    m_research = Research();
    emit tellChanged();
    emit messagesChanged();
    emit chatsChanged();
}

void AiController::openChat(const QString &id)
{
    if (busy()) return;
    for (const auto &v : readChats()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString() != id) continue;
        saveChat();
        m_chatId = id;
        m_msgs = restored(o.value(QStringLiteral("msgs")).toArray());
        ++m_chatGen;
        m_last = Last();
        m_research = Research();
        saveChat();   // 排到最前：下次启动接着这一段
        emit messagesChanged();
        emit chatsChanged();
        return;
    }
}

// ---------------- 其他 ----------------

bool AiController::voice()
{
#ifdef Q_OS_WIN
    // Electron 用 PowerShell 调 keybd_event；这里直接 SendInput（左 Win + H）。被安全软件拦时界面提示直接按 Win+H
    INPUT in[4] = {};
    for (INPUT &i : in) i.type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_LWIN; in[1].ki.wVk = 'H';
    in[2].ki.wVk = 'H'; in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_LWIN; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(4, in, sizeof(INPUT)) == 4;
#else
    return false;
#endif
}

void AiController::openSignup(const QString &id)
{
    const Provider *p = ai::provider(id);
    if (p && !p->signup.isEmpty()) QDesktopServices::openUrl(QUrl(p->signup));
}

void AiController::shotSetup(const QString &page)
{
    if (!m_credPrefix.contains(QLatin1String("-test"))) return;   // 只在截图 / 自检时
    m_provider = QStringLiteral("claude");
    m_tier = QStringLiteral("balanced");
    if (page == QLatin1String("ui-ai-guide") || page == QLatin1String("ui-aiplan-guide")) platform::credentials::remove(credTarget(m_provider));
    else platform::credentials::write(credTarget(m_provider), QStringLiteral("sk-ant-test-0000000000000000000000-shot"));
    loadKeys();
    m_msgs = QJsonArray();
    m_shotRun.clear();
    if (page == QLatin1String("ui-ai-chat") || page == QLatin1String("ui-settings-ai")) {
        m_settings->setValue(QStringLiteral("aiUsage"), fromQ(QJsonObject{ { QStringLiteral("m"), monthKey() }, { QStringLiteral("by"), QJsonObject{
            { QStringLiteral("claude"), QJsonObject{ { QStringLiteral("amt"), 0.42 }, { QStringLiteral("cur"), QStringLiteral("USD") }, { QStringLiteral("n"), 12 }, { QStringLiteral("tok"), 96000 } } },
            { QStringLiteral("deepseek"), QJsonObject{ { QStringLiteral("amt"), 0.36 }, { QStringLiteral("cur"), QStringLiteral("CNY") }, { QStringLiteral("n"), 5 }, { QStringLiteral("tok"), 41000 } } } } },
            // 0.13.2 最近的方案用量
            { QStringLiteral("plans"), QJsonArray{
                QJsonObject{ { QStringLiteral("t"), QStringLiteral("2026-11-08T15:42:00") }, { QStringLiteral("p"), QStringLiteral("claude") }, { QStringLiteral("m"), QStringLiteral("claude-haiku-5-5") }, { QStringLiteral("tok"), 6400 }, { QStringLiteral("amt"), 0.0012 }, { QStringLiteral("cur"), QStringLiteral("USD") }, { QStringLiteral("k"), QStringLiteral("order") } },
                QJsonObject{ { QStringLiteral("t"), QStringLiteral("2026-11-08T15:20:00") }, { QStringLiteral("p"), QStringLiteral("claude") }, { QStringLiteral("m"), QStringLiteral("claude-sonnet-5-5") }, { QStringLiteral("tok"), 21500 }, { QStringLiteral("amt"), 0.081 }, { QStringLiteral("cur"), QStringLiteral("USD") }, { QStringLiteral("k"), QStringLiteral("edit") } },
                QJsonObject{ { QStringLiteral("t"), QStringLiteral("2026-11-08T15:02:00") }, { QStringLiteral("p"), QStringLiteral("deepseek") }, { QStringLiteral("m"), QStringLiteral("deepseek-flash") }, { QStringLiteral("tok"), 34800 }, { QStringLiteral("amt"), 0.11 }, { QStringLiteral("cur"), QStringLiteral("CNY") }, { QStringLiteral("k"), QStringLiteral("wizard") } } } } }));
    }
    if (page == QLatin1String("ui-ai-chat")) {
        m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("text"), QStringLiteral("十月去京都，红叶大概什么时候最好？") } });
        m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") }, { QStringLiteral("provider"), QStringLiteral("claude") }, { QStringLiteral("model"), QStringLiteral("claude-sonnet-5-5") },
                                   { QStringLiteral("text"), QStringLiteral("京都红叶一般 11 月中旬到 12 月初最好，十月还偏绿。\n- 10 月下旬：只有高处（鞍马、贵船）开始变色\n- 11 月 15–30 日：市区东福寺、永观堂、岚山最盛\n- 想十月看红叶，可以往北去立山黑部或北海道\n\n具体每年早晚差一两周，出发前看 **日本气象协会** 的红叶预报。") },
                                   { QStringLiteral("meta"), tp::tr("输入 %1 · 输出 %2 token").arg(QLocale().toString(1820), QLocale().toString(164)) + QStringLiteral(" · $0.005") }, { QStringLiteral("cost"), 0.0053 }, { QStringLiteral("cur"), QStringLiteral("USD") } });
    } else if (page.startsWith(QLatin1String("ui-ai-props")) || page == QLatin1String("ui-ai-done") || page == QLatin1String("ui-ai-research")) {
        // 0.13.1：修改预览（含找不到的标红、坐标对不上要选）、已应用 / 已撤销、查找进度停下来问、"没有联网核实"
        const json::Array &plans = root()[QStringLiteral("plans")].asArray();
        const json::Value plan = plans.empty() ? json::Value() : plans.front();
        const QString pid = plan[QStringLiteral("id")].toString();
        const json::Array &stops = plan[QStringLiteral("stops")].asArray();
        const QString s1 = stops.empty() ? QString() : stops.front()[QStringLiteral("id")].toString();
        auto user = [this](const QString &t) { m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("text"), t } }); };
        auto bot = [this](const QString &t, const QJsonArray &props, bool noWeb) {
            m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") }, { QStringLiteral("provider"), QStringLiteral("deepseek") }, { QStringLiteral("text"), t },
                                       { QStringLiteral("props"), props }, { QStringLiteral("noWeb"), noWeb },
                                       { QStringLiteral("meta"), tp::tr("输入 %1 · 输出 %2 token").arg(QLocale().toString(9120), QLocale().toString(860)) + QStringLiteral(" · ¥0.026") } });
        };
        const QJsonObject input{ { QStringLiteral("summary"), QStringLiteral("第二天加一站岚山，补上交通方式和两条日程") }, { QStringLiteral("plan_id"), pid },
            { QStringLiteral("changes"), QJsonArray{
                QJsonObject{ { QStringLiteral("op"), QStringLiteral("add_stop") }, { QStringLiteral("name"), QStringLiteral("Arashiyama") }, { QStringLiteral("lat"), 35.0094 }, { QStringLiteral("lng"), 135.6668 }, { QStringLiteral("after_stop_id"), s1 }, { QStringLiteral("legMode"), QStringLiteral("train") },
                             { QStringLiteral("schedule"), QJsonArray{   // 0.13.3 新站点带日程
                                 QJsonObject{ { QStringLiteral("time"), QStringLiteral("08:30") }, { QStringLiteral("end_time"), QStringLiteral("10:00") }, { QStringLiteral("text"), QStringLiteral("竹林小径、野宫神社") } },
                                 QJsonObject{ { QStringLiteral("time"), QStringLiteral("10:30") }, { QStringLiteral("text"), QStringLiteral("天龙寺庭园") } },
                                 QJsonObject{ { QStringLiteral("time"), QStringLiteral("13:00") }, { QStringLiteral("text"), QStringLiteral("渡月桥一带吃午饭") } } } } },
                QJsonObject{ { QStringLiteral("op"), QStringLiteral("update_stop") }, { QStringLiteral("stop_id"), s1 }, { QStringLiteral("legMode"), QStringLiteral("train") } },
                QJsonObject{ { QStringLiteral("op"), QStringLiteral("add_schedule") }, { QStringLiteral("stop_id"), s1 }, { QStringLiteral("time"), QStringLiteral("09:00") }, { QStringLiteral("text"), QStringLiteral("竹林小径，早去人少") } },
                QJsonObject{ { QStringLiteral("op"), QStringLiteral("remove_stop") }, { QStringLiteral("stop_id"), QStringLiteral("s_gone") } },
                QJsonObject{ { QStringLiteral("op"), QStringLiteral("add_todo") }, { QStringLiteral("text"), QStringLiteral("订嵯峨野小火车") }, { QStringLiteral("date"), QStringLiteral("2026-12-01") } } } },
            { QStringLiteral("gaps"), QJsonArray{ QJsonObject{ { QStringLiteral("text"), QStringLiteral("嵯峨野小火车的发车时间和订票网址") }, { QStringLiteral("search"), true } },
                                                  QJsonObject{ { QStringLiteral("text"), QStringLiteral("第二晚住宿还没订") } } } } };
        QJsonObject P = tools::prepare(root(), input, pid, QStringLiteral("toolu_shot1"), false);
        QJsonArray chs = P.value(QStringLiteral("changes")).toArray();
        QJsonObject c0 = chs.at(0).toObject();
        c0.insert(QStringLiteral("geo"), QJsonObject{ { QStringLiteral("state"), QStringLiteral("pick") }, { QStringLiteral("pick"), 0 }, { QStringLiteral("cands"), QJsonArray{
            QJsonObject{ { QStringLiteral("name"), QStringLiteral("Arashiyama") }, { QStringLiteral("addr"), QStringLiteral("Arashiyama, Ukyo Ward, Kyoto, Japan") }, { QStringLiteral("lat"), 35.0170 }, { QStringLiteral("lng"), 135.6770 } },
            QJsonObject{ { QStringLiteral("name"), QStringLiteral("Arashiyama Station") }, { QStringLiteral("addr"), QStringLiteral("Arashiyama Station, Nishikyo Ward, Kyoto, Japan") }, { QStringLiteral("lat"), 35.0122 }, { QStringLiteral("lng"), 135.6810 } } } } });
        c0.insert(QStringLiteral("schOff"), QJsonArray{ 2 });   // 截图里取消一条
        chs.replace(0, c0);
        P.insert(QStringLiteral("changes"), chs);
        if (page.startsWith(QLatin1String("ui-ai-props"))) {
            user(QStringLiteral("第二天想去岚山，帮我加进去"));
            bot(QStringLiteral("岚山放在第二天上午最顺：京都站坐 JR 嵯峨野线 15 分钟到嵯峨岚山，竹林小径早上 9 点前人少。\n- 下午回市区，傍晚去清水寺"), QJsonArray{ P }, true);
        } else if (page == QLatin1String("ui-ai-done")) {
            QJsonObject A = P, U = P;
            A.insert(QStringLiteral("state"), QStringLiteral("applied")); A.insert(QStringLiteral("applied"), 4); A.insert(QStringLiteral("gapState"), QStringLiteral("closed"));
            U.insert(QStringLiteral("state"), QStringLiteral("undone")); U.insert(QStringLiteral("applied"), 2); U.insert(QStringLiteral("gaps"), QJsonArray());
            user(QStringLiteral("第二天想去岚山，帮我加进去"));
            bot(QStringLiteral("加好了岚山和两条日程。"), QJsonArray{ A }, false);
            user(QStringLiteral("把最后一站删掉"));
            bot(QStringLiteral("最后一站已经提成删除。"), QJsonArray{ U }, true);
        } else {
            user(QStringLiteral("补充：嵯峨野小火车的发车时间和订票网址；第二晚住宿还没订"));
            m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") }, { QStringLiteral("provider"), QStringLiteral("claude") }, { QStringLiteral("text"), QStringLiteral("小火车 12 月每天 9:02 起每小时一班，周三停运。") },
                                       { QStringLiteral("act"), QJsonArray{ tp::tr("读了「%1」").arg(plan[QStringLiteral("name")].toString()) } }, { QStringLiteral("api"), QStringLiteral("anthropic") },
                                       { QStringLiteral("sources"), QJsonArray{ QStringLiteral("https://www.sagano-kanko.co.jp/"), QStringLiteral("https://www.japan-guide.com/e/e3912.html") } },
                                       { QStringLiteral("research"), QJsonObject{ { QStringLiteral("todo"), QJsonArray{ QStringLiteral("住宿空房"), QStringLiteral("小火车订票网址") } }, { QStringLiteral("done"), QJsonArray{ QStringLiteral("小火车时刻") } },
                                                                                   { QStringLiteral("secs"), 60 }, { QStringLiteral("used"), 125000.0 }, { QStringLiteral("paused"), true }, { QStringLiteral("note"), QStringLiteral("周三停运，避开周三") } } } });
            m_shotRun = tp::tr("已用 %1 / 预计 %2").arg(QStringLiteral("2:05"), QStringLiteral("2:00")) + QStringLiteral(" · ") + tp::tr("还剩 %1 项").arg(2) + QStringLiteral(" · ") + tp::tr("已写 %1 字").arg(QLocale().toString(1240));
        }
    } else if (page == QLatin1String("ui-ai-tell")) {
        m_pending = QStringLiteral("帮我看看五天的东京行程怎么排");
    }
    emit changed(); emit keysChanged(); emit usageChanged(); emit messagesChanged(); emit tellChanged(); emit liveChanged();
}

void AiController::setTestHooks(const TestHooks &h)
{
    m_hooks = h;
    if (!h.credPrefix.isEmpty()) { m_credPrefix = h.credPrefix; loadKeys(); }
    if (!h.chatsFile.isEmpty()) setChatsFile(h.chatsFile);
    emit changed();
}

} // namespace tp::app
