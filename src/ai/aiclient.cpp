#include "aiclient.h"

#include "translator.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace tp::ai {

namespace {
QNetworkRequest makeRequest(const Request &r, const QByteArray &ua)
{
    QNetworkRequest q(r.url);
    for (const auto &h : r.headers) q.setRawHeader(h.first, h.second);
    if (!ua.isEmpty()) q.setHeader(QNetworkRequest::UserAgentHeader, ua);
    q.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return q;
}
} // namespace

AiClient::AiClient(QObject *parent) : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_stall(new QTimer(this))
{
    m_stall->setSingleShot(true);
    connect(m_stall, &QTimer::timeout, this, [this] {
        if (!m_reply) return;
        m_stalled = true;
        m_reply->abort();
    });
}

void AiClient::start(const Request &r, Kind kind)
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); }
    m_kind = kind;
    m_state = StreamState();
    m_sse.reset();
    m_errBody.clear();
    m_aborted = m_stalled = false;
    QNetworkRequest q = makeRequest(r, m_ua);
    q.setRawHeader("accept", "text/event-stream");
    m_reply = m_nam->post(q, r.body);
    m_lastByte.start();
    m_stall->start(m_noByteMs);
    connect(m_reply, &QNetworkReply::readyRead, this, &AiClient::onData);
    connect(m_reply, &QNetworkReply::finished, this, &AiClient::onDone);
}

void AiClient::abort()
{
    if (!m_reply) return;
    m_aborted = true;
    m_reply->abort();
}

void AiClient::onData()
{
    m_lastByte.restart();
    m_stall->start(m_noByteMs);
    const QByteArray bytes = m_reply->readAll();
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400) { m_errBody += bytes; return; }
    bool any = false;
    for (const QString &d : m_sse.feed(bytes)) {
        any = true;
        if (m_kind == Kind::Anthropic) m_state.feedAnthropic(d);
        else m_state.feedOpenAi(d);
    }
    if (any) emit updated();
}

void AiClient::onDone()
{
    m_stall->stop();
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    if (!r) return;
    if (r->bytesAvailable()) { m_reply = r; onData(); m_reply = nullptr; }
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString err;
    if (m_stalled) err = tp::tr("连接卡住了（90 秒没有收到内容），已停下。可以再说一次。");
    else if (m_aborted) err.clear();
    else if (status >= 400) err = httpError(status, m_errBody);
    else if (r->error() != QNetworkReply::NoError) err = status ? httpError(status, m_errBody) : tp::tr("网络出错：%1").arg(r->errorString());
    else if (!m_state.error.isEmpty()) err = m_state.error;
    r->deleteLater();
    emit finished(err, m_aborted, m_stalled);
}

void AiClient::get(const Request &r, std::function<void(int, const QByteArray &, const QString &)> done)
{
    QNetworkRequest q = makeRequest(r, m_ua);
    q.setTransferTimeout(25000);
    QNetworkReply *rep = m_nam->get(q);
    connect(rep, &QNetworkReply::finished, this, [rep, done] {
        const int status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        done(status, rep->readAll(), rep->error() == QNetworkReply::NoError ? QString() : rep->errorString());
        rep->deleteLater();
    });
}

} // namespace tp::ai
