#include "tripcontroller.h"

#include "appcontroller.h"
#include "datastore.h"
#include "planmerge.h"
#include "translator.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QUrl>

namespace tp::app {

using json::Value;
namespace trip = exporter::trip;

namespace {

QVariantMap fail(const QString &why) { return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), why } }; }

QString uid(const QString &prefix)
{
    QString r;
    for (int i = 0; i < 5; ++i) r += QString::number(QRandomGenerator::global()->bounded(36), 36);
    return prefix + u'_' + QString::number(QDateTime::currentMSecsSinceEpoch(), 36) + r;
}

QString planName(const Value &doc, const QString &id)
{
    for (const Value &p : doc[QStringLiteral("plans")].asArray())
        if (p[QStringLiteral("id")].toString() == id) return p[QStringLiteral("name")].toString();
    return {};
}

} // namespace

TripController::TripController(data::DataStore *store, AppController *app, std::function<QString()> currency, QObject *parent)
    : QObject(parent), m_store(store), m_app(app), m_currency(std::move(currency))
{
}

TripController::~TripController() = default;

QString TripController::localPath(const QString &p) const { return p.startsWith(QLatin1String("file:")) ? QUrl(p).toLocalFile() : p; }

QString TripController::defaultName(const QStringList &ids) const
{
    if (ids.size() == 1) {
        const QString n = trip::safeFileName(planName(m_store->data(), ids.first()));
        return n.isEmpty() ? tp::tr("计划") : n;
    }
    return tp::tr("%1 个计划").arg(ids.size());
}

bool TripController::isTripFile(const QString &urlOrPath) const
{
    return localPath(urlOrPath).endsWith(QLatin1String(".trip"), Qt::CaseInsensitive);
}

QVariantMap TripController::exportTrip(const QStringList &ids, const QString &pathIn)
{
    const QString path = localPath(pathIn);
    if (ids.isEmpty() || path.isEmpty()) return fail(tp::tr("没有选要导出的计划"));
    const trip::Built b = trip::build(m_store->data(), ids, m_currency ? m_currency() : QString(), m_app->version(), QDateTime::currentDateTime());
    if (b.plans == 0) return fail(tp::tr("找不到这个计划"));
    const trip::WriteResult w = trip::write(path, b.manifest, b.photos, m_store->dataDir());
    if (!w.ok) return fail(w.error);
    qInfo(".trip 导出：%d 个计划、%d 张照片（%lld 张找不到）→ %s", b.plans, w.photos, qlonglong(w.missing.size()), qUtf8Printable(w.file));
    return { { QStringLiteral("ok"), true }, { QStringLiteral("file"), w.file }, { QStringLiteral("photos"), w.photos },
             { QStringLiteral("missing"), int(w.missing.size()) }, { QStringLiteral("plans"), b.plans } };
}

QVariantMap TripController::exportEach(const QStringList &ids, const QString &dirIn)
{
    const QString dir = localPath(dirIn);
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) return fail(tp::tr("没有选导出到哪里"));
    int n = 0, photos = 0, missing = 0;
    for (const QString &id : ids) {
        const QString name = defaultName({ id });
        const QVariantMap r = exportTrip({ id }, trip::uniquePath(dir, name, QStringLiteral(".trip")));
        if (!r.value(QStringLiteral("ok")).toBool()) return fail(tp::tr("导出“%1”失败：%2").arg(name, r.value(QStringLiteral("error")).toString()));
        ++n;
        photos += r.value(QStringLiteral("photos")).toInt();
        missing += r.value(QStringLiteral("missing")).toInt();
    }
    return { { QStringLiteral("ok"), true }, { QStringLiteral("dir"), QDir::toNativeSeparators(dir) }, { QStringLiteral("count"), n },
             { QStringLiteral("photos"), photos }, { QStringLiteral("missing"), missing } };
}

QVariantMap TripController::exportShare(const QStringList &ids, const QVariantMap &opts, const QString &destIn, bool each)
{
    const QString dest = localPath(destIn);
    if (dest.isEmpty()) return fail(tp::tr("没有选导出到哪里"));
    trip::ShareOptions o;
    o.noMoney = opts.value(QStringLiteral("noMoney"), true).toBool();
    o.noBook = opts.value(QStringLiteral("noBook"), true).toBool();
    o.noTodo = opts.value(QStringLiteral("noTodo")).toBool();
    o.noJournal = opts.value(QStringLiteral("noJournal")).toBool();
    o.noVisited = opts.value(QStringLiteral("noVisited")).toBool();
    o.allPhotos = opts.value(QStringLiteral("allPhotos")).toBool();
    o.unknownCovers = opts.value(QStringLiteral("unknownCovers")).toBool();
    o.unknownStops = opts.value(QStringLiteral("unknownStops")).toBool();
    const QDateTime now = QDateTime::currentDateTime();
    const QString cur = m_currency ? m_currency() : QString();
    QList<QStringList> jobs;
    if (each && ids.size() > 1) for (const QString &id : ids) jobs << QStringList{ id };
    else jobs << ids;
    int photos = 0, skipped = 0, missing = 0;
    QString lastDir;
    for (const QStringList &job : jobs) {
        const trip::Shared s = trip::share(m_store->data(), job, o, cur, now);
        const QString name = jobs.size() > 1 ? defaultName(job) : QString();
        const trip::ShareWrite w = trip::writeShare(dest, name, s, m_store->dataDir(), m_app->version(), now);
        if (!w.ok) return fail(tp::tr("导出失败：") + w.error);
        photos += w.photos;
        skipped += s.skipped;
        missing += int(w.missing.size());
        lastDir = w.dir;
    }
    qInfo("分享版导出：%lld 个文件夹，照片 %d 张（%d 张不能分发、%d 张找不到）", qlonglong(jobs.size()), photos, skipped, missing);
    return { { QStringLiteral("ok"), true }, { QStringLiteral("dir"), jobs.size() > 1 ? QDir::toNativeSeparators(dest) : lastDir },
             { QStringLiteral("count"), int(jobs.size()) }, { QStringLiteral("photos"), photos }, { QStringLiteral("skipped"), skipped },
             { QStringLiteral("missing"), missing } };
}

