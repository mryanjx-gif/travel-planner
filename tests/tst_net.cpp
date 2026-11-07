// 联网（0.8.4）：统一请求通道 NetClient（本机假服务器）和批量算道路路线 RoadRouter（模拟的路线服务）。
// 不连外网：服务器是 QTcpServer 在 127.0.0.1 上临时开的。
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include "datastore.h"
#include "geocoder.h"
#include "jsonio.h"
#include "netclient.h"
#include "roadrouter.h"
#include "roads.h"
#include "tilecache.h"
#include "tileserver.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

using namespace tp::services;
using tp::map::geo::LatLng;

namespace {

// 极简 HTTP 服务器：/ok → 200 JSON；/bad → 400 JSON；/hang → 不回；其余 404。记下每个请求的时间和请求头
class FakeServer : public QObject {
public:
    struct Req { qint64 t; QByteArray head; };
    QTcpServer server;
    QElapsedTimer clock;
    QVector<Req> reqs;
    FakeServer()
    {
        clock.start();
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto *buf = new QByteArray;
                connect(s, &QTcpSocket::disconnected, s, [s, buf] { delete buf; s->deleteLater(); });
                connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    buf->append(s->readAll());
                    const int end = buf->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QByteArray head = buf->left(end);
                    buf->clear();
                    reqs.push_back({ clock.elapsed(), head });
                    const QByteArray path = head.split(' ').value(1);
                    QByteArray status = "404 Not Found", body = "{}";
                    if (path.startsWith("/ok")) { status = "200 OK"; body = R"({"code":"Ok"})"; }
                    else if (path.startsWith("/bad")) { status = "400 Bad Request"; body = R"({"code":"NoRoute"})"; }
                    else if (path.startsWith("/hang")) return;
                    s->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
                             + "\r\nConnection: close\r\n\r\n" + body);
                    s->flush();
                    s->disconnectFromHost();
                });
            }
        });
    }
    QUrl url(const QString &path) const { return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path)); }
};

NetClient::Reply getSync(NetClient &net, const QUrl &url, int timeoutMs = 5000)
{
    NetClient::Reply out;
    bool done = false;
    net.get(url, timeoutMs, nullptr, [&](const NetClient::Reply &r) { out = r; done = true; });
    (void)QTest::qWaitFor([&] { return done; }, 10000);   // 等不到时 out 是默认值（error None、status 0），调用方会发现
    return out;
}

// 一个计划：路线 A → B → C，A→B 自驾、B→C 徒步，都还没算
const char *kTravel = R"({"format":"travelplanner","formatVersion":1,"plans":[
 {"id":"p1","name":"测试路线","type":"route","status":"planning","stops":[
  {"name":"A","lat":35.0,"lng":139.0,"leg":{"mode":"drive"}},
  {"name":"B","lat":35.1,"lng":139.1,"leg":{"mode":"walk","note":"保留"}},
  {"name":"C","lat":35.2,"lng":139.2}]}]})";

QByteArray osrmOk(const QVector<LatLng> &pts, double dist, double time)
{
    return QJsonDocument(QJsonObject{ { "code", "Ok" }, { "routes", QJsonArray{ QJsonObject{
        { "geometry", tp::map::roads::encodePolyline(pts) }, { "distance", dist }, { "duration", time } } } } }).toJson(QJsonDocument::Compact);
}

} // namespace

class TestNet : public QObject {
    Q_OBJECT
private slots:
    void userAgentAndGet();
    void rejects();
    void minInterval();
    void timeout();
    void httpErrorKeepsBody();
    void contextGone();
    void routerBatch();
    void routerFailures();
    void routerStop();
    void tileCacheBasics();
    void tileCacheEvictAndScan();
    void tileServerServes();
    void tileServerFailures();
    void tileServerOfflineAndPrefetch();
    void tileServerKeepAlive();
    void geocoderSearch();     // 0.8.9 地名搜索（Nominatim）：网址、解析、缓存、失败原因
};

void TestNet::userAgentAndGet()
{
    FakeServer srv;
    NetClient net;
    net.setAllowPlainHttpForTest(true);
    net.addService({ "test", "127.0.0.1", 0 });
    net.setUserAgent(NetClient::userAgentFor("0.8.4"));
    QCOMPARE(net.userAgent(), QStringLiteral("TravelPlanner/0.8.4 (personal desktop app)"));
    const NetClient::Reply r = getSync(net, srv.url("/ok?x=1"));
    QVERIFY(r.ok());
    QCOMPARE(r.status, 200);
    QCOMPARE(r.body, QByteArray(R"({"code":"Ok"})"));
    QCOMPARE(srv.reqs.size(), 1);
    QVERIFY(srv.reqs[0].head.contains("User-Agent: TravelPlanner/0.8.4 (personal desktop app)"));
    QVERIFY(srv.reqs[0].head.startsWith("GET /ok?x=1 "));
    // 默认白名单里有 Electron 用到的服务
    QCOMPARE(net.serviceForHost("routing.openstreetmap.de"), QStringLiteral("routing"));
    QCOMPARE(net.serviceForHost("Nominatim.OpenStreetMap.org"), QStringLiteral("geocode"));
    QCOMPARE(net.serviceForHost("api.open-meteo.com"), QStringLiteral("weather"));
    QCOMPARE(net.serviceForHost("archive-api.open-meteo.com"), QStringLiteral("climate"));
    QCOMPARE(net.serviceForHost("example.com"), QString());
}

