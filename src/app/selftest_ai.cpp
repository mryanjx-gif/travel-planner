// 0.13.0 AI 助手的自检：本机假服务器（QTcpServer）模拟 Anthropic 和 OpenAI 兼容两种流式回答，不花钱、不联网。
// 测：钥匙（格式、凭据管理器读回、清除）、没钥匙 / 离线 / 到上限拦住、第一次发给某家的提示、流式回答和计费、
// 截断提示、停止、90 秒没字节（缩短）、150 秒没事件的保险（缩短）、401、模型列表（拉到 / 拉不到）、80% 提醒、当场提高上限、
// 历史（新对话、打开旧的）、面板上的 Enter / Shift+Enter / 输入法组字。钥匙用单独的目标名，跑完删掉；设置跑完还原。
#include "selftest_util.h"

#include "aicontroller.h"
#include "appcontroller.h"
#include "credentials.h"
#include "jsonio.h"
#include "netclient.h"
#include "settings.h"

#include <QCoreApplication>
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
        if (path.endsWith("/v1/models?limit=100")) { sendJson(s, 200, R"({"data":[{"id":"claude-test-1"},{"id":"claude-test-2"}]})"); return; }
        if (path.endsWith("/oa/models")) { sendJson(s, 404, R"({"error":{"message":"no list"}})"); return; }
        const QJsonArray msgs = QJsonDocument::fromJson(body).object().value(QStringLiteral("messages")).toArray();
        const QString last = msgs.isEmpty() ? QString() : msgs.last().toObject().value(QStringLiteral("content")).toString();
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

} // namespace

void runAi(Report &r, AppController *app, QQuickWindow *win, services::NetClient *net)
{
    Q_UNUSED(app);
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
    for (const char *id : { "claude", "deepseek", "custom" }) platform::credentials::remove(prefix + QLatin1String(id));
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

    // 收尾：删掉测试钥匙、还原设置
    for (const char *id : { "claude", "deepseek", "custom" }) platform::credentials::remove(prefix + QLatin1String(id));
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
