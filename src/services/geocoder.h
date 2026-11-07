// 地名搜索和反查（0.8.9，功能清单 M-24 / M-25；Electron map-app.js nominatim / doSearch / reverseGeocode）。
//
// 用 Nominatim（OpenStreetMap 基金会的公共服务）。使用条款（PO 0.8.9 知情后选了"继续用，按条款补齐"；
// https://operations.osmfoundation.org/policies/nominatim/ ，2026-11-06 读过，见技术选型.md §6a）：
//   - 每秒最多 1 次：经统一请求通道（NetClient），同一主机间隔 1 秒（Electron 用 1.1 秒，这里另外自己再隔到 1.1 秒）；
//   - User-Agent 能认出程序：NetClient 统一发 "TravelPlanner/<版本> (personal desktop app)"；
//   - 客户端要缓存：结果存进 <缓存目录>/geocode.json，同一个词 / 同一个点 30 天内不再请求，最多 500 条；
//   - 不许边打边搜：只在按 Enter / 点"搜索"时调用 search（界面保证）；点选时只反查那一个点；不发"我的位置"；
//   - 要署名：结果列表下面写"地名数据 © OpenStreetMap 贡献者"（界面），"关于"里也写（阶段 ⑥）；
//   - 对方要求时能停用：设置 → 联网关掉地名搜索（settings.json 的 net），geocodeUrl 可以换成别的兼容服务（不用更新软件）。
// 和 Electron 一样：jsonv2、limit=8、accept-language=en（站点名一律存英文名；PO 0.8.9 没选"用界面语言"）。
#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <functional>

namespace tp::services {

class NetClient;

class Geocoder : public QObject
{
    Q_OBJECT
public:
    static constexpr int kLimit = 8;                 // Electron limit=8
    static constexpr int kMinIntervalMs = 1100;      // Electron：两次请求至少隔 1.1 秒
    static constexpr int kTimeoutMs = 20000;
    static constexpr qint64 kCacheDays = 30;
    static constexpr int kCacheMax = 500;
    static QString defaultBase() { return QStringLiteral("https://nominatim.openstreetmap.org"); }

    explicit Geocoder(QObject *parent = nullptr);

    void setNet(NetClient *net) { m_net = net; }
    // 服务地址（settings.json 的 geocodeUrl；空 = 默认）。只认 https；主机不在白名单时登记成 geocode 服务（间隔 1 秒）
    void setBase(const QString &base);
    QString base() const { return m_base; }
    // 缓存文件（不设 = 只记在内存里）
    void setCacheFile(const QString &path);
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }
    void setTranslate(std::function<QString(const char *)> tr) { m_tr = std::move(tr); }

    // 测试用：替换真正的请求
    using Fetch = std::function<void(const QUrl &, std::function<void(int httpStatus, const QByteArray &body, int netError)>)>;
    void setFetchForTest(Fetch f) { m_fetch = std::move(f); }

    QUrl searchUrl(const QString &q) const;
    QUrl reverseUrl(double lat, double lng) const;

    // 结果：[{ name, address, lat, lng }]；error 非空 = 失败（给人看的原因）。fromCache = 没有发请求
    using SearchCb = std::function<void(const QVariantList &rows, const QString &error, bool fromCache)>;
    void search(const QString &q, QObject *context, SearchCb cb);
    // 反查一个点的名字（失败返回空，不影响使用——Electron 一样）
    using ReverseCb = std::function<void(const QString &name)>;
    void reverse(double lat, double lng, QObject *context, ReverseCb cb);

    // 解析（单元测试也用）：jsonv2 的 search 数组 → 行；reverse 对象 → 名字
    static QVariantList parseSearch(const QByteArray &body, bool *ok);
    static QString parseReverse(const QByteArray &body);
    // 名字：name，没有就取 display_name 逗号前的第一段（Electron r.name || r.display_name.split(',')[0]）
    static QString nameOf(const QString &name, const QString &display);

    int requests() const { return m_requests; }   // 真的发出去的请求数（测试 / 自检用）

private:
    void request(const QUrl &url, QObject *context, std::function<void(int status, const QByteArray &body, int netError)> done);
    QString errorText(int netError, int status) const;
    void loadCache();
    void saveCache();
    bool cached(const QString &key, QVariant *out);
    void remember(const QString &key, const QVariant &v);
    QString trd(const char *s) const { return m_tr ? m_tr(s) : QString::fromUtf8(s); }
    QDateTime now() const { return m_clock ? m_clock() : QDateTime::currentDateTimeUtc(); }

    NetClient *m_net = nullptr;
    QString m_base = defaultBase();
    QString m_cacheFile;
    struct Entry { qint64 t = 0; QVariant v; };
    QHash<QString, Entry> m_cache;
    std::function<QDateTime()> m_clock;
    std::function<QString(const char *)> m_tr;
    Fetch m_fetch;
    qint64 m_lastStart = 0;   // 毫秒
    int m_requests = 0;
};

} // namespace tp::services
