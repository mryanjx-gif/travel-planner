#include "plankit.h"

#include "plans.h"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QVector>

#include <algorithm>

namespace tp::map::plans {

using json::Value;

namespace {

const QString kPacking = QStringLiteral("packing"), kTodos = QStringLiteral("todos");
constexpr int kDueSoonDays = 3;   // M2：方案页写的"3 天内到期"

QString str(const Value &o, const char *key) { return o[QString::fromLatin1(key)].toString(); }

json::Array &mutList(Value &o, const QString &key)
{
    json::Object &m = o.mutObject();
    Value *v = m.find(key);
    if (!v || !v->isArray()) { m.set(key, Value::array()); v = m.find(key); }
    return v->mutArray();
}

QString newId(const QString &key)   // 和 Electron TP.uid 一样的样子
{
    static const char kChars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    QString s = key == kTodos ? QStringLiteral("td_") : QStringLiteral("pk_");
    for (int i = 0; i < 13; ++i) s += QLatin1Char(kChars[QRandomGenerator::global()->bounded(36)]);
    return s;
}

Value newCheck(const QString &key, const QString &text)
{
    json::Object o;
    o.set(QStringLiteral("id"), Value(newId(key)));
    o.set(QStringLiteral("text"), Value(text));
    o.set(QStringLiteral("done"), Value(false));
    return Value(std::move(o));
}

Value *findCheck(Value &plan, const QString &key, const QString &id)
{
    if (id.isEmpty() || !plan[key].isArray()) return nullptr;
    for (Value &x : mutList(plan, key))
        if (x.isObject() && str(x, "id") == id) return &x;
    return nullptr;
}

QVariantMap checkRow(const Value &x, const QString &dueState)
{
    return { { QStringLiteral("id"), str(x, "id") }, { QStringLiteral("text"), str(x, "text") }, { QStringLiteral("done"), x[QStringLiteral("done")].toBool() },
             { QStringLiteral("due"), str(x, "due") }, { QStringLiteral("dueState"), dueState } };
}

QString photoFileOf(const Value &root, const QString &target, const Value **rec = nullptr)
{
    if (!target.startsWith(QLatin1String("photo:"))) return {};
    const QString id = target.mid(6);
    for (const Value &ph : root[QStringLiteral("photos")].asArray())
        if (str(ph, "id") == id) { if (rec) *rec = &ph; return str(ph, "file"); }
    return {};
}

// Electron store.js mdInline：先把链接挑出来，其余转义，最后换加粗
QString mdInline(const QString &s)
{
    static const QRegularExpression re(QStringLiteral(R"(\[([^\]]+)\]\((https?://[^)\s]+)\)|(https?://[^\s<>()，。、；！？]+))"));
    QString out;
    int last = 0;
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += s.mid(last, m.capturedStart() - last).toHtmlEscaped();
        const QString url = !m.captured(2).isEmpty() ? m.captured(2) : m.captured(3);
        QString text = !m.captured(1).isEmpty() ? m.captured(1) : m.captured(3);
        if (m.captured(1).isEmpty() && text.size() > 60) text = text.left(57) + QChar(0x2026);
        out += QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), text.toHtmlEscaped());
        last = int(m.capturedEnd());
    }
    out += s.mid(last).toHtmlEscaped();
    static const QRegularExpression bold(QStringLiteral(R"(\*\*([^*]+)\*\*)"));
    out.replace(bold, QStringLiteral("<b>\\1</b>"));
    return out;
}

} // namespace

