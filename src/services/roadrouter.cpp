#include "roadrouter.h"

#include "atomicfile.h"
#include "datastore.h"
#include "netclient.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace tp::services {

using namespace tp::map;
using Err = NetClient::Error;

RoadRouter::RoadRouter(QObject *parent) : QObject(parent)
{
    m_recount.setSingleShot(true);
    m_recount.setInterval(200);   // 连续改几次只数一次
    connect(&m_recount, &QTimer::timeout, this, &RoadRouter::recount);
}

void RoadRouter::setStore(tp::data::DataStore *store)
{
    m_store = store;
    connect(store, &tp::data::DataStore::contentChanged, this, [this] { if (!m_running) m_recount.start(); });
    connect(store, &tp::data::DataStore::lockChanged, this, [this] { m_recount.start(); });
    connect(store, &tp::data::DataStore::dirtyChanged, this, [this] { if (m_done) emit changed(); });   // "已保存 / 还没保存"
    recount();
}

void RoadRouter::setFailFile(const QString &path)
{
    m_failFile = path;
    m_failKeys.clear();
    m_failMsg.clear();
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonObject o = doc.object();
        // QJsonObject 按键排序，先后丢了；这里的先后只用来决定超过 500 个时丢哪些，影响不大
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (!it.value().isString()) continue;
            m_failKeys << it.key();
            m_failMsg.insert(it.key(), it.value().toString());
        }
    }
    recount();
}

QSet<QString> RoadRouter::excluded() const
{
    QSet<QString> s = m_sessionFail;
    for (const QString &k : m_failKeys) s.insert(k);
    return s;
}

void RoadRouter::recount()
{
    if (m_demo) return;
    int n = 0;
    if (m_store && !m_store->isReadOnly()) n = int(roads::pendingLegs(m_store->data(), excluded()).size());
    if (n == m_pending) { emit changed(); return; }
    m_pending = n;
    emit changed();
}

QString RoadRouter::failReason(const QString &key) const
{
    const QString m = m_lastMsg.value(key);
    return m.isEmpty() ? m_failMsg.value(key) : m;
}

bool RoadRouter::shown() const
{
    if (m_running || m_done) return true;
    return m_pending > 0 && !m_dismissed;
}

QString RoadRouter::text() const
{
    if (m_running) {
        const int left = m_total - m_ok - m_fail;
        return trd("正在按道路计算路线 %1 / %2…（约 %3 分钟）").arg(m_ok + m_fail).arg(m_total).arg(int(std::ceil(left * 1.4 / 60)));
    }
    if (m_done) {
        QString t = trd("已算好 %1 段").arg(m_ok);
        if (m_fail) t += trd("，%1 段没算出来（按直线显示，可在编辑器里重试）").arg(m_fail);
        const bool dirty = m_store && m_store->isDirty();
        t += dirty ? trd("。还没保存。") : trd("，已保存。");
        return t;
    }
    return trd("有 %1 段自驾/徒步还没按道路算路线，现在地图上先画直线。").arg(m_pending);
}

QString RoadRouter::button() const
{
    if (m_running) return trd("停止");
    if (m_done) return m_store && m_store->isDirty() ? trd("保存") : trd("好");
    return trd("现在计算");
}

void RoadRouter::primary()
{
    if (m_running) { stop(); return; }
    if (m_done) {
        m_done = false;
        m_dismissed = true;
        if (m_store && m_store->isDirty() && m_save) m_save(false);   // Electron：还没保存时按钮是"保存到 data.js"
        emit changed();
        return;
    }
    start();
}

void RoadRouter::dismiss()
{
    if (m_running) m_stopReq = true;
    m_done = false;
    m_dismissed = true;
    emit changed();
}

void RoadRouter::stop()
{
    if (m_running) m_stopReq = true;
}

int RoadRouter::pendingFor(const QString &planId) const
{
    if (!m_store || m_store->isReadOnly()) return 0;
    int n = 0;
    for (const roads::Pending &p : roads::pendingLegs(m_store->data(), excluded())) n += planId.isEmpty() || p.planId == planId;
    return n;
}

void RoadRouter::startFor(const QString &planId)
{
    if (planId.isEmpty()) start();
    else startForIds({ planId });
}

int RoadRouter::pendingForIds(const QStringList &planIds) const
{
    if (!m_store || m_store->isReadOnly()) return 0;
    int n = 0;
    for (const roads::Pending &p : roads::pendingLegs(m_store->data(), excluded())) n += planIds.contains(p.planId);
    return n;
}

void RoadRouter::startForIds(const QStringList &planIds)
{
    m_only = planIds;
    start();
    m_only.clear();
}

void RoadRouter::start()
{
    if (m_running || !m_store || m_store->isReadOnly()) return;
    if (m_blocker) {
        const QString why = m_blocker();
        if (!why.isEmpty()) { emit message(why, true); return; }
    }
    m_list = roads::pendingLegs(m_store->data(), excluded());
    if (!m_only.isEmpty()) m_list.removeIf([this](const roads::Pending &p) { return !m_only.contains(p.planId); });
    if (m_list.isEmpty()) { recount(); return; }
    m_running = true;
    m_stopReq = false;
    m_done = false;
    m_dismissed = false;
    m_ok = m_fail = 0;
    m_total = int(m_list.size());
    m_index = 0;
    qInfo("道路路线：开始算 %d 段", m_total);
    emit changed();
    next();
}

