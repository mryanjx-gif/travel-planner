// 站点编辑器（0.8.9，功能清单 M-23 / M-24 / M-25 / M-18 编辑时单段 / M-11 / M-09）。QML 里是 editor（qml/components/PlanEditor.qml）。
//
// 照搬 Electron v6.3.0 map-app.js 的 openEditor / renderEditor / renderEditorStops / bindStopEvents / bindLegEvents /
// scheduleLegRouting / runLegRouting / addStop / setPickMode / doSearch / reverseGeocode / validateEditor / edDelete，
// PO 0.8.9 选定的不同之处：
//   - 方案 B 排法（界面的事）；
//   - 范围 B：照片、封面、行程安排这一版不在编辑器里改（数据原样保留，阶段 ④ 补）；
//   - 保存 B：边改边存——表单没问题时停手 0.4 秒写进数据、再停手 0.8 秒自动保存（和别处一样）；有问题（名称空、日期反了、坐标无效）
//     时先不写，说明为什么；"放弃这次的修改"恢复成打开时的样子（新建的就删掉）。整次编辑在撤销里算一步；
//   - 搜索结果同时标在地图上（方案 B，界面）；改进 a（点选预览，界面）、e（总里程）、g（新建时第二站自动改成路线）；
//   - 站点名照 Electron 存英文名（PO 没选"用界面语言"）。
// 工作副本是打开时那一份计划的完整 JSON：编辑器只改它管的键，其余（交通住宿、消费、行程安排……）跟着原样写回。
#pragma once

#include "jsonvalue.h"
#include "planedit.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace tp::data { class DataStore; }
namespace tp::map { class MapController; }
namespace tp::services { class Geocoder; class NetClient; class RoadRouter; }

namespace tp::app {

class AppController;

class PlanEditor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(bool isNew READ isNew NOTIFY openChanged)
    Q_PROPERTY(QString planId READ planId NOTIFY openChanged)
    Q_PROPERTY(QVariantMap plan READ plan NOTIFY planChanged)          // 给界面的整份内容（见 planeditor.cpp viewOf）
    Q_PROPERTY(QVariantMap problems READ problems NOTIFY planChanged)  // { name, date, stops, coords: [站点 id] }（已翻译的句子，空 = 没问题）
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY planChanged)        // 改进 e：{ stops, km, approx, driveSec, walkSec, … }
    // 保存状态：saved 已保存 / pending 还没存（马上存）/ waiting 新计划还没填好（名称 + 至少一站）/ invalid 有问题先不存 / readonly
    Q_PROPERTY(QString saveState READ saveState NOTIFY saveStateChanged)
    Q_PROPERTY(QString saveNote READ saveNote NOTIFY saveStateChanged)      // 为什么先不存（已翻译；saved / pending 时为空）
    Q_PROPERTY(bool pickMode READ pickMode WRITE setPickMode NOTIFY pickModeChanged)
    Q_PROPERTY(int activeStop READ activeStop WRITE setActiveStop NOTIFY activeStopChanged)   // 面板里展开 / 地图上变大的那一站（-1 = 没有）
    // 地名搜索：idle / busy / done / empty / error
    Q_PROPERTY(QString searchState READ searchState NOTIFY searchChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY searchChanged)     // [{ name, address, lat, lng }]
    Q_PROPERTY(int hoverResult READ hoverResult WRITE setHoverResult NOTIFY hoverResultChanged)
    Q_PROPERTY(QString typeNote READ typeNote NOTIFY planChanged)           // 改进 g：自动改成路线后的提示（空 = 没有）

