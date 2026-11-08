// 0.13.0 AI 助手的自检：本机假服务器（QTcpServer）模拟 Anthropic 和 OpenAI 兼容两种流式回答，不花钱、不联网。
// 测：钥匙（格式、凭据管理器读回、清除）、没钥匙 / 离线 / 到上限拦住、第一次发给某家的提示、流式回答和计费、
// 截断提示、停止、90 秒没字节（缩短）、150 秒没事件的保险（缩短）、401、模型列表（拉到 / 拉不到）、80% 提醒、当场提高上限、
// 历史（新对话、打开旧的）、面板上的 Enter / Shift+Enter / 输入法组字。钥匙用单独的目标名，跑完删掉；设置跑完还原。
#include "selftest_util.h"

#include "aicontroller.h"
#include "appcontroller.h"
#include "credentials.h"
#include "datastore.h"
#include "geocoder.h"
#include "planedit.h"
#include "jsonio.h"
#include "netclient.h"
#include "plancontroller.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace tp::app::selftest {

namespace {

QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findItem(c, name)) return f;
    return nullptr;
}

// 假服务器：按请求里用户最后一句话里的暗号决定怎么回
class FakeAi : public QObject
{
public:
    QTcpServer server;
    QByteArray lastPath, lastBody, lastAuth, lastKey;
    int requests = 0;
    QList<QByteArray> script;   // 0.13.1：排好的流式回答（每个请求取一个）
    QList<QByteArray> bodies;   // 收到的请求体
    QList<QPointer<QTcpSocket>> held;   // 停止、卡住时一直不关的连接

    FakeAi() { server.listen(QHostAddress::LocalHost, 0); connect(&server, &QTcpServer::newConnection, this, [this] { while (auto *s = server.nextPendingConnection()) serve(s); }); }
    QString base(const char *path) const { return QStringLiteral("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(QLatin1String(path)); }

private:
    void serve(QTcpSocket *s)
    {
        auto *buf = new QByteArray;
        connect(s, &QTcpSocket::disconnected, s, [s, buf] { delete buf; s->deleteLater(); });
        connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
            *buf += s->readAll();
            const qsizetype he = buf->indexOf("\r\n\r\n");
            if (he < 0) return;
            const QByteArray head = buf->left(he);
            qsizetype len = 0;
            for (const QByteArray &l : head.split('\n')) if (l.toLower().startsWith("content-length:")) len = l.mid(15).trimmed().toLongLong();
            if (buf->size() < he + 4 + len) return;
            const QByteArray body = buf->mid(he + 4, len);
            const QList<QByteArray> lines = head.split('\n');
            const QByteArray path = lines.value(0).split(' ').value(1);
            lastAuth.clear(); lastKey.clear();
            for (const QByteArray &l : lines) {
                if (l.toLower().startsWith("authorization:")) lastAuth = l.mid(14).trimmed();
                if (l.toLower().startsWith("x-api-key:")) lastKey = l.mid(10).trimmed();
            }
            buf->clear();
            reply(s, path, body);
        });
    }
    static void sendHead(QTcpSocket *s, int status, const char *type)
    {
        s->write(QByteArray("HTTP/1.1 ") + QByteArray::number(status) + (status == 200 ? " OK" : " Error") + "\r\nContent-Type: " + type + "\r\nConnection: close\r\n\r\n");
    }
    static void sendJson(QTcpSocket *s, int status, const QByteArray &json)
    {
        s->write(QByteArray("HTTP/1.1 ") + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(json.size()) + "\r\nConnection: close\r\n\r\n" + json);
        s->disconnectFromHost();
    }
    static QByteArray ev(const QByteArray &json) { return "data: " + json + "\n\n"; }
    void reply(QTcpSocket *s, const QByteArray &path, const QByteArray &body)
    {
        ++requests;
        lastPath = path; lastBody = body;
        bodies << body;
        if (!script.isEmpty() && !path.contains("/models")) {
            sendHead(s, 200, "text/event-stream");
            s->write(script.takeFirst());
            s->flush();
            s->disconnectFromHost();
            return;
        }
        if (path.endsWith("/v1/models?limit=100")) { sendJson(s, 200, R"({"data":[{"id":"claude-test-1"},{"id":"claude-test-2"}]})"); return; }
        if (path.endsWith("/oa/models")) { sendJson(s, 404, R"({"error":{"message":"no list"}})"); return; }
        const QJsonArray msgs = QJsonDocument::fromJson(body).object().value(QStringLiteral("messages")).toArray();
        QString last = msgs.isEmpty() ? QString() : msgs.last().toObject().value(QStringLiteral("content")).toString();
        if (!msgs.isEmpty()) for (const auto &b : msgs.last().toObject().value(QStringLiteral("content")).toArray()) last += b.toObject().value(QStringLiteral("text")).toString();
        if (last.contains(QLatin1String("E401"))) { sendJson(s, 401, R"({"error":{"type":"authentication_error","message":"invalid x-api-key"}})"); return; }
        sendHead(s, 200, "text/event-stream");
        if (path.endsWith("/v1/messages")) {
            s->write(ev(R"({"type":"message_start","message":{"usage":{"input_tokens":1000,"cache_read_input_tokens":500,"output_tokens":1}}})"));
            s->write(ev(R"({"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}})"));
            s->write(ev(R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"你好，"}})"));
            if (last.contains(QLatin1String("STOP")) || last.contains(QLatin1String("STALL"))) { s->flush(); held << s; return; }
            if (last.contains(QLatin1String("PING"))) {   // 只发 ping（有字节、没有事件）：靠 150 秒保险停下
                s->flush(); held << s;
                auto *t = new QTimer(s);
                connect(t, &QTimer::timeout, s, [s] { s->write(": ping\n\n"); s->flush(); });
                t->start(200);
                return;
            }
            s->write(ev(R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"测试"}})"));
            const bool trunc = last.contains(QLatin1String("TRUNC"));
            s->write(ev(QByteArray(R"({"type":"message_delta","delta":{"stop_reason":")") + (trunc ? "max_tokens" : "end_turn") + R"("},"usage":{"output_tokens":200}})"));
            s->write(ev(R"({"type":"message_stop"})"));
        } else {
            s->write(ev(R"({"choices":[{"delta":{"reasoning_content":"推理"}}]})"));
            s->write(ev(R"({"choices":[{"delta":{"content":"答"}}]})"));
            s->write(ev(R"({"choices":[{"delta":{"content":"案"},"finish_reason":"stop"}]})"));
            s->write(ev(R"({"choices":[],"usage":{"prompt_tokens":2000,"completion_tokens":100,"prompt_cache_hit_tokens":1000}})"));
            s->write("data: [DONE]\n\n");
        }
        s->flush();
        s->disconnectFromHost();
    }
};

QJsonObject lastMsg(AiController *ai) { const QVariantList m = ai->messages(); return m.isEmpty() ? QJsonObject() : QJsonObject::fromVariantMap(m.last().toMap()); }
QJsonObject lastOf(AiController *ai, const char *role)
{
    const QVariantList m = ai->messages();
    for (qsizetype i = m.size() - 1; i >= 0; --i) if (m.at(i).toMap().value(QStringLiteral("role")) == QLatin1String(role)) return QJsonObject::fromVariantMap(m.at(i).toMap());
    return {};
}


// ---------------- 0.13.1 改计划（工具、预览、应用、撤销、时间预算、续轮上限、联网次数）----------------

QByteArray sse(const QJsonObject &o) { return "data: " + QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n\n"; }
QJsonObject J(std::initializer_list<std::pair<QString, QJsonValue>> l) { QJsonObject o; for (const auto &p : l) o.insert(p.first, p.second); return o; }

// Anthropic：可选的思考块（含 signature）+ 一个工具调用（参数分两段）+ 可选的文字
QByteArray anthTool(const QString &id, const QString &name, const QJsonObject &input, bool thinking, const QString &text = {})
{
    QByteArray b = sse(J({ { "type", "message_start" }, { "message", J({ { "usage", J({ { "input_tokens", 3000 }, { "output_tokens", 1 } }) } }) } }));
    int i = 0;
    if (thinking) {
        b += sse(J({ { "type", "content_block_start" }, { "index", i }, { "content_block", J({ { "type", "thinking" }, { "thinking", "" }, { "signature", "" } }) } }));
        b += sse(J({ { "type", "content_block_delta" }, { "index", i }, { "delta", J({ { "type", "thinking_delta" }, { "thinking", QStringLiteral("想一想") } }) } }));
        b += sse(J({ { "type", "content_block_delta" }, { "index", i }, { "delta", J({ { "type", "signature_delta" }, { "signature", "sig-abc" } }) } }));
        b += sse(J({ { "type", "content_block_stop" }, { "index", i } }));
        ++i;
    }
    if (!text.isEmpty()) {
        b += sse(J({ { "type", "content_block_start" }, { "index", i }, { "content_block", J({ { "type", "text" }, { "text", "" } }) } }));
        b += sse(J({ { "type", "content_block_delta" }, { "index", i }, { "delta", J({ { "type", "text_delta" }, { "text", text } }) } }));
        b += sse(J({ { "type", "content_block_stop" }, { "index", i } }));
        ++i;
    }
    if (!name.isEmpty()) {
        const QString js = QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));
        b += sse(J({ { "type", "content_block_start" }, { "index", i }, { "content_block", J({ { "type", "tool_use" }, { "id", id }, { "name", name }, { "input", QJsonObject() } }) } }));
        b += sse(J({ { "type", "content_block_delta" }, { "index", i }, { "delta", J({ { "type", "input_json_delta" }, { "partial_json", js.left(js.size() / 2) } }) } }));
        b += sse(J({ { "type", "content_block_delta" }, { "index", i }, { "delta", J({ { "type", "input_json_delta" }, { "partial_json", js.mid(js.size() / 2) } }) } }));
        b += sse(J({ { "type", "content_block_stop" }, { "index", i } }));
    }
    b += sse(J({ { "type", "message_delta" }, { "delta", J({ { "stop_reason", name.isEmpty() ? "end_turn" : "tool_use" } }) }, { "usage", J({ { "output_tokens", 300 } }) } }));
    b += sse(J({ { "type", "message_stop" } }));
    return b;
}