void TestNet::rejects()
{
    FakeServer srv;
    NetClient net;
    net.addService({ "test", "127.0.0.1", 0 });
    // 不是 https：拒绝（测试开关没开）；没发到服务器
    QCOMPARE(getSync(net, srv.url("/ok")).error, NetClient::Error::NotAllowed);
    QCOMPARE(getSync(net, QUrl("https://example.com/")).error, NetClient::Error::NotAllowed);
    QCOMPARE(srv.reqs.size(), 0);
    net.setAllowPlainHttpForTest(true);
    net.setOffline(true);
    QCOMPARE(getSync(net, srv.url("/ok")).error, NetClient::Error::Offline);
    net.setOffline(false);
    net.setServiceEnabled("test", false);
    QCOMPARE(getSync(net, srv.url("/ok")).error, NetClient::Error::ServiceOff);
    QCOMPARE(srv.reqs.size(), 0);
    net.setServiceEnabled("test", true);
    QVERIFY(getSync(net, srv.url("/ok")).ok());
    QCOMPARE(srv.reqs.size(), 1);
    // 同一主机上点名服务（0.12.5：海拔、时区）
    net.setServiceEnabled("elev", false);
    bool named = false;
    NetClient::Error namedErr = NetClient::Error::None;
    net.get(srv.url("/ok"), 5000, nullptr, [&](const NetClient::Reply &r) { namedErr = r.error; named = true; }, "application/json", "elev");
    QVERIFY(QTest::qWaitFor([&] { return named; }, 3000));
    QCOMPARE(namedErr, NetClient::Error::ServiceOff);
    QCOMPARE(srv.reqs.size(), 1);
    QVERIFY(getSync(net, srv.url("/ok")).ok());
}

void TestNet::minInterval()
{
    FakeServer srv;
    NetClient net;
    net.setAllowPlainHttpForTest(true);
    net.addService({ "test", "127.0.0.1", 300 });
    int done = 0;
    for (int i = 0; i < 3; ++i) net.get(srv.url("/ok"), 5000, nullptr, [&](const NetClient::Reply &r) { QVERIFY(r.ok()); ++done; });
    QCOMPARE(net.queued(), 2);   // 第一个马上发，其余排队
    QTRY_COMPARE_WITH_TIMEOUT(done, 3, 5000);
    QCOMPARE(srv.reqs.size(), 3);
    // 量的是服务器收到的时刻，电脑忙时前一个请求可能晚到（0.9.8 实测 284）；保证的是发送间隔 300，这里留 50 的抖动
    for (int i = 1; i < 3; ++i) QVERIFY2(srv.reqs[i].t - srv.reqs[i - 1].t >= 250, qPrintable(QString::number(srv.reqs[i].t - srv.reqs[i - 1].t)));
    // 取消排队中的：不发、不回调
    int got = 0;
    net.get(srv.url("/ok"), 5000, nullptr, [&](const NetClient::Reply &) { ++got; });
    const quint64 id = net.get(srv.url("/ok"), 5000, nullptr, [&](const NetClient::Reply &) { got += 100; });
    net.cancel(id);
    QTRY_COMPARE_WITH_TIMEOUT(got, 1, 3000);
    QTest::qWait(500);
    QCOMPARE(got, 1);
    QCOMPARE(srv.reqs.size(), 4);
}

void TestNet::timeout()
{
    FakeServer srv;
    NetClient net;
    net.setAllowPlainHttpForTest(true);
    net.addService({ "test", "127.0.0.1", 0 });
    QElapsedTimer t;
    t.start();
    const NetClient::Reply r = getSync(net, srv.url("/hang"), 400);
    QCOMPARE(r.error, NetClient::Error::Timeout);
    QVERIFY(t.elapsed() >= 380 && t.elapsed() < 3000);
}

void TestNet::httpErrorKeepsBody()
{
    FakeServer srv;
    NetClient net;
    net.setAllowPlainHttpForTest(true);
    net.addService({ "test", "127.0.0.1", 0 });
    const NetClient::Reply r = getSync(net, srv.url("/bad"));
    QCOMPARE(r.error, NetClient::Error::Http);
    QCOMPARE(r.status, 400);
    QCOMPARE(r.body, QByteArray(R"({"code":"NoRoute"})"));   // OSRM 把原因写在 400 的内容里
    // 连不上（端口没人听）
    QTcpServer dead;
    dead.listen(QHostAddress::LocalHost);
    const quint16 port = dead.serverPort();
    dead.close();
    QCOMPARE(getSync(net, QUrl(QStringLiteral("http://127.0.0.1:%1/ok").arg(port))).error, NetClient::Error::Network);
}

