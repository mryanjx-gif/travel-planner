#include "aiproviders.h"

#include "translator.h"

#include <QJsonDocument>
#include <QRegularExpression>

namespace tp::ai {

namespace {

QList<Provider> makeTable()
{
    QList<Provider> t;
    // 价格（每百万 token）和对应表：claude/技术选型.md §11（2026-11-08 核实）
    {
        Provider p;
        p.id = QStringLiteral("claude"); p.name = QStringLiteral("Claude");
        p.kind = Kind::Anthropic; p.base = QStringLiteral("https://api.anthropic.com/v1"); p.currency = QStringLiteral("USD");
        p.signup = QStringLiteral("https://platform.claude.com/settings/keys");
        p.thinkStyle = QStringLiteral("claude"); p.keyPrefix = QStringLiteral("sk-ant-");
        p.chinaBlocked = true; p.searchPrice = 0.01;
        p.tiers[0] = { QStringLiteral("claude-haiku-5-5"), false, {} };
        p.tiers[1] = { QStringLiteral("claude-sonnet-5-5"), false, {} };
        p.tiers[2] = { QStringLiteral("claude-opus-5-5"), true, QStringLiteral("high") };
        p.prices = { { QStringLiteral("claude-haiku-5"), 0.10, 0.50, 0.01, 0.125, 100000, 5 },
                     { QStringLiteral("claude-sonnet-5"), 2, 10, 0.10, 2.5 },
                     { QStringLiteral("claude-opus-5"), 4, 20, 0.20, 5 },
                     { QStringLiteral("claude-fable"), 10, 50, 0.25, 12.5 },
                     { QStringLiteral("claude-haiku-4"), 1, 5, 0.10, 1.25 },
                     { QStringLiteral("claude-sonnet-4"), 3, 15, 0.30, 3.75 },
                     { QStringLiteral("claude-opus-4"), 5, 25, 0.50, 6.25 } };
        t << p;
    }
    {
        Provider p;
        p.id = QStringLiteral("deepseek"); p.name = QStringLiteral("DeepSeek");
        p.base = QStringLiteral("https://api.deepseek.com"); p.currency = QStringLiteral("CNY");
        p.signup = QStringLiteral("https://platform.deepseek.com/api_keys"); p.thinkStyle = QStringLiteral("deepseek");
        p.tiers[0] = { QStringLiteral("deepseek-flash"), false, {} };
        p.tiers[1] = { QStringLiteral("deepseek-flash"), true, QStringLiteral("low") };
        p.tiers[2] = { QStringLiteral("deepseek-v4-pro"), true, QStringLiteral("high") };
        // 高峰价（空闲时段半价；按高峰算，偏保守）
        p.prices = { { QStringLiteral("deepseek-v4-pro"), 9, 27, 0.30, 9 },
                     { QStringLiteral("deepseek"), 2, 8, 0.04, 2 } };
        t << p;
    }
    {
        Provider p;
        p.id = QStringLiteral("qwen"); p.name = QStringLiteral("通义千问");
        p.base = QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1"); p.currency = QStringLiteral("CNY");
        p.signup = QStringLiteral("https://bailian.console.aliyun.com/?tab=model#/api-key"); p.thinkStyle = QStringLiteral("qwen");
        p.maxTokens = 8192;
        p.tiers[0] = { QStringLiteral("qwen3.8-flash"), false, {} };
        p.tiers[1] = { QStringLiteral("qwen3.7-plus"), false, {} };
        p.tiers[2] = { QStringLiteral("qwen3.8-max"), true, {} };
        p.prices = { { QStringLiteral("qwen3.8-flash"), 0.8, 2.7 }, { QStringLiteral("qwen3.7-plus"), 2, 8 }, { QStringLiteral("qwen3.8-max"), 12, 36 },
                     { QStringLiteral("qwen-flash"), 0.15, 1.5 }, { QStringLiteral("qwen-plus"), 0.8, 8 }, { QStringLiteral("qwen-max"), 2.4, 9.6 } };
        t << p;
    }
    {
        Provider p;
        p.id = QStringLiteral("kimi"); p.name = QStringLiteral("Kimi");
        p.base = QStringLiteral("https://api.moonshot.cn/v1"); p.currency = QStringLiteral("CNY");
        p.signup = QStringLiteral("https://platform.kimi.com/console/api-keys"); p.thinkStyle = QStringLiteral("kimi");
        // kimi-k3 一直思考、关不掉，三档只改思考强度
        p.tiers[0] = { QStringLiteral("kimi-k3"), true, QStringLiteral("low") };
        p.tiers[1] = { QStringLiteral("kimi-k3"), true, QStringLiteral("high") };
        p.tiers[2] = { QStringLiteral("kimi-k3"), true, QStringLiteral("max") };
        p.prices = { { QStringLiteral("kimi-k3"), 20, 100, 2, 20 } };
        t << p;
    }
    {
        Provider p;
        p.id = QStringLiteral("doubao"); p.name = QStringLiteral("豆包");
        p.base = QStringLiteral("https://ark.cn-beijing.volces.com/api/v3"); p.currency = QStringLiteral("CNY");
        p.signup = QStringLiteral("https://console.volcengine.com/ark/region:ark+cn-beijing/apiKey"); p.thinkStyle = QStringLiteral("doubao");
        p.tiers[0] = { QStringLiteral("doubao-seed-2-1-lite-260915"), false, {} };
        p.tiers[1] = { QStringLiteral("doubao-seed-2-1-turbo-260628"), false, {} };
        p.tiers[2] = { QStringLiteral("doubao-seed-2-1-pro-260915"), true, QStringLiteral("high") };
        p.prices = { { QStringLiteral("doubao-seed-2-1-lite"), 0.8, 2.7, 0.16, 0.8 },
                     { QStringLiteral("doubao-seed-2-1-turbo"), 3, 15, 0.6, 3 },
                     { QStringLiteral("doubao-seed-2-1-pro"), 6, 30, 1.2, 6 } };
        t << p;
    }
    {
        Provider p;   // 自定义：地址、模型名、单价都由用户填（AiController 给）
        p.id = QStringLiteral("custom"); p.name = QStringLiteral("自定义");
        p.thinkStyle = QStringLiteral("none"); p.untested = true; p.maxTokens = 4096;
        t << p;
    }
    return t;
}

QJsonObject msg(const QString &role, const QString &text) { return { { QStringLiteral("role"), role }, { QStringLiteral("content"), text } }; }

// 1.2.0 给一条消息的最后一个能带标记的块打 cache_control（思考块、空文字块不能带）。字符串内容先改成一个 text 块
void markCache(QJsonObject &m)
{
    const QJsonObject cc{ { QStringLiteral("type"), QStringLiteral("ephemeral") } };
    const QJsonValue c = m.value(QStringLiteral("content"));
    if (c.isString()) {
        if (c.toString().isEmpty()) return;
        m.insert(QStringLiteral("content"), QJsonArray{ QJsonObject{ { QStringLiteral("type"), QStringLiteral("text") }, { QStringLiteral("text"), c.toString() }, { QStringLiteral("cache_control"), cc } } });
        return;
    }
    QJsonArray a = c.toArray();
    for (qsizetype i = a.size() - 1; i >= 0; --i) {
        QJsonObject b = a.at(i).toObject();
        const QString t = b.value(QStringLiteral("type")).toString();
        if (t == QLatin1String("thinking") || t == QLatin1String("redacted_thinking") || (t == QLatin1String("text") && b.value(QStringLiteral("text")).toString().isEmpty())) continue;
        b.insert(QStringLiteral("cache_control"), cc);
        a.replace(i, b);
        m.insert(QStringLiteral("content"), a);
        return;
    }
}

QUrl joinUrl(const QString &base, const QString &path)
{
    QString b = base.trimmed();
    while (b.endsWith(u'/')) b.chop(1);
    return QUrl(b + path);
}

} // namespace

const QList<Provider> &providers()
{
    static const QList<Provider> t = makeTable();
    return t;
}

const Provider *provider(const QString &id)
{
    for (const Provider &p : providers())
        if (p.id == id) return &p;
    return nullptr;
}

int tierIndex(const QString &key) { return key == QLatin1String("fast") ? 0 : key == QLatin1String("careful") ? 2 : 1; }
QString tierKey(int i) { return i == 0 ? QStringLiteral("fast") : i == 2 ? QStringLiteral("careful") : QStringLiteral("balanced"); }

QString checkKey(const QString &providerId, const QString &key)
{
    const QString k = key.trimmed();
    if (k.isEmpty()) return tp::tr("先粘贴钥匙");
    static const QRegularExpression space(QStringLiteral("\\s"));
    if (k.contains(space)) return tp::tr("钥匙里不能有空格或换行");
    if (providerId == QLatin1String("claude")) {
        static const QRegularExpression re(QStringLiteral("^sk-ant-[A-Za-z0-9_\\-]{20,}$"));
        if (!re.match(k).hasMatch()) return tp::tr("格式不对：Claude 的 API 钥匙以 sk-ant- 开头（在 Claude 开发者平台申请，和 Claude 订阅分开）");
    }
    return {};
}

const Price *priceFor(const Provider &p, const QString &model)
{
    for (const Price &x : p.prices)
        if (model.startsWith(x.prefix)) return &x;
    return nullptr;
}

double cost(const Price *pr, double searchPrice, const Usage &u)
{
    if (!pr || pr->in < 0 || pr->out < 0) return -1;
    const double mul = pr->bigOver > 0 && u.inTotal() > pr->bigOver ? pr->bigMul : 1;
    const double cr = (pr->cacheRead >= 0 ? pr->cacheRead : pr->in * 0.1) * mul;
    const double cw = (pr->cacheWrite >= 0 ? pr->cacheWrite : pr->in * 1.25) * mul;
    return (u.input * pr->in * mul + u.cacheRead * cr + u.cacheWrite * cw + u.output * pr->out * mul) / 1e6 + u.searches * searchPrice;
}

double estimate(const Price *pr, bool think)
{
    Usage u;
    u.input = 8000;
    u.output = think ? 4000 : 1500;
    return cost(pr, 0, u);
}

Request buildChat(const Provider &p, const QString &base, const QString &key, const QString &model, bool think, const QString &effort,
                  const QString &system, const QJsonArray &messages, const ChatExtra &x)
{
    auto pass = [](const QJsonValue &v) {   // 已经拼好的消息原样发，否则 {role, text} → {role, content}
        const QJsonObject o = v.toObject();
        return o.contains(QStringLiteral("content")) || o.contains(QStringLiteral("tool_calls")) ? o : msg(o.value(QStringLiteral("role")).toString(), o.value(QStringLiteral("text")).toString());
    };
    Request r;
    const QString b = base.isEmpty() ? p.base : base;
    QJsonObject body{ { QStringLiteral("model"), model }, { QStringLiteral("max_tokens"), x.maxTokens > 0 ? x.maxTokens : p.maxTokens }, { QStringLiteral("stream"), true } };
    if (!x.toolChoice.isUndefined() && !x.toolChoice.isNull()) body.insert(QStringLiteral("tool_choice"), x.toolChoice);
    if (p.kind == Kind::Anthropic) {
        r.url = joinUrl(b, QStringLiteral("/messages"));
        r.headers = { { "x-api-key", key.toUtf8() }, { "anthropic-version", "2023-06-01" } };
        // 规则放 system 并打缓存标记（同一对话后面几轮更省；0.13.1 加计划简表后效果才明显）
        QJsonArray sys{ QJsonObject{ { QStringLiteral("type"), QStringLiteral("text") }, { QStringLiteral("text"), system },
                                     { QStringLiteral("cache_control"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("ephemeral") } } } } };
        if (!x.systemTail.isEmpty()) sys.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("text") }, { QStringLiteral("text"), x.systemTail } });
        body.insert(QStringLiteral("system"), sys);
        // 1.2.0 缓存整段对话（最多 4 个断点：系统 1 + 这一问的开头 1 + 最后一条 1）。前缀不变的部分下一次只按约一成收费
        QJsonArray ms;
        for (const auto &v : messages) ms.append(pass(v));
        for (const qsizetype i : { qsizetype(x.cacheAt), ms.size() - 1 }) {
            if (i < 0 || i >= ms.size()) continue;
            QJsonObject m = ms.at(i).toObject();
            markCache(m);
            ms.replace(i, m);
        }
        body.insert(QStringLiteral("messages"), ms);
        if (!x.tools.isEmpty()) body.insert(QStringLiteral("tools"), x.tools);
        if (think) {   // 5.x 模型只认 adaptive（技术选型.md §11）
            body.insert(QStringLiteral("thinking"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("adaptive") } });
            if (!effort.isEmpty()) body.insert(QStringLiteral("output_config"), QJsonObject{ { QStringLiteral("effort"), effort } });
        }
    } else {
        r.url = joinUrl(b, QStringLiteral("/chat/completions"));
        r.headers = { { "Authorization", "Bearer " + key.toUtf8() } };
        QJsonArray ms{ msg(QStringLiteral("system"), x.systemTail.isEmpty() ? system : system + QStringLiteral("\n\n") + x.systemTail) };
        for (const auto &v : messages) ms.append(pass(v));
        body.insert(QStringLiteral("messages"), ms);
        if (!x.tools.isEmpty()) body.insert(QStringLiteral("tools"), x.tools);
        if (x.search) body.insert(QStringLiteral("enable_search"), true);
        body.insert(QStringLiteral("stream_options"), QJsonObject{ { QStringLiteral("include_usage"), true } });
        const QString &s = p.thinkStyle;
        const QJsonObject onOff{ { QStringLiteral("type"), think ? QStringLiteral("enabled") : QStringLiteral("disabled") } };
        if (s == QLatin1String("deepseek") || s == QLatin1String("doubao")) {
            body.insert(QStringLiteral("thinking"), onOff);
            if (think && !effort.isEmpty()) body.insert(QStringLiteral("reasoning_effort"), effort);
        } else if (s == QLatin1String("qwen")) {
            body.insert(QStringLiteral("enable_thinking"), think);
        } else if (s == QLatin1String("kimi")) {
            if (!effort.isEmpty()) body.insert(QStringLiteral("reasoning_effort"), effort);
        }
    }
    r.headers.append(std::pair<QByteArray, QByteArray>("content-type", "application/json"));
    r.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return r;
}

Request buildModels(const Provider &p, const QString &base, const QString &key)
{
    Request r;
    const QString b = base.isEmpty() ? p.base : base;
    if (p.kind == Kind::Anthropic) {
        r.url = joinUrl(b, QStringLiteral("/models?limit=100"));
        r.headers = { { "x-api-key", key.toUtf8() }, { "anthropic-version", "2023-06-01" } };
    } else {
        r.url = joinUrl(b, QStringLiteral("/models"));
        r.headers = { { "Authorization", "Bearer " + key.toUtf8() } };
    }
    return r;
}

QStringList parseModels(const QByteArray &body)
{
    QStringList out;
    const QJsonArray a = QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray();
    for (const auto &v : a) {
        const QString id = v[QStringLiteral("id")].toString();
        if (!id.isEmpty() && !out.contains(id)) out << id;
    }
    return out;
}

} // namespace tp::ai
