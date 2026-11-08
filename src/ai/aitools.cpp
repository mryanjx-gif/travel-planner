#include "aitools.h"

#include "jsonio.h"
#include "planedit.h"
#include "plans.h"
#include "roads.h"
#include "translator.h"

#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSet>
#include <cmath>

namespace tp::ai::tools {

using json::Value;
namespace pe = tp::map::planedit;

namespace {

QString L(const char *s) { return QString::fromLatin1(s); }
QString str(const Value &v, const char *k) { return v[L(k)].toString(); }
QString cs(const QJsonObject &c, const char *k) { const QJsonValue v = c.value(L(k)); return v.isString() ? v.toString() : v.isDouble() ? QString::number(v.toDouble()) : QString(); }
bool hasNum(const QJsonObject &c, const char *k) { return c.value(L(k)).isDouble() && std::isfinite(c.value(L(k)).toDouble()); }
bool okDate(const QString &s) { return tp::map::plans::okDate(s); }

const char *const kOps[] = { "create_plan", "update_plan", "delete_plan", "set_days",
                             "add_stop", "update_stop", "remove_stop", "move_stop",
                             "add_schedule", "update_schedule", "remove_schedule",
                             "add_booking", "update_booking", "remove_booking",
                             "add_todo", "update_todo", "remove_todo",
                             "add_packing", "update_packing", "remove_packing" };
const char *const kOpZh[] = { "新建计划", "改计划", "删除计划", "预计天数",
                              "加站点", "改站点", "删站点", "挪站点",
                              "加日程", "改日程", "删日程",
                              "加交通住宿", "改交通住宿", "删交通住宿",
                              "加待办", "改待办", "删待办",
                              "加行李", "改行李", "删行李" };
int opIndex(const QString &op) { for (int i = 0; i < 20; ++i) if (op == QLatin1String(kOps[i])) return i; return -1; }
QString opZh(const QString &op) { const int i = opIndex(op); return i < 0 ? op : QString::fromUtf8(kOpZh[i]); }

// 工具定义（Anthropic 形状；Electron READ_TOOL / RESEARCH_TOOL / TOOL）
const char kDefs[] = R"JSON([
{"name":"get_plans","description":"读取计划的完整内容（站点带 id，日程带序号 n，交通住宿 / 待办 / 行李带 id）。要改或评估某个计划、但还没有它的完整内容时调用。只读，不需要用户确认。",
 "input_schema":{"type":"object","required":["plan_ids"],"properties":{"plan_ids":{"type":"array","items":{"type":"string"},"description":"计划 id，可以一次读几个"}}}},
{"name":"research_status","description":"查找进度汇报。要搜 3 次以上时：开始前先调用一次（todo 列出要查的几项，remaining_seconds 写预计还要多少秒），然后开始查；每一轮结束时再调用一次汇报。全部查完时 finished = true。app 会按时间预算决定自动继续，还是停下来问用户。",
 "input_schema":{"type":"object","required":["todo","remaining_seconds"],"properties":{
  "done":{"type":"array","items":{"type":"string"},"description":"已经查完的项（每项几个字）"},
  "todo":{"type":"array","items":{"type":"string"},"description":"还要查的项"},
  "remaining_seconds":{"type":"number","description":"预计还要多少秒（每次搜索大约按 15 秒算，再加整理的时间）"},
  "finished":{"type":"boolean"},
  "note":{"type":"string","description":"一句话：这一轮查到了什么要紧的"}}}},
{"name":"propose_changes","description":"向用户提出对计划的修改（可以跨多个计划，可以增、删、改、挪任何内容），并列出还缺什么。不会直接生效：用户先看到逐条预览，勾选后点“应用”才写入。",
 "input_schema":{"type":"object","required":["summary","changes"],"properties":{
  "summary":{"type":"string","description":"一句话说明这些修改"},
  "plan_id":{"type":"string","description":"默认的计划 id（每一项没写 plan_id 时用它；都不写 = 当前计划）"},
  "changes":{"type":"array","items":{"type":"object","required":["op"],"properties":{
   "op":{"type":"string","enum":["create_plan","update_plan","delete_plan","set_days","add_stop","update_stop","remove_stop","move_stop","add_schedule","update_schedule","remove_schedule","add_booking","update_booking","remove_booking","add_todo","update_todo","remove_todo","add_packing","update_packing","remove_packing"]},
   "plan_id":{"type":"string","description":"这一项改哪个计划；新建计划的 new_id 也可以"},
   "new_id":{"type":"string","description":"create_plan：给新计划起的临时 id，例如 new1"},
   "stop_id":{"type":"string","description":"站点 id（update_stop / remove_stop / move_stop / 日程相关）"},
   "index":{"type":"integer","description":"没有 stop_id 时可以用站点序号（从 1 开始）"},
   "after_stop_id":{"type":"string","description":"add_stop / move_stop：放在这个站点后面；\"start\" = 最前面；不填 = 最后"},
   "n":{"type":"integer","description":"update_schedule / remove_schedule：日程序号（get_plans 给的 n）"},
   "to_stop_id":{"type":"string","description":"update_schedule：把这条日程挪到另一个站点"},
   "booking_id":{"type":"string"},"todo_id":{"type":"string"},"packing_id":{"type":"string"},
   "name":{"type":"string","description":"站点名 / 计划名"},
   "type":{"type":"string","enum":["route","single"],"description":"create_plan：路线 / 单一目的地"},
   "status":{"type":"string","enum":["planning","completed","wish"]},
   "startDate":{"type":"string","description":"YYYY-MM-DD"},"endDate":{"type":"string","description":"YYYY-MM-DD"},
   "guide":{"type":"string","description":"update_plan / create_plan：攻略全文（整段替换）"},
   "lat":{"type":"number"},"lng":{"type":"number"},
   "legMode":{"type":"string","enum":["flight","train","ferry","drive","walk"]},
   "arriveDate":{"type":"string","description":"YYYY-MM-DD"},"departDate":{"type":"string","description":"YYYY-MM-DD"},
   "notes":{"type":"string","description":"站点备注 / 计划备注 / 交通住宿备注（整段替换）"},
   "date":{"type":"string","description":"日程日期 / 待办截止：YYYY-MM-DD"},
   "time":{"type":"string","description":"HH:MM"},"end_time":{"type":"string","description":"HH:MM"},
   "text":{"type":"string","description":"日程 / 待办 / 行李的内容"},
   "done":{"type":"boolean","description":"待办 / 行李：完成了没有"},
   "days":{"type":"number","description":"set_days：预计天数"},
   "kind":{"type":"string","enum":["flight","train","bus","ferry","car","hotel","ticket","other"]},
   "title":{"type":"string"},"from":{"type":"string"},"to":{"type":"string"},
   "start":{"type":"string","description":"YYYY-MM-DDTHH:MM（当地时间）"},"end":{"type":"string","description":"YYYY-MM-DDTHH:MM"},
   "ref":{"type":"string"},"cost":{"type":"number"}}}},
  "gaps":{"type":"array","description":"这个方案还缺什么、哪些没查（订票网站、开放日期、具体时间、班次、价格、预约、住宿等）。每条一句话。",
   "items":{"type":"object","required":["text"],"properties":{"text":{"type":"string"},"search":{"type":"boolean","description":"要不要联网查"}}}}}}}
])JSON";

