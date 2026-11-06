// 我的位置（0.8.9，功能清单 M-26；Electron mapfirst.js locateMe / showMe / locateFallback）。QML 里是 locator。
//
// 顺序和 Electron v6.1.0 一样：系统定位（6 秒超时，允许 10 分钟内的旧位置）→ 正在进行的旅行今天所在的站点 → "所在国家"。
//   - 系统定位用 Qt Positioning（LGPL-3，技术选型.md §4 已核实）→ Windows 定位服务；Windows"设置 → 隐私和安全性 → 定位"里
//     关了"允许桌面应用访问你的位置"时拿不到。Qt 在没打包成商店应用的程序里能不能拿到位置：不确定，自检报告里写实际结果。
//   - "所在国家"：settings.json 的 homeCountry（ISO 3166 三字母；设置页在阶段 ⑥）；没有时按 Windows 的区域设置猜（PO 0.8.9 选的改进 c，
//     Electron 没设过就当澳大利亚）。
//   - 改进 b（PO 选）：拿到位置时带上精度（米），地图上画一圈范围，提示里写"精度约 3 公里 · 来自 Windows 定位"。
// 不把位置发给任何联网服务（Nominatim 条款：不提交个人数据），也不存。
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

class QGeoPositionInfoSource;

namespace tp::data { class DataStore; }
namespace tp::map { class MapController; }

namespace tp::app {

class Settings;

class Locator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool hasFix READ hasFix NOTIFY fixChanged)       // 地图上显示蓝点
    Q_PROPERTY(double lat READ lat NOTIFY fixChanged)
    Q_PROPERTY(double lng READ lng NOTIFY fixChanged)
    Q_PROPERTY(double accuracy READ accuracy NOTIFY fixChanged) // 米；0 = 不知道

public:
    static constexpr int kTimeoutMs = 6000;   // Electron getCurrentPosition timeout: 6000

    Locator(Settings *settings, data::DataStore *store, map::MapController *mapc, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    bool hasFix() const { return m_hasFix; }
    double lat() const { return m_lat; }
    double lng() const { return m_lng; }
    double accuracy() const { return m_acc; }

    Q_INVOKABLE void locate();
    Q_INVOKABLE void clearFix();

    // 今天的本地日期（"此刻"用的同一个，测试可以假装）
    void setToday(std::function<QString()> f) { m_today = std::move(f); }
    // 自检 / 测试：不用系统定位，直接当作拿到了（acc 米）/ 当作失败（why：权限 / 超时 / 其他）
    Q_INVOKABLE void simulateFix(double lat, double lng, double acc);
    Q_INVOKABLE void simulateError(const QString &why);

    // 系统可用的定位来源（自检报告用）
    static QStringList sources();
    // 所在国家（三字母）：settings 的 homeCountry → Windows 区域设置 → AUS
    QString homeCountry() const;
    static QString iso3FromIso2(const QString &iso2);

signals:
    void busyChanged();
    void fixChanged();
    void toast(const QString &text, bool warn, int ms);   // 已翻译
    void result(const QString &kind);                     // fix / today / country / none（自检用）

private:
    void gotFix(double lat, double lng, double acc);
    void fallback(const QString &why);
    void setBusy(bool b) { if (b != m_busy) { m_busy = b; emit busyChanged(); } }

    Settings *m_settings;
    data::DataStore *m_store;
    map::MapController *m_mapc;
    QGeoPositionInfoSource *m_src = nullptr;
    bool m_srcTried = false;
    bool m_busy = false, m_hasFix = false;
    double m_lat = 0, m_lng = 0, m_acc = 0;
    std::function<QString()> m_today;
    QTimer m_guard;   // 来源本身不报超时时的保险（6 秒 + 1 秒）
};

} // namespace tp::app
