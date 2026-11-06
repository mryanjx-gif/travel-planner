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

public:
    PlanPageController(AppController *app, PlanController *plans, data::DataStore *store, services::NetClient *net,
                       RatesController *rates, Settings *settings, QObject *parent = nullptr);

    QString planId() const { return m_id; }
    QVariantMap info() const { return m_info; }

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
    Q_INVOKABLE void dismissNights();                    // J1"这几晚不用"：记进 settings.json 的 noStayNights，不改数据

    // 测试用：替换 Open-Meteo 请求（和 PlanEditor::Fetch 一样的签名）
    using Fetch = std::function<void(const QUrl &, std::function<void(int httpStatus, const QByteArray &body, int netError)>)>;
    void setFetchForTest(Fetch f) { m_fetch = std::move(f); }
    int tzRequests() const { return m_tzRequests; }

signals:
    void changed();

private:
    void refresh();
    void fillTz(const QString &planId);
    void fillBatch(const QString &planId);
    void edited();
    QString applyPlan(const std::function<QString(json::Value &plan)> &fn);

    AppController *m_app;
    PlanController *m_plans;
    data::DataStore *m_store;
    services::NetClient *m_net;
    RatesController *m_rates;
    Settings *m_settings;
    Fetch m_fetch;
    QString m_id;
    QVariantMap m_info;
    QSet<QString> m_filling;
    int m_tzRequests = 0;
};

} // namespace tp::app