QVariantMap kitInfo(const Value &root, const Value &plan, const QString &today)
{
    QVariantMap m;
    const bool remind = statusOf(plan) == QLatin1String("planning");
    int doneCount[2] = { 0, 0 }, soon = 0, over = 0, total = 0;
    const QString keys[2] = { kPacking, kTodos };
    for (int t = 0; t < 2; ++t) {
        QVariantList open, finished;
        for (const Value &x : plan[keys[t]].asArray()) {
            if (!x.isObject()) continue;
            ++total;
            const bool done = x[QStringLiteral("done")].toBool();
            QString state;
            const QString due = str(x, "due");
            if (t == 1 && remind && !done && okDate(due)) {
                const int n = days(today, due);
                if (n < 0) { state = QStringLiteral("over"); ++over; }
                else if (n <= kDueSoonDays) { state = QStringLiteral("soon"); ++soon; }
            }
            if (done) { ++doneCount[t]; finished << checkRow(x, state); } else open << checkRow(x, state);
        }
        m.insert(keys[t], open + finished);
    }
    m.insert(QStringLiteral("packDone"), doneCount[0]);
    m.insert(QStringLiteral("todoDone"), doneCount[1]);
    m.insert(QStringLiteral("doneAll"), doneCount[0] + doneCount[1]);
    m.insert(QStringLiteral("totalAll"), total);
    m.insert(QStringLiteral("dueSoon"), soon);
    m.insert(QStringLiteral("dueOver"), over);
    m.insert(QStringLiteral("tplCount"), int(root[QStringLiteral("packingTemplate")].asArray().size()));

    QVariantList local;
    for (const Value &x : plan[QStringLiteral("localEmergency")].asArray())
        local << QVariantMap{ { QStringLiteral("label"), str(x, "label") }, { QStringLiteral("phone"), str(x, "phone") } };
    m.insert(QStringLiteral("local"), local);

    const Value &e = root[QStringLiteral("emergency")];
    QVariantList contacts;
    for (const Value &c : e[QStringLiteral("contacts")].asArray())
        contacts << QVariantMap{ { QStringLiteral("name"), str(c, "name") }, { QStringLiteral("relation"), str(c, "relation") }, { QStringLiteral("phone"), str(c, "phone") } };
    m.insert(QStringLiteral("emergency"), QVariantMap{
        { QStringLiteral("insurer"), str(e, "insurer") }, { QStringLiteral("policyNo"), str(e, "policyNo") },
        { QStringLiteral("insurerPhone"), str(e, "insurerPhone") }, { QStringLiteral("notes"), str(e, "notes") }, { QStringLiteral("contacts"), contacts } });
    return m;
}

QStringList splitLines(const QString &text)
{
    static const QRegularExpression nl(QStringLiteral("[\\r\\n\\x{2028}\\x{2029}]+"));
    static const QRegularExpression mark(QStringLiteral(R"(^(?:[-*•]\s+|\d+[.)]\s+)?(?:\[[ xX]\]\s+)?)"));
    QStringList out;
    for (QString l : text.split(nl, Qt::SkipEmptyParts)) {
        l = l.trimmed();
        l.remove(mark);
        l = l.trimmed();
        if (!l.isEmpty()) out << l;
    }
    return out;
}

int addChecks(Value &plan, const QString &key, const QString &text)
{
    if (key != kPacking && key != kTodos) return 0;
    const QStringList lines = splitLines(text);
    if (lines.isEmpty()) return 0;
    json::Array &list = mutList(plan, key);
    for (const QString &l : lines) list.push_back(newCheck(key, l));
    return int(lines.size());
}

bool setCheckDone(Value &plan, const QString &key, const QString &id, bool done)
{
    Value *x = findCheck(plan, key, id);
    if (!x) return false;
    x->mutObject().set(QStringLiteral("done"), Value(done));
    return true;
}

bool setCheckDue(Value &plan, const QString &id, const QString &due)
{
    Value *x = findCheck(plan, kTodos, id);
    if (!x || (!due.isEmpty() && !okDate(due))) return false;
    if (due.isEmpty()) x->mutObject().remove(QStringLiteral("due"));
    else x->mutObject().set(QStringLiteral("due"), Value(due));
    return true;
}

bool removeCheck(Value &plan, const QString &key, const QString &id)
{
    if (!findCheck(plan, key, id)) return false;
    json::Array &list = mutList(plan, key);
    for (auto it = list.begin(); it != list.end(); ++it)
        if (it->isObject() && str(*it, "id") == id) { list.erase(it); return true; }
    return false;
}

const QStringList &defaultPacking()
{
    static const QStringList l{ QStringLiteral("护照 / 身份证"), QStringLiteral("签证、保险单（电子版 + 打印）"), QStringLiteral("银行卡、少量现金"),
                                QStringLiteral("手机、充电器、充电宝"), QStringLiteral("转换插头"), QStringLiteral("常用药"), QStringLiteral("雨具"),
                                QStringLiteral("防晒"), QStringLiteral("换洗衣物"), QStringLiteral("洗漱用品"), QStringLiteral("水杯") };
    return l;
}

