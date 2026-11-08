#include "trippack.h"

#include "electronimport.h"
#include "jsonio.h"
#include "nativeformat.h"
#include "planmerge.h"
#include "translator.h"

#include "miniz.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cstring>

namespace tp::exporter::trip {

using json::Value;

namespace {

QString str(const Value &o, const char *k) { return o[QString::fromLatin1(k)].toString(); }

const QRegularExpression &refRe()
{
    static const QRegularExpression re(QStringLiteral("\\]\\(photo:([^)\\s]+)\\)"));
    return re;
}

// ](photo:<id>) → ](<文件路径>)（Electron 正文里写的是路径）
QString refsToPaths(const QString &text, const QHash<QString, QString> &fileOf)
{
    if (!text.contains(QLatin1String("](photo:"))) return text;
    QString out;
    int last = 0;
    for (auto it = refRe().globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString f = fileOf.value(m.captured(1));
        if (f.isEmpty()) continue;
        out += text.mid(last, m.capturedStart() - last) + QStringLiteral("](") + f + u')';
        last = m.capturedEnd();
    }
    return last == 0 ? text : out + text.mid(last);
}

bool truthy(const Value &v)
{
    switch (v.type()) {
    case Value::Type::Bool: return v.toBool();
    case Value::Type::Number: return v.toNumber() != 0 && v.toNumber() == v.toNumber();
    case Value::Type::String: return !v.toString().isEmpty();
    case Value::Type::Array: case Value::Type::Object: return true;
    default: return false;
    }
}

void fillCurrency(Value &plan, const QString &cur)
{
    if (cur.isEmpty() || !str(plan, "currency").isEmpty()) return;
    if (!plan[QStringLiteral("expenses")].asArray().empty() || truthy(plan[QStringLiteral("budget")]))
        plan.mutObject().set(QStringLiteral("currency"), Value(cur));
}

Value electronStop(const Value &s, int index, const QList<Value> &recs)
{
    if (!s.isObject()) return s;
    json::Object o;
    for (const auto &[k, v] : s.asObject().members()) {
        if (k == QLatin1String("leg") && v.isObject()) {
            const struct { const char *from, *to; } map[] = { { "mode", "legMode" }, { "path", "legPath" }, { "dist", "legDist" },
                                                              { "time", "legTime" }, { "key", "legKey" } };
            for (const auto &m : map)
                if (const Value *x = v.asObject().find(QString::fromLatin1(m.from))) o.set(QString::fromLatin1(m.to), *x);
            continue;
        }
        o.set(k, v);
    }
    o.set(QStringLiteral("order"), Value(index));
    json::Array photos;
    for (const Value &r : recs) {
        if (str(r, "stop") != str(s, "id")) continue;
        json::Object ph;
        ph.set(QStringLiteral("id"), r[QStringLiteral("id")]);
        ph.set(QStringLiteral("path"), r[QStringLiteral("file")]);
        if (!str(r, "caption").isEmpty()) ph.set(QStringLiteral("caption"), r[QStringLiteral("caption")]);
        if (r[QStringLiteral("fav")].toBool()) ph.set(QStringLiteral("fav"), Value(true));
        for (const char *k : { "takenAt", "lat", "lng" })
            if (const Value *x = r.asObject().find(QString::fromLatin1(k))) ph.set(QString::fromLatin1(k), *x);
        photos.push_back(Value(std::move(ph)));
    }
    if (!photos.empty()) o.set(QStringLiteral("photos"), Value(std::move(photos)));
    return Value(std::move(o));
}

Value electronPlan(const Value &p, const QList<Value> &recs, const QHash<QString, QString> &fileOf)
{
    json::Object o;
    for (const auto &[k, v] : p.asObject().members()) {
        if (k == QLatin1String("cover")) {
            const QString f = fileOf.value(v.toString());
            if (!f.isEmpty()) o.set(QStringLiteral("coverPhoto"), Value(f));
            continue;
        }
        if (k == QLatin1String("stops")) {
            json::Array a;
            int i = 0;
            for (const Value &s : v.asArray()) a.push_back(electronStop(s, i++, recs));
            o.set(k, Value(std::move(a)));
            continue;
        }
        if (k == QLatin1String("guide") && v.isString()) { o.set(k, Value(refsToPaths(v.toString(), fileOf))); continue; }
        o.set(k, v);
    }
    return Value(std::move(o));
}

Value electronJournal(const Value &j, const QHash<QString, QString> &fileOf)
{
    if (!j.isObject()) return j;
    json::Object o;
    for (const auto &[k, v] : j.asObject().members()) {
        if (k == QLatin1String("plan")) o.set(QStringLiteral("planId"), v);
        else if (k == QLatin1String("stop")) o.set(QStringLiteral("stopId"), v);
        else if (k == QLatin1String("body") && v.isString()) o.set(k, Value(refsToPaths(v.toString(), fileOf)));
        else o.set(k, v);
    }
    return Value(std::move(o));
}

bool isLocal(const QString &rel) { return safeRel(rel); }

} // namespace

QString formatName() { return QStringLiteral("travel-planner.trip"); }

bool safeRel(const QString &p)
{
    if (!p.startsWith(QLatin1String("photos/")) || p.contains(u'\\') || p.contains(u':') || p.contains(QChar(0))) return false;
    for (const QString &seg : p.split(u'/'))
        if (seg.isEmpty() || seg == QLatin1String(".") || seg == QLatin1String("..")) return false;
    return true;
}

QString safeFileName(const QString &name, int max)
{
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|]+"));
    return QString(name).replace(bad, QStringLiteral("_")).trimmed().left(max);
}