// OpenAI 兼容：推理 + 若干工具调用（[id, 名字, 参数]）或文字
QByteArray oaTools(const QList<std::tuple<QString, QString, QJsonObject>> &calls, const QString &text = {})
{
    QByteArray b = sse(J({ { "choices", QJsonArray{ J({ { "delta", J({ { "reasoning_content", QStringLiteral("推理") } }) } }) } } }));
    for (int i = 0; i < calls.size(); ++i) {
        const auto &[id, name, args] = calls.at(i);
        const QString js = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
        b += sse(J({ { "choices", QJsonArray{ J({ { "delta", J({ { "tool_calls", QJsonArray{ J({ { "index", i }, { "id", id }, { "type", "function" }, { "function", J({ { "name", name }, { "arguments", js.left(3) } }) } }) } } }) } }) } } }));
        b += sse(J({ { "choices", QJsonArray{ J({ { "delta", J({ { "tool_calls", QJsonArray{ J({ { "index", i }, { "function", J({ { "arguments", js.mid(3) } }) } }) } } }) } }) } } }));
    }
    if (!text.isEmpty()) b += sse(J({ { "choices", QJsonArray{ J({ { "delta", J({ { "content", text } }) } }) } } }));
    b += sse(J({ { "choices", QJsonArray{ J({ { "delta", QJsonObject() }, { "finish_reason", calls.isEmpty() ? "stop" : "tool_calls" } }) } } }));
    b += sse(J({ { "choices", QJsonArray() }, { "usage", J({ { "prompt_tokens", 2000 }, { "completion_tokens", 100 } }) } }));
    b += "data: [DONE]\n\n";
    return b;
}

QVariantMap lastProps(AiController *ai, int k = 0)
{
    const QVariantList m = ai->messages();
    for (qsizetype i = m.size() - 1; i >= 0; --i) {
        const QVariantList ps = m.at(i).toMap().value(QStringLiteral("props")).toList();
        if (ps.size() > k) return ps.at(k).toMap();
    }
    return {};
}
int countBeforeAi(const QString &dir) { return QDir(dir).entryList({ QStringLiteral("travel.before-ai-*.json") }, QDir::Files).size(); }
const json::Value *planIn(const json::Value &root, const QString &id) { return tp::map::planedit::findPlan(root, id); }
QString jstr(const json::Value &v) { return json::stringify(v, 0); }

