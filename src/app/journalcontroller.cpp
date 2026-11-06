#include "journalcontroller.h"

#include "appcontroller.h"
#include "datastore.h"
#include "plankit.h"
#include "plans.h"
#include "thumbprovider.h"

#include <QRandomGenerator>

namespace tp::app {

using json::Value;
namespace plans = map::plans;

JournalController::JournalController(AppController *app, data::DataStore *store, QObject *parent)
    : QObject(parent), m_app(app), m_store(store)
{
    connect(store, &data::DataStore::contentChanged, this, &JournalController::refresh);
    connect(app, &AppController::dataChanged, this, &JournalController::refresh);
}

void JournalController::setActive(bool on)
{
    if (on == m_active) return;
    m_active = on;
    refresh();
    emit pageChanged();
}

void JournalController::setFilter(const QString &f)
{
    if (f == m_filter) return;
    m_filter = f;
    refresh();
    emit pageChanged();
}

void JournalController::refresh()
{
    if (!m_active) return;
    emit touched();
    QVariantMap p = plans::journalPage(m_store->data(), m_filter);
    QVariantList rows = p.value(QStringLiteral("rows")).toList();
    for (QVariant &v : rows) {
        QVariantMap m = v.toMap();
        const QString f = m.value(QStringLiteral("file")).toString();
        if (f.startsWith(QLatin1String("photos/"))) m.insert(QStringLiteral("thumb"), ThumbProvider::urlFor(f, 640, 0));
        v = m;
    }
    p.insert(QStringLiteral("rows"), rows);
    // 筛选的计划已经没有游记了：回到全部（Electron renderList）
    if (p.value(QStringLiteral("filterGone")).toBool()) { m_filter = QStringLiteral("all"); refresh(); return; }
    if (p == m_page) return;
    m_page = p;
    emit pageChanged();
}

QString JournalController::newId() const
{
    static const char kChars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    QString s = QStringLiteral("jn_");
    for (int i = 0; i < 13; ++i) s += QLatin1Char(kChars[QRandomGenerator::global()->bounded(36)]);
    return s;
}

QVariantMap JournalController::entry(const QString &id) const { return plans::journalEntry(m_store->data(), id); }

QVariantList JournalController::planOptions() const { return plans::journalPlanOptions(m_store->data()); }

QVariantList JournalController::stopOptions(const QString &planId) const { return plans::journalStopOptions(m_store->data(), planId); }

bool JournalController::save(const QString &id, const QVariantMap &f)
{
    if (m_store->isReadOnly() || id.isEmpty()) return false;
    Value copy = m_store->data();
    if (!plans::putJournal(copy, id, f) || copy == m_store->data()) return false;
    m_store->modify([&](Value &d) { d = copy; });
    m_app->noteChanged(false);   // 停手 0.8 秒自动保存
    return true;
}

bool JournalController::remove(const QString &id)
{
    if (m_store->isReadOnly()) return false;
    Value copy = m_store->data();
    if (!plans::removeJournal(copy, id)) return false;
    m_store->modify([&](Value &d) { d = copy; });
    m_app->noteChanged(true);
    return true;
}

int JournalController::dropEmpty(const QString &keepId)
{
    if (m_store->isReadOnly()) return 0;
    Value copy = m_store->data();
    const int n = plans::dropEmptyJournal(copy, keepId);
    if (n > 0) {
        m_store->modify([&](Value &d) { d = copy; });
        m_app->noteChanged(false);
    }
    return n;
}

} // namespace tp::app