void TestNet::contextGone()
{
    FakeServer srv;
    NetClient net;
    net.setAllowPlainHttpForTest(true);
    net.addService({ "test", "127.0.0.1", 0 });
    bool called = false;
    auto *ctx = new QObject;
    net.get(srv.url("/ok"), 5000, ctx, [&](const NetClient::Reply &) { called = true; });
    delete ctx;
    QTest::qWait(500);
    QVERIFY(!called);
}

void TestNet::routerBatch()
{
    QTemporaryDir tmp;
    QFile f(tmp.filePath("travel.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(kTravel);
    f.close();
    tp::data::DataStore store;
    QVERIFY(store.load(tmp.path()));
    RoadRouter rr;
    rr.setStore(&store);
    QCOMPARE(rr.pending(), 2);
    QVERIFY(rr.shown());
    QVERIFY(rr.text().contains("2"));
    QStringList urls;
    rr.setFetchForTest([&](const QUrl &url, std::function<void(int, const QByteArray &, int)> done) {
        urls << url.toString();
        const bool walk = url.path().contains("foot");
        const QVector<LatLng> pts = walk ? QVector<LatLng>{ { 35.1, 139.1 }, { 35.15, 139.12 }, { 35.2, 139.2 } }
                                         : QVector<LatLng>{ { 35.0, 139.0 }, { 35.05, 139.08 }, { 35.1, 139.1 } };
        QTimer::singleShot(5, [done, pts] { done(200, osrmOk(pts, 12345.4, 987.6), 0); });
    });
    int saves = 0;
    bool lastQuiet = false;
    rr.setSave([&](bool quiet) { ++saves; lastQuiet = quiet; return store.save().ok; });
    rr.start();
    QVERIFY(rr.running());
    QCOMPARE(rr.button(), QStringLiteral("停止"));
    QTRY_VERIFY_WITH_TIMEOUT(!rr.running(), 5000);
    QCOMPARE(rr.okCount(), 2);
    QCOMPARE(rr.failCount(), 0);
    QCOMPARE(rr.pending(), 0);
    QVERIFY(rr.done());
    QCOMPARE(saves, 1);           // 算完自动保存一次
    QVERIFY(lastQuiet);
    QVERIFY(!store.isDirty());
    QVERIFY(rr.text().contains(QStringLiteral("已保存")));
    QCOMPARE(rr.button(), QStringLiteral("好"));
    QCOMPARE(urls.size(), 2);
    QVERIFY(urls[0].contains("routed-car") && urls[0].contains("139.000000,35.000000;139.100000,35.100000"));
    QVERIFY(urls[1].contains("routed-foot"));
    // 写进去了：path / dist / time / key；leg 里原来的键（note）保留
    const tp::json::Value &b = store.data()["plans"].asArray()[0]["stops"].asArray()[1];
    QCOMPARE(b["leg"]["note"].toString(), QStringLiteral("保留"));
    QCOMPARE(b["leg"]["dist"].toNumber(), 12345.0);
    QCOMPARE(b["leg"]["time"].toNumber(), 988.0);
    QCOMPARE(b["leg"]["key"].toString(), QStringLiteral("walk:35.10000,139.10000,35.20000,139.20000"));
    QVERIFY(!b["leg"]["path"].toString().isEmpty());
    // "好"：收起来，这次运行里不再显示
    rr.primary();
    QVERIFY(!rr.shown());
}

void TestNet::routerFailures()
{
    QTemporaryDir tmp;
    QFile f(tmp.filePath("travel.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(kTravel);
    f.close();
    tp::data::DataStore store;
    QVERIFY(store.load(tmp.path()));
    const QString failFile = tmp.filePath("road-fail.json");
    {
        RoadRouter rr;
        rr.setStore(&store);
        rr.setFailFile(failFile);
        // 第 1 段：找不到路（HTTP 400 + NoRoute → 记住）；第 2 段：断网（只在这次运行里记）
        int n = 0;
        rr.setFetchForTest([&](const QUrl &, std::function<void(int, const QByteArray &, int)> done) {
            const int k = n++;
            QTimer::singleShot(5, [done, k] {
                if (k == 0) done(400, R"({"code":"NoRoute"})", int(NetClient::Error::Http));
                else done(0, {}, int(NetClient::Error::Network));
            });
        });
        rr.setSave([&](bool) { return store.save().ok; });
        rr.start();
        QTRY_VERIFY_WITH_TIMEOUT(!rr.running(), 5000);
        QCOMPARE(rr.okCount(), 0);
        QCOMPARE(rr.failCount(), 2);
        QCOMPARE(rr.pending(), 0);   // 都不算"还没算"了（一个记住了，一个这次运行里失败过）
        QCOMPARE(rr.rememberedFails(), 1);
        QVERIFY(rr.text().contains(QStringLiteral("2 段没算出来")));
        const QString k1 = tp::map::roads::legKeyOf("drive", 35.0, 139.0, 35.1, 139.1);
        const QString k2 = tp::map::roads::legKeyOf("walk", 35.1, 139.1, 35.2, 139.2);
        QCOMPARE(rr.failReason(k1), QStringLiteral("两站之间找不到可走的道路"));
        QCOMPARE(rr.failReason(k2), QStringLiteral("连不上路线服务（可能断网）"));
        QVERIFY(QFile::exists(failFile));
    }
    // 下次打开：记住的那段不算，断网的那段又算"还没算"
    RoadRouter again;
    again.setStore(&store);
    again.setFailFile(failFile);
    QCOMPARE(again.pending(), 1);
    // 错误文字
    tp::map::roads::OsrmResult tb;
    tb.kind = tp::map::roads::OsrmResult::TooBig;
    QCOMPARE(again.errorText(0, &tb, "drive"), QStringLiteral("距离太远，路线服务不支持"));
    tp::map::roads::OsrmResult nr;
    nr.kind = tp::map::roads::OsrmResult::NoRoute;
    QCOMPARE(again.errorText(0, &nr, "walk"), QStringLiteral("两站之间找不到可走的路"));
    QCOMPARE(again.errorText(int(NetClient::Error::Timeout), nullptr, "drive"), QStringLiteral("路线服务超时"));
    QCOMPARE(again.errorText(int(NetClient::Error::ServiceOff), nullptr, "drive"), QStringLiteral("道路路线已关闭（设置 → 联网服务）"));
}

void TestNet::routerStop()
{
    QTemporaryDir tmp;
    QFile f(tmp.filePath("travel.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(kTravel);
    f.close();
    tp::data::DataStore store;
    QVERIFY(store.load(tmp.path()));
    RoadRouter rr;
    rr.setStore(&store);
    rr.setFetchForTest([&](const QUrl &, std::function<void(int, const QByteArray &, int)> done) {
        QTimer::singleShot(50, [done] { done(200, osrmOk({ { 35.0, 139.0 }, { 35.1, 139.1 } }, 1, 1), 0); });
    });
    rr.start();
    rr.primary();   // 运行中按主按钮 = 停止：这一段算完就停
    QTRY_VERIFY_WITH_TIMEOUT(!rr.running(), 5000);
    QCOMPARE(rr.okCount() + rr.failCount(), 1);
    QCOMPARE(rr.pending(), 1);
    QVERIFY(rr.done());
    // 没有 setSave：还没保存 → 主按钮是"保存"
    QVERIFY(store.isDirty());
    QCOMPARE(rr.button(), QStringLiteral("保存"));
    QVERIFY(rr.text().contains(QStringLiteral("还没保存")));
    // ×：收起来
    rr.dismiss();
    QVERIFY(!rr.shown());
}

// ---- 瓦片（0.8.5） ----

namespace {
const QByteArray kJpeg = QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray(200, 'j');

// 假的上游瓦片服务：/up/<z>/<y>/<x> → JPEG；x = 999 → 500；x = 998 → 200 但内容是 HTML；x = 997 → 404；x = 996 → 慢 300 ms
class FakeTiles : public QObject {
public:
    QTcpServer server;
    int hits = 0;
    int inFlight = 0, maxInFlight = 0;
    FakeTiles()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto *buf = new QByteArray;
                connect(s, &QTcpSocket::disconnected, s, [s, buf] { delete buf; s->deleteLater(); });
                connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    buf->append(s->readAll());
                    const int end = buf->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QByteArray path = buf->left(end).split(' ').value(1);
                    buf->clear();
                    ++hits;
                    const int x = path.split('/').value(4).toInt();
                    QByteArray status = "200 OK", body = kJpeg;
                    if (x == 999) { status = "500 Internal Server Error"; body = "oops"; }
                    else if (x == 998) body = "<html>error</html>";
                    else if (x == 997) { status = "404 Not Found"; body = ""; }
                    const int delay = x == 996 || path.contains("slow") ? 300 : 0;
                    ++inFlight;
                    maxInFlight = std::max(maxInFlight, inFlight);
                    QPointer<QTcpSocket> sp(s);
                    QTimer::singleShot(delay, this, [this, sp, status, body] {
                        --inFlight;
                        if (!sp) return;
                        sp->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        sp->flush();
                        sp->disconnectFromHost();
                    });
                });
            }
        });
    }
};

struct TileRig {
    QTemporaryDir dir;
    NetClient net;
    FakeTiles up;
    TileCache cache;
    TileServer server;
    QNetworkAccessManager nam;
    TileRig() : cache(dir.path() + QStringLiteral("/tiles")), server(&net, &cache)
    {
        net.setAllowPlainHttpForTest(true);
        net.addService({ QStringLiteral("tiles"), QStringLiteral("127.0.0.1"), 0 });
        server.addSource({ QStringLiteral("street"), QStringLiteral("https://server.arcgisonline.com/x/tile/{z}/{y}/{x}"), 19 });
        const quint16 port = up.server.serverPort();
        server.setUpstreamRewrite([port](const QString &u) {
            QString t = u;
            t.replace(QStringLiteral("https://server.arcgisonline.com/x/tile/"), QStringLiteral("http://127.0.0.1:%1/up/").arg(port));
            return t;
        });
        cache.startScan();
        cache.waitScan();
        server.start();
    }
    // 向本机瓦片服务要一张，返回 (状态码, 内容)
    QPair<int, QByteArray> get(const QString &path)
    {
        QNetworkReply *r = nam.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server.port()).arg(path))));
        (void)QTest::qWaitFor([r] { return r->isFinished(); }, 10000);
        const QPair<int, QByteArray> out{ r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), r->readAll() };
        r->deleteLater();
        return out;
    }
};
} // namespace