int addFromTemplate(Value &root, const QString &planId)
{
    QStringList tpl;
    for (const Value &x : root[QStringLiteral("packingTemplate")].asArray())
        if (x.isString() && !x.toString().isEmpty()) tpl << x.toString();
    if (tpl.isEmpty()) tpl = defaultPacking();
    Value *p = mutPlan(root, planId);
    if (!p) return 0;
    QSet<QString> have;
    for (const Value &x : (*p)[kPacking].asArray()) have.insert(str(x, "text"));
    int n = 0;
    for (const QString &t : tpl) {
        if (have.contains(t)) continue;
        have.insert(t);
        mutList(*p, kPacking).push_back(newCheck(kPacking, t));
        ++n;
    }
    return n;
}

int saveTemplate(Value &root, const QString &planId)
{
    const Value *p = planById(root, planId);
    if (!p) return 0;
    json::Array items;
    for (const Value &x : (*p)[kPacking].asArray())
        if (!str(x, "text").isEmpty()) items.push_back(Value(str(x, "text")));
    const int n = int(items.size());
    if (n > 0) root.mutObject().set(QStringLiteral("packingTemplate"), Value(std::move(items)));
    return n;
}

void setLocalEmergency(Value &plan, const QVariantMap &phones)
{
    static const QStringList labels{ QStringLiteral("报警"), QStringLiteral("急救"), QStringLiteral("消防"), QStringLiteral("使领馆 / 领事保护"), QStringLiteral("其他") };
    json::Array out;
    for (const QString &l : labels) {
        const QString phone = phones.value(l).toString().trimmed();
        if (phone.isEmpty()) continue;
        json::Object o;
        o.set(QStringLiteral("label"), Value(l));
        o.set(QStringLiteral("phone"), Value(phone));
        out.push_back(Value(std::move(o)));
    }
    for (const Value &x : plan[QStringLiteral("localEmergency")].asArray())
        if (!labels.contains(str(x, "label"))) out.push_back(x);
    if (out.empty()) plan.mutObject().remove(QStringLiteral("localEmergency"));
    else plan.mutObject().set(QStringLiteral("localEmergency"), Value(std::move(out)));
}

void setEmergency(Value &root, const QVariantMap &f)
{
    Value e = root[QStringLiteral("emergency")].isObject() ? root[QStringLiteral("emergency")] : Value::object();
    const auto text = [&](const QString &k) { return f.value(k).toString().trimmed(); };
    for (const char *k : { "insurer", "policyNo", "insurerPhone", "notes" }) {
        const QString key = QString::fromLatin1(k), v = text(key);
        if (v.isEmpty()) e.mutObject().remove(key); else e.mutObject().set(key, Value(v));
    }
    const json::Array old = e[QStringLiteral("contacts")].asArray();
    json::Array contacts;
    for (int i = 0; i < 3; ++i) {
        Value c = size_t(i) < old.size() && old[size_t(i)].isObject() ? old[size_t(i)] : Value::object();
        const QString name = text(QStringLiteral("n%1").arg(i)), rel = text(QStringLiteral("r%1").arg(i)), phone = text(QStringLiteral("p%1").arg(i));
        if (name.isEmpty() && phone.isEmpty()) continue;   // Electron：名字和电话都空 = 这一个不要了
        c.mutObject().set(QStringLiteral("name"), Value(name));
        c.mutObject().set(QStringLiteral("relation"), Value(rel));
        c.mutObject().set(QStringLiteral("phone"), Value(phone));
        contacts.push_back(std::move(c));
    }
    for (size_t i = 3; i < old.size(); ++i) contacts.push_back(old[i]);
    if (contacts.empty()) e.mutObject().remove(QStringLiteral("contacts"));
    else e.mutObject().set(QStringLiteral("contacts"), Value(std::move(contacts)));
    if (e.asObject().isEmpty()) root.mutObject().remove(QStringLiteral("emergency"));
    else root.mutObject().set(QStringLiteral("emergency"), std::move(e));
}

