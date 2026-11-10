#include "placeinfo.h"

#include "netclient.h"
#include "planweather.h"
#include "suncalc.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrlQuery>

#include <cmath>
#include <limits>
#include <numbers>

namespace tp::services {

namespace {
constexpr int kTimeoutMs = 20000;
constexpr qint64 kForecastTtl = 3 * 3600;   // 预报 3 小时后重新取
const QString kNominatim = QStringLiteral("https://nominatim.openstreetmap.org");

QString S(const char *s) { return QString::fromLatin1(s); }
QJsonDocument doc(const QByteArray &b) { return QJsonDocument::fromJson(b); }
bool hasCjk(const QString &s)
{
    for (const QChar c : s) if (c.unicode() >= 0x3040 && c.unicode() <= 0x9fff) return true;
    return false;
}
// claims 里第一个不是 deprecated 的值
QJsonValue claim(const QJsonObject &claims, const char *p, int n = 0)
{
    int k = 0;
    for (const QJsonValue &c : claims.value(S(p)).toArray()) {
        const QJsonObject o = c.toObject();
        if (o.value(S("rank")).toString() == S("deprecated")) continue;
        if (k++ == n) return o.value(S("mainsnak")).toObject().value(S("datavalue")).toObject().value(S("value"));
    }
    return {};
}
QString stripHtml(QString s)
{
    s.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&")).replace(QStringLiteral("&quot;"), QStringLiteral("\"")).replace(QStringLiteral("&#039;"), QStringLiteral("'"))
     .replace(QStringLiteral("&lt;"), QStringLiteral("<")).replace(QStringLiteral("&gt;"), QStringLiteral(">")).replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    return s.simplified();
}
QString fileTitle(QString f) { f.replace(QLatin1Char('_'), QLatin1Char(' ')); if (!f.isEmpty()) f[0] = f[0].toUpper(); return f; }
} // namespace

PlaceInfo::PlaceInfo(NetClient *net, QObject *parent) : QObject(parent), m_net(net) {}

void PlaceInfo::setCacheDir(const QString &dir)
{
    m_dir = dir;
    QDir().mkpath(dir + QStringLiteral("/img"));
}

// ---------------- 解析 ----------------

QString PlaceInfo::normName(const QString &s)
{
    QString t = s;
    t.remove(QRegularExpression(QStringLiteral("[(（][^)）]*[)）]")));   // "Sydney (city)" → "Sydney"
    t = t.normalized(QString::NormalizationForm_KD);
    QString out;
    for (const QChar c : t) if (c.isLetterOrNumber()) out += c.toLower();
    return out;
}

bool PlaceInfo::nameMatches(const QString &title, const QString &name)
{
    const QString a = normName(title), b = normName(name);
    if (a.isEmpty() || b.isEmpty()) return false;
    if (a == b) return true;
    const int minLen = hasCjk(a) || hasCjk(b) ? 2 : 4;
    return qMin(a.size(), b.size()) >= minLen && (a.contains(b) || b.contains(a));
}

double PlaceInfo::distKm(double lat1, double lng1, double lat2, double lng2)
{
    const double r = std::numbers::pi / 180, dLat = (lat2 - lat1) * r, dLng = (lng2 - lng1) * r;
    const double h = std::sin(dLat / 2) * std::sin(dLat / 2) + std::cos(lat1 * r) * std::cos(lat2 * r) * std::sin(dLng / 2) * std::sin(dLng / 2);
    return 6371.0 * 2 * std::atan2(std::sqrt(h), std::sqrt(1 - h));
}

