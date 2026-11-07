// 日历页（0.11.2，C-01–C-03、C-08–C-10）。QML 里是 calendar（qml/pages/CalendarPage.qml）。计算都在 tp::map::plans（plancal.h）；
// 这里负责：现在看哪一段、隐藏了哪些、季节表折叠了哪些（存在 settings.json 的 calendarView；一周起始是顶层的 weekStart，键名和 Electron 一样；
// 都是每台电脑自己的界面状态，不进 travel.json）、
// 改日期（改完立即保存，撤销是一步，PO 第 9 题）。
#pragma once

#include <QDate>
#include <QObject>
#include <QSet>
#include <QVariantMap>

#include <functional>

namespace tp::data { class DataStore; }
namespace tp::json { class Value; }
namespace tp::map { class MapController; }

namespace tp::app {

class AppController;
class PlanController;
class Settings;

class CalendarController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY stateChanged)       // 页面开着才算
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY stateChanged)          // year（默认）/ month / week / season
    Q_PROPERTY(int weekStart READ weekStart WRITE setWeekStart NOTIFY stateChanged) // 1 周一（默认）/ 0 周日（0.11.3，C-11）
    Q_PROPERTY(QString weekFirst READ weekFirst NOTIFY stateChanged)              // 周视图看的这一周的第一天
    Q_PROPERTY(int year READ year NOTIFY stateChanged)
    Q_PROPERTY(int month READ month NOTIFY stateChanged)                          // 1–12
    Q_PROPERTY(bool sideOpen READ sideOpen WRITE setSideOpen NOTIFY stateChanged) // 右边的"还没定日期"（PO 第 8 题，记住）
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)                     // { year / month / week / season 其一（plans::cal…）, side（plans::calSide）}

public:
    CalendarController(AppController *app, PlanController *plans, Settings *settings, data::DataStore *store, map::MapController *mapc, QObject *parent = nullptr);

    bool active() const { return m_active; }
    QString view() const { return m_view; }
    int year() const { return m_y; }
    int month() const { return m_m; }
    int weekStart() const { return m_weekStart; }
    QString weekFirst() const;
    void setWeekStart(int v);
    bool sideOpen() const { return m_side; }
    QVariantMap info() const { return m_info; }
    void setActive(bool on);
    void setView(const QString &v);
    void setSideOpen(bool on);

    Q_INVOKABLE void step(int dir);                 // ‹ ›：年视图 6 个月，月视图 1 个月，周视图 1 周
    Q_INVOKABLE void showWeek(const QString &date); // 月视图点日期：跳到那一周
    Q_INVOKABLE void setSeasonGroupClosed(const QString &group, bool closed);
    Q_INVOKABLE void setAllSeasonGroups(bool closed);
    Q_INVOKABLE void goToday();
    Q_INVOKABLE void goTo(int y, int m);
    Q_INVOKABLE void showDate(const QString &date); // 计划页"在日历里看"：跳到那个月，年、季节视图换成月视图（Electron render 的 params.date）
    Q_INVOKABLE QVariantMap picker() const;         // plans::calPicker

    Q_INVOKABLE void setHidden(const QString &planId, bool hidden);
    Q_INVOKABLE bool isHidden(const QString &planId) const { return m_hidden.contains(planId); }

    // 拖动的落点：day = 松手的那天，grab = 按住横条的那一天（没定日期的给空）。返回 { start, end, overlaps }；没变化或不是日期 start 为空
    Q_INVOKABLE QVariantMap dropRange(const QString &planId, const QString &day, const QString &grab) const;
    // 下面三个改数据，返回 { ok, id, name, start, end, moved（日期真的变了）, placed（原来没定日期）, toPlanning, bookings, overlaps, days, error }
    Q_INVOKABLE QVariantMap drop(const QString &planId, const QString &day, const QString &grab);
    Q_INVOKABLE QVariantMap removeDates(const QString &planId);
    Q_INVOKABLE QVariantMap savePlan(const QString &planId, const QVariantMap &fields);   // 弹出卡：{ name, start, days, status, color（空 = 不改）}
    Q_INVOKABLE QVariantMap planFields(const QString &planId) const;                       // 弹出卡打开时：{ name, start, days, status, color }

    // 截图 / 自检不读不写 settings.json，每次从同一个样子开始
    void setPersist(bool on);
    void reopenForTest(const QString &settingsFile);   // 自检：现在的设置写进这个文件，清空，再读回来

signals:
    void stateChanged();
    void infoChanged();

private:
    void load(const Settings &s);
    QDate weekOf(const QDate &d) const;
    void save();
    void refresh();
    bool apply(const QString &planId, const std::function<void(json::Value &plan)> &fn, const char *what);
    QVariantMap result(const QString &planId, bool moved) const;

    AppController *m_app;
    PlanController *m_plans;
    Settings *m_settings;
    data::DataStore *m_store;
    bool m_active = false, m_persist = true, m_side = true;
    QString m_view = QStringLiteral("year");
    int m_y = 0, m_m = 1, m_weekStart = 1;
    QDate m_ws;                                     // 周视图这一周的第一天
    QSet<QString> m_hidden, m_seasonClosed;
    map::MapController *m_map;
    QVariantMap m_info;
};

} // namespace tp::app