// ---- 数据里找东西 ----
int planIdx(const Value &root, const QString &id) { int i = -1; pe::findPlan(root, id, &i); return i; }
Value *mutPlanAt(Value &root, int i) { return &root.mutObject().find(L("plans"))->mutArray()[i]; }
json::Array &stopsOf(Value &plan)
{
    json::Object &o = plan.mutObject();
    if (!o.find(L("stops")) || !o.find(L("stops"))->isArray()) o.set(L("stops"), Value::array());
    return o.find(L("stops"))->mutArray();
}
json::Array &listOf(Value &plan, const QString &key)
{
    json::Object &o = plan.mutObject();
    if (!o.find(key) || !o.find(key)->isArray()) o.set(key, Value::array());
    return o.find(key)->mutArray();
}
int idxById(const json::Array &a, const QString &id)
{
    if (id.isEmpty()) return -1;
    for (int i = 0; i < int(a.size()); ++i) if (str(a[i], "id") == id) return i;
    return -1;
}
int photoCount(const Value &root, const QString &planId, const QString &stopId)
{
    int n = 0;
    for (const Value &p : root[L("photos")].asArray())
        if (str(p, "plan") == planId && (stopId.isEmpty() || str(p, "stop") == stopId)) ++n;
    return n;
}
// 照片移到"未归类"（照片库里 plan / stop 都空）；照片文件不动（Electron toInbox）
void unassignPhotos(Value &root, const QString &planId, const QString &stopId)
{
    Value *ph = root.mutObject().find(L("photos"));
    if (!ph || !ph->isArray() || photoCount(root, planId, stopId) == 0) return;
    for (Value &p : ph->mutArray())
        if (str(p, "plan") == planId && (stopId.isEmpty() || str(p, "stop") == stopId)) { p.mutObject().remove(L("plan")); p.mutObject().remove(L("stop")); }
}
// 去下一站的道路折线不再对：清掉 path / dist / time / key（方式留着）
void clearLeg(Value &stop, bool &needRoute)
{
    Value *lv = stop.mutObject().find(L("leg"));
    if (!lv || !lv->isObject()) return;
    bool had = false;
    for (const char *k : { "path", "key", "dist", "time" }) had = lv->mutObject().remove(L(k)) || had;
    if (had && tp::map::roads::isRoadMode(tp::map::roads::legModeOf(str(*lv, "mode")))) needRoute = true;
    if (lv->asObject().isEmpty()) stop.mutObject().remove(L("leg"));
}
QString legMode(const Value &stop) { return tp::map::roads::legModeOf(stop[L("leg")][L("mode")].toString()); }

const char kMark[] = "_aiMark";   // 应用时临时给要改的日程做记号（删、挪不会让后面的项指错），应用完去掉

QString newKey(const QString &id) { return QStringLiteral("new:") + (id.isEmpty() ? QStringLiteral("new1") : id); }

void locate(const Value &root, QJsonObject &ch, const QString &defPlan, const QSet<QString> &newIds)
{
    const QJsonObject c = ch.value(L("c")).toObject();
    const QString op = cs(c, "op");
    auto bad = [&](const char *why) { ch.insert(L("bad"), QString::fromUtf8(why)); };
    if (op == QLatin1String("create_plan")) { ch.insert(L("key"), newKey(cs(c, "new_id"))); return; }
    const QString pid = c.contains(L("plan_id")) && !cs(c, "plan_id").isEmpty() ? cs(c, "plan_id") : defPlan;
    if (newIds.contains(pid)) {
        ch.insert(L("key"), newKey(pid));
        ch.insert(L("isNew"), true);
        static const QSet<QString> okNew{ L("add_stop"), L("add_booking"), L("add_todo"), L("add_packing"), L("update_plan"), L("set_days") };
        if (!okNew.contains(op)) bad("新计划还没有这一项");
        return;
    }
    const int pi = planIdx(root, pid);
    ch.insert(L("key"), pid.isEmpty() ? QStringLiteral("?") : pid);
    if (pi < 0) { bad("找不到这个计划"); return; }
    const Value &plan = root[L("plans")].asArray()[pi];
    const json::Array &stops = plan[L("stops")].asArray();
    static const QSet<QString> needStop{ L("update_stop"), L("remove_stop"), L("move_stop"), L("add_schedule"), L("update_schedule"), L("remove_schedule") };
    int si = -1;
    if (needStop.contains(op)) {
        si = idxById(stops, cs(c, "stop_id"));
        if (si < 0 && cs(c, "stop_id").isEmpty() && c.value(L("index")).toInt() >= 1 && c.value(L("index")).toInt() <= int(stops.size())) si = c.value(L("index")).toInt() - 1;
        if (si < 0 || str(stops[si], "id").isEmpty()) { bad("找不到这个站点"); return; }
        ch.insert(L("stop"), str(stops[si], "id"));
    }
    if (op == QLatin1String("update_schedule") || op == QLatin1String("remove_schedule")) {
        const int n = c.value(L("n")).toInt();
        if (n < 1 || n > int(stops[si][L("schedule")].asArray().size())) { bad("找不到这条日程"); return; }
        ch.insert(L("item"), n - 1);
    }
    if (op == QLatin1String("update_schedule") && !cs(c, "to_stop_id").isEmpty()) {
        if (idxById(stops, cs(c, "to_stop_id")) < 0) { bad("找不到要挪去的站点"); return; }
        ch.insert(L("toStop"), cs(c, "to_stop_id"));
    }
    if ((op == QLatin1String("add_stop") || op == QLatin1String("move_stop")) && !cs(c, "after_stop_id").isEmpty()) {
        const QString a = cs(c, "after_stop_id");
        if (a != QLatin1String("start") && idxById(stops, a) < 0) { bad("找不到要放在后面的那个站点"); return; }
        ch.insert(L("after"), a);
    }
    auto pick = [&](const char *list, const char *idKey, const char *why) {
        if (idxById(plan[L(list)].asArray(), cs(c, idKey)) < 0) bad(why);
        else ch.insert(L("obj"), cs(c, idKey));
    };
    if (op == QLatin1String("update_booking") || op == QLatin1String("remove_booking")) pick("bookings", "booking_id", "找不到这条交通住宿");
    if (op == QLatin1String("update_todo") || op == QLatin1String("remove_todo")) pick("todos", "todo_id", "找不到这条待办");
    if (op == QLatin1String("update_packing") || op == QLatin1String("remove_packing")) pick("packing", "packing_id", "找不到这条行李");
}

