#include "electronimport.h"

#include "datafile.h"
#include "nativeformat.h"
#include "trdata.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace tp::data {

using json::Value;

namespace {

const QStringList &knownRoot()
{
    static const QStringList k = { "version", "plans", "journal", "inbox", "visitedRegions", "seasonGuide", "photoCredits",
                                   "documents", "emergency", "packingTemplate", "nextTripId" };
    return k;
}
const QStringList &knownPlan()
{
    static const QStringList k = { "id", "name", "type", "status", "regions", "startDate", "endDate", "days", "color", "notes",
                                   "guide", "coverPhoto", "seasonRef", "currency", "budget", "pinnedAt", "updatedAt", "createdAt",
                                   "stops", "bookings", "todos", "packing", "expenses", "people", "localEmergency" };
    return k;
}
const QStringList &knownStop()
{
    static const QStringList k = { "id", "name", "lat", "lng", "order", "arriveDate", "departDate", "tz", "elev", "color", "notes",
                                   "schedule", "photos", "legMode", "legPath", "legKey", "legDist", "legTime" };
    return k;
}
const QStringList &knownPhoto()
{
    static const QStringList k = { "id", "path", "caption", "fav", "takenAt", "lat", "lng" };
    return k;
}
const QStringList &knownJournal()
{
    static const QStringList k = { "id", "planId", "stopId", "title", "date", "body" };
    return k;
}

const QRegularExpression &mdPhotoRe()
{
    // ![说明](photos/…)：Electron "插入照片"生成的写法
    static const QRegularExpression re(QStringLiteral("\\]\\(\\s*(photos/[^)\\s]+)\\s*\\)"));
    return re;
}

QString normPath(QString p)
{
    p.replace(u'\\', u'/');
    while (p.startsWith(QLatin1String("./"))) p.remove(0, 2);
    return p;
}

bool isLocalPhotoPath(const QString &p) { return p.startsWith(QLatin1String("photos/")) && !p.contains(QLatin1String("..")); }

QString stableHash(const QString &s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
}

// JS 的 (a.order ?? 0)：null / undefined → 0，其余按 ToNumber（NaN 当 0，比较器返回 NaN 时 JS 当作相等）
double orderOf(const Value &stop)
{
    const Value &o = stop[QStringLiteral("order")];
    if (o.isNull()) return 0;
    const double d = o.jsToNumber();
    return std::isnan(d) ? 0 : d;
}

struct PhotoRec
{
    QString id, key, file, plan, stop;
    bool external = false;
    Value src;          // Electron 的照片对象（只有路径的时候是 null）
    Value credit;       // photoCredits 里的
};

struct PlanInfo
{
    QString id;
    std::vector<int> order;        // 排好序后的原下标
    QStringList stopIds;           // 按原下标
};

} // namespace

QString ElectronImport::safeName(const QString &name)
{
    // JS \s：\t \n \v \f \r 空格      -
    const auto isJsSpace = [](char16_t c) {
        return c == 0x09 || c == 0x0a || c == 0x0b || c == 0x0c || c == 0x0d || c == 0x20 || c == 0xa0 || c == 0x1680
            || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
    };
    const QString bad = QStringLiteral("\\/:*?\"<>|#%");
    const QString in = name.isEmpty() ? QStringLiteral("photo") : name;
    QString out;
    bool inRun = false;
    for (const QChar ch : in) {
        if (bad.contains(ch) || isJsSpace(ch.unicode())) {
            if (!inRun) out += u'_';
            inRun = true;
        } else {
            out += ch;
            inRun = false;
        }
    }
    if (out.size() > 60) out = out.right(60);
    return out.isEmpty() ? QStringLiteral("photo") : out;
}

bool ElectronImport::isElectronFolder(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo(QDir(dir).filePath(nativefmt::electronDataFileName())).isFile();
}

