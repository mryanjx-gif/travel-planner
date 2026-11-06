// 地图页的计划：筛选、"此刻"、简易计划面板（0.8.7，M-20、M-21、M-22、S-09–S-11）。
// 计算都在 tp::map::plans（plans.h，和 Electron 对照测试）；这里负责：
//   - 筛选 / 隐藏的状态，存在 settings.json 的 mapFilter（Electron 存 localStorage；每台电脑自己的界面状态，不进 travel.json）；
//   - 把筛选交给地图（被筛掉 / 隐藏的计划不画）；
//   - 给 QML 的"此刻"（now）和简易计划面板（sheet）；本地日期过了零点自动换；
//   - 改数据的几个操作（设为下次出发、勾待办、置顶、标记为已完成、删除计划），改完立即保存（撤销是一步）；
//   - 0.8.8："我的旅程"列表（list）、Ctrl K 的计划和站点（commandItems）、最近打开的计划（recent，存在 settings.json 的 recentPlans）。
//   - 0.8.10：地图的四种模式（PO 2026-11-06 定的页面结构）：plan 计划 / wish 想去 / foot 足迹 / photo 照片。
//     状态不再由用户直接勾，而是由模式决定：计划 = 计划中 + "也显示"里勾的（想去、已完成）；想去 = 想去 + "也显示"（PO 选改进 c）；
//     足迹 = 只看已完成；照片 = 全部（照片钉到地图上在阶段 ⑤）。类型、区域、眼睛隐藏在四种模式里通用。
//     每种模式记住自己的"也显示"（PO 选改进 a）：settings.json 的 mapMode、modeAlso（0.9.5 起不再用 modeBase：四种模式共用一个底图）。
//   - 0.9.7 左上模式 / 左下胶囊去重复（PO 2026-11-06 15:15 选方案 A）：不再有"也显示"，每种模式只显示自己的状态；
//     筛选弹窗最下面一个"叠上其他旅程"开关（计划 / 想去 / 足迹有，照片没有；每种模式各自记住）：开着时其他状态的旅程
//     画成淡灰细线垫在下面（不能点、没有标记）。settings.json 的 modeAlso 从"每种模式一个状态列表"改成"每种模式开 / 关"（旧的列表非空 = 开）。
#pragma once

#include "plans.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace tp::data { class DataStore; }
namespace tp::map { class MapController; }

namespace tp::app {

class AppController;
class Settings;

class PlanController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList filterStatus READ filterStatus NOTIFY filterChanged)
    Q_PROPERTY(QStringList filterType READ filterType NOTIFY filterChanged)
    Q_PROPERTY(QStringList filterRegion READ filterRegion NOTIFY filterChanged)
    Q_PROPERTY(QStringList regions READ regions CONSTANT)              // 9 个区域 + 未分类（中文键）
    Q_PROPERTY(int excludedCount READ excludedCount NOTIFY filterChanged)
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY filterChanged)
    Q_PROPERTY(QVariantMap now READ now NOTIFY nowChanged)
    Q_PROPERTY(QVariantMap sheet READ sheet NOTIFY sheetChanged)
    Q_PROPERTY(QString today READ today NOTIFY nowChanged)
    Q_PROPERTY(QVariantMap list READ list NOTIFY listChanged)          // 0.8.8 我的旅程（plans::listInfo）
    Q_PROPERTY(QStringList recent READ recent NOTIFY recentChanged)    // 0.8.8 最近打开的计划 id（新的在前，最多 8 个）
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)              // 0.8.10 plan / wish / foot / photo
    Q_PROPERTY(QStringList modes READ modes CONSTANT)
    Q_PROPERTY(QString modePrimary READ modePrimary NOTIFY modeChanged)  // 这个模式固定显示的状态（照片模式为空 = 全部）
    Q_PROPERTY(bool overlayAvailable READ overlayAvailable NOTIFY overlayChanged)  // 0.9.7：这个模式有"叠上其他旅程"（照片没有）
    Q_PROPERTY(bool overlay READ overlay NOTIFY overlayChanged)                    // 0.9.7：这个模式叠上了其他旅程