QString cut(const QString &s, int n) { return s.size() > n ? s.left(n) + QStringLiteral("…") : s; }
QString legZh(const QString &m)
{
    if (m == QLatin1String("flight")) return tp::tr("飞机");
    if (m == QLatin1String("train")) return tp::tr("火车");
    if (m == QLatin1String("ferry")) return tp::tr("船");
    if (m == QLatin1String("drive")) return tp::tr("车");
    if (m == QLatin1String("walk")) return tp::tr("徒步");
    return m;
}
QString statusZh(const QString &s)
{
    if (s == QLatin1String("planning")) return tp::tr("计划中");
    if (s == QLatin1String("completed")) return tp::tr("已完成");
    if (s == QLatin1String("wish")) return tp::tr("想去");
    return s;
}

// 每一项的说明（Electron changeHtml；这里是纯文字）
QString describe(const Value &root, const QJsonObject &ch)
{
    const QJsonObject c = ch.value(L("c")).toObject();
    const QString op = cs(c, "op");
    const int pi = ch.value(L("isNew")).toBool() ? -1 : planIdx(root, ch.value(L("key")).toString());
    const Value &plan = pi >= 0 ? root[L("plans")].asArray()[pi] : Value();
    const json::Array &stops = plan[L("stops")].asArray();
    auto stopName = [&](const QString &id) { const int i = idxById(stops, id); return i >= 0 ? str(stops[i], "name") : QString(); };
    auto objText = [&](const char *list) { const json::Array &a = plan[L(list)].asArray(); const int i = idxById(a, ch.value(L("obj")).toString()); return i >= 0 ? a[i] : Value(); };
    auto when = [](const QString &d, const QString &t, const QString &e) { QStringList p; if (!d.isEmpty()) p << d; if (!t.isEmpty()) p << t + (e.isEmpty() ? QString() : QStringLiteral("–") + e); return p.join(u' '); };
    auto place = [&](const QString &after) {
        if (after == QLatin1String("start")) return tp::tr("放在最前面");
        if (!after.isEmpty()) return tp::tr("放在 %1 后面").arg(stopName(after));
        return tp::tr("加在最后");
    };
    QStringList f;
    QString sn = stopName(ch.value(L("stop")).toString());
    if (ch.contains(L("bad"))) {   // 没认领到：照 AI 写的说，不去数照片
        if (sn.isEmpty()) sn = cs(c, "stop_id");
        if (op == QLatin1String("remove_stop") || op == QLatin1String("delete_plan")) return op == QLatin1String("delete_plan") ? ch.value(L("key")).toString() : sn;
    }
    if (op == QLatin1String("create_plan")) {
        QString d = cs(c, "name").isEmpty() ? tp::tr("新计划") : cs(c, "name");
        if (!cs(c, "startDate").isEmpty()) d += QStringLiteral(" · %1 – %2").arg(cs(c, "startDate"), cs(c, "endDate").isEmpty() ? QStringLiteral("?") : cs(c, "endDate"));
        if (!cs(c, "guide").isEmpty()) d += QStringLiteral(" · ") + tp::tr("附攻略");
        return d;
    }
    if (op == QLatin1String("update_plan")) {
        if (!cs(c, "name").isEmpty()) f << tp::tr("名字 → %1").arg(cs(c, "name"));
        if (!cs(c, "startDate").isEmpty() || !cs(c, "endDate").isEmpty()) f << tp::tr("日期 → %1 – %2").arg(cs(c, "startDate"), cs(c, "endDate"));
        if (!cs(c, "status").isEmpty()) f << tp::tr("状态 → %1").arg(statusZh(cs(c, "status")));
        if (!cs(c, "notes").isEmpty()) f << tp::tr("备注 → %1").arg(cut(cs(c, "notes"), 60));
        if (!cs(c, "guide").isEmpty()) f << tp::tr("攻略 → %1").arg(cut(cs(c, "guide"), 80));
        return f.isEmpty() ? tp::tr("（没有内容）") : f.join(QStringLiteral("；"));
    }
    if (op == QLatin1String("delete_plan")) {
        const int n = photoCount(root, ch.value(L("key")).toString(), QString());
        return str(plan, "name") + (n ? tp::tr("（%1 张照片会移到相册“未归类”）").arg(n) : QString());
    }
    if (op == QLatin1String("set_days")) return tp::tr("%1 天").arg(cs(c, "days"));
    auto stopFields = [&] {
        if (!cs(c, "name").isEmpty()) f << tp::tr("名字 → %1").arg(cs(c, "name"));
        if (hasNum(c, "lat") && hasNum(c, "lng")) f << tp::tr("坐标 → %1, %2").arg(c.value(L("lat")).toDouble(), 0, 'f', 4).arg(c.value(L("lng")).toDouble(), 0, 'f', 4);
        if (!cs(c, "legMode").isEmpty()) f << tp::tr("去下一站 → %1").arg(legZh(cs(c, "legMode")));
        if (!cs(c, "arriveDate").isEmpty()) f << tp::tr("到达 %1").arg(cs(c, "arriveDate"));
        if (!cs(c, "departDate").isEmpty()) f << tp::tr("离开 %1").arg(cs(c, "departDate"));
        if (!cs(c, "notes").isEmpty()) f << tp::tr("备注 → %1").arg(cut(cs(c, "notes"), 60));
        return f.isEmpty() ? tp::tr("（没有内容）") : f.join(QStringLiteral("；"));
    };
    if (op == QLatin1String("add_stop")) {
        QString d = cs(c, "name") + QStringLiteral("，") + place(ch.value(L("after")).toString());
        if (!cs(c, "legMode").isEmpty()) d += QStringLiteral(" · ") + tp::tr("去下一站：%1").arg(legZh(cs(c, "legMode")));
        if (!cs(c, "arriveDate").isEmpty()) d += QStringLiteral(" · ") + cs(c, "arriveDate");
        return d;
    }
    if (op == QLatin1String("update_stop")) return sn + QStringLiteral("：") + stopFields();
    if (op == QLatin1String("remove_stop")) {
        const int n = photoCount(root, ch.value(L("key")).toString(), ch.value(L("stop")).toString());
        return sn + (n ? tp::tr("（%1 张照片会移到相册“未归类”）").arg(n) : QString());
    }
    if (op == QLatin1String("move_stop")) {
        const QString a = ch.value(L("after")).toString();
        return sn + QStringLiteral(" → ") + (a == QLatin1String("start") ? tp::tr("最前面") : !a.isEmpty() ? tp::tr("%1 后面").arg(stopName(a)) : tp::tr("最后"));
    }
    Value item;
    if (const int si = idxById(stops, ch.value(L("stop")).toString()); si >= 0 && ch.contains(L("item"))) {
        const json::Array &sch = stops[si][L("schedule")].asArray();
        const int ii = ch.value(L("item")).toInt();
        if (ii >= 0 && ii < int(sch.size())) item = sch[ii];
    }
    if (op == QLatin1String("add_schedule")) return sn + QStringLiteral("：") + when(cs(c, "date"), cs(c, "time"), cs(c, "end_time")) + u' ' + cs(c, "text");
    if (op == QLatin1String("update_schedule")) {
        QString d = sn + QStringLiteral("：") + cut(str(item, "text"), 30) + QStringLiteral(" → ") + when(cs(c, "date"), cs(c, "time"), cs(c, "end_time")) + u' ' + cs(c, "text");
        if (ch.contains(L("toStop"))) d += QStringLiteral(" · ") + tp::tr("挪到 %1").arg(stopName(ch.value(L("toStop")).toString()));
        return d;
    }
    if (op == QLatin1String("remove_schedule")) return sn + QStringLiteral("：") + when(str(item, "date"), str(item, "time"), str(item, "end")) + u' ' + cut(str(item, "text"), 60);
    if (op == QLatin1String("add_booking")) {
        QString d = cs(c, "title").isEmpty() ? cs(c, "kind") : cs(c, "title");
        if (!cs(c, "from").isEmpty() || !cs(c, "to").isEmpty()) d += QStringLiteral(" · %1 → %2").arg(cs(c, "from").isEmpty() ? QStringLiteral("?") : cs(c, "from"), cs(c, "to").isEmpty() ? QStringLiteral("?") : cs(c, "to"));
        if (!cs(c, "start").isEmpty()) d += QStringLiteral(" · ") + cs(c, "start").replace(u'T', u' ');
        if (!cs(c, "ref").isEmpty()) d += QStringLiteral(" · ") + tp::tr("订单号 %1").arg(cs(c, "ref"));
        if (hasNum(c, "cost")) d += QStringLiteral(" · ") + cs(c, "cost");
        return d;
    }
    if (op == QLatin1String("update_booking") || op == QLatin1String("remove_booking")) {
        const Value b = objText("bookings");
        QString d = str(b, "title").isEmpty() ? str(b, "kind") : str(b, "title");
        if (op == QLatin1String("remove_booking")) return d + (str(b, "start").isEmpty() ? QString() : QStringLiteral(" · ") + str(b, "start").replace(u'T', u' '));
        for (const char *k : { "title", "from", "to", "start", "end", "ref", "notes" })
            if (!cs(c, k).isEmpty()) f << QStringLiteral("%1 → %2").arg(L(k), cut(cs(c, k).replace(u'T', u' '), 40));
        return d + QStringLiteral("：") + f.join(QStringLiteral("；"));
    }
    if (op == QLatin1String("add_todo") || op == QLatin1String("add_packing")) return cs(c, "text") + (cs(c, "date").isEmpty() || op == QLatin1String("add_packing") ? QString() : tp::tr("（截止 %1）").arg(cs(c, "date")));
    const Value o = objText(op.endsWith(QLatin1String("todo")) ? "todos" : "packing");
    if (op.startsWith(QLatin1String("remove"))) return str(o, "text");
    QString d = str(o, "text");
    if (!cs(c, "text").isEmpty()) d += QStringLiteral(" → ") + cs(c, "text");
    if (c.value(L("done")).isBool()) d += QStringLiteral(" · ") + (c.value(L("done")).toBool() ? tp::tr("标记完成") : tp::tr("标记未完成"));
    if (!cs(c, "date").isEmpty() && op == QLatin1String("update_todo")) d += QStringLiteral(" · ") + tp::tr("截止 %1").arg(cs(c, "date"));
    return d;
}

} // namespace