ElectronImport::Converted ElectronImport::convert(const Value &electron, const QDateTime &now, const QString &appVersion,
                                                  const std::function<bool(const QString &)> &fileExists)
{
    Converted out;
    ImportReport &rep = out.report;
    const double nowMs = double(now.toMSecsSinceEpoch());
    const QString nowMonth = now.toString(QStringLiteral("yyyy-MM"));
    const Value &plansIn = electron[QStringLiteral("plans")];

    // ---------- 第一遍：计划 / 站点 id、站点顺序、照片登记 ----------
    std::vector<PhotoRec> photos;
    QHash<QString, int> byKey;      // 路径 → photos 下标
    QSet<QString> usedIds, usedFiles;

    const auto registerPhoto = [&](const Value &ph, const QString &plan, const QString &stop) -> QString {
        QString path;
        if (ph.isString()) path = ph.toString();
        else if (ph.isObject()) path = ph[QStringLiteral("path")].toString();
        path = normPath(path);
        if (path.isEmpty()) return {};
        if (const auto it = byKey.constFind(path); it != byKey.constEnd()) {
            if (ph.isObject()) rep.duplicatePhotos << path;
            return photos[size_t(*it)].id;
        }
        PhotoRec r;
        r.key = path;
        r.plan = plan;
        r.stop = stop;
        r.src = ph.isObject() ? ph : Value();
        QString id = ph.isObject() ? ph[QStringLiteral("id")].toString() : QString();
        if (id.isEmpty() || usedIds.contains(id)) {
            id = QStringLiteral("ph_") + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
            for (int n = 2; usedIds.contains(id); ++n) id = QStringLiteral("ph_%1-%2").arg(stableHash(path)).arg(n);
        }
        usedIds.insert(id);
        r.id = id;
        r.external = !isLocalPhotoPath(path);
        if (r.external) {
            r.file = path;
            rep.externalPhotos << path;
        } else {
            QString month = nowMonth;
            static const QRegularExpression ym(QStringLiteral("^(\\d{4})-(\\d{2})"));
            const QRegularExpressionMatch m = ym.match(r.src[QStringLiteral("takenAt")].toString());
            if (m.hasMatch()) month = m.captured(1) + u'-' + m.captured(2);
            const QString base = path.section(u'/', -1);
            QString fname = base.startsWith(id + u'_') ? safeName(base) : id + u'_' + safeName(base);
            QString file = QStringLiteral("photos/%1/%2").arg(month, fname);
            for (int k = 2; usedFiles.contains(file.toLower()); ++k) {
                const int dot = fname.lastIndexOf(u'.');
                const QString stem = dot > 0 ? fname.left(dot) : fname, ext = dot > 0 ? fname.mid(dot) : QString();
                file = QStringLiteral("photos/%1/%2-%3%4").arg(month, stem).arg(k).arg(ext);
            }
            usedFiles.insert(file.toLower());
            r.file = file;
            if (fileExists(path)) {
                out.copies.append({ path, file });
                const QString orig = QStringLiteral("photos/.originals/") + path.mid(7);
                if (fileExists(orig)) out.originals.append({ orig, QStringLiteral("photos/.originals/") + file.mid(7) });
            } else {
                rep.missingFiles << path;
            }
        }
        byKey.insert(path, int(photos.size()));
        photos.push_back(std::move(r));
        return id;
    };

    std::vector<PlanInfo> infos;
    QSet<QString> planIds;
    for (const Value &p : plansIn.asArray()) {
        PlanInfo info;
        info.id = p[QStringLiteral("id")].toString();
        // 没有 id 的计划 / 站点：按位置算一个固定的 id（再次导入时还是同一个，"只加新的"才认得出来）
        if (p.isObject() && (info.id.isEmpty() || planIds.contains(info.id)))
            info.id = QStringLiteral("p_") + stableHash(QStringLiteral("plan#%1#%2").arg(infos.size()).arg(p[QStringLiteral("name")].toString()));
        planIds.insert(info.id);
        const json::Array &stops = p[QStringLiteral("stops")].asArray();
        info.order.resize(stops.size());
        for (size_t k = 0; k < stops.size(); ++k) info.order[k] = int(k);
        std::stable_sort(info.order.begin(), info.order.end(), [&stops](int a, int b) { return orderOf(stops[size_t(a)]) < orderOf(stops[size_t(b)]); });
        for (size_t k = 0; k < stops.size(); ++k) {
            QString sid = stops[k][QStringLiteral("id")].toString();
            if (stops[k].isObject() && sid.isEmpty()) sid = QStringLiteral("s_") + stableHash(QStringLiteral("%1#%2").arg(info.id).arg(k));
            info.stopIds << sid;
        }
        // 站点照片（按站点顺序登记）
        for (int k : info.order)
            for (const Value &ph : stops[size_t(k)][QStringLiteral("photos")].asArray()) registerPhoto(ph, info.id, info.stopIds.at(k));
        infos.push_back(std::move(info));
    }
    for (const Value &ph : electron[QStringLiteral("inbox")].asArray()) registerPhoto(ph, QString(), QString());
    for (size_t k = 0; k < plansIn.asArray().size(); ++k) {
        const QString cover = plansIn.asArray()[k][QStringLiteral("coverPhoto")].toString();
        if (!cover.isEmpty()) registerPhoto(Value(cover), infos[k].id, QString());
    }
    const auto registerMd = [&](const QString &text, const QString &plan) {
        for (auto it = mdPhotoRe().globalMatch(text); it.hasNext();) registerPhoto(Value(it.next().captured(1)), plan, QString());
    };
    for (size_t k = 0; k < plansIn.asArray().size(); ++k) registerMd(plansIn.asArray()[k][QStringLiteral("guide")].toString(), infos[k].id);
    QHash<QString, QString> planIdMap;   // Electron 计划 id → 原生（一般相同）
    for (size_t k = 0; k < plansIn.asArray().size(); ++k) planIdMap.insert(plansIn.asArray()[k][QStringLiteral("id")].toString(), infos[k].id);
    for (const Value &j : electron[QStringLiteral("journal")].asArray())
        registerMd(j[QStringLiteral("body")].toString(), planIdMap.value(j[QStringLiteral("planId")].toString()));
    // 照片署名：只给登记过的照片
    for (const auto &[path, credit] : electron[QStringLiteral("photoCredits")].asObject().members()) {
        const auto it = byKey.constFind(normPath(path));
        if (it != byKey.constEnd()) photos[size_t(*it)].credit = credit;
    }

    const auto replaceMd = [&](const QString &text) {
        QString outText;
        int last = 0;
        for (auto it = mdPhotoRe().globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            const auto f = byKey.constFind(normPath(m.captured(1)));
            if (f == byKey.constEnd()) continue;
            outText += text.mid(last, m.capturedStart() - last);
            outText += QStringLiteral("](photo:") + photos[size_t(*f)].id + u')';
            last = m.capturedEnd();
        }
        return last == 0 ? text : outText + text.mid(last);
    };
    const auto noteUnknown = [&rep](const QString &where, const QString &key) {
        const QString s = where + u'.' + key;
        if (!rep.unknownFields.contains(s)) rep.unknownFields << s;
    };

    // ---------- 第二遍：生成原生格式 ----------
    const auto convertStop = [&](const Value &s, const QString &sid) -> Value {
        if (!s.isObject()) return s;
        json::Object o;
        json::Object leg;
        bool legPlaced = false;
        for (const auto &[k, v] : s.asObject().members()) {
            if (k == QLatin1String("order") || k == QLatin1String("photos")) continue;
            if (k.startsWith(QLatin1String("leg")) && knownStop().contains(k)) {
                if (!legPlaced) { o.set(QStringLiteral("leg"), Value::object()); legPlaced = true; }
                continue;
            }
            if (k == QLatin1String("schedule") && v.isString()) {
                json::Array a;
                if (!v.toString().trimmed().isEmpty()) {
                    json::Object item;
                    item.set(QStringLiteral("text"), v);
                    a.push_back(Value(std::move(item)));
                }
                o.set(k, Value(std::move(a)));
                continue;
            }
            if (!knownStop().contains(k)) noteUnknown(trd("站点"), k);
            o.set(k, v);
        }
        if (!o.contains(QStringLiteral("id"))) {
            // 没有 id 的站点：放在最前面，照片的归属要用它
            json::Object withId;
            withId.set(QStringLiteral("id"), Value(sid));
            for (auto &m : o.members()) withId.set(m.first, m.second);
            o = std::move(withId);
        }
        if (legPlaced) {
            const struct { const char *from, *to; } map[] = { { "legMode", "mode" }, { "legPath", "path" }, { "legDist", "dist" },
                                                              { "legTime", "time" }, { "legKey", "key" } };
            for (const auto &m : map)
                if (const Value *v = s.asObject().find(QString::fromLatin1(m.from))) leg.set(QString::fromLatin1(m.to), *v);
            *o.find(QStringLiteral("leg")) = Value(std::move(leg));
        }
        return Value(std::move(o));
    };

    json::Array plansOut;
    for (size_t pi = 0; pi < plansIn.asArray().size(); ++pi) {
        const Value &p = plansIn.asArray()[pi];
        const PlanInfo &info = infos[pi];
        if (!p.isObject()) { plansOut.push_back(p); continue; }
        json::Object o;
        bool createdPlaced = p.asObject().contains(QStringLiteral("createdAt"));
        if (!p.asObject().contains(QStringLiteral("id"))) o.set(QStringLiteral("id"), Value(info.id));
        for (const auto &[k, v] : p.asObject().members()) {
            if (k == QLatin1String("id")) { o.set(k, Value(info.id)); continue; }
            if (k == QLatin1String("coverPhoto")) {
                const QString path = normPath(v.toString());
                const auto it = byKey.constFind(path);
                o.set(QStringLiteral("cover"), Value(it != byKey.constEnd() ? photos[size_t(*it)].id : QString()));
                continue;
            }
            if (k == QLatin1String("stops")) {
                json::Array a;
                const json::Array &stops = v.asArray();
                for (int idx : info.order) a.push_back(convertStop(stops[size_t(idx)], info.stopIds.at(idx)));
                rep.stops += int(a.size());
                o.set(k, Value(std::move(a)));
                continue;
            }
            if (k == QLatin1String("localEmergency") && v.isString()) {
                json::Array a;
                if (!v.toString().trimmed().isEmpty()) {
                    json::Object item;
                    item.set(QStringLiteral("label"), v);
                    item.set(QStringLiteral("phone"), Value(QString()));
                    a.push_back(Value(std::move(item)));
                }
                o.set(k, Value(std::move(a)));
                continue;
            }
            if (k == QLatin1String("guide") && v.isString()) { o.set(k, Value(replaceMd(v.toString()))); continue; }
            if (k == QLatin1String("updatedAt") && !createdPlaced) {
                o.set(QStringLiteral("createdAt"), Value(nowMs));
                createdPlaced = true;
            }
            if (!knownPlan().contains(k)) noteUnknown(trd("计划"), k);
            o.set(k, v);
        }
        if (!createdPlaced) o.set(QStringLiteral("createdAt"), Value(nowMs));
        plansOut.push_back(Value(std::move(o)));
    }
    rep.plans = int(plansOut.size());

    json::Array photosOut;
    for (const PhotoRec &r : photos) {
        json::Object o;
        o.set(QStringLiteral("id"), Value(r.id));
        o.set(QStringLiteral("file"), Value(r.file));
        o.set(QStringLiteral("plan"), Value(r.plan));
        o.set(QStringLiteral("stop"), Value(r.stop));
        const json::Object &src = r.src.asObject();
        const Value *cap = src.find(QStringLiteral("caption"));
        o.set(QStringLiteral("caption"), cap ? *cap : Value(QString()));
        const Value *fav = src.find(QStringLiteral("fav"));
        o.set(QStringLiteral("fav"), fav ? *fav : Value(false));
        for (const char *k : { "takenAt", "lat", "lng" })
            if (const Value *v = src.find(QString::fromLatin1(k))) o.set(QString::fromLatin1(k), *v);
        if (!r.credit.isNull()) o.set(QStringLiteral("credit"), r.credit);
        o.set(QStringLiteral("addedAt"), Value(nowMs));
        for (const auto &[k, v] : src.members()) {
            if (knownPhoto().contains(k)) continue;
            noteUnknown(trd("照片"), k);
            if (!o.contains(k)) o.set(k, v);
        }
        photosOut.push_back(Value(std::move(o)));
    }
    rep.photos = int(photosOut.size());
    for (const PhotoRec &r : photos)
        if (!r.external) out.referenced << r.key;

    json::Array journalOut;
    for (const Value &j : electron[QStringLiteral("journal")].asArray()) {
        if (!j.isObject()) { journalOut.push_back(j); continue; }
        json::Object o;
        for (const auto &[k, v] : j.asObject().members()) {
            if (k == QLatin1String("planId")) { o.set(QStringLiteral("plan"), Value(planIdMap.value(v.toString(), v.toString()))); continue; }
            if (k == QLatin1String("stopId")) { o.set(QStringLiteral("stop"), v); continue; }
            if (k == QLatin1String("body") && v.isString()) { o.set(k, Value(replaceMd(v.toString()))); continue; }
            if (!knownJournal().contains(k)) noteUnknown(trd("游记"), k);
            o.set(k, v);
        }
        journalOut.push_back(Value(std::move(o)));
    }
    rep.journal = int(journalOut.size());

    json::Object root = nativefmt::emptyDoc(appVersion, now).asObject();
    root.set(QStringLiteral("plans"), Value(std::move(plansOut)));
    root.set(QStringLiteral("photos"), Value(std::move(photosOut)));
    root.set(QStringLiteral("journal"), Value(std::move(journalOut)));
    const json::Object &ein = electron.asObject();
    if (const Value *v = ein.find(QStringLiteral("documents"))) root.set(QStringLiteral("documents"), *v);
    if (const Value *v = ein.find(QStringLiteral("emergency"))) root.set(QStringLiteral("emergency"), *v);
    if (const Value *v = ein.find(QStringLiteral("visitedRegions"))) root.set(QStringLiteral("visitedRegions"), *v);
    for (const char *k : { "seasonGuide", "packingTemplate", "nextTripId" })
        if (const Value *v = ein.find(QString::fromLatin1(k))) root.set(QString::fromLatin1(k), *v);
    for (const auto &[k, v] : ein.members()) {
        if (knownRoot().contains(k)) continue;
        noteUnknown(trd("顶层"), k);
        if (!root.contains(k)) root.set(k, v);
    }
    out.doc = Value(std::move(root));
    return out;
}