void runTools(Report &r, AppController *app, AiController *ai, FakeAi &fake, AiController::TestHooks h, const QString &chats)
{
    r.section(QStringLiteral("0.13.1 AI 改计划（工具、预览、应用、撤销）"));
    data::DataStore *store = app->store();
    const auto idle = [&](int ms = 8000) { return waitFor([ai] { return !ai->busy(); }, ms); };
    const json::Value original = store->data();
    // 自检用的计划（数据是验证用的副本）：p_aitest 三站 + 交通住宿 / 待办 / 行李各两条，s_a 有走路的折线、s_b 有一张照片；p_aidel 用来删
    store->modify([](json::Value &d) {
        const json::Value fx = json::parse(QString::fromUtf8(R"({"plans":[
          {"id":"p_aitest","name":"AI自检","type":"route","status":"planning","startDate":"2026-12-01","endDate":"2026-12-04","notes":"","stops":[
            {"id":"s_a","name":"Kyoto","lat":35.0,"lng":135.75,"arriveDate":"2026-12-01","departDate":"","schedule":[{"date":"2026-12-01","text":"早饭"},{"date":"2026-12-01","time":"10:00","text":"博物馆"}],"leg":{"mode":"walk","path":"abc","key":"k","dist":10,"time":20}},
            {"id":"s_b","name":"Nara","lat":34.68,"lng":135.8,"arriveDate":"","departDate":"","schedule":[]},
            {"id":"s_c","name":"Osaka","lat":34.69,"lng":135.5,"arriveDate":"","departDate":"","schedule":[]}],
           "bookings":[{"id":"bk_a","kind":"flight","title":"航班"},{"id":"bk_b","kind":"hotel","title":"酒店"}],
           "todos":[{"id":"td_a","text":"办签证","done":false},{"id":"td_b","text":"换钱","done":false}],
           "packing":[{"id":"pk_a","text":"护照","done":false},{"id":"pk_b","text":"充电器","done":false}],
           "expenses":[{"id":"ex_a","amount":12,"note":"秘密的消费"}]},
          {"id":"p_aidel","name":"AI要删的","type":"single","status":"wish","stops":[]}],
          "photos":[{"id":"ph_aitest","file":"photos/x.jpg","plan":"p_aitest","stop":"s_b"}]})"));
        json::Array &ps = d.mutObject().find(QStringLiteral("plans"))->mutArray();
        for (const json::Value &p : fx[QStringLiteral("plans")].asArray()) ps.push_back(p);
        if (!d.mutObject().find(QStringLiteral("photos"))) d.mutObject().set(QStringLiteral("photos"), json::Value::array());
        d.mutObject().find(QStringLiteral("photos"))->mutArray().push_back(fx[QStringLiteral("photos")].asArray().front());
    });
    app->commitStep();
    // 地名搜索换成假的：岚山附近有一个结果（在 5 公里内 → 核对过）
    services::Geocoder *geo = geocoderCtl();
    if (geo) geo->setFetchForTest([](const QUrl &, std::function<void(int, const QByteArray &, int)> done) {
        QTimer::singleShot(10, [done] { done(200, R"([{"name":"Test Place","display_name":"Test Place, Kyoto, Japan","lat":"35.0102","lon":"135.0101"}])", 0); });
    });
    ai->setProvider(QStringLiteral("claude"));
    ai->setTier(QStringLiteral("balanced"));
    ai->setWeb(true);
    ai->newChat();

    // ---- Anthropic：读计划（带思考块）→ 自动续轮 → propose_changes（20 种 op）----
    const QJsonArray changes{
        J({ { "op", "create_plan" }, { "new_id", "new1" }, { "name", QStringLiteral("AI新计划") }, { "startDate", "2027-01-01" }, { "endDate", "2027-01-03" } }),
        J({ { "op", "add_stop" }, { "plan_id", "new1" }, { "name", QStringLiteral("新站") }, { "lat", 35.01 }, { "lng", 135.01 },
            { "schedule", QJsonArray{ J({ { "date", "2027-01-01" }, { "time", "09:00" }, { "end_time", "11:00" }, { "text", QStringLiteral("清水寺") } }),
                                      J({ { "date", "2027-01-01" }, { "text", QStringLiteral("取消的一条") } }), J({ { "text", QStringLiteral("祇园散步") } }) } } }),   // 0.13.3 新站点带日程
        J({ { "op", "update_plan" }, { "name", QStringLiteral("AI自检改") } }),
        J({ { "op", "delete_plan" }, { "plan_id", "p_aidel" } }),
        J({ { "op", "set_days" }, { "days", 5 } }),
        J({ { "op", "add_stop" }, { "name", QStringLiteral("加的站") }, { "lat", 35.01 }, { "lng", 135.01 }, { "after_stop_id", "s_a" }, { "legMode", "train" } }),
        J({ { "op", "update_stop" }, { "stop_id", "s_c" }, { "notes", QStringLiteral("改备注") } }),
        J({ { "op", "remove_stop" }, { "stop_id", "s_b" } }),
        J({ { "op", "move_stop" }, { "stop_id", "s_c" }, { "after_stop_id", "start" } }),
        J({ { "op", "add_schedule" }, { "stop_id", "s_a" }, { "text", QStringLiteral("晚饭") }, { "time", "19:00" } }),
        J({ { "op", "update_schedule" }, { "stop_id", "s_a" }, { "n", 2 }, { "text", QStringLiteral("美术馆") } }),
        J({ { "op", "remove_schedule" }, { "stop_id", "s_a" }, { "n", 1 } }),
        J({ { "op", "add_booking" }, { "kind", "train" }, { "title", QStringLiteral("火车") } }),
        J({ { "op", "update_booking" }, { "booking_id", "bk_a" }, { "title", QStringLiteral("新航班") } }),
        J({ { "op", "remove_booking" }, { "booking_id", "bk_b" } }),
        J({ { "op", "add_todo" }, { "text", QStringLiteral("买票") }, { "date", "2026-11-30" } }),
        J({ { "op", "update_todo" }, { "todo_id", "td_a" }, { "done", true } }),
        J({ { "op", "remove_todo" }, { "todo_id", "td_b" } }),
        J({ { "op", "add_packing" }, { "text", QStringLiteral("雨伞") } }),
        J({ { "op", "update_packing" }, { "packing_id", "pk_a" }, { "text", QStringLiteral("护照夹") } }),
        J({ { "op", "remove_packing" }, { "packing_id", "pk_b" } }) };
    fake.bodies.clear();
    fake.script = { anthTool(QStringLiteral("toolu_read"), QStringLiteral("get_plans"), J({ { "plan_ids", QJsonArray{ "p_aitest" } } }), true),
                    anthTool(QStringLiteral("toolu_prop"), QStringLiteral("propose_changes"), J({ { "summary", QStringLiteral("全部 20 种") }, { "plan_id", "p_aitest" }, { "changes", changes },
                                                                                                  { "gaps", QJsonArray{ J({ { "text", QStringLiteral("查火车时刻") }, { "search", true } }) } } }), false, QStringLiteral("改好了。")) };
    ai->send(QStringLiteral("帮我改一下 AI自检"));
    r.check(idle() && fake.bodies.size() == 2, QStringLiteral("读计划后自动续一轮（共 2 个请求）"), QString::number(fake.bodies.size()));
    const QJsonObject b0 = QJsonDocument::fromJson(fake.bodies.value(0)).object();
    QStringList toolNames;
    int maxUses = 0;
    for (const auto &t : b0.value(QStringLiteral("tools")).toArray()) { toolNames << t.toObject().value(QStringLiteral("name")).toString(); if (t.toObject().contains(QStringLiteral("max_uses"))) maxUses = t.toObject().value(QStringLiteral("max_uses")).toInt(); }
    r.check(toolNames == QStringList{ "get_plans", "research_status", "propose_changes", "web_search" } && maxUses == 3, QStringLiteral("Claude 带四个工具，联网搜索每轮最多 3 次"), toolNames.join(u','));
    const QJsonArray sys = b0.value(QStringLiteral("system")).toArray();
    r.check(sys.size() == 2 && sys.at(0).toObject().contains(QStringLiteral("cache_control")) && !sys.at(1).toObject().contains(QStringLiteral("cache_control"))
            && sys.at(0).toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("p_aitest")) && sys.at(1).toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("现在的情况"))
            && !sys.at(0).toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("读不到")),
            QStringLiteral("system：规则 + 简表打缓存标记，现在的情况放后面；不再说看不到计划"));
    const QString b1 = QString::fromUtf8(fake.bodies.value(1));
    const QJsonArray m1 = QJsonDocument::fromJson(fake.bodies.value(1)).object().value(QStringLiteral("messages")).toArray();
    const QJsonArray amContent = m1.size() >= 2 ? m1.at(m1.size() - 2).toObject().value(QStringLiteral("content")).toArray() : QJsonArray();
    r.check(!amContent.isEmpty() && amContent.at(0).toObject().value(QStringLiteral("type")) == QLatin1String("thinking") && amContent.at(0).toObject().value(QStringLiteral("signature")) == QLatin1String("sig-abc")
            && amContent.at(1).toObject().value(QStringLiteral("type")) == QLatin1String("tool_use"),
            QStringLiteral("思考块连 signature 原样回传，后面跟工具调用"), QString::fromUtf8(QJsonDocument(amContent).toJson(QJsonDocument::Compact)).left(200));
    const QJsonObject tr0 = m1.isEmpty() ? QJsonObject() : m1.last().toObject().value(QStringLiteral("content")).toArray().at(0).toObject();
    r.check(tr0.value(QStringLiteral("type")) == QLatin1String("tool_result") && tr0.value(QStringLiteral("tool_use_id")) == QLatin1String("toolu_read") && tr0.value(QStringLiteral("content")).toString().contains(QStringLiteral("AI自检"))
            && !tr0.value(QStringLiteral("content")).toString().contains(QStringLiteral("秘密的消费")) && !tr0.value(QStringLiteral("content")).toString().contains(QStringLiteral("\"path\"")),
            QStringLiteral("get_plans 的结果回传：有计划内容，不带消费（默认不发）、不带折线"));
    r.check(b1.contains(QStringLiteral("AI自检")), QStringLiteral("请求里带上计划"));
    QVariantMap P = lastProps(ai);
    r.check(P.value(QStringLiteral("state")) == QLatin1String("open") && P.value(QStringLiteral("total")).toInt() == 21 && P.value(QStringLiteral("nOk")).toInt() == 21,
            QStringLiteral("20 种 op 都认领到了（21 项、默认全勾）"), QStringLiteral("%1 %2/%3").arg(P.value(QStringLiteral("state")).toString()).arg(P.value(QStringLiteral("nOk")).toInt()).arg(P.value(QStringLiteral("total")).toInt()));
    r.check(waitFor([ai] { return !lastProps(ai).value(QStringLiteral("busyGeo")).toBool(); }, 6000), QStringLiteral("add_stop 用地名搜索核对完坐标"));
    bool geoOk = false;
    for (const QVariant &g : lastProps(ai).value(QStringLiteral("groups")).toList())
        for (const QVariant &row : g.toMap().value(QStringLiteral("rows")).toList())
            if (row.toMap().value(QStringLiteral("geo")).toMap().value(QStringLiteral("state")) == QLatin1String("ok")) geoOk = true;
    r.check(geoOk, QStringLiteral("坐标在 5 公里内：标“核对过”"));
    bool actOk = false;
    for (const QVariant &m : ai->messages()) actOk = actOk || m.toMap().value(QStringLiteral("act")).toStringList().join(u' ').contains(QStringLiteral("读了「AI自检」"));
    r.check(actOk, QStringLiteral("回答上面写读了哪个计划"));

    // ---- 0.13.3 新站点带的日程逐条显示，取消一条 ----
    const int mi = P.value(QStringLiteral("mi")).toInt();
    QStringList schRows;
    for (const QVariant &g : lastProps(ai).value(QStringLiteral("groups")).toList())
        for (const QVariant &row : g.toMap().value(QStringLiteral("rows")).toList())
            if (row.toMap().value(QStringLiteral("k")).toInt() == 1)
                for (const QVariant &x : row.toMap().value(QStringLiteral("sched")).toList()) schRows << x.toMap().value(QStringLiteral("text")).toString();
    r.check(schRows == QStringList{ QStringLiteral("01-01 09:00–11:00 清水寺"), QStringLiteral("01-01 取消的一条"), QStringLiteral("祇园散步") }, QStringLiteral("add_stop 带的日程在预览里逐条列出"), schRows.join(u'|'));
    ai->toggleSched(mi, 0, 1, 1);
    bool schOff = false;
    for (const QVariant &g : lastProps(ai).value(QStringLiteral("groups")).toList())
        for (const QVariant &row : g.toMap().value(QStringLiteral("rows")).toList())
            if (row.toMap().value(QStringLiteral("k")).toInt() == 1) schOff = !row.toMap().value(QStringLiteral("sched")).toList().value(1).toMap().value(QStringLiteral("on")).toBool();
    r.check(schOff, QStringLiteral("新站点的一条日程可以单独取消勾选"));

    // ---- 应用（含 AI 修改前备份）→ 撤销 → 重做 ----
    const int backups0 = countBeforeAi(store->dataDir());
    const json::Value pre = store->data();
    ai->applyProposal(mi, 0);
    P = lastProps(ai);
    r.check(P.value(QStringLiteral("state")) == QLatin1String("applied") && P.value(QStringLiteral("applied")).toInt() == 21, QStringLiteral("应用 21 项"), QString::number(P.value(QStringLiteral("applied")).toInt()));
    r.check(countBeforeAi(store->dataDir()) == std::min(backups0 + 1, 10), QStringLiteral("应用前存了一份 travel.before-ai-*.json"), QStringLiteral("%1 → %2").arg(backups0).arg(countBeforeAi(store->dataDir())));
    const json::Value &d = store->data();
    const json::Value *pt = planIn(d, QStringLiteral("p_aitest"));
    QStringList order;
    if (pt) for (const json::Value &s : (*pt)[QStringLiteral("stops")].asArray()) order << s[QStringLiteral("name")].toString();
    r.check(pt && (*pt)[QStringLiteral("name")].toString() == QStringLiteral("AI自检改") && (*pt)[QStringLiteral("days")].toNumber() == 5, QStringLiteral("update_plan、set_days"));
    r.check(order == QStringList{ QStringLiteral("Osaka"), QStringLiteral("Kyoto"), QStringLiteral("加的站") }, QStringLiteral("add_stop（放在后面）、remove_stop、move_stop（最前面）"), order.join(u','));
    const json::Value *sa = nullptr;
    if (pt) for (const json::Value &s : (*pt)[QStringLiteral("stops")].asArray()) if (s[QStringLiteral("id")].toString() == QLatin1String("s_a")) sa = &s;
    QStringList sched;
    if (sa) for (const json::Value &it : (*sa)[QStringLiteral("schedule")].asArray()) sched << it[QStringLiteral("text")].toString();
    r.check(sched == QStringList{ QStringLiteral("美术馆"), QStringLiteral("晚饭") }, QStringLiteral("add_schedule、update_schedule、remove_schedule（序号先认领好，删前一条不指错）"), sched.join(u','));
    r.check(sa && (*sa)[QStringLiteral("leg")][QStringLiteral("path")].isNull() && (*sa)[QStringLiteral("leg")][QStringLiteral("mode")].toString() == QLatin1String("walk"), QStringLiteral("前一站去下一站的折线清掉了，方式留着"));
    const QString ptext = pt ? jstr(*pt) : QString();
    r.check(ptext.contains(QStringLiteral("改备注")) && ptext.contains(QStringLiteral("新航班")) && ptext.contains(QStringLiteral("火车")) && !ptext.contains(QStringLiteral("酒店")),
            QStringLiteral("update_stop、add / update / remove_booking"));
    r.check(ptext.contains(QStringLiteral("买票")) && ptext.contains(QStringLiteral("\"due\":\"2026-11-30\"")) && !ptext.contains(QStringLiteral("换钱")) && ptext.contains(QStringLiteral("雨伞")) && ptext.contains(QStringLiteral("护照夹")) && !ptext.contains(QStringLiteral("充电器")),
            QStringLiteral("add / update / remove_todo、add / update / remove_packing"));
    r.check(ptext.contains(QStringLiteral("{\"id\":\"td_a\",\"text\":\"办签证\",\"done\":true}")), QStringLiteral("update_todo 标记完成"));
    r.check(!planIn(d, QStringLiteral("p_aidel")), QStringLiteral("delete_plan"));
    bool newOk = false;
    for (const json::Value &p : d[QStringLiteral("plans")].asArray())
        if (p[QStringLiteral("name")].toString() == QStringLiteral("AI新计划")) newOk = p[QStringLiteral("stops")].asArray().size() == 1 && p[QStringLiteral("startDate")].toString() == QLatin1String("2027-01-01");
    r.check(newOk, QStringLiteral("create_plan + 往新计划 add_stop"));
    QStringList newSch;
    for (const json::Value &p : d[QStringLiteral("plans")].asArray())
        if (p[QStringLiteral("name")].toString() == QStringLiteral("AI新计划") && !p[QStringLiteral("stops")].asArray().empty())
            for (const json::Value &it : p[QStringLiteral("stops")].asArray()[0][QStringLiteral("schedule")].asArray())
                newSch << it[QStringLiteral("date")].toString() + u'/' + it[QStringLiteral("time")].toString() + u'/' + it[QStringLiteral("end")].toString() + u'/' + it[QStringLiteral("text")].toString();
    r.check(newSch == QStringList{ QStringLiteral("2027-01-01/09:00/11:00/清水寺"), QStringLiteral("///祇园散步") }, QStringLiteral("add_stop 带日程：写成现有日程项（date / time / end / text），取消的那条没写"), newSch.join(u'|'));
    QString phPlan = QStringLiteral("?");
    for (const json::Value &ph : d[QStringLiteral("photos")].asArray()) if (ph[QStringLiteral("id")].toString() == QLatin1String("ph_aitest")) phPlan = ph[QStringLiteral("plan")].toString();
    r.check(phPlan.isEmpty(), QStringLiteral("删掉的站点的照片移到“未归类”"));
    r.check(!jstr(d).contains(QStringLiteral("_aiMark")), QStringLiteral("临时记号没留下"));
    r.check(!store->isDirty(), QStringLiteral("应用后马上存好"));
    const QString res = lastOf(ai, "assistant").isEmpty() ? QString() : QString();
    Q_UNUSED(res);
    const json::Value post = store->data();
    app->undo();
    r.check(jstr(store->data()[QStringLiteral("plans")]) == jstr(pre[QStringLiteral("plans")]) && jstr(store->data()[QStringLiteral("photos")]) == jstr(pre[QStringLiteral("photos")]),
            QStringLiteral("Ctrl+Z 一步撤回全部 21 项（含新站点的日程，数据和应用前一样）"));
    r.check(lastProps(ai).value(QStringLiteral("state")) == QLatin1String("undone"), QStringLiteral("预览标“已撤销”"));
    app->redo();
    r.check(jstr(store->data()[QStringLiteral("plans")]) == jstr(post[QStringLiteral("plans")]) && lastProps(ai).value(QStringLiteral("state")) == QLatin1String("applied"), QStringLiteral("Ctrl+Y 重做：数据回来，预览标回“已应用”"));

    // ---- 找不到的目标标红；回传上一份预览的结果 ----
    fake.bodies.clear();
    fake.script = { anthTool(QStringLiteral("toolu_bad"), QStringLiteral("propose_changes"), J({ { "summary", "bad" }, { "plan_id", "p_aitest" }, { "changes", QJsonArray{
        J({ { "op", "update_stop" }, { "stop_id", "s_none" }, { "notes", "x" } }), J({ { "op", "remove_todo" }, { "todo_id", "td_none" } }),
        J({ { "op", "update_plan" }, { "plan_id", "p_none" }, { "name", "x" } }), J({ { "op", "update_schedule" }, { "stop_id", "s_a" }, { "n", 99 }, { "text", "x" } }),
        J({ { "op", "add_todo" }, { "text", QStringLiteral("能用的一项") } }) } } }), false) };
    ai->send(QStringLiteral("再改"));
    idle();
    P = lastProps(ai);
    QStringList bads;
    for (const QVariant &g : P.value(QStringLiteral("groups")).toList())
        for (const QVariant &row : g.toMap().value(QStringLiteral("rows")).toList()) if (!row.toMap().value(QStringLiteral("bad")).toString().isEmpty()) bads << row.toMap().value(QStringLiteral("bad")).toString();
    r.check(bads.size() == 4 && P.value(QStringLiteral("nOn")).toInt() == 1 && P.value(QStringLiteral("nOk")).toInt() == 1, QStringLiteral("找不到的站点 / 待办 / 计划 / 日程标红、不勾，能用的照常勾"), bads.join(u'|'));
    r.check(QString::fromUtf8(fake.bodies.value(0)).contains(QStringLiteral("用户应用了 21 项")), QStringLiteral("上一份预览应用的结果回传给 AI"));

    // ---- AI 备份只留 10 份 ----
    int tick = 0;
    const QDateTime base = QDateTime::currentDateTime().addDays(1);
    store->setClock([&tick, base] { return base.addSecs(++tick); });
    for (int i = 0; i < 12; ++i) store->snapshotBeforeAi();
    store->setClock({});
    r.check(countBeforeAi(store->dataDir()) == 10, QStringLiteral("AI 修改前的备份只留最近 10 份"), QString::number(countBeforeAi(store->dataDir())));

    // ---- 恢复历史：没应用的预览标“已过期” ----
    ai->setChatsFile(chats);
    r.check(lastProps(ai).value(QStringLiteral("state")) == QLatin1String("expired"), QStringLiteral("重新打开后，没应用的预览标“已过期”"), lastProps(ai).value(QStringLiteral("state")).toString());

    // ---- 0.13.3 撤销栈满 30 步以后：预览按数据版本认，不靠步数 ----
    ai->newChat();
    auto edit = [&](int i) { store->modify([i](json::Value &dd) { dd.mutObject().set(QStringLiteral("_aiUndoTest"), json::Value(double(i))); }); app->commitStep(); };
    for (int i = 0; i < data::UndoStack::kMaxSteps + 1; ++i) edit(i);
    r.check(app->undoCount() == data::UndoStack::kMaxSteps, QStringLiteral("撤销栈先填满 30 步"), QString::number(app->undoCount()));
    fake.script = { anthTool(QStringLiteral("toolu_u30"), QStringLiteral("propose_changes"), J({ { "summary", "u30" }, { "plan_id", "p_aitest" }, { "changes", QJsonArray{ J({ { "op", "add_todo" }, { "text", QStringLiteral("满栈待办") } }) } } }), false) };
    ai->send(QStringLiteral("加一项"));
    idle();
    ai->applyProposal(lastProps(ai).value(QStringLiteral("mi")).toInt(), 0);
    auto hasTodo = [store] { return jstr(store->data()).contains(QStringLiteral("满栈待办")); };
    r.check(lastProps(ai).value(QStringLiteral("state")) == QLatin1String("applied") && hasTodo(), QStringLiteral("栈满时应用 AI 修改"));
    edit(100);
    app->undo();
    r.check(lastProps(ai).value(QStringLiteral("state")) == QLatin1String("applied") && hasTodo(), QStringLiteral("撤销应用之后的一步：预览还是“已应用”（以前按步数会误标已撤销）"), lastProps(ai).value(QStringLiteral("state")).toString());
    app->undo();
    r.check(lastProps(ai).value(QStringLiteral("state")) == QLatin1String("undone") && !hasTodo(), QStringLiteral("再撤一步撤到应用前：标“已撤销”"), lastProps(ai).value(QStringLiteral("state")).toString());
    app->redo();
    r.check(lastProps(ai).value(QStringLiteral("state")) == QLatin1String("applied") && hasTodo(), QStringLiteral("重做：标回“已应用”"));
    app->undo();
    edit(101);   // 撤到应用前又改了别的：重做栈清空，AI 这次再也回不来
    app->undo();
    app->redo();
    r.check(lastProps(ai).value(QStringLiteral("state")) == QLatin1String("undone") && !hasTodo(), QStringLiteral("撤销后改了别的再撤 / 重做：仍是“已撤销”"), lastProps(ai).value(QStringLiteral("state")).toString());

    // ---- OpenAI 兼容（DeepSeek）：工具调用往返，reasoning_content 回传；没有联网 ----
    ai->setProvider(QStringLiteral("deepseek"));
    ai->newChat();
    fake.bodies.clear();
    fake.script = { oaTools({ { QStringLiteral("call_1"), QStringLiteral("get_plans"), J({ { "plan_ids", QJsonArray{ "p_aitest" } } }) } }),
                    oaTools({}, QStringLiteral("好的，看过了。")) };
    ai->send(QStringLiteral("看看计划"));
    r.check(idle() && fake.bodies.size() == 2, QStringLiteral("OpenAI 兼容：读计划后自动续一轮"));
    const QJsonObject ob0 = QJsonDocument::fromJson(fake.bodies.value(0)).object();
    QStringList oaNames;
    for (const auto &t : ob0.value(QStringLiteral("tools")).toArray()) oaNames << t.toObject().value(QStringLiteral("type")).toString() + u':' + t.toObject().value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString();
    r.check(oaNames == QStringList{ "function:get_plans", "function:research_status", "function:propose_changes" } && !ob0.contains(QStringLiteral("enable_search")), QStringLiteral("函数工具形状；DeepSeek 不带联网"), oaNames.join(u','));
    const QJsonArray om = QJsonDocument::fromJson(fake.bodies.value(1)).object().value(QStringLiteral("messages")).toArray();
    const QJsonObject oaAm = om.size() >= 2 ? om.at(om.size() - 2).toObject() : QJsonObject();
    const QJsonObject oaTool = om.isEmpty() ? QJsonObject() : om.last().toObject();
    r.check(oaAm.value(QStringLiteral("reasoning_content")) == QStringLiteral("推理") && oaAm.value(QStringLiteral("tool_calls")).toArray().at(0).toObject().value(QStringLiteral("id")) == QLatin1String("call_1"),
            QStringLiteral("带工具时 reasoning_content 和 tool_calls 原样回传"), QString::fromUtf8(QJsonDocument(oaAm).toJson(QJsonDocument::Compact)).left(200));
    r.check(oaTool.value(QStringLiteral("role")) == QLatin1String("tool") && oaTool.value(QStringLiteral("tool_call_id")) == QLatin1String("call_1") && oaTool.value(QStringLiteral("content")).toString().contains(QStringLiteral("AI自检")),
            QStringLiteral("工具结果用 role = tool 回传"));
    r.check(lastOf(ai, "assistant").value(QStringLiteral("noWeb")).toBool(), QStringLiteral("没有联网能力的家：回复下标“没有联网核实”"));

    // ---- Kimi：内置 $web_search，每次请求最多回传 3 次 ----
    ai->setProvider(QStringLiteral("kimi"));
    ai->setKey(QStringLiteral("sk-selftest-kimi-0000"));
    fake.bodies.clear();
    QList<std::tuple<QString, QString, QJsonObject>> four;
    for (int i = 1; i <= 4; ++i) four << std::tuple{ QStringLiteral("ws_%1").arg(i), QStringLiteral("$web_search"), J({ { "search_result", J({ { "search_id", QStringLiteral("q%1").arg(i) } }) } }) };
    fake.script = { oaTools(four), oaTools({}, QStringLiteral("查到了。")) };
    if (!ai->send(QStringLiteral("查一下"))) ai->confirmTell();
    r.check(idle() && fake.bodies.size() == 2, QStringLiteral("Kimi 联网：搜完自动续一轮"));
    QStringList kNames;
    for (const auto &t : QJsonDocument::fromJson(fake.bodies.value(0)).object().value(QStringLiteral("tools")).toArray()) kNames << t.toObject().value(QStringLiteral("type")).toString();
    int echoed = 0, refused = 0;
    for (const auto &m : QJsonDocument::fromJson(fake.bodies.value(1)).object().value(QStringLiteral("messages")).toArray()) {
        const QJsonObject o = m.toObject();
        if (o.value(QStringLiteral("role")) != QLatin1String("tool")) continue;
        if (o.value(QStringLiteral("content")).toString().contains(QStringLiteral("search_id"))) ++echoed;
        else if (o.value(QStringLiteral("content")).toString().contains(QStringLiteral("3 次"))) ++refused;
    }
    r.check(kNames.contains(QStringLiteral("builtin_function")) && echoed == 3 && refused == 1, QStringLiteral("联网次数上限：4 次里回传 3 次，第 4 次回绝"), QStringLiteral("%1 echo=%2 refused=%3").arg(kNames.join(u',')).arg(echoed).arg(refused));
    bool searchedMeta = false;
    for (const QVariant &m : ai->messages()) searchedMeta = searchedMeta || m.toMap().value(QStringLiteral("meta")).toString().contains(QStringLiteral("搜索 3 次"));
    r.check(searchedMeta && !lastOf(ai, "assistant").value(QStringLiteral("noWeb")).toBool(), QStringLiteral("搜索次数记进用量；联网的家不标“没有联网核实”"));

    // ---- 时间预算：剩不到 45 秒提醒；剩不到 5 秒停下来问；继续查 ----
    ai->setProvider(QStringLiteral("claude"));
    ai->newChat();
    fake.bodies.clear();
    fake.script = { anthTool(QStringLiteral("rs1"), QStringLiteral("research_status"), J({ { "todo", QJsonArray{ "a", "b" } }, { "remaining_seconds", 30 } }), false),
                    anthTool(QString(), QString(), {}, false, QStringLiteral("查完了。")) };
    ai->send(QStringLiteral("补充一下"));
    idle();
    r.check(fake.bodies.size() == 2 && QString::fromUtf8(fake.bodies.value(1)).contains(QStringLiteral("剩下的时间不多")), QStringLiteral("预算 30 秒：自动续轮，并提醒剩下的时间不多（< 45 秒）"));
    h.budgetMul = 0.1;   // 10 秒的预算缩成 1 秒：第一次汇报就到点
    ai->setTestHooks(h);
    ai->newChat();
    fake.bodies.clear();
    fake.script = { anthTool(QStringLiteral("rs2"), QStringLiteral("research_status"), J({ { "todo", QJsonArray{ "a", "b" } }, { "remaining_seconds", 10 } }), false) };
    ai->send(QStringLiteral("补充一下"));
    idle();
    pause(1100);
    QVariantMap rs = lastOf(ai, "assistant").value(QStringLiteral("research")).toObject().toVariantMap();
    // 第一次汇报时预算 = 已用 + 1 秒，离到点还有约 1 秒（≤ 5 秒）→ 停下
    r.check(fake.bodies.size() == 1 && rs.value(QStringLiteral("paused")).toBool(), QStringLiteral("剩不到 5 秒：停下来问“继续查 / 先到这里”，不再自动续"), QString::number(fake.bodies.size()));
    fake.script = { anthTool(QStringLiteral("rs3"), QStringLiteral("research_status"), J({ { "todo", QJsonArray() }, { "remaining_seconds", 10 }, { "finished", true } }), false, QStringLiteral("都查完了。")) };
    int rmi = -1;
    const QVariantList ms = ai->messages();
    for (int i = 0; i < ms.size(); ++i) if (ms.at(i).toMap().contains(QStringLiteral("research"))) rmi = i;
    ai->researchAnswer(rmi, true);
    idle();
    r.check(fake.bodies.size() == 2 && QString::fromUtf8(fake.bodies.value(1)).contains(QStringLiteral("继续查")), QStringLiteral("点“继续查”接着问（预算延长）"));
    h.budgetMul = 1;
    ai->setTestHooks(h);

    // ---- 自动续轮最多 12 轮 ----
    ai->newChat();
    fake.bodies.clear();
    fake.script.clear();
    for (int i = 0; i < 14; ++i) fake.script << anthTool(QStringLiteral("loop%1").arg(i), QStringLiteral("get_plans"), J({ { "plan_ids", QJsonArray{ "p_aitest" } } }), false);
    ai->send(QStringLiteral("一直读"));
    idle(20000);
    r.check(fake.bodies.size() == 13 && lastMsg(ai).value(QStringLiteral("text")).toString().contains(QStringLiteral("上限")), QStringLiteral("自动续轮到 12 轮停下（共 13 个请求）"), QString::number(fake.bodies.size()));
    fake.script.clear();

    // 还原数据（这一段加的计划、改动都撤掉）
    if (geo) geo->setFetchForTest({});
    store->modify([&original](json::Value &dd) { dd = original; });
    app->commitStep();
    ai->newChat();
}