QJsonObject PlaceInfo::parseOsm(const QByteArray &body)
{
    const QJsonArray a = doc(body).array();
    if (a.isEmpty()) return {};
    const QJsonObject t = a.first().toObject().value(S("extratags")).toObject();
    const auto tag = [&](const char *k1, const char *k2) { const QString v = t.value(S(k1)).toString(); return v.isEmpty() && k2 ? t.value(S(k2)).toString() : v; };
    QJsonObject o;
    const auto put = [&](const char *k, const QString &v) { if (!v.isEmpty()) o.insert(S(k), v); };
    put("hours", tag("opening_hours", nullptr));
    put("fee", tag("fee", nullptr));
    put("website", tag("website", "contact:website"));
    put("phone", tag("phone", "contact:phone"));
    const QString wd = tag("wikidata", nullptr);
    if (QRegularExpression(QStringLiteral("^Q\\d+$")).match(wd).hasMatch()) o.insert(S("wikidata"), wd);
    return o;
}

QString PlaceInfo::pickGeoPage(const QByteArray &body, const QString &name)
{
    const QJsonObject pages = doc(body).object().value(S("query")).toObject().value(S("pages")).toObject();
    QString best;
    double bestD = std::numeric_limits<double>::max();
    for (const QJsonValue &v : pages) {
        const QJsonObject p = v.toObject();
        const QString q = p.value(S("pageprops")).toObject().value(S("wikibase_item")).toString();
        if (q.isEmpty() || !nameMatches(p.value(S("title")).toString(), name)) continue;
        const QJsonArray co = p.value(S("coordinates")).toArray();
        const double d = co.isEmpty() ? 1e9 : co.first().toObject().value(S("dist")).toDouble(1e9);
        if (d < bestD) { bestD = d; best = q; }
    }
    return best;
}

QStringList PlaceInfo::parseSearchIds(const QByteArray &body)
{
    QStringList out;
    for (const QJsonValue &v : doc(body).object().value(S("search")).toArray()) {
        const QString id = v.toObject().value(S("id")).toString();
        if (!id.isEmpty()) out << id;
    }
    return out;
}

QJsonObject PlaceInfo::pickEntity(const QByteArray &body, const QStringList &ids, double lat, double lng, double maxKm)
{
    const QJsonObject ents = doc(body).object().value(S("entities")).toObject();
    for (const QString &id : ids) {
        const QJsonObject e = ents.value(id).toObject();
        if (e.isEmpty() || e.contains(S("missing"))) continue;
        const QJsonObject claims = e.value(S("claims")).toObject();
        if (maxKm > 0) {
            const QJsonObject c = claim(claims, "P625").toObject();
            if (!c.contains(S("latitude")) || distKm(lat, lng, c.value(S("latitude")).toDouble(), c.value(S("longitude")).toDouble()) > maxKm) continue;
        }
        QJsonObject o{ { S("qid"), id } };
        const QString web = claim(claims, "P856").toString(), tel = claim(claims, "P1329").toString();
        if (!web.isEmpty()) o.insert(S("website"), web);
        if (!tel.isEmpty()) o.insert(S("phone"), tel);
        QJsonArray imgs;
        for (const char *p : { "P18", "P3451", "P5775" })   // 图片、夜景、室内
            for (int n = 0; n < 3 && imgs.size() < 3; ++n) {
                const QString f = claim(claims, p, n).toString();
                if (f.isEmpty()) break;
                if (!imgs.contains(f)) imgs.append(f);
            }
        if (!imgs.isEmpty()) o.insert(S("images"), imgs);
        const QJsonObject links = e.value(S("sitelinks")).toObject(), labels = e.value(S("labels")).toObject();
        QJsonObject wiki, voyage, label;
        for (const char *l : { "zh", "en" }) {
            const QString w = links.value(S(l) + S("wiki")).toObject().value(S("title")).toString();
            const QString y = links.value(S(l) + S("wikivoyage")).toObject().value(S("title")).toString();
            const QString b = labels.value(S(l)).toObject().value(S("value")).toString();
            if (!w.isEmpty()) wiki.insert(S(l), w);
            if (!y.isEmpty()) voyage.insert(S(l), y);
            if (!b.isEmpty()) label.insert(S(l), b);
        }
        o.insert(S("wiki"), wiki);
        o.insert(S("voyage"), voyage);
        o.insert(S("label"), label);
        return o;
    }
    return {};
}