bool canSearch(const QString &id) { return id == QLatin1String("claude") || id == QLatin1String("qwen") || id == QLatin1String("kimi"); }

QJsonArray defs()
{
    static const QJsonArray a = QJsonDocument::fromJson(QByteArray(kDefs)).array();
    return a;
}

QJsonArray anthropicTools(bool web)
{
    QJsonArray a = defs();
    if (web) a.append(QJsonObject{ { L("type"), L("web_search_20250305") }, { L("name"), L("web_search") }, { L("max_uses"), kMaxSearch } });
    return a;
}

QJsonArray openAiTools(const QString &providerId, bool web)
{
    QJsonArray a;
    for (const auto &v : defs()) {
        const QJsonObject d = v.toObject();
        a.append(QJsonObject{ { L("type"), L("function") }, { L("function"), QJsonObject{ { L("name"), d.value(L("name")) }, { L("description"), d.value(L("description")) }, { L("parameters"), d.value(L("input_schema")) } } } });
    }
    if (web && providerId == QLatin1String("kimi"))   // Kimi 内置联网：模型调 $web_search，我们把参数原样回传，它在服务器上搜
        a.append(QJsonObject{ { L("type"), L("builtin_function") }, { L("function"), QJsonObject{ { L("name"), L("$web_search") } } } });
    return a;
}

// ---------------- 上下文 ----------------

QString planForAi(const Value &root, const Value &plan, bool noMoney)
{
    Value x = plan;
    json::Object &o = x.mutObject();
    for (const char *k : { "cover", "pinnedAt", "icon", "shape", "markerMode", "color", "photoCredits", "createdAt", "updatedAt", "localEmergency" }) o.remove(L(k));
    if (noMoney) {
        o.remove(L("expenses"));
        o.remove(L("budget"));
        if (Value *b = o.find(L("bookings")); b && b->isArray())
            for (Value &bk : b->mutArray()) { bk.mutObject().remove(L("ref")); bk.mutObject().remove(L("cost")); }
    }
    const QString pid = str(plan, "id");
    if (Value *sv = o.find(L("stops")); sv && sv->isArray()) {
        int i = 0;
        for (Value &s : sv->mutArray()) {
            json::Object &so = s.mutObject();
            const QString mode = legMode(s);
            for (const char *k : { "leg", "icon", "shape", "color" }) so.remove(L(k));
            if (mode != QLatin1String("flight")) so.set(L("legMode"), Value(mode));
            so.set(L("index"), Value(++i));
            if (const int n = photoCount(root, pid, str(s, "id"))) so.set(L("photoCount"), Value(n));
            if (Value *sch = so.find(L("schedule")); sch && sch->isArray()) {
                int k = 0;
                for (Value &it : sch->mutArray()) if (it.isObject()) it.mutObject().set(L("n"), Value(++k)); else ++k;
            }
        }
    }
    return json::stringify(x, 0);
}

QString plansSummary(const Value &root)
{
    json::Array out;
    for (const Value &p : root[L("plans")].asArray()) {
        json::Object o;
        for (const char *k : { "id", "name", "status", "type", "startDate", "endDate", "days", "regions" })
            if (!p[L(k)].isNull()) o.set(L(k), p[L(k)]);
        const json::Array &st = p[L("stops")].asArray();
        o.set(L("stops"), Value(int(st.size())));
        json::Array first;
        for (size_t i = 0; i < st.size() && i < 6; ++i) first.push_back(Value(str(st[i], "name")));
        o.set(L("firstStops"), Value(std::move(first)));
        out.push_back(Value(std::move(o)));
    }
    return json::stringify(Value(std::move(out)), 0);
}

