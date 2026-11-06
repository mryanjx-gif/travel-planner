#include "plans.h"

#include <QCollator>
#include <QDate>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace tp::map::plans {

using json::Value;

namespace {

const QString kId = QStringLiteral("id");

QString str(const Value &o, const char *key) { return o[QString::fromLatin1(key)].toString(); }

// JS 的真值（a.startDate || …）：只认非空字符串
QString truthyStr(const Value &o, const char *key) { return str(o, key); }

// JS 的 Number.isFinite(+v)（tripdata.cpp jsNum 的同款）
bool jsNum(const Value &o, const char *key, double *out)
{
    const Value *v = o.asObject().find(QString::fromLatin1(key));
    if (!v || v->isArray() || v->isObject()) return false;
    *out = v->jsToNumber();
    return std::isfinite(*out);
}

// 计划的有效站点（validStops）：坐标有效，按 order 排（原生版没有 order = 数组顺序）。返回 stops 数组里的下标
QVector<int> validStopIndexes(const Value &plan)
{
    struct S { int i; double order; };
    QVector<S> v;
    const json::Array &arr = plan[QStringLiteral("stops")].asArray();
    for (int i = 0; i < int(arr.size()); ++i) {
        double lat, lng;
        if (!jsNum(arr[i], "lat", &lat) || !jsNum(arr[i], "lng", &lng)) continue;
        double ord = 0;
        const bool hasOrder = arr[i].asObject().contains(QStringLiteral("order"));
        v.push_back({ i, hasOrder ? (jsNum(arr[i], "order", &ord) ? ord : 0) : double(i + 1) });
    }
    std::stable_sort(v.begin(), v.end(), [](const S &a, const S &b) { return a.order < b.order; });
    QVector<int> out;
    for (const S &s : v) out << s.i;
    return out;
}

// pinnedAt || 0
double pinOf(const Value &p)
{
    const Value &v = p[QStringLiteral("pinnedAt")];
    if (v.isNull() || (v.isBool() && !v.toBool()) || (v.isString() && v.toString().isEmpty())) return 0;
    const double d = v.jsToNumber();
    return std::isfinite(d) ? d : 0;
}

// 名字的第一个字属于哪一组（0.8.7 验证后加）：Electron 用 ICU 的 zh 排序，顺序是 空 → 标点和符号 → 数字 → 汉字（按拼音）→ 其他文字（拉丁、西里尔…）；
// Windows 上 QCollator 走系统排序规则，会把拉丁字母排在汉字前面（PO 0.8.7 验证）。先比组，组内再交给 QCollator，两个平台都和 Electron 一样
int nameGroup(const QString &s)
{
    if (s.isEmpty()) return -1;
    const char32_t c = s.at(0).isHighSurrogate() && s.size() > 1 ? QChar::surrogateToUcs4(s.at(0), s.at(1)) : char32_t(s.at(0).unicode());
    if (QChar::script(c) == QChar::Script_Han) return 2;
    if (QChar::isLetter(c)) return 3;
    if (QChar::isDigit(c)) return 1;
    return 0;
}

QCollator &zhCollator()
{
    static QCollator c = [] {
        QCollator k(QLocale(QLocale::Chinese, QLocale::China));
        return k;
    }();
    return c;
}

QString statusColor(const QString &status)
{
    if (status == QLatin1String("completed")) return QStringLiteral("#2b8a3e");
    if (status == QLatin1String("wish")) return QStringLiteral("#7b61b5");
    return QStringLiteral("#e8590c");
}

QString planColor(const Value &p)
{
    const QString c = str(p, "color");
    return c.isEmpty() ? statusColor(str(p, "status")) : c;
}

const Value *findPlan(const Value &root, const QString &id, int *index = nullptr)
{
    if (id.isEmpty()) return nullptr;
    const json::Array &arr = root[QStringLiteral("plans")].asArray();
    for (int i = 0; i < int(arr.size()); ++i)
        if (arr[i][kId].toString() == id) {
            if (index) *index = i;
            return &arr[i];
        }
    return nullptr;
}

QVariantMap legOf(const Value &stop)
{
    const Value &leg = stop[QStringLiteral("leg")];
    QString mode;
    double dist = 0, time = 0;
    if (leg.isObject()) {
        mode = str(leg, "mode");
        jsNum(leg, "dist", &dist);
        jsNum(leg, "time", &time);
    } else {
        mode = str(stop, "legMode");
        jsNum(stop, "legDist", &dist);
        jsNum(stop, "legTime", &time);
    }
    static const QStringList modes = { "flight", "train", "ferry", "drive", "walk" };
    QVariantMap m;
    m.insert(QStringLiteral("set"), !mode.isEmpty());
    m.insert(QStringLiteral("mode"), modes.contains(mode) ? mode : QStringLiteral("flight"));
    m.insert(QStringLiteral("dist"), dist);
    m.insert(QStringLiteral("time"), time);
    return m;
}

QVariantList scheduleOf(const Value &stop)
{
    QVariantList out;
    const Value &sch = stop[QStringLiteral("schedule")];
    if (sch.isString()) {
        if (!sch.toString().isEmpty()) out << QVariantMap{ { QStringLiteral("text"), sch.toString() } };
        return out;
    }
    for (const Value &it : sch.asArray())
        out << QVariantMap{ { QStringLiteral("date"), str(it, "date") }, { QStringLiteral("time"), str(it, "time") },
                            { QStringLiteral("end"), str(it, "end") }, { QStringLiteral("text"), str(it, "text") } };
    return out;
}

} // namespace