QJsonObject PlaceInfo::parseSummary(const QByteArray &body)
{
    const QJsonObject o = doc(body).object();
    const QString text = o.value(S("extract")).toString().trimmed();
    if (text.isEmpty() || o.value(S("type")).toString() == S("disambiguation")) return {};
    return { { S("title"), o.value(S("title")).toString() }, { S("text"), text },
             { S("url"), o.value(S("content_urls")).toObject().value(S("desktop")).toObject().value(S("page")).toString() } };
}

QJsonArray PlaceInfo::parseImageInfo(const QByteArray &body, const QStringList &files)
{
    QHash<QString, QJsonObject> byTitle;
    for (const QJsonValue &v : doc(body).object().value(S("query")).toObject().value(S("pages")).toObject()) {
        const QJsonObject p = v.toObject();
        const QJsonObject ii = p.value(S("imageinfo")).toArray().at(0).toObject();
        const QString thumb = ii.value(S("thumburl")).toString();
        if (thumb.isEmpty()) continue;
        const QJsonObject meta = ii.value(S("extmetadata")).toObject();
        byTitle.insert(p.value(S("title")).toString(), QJsonObject{
            { S("thumb"), thumb }, { S("page"), ii.value(S("descriptionurl")).toString() },
            { S("author"), stripHtml(meta.value(S("Artist")).toObject().value(S("value")).toString()) },
            { S("license"), meta.value(S("LicenseShortName")).toObject().value(S("value")).toString() } });
    }
    QJsonArray out;
    for (const QString &f : files) {
        QJsonObject o = byTitle.value(QStringLiteral("File:") + fileTitle(f));
        if (o.isEmpty()) continue;
        o.insert(S("file"), f);
        out.append(o);
    }
    return out;
}

QJsonObject PlaceInfo::climFromDaily(const QByteArray &body, int month)
{
    const QJsonObject d = doc(body).object().value(S("daily")).toObject();
    const QJsonArray time = d.value(S("time")).toArray(), hi = d.value(S("temperature_2m_max")).toArray(),
                     lo = d.value(S("temperature_2m_min")).toArray(), pr = d.value(S("precipitation_sum")).toArray();
    double sHi = 0, sLo = 0;
    int nHi = 0, nLo = 0, rain = 0;
    QSet<int> years;
    for (int i = 0; i < time.size(); ++i) {
        const QDate t = QDate::fromString(time[i].toString(), Qt::ISODate);
        if (!t.isValid() || t.month() != month) continue;
        years.insert(t.year());
        if (hi[i].isDouble()) { sHi += hi[i].toDouble(); ++nHi; }
        if (lo[i].isDouble()) { sLo += lo[i].toDouble(); ++nLo; }
        if (pr[i].isDouble() && pr[i].toDouble() >= 1.0) ++rain;   // 1 mm 以上算下雨天
    }
    if (!nHi || !nLo || years.isEmpty()) return {};
    return { { S("hi"), std::round(sHi / nHi) }, { S("lo"), std::round(sLo / nLo) },
             { S("rainDays"), std::round(double(rain) / years.size()) }, { S("years"), int(years.size()) }, { S("month"), month } };
}

// ---------------- 缓存 ----------------

QString PlaceInfo::today() const { return m_today ? m_today() : QDate::currentDate().toString(Qt::ISODate); }

// 天气看哪一天：到达日；正在这一站时看今天（和计划页 stopWxDate 一样）
QString PlaceInfo::wxDate(const Cur &c) const
{
    const QString t = today(), b = c.depart.isEmpty() ? c.arrive : c.depart;
    return !c.arrive.isEmpty() && t >= c.arrive && t <= b ? t : c.arrive;
}

static QString keyOf(const QString &name, double lat, double lng)
{
    return name.trimmed().toLower() + QLatin1Char('|') + QString::number(lat, 'f', 3) + QLatin1Char('|') + QString::number(lng, 'f', 3);
}
static QString hashOf(const QString &s) { return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex()); }

