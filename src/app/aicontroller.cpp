#include "aicontroller.h"

#include "aiprompt.h"
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
    return u.value(QStringLiteral("m")).toString() == monthKey() ? u : QJsonObject{ { QStringLiteral("m"), monthKey() } };
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

void AiController::note(const QString &text, const QString &kind)
{
    m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("note") }, { QStringLiteral("text"), text }, { QStringLiteral("kind"), kind } });
    emit messagesChanged();
}

QVariantList AiController::messages() const
{
    QVariantList out;
    for (int i = 0; i < m_msgs.size(); ++i) {
        QVariantMap m = m_msgs.at(i).toObject().toVariantMap();
        if (m.value(QStringLiteral("role")) == QLatin1String("assistant")) m.insert(QStringLiteral("html"), mdLite(m.value(QStringLiteral("text")).toString()));
        m.insert(QStringLiteral("live"), busy() && i == m_msgs.size() - 1);
        out << m;
    }
    return out;
}

QString AiController::liveText() const { return mdLite(m_client.state().text); }

QString AiController::runText() const
{
    if (!busy()) return {};
    const qint64 now = nowMs();
    QStringList parts{ tp::tr("已用 %1 秒").arg((now - m_startMs) / 1000) };
    const qsizetype chars = m_client.state().text.size();
    if (chars) parts << tp::tr("已写 %1 字").arg(chars);
    else if (!m_client.state().thinking.isEmpty()) parts << tp::tr("正在思考");
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

bool AiController::send(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || busy()) return false;
    if (!m_net->allows(QStringLiteral("ai"))) { note(tp::tr("AI 助手现在不能用：离线模式开着，或者设置 → 联网里关掉了“AI 助手”。")); return false; }
    if (!hasKey()) { note(tp::tr("还没有填 %1 的钥匙：设置 → AI。").arg(providerName()), QStringLiteral("nokey")); return false; }
    if (m_provider == QLatin1String("custom") && (base().isEmpty() || model().isEmpty())) { note(tp::tr("自定义服务要先在设置 → AI 里填地址和模型名。")); return false; }
    if (capReached()) {
        note(tp::tr("这个月已经用了约 %1，到了上限 %2，先停在这里。").arg(money(used(), mainCur()), money(cap(), mainCur())), QStringLiteral("cap"));
        return false;
    }
    if (!conf().value(QStringLiteral("told")).toArray().contains(m_provider)) {
        m_pending = t;
        emit tellChanged();
        return false;
    }
    doSend(t);
    return true;
}

void AiController::confirmTell()
{
    if (m_pending.isEmpty()) return;
    QJsonArray told = conf().value(QStringLiteral("told")).toArray();
    if (!told.contains(m_provider)) told.append(m_provider);
    setConf(QStringLiteral("told"), told);
    const QString t = m_pending;
    m_pending.clear();
    emit tellChanged();
    doSend(t);
}

void AiController::cancelTell() { m_pending.clear(); emit tellChanged(); }