// ---------------- 基本属性 ----------------

const QStringList &allStatuses()
{
    static const QStringList l = { QStringLiteral("planning"), QStringLiteral("wish"), QStringLiteral("completed") };
    return l;
}
const QStringList &allTypes()
{
    static const QStringList l = { QStringLiteral("single"), QStringLiteral("route") };
    return l;
}
QString regionNone() { return QStringLiteral("未分类"); }
const QStringList &allRegions()
{
    static const QStringList l = { QStringLiteral("中国"), QStringLiteral("东亚"), QStringLiteral("东南亚"), QStringLiteral("西亚/中亚"),
                                   QStringLiteral("欧洲"), QStringLiteral("非洲"), QStringLiteral("大洋洲"), QStringLiteral("北美洲"),
                                   QStringLiteral("南美洲"), QStringLiteral("未分类") };
    return l;
}

QString statusOf(const Value &plan)
{
    const QString s = str(plan, "status");
    return allStatuses().contains(s) ? s : QStringLiteral("planning");
}

QString typeOf(const Value &plan) { return str(plan, "type") == QLatin1String("route") ? QStringLiteral("route") : QStringLiteral("single"); }

QStringList regionsOf(const Value &plan)
{
    const Value &rs = plan[QStringLiteral("regions")];
    if (!rs.isArray()) return {};
    QSet<QString> set;
    for (const Value &r : rs.asArray()) {
        if (!r.isString()) continue;
        const QString n = r.toString();
        set.insert(n == QStringLiteral("西亚") ? QStringLiteral("西亚/中亚") : n);   // REGION_ALIAS
    }
    QStringList out;
    for (int i = 0; i < allRegions().size() - 1; ++i)   // REGIONS（不含"未分类"）
        if (set.contains(allRegions()[i])) out << allRegions()[i];
    return out;
}

// ---------------- 排序 ----------------

int groupRank(const Value &p)
{
    const QString st = str(p, "status");
    return (st == QLatin1String("completed") ? 4 : st == QLatin1String("wish") ? 2 : 0) + (str(p, "type") == QLatin1String("route") ? 0 : 1);
}