QJsonObject &PlaceInfo::entry(const QString &key)
{
    auto it = m_mem.find(key);
    if (it != m_mem.end()) return *it;
    QJsonObject o;
    if (!m_dir.isEmpty()) {
        QFile f(m_dir + QLatin1Char('/') + hashOf(key) + QStringLiteral(".json"));
        if (f.open(QIODevice::ReadOnly)) o = doc(f.readAll()).object();
    }
    return *m_mem.insert(key, o);
}

void PlaceInfo::save(const QString &key)
{
    if (m_dir.isEmpty()) return;
    QSaveFile f(m_dir + QLatin1Char('/') + hashOf(key) + QStringLiteral(".json"));
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(m_mem.value(key)).toJson(QJsonDocument::Compact)); f.commit(); }
}

QString PlaceInfo::imgPath(const QString &url) const
{
    const QString ext = QFileInfo(QUrl(url).path()).suffix().toLower();
    return QStringLiteral("img/") + hashOf(url) + QLatin1Char('.') + (ext.isEmpty() ? QStringLiteral("jpg") : ext);
}

void PlaceInfo::putCacheForTest(const QString &name, double lat, double lng, const QVariantMap &e)
{
    const QString key = keyOf(name, lat, lng);
    m_mem.insert(key, QJsonObject::fromVariantMap(e));
    save(key);
    if (key == m_key) emit changed();
}

// ---------------- 查 ----------------

void PlaceInfo::show(const QString &name, double lat, double lng, const QString &arrive, const QString &depart, const QString &tz)
{
    if (name.trimmed().isEmpty() || !std::isfinite(lat) || !std::isfinite(lng)) { clear(); return; }
    const QString key = keyOf(name, lat, lng);
    m_curs.insert(key, Cur{ name.trimmed(), arrive, depart, tz, lat, lng });
    m_key = key;
    entry(key);
    m_failed.remove(key);   // 每次打开都再试一次
    next(key);
    emit changed();
}

void PlaceInfo::clear()
{
    if (m_key.isEmpty()) return;
    m_key.clear();
    emit changed();
}

void PlaceInfo::get(const QString &key, const QUrl &url, std::function<void(const QByteArray &)> ok, const QByteArray &accept, const QByteArray &acceptLanguage)
{
    ++m_requests;
    m_running.insert(key);
    m_net->get(url, kTimeoutMs, this, [this, key, ok](const NetClient::Reply &r) {
        m_running.remove(key);
        // 4xx（429 除外）当作"没有"，存下来不再问；断网、超时、5xx、开关关着：这次停下，下次打开再试
        if (r.ok() || (r.error == NetClient::Error::Http && r.status >= 400 && r.status < 500 && r.status != 429)) {
            ok(r.ok() ? r.body : QByteArray());
            save(key);
            next(key);
        } else {
            if (r.error != NetClient::Error::Canceled) qInfo("地点介绍没取到（%d %d）：%s", int(r.error), r.status, qUtf8Printable(r.detail));
            m_failed.insert(key);
        }
        if (key == m_key) emit changed();
    }, accept, QString(), acceptLanguage);
}