QString uniquePath(const QString &dir, const QString &baseName, const QString &ext)
{
    const QDir d(dir);
    QString p = d.filePath(baseName + ext);
    for (int k = 2; QFileInfo::exists(p); ++k) p = d.filePath(QStringLiteral("%1-%2%3").arg(baseName).arg(k).arg(ext));
    return p;
}

// ---------- 导出 ----------
Built build(const Value &doc, const QStringList &planIds, const QString &cur, const QString &appVersion, const QDateTime &now)
{
    Built b;
    const QSet<QString> ids(planIds.cbegin(), planIds.cend());
    const json::Array &allPhotos = doc[QStringLiteral("photos")].asArray();
    QHash<QString, int> byId;
    for (size_t k = 0; k < allPhotos.size(); ++k) byId.insert(str(allPhotos[k], "id"), int(k));

    json::Array ePlans, eJournal, nPlans, nPhotos, nJournal;
    json::Object credits;
    QSet<QString> fileSet, added;
    for (const Value &p0 : doc[QStringLiteral("plans")].asArray()) {
        const QString id = str(p0, "id");
        if (!p0.isObject() || !ids.contains(id)) continue;
        Value p = p0;
        fillCurrency(p, cur);
        QList<Value> recs;
        for (const Value &r : allPhotos)
            if (str(r, "plan") == id) recs << r;
        // 封面、攻略、游记里引用的、不属于这个计划的照片也带上（算这个计划的、不在站点）
        QStringList refs{ str(p, "cover") };
        refs += data::planmerge::photoRefs(str(p, "guide"));
        QList<Value> journal;
        for (const Value &j : doc[QStringLiteral("journal")].asArray())
            if (str(j, "plan") == id) { journal << j; refs += data::planmerge::photoRefs(str(j, "body")); }
        for (const QString &ref : refs) {
            const auto it = byId.constFind(ref);
            if (ref.isEmpty() || it == byId.constEnd() || str(allPhotos[size_t(*it)], "plan") == id) continue;
            bool dup = false;
            for (const Value &r : recs) dup = dup || str(r, "id") == ref;
            if (dup) continue;
            Value r = allPhotos[size_t(*it)];
            r.mutObject().set(QStringLiteral("plan"), Value(id));
            r.mutObject().set(QStringLiteral("stop"), Value(QString()));
            recs << r;
        }
        QHash<QString, QString> fileOf;
        for (const Value &r : recs) {
            const QString f = str(r, "file");
            fileOf.insert(str(r, "id"), f);
            if (added.contains(str(r, "id"))) continue;
            added.insert(str(r, "id"));
            nPhotos.push_back(r);
            if (isLocal(f) && !fileSet.contains(f)) { fileSet.insert(f); b.photos << f; }
            if (r[QStringLiteral("credit")].isObject()) credits.set(f, r[QStringLiteral("credit")]);
        }
        ePlans.push_back(electronPlan(p, recs, fileOf));
        for (const Value &j : journal) { nJournal.push_back(j); eJournal.push_back(electronJournal(j, fileOf)); }
        nPlans.push_back(p);
        ++b.plans;
    }

    json::Object m;
    m.set(QStringLiteral("format"), Value(formatName()));
    const bool many = b.plans != 1;
    m.set(QStringLiteral("formatVersion"), Value(many ? 2 : 1));
    if (many) m.set(QStringLiteral("plans"), Value(ePlans));
    else m.set(QStringLiteral("plan"), ePlans.front());
    m.set(QStringLiteral("journal"), Value(std::move(eJournal)));
    m.set(QStringLiteral("photoCredits"), Value(std::move(credits)));
    m.set(QStringLiteral("appVersion"), Value(appVersion));
    m.set(QStringLiteral("exportedAt"), Value(now.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"))));
    json::Object native;
    native.set(QStringLiteral("formatVersion"), Value(data::nativefmt::kFormatVersion));
    native.set(QStringLiteral("plans"), Value(std::move(nPlans)));
    native.set(QStringLiteral("photos"), Value(std::move(nPhotos)));
    native.set(QStringLiteral("journal"), Value(std::move(nJournal)));
    m.set(QStringLiteral("native"), Value(std::move(native)));
    m.set(QStringLiteral("photos"), Value::array());
    b.manifest = Value(std::move(m));
    return b;
}

WriteResult write(const QString &path, Value manifest, const QStringList &photos, const QString &dataDir)
{
    WriteResult w;
    const QDir src(dataDir);
    QStringList take;
    for (const QString &rel : photos) {
        const QFileInfo fi(src.filePath(rel));
        if (safeRel(rel) && fi.isFile() && fi.isReadable()) take << rel;
        else w.missing << rel;
    }
    json::Array list;
    for (const QString &rel : take) list.push_back(Value(rel));
    manifest.mutObject().set(QStringLiteral("photos"), Value(std::move(list)));
    const QByteArray planJson = json::stringify(manifest, 2).toUtf8();

    const QString tmp = path + QStringLiteral(".tmp-%1").arg(QCoreApplication::applicationPid());
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    const auto fail = [&](const QString &why) {
        mz_zip_writer_end(&zip);
        QFile::remove(tmp);
        w.error = why;
        return w;
    };
    if (!mz_zip_writer_init_file(&zip, tmp.toUtf8().constData(), 0)) return fail(tp::tr("写不了这个位置：%1").arg(QDir::toNativeSeparators(path)));
    if (!mz_zip_writer_add_mem(&zip, "plan.json", planJson.constData(), size_t(planJson.size()), 6))
        return fail(tp::tr("打包失败：%1").arg(QString::fromUtf8(mz_zip_get_error_string(mz_zip_get_last_error(&zip)))));
    for (const QString &rel : take) {
        QFile f(src.filePath(rel));
        if (!f.open(QIODevice::ReadOnly)) { w.missing << rel; continue; }
        const QByteArray bytes = f.readAll();
        if (!mz_zip_writer_add_mem(&zip, rel.toUtf8().constData(), bytes.constData(), size_t(bytes.size()), MZ_NO_COMPRESSION))
            return fail(tp::tr("打包失败：%1").arg(QString::fromUtf8(mz_zip_get_error_string(mz_zip_get_last_error(&zip)))));
        ++w.photos;
    }
    if (!mz_zip_writer_finalize_archive(&zip)) return fail(tp::tr("打包失败：%1").arg(QString::fromUtf8(mz_zip_get_error_string(mz_zip_get_last_error(&zip)))));
    mz_zip_writer_end(&zip);
    if (QFileInfo::exists(path) && !QFile::remove(path)) { QFile::remove(tmp); w.error = tp::tr("覆盖不了原来的文件：%1").arg(QDir::toNativeSeparators(path)); return w; }
    if (!QFile::rename(tmp, path)) { QFile::remove(tmp); w.error = tp::tr("写不了这个位置：%1").arg(QDir::toNativeSeparators(path)); return w; }
    w.ok = true;
    w.file = QDir::toNativeSeparators(path);
    w.size = QFileInfo(path).size();
    return w;
}

// ---------- 导入 ----------
struct Reader::Zip
{
    mz_zip_archive a;
    bool open = false;
    Zip() { std::memset(&a, 0, sizeof a); }
    ~Zip() { if (open) mz_zip_reader_end(&a); }
};

Reader::Reader() = default;
Reader::~Reader() = default;

void Reader::close()
{
    m_zip.reset();
    m_index.clear();
}

bool Reader::open(const QString &path)
{
    close();
    m_manifest = Value();
    m_zip = std::make_unique<Zip>();
    const auto bad = [this](const QString &why) { m_error = tp::tr("不是有效的计划文件（%1）").arg(why); close(); return false; };
    if (!mz_zip_reader_init_file(&m_zip->a, path.toUtf8().constData(), 0)) {
        if (!QFileInfo(path).isFile()) { m_error = tp::tr("找不到这个文件"); close(); return false; }
        return bad(tp::tr("不是 zip 文件"));
    }
    m_zip->open = true;
    const mz_uint n = mz_zip_reader_get_num_files(&m_zip->a);
    if (n > mz_uint(kMaxEntries)) return bad(tp::tr("文件里的条目太多"));
    qint64 total = 0;
    int planIdx = -1;
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&m_zip->a, i, &st)) return bad(tp::tr("zip 目录坏了"));
        total += qint64(st.m_uncomp_size);
        if (total > kMaxTotal) return bad(tp::tr("文件解压后太大"));
        if (st.m_is_directory) continue;
        const QString name = QString::fromUtf8(st.m_filename);
        if (name == QLatin1String("plan.json")) planIdx = int(i);
        else if (safeRel(name)) m_index.insert(name, i);   // 其他路径（../、绝对路径、反斜杠……）一律不认
    }
    if (planIdx < 0) return bad(tp::tr("缺少 plan.json"));
    size_t sz = 0;
    void *p = mz_zip_reader_extract_to_heap(&m_zip->a, mz_uint(planIdx), &sz, 0);
    if (!p) { m_error = tp::tr("计划文件里的 plan.json 读不出来"); close(); return false; }
    const QString text = QString::fromUtf8(static_cast<const char *>(p), qsizetype(sz));
    mz_free(p);
    json::ParseError perr;
    Value m = json::parse(text, &perr);
    if (!perr.ok || !m.isObject()) { m_error = tp::tr("计划文件里的 plan.json 读不出来"); close(); return false; }
    if (str(m, "format") != formatName()) { m_error = tp::tr("不是「旅行计划」的计划文件"); close(); return false; }
    const double fv = m[QStringLiteral("formatVersion")].toNumber();
    if (!(fv >= 1)) { m_error = tp::tr("计划文件的格式版本不对"); close(); return false; }
    if (fv > kMaxVersion) { m_error = tp::tr("这个计划文件是更新版本的旅行计划导出的，请先更新软件"); close(); return false; }
    const auto okPlan = [](const Value &x) { return x.isObject() && x[QStringLiteral("id")].isString(); };
    bool ok = true;
    if (m[QStringLiteral("plans")].isArray()) {
        ok = !m[QStringLiteral("plans")].asArray().empty();
        for (const Value &x : m[QStringLiteral("plans")].asArray()) ok = ok && okPlan(x);
    } else ok = okPlan(m[QStringLiteral("plan")]);
    if (!ok) { m_error = tp::tr("计划文件里没有计划数据"); close(); return false; }
    m_manifest = std::move(m);
    m_error.clear();
    return true;
}