QString readPlans(const Value &root, const QJsonObject &input, bool noMoney, QStringList *names)
{
    QStringList parts, miss;
    const QJsonArray ids = input.value(L("plan_ids")).toArray();
    for (int i = 0; i < ids.size() && i < 12; ++i) {
        const QString id = ids.at(i).toString();
        const int pi = planIdx(root, id);
        if (pi < 0) { miss << id; continue; }
        const Value &p = root[L("plans")].asArray()[pi];
        parts << planForAi(root, p, noMoney);
        if (names) *names << str(p, "name");
    }
    QJsonArray m;
    for (const QString &s : miss) m.append(s);
    return QStringLiteral("{\"plans\":[%1],\"notFound\":%2}").arg(parts.join(u','), QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact)));
}

QString liveLine(const Value &root, const QString &today, QString *planId)
{
    const QString id = root[L("nextTripId")].toString();
    const int pi = planIdx(root, id);
    if (pi < 0) return {};
    const Value &p = root[L("plans")].asArray()[pi];
    const QString s = str(p, "startDate"), e = str(p, "endDate");
    if (!okDate(s) || !okDate(e) || s > today || today > e) return {};
    const json::Array &st = p[L("stops")].asArray();
    int i = -1;
    for (int k = 0; k < int(st.size()); ++k) if (okDate(str(st[k], "arriveDate")) && str(st[k], "arriveDate") <= today) i = k;
    const int day = int(QDate::fromString(s, Qt::ISODate).daysTo(QDate::fromString(today, Qt::ISODate))) + 1;
    if (planId) *planId = id;
    QString line = QStringLiteral("正在旅行中：「%1」第 %2 天").arg(str(p, "name")).arg(day);
    if (i >= 0) line += QStringLiteral("，今天在第 %1 站 %2").arg(i + 1).arg(str(st[i], "name"));
    if (i + 1 < int(st.size())) line += QStringLiteral("，下一站 %1（%2）").arg(str(st[i + 1], "name"), str(st[i + 1], "arriveDate").isEmpty() ? QStringLiteral("日期未定") : str(st[i + 1], "arriveDate"));
    return line + QStringLiteral("。");
}

// ---------------- 预览 ----------------

bool isDel(const QString &op) { return op.startsWith(QLatin1String("remove_")) || op == QLatin1String("delete_plan"); }
QString opLabel(const QString &op) { const int i = opIndex(op); return i < 0 ? op : tp::tr(kOpZh[i]); }

QJsonObject prepare(const Value &root, const QJsonObject &input, const QString &defPlan, const QString &toolId, bool truncated)
{
    QJsonObject P{ { L("toolId"), toolId }, { L("summary"), input.value(L("summary")).toString() } };
    if (truncated) { P.insert(L("state"), L("truncated")); return P; }
    QSet<QString> newIds;
    QJsonArray raw;
    for (const auto &v : input.value(L("changes")).toArray()) {
        const QJsonObject c = v.toObject();
        if (opIndex(cs(c, "op")) < 0) continue;
        raw.append(c);
        if (cs(c, "op") == QLatin1String("create_plan")) newIds.insert(cs(c, "new_id").isEmpty() ? QStringLiteral("new1") : cs(c, "new_id"));
    }
    const QString def = !input.value(L("plan_id")).toString().isEmpty() ? input.value(L("plan_id")).toString() : defPlan;
    QJsonArray changes;
    for (const auto &v : raw) {
        QJsonObject ch{ { L("c"), v }, { L("on"), true } };
        locate(root, ch, def, newIds);
        if (ch.contains(L("bad"))) ch.insert(L("on"), false);
        else if (cs(v.toObject(), "op") == QLatin1String("add_stop")) ch.insert(L("geo"), QJsonObject{ { L("state"), L("checking") } });
        changes.append(ch);
    }
    QJsonArray gaps;
    for (const auto &v : input.value(L("gaps")).toArray())
        if (!v.toObject().value(L("text")).toString().isEmpty())
            gaps.append(QJsonObject{ { L("text"), v.toObject().value(L("text")).toString() }, { L("search"), v.toObject().value(L("search")).toBool() }, { L("on"), true } });
    P.insert(L("changes"), changes);
    P.insert(L("gaps"), gaps);
    P.insert(L("gapState"), gaps.isEmpty() ? QString() : L("open"));
    P.insert(L("state"), !changes.isEmpty() ? L("open") : !gaps.isEmpty() ? L("gapsonly") : L("empty"));
    return P;
}

bool geoBusy(const QJsonObject &P)
{
    for (const auto &v : P.value(L("changes")).toArray())
        if (v.toObject().value(L("geo")).toObject().value(L("state")).toString() == QLatin1String("checking") && !v.toObject().contains(L("bad"))) return true;
    return false;
}

