#include "planmerge.h"

#include "nativeformat.h"
#include "trdata.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace tp::data::planmerge {

using json::Value;

namespace {

const QRegularExpression &refRe()
{
    static const QRegularExpression re(QStringLiteral("\\]\\(photo:([^)\\s]+)\\)"));
    return re;
}

bool isLocal(const QString &rel)
{
    return rel.startsWith(QLatin1String("photos/")) && !rel.split(u'/').contains(QStringLiteral("..")) && !rel.contains(u'\\')
           && !rel.contains(u':');
}

json::Array &arr(json::Object &root, const char *key)
{
    Value *v = root.find(QString::fromLatin1(key));
    if (!v || !v->isArray()) { root.set(QString::fromLatin1(key), Value::array()); v = root.find(QString::fromLatin1(key)); }
    return v->mutArray();
}

QString str(const Value &o, const char *key) { return o[QString::fromLatin1(key)].toString(); }

void remapPlan(Value &plan, const QHash<QString, QString> &map)
{
    if (map.isEmpty()) return;
    json::Object &o = plan.mutObject();
    const QString cover = o.find(QStringLiteral("cover")) ? o.find(QStringLiteral("cover"))->toString() : QString();
    if (map.contains(cover)) o.set(QStringLiteral("cover"), Value(map.value(cover)));
    if (const Value *g = o.find(QStringLiteral("guide")); g && g->isString()) o.set(QStringLiteral("guide"), Value(remapPhotoRefs(g->toString(), map)));
}

} // namespace

Mode modeFromKey(const QString &k)
{
    if (k == QLatin1String("replace")) return Mode::Replace;
    if (k == QLatin1String("copy")) return Mode::Copy;
    if (k == QLatin1String("merge")) return Mode::Merge;
    if (k == QLatin1String("skip")) return Mode::Skip;
    return Mode::New;
}

QString remapPhotoRefs(const QString &text, const QHash<QString, QString> &map)
{
    if (map.isEmpty() || !text.contains(QLatin1String("](photo:"))) return text;
    QString out;
    int last = 0;
    for (auto it = refRe().globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const auto f = map.constFind(m.captured(1));
        if (f == map.constEnd()) continue;
        out += text.mid(last, m.capturedStart(1) - last) + *f;
        last = m.capturedEnd(1);
    }
    return last == 0 ? text : out + text.mid(last);
}

QVariantMap summary(const Value &doc, const QString &planId)
{
    QVariantMap m;
    for (const Value &p : doc[QStringLiteral("plans")].asArray()) {
        if (str(p, "id") != planId) continue;
        int photos = 0;
        for (const Value &ph : doc[QStringLiteral("photos")].asArray()) photos += str(ph, "plan") == planId;
        m = { { QStringLiteral("id"), planId }, { QStringLiteral("name"), str(p, "name") },
              { QStringLiteral("updatedAt"), p[QStringLiteral("updatedAt")].toNumber() },
              { QStringLiteral("stops"), int(p[QStringLiteral("stops")].asArray().size()) },
              { QStringLiteral("expenses"), int(p[QStringLiteral("expenses")].asArray().size()) },
              { QStringLiteral("photos"), photos } };
        break;
    }
    return m;
}

QStringList photoRefs(const QString &text)
{
    QStringList out;
    for (auto it = refRe().globalMatch(text); it.hasNext();) out << it.next().captured(1);
    return out;
}