// ---------------- 0.13.2 引导式规划（整页向导）、从订单生成、每份方案的用量 ----------------

QJsonArray usagePlans(Settings *st) { return QJsonDocument::fromJson(json::stringify(st->value(QStringLiteral("aiUsage")), 0).toUtf8()).object().value(QStringLiteral("plans")).toArray(); }
QString lastUserBody(const QByteArray &body)
{
    const QJsonArray ms = QJsonDocument::fromJson(body).object().value(QStringLiteral("messages")).toArray();
    for (qsizetype i = ms.size() - 1; i >= 0; --i) {
        const QJsonObject m = ms.at(i).toObject();
        if (m.value(QStringLiteral("role")) != QLatin1String("user")) continue;
        if (m.value(QStringLiteral("content")).isString()) return m.value(QStringLiteral("content")).toString();
        QString t;
        for (const auto &b : m.value(QStringLiteral("content")).toArray()) t += b.toObject().value(QStringLiteral("text")).toString();
        return t;
    }
    return {};
}

void runPlanWizard(Report &r, AppController *app, AiController *ai, FakeAi &fake, QQuickWindow *win, services::NetClient *net, Settings *st, const QString &key, const QString &prefix)
{
    r.section(QStringLiteral("0.13.2 让 AI 帮我规划、从订单生成、方案用量"));
    data::DataStore *store = app->store();
    const auto idle = [&](int ms = 8000) { return waitFor([ai] { return !ai->busy(); }, ms); };
    const json::Value original = store->data();
    store->modify([](json::Value &d) {
        const json::Value p = json::parse(QString::fromUtf8(R"({"id":"p_ord","name":"订单自检","type":"route","status":"planning","startDate":"2026-12-01","endDate":"2026-12-04",
            "stops":[{"id":"s_o1","name":"Tokyo","lat":35.68,"lng":139.76,"schedule":[]}],"bookings":[]})"));
        d.mutObject().find(QStringLiteral("plans"))->mutArray().push_back(p);
    });
    app->commitStep();
    ai->setProvider(QStringLiteral("claude"));
    ai->setTier(QStringLiteral("balanced"));
    ai->newChat();
    st->setValue(QStringLiteral("aiUsage"), json::Value());
    ai->setCap(1000);

    QQuickItem *page = findItem(win->contentItem(), QStringLiteral("aiPlanPage"));
    QQuickItem *panel = findItem(win->contentItem(), QStringLiteral("aiPanel"));
    const auto call = [&](const char *fn) { if (page) QMetaObject::invokeMethod(page, fn); };
    const auto stepIs = [&](int n) {
        QQuickItem *box = findItem(win->contentItem(), QStringLiteral("aiPlanPage-%1").arg(n));
        return page && page->property("step").toInt() == n && box && box->isVisible();
    };
    QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("aiplan")));
    r.check(page && waitFor([page] { return page->isVisible() && page->opacity() > 0.9; }, 2000), QStringLiteral("整页向导打开了（goPage aiplan）"));
    if (!page) return;

    // 没钥匙：只有引导卡
    ai->clearKey();
    QQuickItem *guide = findItem(win->contentItem(), QStringLiteral("aiPlanGuide"));
    QQuickItem *steps = findItem(win->contentItem(), QStringLiteral("aiPlanSteps"));
    r.check(guide && steps && waitFor([guide, steps] { return guide->isVisible() && !steps->isVisible(); }, 1000), QStringLiteral("没钥匙时向导首页是引导卡，没有步骤"));
    ai->setKey(key);
    r.check(waitFor([guide, steps] { return !guide->isVisible() && steps->isVisible(); }, 1000) && stepIs(0), QStringLiteral("有钥匙：第 1 步（出发地、目的地）"));

    // 上一步 / 下一步 / 跳过
    call("back");
    QQuickItem *backBtn = findItem(win->contentItem(), QStringLiteral("aiPlanBack"));
    r.check(stepIs(0) && backBtn && !backBtn->isVisible(), QStringLiteral("第 1 步没有上一步"));
    page->setProperty("fFrom", QStringLiteral("上海"));
    page->setProperty("fTo", QStringLiteral("京都"));
    call("next");
    r.check(stepIs(1) && backBtn->isVisible(), QStringLiteral("下一步 → 第 2 步（日期或天数）"));
    page->setProperty("dateMode", QStringLiteral("days"));
    page->setProperty("fDays", QStringLiteral("9"));
    call("skip");
    r.check(stepIs(2) && page->property("fDays").toString().isEmpty(), QStringLiteral("跳过：清掉这一步填的，到第 3 步"));
    call("back");
    r.check(stepIs(1) && page->property("fFrom").toString() == QStringLiteral("上海"), QStringLiteral("上一步回到第 2 步，前面填的还在"), QStringLiteral("step=%1 from=%2 box=%3").arg(page->property("step").toInt()).arg(page->property("fFrom").toString()).arg(findItem(win->contentItem(), QStringLiteral("aiPlanPage-1")) ? findItem(win->contentItem(), QStringLiteral("aiPlanPage-1"))->isVisible() : -1));
    page->setProperty("fDays", QStringLiteral("5"));
    call("next");
    page->setProperty("fPeople", QStringLiteral("2"));
    page->setProperty("fBudget", QStringLiteral("8000"));
    call("next");
    r.check(stepIs(3), QStringLiteral("第 4 步（节奏、兴趣）"));
    page->setProperty("fPace", QStringLiteral("slow"));
    QMetaObject::invokeMethod(page, "toggleInterest", Q_ARG(QVariant, QStringLiteral("美食")));
    QMetaObject::invokeMethod(page, "toggleInterest", Q_ARG(QVariant, QStringLiteral("摄影")));
    QMetaObject::invokeMethod(page, "toggleInterest", Q_ARG(QVariant, QStringLiteral("摄影")));
    r.check(page->property("fInterests").toStringList() == QStringList{ QStringLiteral("美食") }, QStringLiteral("兴趣可多选，再点一下取消"));
    call("next");
    r.check(stepIs(4), QStringLiteral("第 5 步（必去 / 不去）"));
    page->setProperty("fMust", QStringLiteral("伏见稻荷"));

    // 发送前检查：到上限、离线 → 留在向导页说明，不发
    const int req0 = fake.requests;
    ai->setCap(0.0000001);
    st->setValue(QStringLiteral("aiUsage"), json::parse(QStringLiteral(R"({"m":"%1","by":{"claude":{"amt":1,"cur":"USD","n":1,"tok":1}}})").arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM")))));
    call("next");
    r.check(page->property("blockText").toString().contains(QStringLiteral("上限")) && page->property("shown").toBool() && fake.requests == req0, QStringLiteral("到上限：向导里说明、不发、不离开"), page->property("blockText").toString());
    st->setValue(QStringLiteral("aiUsage"), json::Value());
    ai->setCap(1000);
    net->setOffline(true);
    call("next");
    r.check(page->property("blockText").toString().contains(QStringLiteral("离线")) && fake.requests == req0, QStringLiteral("离线：向导里说明、不发"), page->property("blockText").toString());
    net->setOffline(false);

    // 提交（第 5 步点跳过 = 必去清掉再提交）：读计划 → 自动续轮 → propose_changes 出新计划
    ai->send(QStringLiteral("先说一句（让对话不是空的）")); idle();
    const QString oldChat = ai->chatId();
    fake.bodies.clear();
    const QJsonArray wiz{ J({ { "op", "create_plan" }, { "new_id", "new1" }, { "name", QStringLiteral("京都五日") }, { "status", "planning" }, { "guide", QStringLiteral("粗略攻略") } }),
                          J({ { "op", "set_days" }, { "plan_id", "new1" }, { "days", 5 } }),
                          J({ { "op", "add_stop" }, { "plan_id", "new1" }, { "name", QStringLiteral("Kyoto") }, { "lat", 35.0 }, { "lng", 135.76 }, { "legMode", "train" } }) };
    fake.script = { anthTool(QStringLiteral("toolu_wr"), QStringLiteral("get_plans"), J({ { "plan_ids", QJsonArray{ "p_ord" } } }), false),
                    anthTool(QStringLiteral("toolu_wp"), QStringLiteral("propose_changes"), J({ { "summary", QStringLiteral("新建京都五日") }, { "changes", wiz } }), false, QStringLiteral("每天一行的粗略方案。")) };
    call("skip");
    r.check(idle() && fake.bodies.size() == 2, QStringLiteral("向导提交后发出去（读计划后自动续一轮）"), QString::number(fake.bodies.size()));
    const QString wmsg = lastUserBody(fake.bodies.value(0));
    r.check(wmsg.contains(QStringLiteral("出发地：上海")) && wmsg.contains(QStringLiteral("目的地：京都")) && wmsg.contains(QStringLiteral("天数：5 天")) && wmsg.contains(QStringLiteral("人数：2 人"))
            && wmsg.contains(QStringLiteral("预算：约")) && wmsg.contains(QStringLiteral("8000")) && wmsg.contains(QStringLiteral("节奏：松")) && wmsg.contains(QStringLiteral("兴趣：美食"))
            && !wmsg.contains(QStringLiteral("必去")) && wmsg.contains(QStringLiteral("propose_changes")) && wmsg.contains(QStringLiteral("只问一个")),
            QStringLiteral("发给 AI 的消息：填了的每一项、跳过的不写、只追问缺的、用 propose_changes 出新计划"), wmsg.left(300));
    const QJsonArray wsys = QJsonDocument::fromJson(fake.bodies.value(0)).object().value(QStringLiteral("system")).toArray();
    r.check(wsys.size() == 2 && wsys.at(1).toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("没有选中计划")), QStringLiteral("向导不带地图上选中的计划"));
    const QVariantList wm = ai->messages();
    r.check(ai->chatId() != oldChat && !wm.isEmpty() && wm.first().toMap().value(QStringLiteral("text")).toString().startsWith(QStringLiteral("用 AI 规划一趟新旅行")),
            QStringLiteral("开了一段新对话，对话里显示整理过的表单"), wm.isEmpty() ? QString() : wm.first().toMap().value(QStringLiteral("text")).toString());
    r.check(panel && waitFor([page, panel] { return !page->property("shown").toBool() && panel->property("open").toBool(); }, 2000), QStringLiteral("转到地图上的 AI 面板"));
    r.check(page->property("fFrom").toString().isEmpty() && page->property("step").toInt() == 0, QStringLiteral("向导清空、回到第 1 步"));
    const QVariantMap wp = lastProps(ai);
    r.check(wp.value(QStringLiteral("state")).toString() == QLatin1String("open") && wp.value(QStringLiteral("total")).toInt() == 3 && wp.value(QStringLiteral("nOk")).toInt() == 3, QStringLiteral("新计划先出预览（还没写入）"), QString::fromUtf8(QJsonDocument::fromVariant(wp).toJson(QJsonDocument::Compact)).left(200));
    QJsonArray up = usagePlans(st);
    const double sonnet = (3000 * 2.0 + 300 * 10.0) / 1e6;
    r.check(up.size() == 1 && up.at(0).toObject().value(QStringLiteral("k")) == QLatin1String("wizard") && up.at(0).toObject().value(QStringLiteral("tok")).toInteger() == 6600
            && qAbs(up.at(0).toObject().value(QStringLiteral("amt")).toDouble() - 2 * sonnet) < 1e-9 && up.at(0).toObject().value(QStringLiteral("m")) == QLatin1String("claude-sonnet-5-5"),
            QStringLiteral("方案用量：向导一次 = 两轮的 token 和花费加起来"), QString::fromUtf8(QJsonDocument(up).toJson(QJsonDocument::Compact)));
    if (panel) panel->setProperty("open", false);

    // 之后的修改也记；普通聊天不记
    fake.script = { anthTool(QStringLiteral("toolu_e1"), QStringLiteral("propose_changes"), J({ { "summary", QStringLiteral("改") }, { "plan_id", "p_ord" }, { "changes", QJsonArray{ J({ { "op", "add_todo" }, { "text", QStringLiteral("订酒店") } }) } } }), false, QStringLiteral("加了待办。")) };
    ai->send(QStringLiteral("加个待办")); idle();
    ai->send(QStringLiteral("随便聊聊")); idle();
    up = usagePlans(st);
    r.check(up.size() == 2 && up.at(0).toObject().value(QStringLiteral("k")) == QLatin1String("edit") && ai->planUsage().size() == 2
            && ai->planUsage().at(0).toMap().value(QStringLiteral("cost")).toString().startsWith(u'$'), QStringLiteral("之后提出修改的那一问也记（最新的在前）；普通聊天不记"), QString::number(up.size()));

    // ---- 从订单生成：Anthropic 强制 propose_changes、快档模型 ----
    if (PlanController *pc = plansCtl()) pc->openPlan(QStringLiteral("p_ord"), false);
    ai->newChat();
    fake.bodies.clear();
    const QJsonObject ordIn = J({ { "summary", QStringLiteral("加上航班") }, { "plan_id", "p_ord" }, { "changes", QJsonArray{ J({ { "op", "add_booking" }, { "kind", "flight" }, { "title", "NH 920" }, { "start", "2026-12-01T09:30" }, { "ref", "ABC123" } }) } } });
    fake.script = { anthTool(QStringLiteral("toolu_o1"), QStringLiteral("propose_changes"), ordIn, false) };
    r.check(ai->sendOrder(QStringLiteral("ANA NH920 上海浦东 → 东京羽田 2026-12-01 09:30 订单号 ABC123")) && idle(), QStringLiteral("从订单生成发出去了"));
    QJsonObject ob = QJsonDocument::fromJson(fake.bodies.value(0)).object();
    const QJsonArray otools = ob.value(QStringLiteral("tools")).toArray();
    r.check(otools.size() == 1 && otools.at(0).toObject().value(QStringLiteral("name")) == QLatin1String("propose_changes")
            && ob.value(QStringLiteral("tool_choice")).toObject() == J({ { "type", "tool" }, { "name", "propose_changes" } })
            && ob.value(QStringLiteral("model")).toString() == QLatin1String("claude-haiku-5-5") && !ob.contains(QStringLiteral("thinking")) && ob.value(QStringLiteral("max_tokens")).toInt() == 4000,
            QStringLiteral("Anthropic：只给 propose_changes、tool_choice 强制、快档 claude-haiku-5-5、不思考、4000 token"), QString::fromUtf8(QJsonDocument(ob.value(QStringLiteral("tool_choice")).toObject()).toJson(QJsonDocument::Compact)) + ob.value(QStringLiteral("model")).toString());
    r.check(ob.value(QStringLiteral("system")).toArray().at(1).toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("订单确认")) && lastUserBody(fake.bodies.value(0)).contains(QStringLiteral("ABC123")),
            QStringLiteral("system 加了订单任务，用户消息带订单文字"));
    const QVariantMap op = lastProps(ai);
    up = usagePlans(st);
    const double haiku = (3000 * 0.10 + 300 * 0.50) / 1e6;
    r.check(op.value(QStringLiteral("state")).toString() == QLatin1String("open") && op.value(QStringLiteral("nOk")).toInt() == 1 && up.at(0).toObject().value(QStringLiteral("k")) == QLatin1String("order")
            && up.at(0).toObject().value(QStringLiteral("m")) == QLatin1String("claude-haiku-5-5") && qAbs(up.at(0).toObject().value(QStringLiteral("amt")).toDouble() - haiku) < 1e-12,
            QStringLiteral("出交通住宿预览；按快档的价钱记一次方案用量"), QString::fromUtf8(QJsonDocument(up.at(0).toObject()).toJson(QJsonDocument::Compact)));

    // OpenAI 兼容：DeepSeek {type: function}、不思考
    ai->setProvider(QStringLiteral("deepseek"));
    fake.bodies.clear();
    fake.script = { oaTools({ { QStringLiteral("call_o2"), QStringLiteral("propose_changes"), ordIn } }) };
    ai->sendOrder(QStringLiteral("订单 ABC123")); idle();
    ob = QJsonDocument::fromJson(fake.bodies.value(0)).object();
    const QJsonObject fnChoice = J({ { "type", "function" }, { "function", J({ { "name", "propose_changes" } }) } });
    r.check(ob.value(QStringLiteral("tool_choice")).toObject() == fnChoice && ob.value(QStringLiteral("tools")).toArray().size() == 1 && ob.value(QStringLiteral("model")).toString() == QLatin1String("deepseek-flash")
            && ob.value(QStringLiteral("thinking")).toObject().value(QStringLiteral("type")).toString() == QLatin1String("disabled") && !lastProps(ai).isEmpty(),
            QStringLiteral("OpenAI 兼容（DeepSeek）：tool_choice {type: function}、快档不思考、出预览"), QString::fromUtf8(QJsonDocument(ob.value(QStringLiteral("tool_choice")).toObject()).toJson(QJsonDocument::Compact)));

    // 千问：细致档本来思考 → 从订单生成关掉思考再强制；第一次发给千问先提示，确认后仍按订单发
    ai->setProvider(QStringLiteral("qwen"));
    ai->setTier(QStringLiteral("careful"));
    ai->setKey(QStringLiteral("sk-selftest-qwen-1234"));
    fake.bodies.clear();
    fake.script = { oaTools({ { QStringLiteral("call_o3"), QStringLiteral("propose_changes"), ordIn } }) };
    r.check(!ai->sendOrder(QStringLiteral("订单 ABC123")) && ai->pendingTell() == QStringLiteral("通义千问"), QStringLiteral("第一次发给千问先提示"));
    ai->confirmTell(); idle();
    ob = QJsonDocument::fromJson(fake.bodies.value(0)).object();
    r.check(ob.value(QStringLiteral("enable_thinking")) == QJsonValue(false) && ob.value(QStringLiteral("tool_choice")).toObject() == fnChoice && ob.value(QStringLiteral("model")).toString() == QLatin1String("qwen3.8-flash")
            && lastUserBody(fake.bodies.value(0)).contains(QStringLiteral("订单确认")),
            QStringLiteral("千问：关思考（enable_thinking false）再强制 tool_choice，快档 qwen3.8-flash；确认提示后仍按订单发"), QString::fromUtf8(QJsonDocument(ob).toJson(QJsonDocument::Compact)).left(240));
    ai->setTier(QStringLiteral("balanced"));

    // Kimi：一直思考、不强制（靠提示词）；没给修改时对话里说明
    ai->setProvider(QStringLiteral("kimi"));
    ai->setKey(QStringLiteral("sk-selftest-kimi-1234"));
    fake.bodies.clear();
    fake.script = { oaTools({}, QStringLiteral("看不出订单内容。")) };
    ai->sendOrder(QStringLiteral("乱七八糟")); ai->confirmTell(); idle();
    ob = QJsonDocument::fromJson(fake.bodies.value(0)).object();
    r.check(!ob.contains(QStringLiteral("tool_choice")) && ob.value(QStringLiteral("tools")).toArray().size() == 1 && lastMsg(ai).value(QStringLiteral("text")).toString().contains(QStringLiteral("没整理出")),
            QStringLiteral("Kimi：不强制（只给 propose_changes），没给修改时说明"), lastMsg(ai).value(QStringLiteral("text")).toString());

    // 没选中计划：不发
    ai->setProvider(QStringLiteral("claude"));
    for (int i = 0; i < 6; ++i) {   // Esc 一层层关，直到取消地图上选中的计划
        QVariant name;
        QMetaObject::invokeMethod(win, "escOnce", Q_RETURN_ARG(QVariant, name));
        if (name.toString() == QLatin1String("sheet") || name.toString().isEmpty()) break;
    }
    const int req1 = fake.requests;
    r.check(!ai->sendOrder(QStringLiteral("订单")) && fake.requests == req1 && lastMsg(ai).value(QStringLiteral("text")).toString().contains(QStringLiteral("先在地图上点开")), QStringLiteral("没选中计划时不发，说先点开计划"));

    // 还原
    fake.script.clear();
    store->modify([&original](json::Value &dd) { dd = original; });
    app->commitStep();
    ai->newChat();
    Q_UNUSED(prefix);
}

} // namespace

