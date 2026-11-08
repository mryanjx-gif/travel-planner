// AI 的纯计算（0.13.0）：SSE 拆包、两种接法的流式事件、用量 → 钱、三档对应表、拼请求、钥匙校验、mdLite。
#include "aiproviders.h"
#include "aistream.h"
#include "aitools.h"
#include "jsonio.h"

#include <QJsonDocument>
#include <QtTest>

using namespace tp::ai;

class TstAi : public QObject
{
    Q_OBJECT
private slots:
    void sseSplitsAcrossChunks()
    {
        SseParser p;
        QCOMPARE(p.feed("event: ping\r\ndata: {\"a\":"), QStringList());
        QCOMPARE(p.feed("1}\r\n\r\ndata: x\n"), QStringList{ QStringLiteral("{\"a\":1}") });
        QCOMPARE(p.feed("\n: comment\n\n"), QStringList{ QStringLiteral("x") });
    }

    void anthropicStream()
    {
        StreamState s;
        const char *evs[] = {
            R"({"type":"message_start","message":{"usage":{"input_tokens":100,"cache_read_input_tokens":50,"cache_creation_input_tokens":10,"output_tokens":1}}})",
            R"({"type":"content_block_start","index":0,"content_block":{"type":"thinking","thinking":""}})",
            R"({"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"想一想"}})",
            R"({"type":"content_block_delta","index":0,"delta":{"type":"signature_delta","signature":"sig"}})",
            R"({"type":"content_block_start","index":1,"content_block":{"type":"text","text":""}})",
            R"({"type":"content_block_delta","index":1,"delta":{"type":"text_delta","text":"你好"}})",
            R"({"type":"content_block_delta","index":1,"delta":{"type":"citations_delta","citation":{"url":"https://a.example","title":"A"}}})",
            R"({"type":"content_block_start","index":2,"content_block":{"type":"tool_use","name":"x"}})",
            "{\"type\":\"content_block_delta\",\"index\":2,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"k\\\":\"}}",
            "{\"type\":\"content_block_delta\",\"index\":2,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"1}\"}}",
            R"({"type":"message_delta","delta":{"stop_reason":"max_tokens"},"usage":{"output_tokens":42,"server_tool_use":{"web_search_requests":2}}})",
            R"({"type":"message_stop"})" };
        for (const char *e : evs) QVERIFY(s.feedAnthropic(QString::fromUtf8(e)));
        QCOMPARE(s.text, QStringLiteral("你好"));
        QCOMPARE(s.thinking, QStringLiteral("想一想"));
        QCOMPARE(s.signature, QStringLiteral("sig"));
        QCOMPARE(s.citations, QStringList{ QStringLiteral("https://a.example") });
        QCOMPARE(s.toolJson.value(2), QStringLiteral("{\"k\":1}"));
        QCOMPARE(s.usage.input, qint64(100)); QCOMPARE(s.usage.cacheRead, qint64(50)); QCOMPARE(s.usage.cacheWrite, qint64(10));
        QCOMPARE(s.usage.output, qint64(42)); QCOMPARE(s.usage.searches, 2);
        QVERIFY(s.truncated());
        QVERIFY(s.done);
        StreamState e;
        e.feedAnthropic(QStringLiteral(R"({"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})"));
        QCOMPARE(e.error, QStringLiteral("Overloaded"));
    }

    void openAiStream()
    {
        StreamState s;
        QVERIFY(s.feedOpenAi(QStringLiteral(R"({"choices":[{"delta":{"reasoning_content":"嗯"}}]})")));
        QVERIFY(s.feedOpenAi(QStringLiteral(R"({"choices":[{"delta":{"content":"答"}}]})")));
        QVERIFY(s.feedOpenAi(QStringLiteral(R"({"choices":[{"delta":{"content":"案"},"finish_reason":"length"}]})")));
        QVERIFY(s.feedOpenAi(QStringLiteral(R"({"choices":[],"usage":{"prompt_tokens":1000,"completion_tokens":300,"prompt_cache_hit_tokens":400}})")));
        QVERIFY(s.feedOpenAi(QStringLiteral("[DONE]")));
        QCOMPARE(s.text, QStringLiteral("答案"));
        QCOMPARE(s.thinking, QStringLiteral("嗯"));
        QVERIFY(s.truncated());
        QCOMPARE(s.usage.input, qint64(600)); QCOMPARE(s.usage.cacheRead, qint64(400)); QCOMPARE(s.usage.output, qint64(300));
        QVERIFY(s.done);
        StreamState q;   // 千问 / OpenAI 的缓存写法；Kimi 把 usage 放在 choices[0]
        q.feedOpenAi(QStringLiteral(R"({"choices":[{"delta":{},"finish_reason":"stop","usage":{"prompt_tokens":50,"completion_tokens":5,"prompt_tokens_details":{"cached_tokens":20}}}]})"));
        QCOMPARE(q.usage.cacheRead, qint64(20)); QCOMPARE(q.usage.input, qint64(30)); QCOMPARE(q.stop, QStringLiteral("end"));
        QVERIFY(!q.feedOpenAi(QStringLiteral("不是 JSON")));
    }

    void costAndConversion()
    {
        const Provider *c = provider(QStringLiteral("claude"));
        QVERIFY(c);
        Usage u; u.input = 1000000; u.output = 1000000;
        QCOMPARE(cost(priceFor(*c, QStringLiteral("claude-sonnet-5-5")), c->searchPrice, u), 12.0);
        Usage w; w.cacheRead = 1000000; w.cacheWrite = 1000000; w.searches = 3;
        QVERIFY(qAbs(cost(priceFor(*c, QStringLiteral("claude-sonnet-5-5")), c->searchPrice, w) - (0.10 + 2.5 + 0.03)) < 1e-9);
        // Haiku 5.5：提示超过 10 万 token 全部 ×5
        Usage big; big.input = 200000; big.output = 1000;
        QVERIFY(qAbs(cost(priceFor(*c, QStringLiteral("claude-haiku-5-5")), 0, big) - (200000 * 0.5 + 1000 * 2.5) / 1e6) < 1e-12);
        // 没有价格（自定义没填）
        QCOMPARE(cost(nullptr, 0, u), -1.0);
        // DeepSeek：缓存命中按表里的价
        const Provider *d = provider(QStringLiteral("deepseek"));
        Usage ds; ds.input = 600; ds.cacheRead = 400; ds.output = 300;
        QVERIFY(qAbs(cost(priceFor(*d, QStringLiteral("deepseek-flash")), 0, ds) - (600 * 2 + 400 * 0.04 + 300 * 8) / 1e6) < 1e-12);
        // 没写缓存价的按输入的 10%
        const Provider *q = provider(QStringLiteral("qwen"));
        Usage qs; qs.cacheRead = 1000000;
        QVERIFY(qAbs(cost(priceFor(*q, QStringLiteral("qwen3.8-max")), 0, qs) - 1.2) < 1e-9);
    }

    void tierTable()
    {
        // 五家 + 自定义；每家三档都有模型和价格，档位估价从快到细致不降
        QCOMPARE(providers().size(), 6);
        for (const Provider &p : providers()) {
            if (p.id == QLatin1String("custom")) { QVERIFY(p.untested); continue; }
            double last = -1;
            for (const Tier &t : p.tiers) {
                QVERIFY2(!t.model.isEmpty(), qPrintable(p.id));
                const Price *pr = priceFor(p, t.model);
                QVERIFY2(pr, qPrintable(p.id + QLatin1Char(' ') + t.model));
                const double e = estimate(pr, t.think);
                QVERIFY(e > 0 && e >= last);
                last = e;
            }
        }
        QCOMPARE(provider(QStringLiteral("claude"))->tiers[2].think, true);
        QCOMPARE(provider(QStringLiteral("claude"))->tiers[0].model, QStringLiteral("claude-haiku-5-5"));
        QCOMPARE(tierIndex(QStringLiteral("fast")), 0);
        QCOMPARE(tierIndex(QStringLiteral("乱写")), 1);
        QCOMPARE(tierKey(2), QStringLiteral("careful"));
    }

    void requests()
    {
        const QJsonArray msgs{ QJsonObject{ { "role", "user" }, { "text", "你好" } } };
        const Request a = buildChat(*provider(QStringLiteral("claude")), QString(), QStringLiteral("sk-ant-x"), QStringLiteral("claude-opus-5-5"), true, QStringLiteral("high"), QStringLiteral("规则"), msgs);
        QCOMPARE(a.url.toString(), QStringLiteral("https://api.anthropic.com/v1/messages"));
        const QJsonObject ab = QJsonDocument::fromJson(a.body).object();
        QCOMPARE(ab["thinking"].toObject()["type"].toString(), QStringLiteral("adaptive"));
        QCOMPARE(ab["output_config"].toObject()["effort"].toString(), QStringLiteral("high"));
        QCOMPARE(ab["system"].toArray()[0].toObject()["text"].toString(), QStringLiteral("规则"));
        QCOMPARE(ab["messages"].toArray()[0].toObject()["content"].toString(), QStringLiteral("你好"));
        QVERIFY(ab["stream"].toBool());
        bool hasKey = false;
        for (const auto &h : a.headers) if (h.first == "x-api-key" && h.second == "sk-ant-x") hasKey = true;
        QVERIFY(hasKey);

        const Request d = buildChat(*provider(QStringLiteral("deepseek")), QString(), QStringLiteral("k"), QStringLiteral("deepseek-flash"), false, QString(), QStringLiteral("规则"), msgs);
        QCOMPARE(d.url.toString(), QStringLiteral("https://api.deepseek.com/chat/completions"));
        const QJsonObject db = QJsonDocument::fromJson(d.body).object();
        QCOMPARE(db["thinking"].toObject()["type"].toString(), QStringLiteral("disabled"));
        QCOMPARE(db["messages"].toArray()[0].toObject()["role"].toString(), QStringLiteral("system"));
        QVERIFY(db["stream_options"].toObject()["include_usage"].toBool());

        const QJsonObject qb = QJsonDocument::fromJson(buildChat(*provider(QStringLiteral("qwen")), QString(), QStringLiteral("k"), QStringLiteral("qwen3.8-max"), true, QString(), QString(), msgs).body).object();
        QCOMPARE(qb["enable_thinking"].toBool(), true);
        QCOMPARE(qb["max_tokens"].toInt(), 8192);
        const QJsonObject kb = QJsonDocument::fromJson(buildChat(*provider(QStringLiteral("kimi")), QString(), QStringLiteral("k"), QStringLiteral("kimi-k3"), true, QStringLiteral("low"), QString(), msgs).body).object();
        QCOMPARE(kb["reasoning_effort"].toString(), QStringLiteral("low"));
        QVERIFY(!kb.contains("thinking"));
        // 自定义地址结尾的 / 不重复
        const Request c = buildChat(*provider(QStringLiteral("custom")), QStringLiteral("http://127.0.0.1:5/v1/"), QStringLiteral("k"), QStringLiteral("m"), true, QString(), QString(), msgs);
        QCOMPARE(c.url.toString(), QStringLiteral("http://127.0.0.1:5/v1/chat/completions"));
        QVERIFY(!QJsonDocument::fromJson(c.body).object().contains("thinking"));

        QCOMPARE(buildModels(*provider(QStringLiteral("claude")), QString(), QStringLiteral("k")).url.toString(), QStringLiteral("https://api.anthropic.com/v1/models?limit=100"));
        QCOMPARE(parseModels(R"({"data":[{"id":"a"},{"id":"b"},{"id":"a"}]})"), (QStringList{ "a", "b" }));
    }

    void keys()
    {
        QVERIFY(checkKey("claude", "sk-ant-api03-abcdefghijklmnopqrstuvwxyz").isEmpty());
        QVERIFY(!checkKey("claude", "sk-proj-abcdefghijklmnopqrstuvwxyz").isEmpty());
        QVERIFY(!checkKey("deepseek", "").isEmpty());
        QVERIFY(!checkKey("deepseek", "sk-12 34").isEmpty());
        QVERIFY(checkKey("deepseek", "  sk-1234  ").isEmpty());
    }

    void markdown()
    {
        QCOMPARE(mdLite(QStringLiteral("## 标题\n- 一\n- **二**\n\n1. 甲\n---\n<b>x</b>")),
                 QStringLiteral("<p><b>标题</b></p><ul><li>一</li><li><b>二</b></li></ul><ol><li>甲</li></ol><p>&lt;b&gt;x&lt;/b&gt;</p>"));
        QCOMPARE(mdLite(QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |")), QStringLiteral("<p>a · b</p><p>1 · 2</p>"));
    }

    void httpErrors()
    {
        QVERIFY(httpError(401, R"({"error":{"message":"invalid x-api-key"}})").contains(QStringLiteral("invalid x-api-key")));
        QVERIFY(httpError(500, R"({"error":{"message":"boom"}})").contains(QStringLiteral("HTTP 500")));
    }
    // 0.13.1：工具定义两种形状、计划上下文去掉什么、预览认领、应用（日程按认领好的那条改，删前一条不指错）
    void toolsAndApply()
    {
        using namespace tp::ai;
        QCOMPARE(tools::anthropicTools(true).size(), 4);
        QCOMPARE(tools::openAiTools(QStringLiteral("deepseek"), true).size(), 3);
        QCOMPARE(tools::openAiTools(QStringLiteral("kimi"), true).last().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("builtin_function"));
        QVERIFY(tools::canSearch(QStringLiteral("qwen")) && !tools::canSearch(QStringLiteral("doubao")));
        tp::json::Value root = tp::json::parse(QString::fromUtf8(R"({"plans":[{"id":"p1","name":"T","cover":"x","localEmergency":[{"label":"a"}],"expenses":[{"amount":1}],
            "bookings":[{"id":"b1","kind":"hotel","ref":"R9","cost":3}],
            "stops":[{"id":"s1","name":"A","lat":1,"lng":2,"schedule":[{"text":"x1"},{"text":"x2"}],"leg":{"mode":"drive","path":"pp","key":"k"}},{"id":"s2","name":"B","lat":3,"lng":4,"schedule":[]}]}],"photos":[]})"));
        const QString ctx = tools::planForAi(root, root[QStringLiteral("plans")].asArray().front(), true);
        QVERIFY(!ctx.contains(QStringLiteral("cover")) && !ctx.contains(QStringLiteral("localEmergency")) && !ctx.contains(QStringLiteral("expenses")) && !ctx.contains(QStringLiteral("R9")) && !ctx.contains(QStringLiteral("pp")));
        QVERIFY(ctx.contains(QStringLiteral("\"legMode\":\"drive\"")) && ctx.contains(QStringLiteral("\"index\":2")) && ctx.contains(QStringLiteral("\"n\":2")));
        const QJsonObject input = QJsonDocument::fromJson(R"({"summary":"s","changes":[{"op":"remove_schedule","stop_id":"s1","n":1},{"op":"update_schedule","stop_id":"s1","n":2,"text":"y2"},
            {"op":"remove_stop","stop_id":"s2"},{"op":"update_stop","stop_id":"nope"},{"op":"bogus"}]})").object();
        const QJsonObject P = tools::prepare(root, input, QStringLiteral("p1"), QStringLiteral("t1"), false);
        QCOMPARE(P.value(QStringLiteral("changes")).toArray().size(), 4);   // 认不出的 op 去掉
        QVERIFY(P.value(QStringLiteral("changes")).toArray().at(3).toObject().contains(QStringLiteral("bad")));
        const tools::Applied a = tools::apply(root, P, [](const QString &pre) { return tools::uid(pre); });
        QCOMPARE(a.n, 3);
        const tp::json::Value &s1 = root[QStringLiteral("plans")].asArray().front()[QStringLiteral("stops")].asArray().front();
        QCOMPARE(tp::json::stringify(s1[QStringLiteral("schedule")], 0), QStringLiteral("[{\"text\":\"y2\"}]"));
        QVERIFY(s1[QStringLiteral("leg")][QStringLiteral("path")].isNull());   // 后一站删了，折线清掉
        QVERIFY(a.needRoute);
        QVERIFY(tools::resultText(P, a).contains(QStringLiteral("用户应用了 3 项")));
    }
};

QTEST_GUILESS_MAIN(TstAi)
#include "tst_ai.moc"