namespace {
bool isEmptyValue(const Value &v)
{
    switch (v.type()) {
    case Value::Type::Null: return true;
    case Value::Type::String: return v.toString().isEmpty();
    case Value::Type::Array: return v.asArray().empty();
    case Value::Type::Object: return v.asObject().isEmpty();
    default: return false;
    }
}
}

Value ElectronImport::merge(const Value &existing, Converted &conv, Mode mode)
{
    if (mode == Mode::Replace || !existing.isObject()) return conv.doc;
    Value result = existing;
    json::Object &ro = result.mutObject();
    nativefmt::normalize(result);
    const json::Object &in = conv.doc.asObject();

    QSet<QString> have, skipped;
    for (const Value &p : ro.find(QStringLiteral("plans"))->asArray()) have.insert(p[QStringLiteral("id")].toString());
    int added = 0;
    {
        json::Array &plans = ro.find(QStringLiteral("plans"))->mutArray();
        for (const Value &p : in.find(QStringLiteral("plans"))->asArray()) {
            const QString id = p[QStringLiteral("id")].toString();
            if (have.contains(id)) { skipped.insert(id); continue; }
            have.insert(id);
            plans.push_back(p);
            ++added;
        }
    }
    conv.report.plansSkipped = int(skipped.size());
    conv.report.plans = added;

    QSet<QString> photoIds, keptFiles;
    for (const Value &p : ro.find(QStringLiteral("photos"))->asArray()) photoIds.insert(p[QStringLiteral("id")].toString());
    int photosAdded = 0;
    {
        json::Array &arr = ro.find(QStringLiteral("photos"))->mutArray();
        for (const Value &p : in.find(QStringLiteral("photos"))->asArray()) {
            const QString id = p[QStringLiteral("id")].toString();
            if (photoIds.contains(id) || skipped.contains(p[QStringLiteral("plan")].toString())) continue;
            photoIds.insert(id);
            arr.push_back(p);
            keptFiles.insert(p[QStringLiteral("file")].toString());
            ++photosAdded;
        }
    }
    conv.report.photos = photosAdded;
    const auto keep = [&keptFiles](QList<ImportCopy> &list, bool original) {
        QList<ImportCopy> out;
        for (const ImportCopy &c : list) {
            const QString f = original ? QStringLiteral("photos/") + c.dst.mid(QStringLiteral("photos/.originals/").size()) : c.dst;
            if (keptFiles.contains(f)) out << c;
        }
        list = out;
    };
    keep(conv.copies, false);
    keep(conv.originals, true);

    const auto addById = [&](const QString &key, const std::function<bool(const Value &)> &extra) -> int {
        json::Value *dst = ro.find(key);
        if (!dst || !dst->isArray()) { ro.set(key, Value::array()); dst = ro.find(key); }
        QSet<QString> ids;
        for (const Value &v : dst->asArray()) ids.insert(v[QStringLiteral("id")].toString());
        int n = 0;
        json::Array &arr = dst->mutArray();
        for (const Value &v : in.find(key) ? in.find(key)->asArray() : json::Array{}) {
            const QString id = v[QStringLiteral("id")].toString();
            if ((!id.isEmpty() && ids.contains(id)) || (extra && !extra(v))) continue;
            ids.insert(id);
            arr.push_back(v);
            ++n;
        }
        return n;
    };
    conv.report.journal = addById(QStringLiteral("journal"), [&skipped](const Value &j) { return !skipped.contains(j[QStringLiteral("plan")].toString()); });
    addById(QStringLiteral("documents"), {});
    addById(QStringLiteral("visitedRegions"), {});

    int stops = 0;
    for (const Value &p : in.find(QStringLiteral("plans"))->asArray())
        if (!skipped.contains(p[QStringLiteral("id")].toString())) stops += int(p[QStringLiteral("stops")].asArray().size());
    conv.report.stops = stops;

    static const QStringList known = { "format", "formatVersion", "savedBy", "savedAt", "plans", "photos", "journal", "documents", "visitedRegions" };
    for (const auto &[k, v] : in.members()) {
        if (known.contains(k)) continue;
        const Value *cur = ro.find(k);
        if (!cur || isEmptyValue(*cur)) ro.set(k, v);
    }
    return result;
}

