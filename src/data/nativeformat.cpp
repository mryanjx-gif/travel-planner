#include "nativeformat.h"

#include "jsonio.h"

#include <cmath>

namespace tp::data::nativefmt {

using json::Value;

QString isoWithOffset(const QDateTime &t)
{
    const QDateTime local = t.toLocalTime();
    return local.toOffsetFromUtc(local.offsetFromUtc()).toString(Qt::ISODate);
}

Value emptyDoc(const QString &appVersion, const QDateTime &now)
{
    json::Object o;
    o.set(QStringLiteral("format"), Value(kFormatName));
    o.set(QStringLiteral("formatVersion"), Value(kFormatVersion));
    o.set(QStringLiteral("savedBy"), Value(appVersion));
    o.set(QStringLiteral("savedAt"), Value(isoWithOffset(now)));
    o.set(QStringLiteral("plans"), Value::array());
    o.set(QStringLiteral("photos"), Value::array());
    o.set(QStringLiteral("journal"), Value::array());
    o.set(QStringLiteral("documents"), Value::array());
    o.set(QStringLiteral("visitedRegions"), Value::array());
    return Value(std::move(o));
}

Check check(const Value &doc, int *formatVersion)
{
    if (formatVersion) *formatVersion = 0;
    if (!doc.isObject() || doc[QStringLiteral("format")].toString() != QLatin1String(kFormatName)) return Check::NotOurs;
    const Value &v = doc[QStringLiteral("formatVersion")];
    const double n = v.isNumber() ? v.toNumber() : 0;
    const int ver = std::isfinite(n) ? int(n) : 0;
    if (formatVersion) *formatVersion = ver;
    return ver > kFormatVersion ? Check::Newer : Check::Ok;
}

void normalize(Value &doc)
{
    json::Object &o = doc.mutObject();
    for (const char *k : { "plans", "photos", "journal" }) {
        const QString key = QString::fromLatin1(k);
        const Value *v = o.find(key);
        if (!v || !v->isArray()) o.set(key, Value::array());
    }
}

void stamp(Value &doc, const QString &appVersion, const QDateTime &now)
{
    json::Object &o = doc.mutObject();
    o.set(QStringLiteral("savedBy"), Value(appVersion));
    o.set(QStringLiteral("savedAt"), Value(isoWithOffset(now)));
}

QByteArray serialize(const Value &doc)
{
    return json::stringify(doc, 2).toUtf8() + '\n';
}

} // namespace tp::data::nativefmt
