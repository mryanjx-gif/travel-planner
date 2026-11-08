#include "exportcontroller.h"

#include "datastore.h"
#include "ics.h"
#include "itinerary.h"
#include "plans.h"
#include "translator.h"

#include <QDate>
#include <QDateTime>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace tp::app {

using json::Value;

namespace {

QString localPath(const QString &p) { return p.startsWith(QLatin1String("file:")) ? QUrl(p).toLocalFile() : p; }

exporter::Options optionsOf(const QVariantMap &m, const ExportController::Env &env)
{
    exporter::Options o;
    o.refs = m.value(QStringLiteral("refs"), false).toBool();
    o.money = m.value(QStringLiteral("money"), false).toBool();
    o.notes = m.value(QStringLiteral("notes"), true).toBool();
    o.emergency = m.value(QStringLiteral("emergency"), false).toBool();
    o.font = env.font;
    o.adminDir = env.adminDir;
    o.currency = env.currency ? env.currency() : QString();
    o.today = env.today ? env.today() : QDate::currentDate().toString(Qt::ISODate);
    return o;
}

bool hasEmergency(const Value &root, const Value &plan)
{
    for (const Value &x : plan[QStringLiteral("localEmergency")].asArray())
        if (!x[QStringLiteral("phone")].toString().isEmpty() || !x[QStringLiteral("label")].toString().isEmpty()) return true;
    const Value &e = root[QStringLiteral("emergency")];
    for (const char *k : { "insurer", "policyNo", "insurerPhone", "notes" })
        if (!e[QString::fromLatin1(k)].toString().isEmpty()) return true;
    return !e[QStringLiteral("contacts")].asArray().empty();
}

} // namespace

ExportController::ExportController(data::DataStore *store, Env env, QObject *parent)
    : QObject(parent), m_store(store), m_env(std::move(env))
{
}

QVariantMap ExportController::info(const QString &planId) const
{
    const Value &root = m_store->data();
    const Value *p = map::plans::planById(root, planId);
    if (!p) return {};
    return { { QStringLiteral("name"), (*p)[QStringLiteral("name")].toString() },
             { QStringLiteral("stops"), int(map::plans::validStops(*p).size()) },
             { QStringLiteral("hasEmergency"), hasEmergency(root, *p) },
             { QStringLiteral("icsCount"), exporter::ics(*p, QDateTime::currentDateTimeUtc()).count } };
}

QString ExportController::preview(const QString &planId, const QString &kind, const QVariantMap &opts, int widthPx)
{
    const QImage img = renderPage(planId, kind, opts, 0, std::max(120, widthPx));
    if (img.isNull()) return {};
    QDir().mkpath(m_env.cacheDir);
    // 每次换个文件名，Image 才会重新读；旧的删掉
    const QString dir = m_env.cacheDir;
    QFile::remove(dir + QStringLiteral("/export-preview-%1.png").arg(m_previewNo));
    const QString file = dir + QStringLiteral("/export-preview-%1.png").arg(++m_previewNo);
    if (!img.save(file)) return {};
    return QUrl::fromLocalFile(file).toString();
}

QImage ExportController::renderPage(const QString &planId, const QString &kind, const QVariantMap &opts, int page, int widthPx, int *pages) const
{
    const Value &root = m_store->data();
    const Value *p = map::plans::planById(root, planId);
    if (!p) return {};
    const exporter::Options o = optionsOf(opts, m_env);
    const exporter::Doc d = kind == QLatin1String("card") ? exporter::Doc::card(root, *p, o) : exporter::Doc::itinerary(root, *p, o);
    if (pages) *pages = d.pageCount();
    return d.renderPage(std::clamp(page, 0, d.pageCount() - 1), widthPx);
}

QString ExportController::defaultName(const QString &planId, const QString &kind, const QString &format) const
{
    const Value *p = map::plans::planById(m_store->data(), planId);
    QString name = p ? (*p)[QStringLiteral("name")].toString().trimmed() : QString();
    if (name.isEmpty()) name = tp::tr("旅行计划");
    static const QString bad = QStringLiteral("\\/:*?\"<>|");
    for (QChar &c : name) if (bad.contains(c) || c < QChar(32)) c = QLatin1Char('_');
    if (kind == QLatin1String("ics")) return name + QStringLiteral(".ics");
    const QString what = kind == QLatin1String("card") ? tp::tr("紧急信息卡") : tp::tr("行程单");
    return name + QLatin1Char(' ') + what + (format == QLatin1String("png") ? QStringLiteral(".png") : QStringLiteral(".pdf"));
}

QString ExportController::defaultFolder() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
}

QVariantMap ExportController::save(const QString &planId, const QString &kind, const QString &format, const QVariantMap &opts, const QString &path0)
{
    const QString path = localPath(path0);
    const auto fail = [&path](const QString &e) {
        qWarning("导出失败：%s %s", qUtf8Printable(path), qUtf8Printable(e));
        return QVariantMap{ { QStringLiteral("ok"), false }, { QStringLiteral("error"), e } };
    };
    const Value &root = m_store->data();
    const Value *p = map::plans::planById(root, planId);
    if (!p) return fail(tp::tr("找不到这个计划"));
    if (path.isEmpty()) return fail(tp::tr("没有选文件"));
    QVariantMap out{ { QStringLiteral("ok"), true }, { QStringLiteral("file"), QDir::toNativeSeparators(path) } };
    if (kind == QLatin1String("ics")) {
        const exporter::IcsResult r = exporter::ics(*p, QDateTime::currentDateTimeUtc());
        if (!r.count) return fail(tp::tr("这个计划还没有带日期的内容（计划日期、交通住宿、日程），没东西可以导出"));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(r.text) != r.text.size() || !f.flush()) return fail(f.errorString());
        out.insert(QStringLiteral("count"), r.count);
    } else {
        const exporter::Options o = optionsOf(opts, m_env);
        const bool card = kind == QLatin1String("card");
        if (card && !hasEmergency(root, *p)) return fail(tp::tr("还没有填紧急信息"));
        const exporter::Doc d = card ? exporter::Doc::card(root, *p, o) : exporter::Doc::itinerary(root, *p, o);
        if (format == QLatin1String("png")) {
            bool cut = false;
            const QImage img = d.longImage(&cut);
            if (!img.save(path, "PNG")) return fail(tp::tr("文件写不进去"));
            out.insert(QStringLiteral("truncated"), cut);
            out.insert(QStringLiteral("height"), img.height());
        } else {
            QString err;
            if (!d.writePdf(path, &err)) return fail(err);
            out.insert(QStringLiteral("pages"), d.pageCount());
        }
    }
    qInfo("导出：%s", qUtf8Printable(QDir::toNativeSeparators(path)));
    return out;
}

} // namespace tp::app