QVariantMap TripController::openImport(const QString &urlOrPath)
{
    cancelImport();
    const QString path = localPath(urlOrPath);
    if (!isTripFile(path)) return fail(tp::tr("这里只能拖入 .trip 计划文件（照片请在相册里点“导入照片”）"));
    if (m_store->isReadOnly()) return fail(tp::tr("现在是只读模式，不能导入。"));
    auto reader = std::make_unique<trip::Reader>();
    if (!reader->open(path)) return fail(reader->error());
    m_srcOf.clear();
    m_incoming = trip::toNative(reader->manifest(), reader->files(), QDateTime::currentDateTime(), &m_srcOf);
    QVariantList plans;
    for (const Value &p : m_incoming[QStringLiteral("plans")].asArray()) {
        const QString id = p[QStringLiteral("id")].toString();
        const QVariantMap local = data::planmerge::summary(m_store->data(), id);
        plans << QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("name"), p[QStringLiteral("name")].toString() },
                              { QStringLiteral("conflict"), !local.isEmpty() }, { QStringLiteral("local"), local },
                              { QStringLiteral("incoming"), data::planmerge::summary(m_incoming, id) } };
    }
    m_reader = std::move(reader);
    emit importChanged();
    qInfo(".trip 打开：%s（%lld 个计划）", qUtf8Printable(QDir::toNativeSeparators(path)), qlonglong(plans.size()));
    return { { QStringLiteral("ok"), true }, { QStringLiteral("file"), QFileInfo(path).fileName() }, { QStringLiteral("plans"), plans } };
}

QVariantMap TripController::commitImport(const QVariantMap &modesIn)
{
    if (!m_reader) return fail(tp::tr("导入已经过期，请重新导入"));
    if (m_store->isReadOnly()) { cancelImport(); return fail(tp::tr("现在是只读模式，不能导入。")); }
    if (m_app->undoGrouped()) return fail(tp::tr("先保存或取消地图编辑器里的改动，再导入"));
    QHash<QString, data::planmerge::Mode> modes;
    for (auto it = modesIn.cbegin(); it != modesIn.cend(); ++it) modes.insert(it.key(), data::planmerge::modeFromKey(it.value().toString()));
    m_app->commitStep();   // 手上没存的改动先存成一步，撤销时只退导入
    data::planmerge::Options po;
    po.uid = uid;
    po.copySuffix = tp::tr("（导入）");
    po.defaultCurrency = m_currency ? m_currency() : QString();
    data::planmerge::Result res = data::planmerge::apply(m_store->data(), m_incoming, modes, po);
    if (res.plans == 0) {
        cancelImport();
        return { { QStringLiteral("ok"), false }, { QStringLiteral("cancelled"), true } };
    }
    const data::planmerge::WriteResult w = data::planmerge::writeFiles(
        res.doc, m_store->dataDir(), res.files, [this](const QString &from, QByteArray *b) {
            const QString src = m_srcOf.value(from);
            return !src.isEmpty() && m_reader->read(src, b);
        });
    if (!w.ok) { cancelImport(); return fail(tp::tr("导入失败：") + w.error); }
    m_store->modify([&res](Value &d) { d = res.doc; });
    m_app->commitStep();   // 一次导入在撤销里是一步
    qInfo(".trip 导入：%d 个计划、%d 张照片（写入 %d、已有 %d、包里没有 %d）、%d 篇游记", res.plans, res.photos, w.written, w.same, w.missing, res.journal);
    QString msg = res.plans > 1 ? tp::tr("已导入 %1 个计划").arg(res.plans) : res.messages.value(0);
    if (w.written > 0) msg += tp::tr("，照片 %1 张").arg(w.written);
    QString openId;
    if (res.plans == 1) openId = res.planIds.cbegin().value();
    cancelImport();
    return { { QStringLiteral("ok"), true }, { QStringLiteral("message"), msg }, { QStringLiteral("openId"), openId },
             { QStringLiteral("count"), res.plans }, { QStringLiteral("photos"), w.written } };
}

void TripController::cancelImport()
{
    if (!m_reader) return;
    m_reader.reset();
    m_incoming = Value();
    m_srcOf.clear();
    emit importChanged();
}

} // namespace tp::app
