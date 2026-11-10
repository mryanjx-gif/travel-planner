// 地点介绍（1.4.0，1.1设计预案.md "# 1.4.0 地点介绍"；QML 里是 place）：简易计划面板里聚焦某一站时，查这一站的介绍。
// 只在聚焦时查一站，不批量查。来源和条款见 claude/技术选型.md §13：
//   1. OpenStreetMap（Nominatim search，按站名、限定在站点附近约 5 km 的框里，extratags 取 opening_hours / fee / website / phone / wikidata）
//   2. 找 Wikidata 实体：OSM 标签里有 wikidata 就用；否则维基百科 geosearch（站点 10 km 内、标题和站名对得上的最近一条）；
//      还没有就 Wikidata 搜站名，取坐标在 30 km 内的第一条（距离校验，免得张冠李戴）
//   3. Wikidata：官网 P856、电话 P1329、图片 P18 / P3451 / P5775、维基百科和 Wikivoyage 的条目名
//   4. 维基百科 REST summary（跟软件语言，中文没有条目时用英文）
//   5. Commons imageinfo：960 px 缩略图地址、作者、授权；缩略图下载到本地
//   6. 当天天气：16 天内用 Open-Meteo 预报，更远用历史数据算往年同月平均（近 3 年）；日出日落本机算（suncalc）
// 缓存：<缓存目录>/place/<键的 sha1>.json（键 = 站名 + 三位小数的坐标），图片在 place/img/。不进数据文件、不进 .trip。
// 预报 3 小时后过期，其余永久。断网时用缓存，没有就显示"没联网"。请求都经 NetClient（白名单、User-Agent、离线开关、同一主机排队）。
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <functional>

namespace tp::services {

class NetClient;

class PlaceInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
public:
    explicit PlaceInfo(NetClient *net, QObject *parent = nullptr);

    void setCacheDir(const QString &dir);                                 // <缓存>/place
    void setToday(std::function<QString()> f) { m_today = std::move(f); }
    void setLang(std::function<QString()> f) { m_lang = std::move(f); }   // "zh" / "en"
    void setNoNet(bool on) { m_noNet = on; }                              // 截图 / 自检：不联网，只用缓存

    // { state: loading / ok / offline / none / "", title, text, wikiUrl, website, phone, hours, fee, photos: [{ thumb, caption }],
    //   wx: { kind, hi, lo, pop? } 或 clim: { hi, lo, rainDays, month }, sun: { rise, set, polar }, voyageUrl }
    QVariantMap info() const;
    Q_INVOKABLE void show(const QString &name, double lat, double lng, const QString &arrive, const QString &depart, const QString &tz);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void retranslate() { if (!m_key.isEmpty()) next(m_key); emit changed(); }
    // 自检用：把一份缓存写进去（和查到的格式一样）
    Q_INVOKABLE void putCacheForTest(const QString &name, double lat, double lng, const QVariantMap &entry);
    int requests() const { return m_requests; }

    // ---- 解析（单元测试用固定样例测）----
    static QString normName(const QString &s);                            // 去掉括号里的、重音、标点，小写
    static bool nameMatches(const QString &title, const QString &name);
    static QJsonObject parseOsm(const QByteArray &body);                  // Nominatim jsonv2 + extratags → { hours, fee, website, phone, wikidata }
    static QString pickGeoPage(const QByteArray &body, const QString &name);   // 维基百科 geosearch → 对得上的最近一条的 Q 号
    static QStringList parseSearchIds(const QByteArray &body);            // wbsearchentities → Q 号
    // wbgetentities → { qid, label{zh,en}, website, phone, images[], wiki{zh,en}, voyage{zh,en} }；
    // maxKm > 0 时取 ids 里第一个坐标在这个距离内的，否则取第一个
    static QJsonObject pickEntity(const QByteArray &body, const QStringList &ids, double lat, double lng, double maxKm);
    static QJsonObject parseSummary(const QByteArray &body);              // REST summary → { title, text, url }；消歧义页为空
    static QJsonArray parseImageInfo(const QByteArray &body, const QStringList &files);   // → [{ file, thumb, page, author, license }]，按 files 的顺序
    static QJsonObject climFromDaily(const QByteArray &body, int month);  // 历史日数据 → 这个月的平均 { hi, lo, rainDays, years }
    static double distKm(double lat1, double lng1, double lat2, double lng2);

signals:
    void changed();

private:
    struct Cur { QString name, arrive, depart, tz; double lat = 0, lng = 0; };
    QString lang() const { return m_lang ? m_lang() : QStringLiteral("zh"); }
    QString today() const;
    QString wxDate(const Cur &c) const;
    QJsonObject &entry(const QString &key);
    void save(const QString &key);
    void next(const QString &key);
    void get(const QString &key, const QUrl &url, std::function<void(const QByteArray &)> ok,
             const QByteArray &accept = QByteArrayLiteral("application/json"), const QByteArray &acceptLanguage = QByteArray());
    QString imgPath(const QString &url) const;

    NetClient *m_net;
    QString m_dir;
    std::function<QString()> m_today, m_lang;
    bool m_noNet = false;
    QString m_key;                       // 现在显示的
    QHash<QString, Cur> m_curs;          // 键 → 站点（查的时候要名字和坐标）
    QHash<QString, QJsonObject> m_mem;   // 键 → 缓存
    QSet<QString> m_running, m_failed;   // 正在查 / 上次没联上（下次 show 再试）
    int m_requests = 0;
};

} // namespace tp::services