void RoadRouter::next()
{
    while (m_running) {
        if (m_stopReq || m_index >= m_list.size()) { finish(); return; }
        const roads::Pending leg = m_list[m_index++];
        // 这段期间站点可能挪了 / 已经算好了（Electron：对不上或已有折线就算"好了"跳过）
        bool still = false;
        for (const roads::Pending &p : roads::pendingLegs(m_store->data(), {}))
            if (p.planId == leg.planId && p.stopIndex == leg.stopIndex && p.key == leg.key) { still = true; break; }
        if (!still) { ++m_ok; continue; }
        const QUrl url = roads::osrmUrl(leg.mode, leg.aLat, leg.aLng, leg.bLat, leg.bLng);
        if (m_fetch) {
            m_fetch(url, [this, leg](int status, const QByteArray &body, int netError) { onResult(leg, status, body, netError); });
        } else if (m_net) {
            m_net->get(url, roads::kTimeoutMs, this, [this, leg](const NetClient::Reply &r) { onResult(leg, r.status, r.body, int(r.error)); });
        } else {
            onResult(leg, 0, {}, int(Err::Network));
        }
        return;
    }
}

QString RoadRouter::errorText(int netError, const roads::OsrmResult *osrm, const QString &mode) const
{
    if (osrm) {
        switch (osrm->kind) {
        case roads::OsrmResult::NoRoute:
            return mode == QLatin1String("walk") ? trd("两站之间找不到可走的路") : trd("两站之间找不到可走的道路");
        case roads::OsrmResult::TooBig: return trd("距离太远，路线服务不支持");
        case roads::OsrmResult::ServiceError: return trd("路线服务出错（%1）").arg(osrm->detail);
        case roads::OsrmResult::BadReply: return trd("路线服务返回异常（HTTP %1）").arg(osrm->detail);
        case roads::OsrmResult::Ok: return {};
        }
    }
    switch (Err(netError)) {
    case Err::Timeout: return trd("路线服务超时");
    case Err::Offline: return trd("联网已关闭（离线模式）");
    case Err::ServiceOff: return trd("道路路线已关闭（设置 → 联网服务）");
    case Err::NotAllowed: return trd("不允许的地址");
    default: return trd("连不上路线服务（可能断网）");
    }
}

void RoadRouter::onResult(const roads::Pending &leg, int status, const QByteArray &body, int netError)
{
    if (!m_running) return;
    // 服务器回了内容（含 4xx：OSRM 找不到路时是 HTTP 400 + {"code":"NoRoute"}）就按内容判断；没回内容按连接错误
    const bool haveBody = !body.isEmpty() && (Err(netError) == Err::None || Err(netError) == Err::Http);
    roads::OsrmResult r;
    QString msg;
    bool ok = false;
    if (haveBody) {
        r = roads::parseOsrm(body, status);
        if (r.kind == roads::OsrmResult::Ok) ok = true;
        else msg = errorText(netError, &r, leg.mode);
    } else if (Err(netError) == Err::Http) {
        r.kind = roads::OsrmResult::BadReply;
        r.detail = QString::number(status);
        msg = errorText(netError, &r, leg.mode);
    } else {
        msg = errorText(netError, nullptr, leg.mode);
    }
    if (ok) {
        bool wrote = false;
        m_store->modify([&](json::Value &d) { wrote = roads::applyLeg(d, leg, r.path, r.dist, r.time); });
        m_lastMsg.remove(leg.key);
        ++m_ok;
        qInfo("道路路线：「%s」第 %d 段 %s 好了（%.0f 米、%.0f 秒%s）", qUtf8Printable(leg.planName), leg.legNo + 1,
              qUtf8Printable(leg.mode), r.dist, r.time, wrote ? "" : "；站点已经变了，没写");
    } else {
        ++m_fail;
        m_lastMsg.insert(leg.key, msg);
        if (r.permanent()) remember(leg.key, msg);
        else m_sessionFail.insert(leg.key);
        qInfo("道路路线：「%s」第 %d 段没算出来：%s", qUtf8Printable(leg.planName), leg.legNo + 1, qUtf8Printable(msg));
    }
    emit legDone(leg.key, ok);
    emit changed();
    next();
}

void RoadRouter::finish()
{
    m_running = false;
    m_done = true;
    m_stopReq = false;
    qInfo("道路路线：算完了，好了 %d 段、没算出来 %d 段（共 %d 段）", m_ok, m_fail, m_total);
    // Electron v5.10.1：算完自动保存
    if (m_store && m_store->isDirty() && m_ok && m_save) m_save(true);
    m_pending = m_store && !m_store->isReadOnly() ? int(roads::pendingLegs(m_store->data(), excluded()).size()) : 0;
    emit changed();
    emit finished();
}

void RoadRouter::noteEditorFail(const QString &key, const QString &msg, bool permanent)
{
    m_lastMsg.insert(key, msg);
    if (permanent) remember(key, msg);
}

void RoadRouter::forget(const QString &key)
{
    m_lastMsg.remove(key);
    m_sessionFail.remove(key);
    if (m_failMsg.remove(key)) {
        m_failKeys.removeAll(key);
        saveFails();
    }
    recount();
}

void RoadRouter::remember(const QString &key, const QString &msg)
{
    if (!m_failMsg.contains(key)) m_failKeys << key;
    m_failMsg.insert(key, msg);
    while (m_failKeys.size() > roads::kFailKeep) m_failMsg.remove(m_failKeys.takeFirst());
    saveFails();
}

void RoadRouter::saveFails()
{
    if (m_failFile.isEmpty()) return;
    QJsonObject o;
    for (const QString &k : m_failKeys) o.insert(k, m_failMsg.value(k));
    const tp::platform::WriteResult w = tp::platform::writeViaTemp(m_failFile, m_failFile + QStringLiteral(".tmp"),
                                                                   QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!w.ok) qWarning("道路路线：记不下算不出来的段（%s）：%s", qUtf8Printable(m_failFile), qUtf8Printable(w.error));
}

} // namespace tp::services