int compare(const Value &a, const Value &b, bool withPin)
{
    if (withPin) {
        const double pa = pinOf(a), pb = pinOf(b);
        if (pa != 0 || pb != 0) return pb > pa ? 1 : pb < pa ? -1 : 0;
    }
    const int g = groupRank(a) - groupRank(b);
    if (g) return g;
    QString da = truthyStr(a, "startDate"), db = truthyStr(b, "startDate");
    if (da.isEmpty()) da = truthyStr(a, "endDate");
    if (db.isEmpty()) db = truthyStr(b, "endDate");
    if (da.isEmpty() && db.isEmpty()) return compareNames(str(a, "name"), str(b, "name"));
    if (da.isEmpty()) return -1;
    if (db.isEmpty()) return 1;
    return db.compare(da);
}

int compareNames(const QString &a, const QString &b)
{
    const int g = nameGroup(a) - nameGroup(b);
    return g ? g : zhCollator().compare(a, b);
}

QVector<int> sortedIndexes(const Value &root, bool withPin)
{
    const json::Array &arr = root[QStringLiteral("plans")].asArray();
    QVector<int> idx(int(arr.size()));
    for (int i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](int x, int y) { return compare(arr[x], arr[y], withPin) < 0; });
    return idx;
}

// ---------------- 筛选 ----------------

Filter Filter::all()
{
    Filter f;
    f.status = QSet<QString>(allStatuses().begin(), allStatuses().end());
    f.type = QSet<QString>(allTypes().begin(), allTypes().end());
    f.region = QSet<QString>(allRegions().begin(), allRegions().end());
    return f;
}

bool Filter::matches(const Value &p) const
{
    if (!status.contains(statusOf(p)) || !type.contains(typeOf(p))) return false;
    const QStringList rs = regionsOf(p);
    if (rs.isEmpty()) return region.contains(regionNone());
    for (const QString &r : rs)
        if (region.contains(r)) return true;
    return false;
}

int Filter::excludedCount() const
{
    return int(allStatuses().size() - status.size()) + int(allTypes().size() - type.size()) + int(allRegions().size() - region.size());
}

QStringList Filter::reveal(const Value &p, bool *unhid)
{
    QStringList opened;
    const QString st = statusOf(p);
    if (!status.contains(st)) { status.insert(st); opened << st; }
    const QString ty = typeOf(p);
    if (!type.contains(ty)) { type.insert(ty); opened << ty; }
    const QStringList rs = regionsOf(p);
    bool any = false;
    for (const QString &r : rs) any = any || region.contains(r);
    if (rs.isEmpty() ? !region.contains(regionNone()) : !any) {
        const QString r = rs.isEmpty() ? regionNone() : rs.first();
        region.insert(r);
        opened << r;
    }
    const QString id = p[kId].toString();
    *unhid = hidden.remove(id);
    return opened;
}

QVariantMap Filter::toVariant() const
{
    auto ordered = [](const QSet<QString> &s, const QStringList &order) {
        QStringList out;
        for (const QString &k : order)
            if (s.contains(k)) out << k;
        return out;
    };
    QStringList h(hidden.begin(), hidden.end());
    h.sort();
    return { { QStringLiteral("status"), ordered(status, allStatuses()) },
             { QStringLiteral("type"), ordered(type, allTypes()) },
             { QStringLiteral("region"), ordered(region, allRegions()) },
             { QStringLiteral("hidden"), h } };
}

Filter Filter::fromVariant(const QVariantMap &m, const QSet<QString> &planIds)
{
    Filter f = all();
    auto pick = [&m](const char *key, const QStringList &allowed, QSet<QString> *out) {
        const auto it = m.constFind(QString::fromLatin1(key));
        if (it == m.constEnd() || (it->typeId() != QMetaType::QVariantList && it->typeId() != QMetaType::QStringList)) return;
        out->clear();
        for (const QVariant &v : it->toList()) {
            QString s = v.toString();
            if (s == QStringLiteral("西亚")) s = QStringLiteral("西亚/中亚");
            if (allowed.contains(s)) out->insert(s);
        }
    };
    pick("status", allStatuses(), &f.status);
    pick("type", allTypes(), &f.type);
    pick("region", allRegions(), &f.region);
    for (const QVariant &v : m.value(QStringLiteral("hidden")).toList())
        if (planIds.contains(v.toString())) f.hidden.insert(v.toString());   // 已删除的计划 id 自动丢掉
    return f;
}