QStringList Reader::files() const { return m_index.keys(); }

bool Reader::read(const QString &rel, QByteArray *out)
{
    const auto it = m_index.constFind(rel);
    if (!m_zip || it == m_index.constEnd()) return false;
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&m_zip->a, *it, &st) || st.m_uncomp_size > quint64(kMaxTotal)) return false;
    out->resize(qsizetype(st.m_uncomp_size));
    return mz_zip_reader_extract_to_mem(&m_zip->a, *it, out->data(), size_t(out->size()), 0);
}

Value toNative(const Value &m, const QStringList &packageFiles, const QDateTime &now, QHash<QString, QString> *srcOf)
{
    const QSet<QString> pkg(packageFiles.cbegin(), packageFiles.cend());
    json::Object out;
    const Value &nat = m[QStringLiteral("native")];
    bool useNative = nat.isObject() && nat[QStringLiteral("plans")].isArray() && !nat[QStringLiteral("plans")].asArray().empty();
    for (const Value &p : nat[QStringLiteral("plans")].asArray()) useNative = useNative && p.isObject() && p[QStringLiteral("id")].isString();
    if (useNative) {
        out.set(QStringLiteral("plans"), nat[QStringLiteral("plans")]);
        json::Array photos;
        for (const Value &ph : nat[QStringLiteral("photos")].asArray()) {
            if (!ph.isObject()) continue;
            photos.push_back(ph);
            const QString f = str(ph, "file");
            if (pkg.contains(f) && srcOf) srcOf->insert(f, f);
        }
        out.set(QStringLiteral("photos"), Value(std::move(photos)));
        out.set(QStringLiteral("journal"), nat[QStringLiteral("journal")].isArray() ? nat[QStringLiteral("journal")] : Value::array());
        return Value(std::move(out));
    }
    // Electron 导出的：按"从 Electron 导入"的规则转换（照片放进照片库，按拍摄月份另起路径）
    json::Object e;
    e.set(QStringLiteral("plans"), m[QStringLiteral("plans")].isArray() ? m[QStringLiteral("plans")] : Value(json::Array{ m[QStringLiteral("plan")] }));
    e.set(QStringLiteral("journal"), m[QStringLiteral("journal")].isArray() ? m[QStringLiteral("journal")] : Value::array());
    if (m[QStringLiteral("photoCredits")].isObject()) e.set(QStringLiteral("photoCredits"), m[QStringLiteral("photoCredits")]);
    const data::ElectronImport::Converted conv =
        data::ElectronImport::convert(Value(std::move(e)), now, QString(), [&pkg](const QString &rel) { return pkg.contains(rel); });
    if (srcOf)
        for (const data::ImportCopy &c : conv.copies) srcOf->insert(c.dst, c.src);
    out.set(QStringLiteral("plans"), conv.doc[QStringLiteral("plans")]);
    out.set(QStringLiteral("photos"), conv.doc[QStringLiteral("photos")]);
    out.set(QStringLiteral("journal"), conv.doc[QStringLiteral("journal")]);
    return Value(std::move(out));
}

