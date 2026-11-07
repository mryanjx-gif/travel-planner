// 完整计划页（0.10.0，P-01–P-06、P-18）。QML 里是 planPage（qml/pages/PlanPage.qml）。
// 计算都在 tp::map::plans（planpage.h）；这里负责：当前打开哪个计划、数据变了刷新、改数据后自动保存（停手 0.8 秒）、打开时补时区。
#pragma once

#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantMap>
#include <functional>

namespace tp::data { class DataStore; }
namespace tp::services { class NetClient; }
namespace tp::json { class Value; }

namespace tp::app {

class AppController;
class PlanController;
class RatesController;
class Settings;

class PlanPageController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString planId READ planId NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)      // plans::pageInfo + 缩略图地址 + readOnly（打开的计划被删掉时为空）
    Q_PROPERTY(QVariantMap ledger READ ledger NOTIFY ledgerChanged)   // 0.10.2 账本（plans::ledgerInfo）；只在账本页开着时算

public:
    PlanPageController(AppController *app, PlanController *plans, data::DataStore *store, services::NetClient *net,
                       RatesController *rates, Settings *settings, QObject *parent = nullptr);

    QString planId() const { return m_id; }
    QVariantMap info() const { return m_info; }
    QVariantMap ledger() const { return m_ledger; }

    Q_INVOKABLE void show(const QString &planId);   // 空 = 关闭

    // 日程（k = 有效站点的下标，j < 0 = 新加）。返回空 = 改好了；否则是原因的键（plans::putSchedule）
    Q_INVOKABLE QString saveSched(int k, int j, const QString &date, const QString &time, const QString &end, const QString &text);
    Q_INVOKABLE bool deleteSched(int k, int j);
    Q_INVOKABLE QString setDays(const QString &text);
    Q_INVOKABLE bool startPlanning(const QString &start, const QString &end);

    // 交通住宿和消费（0.10.1）。id 空 = 新加。返回空 = 改好了；否则是原因的键（plans::putBooking 等）
    Q_INVOKABLE QString saveBooking(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE int deleteBooking(const QString &id);    // 连带删了几笔消费；没删成 -1
    Q_INVOKABLE QString saveExpense(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool deleteExpense(const QString &id);
    Q_INVOKABLE QString setBudget(const QString &text, const QString &currency);
    Q_INVOKABLE QString addPerson(const QString &name, const QString &meName);   // 0.11.5 一次加人（含自动平摊）算一步撤销（plans::addPerson）
    Q_INVOKABLE QString editPerson(const QString &id, const QString &name, bool remove);   // 0.10.2 同行人改名、删除（plans::editPerson）
    Q_INVOKABLE void showLedger(bool on);                // 账本页打开 / 关上
    Q_INVOKABLE QString setElev(int k, const QString &text);   // 0.10.3 空 = 去掉并重新取；否则是原因的键（plans::setStopElev）
    // 行李与待办（0.10.4）。key = packing / todos
    Q_INVOKABLE QStringList splitLines(const QString &text) const;     // M4
    Q_INVOKABLE int addChecks(const QString &key, const QString &text);   // 加了几条；只读时 -1
    Q_INVOKABLE bool setCheckDone(const QString &key, const QString &id, bool done);
    Q_INVOKABLE bool setCheckDue(const QString &id, const QString &due);
    Q_INVOKABLE bool removeCheck(const QString &key, const QString &id);
    Q_INVOKABLE int addFromTemplate();                                 // 加了几条；只读时 -1
    Q_INVOKABLE int saveTemplate();                                    // 存了几项
    // 紧急信息（plans::setLocalEmergency、setEmergency）
    Q_INVOKABLE QString saveLocalEmergency(const QVariantMap &phones);
    Q_INVOKABLE QString saveEmergency(const QVariantMap &fields);
    // 设置页（0.12.4）：紧急信息、证件、手动汇率表。不依赖打开的计划
    Q_INVOKABLE QVariantMap emergencyInfo() const;
    Q_INVOKABLE QVariantList documents() const;                        // plans::documentsInfo
    Q_INVOKABLE QVariantList documentPlanOptions(const QString &keepId) const;
    Q_INVOKABLE QString saveDocument(int index, const QString &id, const QVariantMap &fields);   // 空 = 改好了；readOnly / gone
    Q_INVOKABLE QString deleteDocument(int index, const QString &id);
    Q_INVOKABLE QVariantList rateRows(const QStringList &extra) const;
    // 攻略。planId 跟着调用方：停手保存时可能已经换了计划（写完马上关掉）
    Q_INVOKABLE QString setGuide(const QString &planId, const QString &text);
    Q_INVOKABLE bool toggleGuideTask(int line, bool on);
    Q_INVOKABLE QVariantMap guideBlocks(const QString &text) const;    // plans::guideBlocks + 缩略图地址（预览用）
    Q_INVOKABLE QVariantList planPhotos() const;                       // 这个计划的照片（插入照片）
    Q_INVOKABLE void seedKitForShot();                                 // 截图用：在数据副本上加示例游记、紧急信息
    Q_INVOKABLE void seedPhotosForShot(const QString &planId = {});    // 截图和自检用：借库里的照片放到前三站（副本），一张精选、一张有说明。planId 空 = 当前计划
    Q_INVOKABLE void dismissNights();                    // J1"这几晚不用"：记进 settings.json 的 noStayNights，不改数据

    // 测试用：替换 Open-Meteo 请求（和 PlanEditor::Fetch 一样的签名）
    using Fetch = std::function<void(const QUrl &, std::function<void(int httpStatus, const QByteArray &body, int netError)>)>;
    void setFetchForTest(Fetch f) { m_fetch = std::move(f); }
    int tzRequests() const { return m_tzRequests; }

signals:
    void changed();
    void ledgerChanged();
    void contentChanged();     // 数据变了（设置页的证件、紧急信息、手动汇率表跟着刷新）

private:
    void refresh();
    void fillTz(const QString &planId);
    void fillBatch(const QString &planId);
    void fillElev(const QString &planId);    // 0.10.3 只补空的海拔
    void edited();
    QString applyPlan(const std::function<QString(json::Value &plan)> &fn);
    QString applyRoot(const std::function<QString(json::Value &root)> &fn);

    AppController *m_app;
    PlanController *m_plans;
    data::DataStore *m_store;
    services::NetClient *m_net;
    RatesController *m_rates;
    Settings *m_settings;
    Fetch m_fetch;
    QString m_id;
    QVariantMap m_info;
    QVariantMap m_ledger;
    bool m_ledgerOn = false;
    QSet<QString> m_filling, m_fillingElev;
    int m_tzRequests = 0;
};

} // namespace tp::app
