// AI 助手（0.13.0；QML 里是 ai）。PO 2026-11-08 的选择见 claude/阶段7设计预案.md §〇。
// 0.13.1 改计划：上下文（规则 + 计划简表 + 现在的情况 + 当前计划）、工具（读计划、查找进度、提修改、联网搜索）、自动续轮（最多 12 轮）、
// 修改预览（勾选、核坐标、应用前备份、应用、撤销联动）、查找的时间预算。纯计算在 src/ai/aitools.*。
// 历史里助手的话存原样内容（api = anthropic：content 块含思考 signature；openai：oa{content, reasoning_content, tool_calls}）、工具结果 results、预览 props；
// 0.13.0 存的旧历史只有 text，照样能读，只带文字回传。
// 这一批：钥匙（凭据管理器）、服务商和三档、模型列表、流式聊天（不带工具、不改计划）、停止、卡住检测、
// 费用和月上限、对话历史、第一次发给某家前的提示、麦克风（替用户按 Win+H）。
// 设置存 settings.json：ai（服务商、档位、手填模型、地址、联网搜索开关、月上限、提示过的服务商、自定义）、aiUsage（本月用量，分家）。
// 对话存在 settings.json 旁边的 ai-chats.json（最多 8 段、约 2 MB；Electron 存 localStorage）。钥匙不进这两个文件。
#pragma once

#include "aiclient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace tp::services { class NetClient; class Geocoder; }
namespace tp::data { class DataStore; }
namespace tp::json { class Value; }

namespace tp::app {

class Settings;
class RatesController;
class AppController;

class AiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList providers READ providers CONSTANT)
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY changed)
    Q_PROPERTY(QString providerName READ providerName NOTIFY changed)
    Q_PROPERTY(QString tier READ tier WRITE setTier NOTIFY changed)
    Q_PROPERTY(QVariantList tiers READ tiers NOTIFY changed)            // 当前服务商的三档：key、model、think、est
    Q_PROPERTY(QString manualModel READ manualModel WRITE setManualModel NOTIFY changed)   // 空 = 跟着档位
    Q_PROPERTY(QString model READ model NOTIFY changed)                 // 实际用的模型
    Q_PROPERTY(QString base READ base WRITE setBase NOTIFY changed)     // 地址（空 = 默认；自定义必须填）
    Q_PROPERTY(QString defaultBase READ defaultBase NOTIFY changed)
    Q_PROPERTY(QVariantMap custom READ custom NOTIFY changed)           // 自定义：model、pin、pout（每百万 token，空 = 只记 token）、cur
    Q_PROPERTY(QVariantMap keyTails READ keyTails NOTIFY keysChanged)   // 服务商 → 钥匙末 4 位（没有 = 不在里面）
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY keysChanged)
    Q_PROPERTY(bool web READ web WRITE setWeb NOTIFY changed)
    Q_PROPERTY(bool canWeb READ canWeb NOTIFY changed)                  // 这家能不能联网搜索（DeepSeek、豆包、自定义不能）
    Q_PROPERTY(bool noMoney READ noMoney WRITE setNoMoney NOTIFY changed)   // 发给 AI 的计划不带消费、预算、订单号和金额（默认开）
    Q_PROPERTY(QString mainCur READ mainCur NOTIFY usageChanged)
    Q_PROPERTY(double cap READ cap WRITE setCap NOTIFY usageChanged)    // 月上限（主币种）
    Q_PROPERTY(double used READ used NOTIFY usageChanged)               // 本月已用（主币种）
    Q_PROPERTY(QVariantList usageRows READ usageRows NOTIFY usageChanged)   // 分家：name、text
    Q_PROPERTY(bool capReached READ capReached NOTIFY usageChanged)
    Q_PROPERTY(QStringList modelList READ modelList NOTIFY modelsChanged)
    Q_PROPERTY(QString modelsState READ modelsState NOTIFY modelsChanged)   // "" / busy / ok / 失败原因
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString liveText READ liveText NOTIFY liveChanged)       // 正在写的回答（已转成富文本）
    Q_PROPERTY(QString liveThinking READ liveThinking NOTIFY liveChanged)
    Q_PROPERTY(QString runText READ runText NOTIFY liveChanged)         // 输入框上方：已用时间、已写字数、多久没有新内容
    Q_PROPERTY(QString pendingTell READ pendingTell NOTIFY tellChanged) // 第一次发给这家前的提示（服务商名字；空 = 没有）
    Q_PROPERTY(QVariantList chats READ chats NOTIFY chatsChanged)
    Q_PROPERTY(QString chatId READ chatId NOTIFY chatsChanged)
    Q_PROPERTY(QString convCost READ convCost NOTIFY messagesChanged)   // 这段对话约多少（主币种）

