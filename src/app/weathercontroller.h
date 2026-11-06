// 天气预报和往年同月气候（0.10.3，N-01、N-02、L2；QML 里是 weather）。计算在 plans（planweather.h），这里管联网和缓存。
// 缓存在 cache\weather.json，不改数据：预报同一地点 3 小时内不再问（一天后丢掉），气候长期留着。
// 预报：打开计划页、此刻卡在路上时才取；失败时什么都不显示，10 分钟内不重试。气候：只在点按钮时取，每次打开计划要再点。
// 开关都走 NetClient 的 weather（离线总开关 + 单项；Open-Meteo 的预报、气候、海拔共用这一项）。
#pragma once

#include "planweather.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <functional>

namespace tp::data { class DataStore; }
namespace tp::services { class NetClient; }

namespace tp::app {

class PlanController;
class PlanPageController;

class WeatherController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList stops READ stops NOTIFY changed)         // 打开的计划每个有效站点一项 { wx?, clim? }
    Q_PROPERTY(QVariantMap today READ today NOTIFY changed)          // 此刻卡：今天所在站的天气（plans::wxDay），拿不到时为空
    Q_PROPERTY(bool climateAvailable READ climateAvailable NOTIFY changed)
    Q_PROPERTY(bool climateBusy READ climateBusy NOTIFY changed)
    Q_PROPERTY(bool climateShown READ climateShown NOTIFY changed)

public:
    WeatherController(PlanPageController *page, PlanController *plans, data::DataStore *store, services::NetClient *net, QObject *parent = nullptr);

    QVariantList stops() const { return m_stops; }
    QVariantMap today() const { return m_today; }
    bool climateAvailable() const { return m_fetch || allowed(); }
    bool climateBusy() const { return m_climBusy; }
    bool climateShown() const { return m_climOn; }

    void setCacheFile(const QString &path) { m_file = path; loadCache(); }
    Q_INVOKABLE void fetchClimate();
    Q_INVOKABLE void fillDemoForShot();    // 截图用：开发数据的日期不在预报范围里，填几条示例（不写缓存）

    // 测试用：替换请求（不读不写缓存文件），清掉内存里的缓存
    using Fetch = std::function<void(const QUrl &, std::function<void(int httpStatus, const QByteArray &body, int netError)>)>;
    void setFetchForTest(Fetch f);
    int requests() const { return m_requests; }

signals:
    void changed();
    void climateFailed();

private:
    struct Wx { qint64 at = 0; QJsonObject daily; };
    struct ClimTask { int year = 0, month = 0; QVector<map::plans::ClimPoint> points; };
    bool allowed() const;
    void update();
    void requestForecast(const QVector<map::plans::GeoPoint> &points);
    void get(const QUrl &url, std::function<void(int, const QByteArray &, int)> done);
    bool fresh(const QString &key) const;
    const json::Value *nowStop() const;
    void recompute();
    void climNext();
    void loadCache();
    void saveCache() const;

    PlanPageController *m_page;
    PlanController *m_plans;
    data::DataStore *m_store;
    services::NetClient *m_net;
    Fetch m_fetch;
    QString m_file;
    QString m_id;
    QVariantList m_stops;
    QVariantMap m_today;
    QHash<QString, Wx> m_wx;
    QHash<QString, qint64> m_tried;       // 问过的地点（秒）：失败后别一直问
    QHash<QString, QJsonObject> m_clim;
    bool m_climOn = false, m_climBusy = false, m_demo = false;
    QVector<ClimTask> m_climTasks;
    QHash<QString, map::plans::ClimAcc> m_climAcc;
    int m_requests = 0;
};

} // namespace tp::app
