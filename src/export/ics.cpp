#include "ics.h"

#include "plans.h"
#include "translator.h"

#include <QDate>
#include <QRegularExpression>
#include <QTimeZone>
#include <QStringList>
#include <utility>
#include <vector>

namespace tp::exporter {

using json::Value;
namespace plans = tp::map::plans;

namespace {

QString str(const Value &o, const char *k) { return o[QString::fromLatin1(k)].toString(); }

QString ymd(const QString &s) { return s.left(10).remove(QLatin1Char('-')); }
// 2026-12-05T15:00 → 20261205T150000；认不出返回空
QString dt(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2})[T ](\\d{2}):(\\d{2})"));
    const auto m = re.match(s);
    return m.hasMatch() ? m.captured(1) + m.captured(2) + m.captured(3) + QLatin1Char('T') + m.captured(4) + m.captured(5) + QStringLiteral("00") : QString();
}
QString addDay(const QString &s, int k) { return QDate::fromString(s.left(10), QStringLiteral("yyyy-MM-dd")).addDays(k).toString(QStringLiteral("yyyy-MM-dd")); }
// 加一小时，返回 yyyy-MM-ddTHH:mm（跨过半夜时日期也跟着变）
QString plusHour(const QString &s)
{
    QDateTime t = QDateTime::fromString(s.left(16).replace(QLatin1Char(' '), QLatin1Char('T')), QStringLiteral("yyyy-MM-ddTHH:mm"));
    t.setTimeZone(QTimeZone::UTC);
    return t.isValid() ? t.addSecs(3600).toString(QStringLiteral("yyyy-MM-ddTHH:mm")) : QString();
}
QString kindName(const QString &k)
{
    if (k == QLatin1String("flight")) return tp::tr("机票");
    if (k == QLatin1String("train")) return tp::tr("火车");
    if (k == QLatin1String("bus")) return tp::tr("大巴");
    if (k == QLatin1String("ferry")) return tp::tr("船票");
    if (k == QLatin1String("car")) return tp::tr("租车");
    if (k == QLatin1String("hotel")) return tp::tr("住宿");
    if (k == QLatin1String("ticket")) return tp::tr("门票");
    return {};
}

using Fields = std::vector<std::pair<QString, QString>>;

} // namespace

QString icsEscape(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char(';'), QStringLiteral("\\;")).replace(QLatin1Char(','), QStringLiteral("\\,"));
    out.replace(QStringLiteral("\r\n"), QStringLiteral("\\n")).replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return out;
}

QString icsFold(const QString &line)
{
    if (line.toUtf8().size() <= 75) return line;
    QStringList out;
    QString cur;
    int n = 0;
    for (int i = 0; i < line.size(); ++i) {
        const bool pair = line[i].isHighSurrogate() && i + 1 < line.size() && line[i + 1].isLowSurrogate();
        const QString ch = line.mid(i, pair ? 2 : 1);
        if (pair) ++i;
        const int b = int(ch.toUtf8().size());
        if (n + b > (out.isEmpty() ? 75 : 74)) { out << cur; cur.clear(); n = 0; }
        cur += ch;
        n += b;
    }
    out << cur;
    return out.join(QStringLiteral("\r\n "));
}