Result apply(const Value &existing, const Value &incoming, const QHash<QString, Mode> &modes, const Options &opt)
{
    Result r;
    r.doc = existing.isObject() ? existing : Value::object();
    nativefmt::normalize(r.doc);
    json::Object &root = r.doc.mutObject();

    QSet<QString> photoIds, journalIds, planIds;
    const auto reindex = [&] {
        photoIds.clear(); journalIds.clear(); planIds.clear();
        for (const Value &p : arr(root, "photos")) photoIds.insert(str(p, "id"));
        for (const Value &j : arr(root, "journal")) journalIds.insert(str(j, "id"));
        for (const Value &p : arr(root, "plans")) planIds.insert(str(p, "id"));
    };
    reindex();
    const auto fresh = [&opt](const QString &prefix, const QSet<QString> &taken) {
        QString n;
        do n = opt.uid(prefix); while (n.isEmpty() || taken.contains(n));
        return n;
    };

    const json::Array &inPhotos = incoming[QStringLiteral("photos")].asArray();
    const json::Array &inJournal = incoming[QStringLiteral("journal")].asArray();

    for (const Value &P0 : incoming[QStringLiteral("plans")].asArray()) {
        if (!P0.isObject()) continue;
        const QString id = str(P0, "id");
        if (id.isEmpty()) continue;
        int li = -1;
        {
            const json::Array &plans = arr(root, "plans");
            for (size_t k = 0; k < plans.size(); ++k)
                if (str(plans[k], "id") == id) { li = int(k); break; }
        }
        Mode m = modes.value(id, li < 0 ? Mode::New : Mode::Skip);
        if (m == Mode::Skip) continue;
        if (li < 0 && m != Mode::Copy) m = Mode::New;

        QList<Value> phs, js;
        for (const Value &ph : inPhotos) if (ph.isObject() && str(ph, "plan") == id) phs << ph;
        for (const Value &j : inJournal) if (j.isObject() && str(j, "plan") == id) js << j;

        QHash<QString, QString> idMap;   // 导入的照片 id → 结果里的 id
        QSet<QString> added;             // 已经处理过的导入照片 id
        const auto addPhoto = [&](Value ph, const QString &planId, const QString &stop, bool forceNew) {
            const QString old = str(ph, "id");
            QString nid = old;
            if (forceNew || nid.isEmpty() || photoIds.contains(nid)) nid = fresh(QStringLiteral("ph"), photoIds);
            photoIds.insert(nid);
            added.insert(old);
            if (nid != old) idMap.insert(old, nid);
            json::Object &o = ph.mutObject();
            o.set(QStringLiteral("id"), Value(nid));
            o.set(QStringLiteral("plan"), Value(planId));
            o.set(QStringLiteral("stop"), Value(stop));
            const QString file = str(ph, "file");
            arr(root, "photos").push_back(ph);
            ++r.photos;
            if (isLocal(file)) r.files << FileJob{ nid, file, forceNew };
        };

        if (m != Mode::Merge) {
            Value P = P0;
            QString pid = id;
            if (m == Mode::Copy) {
                pid = fresh(QStringLiteral("p"), planIds);
                json::Object &o = P.mutObject();
                o.set(QStringLiteral("id"), Value(pid));
                o.set(QStringLiteral("name"), Value(str(P, "name") + opt.copySuffix));
                o.remove(QStringLiteral("pinnedAt"));
            }
            if (m == Mode::Replace) {
                // 本地这个计划的照片记录、游记换成导入的（照片文件留着，清理照片时再处理）
                json::Array &ap = arr(root, "photos");
                ap.erase(std::remove_if(ap.begin(), ap.end(), [&id](const Value &p) { return str(p, "plan") == id; }), ap.end());
                json::Array &aj = arr(root, "journal");
                aj.erase(std::remove_if(aj.begin(), aj.end(), [&id](const Value &j) { return str(j, "plan") == id; }), aj.end());
                reindex();
            }
            for (const Value &ph : phs) addPhoto(ph, pid, str(ph, "stop"), m == Mode::Copy);
            remapPlan(P, idMap);
            if (m == Mode::Replace) arr(root, "plans")[size_t(li)] = P;
            else arr(root, "plans").push_back(P);
            planIds.insert(pid);
            for (const Value &j0 : js) {
                Value j = j0;
                QString jid = str(j, "id");
                if (m == Mode::Copy || jid.isEmpty() || journalIds.contains(jid)) jid = fresh(QStringLiteral("j"), journalIds);
                journalIds.insert(jid);
                json::Object &o = j.mutObject();
                o.set(QStringLiteral("id"), Value(jid));
                o.set(QStringLiteral("plan"), Value(pid));
                if (const Value *b = o.find(QStringLiteral("body")); b && b->isString()) o.set(QStringLiteral("body"), Value(remapPhotoRefs(b->toString(), idMap)));
                arr(root, "journal").push_back(j);
                ++r.journal;
            }
            r.planIds.insert(id, pid);
            ++r.plans;
            r.messages << (m == Mode::Replace ? trd("已用导入的计划覆盖") : m == Mode::Copy ? trd("已另存一份") : trd("已导入新计划"));
            continue;
        }

        // ---- 只合并消费和照片（Electron applyImport merge）----
        Value &L = arr(root, "plans")[size_t(li)];
        json::Object &lo = L.mutObject();
        // 同行人：同 id 或同名算同一个人
        QHash<QString, QString> pmap;
        {
            Value *pv = lo.find(QStringLiteral("people"));
            if (!pv || !pv->isArray()) { lo.set(QStringLiteral("people"), Value::array()); pv = lo.find(QStringLiteral("people")); }
            json::Array &people = pv->mutArray();
            for (const Value &pp : P0[QStringLiteral("people")].asArray()) {
                QString same;
                for (const Value &q : people) if (str(q, "id") == str(pp, "id")) { same = str(q, "id"); break; }
                if (same.isEmpty())
                    for (const Value &q : people) if (str(q, "name") == str(pp, "name")) { same = str(q, "id"); break; }
                if (!same.isEmpty()) pmap.insert(str(pp, "id"), same);
                else { people.push_back(pp); pmap.insert(str(pp, "id"), str(pp, "id")); }
            }
        }
        const auto remap = [&pmap](const Value &v) { return pmap.contains(v.toString()) ? Value(pmap.value(v.toString())) : v; };
        int n0 = 0;
        {
            Value *ev = lo.find(QStringLiteral("expenses"));
            if (!ev || !ev->isArray()) { lo.set(QStringLiteral("expenses"), Value::array()); ev = lo.find(QStringLiteral("expenses")); }
            json::Array &ex = ev->mutArray();
            n0 = int(ex.size());
            QSet<QString> have;
            for (const Value &x : ex) have.insert(str(x, "id"));
            const QString cur = str(P0, "currency").isEmpty() ? opt.defaultCurrency : str(P0, "currency");
            for (const Value &x : P0[QStringLiteral("expenses")].asArray()) {
                if (!x.isObject() || have.contains(str(x, "id"))) continue;
                Value y = x;
                json::Object &yo = y.mutObject();
                if (const Value *pb = yo.find(QStringLiteral("paidBy"))) yo.set(QStringLiteral("paidBy"), remap(*pb));
                if (Value *sp = yo.find(QStringLiteral("split")); sp && sp->isArray())
                    for (Value &s : sp->mutArray())
                        if (s.isObject())
                            if (const Value *w = s.asObject().find(QStringLiteral("who"))) s.mutObject().set(QStringLiteral("who"), remap(*w));
                if (str(y, "currency").isEmpty() && !cur.isEmpty()) yo.set(QStringLiteral("currency"), Value(cur));
                have.insert(str(y, "id"));
                ex.push_back(y);
            }
            r.expenses += int(ex.size()) - n0;
        }
        if (str(L, "currency").isEmpty() && !str(P0, "currency").isEmpty()) lo.set(QStringLiteral("currency"), P0[QStringLiteral("currency")]);

        // 照片：本地这个计划已经有的（同 id 或同一文件）不重复，对上本地那条
        QHash<QString, QString> localByFile;
        QSet<QString> localIds;
        for (const Value &p : arr(root, "photos"))
            if (str(p, "plan") == id) { localByFile.insert(str(p, "file"), str(p, "id")); localIds.insert(str(p, "id")); }
        QSet<QString> localStops;
        for (const Value &s : L[QStringLiteral("stops")].asArray()) localStops.insert(str(s, "id"));
        const auto takeIn = [&](const Value &ph, bool keepPlanLevel) {
            const QString old = str(ph, "id");
            if (added.contains(old)) return;
            if (localIds.contains(old)) { added.insert(old); return; }
            if (const auto f = localByFile.constFind(str(ph, "file")); f != localByFile.constEnd()) {
                added.insert(old);
                if (*f != old) idMap.insert(old, *f);
                return;
            }
            const QString stop = str(ph, "stop");
            if (localStops.contains(stop)) addPhoto(ph, id, stop, false);
            else if (keepPlanLevel) addPhoto(ph, id, QString(), false);
        };
        for (const Value &ph : phs) if (!str(ph, "stop").isEmpty()) takeIn(ph, false);
        if (str(L, "cover").isEmpty() && !str(P0, "cover").isEmpty()) {
            for (const Value &ph : phs) if (str(ph, "id") == str(P0, "cover")) takeIn(ph, true);
            const QString c = idMap.value(str(P0, "cover"), str(P0, "cover"));
            if (photoIds.contains(c)) lo.set(QStringLiteral("cover"), Value(c));
        }
        for (const Value &j0 : js) {
            if (journalIds.contains(str(j0, "id"))) continue;
            for (const QString &ref : photoRefs(str(j0, "body")))
                for (const Value &ph : phs) if (str(ph, "id") == ref) takeIn(ph, true);
            Value j = j0;
            json::Object &o = j.mutObject();
            o.set(QStringLiteral("plan"), Value(id));
            if (const Value *b = o.find(QStringLiteral("body")); b && b->isString()) o.set(QStringLiteral("body"), Value(remapPhotoRefs(b->toString(), idMap)));
            journalIds.insert(str(j, "id"));
            arr(root, "journal").push_back(j);
            ++r.journal;
        }
        r.planIds.insert(id, id);
        ++r.plans;
        r.messages << trd("已合并：新加 %1 笔消费").arg(int(L[QStringLiteral("expenses")].asArray().size()) - n0);
    }
    return r;
}