// ---------------- 日期 ----------------

bool okDate(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    return re.match(s).hasMatch() && QDate::fromString(s, QStringLiteral("yyyy-MM-dd")).isValid();
}

int days(const QString &a, const QString &b)
{
    return int(QDate::fromString(a, QStringLiteral("yyyy-MM-dd")).daysTo(QDate::fromString(b, QStringLiteral("yyyy-MM-dd"))));
}

QString addDays(const QString &s, int n) { return QDate::fromString(s, QStringLiteral("yyyy-MM-dd")).addDays(n).toString(QStringLiteral("yyyy-MM-dd")); }

bool dated(const Value &p)
{
    const QString a = str(p, "startDate"), b = str(p, "endDate");
    return okDate(a) && okDate(b) && b >= a;
}

// ---------------- 此刻 ----------------

Now pickNow(const Value &root, const QString &today)
{
    Now n;
    n.kind = QStringLiteral("none");
    const Value *p = findPlan(root, root[QStringLiteral("nextTripId")].toString(), &n.planIndex);
    if (!p) { n.planIndex = -1; return n; }
    const QString a = str(*p, "startDate"), b = str(*p, "endDate");
    if (dated(*p) && a <= today && today <= b) n.kind = QStringLiteral("live");
    else if (dated(*p) && b < today) n.kind = QStringLiteral("back");
    else n.kind = QStringLiteral("next");
    return n;
}

