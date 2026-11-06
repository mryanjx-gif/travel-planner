#include "settings.h"
#include "perf.h"

#include "atomicfile.h"
#include "jsonio.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace tp::app {

using json::Value;

namespace {
const char *const kSizes[] = { "s", "m", "l", "xl" };
const double kScales[] = { 0.9, 1.0, 1.12, 1.25 };
}

bool Settings::load(const QString &path)
{
    m_path = path;
    m_root = Value::object();
    m_broken.clear();
    QFile f(path);
    if (!f.exists()) return true;
    if (!f.open(QIODevice::ReadOnly)) { m_error = f.errorString(); return false; }
    QByteArray bytes = f.readAll();
    f.close();
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    json::ParseError pe;
    int consumed = 0;
    const QString text = QString::fromUtf8(bytes);
    Value v = json::parse(text, &pe, &consumed);
    if (pe.ok && v.isObject() && QStringView(text).mid(consumed).trimmed().isEmpty()) {
        m_root = v;
        return true;
    }
    // 坏了：改名留着，用默认设置
    const QFileInfo fi(path);
    m_broken = QStringLiteral("settings.broken-%1.json").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QFile::rename(path, fi.dir().filePath(m_broken));
    m_error = pe.ok ? QStringLiteral("内容不是一个对象") : pe.message;
    return false;
}

bool Settings::save()
{
    if (m_path.isEmpty()) { m_error = QStringLiteral("没有设置文件路径"); return false; }
    perf::Scope timing("写设置", 0);   // 0.9.1：换模式 / 换底图时同步写 settings.json，量一下多久
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    const QByteArray bytes = json::stringify(m_root, 2).toUtf8() + '\n';
    const auto r = platform::writeViaTemp(m_path, m_path + QStringLiteral(".tmp"), bytes);
    if (!r.ok) m_error = r.error;
    return r.ok;
}

Value Settings::value(const QString &key) const { return m_root[key]; }

void Settings::setValue(const QString &key, Value v)
{
    if (m_root[key] == v && m_root.asObject().contains(key)) return;
    m_root.mutObject().set(key, std::move(v));
    emit changed(key);
}

QString Settings::str(const QString &key) const { return m_root[key].toString(); }

QString Settings::dataDir() const { return str(QStringLiteral("dataDir")); }
void Settings::setDataDir(const QString &dir) { setValue(QStringLiteral("dataDir"), QDir::toNativeSeparators(dir)); }

QString Settings::closeAction() const
{
    const QString v = str(QStringLiteral("closeAction"));
    return (v == QLatin1String("tray") || v == QLatin1String("taskbar")) ? v : QStringLiteral("quit");
}
void Settings::setCloseAction(const QString &v)
{
    if (v == QLatin1String("quit") || v == QLatin1String("tray") || v == QLatin1String("taskbar")) setValue(QStringLiteral("closeAction"), v);
}

QString Settings::lang() const
{
    if (!m_langOverride.isEmpty()) return m_langOverride;
    return str(QStringLiteral("lang")) == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("zh");
}
void Settings::setLang(const QString &v) { m_langOverride.clear(); setValue(QStringLiteral("lang"), v == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("zh")); }

QString Settings::fontSize() const
{
    const QString v = str(QStringLiteral("fontSize"));
    for (const char *s : kSizes) if (v == QLatin1String(s)) return v;
    return QStringLiteral("m");
}
void Settings::setFontSize(const QString &v)
{
    for (const char *s : kSizes) if (v == QLatin1String(s)) { setValue(QStringLiteral("fontSize"), v); return; }
}
double Settings::fontScaleFor(const QString &size)
{
    for (int i = 0; i < 4; ++i) if (size == QLatin1String(kSizes[i])) return kScales[i];
    return 1.0;
}
QString Settings::stepFontSize(const QString &size, int dir)
{
    int i = 1;
    for (int k = 0; k < 4; ++k) if (size == QLatin1String(kSizes[k])) i = k;
    i = std::clamp(i + (dir > 0 ? 1 : -1), 0, 3);
    return QString::fromLatin1(kSizes[i]);
}

QString Settings::windowTitle() const { return str(QStringLiteral("windowTitle")); }
void Settings::setWindowTitle(const QString &v) { setValue(QStringLiteral("windowTitle"), v.trimmed().left(60)); }

QString Settings::backupDir() const { return str(QStringLiteral("backupDir")); }
void Settings::setBackupDir(const QString &dir) { setValue(QStringLiteral("backupDir"), QDir::toNativeSeparators(dir)); }
bool Settings::autoBackup() const { return m_root[QStringLiteral("autoBackup")].toBool(); }
bool Settings::spaceAnim() const
{
    const json::Value &v = m_root[QStringLiteral("spaceAnim")];
    return v.isBool() ? v.toBool() : true;   // 没写过 = 开（Electron prefs.js 默认 true）
}
void Settings::setSpaceAnim(bool on) { setValue(QStringLiteral("spaceAnim"), Value(on)); }
void Settings::setAutoBackup(bool on) { setValue(QStringLiteral("autoBackup"), Value(on)); }
// Electron：Math.min(90, Math.max(1, Math.round(+P.backupEveryDays || 7)))
int Settings::clampBackupDays(double days)
{
    if (!std::isfinite(days) || days == 0) days = 7;
    return int(std::clamp(std::floor(days + 0.5), 1.0, 90.0));
}
int Settings::backupEveryDays() const
{
    const Value &v = m_root[QStringLiteral("backupEveryDays")];
    return clampBackupDays(v.isNumber() ? v.toNumber() : v.jsToNumber());
}
void Settings::setBackupEveryDays(int days) { setValue(QStringLiteral("backupEveryDays"), Value(clampBackupDays(days))); }
double Settings::lastBackup() const
{
    const Value &v = m_root[QStringLiteral("lastBackup")];
    return v.isNumber() && std::isfinite(v.toNumber()) ? v.toNumber() : 0;
}
void Settings::setLastBackup(double ms) { setValue(QStringLiteral("lastBackup"), Value(ms)); }

bool Settings::hasBounds() const
{
    const Value &b = m_root[QStringLiteral("bounds")];
    return b.isObject() && b[QStringLiteral("width")].isNumber() && b[QStringLiteral("height")].isNumber();
}
QRect Settings::bounds() const
{
    const Value &b = m_root[QStringLiteral("bounds")];
    const auto num = [&b](const char *k, int def) {
        const Value &v = b[QString::fromLatin1(k)];
        return v.isNumber() && std::isfinite(v.toNumber()) ? int(std::lround(v.toNumber())) : def;
    };
    return QRect(num("x", 0), num("y", 0), num("width", kDefaultWidth), num("height", kDefaultHeight));
}
void Settings::setBounds(const QRect &r)
{
    // 和 Electron 一样的形状：{ x, y, width, height }
    json::Object o;
    o.set(QStringLiteral("x"), Value(r.x()));
    o.set(QStringLiteral("y"), Value(r.y()));
    o.set(QStringLiteral("width"), Value(r.width()));
    o.set(QStringLiteral("height"), Value(r.height()));
    setValue(QStringLiteral("bounds"), Value(std::move(o)));
}
bool Settings::maximized() const { return m_root[QStringLiteral("maximized")].toBool(); }
void Settings::setMaximized(bool m) { setValue(QStringLiteral("maximized"), Value(m)); }

} // namespace tp::app