QVariantMap view(const Value &root, const QJsonObject &P)
{
    const QJsonArray chs = P.value(L("changes")).toArray();
    QStringList keys;
    for (const auto &v : chs) if (!keys.contains(v.toObject().value(L("key")).toString())) keys << v.toObject().value(L("key")).toString();
    int nOn = 0, nOk = 0, nDel = 0;
    QVariantList groups;
    if (P.value(L("state")).toString() != QLatin1String("open")) keys.clear();   // 应用过 / 过期的不再逐条显示（目标可能已经不在了）
    for (const QString &key : keys) {
        QString name;
        if (key.startsWith(QLatin1String("new:"))) {
            for (const auto &v : chs) { const QJsonObject c = v.toObject().value(L("c")).toObject(); if (cs(c, "op") == QLatin1String("create_plan") && v.toObject().value(L("key")).toString() == key) name = cs(c, "name"); }
            name = tp::tr("新计划：%1").arg(name);
        } else {
            const int pi = planIdx(root, key);
            name = pi >= 0 ? str(root[L("plans")].asArray()[pi], "name") : tp::tr("（找不到的计划）");
        }
        QVariantList rows;
        for (int k = 0; k < chs.size(); ++k) {
            const QJsonObject ch = chs.at(k).toObject();
            if (ch.value(L("key")).toString() != key) continue;
            const QJsonObject c = ch.value(L("c")).toObject();
            const QString op = cs(c, "op");
            QVariantMap row{ { L("k"), k }, { L("label"), opLabel(op) }, { L("desc"), describe(root, ch) }, { L("on"), ch.value(L("on")).toBool() },
                             { L("bad"), ch.value(L("bad")).toString().isEmpty() ? QString() : tp::tr(ch.value(L("bad")).toString().toUtf8().constData()) }, { L("del"), isDel(op) } };
            if (ch.contains(L("geo")) && !ch.contains(L("bad"))) {
                const QJsonObject g = ch.value(L("geo")).toObject();
                const QString st = g.value(L("state")).toString();
                const QJsonArray cands = g.value(L("cands")).toArray();
                QVariantList cl;
                for (const auto &x : cands) cl << x.toObject().value(L("addr")).toString();
                QString t;
                if (st == QLatin1String("checking")) t = tp::tr("正在用地名搜索核对坐标…");
                else if (st == QLatin1String("ok")) t = tp::tr("✓ 地名搜索核对过：%1").arg(cands.at(g.value(L("pick")).toInt()).toObject().value(L("addr")).toString());
                else if (st == QLatin1String("off")) t = tp::tr("地名搜索已关闭，没法核对。");
                else t = cands.isEmpty() ? tp::tr("地名搜索查不到这个地点。") : tp::tr("坐标和搜索结果对不上，选一个：");
                row.insert(L("geo"), QVariantMap{ { L("state"), st }, { L("text"), t }, { L("cands"), st == QLatin1String("ok") ? QVariantList() : cl }, { L("pick"), g.value(L("pick")).toInt(-1) },
                                                  { L("ai"), hasNum(c, "lat") && hasNum(c, "lng") ? QStringLiteral("%1, %2").arg(c.value(L("lat")).toDouble(), 0, 'f', 4).arg(c.value(L("lng")).toDouble(), 0, 'f', 4) : QString() } });
            }
            if (ch.value(L("bad")).toString().isEmpty()) { ++nOk; if (ch.value(L("on")).toBool()) { ++nOn; if (isDel(op)) ++nDel; } }
            rows << row;
        }
        groups << QVariantMap{ { L("name"), name }, { L("head"), keys.size() > 1 || key.startsWith(QLatin1String("new:")) }, { L("rows"), rows } };
    }
    QVariantList gaps;
    for (const auto &v : P.value(L("gaps")).toArray()) gaps << v.toObject().toVariantMap();
    return { { L("state"), P.value(L("state")).toString() }, { L("summary"), P.value(L("summary")).toString() }, { L("applied"), P.value(L("applied")).toInt() },
             { L("warn"), P.value(L("warn")).toString() }, { L("total"), int(chs.size()) }, { L("nOn"), nOn }, { L("nOk"), nOk }, { L("nDel"), nDel },
             { L("busyGeo"), geoBusy(P) }, { L("groups"), groups }, { L("gaps"), gaps }, { L("gapState"), P.value(L("gapState")).toString() } };
}

// ---------------- 应用 ----------------

