#include "calendarcontroller.h"

#include "appcontroller.h"
#include "datastore.h"
#include "mapcontroller.h"
#include "plancal.h"
#include "plancontroller.h"
#include "plans.h"
#include "settings.h"

#include <QDate>

namespace tp::app {

using json::Value;
namespace plans = map::plans;

namespace {
const QString kKey = QStringLiteral("calendarView");
const QString kWeekStart = QStringLiteral("weekStart");
QString str(const Value &o, const char *key) { return o[QString::fromLatin1(key)].toString(); }
}

QDate CalendarController::weekOf(const QDate &d) const { return QDate::fromString(plans::weekStartOf(d.toString(Qt::ISODate), m_weekStart), Qt::ISODate); }
QString CalendarController::weekFirst() const { return m_ws.toString(Qt::ISODate); }

CalendarController::CalendarController(AppController *app, PlanController *pc, Settings *settings, data::DataStore *store, map::MapController *mapc, QObject *parent)
    : QObject(parent), m_app(app), m_plans(pc), m_settings(settings), m_store(store), m_map(mapc)
{
    const QDate now = QDate::currentDate();
    m_y = now.year(); m_m = now.month();
    load(*settings);
    m_ws = weekOf(now);
    connect(mapc, &map::MapController::dataChanged, this, &CalendarController::refresh);   // 去过的计算看地图那边的数据（比 contentChanged 晚一点）
    connect(mapc, &map::MapController::visitedCodesChanged, this, &CalendarController::refresh);
    connect(store, &data::DataStore::contentChanged, this, &CalendarController::refresh);
    connect(app, &AppController::dataChanged, this, &CalendarController::refresh);
    connect(pc, &PlanController::nowChanged, this, [this] { refresh(); });
}

void CalendarController::load(const Settings &s)
{
    const Value &v = s.value(kKey);
    const QString view = v[QStringLiteral("view")].toString();
    m_view = view == QLatin1String("month") || view == QLatin1String("week") || view == QLatin1String("season") ? view : QStringLiteral("year");
    const Value &side = v[QStringLiteral("side")];
    m_side = side.isBool() ? side.toBool() : true;
    m_hidden.clear();
    for (const Value &h : v[QStringLiteral("hidden")].asArray()) m_hidden.insert(h.toString());
    m_seasonClosed.clear();
    for (const Value &h : v[QStringLiteral("seasonClosed")].asArray()) m_seasonClosed.insert(h.toString());
    const Value &ws = s.value(kWeekStart);
    m_weekStart = ws.isNumber() && ws.toNumber() == 0 ? 0 : 1;
}

void CalendarController::save()
{
    if (!m_persist) return;
    json::Object o;
    o.set(QStringLiteral("view"), Value(m_view));
    o.set(QStringLiteral("side"), Value(m_side));
    json::Array hid;
    QStringList sorted(m_hidden.begin(), m_hidden.end());
    sorted.sort();
    for (const QString &h : sorted) hid.push_back(Value(h));
    o.set(QStringLiteral("hidden"), Value(std::move(hid)));
    json::Array closed;
    QStringList closedSorted(m_seasonClosed.begin(), m_seasonClosed.end());
    closedSorted.sort();
    for (const QString &g : closedSorted) closed.push_back(Value(g));
    o.set(QStringLiteral("seasonClosed"), Value(std::move(closed)));
    m_settings->setValue(kKey, Value(std::move(o)));
    m_settings->setValue(kWeekStart, Value(double(m_weekStart)));
    m_settings->save();
}

void CalendarController::setPersist(bool on)
{
    m_persist = on;
    if (on) return;
    m_view = QStringLiteral("year");
    m_side = true;
    m_hidden.clear();
    m_seasonClosed.clear();
    m_weekStart = 1;
    m_ws = weekOf(QDate::currentDate());
}

void CalendarController::reopenForTest(const QString &settingsFile)
{
    const bool persist = m_persist;
    Settings *const real = m_settings;
    Settings s;
    s.load(settingsFile);
    m_persist = true;
    m_settings = &s;
    save();
    m_view = QStringLiteral("year");
    m_side = true;
    m_hidden.clear();
    m_seasonClosed.clear();
    m_weekStart = 1;
    Settings back;
    back.load(settingsFile);
    load(back);
    m_settings = real;
    m_persist = persist;
    emit stateChanged();
    refresh();
}

void CalendarController::setActive(bool on)
{
    if (on == m_active) return;
    m_active = on;
    emit stateChanged();
    refresh();
}

void CalendarController::setView(const QString &v)
{
    if (v == m_view || (v != QLatin1String("year") && v != QLatin1String("month") && v != QLatin1String("week") && v != QLatin1String("season"))) return;
    // 换到周视图：看 y 年 m 月的第一周；如果是这个月，就看这一周（Electron goTo）
    if (v == QLatin1String("week")) {
        const QDate t = QDate::fromString(m_plans->today(), Qt::ISODate);
        m_ws = weekOf(t.isValid() && t.year() == m_y && t.month() == m_m ? t : QDate(m_y, m_m, 1));
    }
    m_view = v;
    save();
    emit stateChanged();
    refresh();
}

void CalendarController::setSideOpen(bool on)
{
    if (on == m_side) return;
    m_side = on;
    save();
    emit stateChanged();
}

void CalendarController::setWeekStart(int v)
{
    v = v == 0 ? 0 : 1;
    if (v == m_weekStart) return;
    m_weekStart = v;
    m_ws = weekOf(m_ws);   // 对齐到新的周首
    save();
    emit stateChanged();
    refresh();
}

void CalendarController::setSeasonGroupClosed(const QString &group, bool closed)
{
    if (closed == m_seasonClosed.contains(group)) return;
    if (closed) m_seasonClosed.insert(group);
    else m_seasonClosed.remove(group);
    save();
    refresh();
}

void CalendarController::setAllSeasonGroups(bool closed)
{
    m_seasonClosed.clear();
    if (closed) {
        const QStringList all = plans::calSeason(m_store->data(), m_y, nullptr, {}).value(QStringLiteral("groups")).toStringList();
        m_seasonClosed = QSet<QString>(all.begin(), all.end());
    }
    save();
    refresh();
}

void CalendarController::showWeek(const QString &date)
{
    const QDate d = QDate::fromString(date, Qt::ISODate);
    if (!d.isValid()) return;
    m_ws = weekOf(d);
    m_y = m_ws.year(); m_m = m_ws.month();
    m_view = QStringLiteral("week");
    save();
    emit stateChanged();
    refresh();
}

void CalendarController::goTo(int y, int m)
{
    const QDate d(y, m, 1);
    if (!d.isValid()) return;
    if (m_view == QLatin1String("week")) {   // 选的是这个月 → 这一周，否则这个月第一周
        const QDate t = QDate::fromString(m_plans->today(), Qt::ISODate);
        m_ws = weekOf(t.isValid() && t.year() == y && t.month() == m ? t : d);
    } else if (d.year() == m_y && d.month() == m_m) return;
    m_y = d.year(); m_m = d.month();
    emit stateChanged();
    refresh();
}

void CalendarController::step(int dir)
{
    if (m_view == QLatin1String("week")) {
        m_ws = m_ws.addDays(7 * dir);
        m_y = m_ws.year(); m_m = m_ws.month();
        emit stateChanged();
        refresh();
        return;
    }
    const QDate d = QDate(m_y, m_m, 1).addMonths((m_view == QLatin1String("year") ? 6 : 1) * dir);
    goTo(d.year(), d.month());
}

void CalendarController::goToday()
{
    const QDate d = QDate::fromString(m_plans->today(), Qt::ISODate);
    const QDate now = d.isValid() ? d : QDate::currentDate();
    goTo(now.year(), now.month());
}

void CalendarController::showDate(const QString &date)
{
    const QDate d = QDate::fromString(date, Qt::ISODate);
    if (!d.isValid()) return;
    m_ws = weekOf(d);
    if (m_view == QLatin1String("week")) { showWeek(date); return; }
    goTo(d.year(), d.month());
    setView(QStringLiteral("month"));
}

QVariantMap CalendarController::picker() const { return plans::calPicker(m_store->data(), m_hidden); }

void CalendarController::refresh()
{
    if (!m_active) return;
    const Value &d = m_store->data();
    QVariantMap m{ { QStringLiteral("side"), plans::calSide(d, m_hidden) } };
    if (m_view == QLatin1String("month")) m.insert(QStringLiteral("month"), plans::calMonth(d, m_hidden, m_y, m_m, m_plans->today(), m_weekStart));
    else if (m_view == QLatin1String("week")) m.insert(QStringLiteral("week"), plans::calWeek(d, m_hidden, weekFirst(), m_plans->today()));
    else if (m_view == QLatin1String("season")) {
        const QDate t = QDate::fromString(m_plans->today(), Qt::ISODate);
        m.insert(QStringLiteral("season"), plans::calSeason(d, t.isValid() ? t.year() : QDate::currentDate().year(), m_map->visitedCodes(), m_seasonClosed));
    }
    else m.insert(QStringLiteral("year"), plans::calYear(d, m_hidden, m_y, m_m, m_plans->today()));
    if (m == m_info) return;
    m_info = m;
    emit infoChanged();
}

void CalendarController::setHidden(const QString &planId, bool hidden)
{
    if (hidden == m_hidden.contains(planId)) return;
    if (hidden) m_hidden.insert(planId);
    else m_hidden.remove(planId);
    save();
    refresh();
}

bool CalendarController::apply(const QString &planId, const std::function<void(Value &)> &fn, const char *what)
{
    if (m_store->isReadOnly()) return false;
    // 先在副本上试：没有变化就不碰数据（不标记未保存、不记撤销）
    Value copy = m_store->data();
    Value *p = plans::mutPlan(copy, planId);
    if (!p) return false;
    fn(*p);
    if (copy == m_store->data()) return false;
    m_store->modify([&](Value &d) { d = copy; });
    qInfo("日历：%s", what);
    m_app->noteChanged(true);   // 马上保存，撤销是一步
    return true;
}

QVariantMap CalendarController::result(const QString &planId, bool moved) const
{
    const Value *p = plans::planById(m_store->data(), planId);
    if (!p) return { { QStringLiteral("ok"), false } };
    const QString start = str(*p, "startDate"), end = str(*p, "endDate");
    return { { QStringLiteral("ok"), true }, { QStringLiteral("id"), planId }, { QStringLiteral("name"), str(*p, "name") }, { QStringLiteral("start"), start }, { QStringLiteral("end"), end },
             { QStringLiteral("moved"), moved }, { QStringLiteral("days"), plans::planDays(*p) }, { QStringLiteral("bookings"), moved ? plans::datedBookings(*p) : 0 },
             { QStringLiteral("overlaps"), moved ? plans::overlapCount(m_store->data(), planId, start, end, m_hidden) : 0 } };
}

QVariantMap CalendarController::dropRange(const QString &planId, const QString &day, const QString &grab) const
{
    const Value *p = plans::planById(m_store->data(), planId);
    if (!p || !plans::okDate(day)) return {};
    const QString s = str(*p, "startDate"), e = str(*p, "endDate");
    const QString a = s.isEmpty() ? e : s, b = e.isEmpty() ? s : e;
    QString start, end;
    if (!plans::okDate(a)) {   // 还没定日期
        start = day;
        end = plans::addDays(day, plans::planDays(*p) - 1);
    } else {
        const int n = plans::days(plans::okDate(grab) ? grab : a, day);
        start = plans::addDays(a, n);
        end = plans::addDays(b, n);
    }
    return { { QStringLiteral("start"), start }, { QStringLiteral("end"), end }, { QStringLiteral("same"), start == a },
             { QStringLiteral("overlaps"), plans::overlapCount(m_store->data(), planId, start, end, m_hidden) } };
}

QVariantMap CalendarController::drop(const QString &planId, const QString &day, const QString &grab)
{
    const QVariantMap r = dropRange(planId, day, grab);
    const QString start = r.value(QStringLiteral("start")).toString();
    if (start.isEmpty() || r.value(QStringLiteral("same")).toBool()) return { { QStringLiteral("ok"), false } };
    const Value *p0 = plans::planById(m_store->data(), planId);
    const bool placed = str(*p0, "startDate").isEmpty() && str(*p0, "endDate").isEmpty();
    bool toPlanning = false;
    const bool done = apply(planId, [&](Value &p) {
        plans::placePlan(p, start, plans::planDays(p));
        if (placed && plans::statusOf(p) == QLatin1String("wish")) {   // 想去的计划定了日期 = 开始计划（C-10）
            p.mutObject().set(QStringLiteral("status"), Value(QStringLiteral("planning")));
            toPlanning = true;
        }
    }, placed ? "定日期" : "挪日期");
    if (!done) return { { QStringLiteral("ok"), false } };
    QVariantMap out = result(planId, true);
    out.insert(QStringLiteral("placed"), placed);
    out.insert(QStringLiteral("toPlanning"), toPlanning);
    return out;
}

QVariantMap CalendarController::removeDates(const QString &planId)
{
    if (!apply(planId, [](Value &p) { plans::unplacePlan(p); }, "去掉日期")) return { { QStringLiteral("ok"), false } };
    return result(planId, false);
}

QVariantMap CalendarController::savePlan(const QString &planId, const QVariantMap &f)
{
    const Value *p0 = plans::planById(m_store->data(), planId);
    if (!p0) return { { QStringLiteral("ok"), false } };
    const QString name = f.value(QStringLiteral("name")).toString().trimmed(), start = f.value(QStringLiteral("start")).toString(),
                  status = f.value(QStringLiteral("status")).toString(), color = f.value(QStringLiteral("color")).toString();
    const int n = qRound(f.value(QStringLiteral("days")).toString().toDouble());
    if (name.isEmpty()) return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("name") } };
    if (!start.isEmpty() && (n < 1 || n > 999)) return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("days") } };
    if (!start.isEmpty() && !plans::okDate(start)) return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("date") } };
    const QString oldStart = str(*p0, "startDate"), oldEnd = str(*p0, "endDate");
    apply(planId, [&](Value &p) {
        json::Object &o = p.mutObject();
        o.set(QStringLiteral("name"), Value(name));
        if (!start.isEmpty()) plans::placePlan(p, start, n);
        else { o.set(QStringLiteral("startDate"), Value(QString())); o.set(QStringLiteral("endDate"), Value(QString())); }
        if (status == QLatin1String("planning") || status == QLatin1String("wish") || status == QLatin1String("completed")) o.set(QStringLiteral("status"), Value(status));
        if (!color.isEmpty()) o.set(QStringLiteral("color"), Value(color));
    }, "弹出卡保存");
    const Value *p1 = plans::planById(m_store->data(), planId);
    return result(planId, p1 && !oldStart.isEmpty() && !start.isEmpty() && (str(*p1, "startDate") != oldStart || str(*p1, "endDate") != oldEnd));
}

QVariantMap CalendarController::planFields(const QString &planId) const
{
    const Value *p = plans::planById(m_store->data(), planId);
    if (!p) return {};
    const QString s = str(*p, "startDate");
    return { { QStringLiteral("id"), planId }, { QStringLiteral("name"), str(*p, "name") }, { QStringLiteral("start"), s.isEmpty() ? str(*p, "endDate") : s },
             { QStringLiteral("days"), plans::planDays(*p) }, { QStringLiteral("status"), plans::statusOf(*p) }, { QStringLiteral("color"), plans::planColorOf(*p) } };
}

} // namespace tp::app