QVariantList journalList(const Value &root, const QString &planId)
{
    static const QRegularExpression anyPic(QStringLiteral(R"(!\[[^\]]*\]\(([^)]+)\))"));
    static const QRegularExpression marks(QStringLiteral("[#>*_`]"));
    QVector<const Value *> rows;
    for (const Value &j : root[QStringLiteral("journal")].asArray())
        if (str(j, "plan") == planId) rows << &j;
    std::stable_sort(rows.begin(), rows.end(), [](const Value *a, const Value *b) { return str(*a, "date") > str(*b, "date"); });
    QVariantList out;
    for (const Value *j : rows) {
        const QString body = str(*j, "body");
        QString text = body;
        text.remove(anyPic);
        text.remove(marks);
        out << QVariantMap{ { QStringLiteral("id"), str(*j, "id") }, { QStringLiteral("title"), str(*j, "title") }, { QStringLiteral("date"), str(*j, "date") },
                            { QStringLiteral("text"), text.trimmed().left(120) }, { QStringLiteral("file"), photoFileOf(root, anyPic.match(body).captured(1)) } };
    }
    return out;
}

QVariantMap guideBlocks(const Value &root, const QString &text)
{
    static const QRegularExpression picRe(QStringLiteral(R"(^!\[([^\]]*)\]\(([^)\s]+)\)$)"));
    static const QRegularExpression headRe(QStringLiteral(R"(^(#{1,3}) (.*)$)"));
    static const QRegularExpression liRe(QStringLiteral(R"(^[-*•] (\[( |x|X)\] )?(.*)$)"));
    QVariantList blocks, heads;
    QStringList para;
    const auto flush = [&] {
        if (para.isEmpty()) return;
        QStringList html;
        for (const QString &l : para) html << mdInline(l);
        blocks << QVariantMap{ { QStringLiteral("type"), QStringLiteral("p") }, { QStringLiteral("html"), html.join(QStringLiteral("<br>")) } };
        para.clear();
    };
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int ln = 0; ln < lines.size(); ++ln) {
        const QString l = lines[ln].trimmed();
        if (l.isEmpty()) { flush(); continue; }
        QRegularExpressionMatch m = picRe.match(l);
        if (m.hasMatch()) {
            flush();
            const Value *rec = nullptr;
            const QString file = photoFileOf(root, m.captured(2), &rec);
            blocks << QVariantMap{ { QStringLiteral("type"), QStringLiteral("img") }, { QStringLiteral("id"), rec ? str(*rec, "id") : QString() }, { QStringLiteral("file"), file }, { QStringLiteral("caption"), m.captured(1) },
                                   { QStringLiteral("width"), rec ? (*rec)[QStringLiteral("width")].toNumber() : 0.0 }, { QStringLiteral("height"), rec ? (*rec)[QStringLiteral("height")].toNumber() : 0.0 } };
            continue;
        }
        m = headRe.match(l);
        if (m.hasMatch()) {
            flush();
            const int level = int(m.captured(1).size());
            heads << QVariantMap{ { QStringLiteral("block"), int(blocks.size()) }, { QStringLiteral("level"), level }, { QStringLiteral("text"), QString(m.captured(2)).remove(QStringLiteral("**")) } };
            blocks << QVariantMap{ { QStringLiteral("type"), level == 1 ? QStringLiteral("h1") : QStringLiteral("h2") }, { QStringLiteral("html"), mdInline(m.captured(2)) } };
            continue;
        }
        if (l.startsWith(QLatin1String("> "))) {
            flush();
            blocks << QVariantMap{ { QStringLiteral("type"), QStringLiteral("quote") }, { QStringLiteral("html"), mdInline(l.mid(2)) } };
            continue;
        }
        m = liRe.match(l);
        if (m.hasMatch()) {
            flush();
            const bool task = !m.captured(1).isEmpty();
            blocks << QVariantMap{ { QStringLiteral("type"), task ? QStringLiteral("task") : QStringLiteral("li") }, { QStringLiteral("html"), mdInline(m.captured(3)) },
                                   { QStringLiteral("line"), ln }, { QStringLiteral("done"), task && m.captured(2) != QLatin1String(" ") } };
            continue;
        }
        para << lines[ln];
    }
    flush();
    return { { QStringLiteral("blocks"), blocks }, { QStringLiteral("heads"), heads } };
}