// ---------- 分享版 ----------
Shared share(const Value &doc, const QStringList &planIds, const ShareOptions &o, const QString &cur, const QDateTime &now)
{
    Shared s;
    s.doc = doc;
    json::Object &root = s.doc.mutObject();
    data::nativefmt::normalize(s.doc);
    const auto arr = [&root](const char *k) -> json::Array & { return root.find(QString::fromLatin1(k))->mutArray(); };
    if (!planIds.isEmpty()) {
        // 只要部分计划：去掉别的计划、它们的游记和照片，未归类照片也不带
        const QSet<QString> keepIds(planIds.cbegin(), planIds.cend());
        json::Array &plans = arr("plans");
        plans.erase(std::remove_if(plans.begin(), plans.end(), [&](const Value &p) { return !keepIds.contains(str(p, "id")); }), plans.end());
        json::Array &j = arr("journal");
        j.erase(std::remove_if(j.begin(), j.end(), [&](const Value &x) { return !keepIds.contains(str(x, "plan")); }), j.end());
        json::Array &ph = arr("photos");
        ph.erase(std::remove_if(ph.begin(), ph.end(), [&](const Value &x) { return !keepIds.contains(str(x, "plan")); }), ph.end());
        if (root.contains(QStringLiteral("nextTripId")) && !keepIds.contains(root.find(QStringLiteral("nextTripId"))->toString()))
            root.remove(QStringLiteral("nextTripId"));
    }
    QSet<QString> covers;
    for (Value &p : arr("plans")) {
        fillCurrency(p, cur);
        if (!str(p, "cover").isEmpty()) covers.insert(str(p, "cover"));
    }
    static const QSet<QString> shareable = { QStringLiteral("wikimedia"), QStringLiteral("nasa"), QStringLiteral("own") };
    const bool coverOnly = !o.allPhotos;
    const auto ok = [&](const Value &r) {
        const QString id = str(r, "id");
        if (coverOnly && !covers.contains(id)) return false;
        const Value &c = r[QStringLiteral("credit")];
        if (c.isObject() && !c.asObject().isEmpty()) return shareable.contains(str(c, "src"));
        if (covers.contains(id) && o.unknownCovers) return true;
        const bool own = !str(r, "stop").isEmpty() || str(r, "plan").isEmpty();   // 站点照片、未归类
        return !coverOnly && own && o.unknownStops;
    };
    QSet<QString> keep;
    {
        json::Array &ph = arr("photos");
        const int before = int(ph.size());
        ph.erase(std::remove_if(ph.begin(), ph.end(), [&](const Value &r) { return !ok(r); }), ph.end());
        s.skipped = before - int(ph.size());
        for (const Value &r : ph) keep.insert(str(r, "id"));
    }
    // 私人信息：不管怎么选都去掉
    root.remove(QStringLiteral("emergency"));
    root.remove(QStringLiteral("documents"));
    static const QRegularExpression lineRe(QStringLiteral("^\\s*!\\[[^\\]]*\\]\\(photo:([^)\\s]+)\\)\\s*$"));
    const auto stripImgs = [&keep](const QString &body) {
        QStringList out;
        for (const QString &l : body.split(u'\n')) {
            const auto mm = lineRe.match(l);
            if (!mm.hasMatch() || keep.contains(mm.captured(1))) out << l;
        }
        return out.join(u'\n');
    };
    for (Value &p : arr("plans")) {
        json::Object &po = p.mutObject();
        po.remove(QStringLiteral("localEmergency"));
        if (!str(p, "cover").isEmpty() && !keep.contains(str(p, "cover"))) po.remove(QStringLiteral("cover"));
        if (const Value *g = po.find(QStringLiteral("guide")); g && g->isString()) po.set(QStringLiteral("guide"), Value(stripImgs(g->toString())));
        if (o.noMoney) { po.remove(QStringLiteral("expenses")); po.remove(QStringLiteral("budget")); }
        if (o.noBook)
            if (Value *bk = po.find(QStringLiteral("bookings")); bk && bk->isArray())
                for (Value &b : bk->mutArray())
                    if (b.isObject()) { b.mutObject().remove(QStringLiteral("ref")); b.mutObject().remove(QStringLiteral("cost")); b.mutObject().remove(QStringLiteral("notes")); }
        if (o.noTodo) { po.remove(QStringLiteral("todos")); po.remove(QStringLiteral("packing")); }
    }
    if (o.noJournal) arr("journal").clear();
    else
        for (Value &j : arr("journal"))
            if (j.isObject() && j[QStringLiteral("body")].isString()) j.mutObject().set(QStringLiteral("body"), Value(stripImgs(str(j, "body"))));
    if (o.noVisited) root.remove(QStringLiteral("visitedRegions"));
    if (o.noTodo) root.remove(QStringLiteral("packingTemplate"));

    QSet<QString> seen;
    QStringList lines{ tp::tr("旅行计划 · 分享版 · 照片署名"), tp::tr("导出时间：%1").arg(QLocale().toString(now, QLocale::ShortFormat)), QString(),
                       tp::tr("下面这些照片来自网上，按各自的授权使用，转发时请保留署名。"), QString() };
    const QHash<QString, QString> srcName = { { "wikimedia", "Wikimedia Commons" }, { "nasa", "NASA" }, { "own", tp::tr("自己拍的") },
                                              { "google", tp::tr("Google 地图") }, { "other", tp::tr("其他") } };
    for (const Value &r : arr("photos")) {
        const QString f = str(r, "file");
        if (!safeRel(f) || seen.contains(f)) continue;
        seen.insert(f);
        s.photos << f;
        const Value &c = r[QStringLiteral("credit")];
        if (!c.isObject() || c.asObject().isEmpty()) continue;
        lines << f << QStringLiteral("  ") + str(c, "title")
              << QStringLiteral("  ") + tp::tr("作者：%1 · 授权：%2 · 来源：%3")
                                            .arg(str(c, "author").isEmpty() ? tp::tr("未署名") : str(c, "author"),
                                                 str(c, "license").isEmpty() ? tp::tr("未知") : str(c, "license"),
                                                 srcName.value(str(c, "src"), str(c, "src")))
              << (str(c, "url").isEmpty() ? QString() : QStringLiteral("  ") + str(c, "url")) << QString();
    }
    lines << QString() << tp::tr("地图和数据来源：") << QStringLiteral("  ") + tp::tr("省界、国界：Natural Earth（公有领域）")
          << QStringLiteral("  ") + tp::tr("街道图 / 卫星图（地图和地球）：Esri 在线瓦片 · Esri 及其数据提供方")
          << QStringLiteral("  ") + tp::tr("天气、往年气候、海拔和时区：Open-Meteo（CC BY 4.0；海拔 Copernicus DEM GLO-90，往年气候 ERA5 · Copernicus 气候变化服务）")
          << QStringLiteral("  ") + tp::tr("地名搜索、道路路线：© OpenStreetMap 贡献者（ODbL），Nominatim、OSRM（FOSSGIS）")
          << QStringLiteral("  ") + tp::tr("日出日落：suncalc 2.0.2（BSD-2-Clause，Copyright (c) 2026, Volodymyr Agafonkin）");
    s.credits = lines.join(QStringLiteral("\r\n"));
    return s;
}