Applied apply(Value &root, const QJsonObject &P, const std::function<QString(const QString &)> &newId)
{
    Applied a;
    QList<QJsonObject> list;
    for (const auto &v : P.value(L("changes")).toArray())
        if (v.toObject().value(L("on")).toBool() && !v.toObject().contains(L("bad"))) list << v.toObject();
    QHash<QString, QString> created;   // new:key → 计划 id
    auto planIndex = [&](const QString &key) { return planIdx(root, created.value(key, key)); };
    // 0. 给要改 / 删的日程做记号
    for (int k = 0; k < list.size(); ++k) {
        const QString op = cs(list[k].value(L("c")).toObject(), "op");
        if (op != QLatin1String("update_schedule") && op != QLatin1String("remove_schedule")) continue;
        const int pi = planIndex(list[k].value(L("key")).toString());
        if (pi < 0) continue;
        json::Array &stops = stopsOf(*mutPlanAt(root, pi));
        const int si = idxById(stops, list[k].value(L("stop")).toString());
        if (si < 0) continue;
        json::Array &sch = stops[si].mutObject().find(L("schedule"))->mutArray();
        const int it = list[k].value(L("item")).toInt();
        if (it >= 0 && it < int(sch.size()) && sch[it].isObject()) sch[it].mutObject().set(L(kMark), Value(k));
    }
    auto findMarked = [](json::Array &stops, int k, int *si, int *ii) {
        for (int s = 0; s < int(stops.size()); ++s) {
            const json::Array &sch = stops[s][L("schedule")].asArray();
            for (int i = 0; i < int(sch.size()); ++i)
                if (sch[i][L(kMark)].isNumber() && int(sch[i][L(kMark)].toNumber()) == k) { *si = s; *ii = i; return true; }
        }
        return false;
    };
    const double now = double(QDateTime::currentMSecsSinceEpoch());
    // 1. 新建计划
    for (const QJsonObject &ch : list) {
        const QJsonObject c = ch.value(L("c")).toObject();
        if (cs(c, "op") != QLatin1String("create_plan")) continue;
        Value p = pe::blankPlan(newId(L("p")), now);
        json::Object &o = p.mutObject();
        o.set(L("name"), Value(cs(c, "name").isEmpty() ? QStringLiteral("新计划") : cs(c, "name")));
        o.set(L("type"), Value(cs(c, "type") == QLatin1String("single") ? L("single") : L("route")));
        const QString stt = cs(c, "status");
        if (stt == QLatin1String("completed") || stt == QLatin1String("wish")) o.set(L("status"), Value(stt));
        if (okDate(cs(c, "startDate"))) o.set(L("startDate"), Value(cs(c, "startDate")));
        if (okDate(cs(c, "endDate"))) o.set(L("endDate"), Value(cs(c, "endDate")));
        o.set(L("notes"), Value(cs(c, "notes")));
        if (!cs(c, "guide").isEmpty()) o.set(L("guide"), Value(cs(c, "guide")));
        const QString id = str(p, "id");
        root.mutObject().find(L("plans"))->mutArray().push_back(p);
        created.insert(ch.value(L("key")).toString(), id);
        a.created.insert(ch.value(L("key")).toString().mid(4), id);
        ++a.n;
    }
    QHash<QString, QString> lastAfter;   // 同一个锚点后面连着加几站时按顺序排
    // 2. 其余修改（按 AI 给的顺序）
    for (int k = 0; k < list.size(); ++k) {
        const QJsonObject &ch = list[k];
        const QJsonObject c = ch.value(L("c")).toObject();
        const QString op = cs(c, "op");
        if (op == QLatin1String("create_plan") || op == QLatin1String("delete_plan")) continue;
        const int pi = planIndex(ch.value(L("key")).toString());
        if (pi < 0) continue;
        Value &plan = *mutPlanAt(root, pi);
        const QString pid = str(plan, "id");
        json::Object &po = plan.mutObject();
        if (op == QLatin1String("update_plan")) {
            if (!cs(c, "name").isEmpty()) po.set(L("name"), Value(cs(c, "name")));
            if (okDate(cs(c, "startDate"))) po.set(L("startDate"), Value(cs(c, "startDate")));
            if (okDate(cs(c, "endDate"))) po.set(L("endDate"), Value(cs(c, "endDate")));
            const QString stt = cs(c, "status");
            if (stt == QLatin1String("planning") || stt == QLatin1String("completed") || stt == QLatin1String("wish")) po.set(L("status"), Value(stt));
            if (!cs(c, "notes").isEmpty()) po.set(L("notes"), Value(cs(c, "notes")));
            if (!cs(c, "guide").isEmpty()) po.set(L("guide"), Value(cs(c, "guide")));
            ++a.n;
        } else if (op == QLatin1String("set_days")) {
            if (c.value(L("days")).toDouble() >= 1) { po.set(L("days"), Value(double(std::lround(c.value(L("days")).toDouble())))); ++a.n; }
        } else if (op == QLatin1String("add_stop")) {
            const QJsonObject g = ch.value(L("geo")).toObject();
            const int pk = g.value(L("pick")).toInt(-1);
            const QJsonObject use = pk >= 0 ? g.value(L("cands")).toArray().at(pk).toObject() : QJsonObject();
            const double lat = use.isEmpty() ? c.value(L("lat")).toDouble(qQNaN()) : use.value(L("lat")).toDouble();
            const double lng = use.isEmpty() ? c.value(L("lng")).toDouble(qQNaN()) : use.value(L("lng")).toDouble();
            if (!std::isfinite(lat) || !std::isfinite(lng)) continue;
            QString name = cs(c, "name");
            if (name.isEmpty()) name = use.value(L("name")).toString(QStringLiteral("未命名地点"));
            Value s = pe::newStop(newId(L("s")), name, lat, lng);
            json::Object &so = s.mutObject();
            if (okDate(cs(c, "arriveDate"))) so.set(L("arriveDate"), Value(cs(c, "arriveDate")));
            if (okDate(cs(c, "departDate"))) so.set(L("departDate"), Value(cs(c, "departDate")));
            const QString mode = tp::map::roads::legModeOf(cs(c, "legMode"));
            QStringList notes;
            if (!cs(c, "notes").isEmpty()) notes << cs(c, "notes");
            if (use.isEmpty()) notes << QStringLiteral("（坐标待核实：AI 给的，地名搜索没对上）");
            if (!notes.isEmpty()) so.set(L("notes"), Value(notes.join(u'\n')));
            if (!cs(c, "legMode").isEmpty() && mode != QLatin1String("flight")) {
                so.set(L("leg"), Value(json::Object()));
                so.find(L("leg"))->mutObject().set(L("mode"), Value(mode));
                if (tp::map::roads::isRoadMode(mode)) a.needRoute = true;
            }
            json::Array &stops = stopsOf(plan);
            const QString anchor = pid + u'|' + (ch.contains(L("after")) ? ch.value(L("after")).toString() : QStringLiteral("_end"));
            int at;
            if (lastAfter.contains(anchor)) at = idxById(stops, lastAfter.value(anchor)) + 1;
            else if (ch.value(L("after")).toString() == QLatin1String("start")) at = 0;
            else if (ch.contains(L("after"))) at = idxById(stops, ch.value(L("after")).toString()) + 1;
            else at = int(stops.size());
            if (at <= 0 && ch.value(L("after")).toString() != QLatin1String("start") && !lastAfter.contains(anchor) && ch.contains(L("after"))) at = int(stops.size());
            at = std::clamp(at, 0, int(stops.size()));
            if (at > 0) clearLeg(stops[at - 1], a.needRoute);   // 插进中间：前一站原来到下一站的折线不再对
            lastAfter.insert(anchor, str(s, "id"));
            stops.insert(stops.begin() + at, s);
            ++a.n;
        } else if (op == QLatin1String("update_stop")) {
            json::Array &stops = stopsOf(plan);
            const int i = idxById(stops, ch.value(L("stop")).toString());
            if (i < 0) continue;
            json::Object &so = stops[i].mutObject();
            if (!cs(c, "name").isEmpty()) so.set(L("name"), Value(cs(c, "name")));
            if (hasNum(c, "lat") && hasNum(c, "lng")) {
                so.set(L("lat"), Value(pe::round6(c.value(L("lat")).toDouble())));
                so.set(L("lng"), Value(pe::round6(pe::normLng(c.value(L("lng")).toDouble()))));
                so.remove(L("elev"));
                so.remove(L("tz"));
                clearLeg(stops[i], a.needRoute);
                if (i > 0) clearLeg(stops[i - 1], a.needRoute);
            }
            if (okDate(cs(c, "arriveDate"))) stops[i].mutObject().set(L("arriveDate"), Value(cs(c, "arriveDate")));
            if (okDate(cs(c, "departDate"))) stops[i].mutObject().set(L("departDate"), Value(cs(c, "departDate")));
            if (!cs(c, "notes").isEmpty()) stops[i].mutObject().set(L("notes"), Value(cs(c, "notes")));
            if (!cs(c, "legMode").isEmpty() && pe::setLegMode(plan, i, cs(c, "legMode"))) {
                json::Array &st2 = stopsOf(plan);
                clearLeg(st2[i], a.needRoute);
                if (tp::map::roads::isRoadMode(legMode(st2[i]))) a.needRoute = true;
            }
            ++a.n;
        } else if (op == QLatin1String("remove_stop")) {
            json::Array &stops = stopsOf(plan);
            const int i = idxById(stops, ch.value(L("stop")).toString());
            if (i < 0) continue;
            if (i > 0) clearLeg(stops[i - 1], a.needRoute);
            stops.erase(stops.begin() + i);
            unassignPhotos(root, pid, ch.value(L("stop")).toString());
            ++a.n;
        } else if (op == QLatin1String("move_stop")) {
            json::Array &stops = stopsOf(plan);
            const int i = idxById(stops, ch.value(L("stop")).toString());
            if (i < 0) continue;
            if (i > 0) clearLeg(stops[i - 1], a.needRoute);
            clearLeg(stops[i], a.needRoute);
            Value s = stops[i];
            stops.erase(stops.begin() + i);
            const QString af = ch.value(L("after")).toString();
            int at = af == QLatin1String("start") ? 0 : !af.isEmpty() ? idxById(stops, af) + 1 : int(stops.size());
            at = std::clamp(at, 0, int(stops.size()));
            if (at > 0) clearLeg(stops[at - 1], a.needRoute);
            stops.insert(stops.begin() + at, s);
            ++a.n;
        } else if (op == QLatin1String("add_schedule")) {
            json::Array &stops = stopsOf(plan);
            const int i = idxById(stops, ch.value(L("stop")).toString());
            if (i < 0 || cs(c, "text").isEmpty()) continue;
            json::Object it;
            it.set(L("date"), Value(okDate(cs(c, "date")) ? cs(c, "date") : QString()));
            if (!cs(c, "time").isEmpty()) it.set(L("time"), Value(cs(c, "time")));
            if (!cs(c, "time").isEmpty() && !cs(c, "end_time").isEmpty()) it.set(L("end"), Value(cs(c, "end_time")));
            it.set(L("text"), Value(cs(c, "text")));
            listOf(stops[i], L("schedule")).push_back(Value(std::move(it)));
            ++a.n;
        } else if (op == QLatin1String("update_schedule") || op == QLatin1String("remove_schedule")) {
            json::Array &stops = stopsOf(plan);
            int si = -1, ii = -1;
            if (!findMarked(stops, k, &si, &ii)) continue;
            json::Array &sch = stops[si].mutObject().find(L("schedule"))->mutArray();
            if (op == QLatin1String("remove_schedule")) { sch.erase(sch.begin() + ii); ++a.n; continue; }
            json::Object &it = sch[ii].mutObject();
            if (c.value(L("date")).isString() && (okDate(cs(c, "date")) || cs(c, "date").isEmpty())) it.set(L("date"), Value(cs(c, "date")));
            if (!cs(c, "time").isEmpty()) it.set(L("time"), Value(cs(c, "time")));
            if (!cs(c, "end_time").isEmpty()) it.set(L("end"), Value(cs(c, "end_time")));
            if (!cs(c, "text").isEmpty()) it.set(L("text"), Value(cs(c, "text")));
            const int to = idxById(stops, ch.value(L("toStop")).toString());
            if (to >= 0 && to != si) {
                Value moved = sch[ii];
                sch.erase(sch.begin() + ii);
                listOf(stops[to], L("schedule")).push_back(moved);
            }
            ++a.n;
        } else if (op == QLatin1String("add_booking") || op == QLatin1String("update_booking")) {
            json::Array &bs = listOf(plan, L("bookings"));
            int i = op == QLatin1String("add_booking") ? -1 : idxById(bs, ch.value(L("obj")).toString());
            if (op == QLatin1String("update_booking") && i < 0) continue;
            if (i < 0) {
                json::Object b;
                b.set(L("id"), Value(newId(L("bk"))));
                b.set(L("kind"), Value(cs(c, "kind").isEmpty() ? L("other") : cs(c, "kind")));
                bs.push_back(Value(std::move(b)));
                i = int(bs.size()) - 1;
            } else if (!cs(c, "kind").isEmpty()) bs[i].mutObject().set(L("kind"), Value(cs(c, "kind")));
            for (const char *f : { "title", "from", "to", "start", "end", "ref", "notes" })
                if (!cs(c, f).isEmpty()) bs[i].mutObject().set(L(f), Value(cs(c, f)));
            if (hasNum(c, "cost")) bs[i].mutObject().set(L("cost"), Value(c.value(L("cost")).toDouble()));
            ++a.n;
        } else if (op == QLatin1String("add_todo") || op == QLatin1String("add_packing")) {
            if (cs(c, "text").isEmpty()) continue;
            const bool todo = op == QLatin1String("add_todo");
            json::Object t;
            t.set(L("id"), Value(newId(todo ? L("td") : L("pk"))));
            t.set(L("text"), Value(cs(c, "text")));
            t.set(L("done"), Value(c.value(L("done")).toBool()));
            if (todo && okDate(cs(c, "date"))) t.set(L("due"), Value(cs(c, "date")));
            listOf(plan, todo ? L("todos") : L("packing")).push_back(Value(std::move(t)));
            ++a.n;
        } else if (op == QLatin1String("update_todo") || op == QLatin1String("update_packing")) {
            json::Array &arr = listOf(plan, op == QLatin1String("update_todo") ? L("todos") : L("packing"));
            const int i = idxById(arr, ch.value(L("obj")).toString());
            if (i < 0) continue;
            json::Object &o = arr[i].mutObject();
            if (!cs(c, "text").isEmpty()) o.set(L("text"), Value(cs(c, "text")));
            if (c.value(L("done")).isBool()) o.set(L("done"), Value(c.value(L("done")).toBool()));
            if (op == QLatin1String("update_todo") && okDate(cs(c, "date"))) o.set(L("due"), Value(cs(c, "date")));
            ++a.n;
        } else if (op == QLatin1String("remove_booking") || op == QLatin1String("remove_todo") || op == QLatin1String("remove_packing")) {
            json::Array &arr = listOf(plan, op == QLatin1String("remove_booking") ? L("bookings") : op == QLatin1String("remove_todo") ? L("todos") : L("packing"));
            const int i = idxById(arr, ch.value(L("obj")).toString());
            if (i < 0) continue;
            arr.erase(arr.begin() + i);
            ++a.n;
        }
    }
    // 3. 删除计划放最后（照片移到"未归类"，文件不动；下次出发是它就取消）
    for (const QJsonObject &ch : list) {
        if (cs(ch.value(L("c")).toObject(), "op") != QLatin1String("delete_plan")) continue;
        const QString id = ch.value(L("key")).toString();
        if (!pe::removePlan(root, id)) continue;
        unassignPhotos(root, id, QString());
        if (root[L("nextTripId")].toString() == id) root.mutObject().remove(L("nextTripId"));
        a.deleted << id;
        ++a.n;
    }
    // 去掉记号
    if (Value *ps = root.mutObject().find(L("plans")); ps && ps->isArray())
        for (Value &p : ps->mutArray()) {
            bool any = false;
            for (const Value &s : p[L("stops")].asArray()) for (const Value &it : s[L("schedule")].asArray()) any = any || it[L(kMark)].isNumber();
            if (!any) continue;
            for (Value &s : stopsOf(p)) if (Value *sch = s.mutObject().find(L("schedule")); sch && sch->isArray())
                for (Value &it : sch->mutArray()) if (it.isObject()) it.mutObject().remove(L(kMark));
        }
    return a;
}