void TestNet::tileCacheBasics()
{
    QTemporaryDir dir;
    TileCache c(dir.path() + QStringLiteral("/tiles"));
    qint64 now = 1'800'000'000'000;
    c.setClock([&now] { return now; });
    c.startScan();
    c.waitScan();
    QVERIFY(c.scanned());
    const TileKey k{ QStringLiteral("street"), 5, 10, 12 };
    QVERIFY(!c.contains(k));
    QVERIFY(!c.get(k));
    QVERIFY(c.put(k, kJpeg));
    QVERIFY(c.contains(k));
    QCOMPARE(c.count(), 1);
    QCOMPARE(c.bytes(), qint64(kJpeg.size()));
    QVERIFY(QFile::exists(dir.path() + QStringLiteral("/tiles/street/5/10/12.t")));
    auto h = c.get(k);
    QVERIFY(h);
    QCOMPARE(h->data, kJpeg);
    QVERIFY(!h->stale);
    // 30 天以后算旧了（照样给）
    now += qint64(31) * 86400 * 1000;
    h = c.get(k);
    QVERIFY(h && h->stale);
    // 重新存一次就不旧了，总量不重复算
    QVERIFY(c.put(k, kJpeg + "x"));
    QCOMPARE(c.bytes(), qint64(kJpeg.size() + 1));
    QVERIFY(!c.get(k)->stale);
    QVERIFY(!c.put(k, QByteArray()));   // 空的不存
    c.clear();
    QCOMPARE(c.count(), 0);
    QCOMPARE(c.bytes(), 0);
    QVERIFY(!c.contains(k));
    QVERIFY(!QFile::exists(dir.path() + QStringLiteral("/tiles/street/5/10/12.t")));
}