// 找出第一样还没有的，去取；取到了再回到这里。一站同一时间只发一个请求
void PlaceInfo::next(const QString &key)
{
    if (m_noNet || !m_net || m_running.contains(key) || m_failed.contains(key) || !m_curs.contains(key)) return;
    const Cur c = m_curs.value(key);
    QJsonObject &e = entry(key);

    // 天气（和介绍的开关分开：走 weather / climate）
    const QString date = wxDate(c);
    const QDate d = QDate::fromString(date, Qt::ISODate), t = QDate::fromString(today(), Qt::ISODate);
    if (d.isValid() && t.isValid()) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (d >= t && d <= t.addDays(15)) {
            const QJsonObject wx = e.value(S("wx")).toObject();
            if ((wx.value(S("date")).toString() != date || now - qint64(wx.value(S("at")).toDouble()) > kForecastTtl) && m_net->allows(S("weather"))) {
                const QUrl u = map::plans::forecastUrl({ { QString(), c.lat, c.lng } });
                get(key, u, [this, key, date, now](const QByteArray &b) {
                    const QVector<QJsonObject> daily = map::plans::parseDaily(b, 1);
                    const QVariantMap day = daily.isEmpty() ? QVariantMap() : map::plans::wxDay(daily.first(), date);
                    m_mem[key].insert(S("wx"), QJsonObject{ { S("date"), date }, { S("at"), double(now) }, { S("day"), QJsonObject::fromVariantMap(day) } });
                });
                return;
            }
        } else {
            const QString mk = QString::number(d.month());
            QJsonObject clim = e.value(S("clim")).toObject();
            if (!clim.contains(mk) && m_net->allows(S("climate"))) {
                // 近 3 个完整的同月（ERA5 约晚 5 天，年份按计划页的 climYears 算），一次取 3 年的日数据再挑这个月
                // ponytail: 计划页用 10 年、这里只用 3 年省流量；要更准就改成 climYears 全部
                const int last = map::plans::climYears(d.month(), today()).constLast();
                QUrl u(QStringLiteral("https://archive-api.open-meteo.com/v1/archive"));
                QUrlQuery q;
                q.addQueryItem(S("latitude"), QString::number(c.lat, 'f', 3));
                q.addQueryItem(S("longitude"), QString::number(c.lng, 'f', 3));
                q.addQueryItem(S("start_date"), QDate(last - 2, d.month(), 1).toString(Qt::ISODate));
                q.addQueryItem(S("end_date"), QDate(last, d.month(), 1).addMonths(1).addDays(-1).toString(Qt::ISODate));
                q.addQueryItem(S("daily"), S("temperature_2m_max,temperature_2m_min,precipitation_sum"));
                q.addQueryItem(S("timezone"), S("auto"));
                u.setQuery(q);
                const int month = d.month();
                get(key, u, [this, key, mk, month](const QByteArray &b) {
                    QJsonObject cl = m_mem[key].value(S("clim")).toObject();
                    cl.insert(mk, climFromDaily(b, month));
                    m_mem[key].insert(S("clim"), cl);
                });
                return;
            }
        }
    }

    if (!m_net->allows(S("place"))) return;
    const QString L = lang();
    // 1. OpenStreetMap 标签（Nominatim，1 次/秒由 NetClient 排队保证；地名搜索的开关关着就跳过）
    if (!e.contains(S("osm"))) {
        if (!m_net->allows(S("geocode"))) { e.insert(S("osm"), QJsonObject()); }
        else {
            QUrl u(kNominatim + QStringLiteral("/search"));
            QUrlQuery q;
            q.addQueryItem(S("q"), c.name);
            q.addQueryItem(S("format"), S("jsonv2"));
            q.addQueryItem(S("limit"), S("1"));
            q.addQueryItem(S("extratags"), S("1"));
            q.addQueryItem(S("bounded"), S("1"));
            q.addQueryItem(S("viewbox"), QStringLiteral("%1,%2,%3,%4").arg(c.lng - 0.05, 0, 'f', 4).arg(c.lat + 0.05, 0, 'f', 4).arg(c.lng + 0.05, 0, 'f', 4).arg(c.lat - 0.05, 0, 'f', 4));
            u.setQuery(q);
            get(key, u, [this, key](const QByteArray &b) { m_mem[key].insert(S("osm"), parseOsm(b)); });
            return;
        }
    }
    // 2. Wikidata 实体
    if (!e.contains(S("qid"))) {
        const QString wd = e.value(S("osm")).toObject().value(S("wikidata")).toString();
        if (!wd.isEmpty()) { e.insert(S("qid"), wd); }
        else if (!e.contains(S("geoTried"))) {
            const QString site = hasCjk(c.name) ? S("zh") : S("en");
            QUrl u(QStringLiteral("https://%1.wikipedia.org/w/api.php").arg(site));
            QUrlQuery q;
            q.addQueryItem(S("action"), S("query"));
            q.addQueryItem(S("format"), S("json"));
            q.addQueryItem(S("generator"), S("geosearch"));
            q.addQueryItem(S("ggscoord"), QStringLiteral("%1|%2").arg(c.lat, 0, 'f', 5).arg(c.lng, 0, 'f', 5));
            q.addQueryItem(S("ggsradius"), S("10000"));
            q.addQueryItem(S("ggslimit"), S("50"));
            q.addQueryItem(S("prop"), S("coordinates|pageprops"));
            q.addQueryItem(S("ppprop"), S("wikibase_item"));
            q.addQueryItem(S("colimit"), S("50"));
            q.addQueryItem(S("codistancefrompoint"), QStringLiteral("%1|%2").arg(c.lat, 0, 'f', 5).arg(c.lng, 0, 'f', 5));
            u.setQuery(q);
            const QString name = c.name;
            get(key, u, [this, key, name](const QByteArray &b) {
                QJsonObject &x = m_mem[key];
                x.insert(S("geoTried"), true);
                const QString qid = pickGeoPage(b, name);
                if (!qid.isEmpty()) x.insert(S("qid"), qid);
            });
            return;
        } else {
            // 3. 搜 Wikidata，取坐标在 30 km 内的
            QUrl u(QStringLiteral("https://www.wikidata.org/w/api.php"));
            QUrlQuery q;
            q.addQueryItem(S("action"), S("wbsearchentities"));
            q.addQueryItem(S("format"), S("json"));
            q.addQueryItem(S("type"), S("item"));
            q.addQueryItem(S("limit"), S("7"));
            q.addQueryItem(S("language"), hasCjk(c.name) ? S("zh") : S("en"));
            q.addQueryItem(S("search"), c.name);
            u.setQuery(q);
            get(key, u, [this, key](const QByteArray &b) {
                const QStringList ids = parseSearchIds(b);
                QJsonObject &x = m_mem[key];
                if (ids.isEmpty()) { x.insert(S("qid"), QString()); return; }
                x.insert(S("cands"), QJsonArray::fromStringList(ids));
                x.insert(S("qid"), S("?"));   // 下一步按坐标挑
            });
            return;
        }
    }
    const QString qid = e.value(S("qid")).toString();
    if (!qid.isEmpty() && !e.contains(S("ent"))) {
        const bool pick = qid == S("?");
        QStringList ids;
        if (pick) for (const QJsonValue &v : e.value(S("cands")).toArray()) ids << v.toString();
        else ids << qid;
        QUrl u(QStringLiteral("https://www.wikidata.org/w/api.php"));
        QUrlQuery q;
        q.addQueryItem(S("action"), S("wbgetentities"));
        q.addQueryItem(S("format"), S("json"));
        q.addQueryItem(S("ids"), ids.join(QLatin1Char('|')));
        q.addQueryItem(S("props"), S("claims|sitelinks|labels"));
        q.addQueryItem(S("languages"), S("zh|en"));
        q.addQueryItem(S("sitefilter"), S("zhwiki|enwiki|zhwikivoyage|enwikivoyage"));
        u.setQuery(q);
        const double lat = c.lat, lng = c.lng;
        get(key, u, [this, key, ids, pick, lat, lng](const QByteArray &b) {
            QJsonObject &x = m_mem[key];
            const QJsonObject ent = pickEntity(b, ids, lat, lng, pick ? 30.0 : 0.0);
            x.insert(S("ent"), ent);
            x.insert(S("qid"), ent.value(S("qid")).toString());
            x.remove(S("cands"));
        });
        return;
    }
    const QJsonObject ent = e.value(S("ent")).toObject();
    if (ent.isEmpty()) return;
    // 4. 维基百科摘要（跟软件语言；中文没有条目用英文）
    QJsonObject sum = e.value(S("sum")).toObject();
    if (!sum.contains(L)) {
        const QJsonObject wiki = ent.value(S("wiki")).toObject();
        const QString site = wiki.contains(L) ? L : S("en");
        const QString title = wiki.value(site).toString();
        if (title.isEmpty()) { sum.insert(L, QJsonObject()); e.insert(S("sum"), sum); }
        else {
            const QByteArray path = QUrl::toPercentEncoding(QString(title).replace(QLatin1Char(' '), QLatin1Char('_')));
            const QUrl u = QUrl::fromEncoded("https://" + site.toLatin1() + ".wikipedia.org/api/rest_v1/page/summary/" + path);
            get(key, u, [this, key, L](const QByteArray &b) {
                QJsonObject s = m_mem[key].value(S("sum")).toObject();
                s.insert(L, parseSummary(b));
                m_mem[key].insert(S("sum"), s);
            }, QByteArrayLiteral("application/json"), site == S("zh") ? QByteArrayLiteral("zh-CN") : QByteArray());
            return;
        }
    }
    // 5. Commons：缩略图地址、作者、授权
    const QJsonArray images = ent.value(S("images")).toArray();
    if (!images.isEmpty() && !e.contains(S("photos"))) {
        QStringList files, titles;
        for (const QJsonValue &v : images) { files << v.toString(); titles << QStringLiteral("File:") + fileTitle(v.toString()); }
        QUrl u(QStringLiteral("https://commons.wikimedia.org/w/api.php"));
        QUrlQuery q;
        q.addQueryItem(S("action"), S("query"));
        q.addQueryItem(S("format"), S("json"));
        q.addQueryItem(S("prop"), S("imageinfo"));
        q.addQueryItem(S("iiprop"), S("url|extmetadata"));
        q.addQueryItem(S("iiurlwidth"), S("960"));
        q.addQueryItem(S("iiextmetadatafilter"), S("Artist|LicenseShortName"));
        q.addQueryItem(S("titles"), titles.join(QLatin1Char('|')));
        u.setQuery(q);
        get(key, u, [this, key, files](const QByteArray &b) { m_mem[key].insert(S("photos"), parseImageInfo(b, files)); });
        return;
    }
    // 6. 缩略图下载到本地
    QJsonArray photos = e.value(S("photos")).toArray();
    for (int i = 0; i < photos.size(); ++i) {
        QJsonObject p = photos[i].toObject();
        if (p.contains(S("local"))) continue;
        const QString url = p.value(S("thumb")).toString(), rel = imgPath(url);
        if (QFileInfo::exists(m_dir + QLatin1Char('/') + rel)) { p.insert(S("local"), rel); photos[i] = p; e.insert(S("photos"), photos); continue; }
        get(key, QUrl(url), [this, key, i, rel](const QByteArray &b) {
            QJsonArray ps = m_mem[key].value(S("photos")).toArray();
            QJsonObject p = ps[i].toObject();
            QSaveFile f(m_dir + QLatin1Char('/') + rel);
            const bool ok = !b.isEmpty() && f.open(QIODevice::WriteOnly) && f.write(b) == b.size() && f.commit();
            p.insert(S("local"), ok ? rel : QString());
            ps[i] = p;
            m_mem[key].insert(S("photos"), ps);
        }, QByteArrayLiteral("image/*"));
        return;
    }
}