QVariantMap nowInfo(const Value &root, const QString &today)
{
    QVariantMap m;
    const Now h = pickNow(root, today);
    m.insert(QStringLiteral("kind"), h.kind);
    if (h.planIndex < 0) return m;
    const Value &p = root[QStringLiteral("plans")].asArray()[h.planIndex];
    const QString start = str(p, "startDate"), end = str(p, "endDate");
    m.insert(QStringLiteral("planId"), p[kId].toString());
    m.insert(QStringLiteral("planName"), str(p, "name"));
    m.insert(QStringLiteral("color"), planColor(p));
    m.insert(QStringLiteral("dated"), dated(p));
    m.insert(QStringLiteral("startDate"), start);
    m.insert(QStringLiteral("endDate"), end);
    m.insert(QStringLiteral("today"), today);

    if (h.kind == QLatin1String("live")) {
        m.insert(QStringLiteral("dayN"), days(start, today) + 1);
        m.insert(QStringLiteral("dayTotal"), days(start, end) + 1);
        // stopOn：最后一个到达日期 ≤ 今天的站点
        const QVector<int> st = validStopIndexes(p);
        const json::Array &stops = p[QStringLiteral("stops")].asArray();
        int i = -1;
        for (int k = 0; k < st.size(); ++k) {
            const QString ad = str(stops[st[k]], "arriveDate");
            if (okDate(ad) && ad <= today) i = k;
        }
        const int hereK = i >= 0 ? i : (st.isEmpty() ? -1 : 0);
        m.insert(QStringLiteral("here"), hereK >= 0 ? str(stops[st[hereK]], "name") : str(p, "name"));
        m.insert(QStringLiteral("hereIndex"), hereK);
        if (i >= 0 && i + 1 < st.size()) {
            const Value &nx = stops[st[i + 1]];
            const QString ad = str(nx, "arriveDate");
            if (okDate(ad)) {
                QVariantMap nm{ { QStringLiteral("name"), str(nx, "name") }, { QStringLiteral("arriveDate"), ad },
                                { QStringLiteral("tomorrow"), ad == addDays(today, 1) } };
                nm.insert(QStringLiteral("leg"), legOf(stops[st[i]]));
                m.insert(QStringLiteral("next"), nm);
            }
        }
        // todayItems：站点安排里 date = 今天的，加上开始于今天的预订
        QVariantList items;
        for (const Value &s : stops)
            for (const QVariant &x : scheduleOf(s)) {
                QVariantMap it = x.toMap();
                if (it.value(QStringLiteral("date")).toString() != today) continue;
                it.insert(QStringLiteral("stop"), str(s, "name"));
                items << it;
            }
        for (const Value &b : p[QStringLiteral("bookings")].asArray()) {
            const Value &bs = b[QStringLiteral("start")];
            if (!bs.isString() || bs.toString().left(10) != today) continue;
            const QString t = bs.toString().mid(11, 5);
            items << QVariantMap{ { QStringLiteral("time"), t }, { QStringLiteral("text"), str(b, "title") }, { QStringLiteral("booking"), true } };
        }
        m.insert(QStringLiteral("todayItems"), items);
    } else if (h.kind == QLatin1String("next")) {
        m.insert(QStringLiteral("daysUntil"), dated(p) ? days(today, start) : -1);
    } else if (h.kind == QLatin1String("back")) {
        m.insert(QStringLiteral("daysAgo"), days(end, today));
    }

    // todos：这趟没做完的（最多 3 条），按截止日期排（没有截止的在后）
    struct T { QString text, id, due; };
    QVector<T> td;
    int open = 0;
    for (const Value &t : p[QStringLiteral("todos")].asArray()) {
        if (t[QStringLiteral("done")].toBool() || str(t, "text").isEmpty()) continue;
        ++open;
        td.push_back({ str(t, "text"), str(t, "id"), str(t, "due") });
    }
    if (str(p, "status") == QLatin1String("completed")) td.clear();   // todos() 跳过已完成的计划（角标的待办数照样算，Electron 同）
    std::stable_sort(td.begin(), td.end(), [](const T &a, const T &b) {
        return (a.due.isEmpty() ? QStringLiteral("9999") : a.due) < (b.due.isEmpty() ? QStringLiteral("9999") : b.due);
    });
    QVariantList todos;
    for (int k = 0; k < td.size() && k < 3; ++k) {
        QVariantMap t{ { QStringLiteral("id"), td[k].id }, { QStringLiteral("text"), td[k].text }, { QStringLiteral("due"), td[k].due } };
        if (okDate(td[k].due)) {
            t.insert(QStringLiteral("late"), td[k].due < today);
            t.insert(QStringLiteral("soon"), td[k].due >= today && days(today, td[k].due) <= 7);
        }
        todos << t;
    }
    m.insert(QStringLiteral("todos"), todos);
    m.insert(QStringLiteral("todoOpen"), open);

    // notices：日期已过还是"计划中"的计划（只列第一个，其余合成一句）；60 天内到期的证件（最多两个）
    QVariantList od, dc;
    for (const Value &q : root[QStringLiteral("plans")].asArray())
        if (str(q, "status") == QLatin1String("planning") && dated(q) && str(q, "endDate") < today)
            od << QVariantMap{ { QStringLiteral("kind"), QStringLiteral("done") }, { QStringLiteral("planId"), q[kId].toString() }, { QStringLiteral("name"), str(q, "name") } };
    for (const Value &d : root[QStringLiteral("documents")].asArray()) {
        const QString ex = str(d, "expires");
        if (!okDate(ex)) continue;
        const int n = days(today, ex);
        if (n <= 60) dc << QVariantMap{ { QStringLiteral("kind"), QStringLiteral("doc") }, { QStringLiteral("label"), str(d, "label") },
                                        { QStringLiteral("expires"), ex }, { QStringLiteral("n"), n } };
    }
    QVariantList nt;
    if (!od.isEmpty()) nt << od.first();
    if (od.size() > 1) nt << QVariantMap{ { QStringLiteral("kind"), QStringLiteral("more") }, { QStringLiteral("n"), int(od.size() - 1) } };
    for (int k = 0; k < dc.size() && k < 2; ++k) nt << dc[k];
    m.insert(QStringLiteral("notices"), nt);
    return m;
}