void TestNet::tileCacheEvictAndScan()
{
    QTemporaryDir dir;
    const QString root = dir.path() + QStringLiteral("/tiles");
    qint64 now = 1'800'000'000'000;
    {
        TileCache c(root);
        c.setClock([&now] { return now; });
        c.startScan();
        c.waitScan();
        for (int i = 0; i < 10; ++i) { now += 1000; QVERIFY(c.put({ QStringLiteral("gray"), 3, i, 1 }, QByteArray(100, 'a'))); }
        QCOMPARE(c.bytes(), qint64(1000));
        // 上限 650：删到 ≤ 90%（585），从最早存的删起 → 剩最后 5 张
        c.setMaxBytes(650);
        QCOMPARE(c.count(), 5);
        for (int i = 0; i < 5; ++i) QVERIFY(!c.contains({ QStringLiteral("gray"), 3, i, 1 }));
        for (int i = 5; i < 10; ++i) QVERIFY(c.contains({ QStringLiteral("gray"), 3, i, 1 }));
    }
    {   // 0.12.6 一次要删很多：分批删，事件循环跑完文件都没了；刚存回来的不会被排队的删掉
        QTemporaryDir many;
        qint64 t = 1000;
        TileCache c(many.path());
        c.setClock([&t] { return t; });
        c.startScan();
        c.waitScan();
        for (int i = 0; i < 700; ++i) { t += 1000; QVERIFY(c.put({ QStringLiteral("gray"), 4, i, 1 }, QByteArray(10, 'b'))); }
        c.setMaxBytes(50);
        QVERIFY(c.bytes() <= 45);
        QVERIFY(QFile::exists(many.path() + QStringLiteral("/gray/4/0/1.t")));   // 第一批从队尾删，这张在队头：还没轮到
        QVERIFY(c.put({ QStringLiteral("gray"), 4, 0, 1 }, QByteArray(10, 'c')));
        QTRY_VERIFY(!QFile::exists(many.path() + QStringLiteral("/gray/4/1/1.t")));
        QTest::qWait(50);
        QVERIFY(QFile::exists(many.path() + QStringLiteral("/gray/4/0/1.t")));
    }
    // 不认识的文件不管、不删
    QFile junk(root + QStringLiteral("/gray/3/readme.txt"));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("x");
    junk.close();
    // 重新打开：数出来 5 张、500 字节
    TileCache c2(root);
    QVERIFY(!c2.scanned());
    QVERIFY(c2.contains({ QStringLiteral("gray"), 3, 7, 1 }));   // 数完之前看文件在不在
    c2.startScan();
    c2.waitScan();
    QCOMPARE(c2.count(), 5);
    QCOMPARE(c2.bytes(), qint64(500));
    QVERIFY(QFile::exists(junk.fileName()));
    c2.clear();
    QVERIFY(QDir(root).exists());   // 缓存文件夹本身留着
}