QString toggleTaskLine(const QString &text, int line, bool on)
{
    static const QRegularExpression re(QStringLiteral(R"(^(\s*[-*•] )\[( |x|X)\])"));
    QStringList lines = text.split(QLatin1Char('\n'));
    if (line < 0 || line >= lines.size()) return text;
    lines[line].replace(re, QStringLiteral("\\1[%1]").arg(on ? QLatin1Char('x') : QLatin1Char(' ')));
    return lines.join(QLatin1Char('\n'));
}

void setGuide(Value &plan, const QString &text)
{
    if (text.trimmed().isEmpty()) plan.mutObject().remove(QStringLiteral("guide"));
    else plan.mutObject().set(QStringLiteral("guide"), Value(text));
}

QVariantList planPhotos(const Value &root, const QString &planId)
{
    const Value *p = planById(root, planId);
    QVariantList out;
    if (!p) return out;
    const json::Array &stops = (*p)[QStringLiteral("stops")].asArray();
    const bool route = typeOf(*p) == QLatin1String("route");
    int stopNo = 0;
    for (int i : validStops(*p)) {
        const QString sid = str(stops[size_t(i)], "id");
        ++stopNo;
        for (const Value &ph : root[QStringLiteral("photos")].asArray())
            if (str(ph, "plan") == planId && str(ph, "stop") == sid)
                out << QVariantMap{ { QStringLiteral("id"), str(ph, "id") }, { QStringLiteral("file"), str(ph, "file") },
                                    { QStringLiteral("caption"), str(ph, "caption") }, { QStringLiteral("stop"), str(stops[size_t(i)], "name") },
                                    { QStringLiteral("stopId"), sid }, { QStringLiteral("stopNo"), stopNo }, { QStringLiteral("route"), route },
                                    { QStringLiteral("fav"), ph[QStringLiteral("fav")].toBool() }, { QStringLiteral("takenAt"), str(ph, "takenAt") } };
    }
    return out;
}

namespace {
Value *mutPhoto(Value &root, const QString &id)
{
    Value *list = root.isObject() ? root.mutObject().find(QStringLiteral("photos")) : nullptr;
    if (!list || !list->isArray() || id.isEmpty()) return nullptr;
    for (Value &ph : list->mutArray())
        if (ph.isObject() && str(ph, "id") == id) return &ph;
    return nullptr;
}
}

bool setPhotoFav(Value &root, const QString &id, bool on)
{
    Value *ph = mutPhoto(root, id);
    if (!ph) return false;
    if (on) ph->mutObject().set(QStringLiteral("fav"), Value(true));
    else ph->mutObject().remove(QStringLiteral("fav"));   // Electron 取消精选时 delete
    return true;
}

bool assignPhotos(Value &root, const QStringList &ids, const QString &planId, const QString &stopId)
{
    bool any = false;
    for (const QString &id : ids) {
        Value *ph = mutPhoto(root, id);
        if (!ph) continue;
        ph->mutObject().set(QStringLiteral("plan"), Value(planId));
        ph->mutObject().set(QStringLiteral("stop"), Value(stopId));
        any = true;
    }
    return any;
}

bool setPhotoCaption(Value &root, const QString &id, const QString &caption)
{
    Value *ph = mutPhoto(root, id);
    if (!ph) return false;
    ph->mutObject().set(QStringLiteral("caption"), Value(caption.trimmed()));
    return true;
}

bool removePhotoFromPlan(Value &root, const QString &planId, const QString &id)
{
    Value *ph = mutPhoto(root, id);
    if (!ph || str(*ph, "plan") != planId) return false;
    ph->mutObject().remove(QStringLiteral("plan"));
    ph->mutObject().remove(QStringLiteral("stop"));
    Value *p = mutPlan(root, planId);
    if (p && str(*p, "cover") == id) p->mutObject().remove(QStringLiteral("cover"));
    return true;
}

bool setPlanCover(Value &root, const QString &planId, const QString &id)
{
    Value *p = mutPlan(root, planId);
    if (!p || (!id.isEmpty() && !mutPhoto(root, id))) return false;
    if (id.isEmpty()) p->mutObject().remove(QStringLiteral("cover"));
    else p->mutObject().set(QStringLiteral("cover"), Value(id));
    return true;
}

} // namespace tp::map::plans