void AiController::doSend(const QString &text)
{
    // 历史：用户和助手说过的话（只带文字；出错、空的回答和它前面那一问不带）
    QJsonArray hist;
    for (qsizetype i = 0; i < m_msgs.size(); ++i) {
        const QJsonObject o = m_msgs.at(i).toObject();
        const QString role = o.value(QStringLiteral("role")).toString();
        // 没答上来的那一问（出错、停在半路一个字没写）不再带：免得和新的一问并在一起又发一遍
        if (role == QLatin1String("user") && i + 1 < m_msgs.size()) {
            const QJsonObject n = m_msgs.at(i + 1).toObject();
            if (n.value(QStringLiteral("role")) == QLatin1String("assistant") && n.value(QStringLiteral("text")).toString().isEmpty()) continue;
        }
        if ((role == QLatin1String("user") || role == QLatin1String("assistant")) && !o.value(QStringLiteral("text")).toString().isEmpty())
            hist.append(QJsonObject{ { QStringLiteral("role"), role }, { QStringLiteral("text"), o.value(QStringLiteral("text")).toString() } });
    }
    // 两种接口都要求用户、助手轮流；最后一条助手没说完（停了）时也没关系，后面接新的用户话
    while (!hist.isEmpty() && hist.first().toObject().value(QStringLiteral("role")).toString() != QLatin1String("user")) hist.removeFirst();
    hist.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("text"), text } });
    QJsonArray merged;   // 连着两条同一个人的话并成一条
    for (const auto &v : hist) {
        const QJsonObject o = v.toObject();
        if (!merged.isEmpty() && merged.last().toObject().value(QStringLiteral("role")) == o.value(QStringLiteral("role"))) {
            QJsonObject l = merged.last().toObject();
            l.insert(QStringLiteral("text"), l.value(QStringLiteral("text")).toString() + QStringLiteral("\n\n") + o.value(QStringLiteral("text")).toString());
            merged.replace(merged.size() - 1, l);
        } else merged.append(o);
    }

    m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("text"), text } });
    m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") }, { QStringLiteral("text"), QString() },
                               { QStringLiteral("provider"), m_provider }, { QStringLiteral("model"), model() } });
    const Provider *p = ai::provider(m_provider);
    const Request r = buildChat(*p, base(), keyFor(m_provider), model(), think(), effort(), systemPrompt(QDateTime::currentDateTime()), merged);
    m_client.setNoByteMs(m_hooks.noByteMs > 0 ? m_hooks.noByteMs : 90000);
    m_startMs = m_lastEventMs = nowMs();
    m_guardStalled = false;
    m_client.start(r, p->kind);
    m_tick.start();
    qInfo("AI 发送：%s %s 思考=%d", qUtf8Printable(m_provider), qUtf8Printable(model()), int(think()));
    emit busyChanged();
    emit messagesChanged();
    emit liveChanged();
}

void AiController::stop() { m_client.abort(); }

void AiController::onFinished(const QString &error, bool aborted, bool stalled)
{
    m_tick.stop();
    m_liveThrottle.stop();
    const StreamState &s = m_client.state();
    QJsonObject am = m_msgs.isEmpty() ? QJsonObject() : m_msgs.last().toObject();
    am.insert(QStringLiteral("text"), s.text);
    if (!s.thinking.isEmpty()) am.insert(QStringLiteral("thinking"), s.thinking);
    if (!s.citations.isEmpty()) am.insert(QStringLiteral("sources"), QJsonArray::fromStringList(s.citations));
    QString err = error;
    if (m_guardStalled) err = tp::tr("连接卡住了（很久没有收到内容），已自动停下。可以再说一次。");
    else if (aborted && !stalled) err = tp::tr("已停止");
    if (!err.isEmpty()) am.insert(QStringLiteral("error"), err);
    // 计费：有用量就记（停下、卡住时服务器也可能已经算了输入）
    const QString pid = am.value(QStringLiteral("provider")).toString(m_provider);
    const Provider *p = ai::provider(pid);
    if (s.usage.any() && p) {
        Price buf;
        const Price *pr = pid == m_provider ? price(&buf) : priceFor(*p, am.value(QStringLiteral("model")).toString());
        const QString cur = pid == QLatin1String("custom") ? custom().value(QStringLiteral("cur")).toString() : p->currency;
        const double c = ai::cost(pr, p->searchPrice, s.usage);
        const qint64 tok = s.usage.inTotal() + s.usage.output;
        QString meta = tp::tr("输入 %1 · 输出 %2 token").arg(QLocale().toString(s.usage.inTotal()), QLocale().toString(s.usage.output));
        if (s.usage.cacheRead) meta += tp::tr("（缓存 %1）").arg(QLocale().toString(s.usage.cacheRead));
        if (c >= 0) { meta += QStringLiteral(" · ") + money(c, cur); am.insert(QStringLiteral("cost"), c); am.insert(QStringLiteral("cur"), cur); }
        else meta += QStringLiteral(" · ") + tp::tr("没填单价，只记 token");
        am.insert(QStringLiteral("meta"), meta);
        m_msgs.replace(m_msgs.size() - 1, am);
        record(pid, c, cur, tok);
    } else if (!m_msgs.isEmpty()) {
        m_msgs.replace(m_msgs.size() - 1, am);
    }
    if (s.truncated()) note(tp::tr("这次回答太长，被截断了。可以说“短一点”或者把问题拆开再问。"));
    emit busyChanged();
    emit messagesChanged();
    emit liveChanged();
    saveChat();
}