public:
    PlanController(AppController *app, Settings *settings, data::DataStore *store, map::MapController *mapc, QObject *parent = nullptr);
    ~PlanController() override;

    QStringList filterStatus() const;
    QStringList filterType() const;
    QStringList filterRegion() const;
    QStringList regions() const { return map::plans::allRegions(); }
    // 0.8.10：角标只数类型、区域（状态归模式管）
    int excludedCount() const { return m_filter.excludedCount() - int(map::plans::allStatuses().size() - m_filter.status.size()); }
    int hiddenCount() const { return int(m_filter.hidden.size()); }
    QVariantMap now() const { return m_now; }
    QVariantMap sheet() const { return m_sheet; }
    QString today() const { return m_today; }
    QVariantMap list() const { return m_list; }
    QStringList recent() const { return m_recent; }
    QString mode() const { return m_mode; }
    static QStringList modes() { return { QStringLiteral("plan"), QStringLiteral("wish"), QStringLiteral("foot"), QStringLiteral("photo") }; }
    QString modePrimary() const;
    bool overlayAvailable() const { return m_mode != QLatin1String("photo"); }
    bool overlay() const { return overlayAvailable() && m_overlay.value(m_mode) && !m_testAll; }
    // 0.9.7：地图画淡灰细线的那些计划：叠上开着时，状态不是这个模式的、但类型 / 区域 / 隐藏都通过的
    bool overlayPlan(const json::Value &plan) const;
    const map::plans::Filter &filter() const { return m_filter; }

    // ---- 筛选 ----
    Q_INVOKABLE void toggleFilter(const QString &group, const QString &key);   // group：status / type / region
    Q_INVOKABLE void onlyRegion(const QString &region);
    Q_INVOKABLE void filterAll();
    Q_INVOKABLE void filterNone();
    Q_INVOKABLE void setHidden(const QString &planId, bool hidden);
    Q_INVOKABLE void showAllHidden();
    // 被筛掉或隐藏的计划要显示时：打开需要的选项。返回 { opened: [键], unhid, mode }（什么都没改时 opened 为空、unhid 为假）。
    // 0.8.10：状态不在当前模式里时，计划 / 想去模式在"也显示"里勾上；足迹模式切到它该在的模式（mode = 切到的模式）
    // 0.9.7：没有"也显示"了，状态不在当前模式里一律切到它该在的模式
    Q_INVOKABLE QVariantMap reveal(const QString &planId);
    // 打开这个计划的简易面板（reveal + 选中）；fly = 同时框住它。返回 reveal 的结果
    Q_INVOKABLE QVariantMap openPlan(const QString &planId, bool fly, int stopIdx = -1);

    // ---- 0.8.8 ----
    Q_INVOKABLE QVariantList commandItems() const;   // Ctrl K 的计划和站点（plans::commandItems）
    Q_INVOKABLE void noteRecent(const QString &planId);

    // ---- 0.8.10 模式 ----
    Q_INVOKABLE void setMode(const QString &mode);
    // 某个状态的计划该在哪种模式里看（planning → plan、wish → wish、completed → foot）
    Q_INVOKABLE static QString modeForStatus(const QString &status);
    // 0.9.7：叠上其他旅程（当前模式）开 / 关
    Q_INVOKABLE void setOverlay(bool on);
    Q_INVOKABLE void toggleOverlay() { setOverlay(!overlay()); }
    void setOverlayForTest(const QString &mode, bool on);
    // 截图 / 自检 / 内存测试：四种状态都按正常样子画（和 0.8.9 一样画全部计划，0.8.10 起靠"也显示"叠上，0.9.7 起靠这个）
    Q_INVOKABLE void setTestAllStatuses(bool on);

    // ---- 改数据（成功返回 true；只读、没有这个计划或没有变化时返回 false） ----
    Q_INVOKABLE bool setNextTrip(const QString &planId, bool on);
    Q_INVOKABLE bool setTodoDone(const QString &planId, const QString &todoId, bool done);
    Q_INVOKABLE bool togglePin(const QString &planId);
    Q_INVOKABLE bool markCompleted(const QString &planId);
    Q_INVOKABLE bool deletePlan(const QString &planId);

    // 截图 / 自检 / 内存测试时不把筛选写进 settings.json（免得影响下一次运行）
    // 0.8.10：同时忘掉 settings.json 里记着的模式、"也显示"和各模式的底图（截图 / 自检每次都从同一个样子开始）
    void setPersist(bool on);
    // 截图 now 页、自检：找一条有日期、站点最多的路线（一个月以内的优先），返回 { planId, today = 开始后第 3 天（不超过结束日期）}；没有时返回空
    Q_INVOKABLE QVariantMap demo() const;
    // 测试 / 截图：假装今天是某一天（空 = 恢复真实日期）
    Q_INVOKABLE void setTodayOverride(const QString &day);
    void setFilterForTest(const map::plans::Filter &f) { m_filter = f; applyFilter(false); }

signals:
    void filterChanged();
    void nowChanged();
    void sheetChanged();
    void listChanged();
    void recentChanged();
    void modeChanged();
    void overlayChanged();   // 0.9.7（换模式时也发）

private:
    void loadFilter();
    void applyFilter(bool save = true);
    void refreshNow();
    void refreshSheet();
    void refreshList();
    void loadRecent();
    void loadModes();
    void saveModes();
    QSet<QString> statusForMode() const;
    bool edit(const std::function<bool(json::Value &)> &fn, const char *what);

    AppController *m_app;
    Settings *m_settings;
    data::DataStore *m_store;
    map::MapController *m_mapc;
    map::plans::Filter m_filter = map::plans::Filter::all();
    QVariantMap m_now, m_sheet, m_list;
    QStringList m_recent;
    QString m_mode = QStringLiteral("plan");
    QHash<QString, bool> m_overlay;          // 0.9.7：plan / wish / foot 各自"叠上其他旅程"
    bool m_testAll = false;
    QString m_today, m_todayOverride;
    QTimer m_dayTimer;
    bool m_persist = true;
};

} // namespace tp::app