public:
    AiController(Settings *settings, services::NetClient *net, RatesController *rates, QObject *parent = nullptr);

    QVariantList providers() const;
    QString provider() const { return m_provider; }
    void setProvider(const QString &id);
    QString providerName() const;
    QString tier() const { return m_tier; }
    void setTier(const QString &t);
    QVariantList tiers() const;
    QString manualModel() const;
    void setManualModel(const QString &m);
    QString model() const;
    QString base() const;
    void setBase(const QString &b);
    QString defaultBase() const;
    QVariantMap custom() const;
    QVariantMap keyTails() const { return m_tails; }
    bool hasKey() const { return m_tails.contains(m_provider); }
    bool web() const;
    void setWeb(bool on);
    bool canWeb() const;
    bool noMoney() const;
    void setNoMoney(bool on);
    // 0.13.1 改计划要用的：数据、保存和撤销、地名搜索；where() 返回 { page, planId（详情页打开的 / 地图上选中的）, today }；
    // onDeleted：应用时删掉了计划（地图上选中的是它就取消选中）
    void setPlanContext(AppController *app, data::DataStore *store, services::Geocoder *geocoder,
                        std::function<QVariantMap()> where, std::function<void(const QStringList &)> onDeleted);
    QString mainCur() const;
    double cap() const;
    void setCap(double v);
    double used() const;
    QVariantList usageRows() const;
    bool capReached() const { return cap() > 0 && used() >= cap(); }
    QStringList modelList() const { return m_models; }
    QString modelsState() const { return m_modelsState; }
    QVariantList messages() const;
    bool busy() const { return m_client.busy(); }
    QString liveText() const;
    QString liveThinking() const { return m_client.state().thinking; }
    QString runText() const;
    QString pendingTell() const;
    QVariantList chats() const;
    QString chatId() const { return m_chatId; }
    QString convCost() const;

    // 钥匙：返回空 = 存好了，否则是原因
    Q_INVOKABLE QString setKey(const QString &key);
    Q_INVOKABLE void clearKey();
    Q_INVOKABLE void setCustom(const QString &field, const QString &value);
    Q_INVOKABLE void refreshModels();
    // 发一句话：返回 false = 没发（没钥匙、离线、到上限会在对话里说明；第一次发给这家时先等确认）
    Q_INVOKABLE bool send(const QString &text);
    Q_INVOKABLE void confirmTell();
    Q_INVOKABLE void cancelTell();
    Q_INVOKABLE void stop();
    // 修改预览（mi = 消息序号，pi = 这条回答里的第几份预览，k = 第几项）
    Q_INVOKABLE void toggleChange(int mi, int pi, int k);
    Q_INVOKABLE void toggleAll(int mi, int pi);
    Q_INVOKABLE void pickGeo(int mi, int pi, int k, int j);   // j = -1：用 AI 给的坐标
    Q_INVOKABLE void applyProposal(int mi, int pi, bool force = false);   // force：备份失败也应用
    Q_INVOKABLE void skipProposal(int mi, int pi);
    Q_INVOKABLE void toggleGap(int mi, int pi, int j);
    Q_INVOKABLE void gapsFill(int mi, int pi);
    Q_INVOKABLE void gapsSkip(int mi, int pi);
    Q_INVOKABLE void researchAnswer(int mi, bool more);   // 到预计时间：继续查 / 先到这里
    Q_INVOKABLE void newChat();
    Q_INVOKABLE void openChat(const QString &id);
    Q_INVOKABLE void raiseCap();                  // 到上限时当场提高（加约 $5）
    Q_INVOKABLE bool voice();                     // 替用户按 Win+H（Windows 自带的语音输入）
    Q_INVOKABLE void openSignup(const QString &id);
    Q_INVOKABLE QString money(double amount, const QString &cur) const;   // "¥0.03" / "$0.012"

    // 截图页用（只在 --screenshot 时由 Main.qml 调；钥匙用测试的目标名，用量只改内存不存盘）
    Q_INVOKABLE void shotSetup(const QString &page);

    // ---- 自检用 ----
    struct TestHooks { QString credPrefix, chatsFile, baseAnthropic, baseOpenAi; int noByteMs = 0, noEventMs = 0; double budgetMul = 1; };   // budgetMul：查找预算的秒数乘它（自检缩短）
    void setTestHooks(const TestHooks &h);
    double toMain(double amount, const QString &cur) const;   // 换不了时 < 0
    QString chatsFile() const { return m_chatsFile; }
    void setChatsFile(const QString &f);