// ---------------- 简易计划面板 ----------------

QVariantMap sheetInfo(const Value &root, const QString &planId)
{
    const Value *pp = findPlan(root, planId);
    if (!pp) return {};
    const Value &p = *pp;
    QVariantMap m;
    m.insert(QStringLiteral("id"), planId);
    m.insert(QStringLiteral("name"), str(p, "name"));
    m.insert(QStringLiteral("status"), statusOf(p));
    m.insert(QStringLiteral("type"), typeOf(p));
    m.insert(QStringLiteral("color"), planColor(p));
    m.insert(QStringLiteral("regions"), regionsOf(p));
    m.insert(QStringLiteral("startDate"), str(p, "startDate"));
    m.insert(QStringLiteral("endDate"), str(p, "endDate"));
    m.insert(QStringLiteral("notes"), str(p, "notes"));
    m.insert(QStringLiteral("pinned"), pinOf(p) != 0);
    m.insert(QStringLiteral("isNext"), root[QStringLiteral("nextTripId")].toString() == planId);
    m.insert(QStringLiteral("cover"), str(p, "cover"));

    // 照片库里属于各站点的照片（按库里的先后）
    QHash<QString, QStringList> photosByStop;
    for (const Value &ph : root[QStringLiteral("photos")].asArray())
        if (str(ph, "plan") == planId && !str(ph, "stop").isEmpty()) photosByStop[str(ph, "stop")] << str(ph, "file");
    QString coverFile;
    if (!str(p, "cover").isEmpty())
        for (const Value &ph : root[QStringLiteral("photos")].asArray())
            if (str(ph, "id") == str(p, "cover")) { coverFile = str(ph, "file"); break; }
    m.insert(QStringLiteral("coverFile"), coverFile);

    const json::Array &stops = p[QStringLiteral("stops")].asArray();
    const QVector<int> st = validStopIndexes(p);
    QVariantList out;
    for (int k = 0; k < st.size(); ++k) {
        const Value &s = stops[st[k]];
        QVariantMap sm;
        sm.insert(QStringLiteral("name"), str(s, "name"));
        const QString c = str(s, "color");
        sm.insert(QStringLiteral("color"), c.isEmpty() ? planColor(p) : c);
        sm.insert(QStringLiteral("arriveDate"), str(s, "arriveDate"));
        sm.insert(QStringLiteral("departDate"), str(s, "departDate"));
        sm.insert(QStringLiteral("notes"), str(s, "notes"));
        sm.insert(QStringLiteral("schedule"), scheduleOf(s));
        if (k + 1 < st.size()) sm.insert(QStringLiteral("leg"), legOf(s));
        sm.insert(QStringLiteral("photos"), photosByStop.value(str(s, "id")));
        out << sm;
    }
    m.insert(QStringLiteral("stops"), out);
    m.insert(QStringLiteral("stopCount"), int(out.size()));
    return m;
}

// ---------------- 改数据 ----------------

bool setNextTrip(Value &root, const QString &planId, bool on)
{
    if (on && !findPlan(root, planId)) return false;
    json::Object &o = root.mutObject();
    const Value *cur = std::as_const(o).find(QStringLiteral("nextTripId"));
    if (on) {
        if (cur && cur->toString() == planId) return false;
        o.set(QStringLiteral("nextTripId"), Value(planId));
        return true;
    }
    return o.remove(QStringLiteral("nextTripId"));
}

namespace {
Value *plansArrayPlan(Value &root, const QString &planId)
{
    if (planId.isEmpty()) return nullptr;
    Value *plans = root.mutObject().find(QStringLiteral("plans"));
    if (!plans) return nullptr;
    for (Value &p : plans->mutArray())
        if (std::as_const(p)[kId].toString() == planId) return &p;
    return nullptr;
}
} // namespace