WriteResult writeFiles(Value &doc, const QString &dataDir, const QList<FileJob> &jobs,
                       const std::function<bool(const QString &, QByteArray *)> &read,
                       const std::function<bool(const QString &, QByteArray *)> &readOriginal)
{
    WriteResult w;
    const QDir dst(dataDir);
    json::Array &photos = arr(doc.mutObject(), "photos");
    QHash<QString, int> byId;
    for (size_t k = 0; k < photos.size(); ++k) byId.insert(str(photos[k], "id"), int(k));
    const auto suffixed = [](const QString &rel, int n) {
        const int dot = rel.lastIndexOf(u'.');
        return dot > rel.lastIndexOf(u'/') ? QStringLiteral("%1-%2%3").arg(rel.left(dot)).arg(n).arg(rel.mid(dot)) : QStringLiteral("%1-%2").arg(rel).arg(n);
    };
    const auto put = [&](const QString &rel, const QByteArray &bytes) -> bool {
        const QString full = dst.filePath(rel);
        if (!QDir().mkpath(QFileInfo(full).absolutePath())) { w.error = trd("建不了文件夹：%1").arg(QDir::toNativeSeparators(QFileInfo(full).absolutePath())); return false; }
        const QString tmp = full + QStringLiteral(".tmp-%1").arg(QDateTime::currentMSecsSinceEpoch());
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.flush()) {
            w.error = trd("写不进数据文件夹：%1").arg(f.errorString());
            f.close(); QFile::remove(tmp);
            return false;
        }
        f.close();
        if (QFileInfo(tmp).size() != bytes.size() || !QFile::rename(tmp, full)) {
            QFile::remove(tmp);
            w.error = trd("写不进数据文件夹：%1").arg(QDir::toNativeSeparators(full));
            return false;
        }
        w.copied << full;
        return true;
    };
    for (const FileJob &j : jobs) {
        const auto it = byId.constFind(j.photoId);
        if (it == byId.constEnd()) continue;
        Value &rec = photos[size_t(*it)];
        const QString rel0 = str(rec, "file");
        if (!isLocal(rel0)) continue;
        QByteArray bytes;
        if (!read || !read(j.from, &bytes)) { ++w.missing; continue; }
        QString rel = rel0;
        bool reuse = false;
        for (int n = 2;; ++n) {
            QFile old(dst.filePath(rel));
            if (!old.exists()) break;
            if (!j.fresh && old.size() == bytes.size() && old.open(QIODevice::ReadOnly) && old.readAll() == bytes) { reuse = true; break; }
            rel = suffixed(rel0, n);
        }
        if (reuse) ++w.same;
        else if (put(rel, bytes)) ++w.written;
        else { for (const QString &f : w.copied) QFile::remove(f); w.copied.clear(); return w; }
        if (rel != rel0) rec.mutObject().set(QStringLiteral("file"), Value(rel));
        QByteArray orig;
        const QString origRel = QStringLiteral("photos/.originals/") + rel.mid(7);
        if (!reuse && readOriginal && readOriginal(j.from, &orig) && !QFileInfo::exists(dst.filePath(origRel)) && !put(origRel, orig)) {
            for (const QString &f : w.copied) QFile::remove(f);
            w.copied.clear();
            return w;
        }
    }
    w.ok = true;
    return w;
}

} // namespace tp::data::planmerge