ImportReport ElectronImport::run(const QString &electronDir, const QString &nativeDir, const Value &existing, Mode mode,
                                 const QDateTime &now, const QString &appVersion, Value *result,
                                 const std::function<bool(int, int)> &progress, const QHash<QString, planmerge::Mode> &picks)
{
    ImportReport fail;
    const QDir src(electronDir), dst(nativeDir);
    QFile f(src.filePath(nativefmt::electronDataFileName()));
    if (!f.open(QIODevice::ReadOnly)) {
        fail.error = f.exists() ? trd("data.js 打不开：%1").arg(f.errorString()) : trd("这个文件夹里没有 Electron 版的 data.js");
        return fail;
    }
    const ParsedDataFile parsed = parseDataFile(f.readAll());
    f.close();
    if (!parsed.ok) { fail.error = trd("data.js 读不出来：%1").arg(parsed.error); return fail; }
    if (!parsed.data.isObject()) { fail.error = trd("data.js 里没有数据"); return fail; }

    Converted conv = convert(parsed.data, now, appVersion,
                             [&src](const QString &rel) { return QFileInfo(src.filePath(rel)).isFile(); });
    // 逐个计划选（0.14.1）要用转换后、合并前的全部照片来源
    QHash<QString, QString> srcOf, origOf;   // 原生相对路径 → Electron 相对路径
    for (const ImportCopy &c : conv.copies) srcOf.insert(c.dst, c.src);
    for (const ImportCopy &c : conv.originals) origOf.insert(QStringLiteral("photos/") + c.dst.mid(QStringLiteral("photos/.originals/").size()), c.src);
    const Value incoming = conv.doc;
    Value doc = merge(existing, conv, existing.isObject() ? mode : Mode::Replace);
    ImportReport rep = conv.report;
    planmerge::Result picked;
    if (mode == Mode::AddNew && existing.isObject() && !picks.isEmpty()) {
        planmerge::Options po;
        po.uid = [](const QString &prefix) {
            return prefix + u'_' + QString::number(QDateTime::currentMSecsSinceEpoch(), 36) + QString::number(QRandomGenerator::global()->bounded(1u << 30), 36);
        };
        po.copySuffix = trd("（导入）");
        picked = planmerge::apply(doc, incoming, picks, po);
        doc = picked.doc;
        rep.plans += picked.plans;
        rep.plansSkipped -= picked.plans;
        rep.photos += picked.photos;
        rep.journal += picked.journal;
        for (const Value &p : incoming[QStringLiteral("plans")].asArray())
            if (picked.planIds.contains(p[QStringLiteral("id")].toString())) rep.stops += int(p[QStringLiteral("stops")].asArray().size());
    }

    // Electron photos\ 里没被引用的文件（只报告）
    {
        QSet<QString> referenced(conv.referenced.cbegin(), conv.referenced.cend());
        const QString root = src.filePath(QStringLiteral("photos"));
        QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        static const QRegularExpression tmpRe(QStringLiteral("\\.tmp(-\\d+)?$"));
        while (it.hasNext()) {
            const QString abs = it.next();
            const QString rel = src.relativeFilePath(abs);
            if (rel.startsWith(QLatin1String("photos/.thumbs/")) || rel.startsWith(QLatin1String("photos/.originals/")) || tmpRe.match(rel).hasMatch())
                continue;
            if (!referenced.contains(rel)) rep.unusedFiles << rel;
        }
        rep.unusedFiles.sort();
    }

    // 复制照片
    QList<ImportCopy> jobs = conv.copies;
    const int nMain = int(jobs.size());
    jobs += conv.originals;
    const int total = int(jobs.size());
    if (progress && !progress(0, total)) { rep.cancelled = true; return rep; }
    for (int k = 0; k < total; ++k) {
        ImportCopy c = jobs.at(k);
        const QString from = src.filePath(c.src);
        QString to = dst.filePath(c.dst);
        const qint64 size = QFileInfo(from).size();
        if (QFileInfo::exists(to)) {
            // 再次导入时，上次复制的同一张照片还在：内容一样就直接用，不一样就另起一个名字
            QFile a(from), b(to);
            bool same = QFileInfo(to).size() == size && a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly)
                        && QCryptographicHash::hash(a.readAll(), QCryptographicHash::Sha1) == QCryptographicHash::hash(b.readAll(), QCryptographicHash::Sha1);
            if (same) {
                if (k < nMain) ++rep.photosCopied; else ++rep.originalsCopied;
                if (progress && !progress(k + 1, total)) { rollback(rep.copiedFiles); rep.copiedFiles.clear(); rep.cancelled = true; return rep; }
                continue;
            }
            const QString oldRel = c.dst;
            const int dot = oldRel.lastIndexOf(u'.');
            for (int n = 2;; ++n) {
                c.dst = dot > oldRel.lastIndexOf(u'/') ? QStringLiteral("%1-%2%3").arg(oldRel.left(dot)).arg(n).arg(oldRel.mid(dot))
                                                        : QStringLiteral("%1-%2").arg(oldRel).arg(n);
                if (!QFileInfo::exists(dst.filePath(c.dst))) break;
            }
            to = dst.filePath(c.dst);
            if (k < nMain) {
                // 照片记录里的 file 跟着改；对应的原图也改名
                json::Array &arr = doc.mutObject().find(QStringLiteral("photos"))->mutArray();
                for (Value &p : arr)
                    if (p[QStringLiteral("file")].toString() == oldRel) p.mutObject().set(QStringLiteral("file"), Value(c.dst));
                for (int j = nMain; j < total; ++j)
                    if (jobs[j].dst == QStringLiteral("photos/.originals/") + oldRel.mid(7))
                        jobs[j].dst = QStringLiteral("photos/.originals/") + c.dst.mid(7);
            }
        }
        if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !QFile::copy(from, to) || QFileInfo(to).size() != size) {
            QFile in(from);
            rep.error = trd("复制 %1 失败：%2").arg(QDir::toNativeSeparators(c.src),
                                                 in.exists() ? (in.open(QIODevice::ReadOnly) ? trd("写不进数据文件夹") : in.errorString()) : trd("文件不见了"));
            if (QFileInfo::exists(to) && !rep.copiedFiles.contains(to)) QFile::remove(to);
            rollback(rep.copiedFiles);
            rep.copiedFiles.clear();
            return rep;
        }
        rep.copiedFiles << to;
        if (k < nMain) ++rep.photosCopied; else ++rep.originalsCopied;
        if (progress && !progress(k + 1, total)) { rollback(rep.copiedFiles); rep.copiedFiles.clear(); rep.cancelled = true; return rep; }
    }
    if (!picked.files.isEmpty()) {
        const auto readRel = [&src](const QString &rel, QByteArray *out) {
            if (rel.isEmpty()) return false;
            QFile f(src.filePath(rel));
            if (!f.open(QIODevice::ReadOnly)) return false;
            *out = f.readAll();
            return true;
        };
        const planmerge::WriteResult w = planmerge::writeFiles(
            doc, nativeDir, picked.files, [&](const QString &from, QByteArray *b) { return readRel(srcOf.value(from), b); },
            [&](const QString &from, QByteArray *b) { return readRel(origOf.value(from), b); });
        if (!w.ok) {
            rep.error = w.error;
            rollback(rep.copiedFiles);
            rep.copiedFiles.clear();
            return rep;
        }
        rep.copiedFiles += w.copied;
        rep.photosCopied += w.written + w.same;
    }
    rep.ok = true;
    if (result) *result = std::move(doc);
    return rep;
}