void TestNet::tileServerServes()
{
    TileRig rig;
    QVERIFY(rig.server.running());
    QVERIFY(rig.server.localTemplate(QStringLiteral("street")).startsWith(QStringLiteral("http://127.0.0.1:")));
    QSignalSpy spy(&rig.server, &TileServer::fetched);
    // 第一次：缓存里没有 → 上游取一次、存进缓存
    auto r = rig.get(QStringLiteral("/tile/street/5/12/10"));
    QCOMPARE(r.first, 200);
    QCOMPARE(r.second, kJpeg);
    QCOMPARE(rig.up.hits, 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("street"));
    QVERIFY(spy.at(0).at(1).toBool());
    QVERIFY(rig.cache.contains({ QStringLiteral("street"), 5, 10, 12 }));   // 路径是 z/y/x
    // 第二次：缓存里有，不联网、不发 fetched
    r = rig.get(QStringLiteral("/tile/street/5/12/10"));
    QCOMPARE(r.first, 200);
    QCOMPARE(rig.up.hits, 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(rig.server.stats().hits, 1);
    // 同一张同时要两次（慢的那张）：上游只取一次
    QNetworkReply *a = rig.nam.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/tile/street/12/5/996").arg(rig.server.port()))));
    QNetworkReply *b = rig.nam.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/tile/street/12/5/996").arg(rig.server.port()))));
    QTRY_VERIFY_WITH_TIMEOUT(a->isFinished() && b->isFinished(), 5000);
    QCOMPARE(a->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    QCOMPARE(b->readAll(), kJpeg);
    QCOMPARE(rig.up.hits, 2);
    a->deleteLater();
    b->deleteLater();
    // 不认识的路径、超出范围的编号、没登记的底图：404，不联网
    for (const char *bad : { "/tile/street/5/12", "/tile/../etc/passwd", "/tile/street/5/40/1", "/tile/street/20/1/1", "/tile/moon/1/0/0", "/x" }) {
        r = rig.get(QString::fromLatin1(bad));
        QCOMPARE(r.first, 404);
    }
    QCOMPARE(rig.up.hits, 2);
}

void TestNet::tileServerFailures()
{
    TileRig rig;
    QSignalSpy spy(&rig.server, &TileServer::fetched);
    // 上游 500 → 502（maplibre 过一会儿重试），算失败
    auto r = rig.get(QStringLiteral("/tile/street/10/5/999"));
    QCOMPARE(r.first, 502);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(1).toBool());
    // 200 但不是图片 → 502，算失败，不存
    r = rig.get(QStringLiteral("/tile/street/10/5/998"));
    QCOMPARE(r.first, 502);
    QVERIFY(!spy.at(1).at(1).toBool());
    QVERIFY(!rig.cache.contains({ QStringLiteral("street"), 10, 998, 5 }));
    // 上游 404 → 404，也算失败（Electron：底图的任何 error 都算）
    r = rig.get(QStringLiteral("/tile/street/10/5/997"));
    QCOMPARE(r.first, 404);
    QCOMPARE(spy.count(), 3);
    QVERIFY(!spy.at(2).at(1).toBool());
    QCOMPARE(rig.server.stats().failed, 3);
}

void TestNet::tileServerOfflineAndPrefetch()
{
    TileRig rig;
    QSignalSpy spy(&rig.server, &TileServer::fetched);
    QSignalSpy sw(&rig.net, &NetClient::switchesChanged);
    // 先存一张
    QCOMPARE(rig.get(QStringLiteral("/tile/street/6/7/8")).first, 200);
    // 离线：缓存里有的照样给；没有的 503，不联网、不算失败
    rig.net.setOffline(true);
    QCOMPARE(sw.count(), 1);
    QVERIFY(!rig.server.online());
    QCOMPARE(rig.get(QStringLiteral("/tile/street/6/7/8")).first, 200);
    QCOMPARE(rig.get(QStringLiteral("/tile/street/6/7/9")).first, 503);
    QCOMPARE(rig.up.hits, 1);
    QCOMPARE(spy.count(), 1);
    // 离线时不预取
    QVector<TileKey> keys;
    for (int i = 0; i < 60; ++i) keys.push_back({ QStringLiteral("street"), 8, 100 + i, 50 });
    QCOMPARE(rig.server.prefetch(keys), 0);
    // 只关瓦片这一项：一样
    rig.net.setOffline(false);
    rig.net.setServiceEnabled(QStringLiteral("tiles"), false);
    QVERIFY(!rig.server.online());
    QVERIFY(rig.net.allows(QStringLiteral("routing")));
    QCOMPARE(rig.get(QStringLiteral("/tile/street/6/7/9")).first, 503);
    rig.net.setServiceEnabled(QStringLiteral("tiles"), true);
    QCOMPARE(sw.count(), 4);
    // 预取：最多 48 张；同一时间最多 4 个；只存不报
    QCOMPARE(rig.server.prefetch(keys), TileServer::kPrefetchMax);
    QTRY_COMPARE_WITH_TIMEOUT(rig.server.stats().prefetched, TileServer::kPrefetchMax, 10000);
    QVERIFY(rig.up.maxInFlight <= TileServer::kPrefetchParallel);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(rig.cache.count(), 1 + TileServer::kPrefetchMax);
    // 同一批再要一次：前 48 张预取过了，剩下 12 张这次排上（Electron：先去掉预取过的，再取前 48 张）；第三次就没有了
    QCOMPARE(rig.server.prefetch(keys), 60 - TileServer::kPrefetchMax);
    QTRY_COMPARE_WITH_TIMEOUT(rig.server.stats().prefetched, 60, 10000);
    QCOMPARE(rig.server.prefetch(keys), 0);
    // 预取过的地图再要：缓存命中
    const int hits = rig.up.hits;
    QCOMPARE(rig.get(QStringLiteral("/tile/street/8/50/100")).first, 200);
    QCOMPARE(rig.up.hits, hits);
}

void TestNet::tileServerKeepAlive()
{
    TileRig rig;
    // 同一个连接上连发两个请求（maplibre 用长连接）：按顺序回两个
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, rig.server.port());
    QVERIFY(s.waitForConnected(3000));
    s.write("GET /tile/street/4/3/2 HTTP/1.1\r\nHost: x\r\n\r\nGET /x HTTP/1.1\r\nHost: x\r\n\r\n");
    QByteArray got;
    QTRY_VERIFY_WITH_TIMEOUT((got += s.readAll()).count("HTTP/1.1 ") >= 2, 5000);
    const int first = got.indexOf("HTTP/1.1 200"), second = got.indexOf("HTTP/1.1 404");
    QVERIFY(first >= 0 && second > first);
    QVERIFY(got.contains("Content-Type: image/jpeg"));
    QVERIFY(got.contains("Connection: keep-alive"));
    // HEAD：只有头
    s.write("HEAD /tile/street/4/3/2 HTTP/1.1\r\nConnection: close\r\n\r\n");
    QByteArray head;
    QTRY_VERIFY_WITH_TIMEOUT((head += s.readAll()).contains("\r\n\r\n"), 5000);
    QVERIFY(head.startsWith("HTTP/1.1 200"));
    QVERIFY(head.contains("Connection: close"));
    QTRY_VERIFY_WITH_TIMEOUT(s.state() == QAbstractSocket::UnconnectedState, 3000);
    QVERIFY(!head.mid(head.indexOf("\r\n\r\n") + 4).contains("j"));
    // 别的方法不行
    QTcpSocket s2;
    s2.connectToHost(QHostAddress::LocalHost, rig.server.port());
    QVERIFY(s2.waitForConnected(3000));
    s2.write("POST /tile/street/4/3/2 HTTP/1.1\r\nConnection: close\r\n\r\n");
    QByteArray p;
    QTRY_VERIFY_WITH_TIMEOUT((p += s2.readAll()).contains("\r\n\r\n"), 5000);
    QVERIFY(p.startsWith("HTTP/1.1 405"));
}