public:
    PlanEditor(AppController *app, data::DataStore *store, map::MapController *mapc, QObject *parent = nullptr);

    void setNet(services::NetClient *net) { m_net = net; }
    void setRoads(services::RoadRouter *roads) { m_roads = roads; }
    void setGeocoder(services::Geocoder *g) { m_geo = g; }
    // 测试用：替换 OSRM 请求（和 RoadRouter::Fetch 一样的签名）
    using Fetch = std::function<void(const QUrl &, std::function<void(int httpStatus, const QByteArray &body, int netError)>)>;
    void setFetchForTest(Fetch f) { m_fetch = std::move(f); }
    // 自检 / 截图：自动写数据前停手的时间（默认 400 ms）、算路前停手的时间（默认 700 ms，Electron scheduleLegRouting）
    void setDelays(int applyMs, int legMs) { m_applyTimer.setInterval(applyMs); m_legTimer.setInterval(legMs); }

    bool isOpen() const { return m_open; }
    bool isNew() const { return m_isNew; }
    QString planId() const { return m_open ? m_plan[QStringLiteral("id")].toString() : QString(); }
    QVariantMap plan() const { return m_view; }
    QVariantMap problems() const { return m_problemsView; }
    QVariantMap stats() const { return m_statsView; }
    QString saveState() const { return m_saveState; }
    QString saveNote() const { return m_saveNote; }
    bool pickMode() const { return m_pick; }
    void setPickMode(bool on);
    int activeStop() const { return m_active; }
    void setActiveStop(int i);
    QString searchState() const { return m_searchState; }
    QString searchError() const { return m_searchError; }
    QVariantList results() const { return m_results; }
    int hoverResult() const { return m_hoverResult; }
    void setHoverResult(int i) { if (i != m_hoverResult) { m_hoverResult = i; emit hoverResultChanged(); } }
    QString typeNote() const { return m_typeNote; }
    const json::Value &working() const { return m_plan; }

    // ---- 打开 / 关闭 ----
    // 返回空 = 打开了；否则是不能打开的原因（已翻译，QML 弹提示）
    Q_INVOKABLE QString openNew();
    Q_INVOKABLE QString openPlan(const QString &planId);
    // "完成"：表单没问题 → 存好、关闭，返回 { ok: true, planId }；有问题 → 不关，返回 { ok: false, reason }（QML 问要不要放弃）。
    // 新建的计划什么都没填 → 直接关（什么都不存）
    Q_INVOKABLE QVariantMap finish(bool stay = false);   // 1.2.0 stay：存好但不关（AI 应用修改时编辑页原地不动）
    bool reload(const QString &planId);                  // 1.2.0 AI 应用后从数据重新读这个计划（还开着同一个就原地刷新）
    // "放弃这次的修改"：恢复成打开时的样子（新建的删掉）、保存，撤销栈回到打开之前；关闭
    Q_INVOKABLE void discard();
    // 完成时问"有多个站点但类型是单一目的地，要改成路线吗"选了"保持"（Electron 类型确认）
    Q_INVOKABLE void keepSingle();
    // 框住正在编辑的计划（打开后 QML 等面板让开再调；Electron openEditor 的 fitBoundsOf(validStops)）
    Q_INVOKABLE void fitPlan();
    // 删除计划（QML 先确认）：从数据里删掉、保存，撤销是一步（"已删除，撤销"）；关闭。返回计划名
    Q_INVOKABLE QString deletePlan();

    // ---- 计划 ----
    Q_INVOKABLE void setField(const QString &key, const QString &value);   // name / type / status / startDate / endDate / notes / markerMode
    Q_INVOKABLE void toggleRegion(const QString &region);
    Q_INVOKABLE void applyRegionSuggestion();
    Q_INVOKABLE void setPlanStyle(const QString &color, const QString &shape, const QString &icon);   // 空 = 用默认

    // ---- 站点 ----
    Q_INVOKABLE int addStop(const QString &name, double lat, double lng);   // 返回新下标（展开它）
    Q_INVOKABLE int addStopAtCenter();                                       // Electron addManualStop：地图中心、名字空
    Q_INVOKABLE void removeStop(int i);
    Q_INVOKABLE void moveStop(int from, int to);
    Q_INVOKABLE void setStopField(int i, const QString &key, const QString &value);   // name / arriveDate / departDate / notes
    Q_INVOKABLE void setStopLatLng(int i, double lat, double lng);                      // 拖动标记松手
    // 手动输入坐标：文字 → 数字（Electron +e.target.value：空 = 0）；返回 false = 不是数字
    Q_INVOKABLE bool setStopCoord(int i, const QString &lat, const QString &lng);
    Q_INVOKABLE void setStopStyle(int i, const QString &color, const QString &shape, const QString &icon);   // 空 = 跟随计划
    Q_INVOKABLE void resetStopStyle(int i);
    Q_INVOKABLE void setLegMode(int i, const QString &mode);
    Q_INVOKABLE void retryLeg(int i);
    Q_INVOKABLE void flyToStop(int i);
    // 0.10.0 行程安排（和计划页共用 plans::putSchedule）：j < 0 = 新加；返回空 = 改好了，否则是原因的键
    Q_INVOKABLE QString saveSched(int i, int j, const QString &date, const QString &time, const QString &end, const QString &text);
    Q_INVOKABLE bool deleteSched(int i, int j);

    // ---- 加站点：地名搜索、在地图上点选 ----
    Q_INVOKABLE void search(const QString &q);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE int addResult(int i);
    Q_INVOKABLE void pickAt(double lat, double lng);   // 点选模式下点了地图：加一站、反查地名

    // ---- 给样式选择器的数据（Electron PRESET_COLORS / SHAPES / ICON_GROUPS，已翻译） ----
    Q_INVOKABLE QStringList presetColors() const;
    Q_INVOKABLE QVariantList iconGroups() const;   // [{ group, icons: [{ name, label, cp }] }]
    Q_INVOKABLE QString statusColor(const QString &status) const;
    Q_INVOKABLE QString iconChar(const QString &name) const;   // 图标名 → 字体里的字符（没有返回空）

    // 截图 / 自检：直接放进一组搜索结果（不联网）
    Q_INVOKABLE void setResultsForTest(const QVariantList &rows) { m_results = rows; m_searchError.clear(); m_searchState = rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("done"); emit searchChanged(); showResults(); }
    // 自检用：现在正在算 / 等着算的段数
    int legsPending() const;
    bool legBusy() const { return m_legBusy; }