signals:
    void changed();
    void keysChanged();
    void usageChanged();
    void modelsChanged();
    void messagesChanged();
    void busyChanged();
    void liveChanged();
    void tellChanged();
    void chatsChanged();
    void capWarned(double used, double cap);   // 本月第一次过 80%
    void toast(const QString &text, bool error);

private:
    QJsonObject conf() const;
    void setConf(const QString &key, const QJsonValue &v);
    QJsonObject usageObj() const;   // 本月的（换月了是空的）
    void loadKeys();
    QString credTarget(const QString &id) const { return m_credPrefix + id; }
    QString keyFor(const QString &id) const;
    bool think() const;
    QString effort() const;
    const ai::Price *price(ai::Price *customBuf) const;
    void note(const QString &text, const QString &kind = QString());
    bool sendWith(const QString &text, const QString &shown, bool gapFill, bool keepResearch);
    void doSend(const QString &text, const QString &shown, bool gapFill, bool keepResearch);
    void run();
    QJsonArray apiHistory(ai::Kind kind) const;
    QString currentPlanId() const;
    const json::Value &root() const;
    void drainTools();
    void prepareProposal(int mi, const QString &toolId, const QJsonObject &input, bool truncated);
    QJsonObject *propRef(int mi, int pi, QJsonObject &am, QJsonArray &props);
    void putProp(int mi, int pi, QJsonObject am, QJsonArray props, const QJsonObject &P, bool save);
    void nextGeo();
    QString onResearch(QJsonObject &am, const QString &id, const QJsonObject &x, QJsonObject &results);
    void onUndo(int dir);
    static QJsonArray restored(const QJsonArray &msgs);
    void onFinished(const QString &error, bool aborted, bool stalled);
    void record(const QString &providerId, double amount, const QString &cur, qint64 tokens);
    void loadChats();
    void saveChat();
    QJsonArray readChats() const;

    Settings *m_settings;
    services::NetClient *m_net;
    RatesController *m_rates;
    ai::AiClient m_client;
    QString m_provider, m_tier;
    QVariantMap m_tails;
    QStringList m_models;
    QString m_modelsState;
    QJsonArray m_msgs;        // {role: user/assistant/note, text, thinking, provider, model, meta, error, kind}
    QString m_chatId;
    QString m_pending;        // 等确认"会发给 X"的那句话
    QString m_credPrefix = QStringLiteral("TravelPlannerNative/ai/");
    QString m_chatsFile;
    TestHooks m_hooks;
    QTimer m_tick;            // 每秒刷新计时；150 秒没有新事件就停下（保险）
    QTimer m_liveThrottle;
    qint64 m_lastEventMs = 0;
    bool m_guardStalled = false;
    // 0.13.1
    AppController *m_app = nullptr;
    data::DataStore *m_store = nullptr;
    services::Geocoder *m_geocoder = nullptr;
    std::function<QVariantMap()> m_where;
    std::function<void(const QStringList &)> m_onDeleted;
    struct Run { qint64 t0 = 0; int autoN = 0; bool gapFill = false; QString planId; } m_run;   // 这一问（含自动续轮）
    struct Research { bool on = false; qint64 start = 0, budget = 0; QStringList todo; int secs = 0; bool paused = false; } m_research;
    struct Last { bool valid = false; QString chat; int mi = -1, pi = -1, mark = 0; QString result; } m_last;   // 最近一次应用（撤销联动）
    struct GeoJob { int gen; int mi; QString toolId; int k; };
    QList<GeoJob> m_geoQueue;
    bool m_geoBusy = false;
    int m_chatGen = 0;        // 换对话 +1：丢掉旧对话的坐标核对结果
    int m_doneSeen = 0;       // 流式中已经处理过的工具块
    QJsonObject m_propBuf;
    QString m_shotRun;        // 截图用：输入框上方那一行
};

} // namespace tp::app
