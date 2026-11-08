// 发一次 AI 请求（0.13.0，I-02 / I-07；Electron main.js ai:start / ai:abort / ai:models）。Qt Network，不加新库。
// 流式：边收边按 SSE 拆、喂给 StreamState，每收到一批发 updated()；结束发 finished()。
// 卡住：noByteMs（默认 90 秒）一个字节都没收到就停下，finished 里 stalled = true（服务器平时会隔一会儿发 ping）。
// 不经 NetClient（那边只有 GET、主机白名单固定）；离线 / "AI 助手"开关由调用方（AiController）先查。
#pragma once

#include "aistream.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace tp::ai {

class AiClient : public QObject
{
    Q_OBJECT
public:
    explicit AiClient(QObject *parent = nullptr);
    void setUserAgent(const QByteArray &ua) { m_ua = ua; }
    void setNoByteMs(int ms) { m_noByteMs = ms; }   // 测试用：缩短

    void start(const Request &r, Kind kind);
    void abort();                                  // 用户点了停止
    bool busy() const { return m_reply != nullptr; }
    const StreamState &state() const { return m_state; }
    qint64 sinceBytesMs() const { return m_lastByte.isValid() ? m_lastByte.elapsed() : 0; }

    // 普通 GET（模型列表）。status 0 = 没连上（netError 写原因）
    void get(const Request &r, std::function<void(int status, const QByteArray &body, const QString &netError)> done);

signals:
    void updated();
    void finished(const QString &error, bool aborted, bool stalled);

private:
    void onData();
    void onDone();
    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
    QTimer *m_stall;
    SseParser m_sse;
    StreamState m_state;
    Kind m_kind = Kind::Anthropic;
    QByteArray m_ua, m_errBody;
    QElapsedTimer m_lastByte;
    int m_noByteMs = 90000;
    bool m_aborted = false, m_stalled = false;
};

} // namespace tp::ai
