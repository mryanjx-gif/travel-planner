// "已解锁"上色（0.8.0）：照搬 Electron v6.3.0 map-app.js 的 admIndex / findAdmin / computeUnlocked。
//
// 去过的省 / 州 = 已完成计划的站点所在的省 + 手动标记的 visitedRegions。
// 国家按"第一次去"的先后（稳定排序）分色相；同一国家里的省份按加入的先后错开深浅；
// 港澳台、很小的国家整块点亮；面积很小的单位另外画一个圆点。
// 原型（0.2–0.7）用 QHash 遍历，同一天去的地区顺序每次运行可能不同（颜色会变）；这里全部按固定次序，结果和 Electron 一致。
#pragma once

#include "geo.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tp::map {

class UnlockModel {
public:
    // adm1：Natural Earth 省界 FeatureCollection（每个 feature 带 bbox 和 properties.id/name/adm0/country）
    bool load(const QByteArray &adm1Json, QString *error = nullptr);
    bool isLoaded() const { return !m_units.isEmpty(); }
    int featureCount() const { return int(m_units.size()); }

    struct PlanStops {
        QString name, startDate;
        QVector<geo::LatLng> stops;
    };
    struct Manual { QString id; int year = 0; };

    struct Result {
        QJsonObject visit;   // FeatureCollection：去过的省（properties: id, name, country, plans, c）
        QJsonObject dots;    // FeatureCollection：小单位的圆点
        int provinces = 0, countries = 0;
        QStringList countryNames;   // 按色相分配的顺序
        QHash<QString, QString> colorById;   // 测试用：省 id → 颜色
        QHash<QString, QJsonObject> propsById;   // 省 id → properties（悬停提示框用，0.8.2）
        QHash<QString, QStringList> plansById;   // 省 id → 点亮它的计划名（手动标记的是空，0.8.5）
    };
    // completed：已完成计划（按数据里的顺序），manual：visitedRegions（按顺序）
    Result compute(const QVector<PlanStops> &completed, const QVector<Manual> &manual) const;

    // 点在哪个省（Electron findAdmin：先精确判断，没落进任何省时找 0.3° 内最近的边界点）；返回下标，没有 -1
    int findAdmin(double lng, double lat) const;
    // 只做精确判断（悬停提示框：鼠标在哪个省的多边形里），没有 -1
    int unitAt(double lng, double lat) const;
    QString unitId(int i) const { return i >= 0 && i < m_units.size() ? m_units[i].id : QString(); }

    // ---- 手动标记（0.8.5，Electron unitKey / unitName / unitSub / unitFeats / sameUnit / renderUnlockList） ----
    int indexOf(const QString &id) const { return m_byId.value(id, -1); }
    // 显示名：整块点亮的组用组名（香港、新加坡……），否则省名
    QString unitName(int i) const;
    // 副标题的种类：Country = 国家名（country()）、China = "中国"、WholeCountry = "整个国家 / 地区"
    enum class Sub { Country, China, WholeCountry };
    Sub unitSub(int i) const;
    QString country(int i) const { return i >= 0 && i < m_units.size() ? m_units[i].country : QString(); }
    bool sameUnit(int a, int b) const;
    // 同一个单位的全部省（组的每一块；普通省就是它自己）
    QVector<int> unitMembers(int i) const;
    // 这几块的 FeatureCollection（悬停描边用）
    QJsonObject unitFeatures(int i) const;
    // 外框中心（组用整组的外框；截图 / 自检用）
    geo::LatLng unitCenter(int i) const;
    // 搜索：名字、组名、国家名、国家代码里含 q（不分大小写）；同一组只列一次；最多 max 个
    QVector<int> search(const QString &q, int max = 120) const;
    // 一个国家的范围（0.8.9 我的位置退回"所在国家"，Electron locateFallback）：adm0 或 grp 等于 code（ISO 3166 三字母）的
    // 所有省 / 州外框合起来。没有返回 false
    bool countryBox(const QString &code, double *w, double *s, double *e, double *n) const;

    static constexpr double kSmallCountryDeg2 = 1.0;   // SMALL_COUNTRY_DEG2
    static constexpr double kDotDeg2 = 0.6;            // UNLOCK_DOT_DEG2

private:
    struct Ring { QVector<geo::Pt> pts; };   // x = 经度，y = 纬度
    struct Unit {
        QString id, name, adm0, country, grp, gname;
        double bbox[4] = { 0, 0, 0, 0 };
        bool hasBbox = false;
        QVector<QVector<Ring>> polys;        // 每个多边形：外环 + 洞
        bool multi = false;                  // 原来是 MultiPolygon（输出时照原样）
    };
    // 输出用的 geometry（0.8.6：不再留着解析出来的原样 QJsonValue——它会把整份省界的 JSON 结构一直留在内存里，约 46 MB；
    // 要输出时由坐标重新拼，数值和原文一样）
    static QJsonObject geometryOf(const Unit &u);
    static bool inRing(double x, double y, const Ring &r);
    static bool inPoly(double x, double y, const QVector<Ring> &poly);
    QString unitKey(const Unit &u) const { return u.grp.isEmpty() ? u.id : u.grp; }

    QVector<Unit> m_units;
    QHash<QString, int> m_byId;
    QHash<QString, QVector<int>> m_byGroup;
    QHash<QString, QVector<double>> m_groupBox;
};

} // namespace tp::map