IcsResult ics(const Value &plan, const QDateTime &nowUtc)
{
    const QString now = nowUtc.toUTC().toString(QStringLiteral("yyyyMMddTHHmmss")) + QLatin1Char('Z');
    const QString name = str(plan, "name");
    QStringList L{ QStringLiteral("BEGIN:VCALENDAR"), QStringLiteral("VERSION:2.0"), QStringLiteral("PRODID:-//TravelPlanner//Travel Planner//ZH"),
                   QStringLiteral("CALSCALE:GREGORIAN"), QStringLiteral("METHOD:PUBLISH"),
                   QStringLiteral("X-WR-CALNAME:") + icsEscape(name.isEmpty() ? tp::tr("旅行计划") : name) };
    int n = 0;
    const auto ev = [&](const QString &uid, const Fields &f) {
        L << QStringLiteral("BEGIN:VEVENT") << QStringLiteral("UID:") + uid + QStringLiteral("@travelplanner") << QStringLiteral("DTSTAMP:") + now;
        for (const auto &[k, v] : f) {
            if (v.isEmpty()) continue;
            L << k + QLatin1Char(':') + (k.contains(QLatin1Char(';')) || k.startsWith(QLatin1String("DT")) ? v : icsEscape(v));
        }
        L << QStringLiteral("END:VEVENT");
        ++n;
    };
    const auto allDay = [](const QString &a, const QString &b) {
        return Fields{ { QStringLiteral("DTSTART;VALUE=DATE"), ymd(a) }, { QStringLiteral("DTEND;VALUE=DATE"), ymd(addDay(b.isEmpty() ? a : b, 1)) } };
    };
    const auto plus = [](Fields f, const Fields &more) { f.insert(f.end(), more.begin(), more.end()); return f; };

    const QString start = str(plan, "startDate"), end = str(plan, "endDate");
    if (!start.isEmpty())
        ev(str(plan, "id"), plus(allDay(start, end.isEmpty() ? start : end),
                                 { { QStringLiteral("SUMMARY"), name.isEmpty() ? tp::tr("旅行") : name }, { QStringLiteral("DESCRIPTION"), str(plan, "notes") } }));
    for (const Value &b : plan[QStringLiteral("bookings")].asArray()) {
        if (!b.isObject()) continue;
        const QString bs = str(b, "start"), be = str(b, "end"), kind = str(b, "kind");
        if (bs.isEmpty()) continue;
        const QString kn = kindName(kind);
        const QString title = kn.isEmpty() ? str(b, "title") : tp::tr("%1：%2").arg(kn, str(b, "title"));
        QStringList d;
        if (!str(b, "ref").isEmpty()) d << tp::tr("订单号：") + str(b, "ref");
        if (!str(b, "notes").isEmpty()) d << str(b, "notes");
        QStringList ft;
        for (const char *k : { "from", "to" }) if (!str(b, k).isEmpty()) ft << str(b, k);
        const QString loc = kind == QLatin1String("hotel") ? str(b, "title") : ft.join(QStringLiteral(" → "));
        const Fields rest{ { QStringLiteral("SUMMARY"), title }, { QStringLiteral("LOCATION"), loc }, { QStringLiteral("DESCRIPTION"), d.join(QLatin1Char('\n')) } };
        if (kind == QLatin1String("hotel")) ev(str(b, "id"), plus(allDay(bs, be.isEmpty() ? bs : addDay(be, -1)), rest));
        else if (!dt(bs).isEmpty()) ev(str(b, "id"), plus({ { QStringLiteral("DTSTART"), dt(bs) }, { QStringLiteral("DTEND"), dt(!be.isEmpty() && be > bs ? be : plusHour(bs)) } }, rest));
        else ev(str(b, "id"), plus(allDay(bs, {}), rest));
    }
    const QVector<int> stops = plans::validStops(plan);
    const json::Array &arr = plan[QStringLiteral("stops")].asArray();
    for (int i = 0; i < stops.size(); ++i) {
        const Value &s = arr[size_t(stops[i])];
        const json::Array &sched = s[QStringLiteral("schedule")].asArray();
        for (int j = 0; j < int(sched.size()); ++j) {
            const Value &x = sched[size_t(j)];
            if (!x.isObject() || str(x, "date").isEmpty()) continue;
            const QString date = str(x, "date"), time = str(x, "time"), xe = str(x, "end");
            const QString text = str(x, "text").trimmed();
            QString first = text.section(QLatin1Char('\n'), 0, 0).left(60);
            if (first.isEmpty()) first = str(s, "name");
            const Fields base{ { QStringLiteral("SUMMARY"), first }, { QStringLiteral("LOCATION"), str(s, "name") }, { QStringLiteral("DESCRIPTION"), text } };
            const QString sid = str(s, "id").isEmpty() ? QString::number(i) : str(s, "id");
            const QString uid = str(plan, "id") + QLatin1Char('-') + sid + QLatin1Char('-') + QString::number(j);
            if (!time.isEmpty()) {
                const QString st = date + QLatin1Char('T') + time;
                // Electron 只取 +1 小时后的时刻、日期不变（23:30 开始会写成当天 00:30 结束）；这里跨过半夜时日期跟着变
                ev(uid, plus({ { QStringLiteral("DTSTART"), dt(st) }, { QStringLiteral("DTEND"), dt(!xe.isEmpty() && xe > time ? date + QLatin1Char('T') + xe : plusHour(st)) } }, base));
            } else {
                ev(uid, plus(allDay(date, {}), base));
            }
        }
    }
    L << QStringLiteral("END:VCALENDAR");
    QStringList folded;
    for (const QString &l : std::as_const(L)) folded << icsFold(l);
    return { (folded.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n")).toUtf8(), n };
}

} // namespace tp::exporter