QVariantList ElectronImport::conflicts(const QString &electronDir, const Value &existing, const QDateTime &now, QString *error)
{
    QVariantList out;
    QFile f(QDir(electronDir).filePath(nativefmt::electronDataFileName()));
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = trd("这个文件夹里没有 Electron 版的 data.js"); return out; }
    const ParsedDataFile parsed = parseDataFile(f.readAll());
    if (!parsed.ok || !parsed.data.isObject()) { if (error) *error = trd("data.js 读不出来：%1").arg(parsed.error); return out; }
    const QDir src(electronDir);
    const Converted conv = convert(parsed.data, now, QString(), [&src](const QString &rel) { return QFileInfo(src.filePath(rel)).isFile(); });
    for (const Value &p : conv.doc[QStringLiteral("plans")].asArray()) {
        const QString id = p[QStringLiteral("id")].toString();
        const QVariantMap local = planmerge::summary(existing, id);
        if (local.isEmpty()) continue;
        out << QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("name"), p[QStringLiteral("name")].toString() },
                            { QStringLiteral("local"), local }, { QStringLiteral("incoming"), planmerge::summary(conv.doc, id) } };
    }
    return out;
}

void ElectronImport::rollback(const QStringList &copiedFiles)
{
    for (const QString &f : copiedFiles) {
        QFile::remove(f);
        // 顺手删掉因此变空的月份文件夹（不删 photos 本身）
        QDir d = QFileInfo(f).dir();
        if (d.dirName() != QLatin1String("photos") && d.isEmpty()) d.rmdir(d.absolutePath());
    }
}

} // namespace tp::data
