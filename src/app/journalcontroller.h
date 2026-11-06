// 游记页（0.11.2，J-01–J-04）。QML 里是 journal（qml/pages/JournalPage.qml）。计算和修改都在 tp::map::plans（plankit.h）。
// 新建的游记先不进数据：写了字才存（journal.save 找不到这个 id 就新建），什么都没写就离开时数据完全没动，撤销里也不多一步。
// 写了又全删掉的，离开时删掉（dropEmpty）。
#pragma once

#include <QObject>
#include <QVariantMap>

namespace tp::data { class DataStore; }

namespace tp::app {

class AppController;
class PlanPageController;

class JournalController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY pageChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY pageChanged)    // all / none / 计划 id（不记住，和 Electron 一样）
    Q_PROPERTY(QVariantMap page READ page NOTIFY pageChanged)                    // plans::journalPage + thumb（640）

public:
    JournalController(AppController *app, data::DataStore *store, QObject *parent = nullptr);

    bool active() const { return m_active; }
    QString filter() const { return m_filter; }
    QVariantMap page() const { return m_page; }
    void setActive(bool on);
    void setFilter(const QString &f);

    Q_INVOKABLE QString newId() const;
    Q_INVOKABLE QVariantMap entry(const QString &id) const;       // plans::journalEntry（没有时为空）
    Q_INVOKABLE QVariantList planOptions() const;                 // [{ key, text }]（第一项"不关联"由 QML 加）
    Q_INVOKABLE QVariantList stopOptions(const QString &planId) const;
    Q_INVOKABLE bool save(const QString &id, const QVariantMap &fields);   // { title, date, plan, stop, body }。没有变化返回 false；全是空的新游记不存
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE int dropEmpty(const QString &keepId);             // 删掉什么都没写的（正在写的除外）

signals:
    void pageChanged();
    void touched();     // 数据变了（正文改了但列表一样时也发）：开着的这一篇要重读

private:
    void refresh();

    AppController *m_app;
    data::DataStore *m_store;
    bool m_active = false;
    QString m_filter = QStringLiteral("all");
    QVariantMap m_page;
};

} // namespace tp::app