QVector<int> validStops(const Value &plan) { return validStopIndexes(plan); }
const Value *planById(const Value &root, const QString &id) { return findPlan(root, id); }
Value *mutPlan(Value &root, const QString &id) { return plansArrayPlan(root, id); }

bool setTodoDone(Value &root, const QString &planId, const QString &todoId, bool done)
{
    if (!findPlan(root, planId)) return false;
    Value *p = plansArrayPlan(root, planId);
    Value *todos = p->mutObject().find(QStringLiteral("todos"));
    if (!todos || !todos->isArray()) return false;
    for (Value &t : todos->mutArray())
        if (std::as_const(t)[kId].toString() == todoId && !todoId.isEmpty()) {
            if (std::as_const(t)[QStringLiteral("done")].toBool() == done && std::as_const(t).asObject().contains(QStringLiteral("done"))) return false;
            t.mutObject().set(QStringLiteral("done"), Value(done));
            return true;
        }
    return false;
}

bool togglePin(Value &root, const QString &planId, double nowMs)
{
    const Value *cp = findPlan(root, planId);
    if (!cp) return false;
    const bool pinned = pinOf(*cp) != 0;
    Value *p = plansArrayPlan(root, planId);
    if (pinned) p->mutObject().remove(QStringLiteral("pinnedAt"));
    else p->mutObject().set(QStringLiteral("pinnedAt"), Value(nowMs));
    return true;
}

bool markCompleted(Value &root, const QString &planId)
{
    const Value *cp = findPlan(root, planId);
    if (!cp || str(*cp, "status") == QLatin1String("completed")) return false;
    plansArrayPlan(root, planId)->mutObject().set(QStringLiteral("status"), Value(QStringLiteral("completed")));
    return true;
}

bool deletePlan(Value &root, const QString &planId)
{
    int idx = -1;
    if (!findPlan(root, planId, &idx)) return false;
    json::Array &arr = root.mutObject().find(QStringLiteral("plans"))->mutArray();
    arr.erase(arr.begin() + idx);
    return true;
}

// ---------------- "我的旅程"列表（0.8.8，S-08） ----------------