ShareWrite writeShare(const QString &destRoot, const QString &name, const Shared &s, const QString &dataDir, const QString &appVersion,
                      const QDateTime &now)
{
    ShareWrite w;
    if (destRoot.isEmpty()) { w.error = tp::tr("没有选导出到哪里"); return w; }
    const QString root = QDir::cleanPath(QFileInfo(destRoot).absoluteFilePath());
    const QString data = QDir::cleanPath(QFileInfo(dataDir).absoluteFilePath());
    if (root.compare(data, Qt::CaseInsensitive) == 0 || root.startsWith(data + u'/', Qt::CaseInsensitive)) {
        w.error = tp::tr("不能导出到数据文件夹里面");
        return w;
    }
    const QString tag = name.isEmpty() ? QString() : safeFileName(name, 60) + u'-';
    QString dir = QDir(root).filePath(tp::tr("旅行计划-分享版") + u'-' + tag + now.toString(QStringLiteral("yyyyMMdd-HHmm")));
    const QString base = dir;
    for (int k = 2; QFileInfo::exists(dir); ++k) dir = base + QStringLiteral("-%1").arg(k);
    if (!QDir().mkpath(dir)) { w.error = tp::tr("建不了文件夹：%1").arg(QDir::toNativeSeparators(dir)); return w; }
    Value doc = s.doc;
    data::nativefmt::stamp(doc, appVersion, now);
    QFile f(QDir(dir).filePath(data::nativefmt::dataFileName()));
    const QByteArray bytes = data::nativefmt::serialize(doc);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) { w.error = tp::tr("写不进去：%1").arg(f.errorString()); return w; }
    f.close();
    const QDir src(dataDir), dst(dir);
    for (const QString &rel : s.photos) {
        const QString to = dst.filePath(rel);
        if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !QFile::copy(src.filePath(rel), to)) w.missing << rel;
        else ++w.photos;
    }
    QFile c(dst.filePath(tp::tr("照片署名.txt")));
    if (!c.open(QIODevice::WriteOnly) || c.write(QByteArray("\xEF\xBB\xBF") + s.credits.toUtf8()) < 0) { w.error = tp::tr("写不进去：%1").arg(c.errorString()); return w; }
    w.ok = true;
    w.dir = QDir::toNativeSeparators(dir);
    return w;
}

} // namespace tp::exporter::trip