signals:
    void openChanged();
    void planChanged();
    void saveStateChanged();
    void pickModeChanged();
    void activeStopChanged();
    void searchChanged();
    void hoverResultChanged();
    void toast(const QString &text, bool warn);   // 已翻译
    void finished(const QString &planId);          // 关闭后（QML 打开它的简易面板）
    void legDone(const QString &key, bool ok);

private:
    void showResults();
    QString openWith(const json::Value &plan, bool isNew);
    void close();
    bool pristine() const;
    void changed(bool structural = true);   // 工作副本变了：刷新界面、草稿、排队写数据、排队算路
    void refreshView();
    void refreshSaveState();
    void applyNow();                        // 写进数据（表单没问题时）
    void scheduleLegs();
    void runLegs();
    void onLegResult(const QString &stopId, const QString &key, const QString &mode, int status, const QByteArray &body, int netError);
    QString newId(const QString &prefix) const;
    QString trd(const char *s) const;

    AppController *m_app;
    data::DataStore *m_store;
    map::MapController *m_mapc;
    services::NetClient *m_net = nullptr;
    services::RoadRouter *m_roads = nullptr;
    services::Geocoder *m_geo = nullptr;
    Fetch m_fetch;

    bool m_open = false, m_isNew = false, m_inStore = false;
    json::Value m_plan;                    // 工作副本
    json::Value m_original;                // 打开时的样子（新建 = null）
    json::Value m_originalPhotos;          // 打开时的照片库（放弃修改时退回说明和归属）
    bool m_autoRegion = false;             // Electron editor.autoRegion：没有区域时跟着站点自动建议，手动点过就不再自动
    bool m_typeAuto = false;               // 改进 g 已经自动改过一次（之后用户改回单一目的地就不再改）
    bool m_singleAsked = false;            // 完成时已经问过"要改成路线吗"
    bool m_nameTouched = false;            // 新计划：名称框碰过以后才标红"请填写计划名称"
    QString m_saveNote;
    QString m_typeNote;
    QVariantMap m_view, m_problemsView, m_statsView;
    QString m_saveState = QStringLiteral("saved");
    bool m_pick = false;
    int m_active = -1;

    QTimer m_applyTimer;
    // 编辑时自动算单段（Electron legState / runLegRouting）：站点 id → { status: loading / fail, key, msg }
    struct LegState { QString status, key, msg; };
    QHash<QString, LegState> m_legState;
    QTimer m_legTimer;
    bool m_legBusy = false;
    int m_legGen = 0;                      // 关闭 / 重新打开后丢掉还在路上的结果

    QString m_searchState = QStringLiteral("idle"), m_searchError;
    QVariantList m_results;
    int m_hoverResult = -1;
    int m_searchGen = 0;
};

} // namespace tp::app
