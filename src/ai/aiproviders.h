// AI 服务商（0.13.0，I-01 / I-02 / I-08；PO 2026-11-08 选五家 + 自定义，阶段7设计预案.md §〇）。
// 一张表：接法、地址、币种、钥匙格式、三档对应（快 / 均衡 / 细致 → 模型 + 是否思考）、价格。
// 核实来源和日期见 claude/技术选型.md §11；价格变了改 aiproviders.cpp 的表。
// 这里只有纯计算（拼请求、算钱、校验钥匙），不联网，单元测试 tst_ai 直接测。
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>
#include <utility>

namespace tp::ai {

enum class Kind { Anthropic, OpenAi };

// 每百万 token 的价格（服务商自己的币种）。prefix：模型名以它开头就用这一行
struct Price {
    QString prefix;
    double in = 0, out = 0;
    double cacheRead = -1, cacheWrite = -1;   // < 0：按输入价的 10% / 125% 算
    qint64 bigOver = 0;                       // 提示超过这么多 token 时全部乘 bigMul（Claude Haiku 5.5）
    double bigMul = 1;
};

struct Tier {
    QString model;
    bool think = false;
    QString effort;   // 思考强度（各家自己的取值）
};

struct Provider {
    QString id, name;
    Kind kind = Kind::OpenAi;
    QString base;         // 接口地址（结尾不带 /）
    QString currency;     // USD / CNY
    QString signup;       // 申请钥匙的页面
    QString thinkStyle;   // claude / deepseek / qwen / kimi / doubao / none
    QString keyPrefix;    // 钥匙开头（只有 Claude 校验）
    bool untested = false;
    bool chinaBlocked = false;   // 中国大陆用不了
    int maxTokens = 16000;
    double searchPrice = 0;      // 每次联网搜索（服务商币种；0 = 不知道或没有）
    Tier tiers[3];
    QList<Price> prices;
};

// 用量（两种接法统一成这几项）。input 不含缓存读写
struct Usage {
    qint64 input = 0, output = 0, cacheRead = 0, cacheWrite = 0;
    int searches = 0;
    bool any() const { return input || output || cacheRead || cacheWrite || searches; }
    qint64 inTotal() const { return input + cacheRead + cacheWrite; }
};

const QList<Provider> &providers();
const Provider *provider(const QString &id);   // 找不到返回 nullptr
int tierIndex(const QString &key);              // fast 0 / balanced 1 / careful 2（认不出 = 1）
QString tierKey(int i);

// 钥匙粗校验：空 = 可以；否则是给用户看的原因（已翻译）
QString checkKey(const QString &providerId, const QString &key);

// 价格：custom 用调用方给的（in / out < 0 = 没填，只记 token）
const Price *priceFor(const Provider &p, const QString &model);
// 算钱（服务商币种）。没有价格时返回 -1
double cost(const Price *price, double searchPrice, const Usage &u);
// 每轮估价（服务商币种）：约 8k 输入、不思考 1.5k / 思考 4k 输出。没有价格时 -1
double estimate(const Price *price, bool think);

// 一次请求要发的东西
struct Request {
    QUrl url;
    QList<std::pair<QByteArray, QByteArray>> headers;
    QByteArray body;
};
// 0.13.1：system 的后半段（常变的：时间、当前计划；Claude 只给前半段打缓存标记）、工具（已按接法转好）、千问联网
struct ChatExtra {
    QString systemTail;
    QJsonArray tools;
    bool search = false;   // 千问 enable_search
};
// messages：[{role: "user" / "assistant", text}]，或已经按接法拼好的消息（带 content / tool_calls、role = tool）原样发；base 为空用服务商默认地址
Request buildChat(const Provider &p, const QString &base, const QString &key, const QString &model, bool think, const QString &effort,
                  const QString &system, const QJsonArray &messages, const ChatExtra &x = {});
Request buildModels(const Provider &p, const QString &base, const QString &key);
// 模型列表回复 → [id]
QStringList parseModels(const QByteArray &body);

} // namespace tp::ai
