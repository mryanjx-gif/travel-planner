// 平面地图上的交通小图标（0.9.7，PO 2026-11-06 14:44 定）：选中计划时一个图标按站点顺序把整条行程走一遍，
// 规则和地球上一样（globe::planVehicle / vehicleAt：屏幕上短于 40 像素的段跳过，每段 1.5–5 秒，到站停 0.3 秒，走完停 1 秒）；
// 平面上飞行段不抬起：飞机沿画出来的线走、顺着方向转。位置按平面的相机（MapController::camera）算，QML 里一个 Image 跟着摆。
// 路线用地球那份三维折线（globeRoutes，和平面的线同一组点，只多了细分；方向就是经纬度），不另建一份。
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "globeroutes.h"

namespace tp::map { class MapController; }

class FlatVehicle : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *mapController READ mapController WRITE setMapController NOTIFY changed)
    // QML 给：动效开着、在平面上、在地图页、窗口没最小化
    Q_PROPERTY(bool allowed READ allowed WRITE setAllowed NOTIFY changed)
    Q_PROPERTY(double dpr READ dpr WRITE setDpr NOTIFY changed)
    Q_PROPERTY(bool shown READ shown NOTIFY changed)
    Q_PROPERTY(double x READ x NOTIFY changed)
    Q_PROPERTY(double y READ y NOTIFY changed)
    Q_PROPERTY(double angle READ angle NOTIFY changed)   // 前进方向（度，顺时针，0 = 朝右）
    Q_PROPERTY(bool flip READ flip NOTIFY changed)        // 圆牌：往左走时左右翻
    Q_PROPERTY(QString img READ img NOTIFY changed)       // image://marker/ 后面的描述
    Q_PROPERTY(double size READ size NOTIFY changed)
public:
    explicit FlatVehicle(QObject *parent = nullptr);
    QObject *mapController() const;
    void setMapController(QObject *o);
    bool allowed() const { return m_allowed; }
    void setAllowed(bool on);
    double dpr() const { return m_dpr; }
    void setDpr(double d);
    bool shown() const { return m_shown; }
    double x() const { return m_x; }
    double y() const { return m_y; }
    double angle() const { return m_angle; }
    bool flip() const { return m_flip; }
    QString img() const { return m_img; }
    double size() const { return m_size; }
    // 自检：跳到第 ms 毫秒（不用等动画）；返回 { shown, x, y, img, leg, legs, totalMs }
    Q_INVOKABLE QVariantMap atMs(double ms);

signals:
    void changed();

private:
    void replan();
    void tick();
    void updateTimer();
    const tp::map::globe::RoutePlan *plan() const;

    QPointer<tp::map::MapController> m_mapc;
    bool m_allowed = false, m_shown = false, m_flip = false;
    double m_dpr = 1, m_x = 0, m_y = 0, m_angle = 0, m_size = 26;
    QString m_img, m_planId;
    tp::map::globe::VehiclePlan m_plan;
    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_msOverride = -1;
};