// ---------------- 给界面的 ----------------

QVariantMap PlaceInfo::info() const
{
    if (m_key.isEmpty()) return {};
    const QJsonObject e = m_mem.value(m_key);
    const Cur c = m_curs.value(m_key);
    const QString L = lang();
    QVariantMap m;
    const QJsonObject osm = e.value(S("osm")).toObject(), ent = e.value(S("ent")).toObject();
    const QJsonObject sum = e.value(S("sum")).toObject().value(L).toObject();
    const auto pick = [&](const char *k) { const QString v = osm.value(S(k)).toString(); return v.isEmpty() ? ent.value(S(k)).toString() : v; };
    const auto put = [&](const char *k, const QString &v) { if (!v.isEmpty()) m.insert(S(k), v); };
    const QJsonObject label = ent.value(S("label")).toObject();
    put("title", !sum.value(S("title")).toString().isEmpty() ? sum.value(S("title")).toString()
                 : label.value(L).toString().isEmpty() ? label.value(S("en")).toString() : label.value(L).toString());
    put("text", sum.value(S("text")).toString());
    put("wikiUrl", sum.value(S("url")).toString());
    put("website", pick("website"));
    put("phone", pick("phone"));
    put("hours", osm.value(S("hours")).toString());
    put("fee", osm.value(S("fee")).toString());
    QVariantList photos;
    for (const QJsonValue &v : e.value(S("photos")).toArray()) {
        const QJsonObject p = v.toObject();
        const QString local = p.value(S("local")).toString();
        if (local.isEmpty() || !QFileInfo::exists(m_dir + QLatin1Char('/') + local)) continue;
        const QString who = p.value(S("author")).toString(), lic = p.value(S("license")).toString();
        QStringList cap;
        if (!who.isEmpty()) cap << who;
        if (!lic.isEmpty()) cap << lic;
        cap << QStringLiteral("Wikimedia Commons");
        photos << QVariantMap{ { S("thumb"), QUrl::fromLocalFile(m_dir + QLatin1Char('/') + local).toString() }, { S("caption"), cap.join(QStringLiteral(" · ")) },
                               { S("page"), p.value(S("page")).toString() } };
    }
    if (!photos.isEmpty()) m.insert(S("photos"), photos);
    const QJsonObject voyage = ent.value(S("voyage")).toObject();
    const QString vs = voyage.contains(L) ? L : voyage.contains(S("en")) ? S("en") : QString();
    m.insert(S("voyageUrl"), !vs.isEmpty()
        ? QStringLiteral("https://%1.wikivoyage.org/wiki/").arg(vs) + QString::fromLatin1(QUrl::toPercentEncoding(voyage.value(vs).toString().replace(QLatin1Char(' '), QLatin1Char('_'))))
        : QStringLiteral("https://%1.wikivoyage.org/w/index.php?search=").arg(L == S("zh") ? S("zh") : S("en")) + QString::fromLatin1(QUrl::toPercentEncoding(c.name)));

    // 天气
    const QString date = wxDate(c);
    const QDate d = QDate::fromString(date, Qt::ISODate), t = QDate::fromString(today(), Qt::ISODate);
    if (d.isValid() && t.isValid()) {
        if (d >= t && d <= t.addDays(15)) {
            const QJsonObject wx = e.value(S("wx")).toObject();
            if (wx.value(S("date")).toString() == date && !wx.value(S("day")).toObject().isEmpty()) m.insert(S("wx"), wx.value(S("day")).toObject().toVariantMap());
        } else {
            const QJsonObject cl = e.value(S("clim")).toObject().value(QString::number(d.month())).toObject();
            if (!cl.isEmpty()) m.insert(S("clim"), cl.toVariantMap());
        }
        const map::sun::Times st = map::sun::info(c.lat, c.lng, date, c.tz);
        if (st.ok) m.insert(S("sun"), QVariantMap{ { S("rise"), st.rise }, { S("set"), st.set }, { S("polar"), st.polar } });
    }

    const bool found = m.contains(S("title")) || m.contains(S("hours")) || m.contains(S("website")) || m.contains(S("phone"));
    const bool done = e.contains(S("osm")) && e.contains(S("qid")) && e.value(S("qid")).toString() != S("?");
    m.insert(S("state"), m_running.contains(m_key) ? S("loading")
                         : found ? S("ok")
                         : (m_failed.contains(m_key) || m_noNet || !m_net || !m_net->allows(S("place"))) && !done ? S("offline")
                         : done ? S("none") : S("loading"));
    return m;
}

} // namespace tp::services