void runAi(Report &r, AppController *app, QQuickWindow *win, services::NetClient *net)
{
    r.section(QStringLiteral("0.13.0 AI 助手（本机假服务器）"));
    AiController *ai = aiCtl();
    Settings *st = settingsObj();
    if (!r.check(ai && st, QStringLiteral("AI 控制器在"))) return;
    FakeAi fake;
    if (!r.check(fake.server.isListening(), QStringLiteral("假服务器起来了"))) return;

    // 还原用：设置里的 ai / aiUsage、联网开关
    const json::Value oldAi = st->value(QStringLiteral("ai")), oldUsage = st->value(QStringLiteral("aiUsage"));
    const bool oldOffline = net->offline(), oldOn = net->serviceEnabled(QStringLiteral("ai"));
    const QString oldChats = ai->chatsFile();
    const QString prefix = QStringLiteral("TravelPlannerNative-selftest/ai/");
    const QString chats = QDir::temp().filePath(QStringLiteral("tp-selftest-ai-chats.json"));
    QFile::remove(chats);
    st->setValue(QStringLiteral("ai"), json::Value());
    st->setValue(QStringLiteral("aiUsage"), json::Value());
    net->setOffline(false);
    net->setServiceEnabled(QStringLiteral("ai"), true);
    for (const char *id : { "claude", "deepseek", "custom", "kimi", "qwen" }) platform::credentials::remove(prefix + QLatin1String(id));
    AiController::TestHooks h;
    h.credPrefix = prefix;
    h.chatsFile = chats;
    h.baseAnthropic = fake.base("/v1");
    h.baseOpenAi = fake.base("/oa");
    h.noByteMs = 1500;
    h.noEventMs = 2500;
    ai->setTestHooks(h);
    const auto idle = [&](int ms = 5000) { return waitFor([ai] { return !ai->busy(); }, ms); };

    // ---- 钥匙 ----
    ai->setProvider(QStringLiteral("claude"));
    ai->setTier(QStringLiteral("balanced"));
    r.check(!ai->hasKey() && ai->messages().isEmpty(), QStringLiteral("干净的起点：没有钥匙、没有对话"));
    r.check(!ai->send(QStringLiteral("你好")) && lastMsg(ai).value(QStringLiteral("kind")).toString() == QLatin1String("nokey"), QStringLiteral("没钥匙时不发，对话里说明（面板上是引导卡）"));
    r.check(!ai->setKey(QStringLiteral("sk-proj-abcdefghijklmnopqrstuvwxyz")).isEmpty() && !ai->hasKey(), QStringLiteral("Claude 钥匙不是 sk-ant- 开头时拒收"));
    const QString key = QStringLiteral("sk-ant-selftest-abcdefghijklmnopqrstu-9z8y");
    r.check(ai->setKey(key).isEmpty() && ai->hasKey() && ai->keyTails().value(QStringLiteral("claude")).toString() == QLatin1String("9z8y"), QStringLiteral("存好钥匙，显示末 4 位"));
    r.check(platform::credentials::read(prefix + QStringLiteral("claude")) == key, QStringLiteral("凭据管理器里读得回来"));
    const QByteArray settingsText = json::stringify(st->value(QStringLiteral("ai"))).toUtf8() + json::stringify(st->value(QStringLiteral("aiUsage"))).toUtf8();
    r.check(!settingsText.contains(key.toUtf8()), QStringLiteral("钥匙不进 settings.json"));

    // ---- 第一次发给这家：先提示 ----
    QVariantList before = ai->messages();
    r.check(!ai->send(QStringLiteral("你好")) && ai->pendingTell() == QLatin1String("Claude") && fake.requests == 0, QStringLiteral("第一次发给 Claude 前先提示，没发出去"));
    ai->confirmTell();
    r.check(ai->busy() && ai->pendingTell().isEmpty(), QStringLiteral("确认后发出去"));
    r.check(idle(), QStringLiteral("回答收完"));
    QJsonObject am = lastOf(ai, "assistant");
    r.check(am.value(QStringLiteral("text")).toString() == QStringLiteral("你好，测试") && am.value(QStringLiteral("error")).toString().isEmpty(), QStringLiteral("Anthropic 流式：文字拼起来了"), am.value(QStringLiteral("text")).toString());
    r.check(fake.lastKey == key.toUtf8() && fake.lastPath.endsWith("/v1/messages"), QStringLiteral("请求带钥匙、发到 /v1/messages"));
    const QJsonObject body1 = QJsonDocument::fromJson(fake.lastBody).object();
    r.check(body1.value(QStringLiteral("model")).toString() == QLatin1String("claude-sonnet-5-5") && !body1.contains(QStringLiteral("thinking")) && body1.value(QStringLiteral("stream")).toBool(),
            QStringLiteral("均衡档 = claude-sonnet-5-5、不思考、流式"));
    // 用量：输入 1000 + 缓存读 500 + 输出 200 → 1000×2 + 500×0.10 + 200×10（每百万 token）
    const double want = (1000 * 2 + 500 * 0.10 + 200 * 10) / 1e6;
    r.check(qAbs(am.value(QStringLiteral("cost")).toDouble() - want) < 1e-9 && am.value(QStringLiteral("cur")).toString() == QLatin1String("USD"),
            QStringLiteral("按 usage 计费（含缓存读）"), QString::number(am.value(QStringLiteral("cost")).toDouble(), 'g', 8));
    r.check(am.value(QStringLiteral("meta")).toString().contains(QStringLiteral("$")) && am.value(QStringLiteral("meta")).toString().contains(QStringLiteral("1,500")), QStringLiteral("回复下面写 token 和花费"), am.value(QStringLiteral("meta")).toString());
    r.check(ai->used() > 0 && !ai->usageRows().isEmpty(), QStringLiteral("记进本月用量（分家）"), QString::number(ai->used()));

    // 第二句不再提示；细致档带 adaptive 思考
    ai->setTier(QStringLiteral("careful"));
    r.check(ai->send(QStringLiteral("第二句")) && idle(), QStringLiteral("同一家第二次不再提示"));
    const QJsonObject body2 = QJsonDocument::fromJson(fake.lastBody).object();
    r.check(body2.value(QStringLiteral("thinking")).toObject().value(QStringLiteral("type")).toString() == QLatin1String("adaptive")
            && body2.value(QStringLiteral("model")).toString() == QLatin1String("claude-opus-5-5") && body2.value(QStringLiteral("messages")).toArray().size() == 3,
            QStringLiteral("细致档 = claude-opus-5-5 + adaptive 思考，带上前面的对话"));
    ai->setTier(QStringLiteral("balanced"));

    // ---- 截断、停止、卡住、401 ----
    ai->send(QStringLiteral("TRUNC")); idle();
    r.check(lastMsg(ai).value(QStringLiteral("role")).toString() == QLatin1String("note") && lastMsg(ai).value(QStringLiteral("text")).toString().contains(QStringLiteral("截断")), QStringLiteral("max_tokens 截断时提示"));
    ai->send(QStringLiteral("STOP"));
    r.check(waitFor([ai] { return ai->liveText().contains(QStringLiteral("你好")); }, 3000), QStringLiteral("流式中能看到写了一半的回答"));
    ai->stop();
    r.check(idle(2000) && lastOf(ai, "assistant").value(QStringLiteral("error")).toString() == QStringLiteral("已停止") && lastOf(ai, "assistant").value(QStringLiteral("text")).toString() == QStringLiteral("你好，"),
            QStringLiteral("■ 停止：留下已写的，标“已停止”"));
    ai->send(QStringLiteral("STALL"));
    r.check(idle(5000) && lastOf(ai, "assistant").value(QStringLiteral("error")).toString().contains(QStringLiteral("卡住")), QStringLiteral("没字节到时限自动停下（90 秒，自检缩成 1.5 秒）"), lastOf(ai, "assistant").value(QStringLiteral("error")).toString());
    ai->send(QStringLiteral("PING"));
    r.check(idle(7000) && lastOf(ai, "assistant").value(QStringLiteral("error")).toString().contains(QStringLiteral("很久没有收到内容")), QStringLiteral("有 ping 没事件到时限也停（150 秒保险，自检缩成 2.5 秒）"), lastOf(ai, "assistant").value(QStringLiteral("error")).toString());
    ai->send(QStringLiteral("E401")); idle();
    r.check(lastOf(ai, "assistant").value(QStringLiteral("error")).toString().contains(QStringLiteral("钥匙不对")), QStringLiteral("401 时说钥匙不对"));

    // ---- 模型列表 ----
    ai->refreshModels();
    r.check(waitFor([ai] { return ai->modelsState() != QLatin1String("busy"); }, 3000) && ai->modelList().contains(QStringLiteral("claude-test-1")), QStringLiteral("Claude 从接口刷新模型列表"));
    ai->setManualModel(QStringLiteral("claude-test-2"));
    r.check(ai->model() == QLatin1String("claude-test-2"), QStringLiteral("手填 / 点选模型后用它"));
    ai->setManualModel(QString());

    // ---- OpenAI 兼容（DeepSeek）----
    ai->setProvider(QStringLiteral("deepseek"));
    ai->setKey(QStringLiteral("sk-selftest-deepseek-1234"));
    r.check(!ai->send(QStringLiteral("换一家")) && ai->pendingTell() == QLatin1String("DeepSeek"), QStringLiteral("换一家再提示一次"));
    ai->confirmTell();
    idle();
    am = lastOf(ai, "assistant");
    r.check(am.value(QStringLiteral("text")).toString() == QStringLiteral("答案") && am.value(QStringLiteral("thinking")).toString() == QStringLiteral("推理"), QStringLiteral("OpenAI 兼容流式：content 和 reasoning_content"), QString::fromUtf8(QJsonDocument(am).toJson(QJsonDocument::Compact)));
    r.check(fake.lastAuth == "Bearer sk-selftest-deepseek-1234" && fake.lastPath.endsWith("/oa/chat/completions"), QStringLiteral("Bearer 钥匙、发到 /chat/completions"));
    const QJsonObject body3 = QJsonDocument::fromJson(fake.lastBody).object();
    r.check(body3.value(QStringLiteral("thinking")).toObject().value(QStringLiteral("type")).toString() == QLatin1String("enabled") && body3.value(QStringLiteral("model")).toString() == QLatin1String("deepseek-flash")
            && body3.value(QStringLiteral("stream_options")).toObject().value(QStringLiteral("include_usage")).toBool(), QStringLiteral("DeepSeek 均衡档 = deepseek-flash + 思考，要用量"));
    const double wantCny = (1000 * 2 + 1000 * 0.04 + 100 * 8) / 1e6;
    r.check(qAbs(am.value(QStringLiteral("cost")).toDouble() - wantCny) < 1e-9 && am.value(QStringLiteral("cur")).toString() == QLatin1String("CNY") && am.value(QStringLiteral("meta")).toString().contains(QStringLiteral("¥")),
            QStringLiteral("按人民币计价（缓存命中价另算）"), am.value(QStringLiteral("meta")).toString());
    const double cnyMain = ai->toMain(1, QStringLiteral("CNY")), usdMain = ai->toMain(1, QStringLiteral("USD"));
    r.check(cnyMain > 0 && usdMain > 0 && usdMain > cnyMain, QStringLiteral("各家币种折成主币种"), QStringLiteral("%1 1 CNY = %2，1 USD = %3").arg(ai->mainCur()).arg(cnyMain).arg(usdMain));
    r.check(ai->usageRows().size() == 2, QStringLiteral("本月用量分两家"));

    // 自定义：拉不到列表只给手填；没填单价只记 token
    ai->setProvider(QStringLiteral("custom"));
    ai->setKey(QStringLiteral("custom-key-0000"));
    ai->setCustom(QStringLiteral("model"), QStringLiteral("my-model"));
    ai->refreshModels();
    r.check(waitFor([ai] { return ai->modelsState() != QLatin1String("busy"); }, 3000) && ai->modelList().isEmpty() && ai->modelsState().contains(QStringLiteral("手填")), QStringLiteral("拉不到模型列表时说可以手填"), ai->modelsState());
    ai->send(QStringLiteral("自定义")); ai->confirmTell(); idle();
    am = lastOf(ai, "assistant");
    r.check(am.value(QStringLiteral("text")).toString() == QStringLiteral("答案") && !am.contains(QStringLiteral("cost")) && am.value(QStringLiteral("meta")).toString().contains(QStringLiteral("只记 token")),
            QStringLiteral("自定义没填单价：能聊，只记 token"), am.value(QStringLiteral("meta")).toString());
    ai->setProvider(QStringLiteral("claude"));

    // ---- 80% 提醒、到上限停下、当场提高 ----
    st->setValue(QStringLiteral("aiUsage"), json::Value());
    const double c = ai->toMain(want, QStringLiteral("USD"));
    ai->setCap(c / 0.9);   // 发一句后到约 90%
    int warned = 0;
    const auto wc = QObject::connect(ai, &AiController::capWarned, ai, [&warned] { ++warned; });
    ai->send(QStringLiteral("再来")); idle();
    QObject::disconnect(wc);
    r.check(warned == 1 && lastMsg(ai).value(QStringLiteral("text")).toString().contains(QStringLiteral("80%")), QStringLiteral("过 80% 提醒一次"), QStringLiteral("warned=%1 cap=%2 used=%3 last=%4").arg(warned).arg(ai->cap()).arg(ai->used()).arg(lastMsg(ai).value(QStringLiteral("text")).toString()));
    ai->setCap(c * 0.5);
    const int reqBefore = fake.requests;
    r.check(ai->capReached() && !ai->send(QStringLiteral("到上限了")) && fake.requests == reqBefore && lastMsg(ai).value(QStringLiteral("kind")).toString() == QLatin1String("cap"), QStringLiteral("到上限停下，没发出去"));
    ai->raiseCap();
    r.check(!ai->capReached() && ai->send(QStringLiteral("提高以后")) && idle(), QStringLiteral("当场提高上限后能接着发"));

    // ---- 离线 ----
    net->setOffline(true);
    r.check(!ai->send(QStringLiteral("离线")) && lastMsg(ai).value(QStringLiteral("text")).toString().contains(QStringLiteral("离线")), QStringLiteral("离线时不发、说明原因"));
    net->setOffline(false);

    // ---- 历史 ----
    const QString first = ai->chatId();
    r.check(!first.isEmpty() && ai->chats().size() == 1, QStringLiteral("对话存进历史"));
    ai->newChat();
    r.check(ai->messages().isEmpty() && ai->chatId().isEmpty(), QStringLiteral("新对话是空的"));
    ai->send(QStringLiteral("另一段")); idle();
    r.check(ai->chats().size() == 2, QStringLiteral("历史里有两段"));
    ai->openChat(first);
    r.check(ai->chatId() == first && ai->messages().size() > 5, QStringLiteral("打开旧的一段，内容回来了"));
    ai->setChatsFile(chats);   // 等于重新启动：接着最近一段
    r.check(ai->chatId() == first, QStringLiteral("启动接着最近一段"));

    // ---- 面板：Enter 发送、Shift+Enter 换行、输入法组字时 Enter 不发 ----
    QQuickItem *panel = findItem(win->contentItem(), QStringLiteral("aiPanel"));
    QQuickItem *btn = findItem(win->contentItem(), QStringLiteral("aiBtn"));
    QMetaObject::invokeMethod(win, "goPage", Q_ARG(QVariant, QStringLiteral("map")));
    if (panel) panel->setProperty("open", true);
    QQuickItem *input = findItem(win->contentItem(), QStringLiteral("aiInput"));
    QQuickItem *edit = nullptr;
    if (input) for (QQuickItem *it : input->findChildren<QQuickItem *>()) if (QByteArray(it->metaObject()->className()).contains("TextEdit")) { edit = it; break; }
    r.check(panel && btn && waitFor([panel] { return panel->isVisible() && panel->opacity() > 0.9; }, 1500) && edit, QStringLiteral("AI 面板打开了，有输入框"));
    if (edit) {
        ai->newChat();
        edit->forceActiveFocus();
        const auto press = [&](int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
            QKeyEvent pe(QEvent::KeyPress, k, m), re(QEvent::KeyRelease, k, m);
            QCoreApplication::sendEvent(edit, &pe);
            QCoreApplication::sendEvent(edit, &re);
        };
        edit->setProperty("text", QStringLiteral("第一行"));
        press(Qt::Key_Return, Qt::ShiftModifier);
        r.check(!ai->busy() && edit->property("text").toString().contains(u'\n'), QStringLiteral("Shift+Enter 换行，不发"));
        QInputMethodEvent pre(QStringLiteral("ni"), {});
        QCoreApplication::sendEvent(edit, &pre);
        const bool composing = edit->property("inputMethodComposing").toBool();
        press(Qt::Key_Return);
        r.check(composing && !ai->busy() && ai->messages().isEmpty(), QStringLiteral("输入法组字时 Enter 不发"));
        QInputMethodEvent commit(QString(), {});
        commit.setCommitString(QString());
        QCoreApplication::sendEvent(edit, &commit);
        press(Qt::Key_Return);
        r.check(waitFor([ai] { return !ai->messages().isEmpty(); }, 1500) && idle() && edit->property("text").toString().isEmpty(), QStringLiteral("Enter 发送，输入框清空"));
    }
    if (panel) panel->setProperty("open", false);

    runTools(r, app, ai, fake, h, chats);   // 0.13.1
    runPlanWizard(r, app, ai, fake, win, net, st, key, prefix);   // 0.13.2

    // 收尾：删掉测试钥匙、还原设置
    for (const char *id : { "claude", "deepseek", "custom", "kimi", "qwen" }) platform::credentials::remove(prefix + QLatin1String(id));
    st->setValue(QStringLiteral("ai"), oldAi);
    st->setValue(QStringLiteral("aiUsage"), oldUsage);
    st->save();
    net->setOffline(oldOffline);
    net->setServiceEnabled(QStringLiteral("ai"), oldOn);
    AiController::TestHooks back;
    back.credPrefix = QStringLiteral("TravelPlannerNative-test/ai/");
    back.chatsFile = oldChats;
    ai->setTestHooks(back);
    ai->setProvider(QStringLiteral("claude"));
    QFile::remove(chats);
    for (auto &s : fake.held) if (s) s->abort();
}

} // namespace tp::app::selftest