// ---------------- 历史 ----------------

QJsonArray AiController::readChats() const
{
    QFile f(m_chatsFile);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}

void AiController::loadChats()
{
    const QJsonArray all = readChats();   // 启动接着最近一段（Electron 同样）
    const QJsonObject c = all.isEmpty() ? QJsonObject() : all.first().toObject();
    m_chatId = c.value(QStringLiteral("id")).toString();
    m_msgs = c.value(QStringLiteral("msgs")).toArray();
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
        if (title.isEmpty() && o.value(QStringLiteral("role")) == QLatin1String("user")) title = o.value(QStringLiteral("text")).toString().simplified().left(40);
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
        m_msgs = o.value(QStringLiteral("msgs")).toArray();
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
    if (page == QLatin1String("ui-ai-guide")) platform::credentials::remove(credTarget(m_provider));
    else platform::credentials::write(credTarget(m_provider), QStringLiteral("sk-ant-test-0000000000000000000000-shot"));
    loadKeys();
    m_msgs = QJsonArray();
    if (page == QLatin1String("ui-ai-chat") || page == QLatin1String("ui-settings-ai")) {
        m_settings->setValue(QStringLiteral("aiUsage"), fromQ(QJsonObject{ { QStringLiteral("m"), monthKey() }, { QStringLiteral("by"), QJsonObject{
            { QStringLiteral("claude"), QJsonObject{ { QStringLiteral("amt"), 0.42 }, { QStringLiteral("cur"), QStringLiteral("USD") }, { QStringLiteral("n"), 12 }, { QStringLiteral("tok"), 96000 } } },
            { QStringLiteral("deepseek"), QJsonObject{ { QStringLiteral("amt"), 0.36 }, { QStringLiteral("cur"), QStringLiteral("CNY") }, { QStringLiteral("n"), 5 }, { QStringLiteral("tok"), 41000 } } } } } }));
    }
    if (page == QLatin1String("ui-ai-chat")) {
        m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("text"), QStringLiteral("十月去京都，红叶大概什么时候最好？") } });
        m_msgs.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") }, { QStringLiteral("provider"), QStringLiteral("claude") }, { QStringLiteral("model"), QStringLiteral("claude-sonnet-5-5") },
                                   { QStringLiteral("text"), QStringLiteral("京都红叶一般 11 月中旬到 12 月初最好，十月还偏绿。\n- 10 月下旬：只有高处（鞍马、贵船）开始变色\n- 11 月 15–30 日：市区东福寺、永观堂、岚山最盛\n- 想十月看红叶，可以往北去立山黑部或北海道\n\n具体每年早晚差一两周，出发前看 **日本气象协会** 的红叶预报。") },
                                   { QStringLiteral("meta"), tp::tr("输入 %1 · 输出 %2 token").arg(QLocale().toString(1820), QLocale().toString(164)) + QStringLiteral(" · $0.005") }, { QStringLiteral("cost"), 0.0053 }, { QStringLiteral("cur"), QStringLiteral("USD") } });
    } else if (page == QLatin1String("ui-ai-tell")) {
        m_pending = QStringLiteral("帮我看看五天的东京行程怎么排");
    }
    emit changed(); emit keysChanged(); emit usageChanged(); emit messagesChanged(); emit tellChanged();
}

void AiController::setTestHooks(const TestHooks &h)
{
    m_hooks = h;
    if (!h.credPrefix.isEmpty()) { m_credPrefix = h.credPrefix; loadKeys(); }
    if (!h.chatsFile.isEmpty()) setChatsFile(h.chatsFile);
    emit changed();
}

} // namespace tp::app