QString resultText(const QJsonObject &P, const Applied &a)
{
    QStringList skipped;
    for (const auto &v : P.value(L("changes")).toArray()) {
        const QJsonObject ch = v.toObject();
        if (ch.value(L("on")).toBool() && !ch.contains(L("bad"))) continue;
        const QJsonObject c = ch.value(L("c")).toObject();
        skipped << opZh(cs(c, "op")) + (cs(c, "name").isEmpty() ? QString() : u' ' + cs(c, "name"));
    }
    QString t = QStringLiteral("用户应用了 %1 项修改").arg(a.n);
    if (!skipped.isEmpty()) t += QStringLiteral("，没勾选 / 不能应用的有 %1 项：%2").arg(skipped.size()).arg(skipped.join(QStringLiteral("、")));
    t += QStringLiteral("。");
    if (!a.created.isEmpty()) {
        QStringList m;
        for (auto it = a.created.begin(); it != a.created.end(); ++it) m << it.key() + QStringLiteral(" = ") + it.value().toString();
        t += QStringLiteral(" 新建计划的 id：%1。").arg(m.join(QStringLiteral("，")));
    }
    return t;
}

QString uid(const QString &prefix)
{
    auto b36 = [](quint64 v) { return QString::number(v, 36); };
    QString r;
    for (int i = 0; i < 5; ++i) r += b36(QRandomGenerator::global()->bounded(36));
    return prefix + u'_' + b36(quint64(QDateTime::currentMSecsSinceEpoch())) + r;
}

} // namespace tp::ai::tools