QVariantMap listInfo(const Value &root, const Filter &f, bool byYear)
{
    // Electron renderList：filteredPlans()（只看筛选，不看眼睛隐藏；隐藏的那一行变淡）按 comparePlans 排序
    const json::Array &arr = root[QStringLiteral("plans")].asArray();
    const QString nextId = root[QStringLiteral("nextTripId")].toString();
    QVariantList rows;
    QVector<int> order = sortedIndexes(root, !byYear);
    if (byYear) {
        // 足迹（0.8.10）：有日期的按日期从新到旧（开始日期，没有就用结束日期），没有日期的按原顺序放最后
        auto dayOf = [&arr](int i) {
            const QString a = str(arr[i], "startDate"), b = str(arr[i], "endDate");
            return okDate(a) ? a : okDate(b) ? b : QString();
        };
        std::stable_sort(order.begin(), order.end(), [&dayOf](int x, int y) {
            const QString a = dayOf(x), b = dayOf(y);
            if (a.isEmpty() != b.isEmpty()) return b.isEmpty();
            return a > b;
        });
    }
    for (int i : order) {
        const Value &p = arr[i];
        if (!f.matches(p)) continue;
        const QString id = p[kId].toString();
        const QString start = str(p, "startDate"), end = str(p, "endDate");
        int n = -1;   // dayCount：两个日期都有且读得出来才算，含首尾
        if (!start.isEmpty() && !end.isEmpty()) {
            const QDate a = QDate::fromString(start, QStringLiteral("yyyy-MM-dd")), b = QDate::fromString(end, QStringLiteral("yyyy-MM-dd"));
            if (a.isValid() && b.isValid()) n = int(a.daysTo(b)) + 1;
        }
        const bool pinned = pinOf(p) != 0;
        QVariantMap r;
        r.insert(QStringLiteral("id"), id);
        r.insert(QStringLiteral("name"), str(p, "name"));
        r.insert(QStringLiteral("color"), planColor(p));
        r.insert(QStringLiteral("status"), statusOf(p));
        r.insert(QStringLiteral("type"), typeOf(p));
        r.insert(QStringLiteral("stops"), int(validStopIndexes(p).size()));
        r.insert(QStringLiteral("startDate"), start);
        r.insert(QStringLiteral("endDate"), end);
        r.insert(QStringLiteral("days"), n);
        r.insert(QStringLiteral("pinned"), pinned);
        r.insert(QStringLiteral("hidden"), f.hidden.contains(id));
        r.insert(QStringLiteral("isNext"), !nextId.isEmpty() && id == nextId);
        // 分组（0.8.8 方案 B 的小标题）：置顶的单独一组，其余按状态；排序本来就是这个顺序，组是连续的。
        // 0.8.10 足迹：按年份
        if (byYear) {
            const QString d = okDate(start) ? start : okDate(end) ? end : QString();
            r.insert(QStringLiteral("group"), d.isEmpty() ? QStringLiteral("undated") : QStringLiteral("y") + d.left(4));
        } else {
            r.insert(QStringLiteral("group"), pinned ? QStringLiteral("pin") : statusOf(p));
        }
        rows << r;
    }
    // 每组几个（小标题上的数字）
    QHash<QString, int> inGroup;
    for (const QVariant &v : rows) inGroup[v.toMap().value(QStringLiteral("group")).toString()]++;
    for (QVariant &v : rows) {
        QVariantMap m = v.toMap();
        m.insert(QStringLiteral("groupCount"), inGroup.value(m.value(QStringLiteral("group")).toString()));
        v = m;
    }
    // 列表底部：全部计划的数目（Electron sbFoot 用 DATA.plans，不看筛选）
    int planning = 0, wish = 0, completed = 0;
    for (const Value &p : arr) {
        const QString st = statusOf(p);
        if (st == QLatin1String("wish")) ++wish;
        else if (st == QLatin1String("completed")) ++completed;
        else ++planning;
    }
    return { { QStringLiteral("rows"), rows }, { QStringLiteral("total"), int(arr.size()) },
             { QStringLiteral("planning"), planning }, { QStringLiteral("wish"), wish }, { QStringLiteral("completed"), completed } };
}

// ---------------- Ctrl K 的计划和站点（0.8.8，S-12） ----------------

QVariantList commandItems(const Value &root)
{
    // palette.js source()：按 DATA.plans 的原顺序，每个计划后面跟着它有名字的有效站点
    QVariantList out;
    for (const Value &p : root[QStringLiteral("plans")].asArray()) {
        const QString id = p[kId].toString();
        const QString color = planColor(p);
        out << QVariantMap{ { QStringLiteral("kind"), QStringLiteral("plan") }, { QStringLiteral("id"), id },
                            { QStringLiteral("label"), str(p, "name") }, { QStringLiteral("status"), statusOf(p) },
                            { QStringLiteral("startDate"), str(p, "startDate") }, { QStringLiteral("endDate"), str(p, "endDate") },
                            { QStringLiteral("color"), color } };
        const json::Array &stops = p[QStringLiteral("stops")].asArray();
        const QVector<int> st = validStopIndexes(p);
        for (int k = 0; k < st.size(); ++k) {
            const QString name = str(stops[st[k]], "name");
            if (name.isEmpty()) continue;
            out << QVariantMap{ { QStringLiteral("kind"), QStringLiteral("stop") }, { QStringLiteral("id"), id },
                                { QStringLiteral("stop"), k }, { QStringLiteral("label"), name },
                                { QStringLiteral("planName"), str(p, "name") }, { QStringLiteral("color"), color } };
        }
    }
    return out;
}

} // namespace tp::map::plans