void TestNet::geocoderSearch()
{
    QTemporaryDir tmp;
    Geocoder g;
    // 网址和 Electron 一样：jsonv2、limit=8、accept-language=en、q 按 encodeURIComponent
    QCOMPARE(g.searchUrl(QStringLiteral("Eiffel Tower")).toString(QUrl::FullyEncoded),
             QStringLiteral("https://nominatim.openstreetmap.org/search?format=jsonv2&limit=8&accept-language=en&q=Eiffel%20Tower"));
    QVERIFY(g.searchUrl(QStringLiteral("京都")).toString(QUrl::FullyEncoded).endsWith(QStringLiteral("q=%E4%BA%AC%E9%83%BD")));
    QVERIFY(g.reverseUrl(35.0116, 135.7681).toString().contains(QStringLiteral("reverse?format=jsonv2&accept-language=en&lat=35.011600&lon=135.768100")));
    // 换服务地址：不是 https 的不认
    g.setBase(QStringLiteral("http://example.com"));
    QCOMPARE(g.base(), Geocoder::defaultBase());
    g.setBase(QStringLiteral("https://geo.example.org/"));
    QCOMPARE(g.base(), QStringLiteral("https://geo.example.org"));
    g.setBase(QString());
    // 解析：name 空时用 display_name 逗号前的第一段
    const QByteArray body = R"([{"name":"Kyoto","display_name":"Kyoto, Kyoto Prefecture, Japan","lat":"35.0116","lon":"135.7681"},
                               {"name":"","display_name":"Gion, Higashiyama Ward, Kyoto","lat":"35.0037","lon":"135.7788"},
                               {"display_name":"bad","lat":"x","lon":"1"}])";
    bool ok = false;
    const QVariantList rows = Geocoder::parseSearch(body, &ok);
    QVERIFY(ok);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[1].toMap().value("name").toString(), QStringLiteral("Gion"));
    QCOMPARE(rows[0].toMap().value("lat").toDouble(), 35.0116);
    QCOMPARE(Geocoder::parseReverse(R"({"name":"","display_name":"Fushimi Inari, Kyoto"})"), QStringLiteral("Fushimi Inari"));
    // 请求 + 缓存：同一个词第二次不发请求（条款要求缓存）；缓存写进文件，重新读进来也认
    int calls = 0;
    QByteArray reply = body;
    int replyErr = 0, replyStatus = 200;
    g.setFetchForTest([&](const QUrl &, std::function<void(int, const QByteArray &, int)> done) { ++calls; done(replyStatus, reply, replyErr); });
    g.setCacheFile(tmp.filePath(QStringLiteral("geocode.json")));
    QVariantList got;
    QString err;
    bool fromCache = true;
    g.search(QStringLiteral(" Kyoto "), this, [&](const QVariantList &r, const QString &e, bool c) { got = r; err = e; fromCache = c; });
    QTRY_COMPARE(calls, 1);
    QCOMPARE(got.size(), 2);
    QVERIFY(err.isEmpty() && !fromCache);
    g.search(QStringLiteral("kyoto"), this, [&](const QVariantList &r, const QString &e, bool c) { got = r; err = e; fromCache = c; });
    QCOMPARE(calls, 1);
    QVERIFY(fromCache && got.size() == 2);
    Geocoder g2;
    g2.setFetchForTest([&](const QUrl &, std::function<void(int, const QByteArray &, int)> done) { ++calls; done(200, reply, 0); });
    g2.setCacheFile(tmp.filePath(QStringLiteral("geocode.json")));
    g2.search(QStringLiteral("KYOTO"), this, [&](const QVariantList &r, const QString &, bool c) { got = r; fromCache = c; });
    QVERIFY(fromCache && got.size() == 2 && calls == 1);
    // 30 天后缓存过期
    g2.setClock([] { return QDateTime::currentDateTimeUtc().addDays(31); });
    g2.search(QStringLiteral("kyoto"), this, [&](const QVariantList &r, const QString &, bool c) { got = r; fromCache = c; });
    QTRY_COMPARE(calls, 2);
    // 失败：被限流 / 停用（403）、断网
    replyErr = int(NetClient::Error::Http); replyStatus = 403;
    g.search(QStringLiteral("osaka"), this, [&](const QVariantList &r, const QString &e, bool) { got = r; err = e; });
    QTRY_VERIFY(!err.isEmpty());
    QVERIFY(err.contains(QStringLiteral("403")));
    err.clear();
    replyErr = int(NetClient::Error::Network); replyStatus = 0;
    g.search(QStringLiteral("nara"), this, [&](const QVariantList &r, const QString &e, bool) { got = r; err = e; });
    QTRY_VERIFY(!err.isEmpty());
    // 失败的不缓存
    replyErr = 0; replyStatus = 200;
    const int before = calls;
    g.search(QStringLiteral("nara"), this, [&](const QVariantList &r, const QString &e, bool) { got = r; err = e; });
    QTRY_COMPARE(calls, before + 1);
    // 两次请求至少隔 1.1 秒（Electron lastReq；条款每秒最多 1 次）
    QElapsedTimer t;
    t.start();
    bool done = false;
    g.search(QStringLiteral("kobe"), this, [&](const QVariantList &, const QString &, bool) { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY2(t.elapsed() >= 900, qPrintable(QString::number(t.elapsed())));
}

QTEST_GUILESS_MAIN(TestNet)
#include "tst_net.moc"
