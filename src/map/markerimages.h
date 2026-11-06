// 地图标记的图片（0.8.2）：QPainter 按 Electron v6.3.0 的 CSS / SVG 画（base.css .mk / .mk-pin / .cluster-icon / .mk-focus），
// 交给 QML 的 image://marker/<描述>。同一个描述 Qt 只请求一次（图片缓存），几百个标记也只画几十张。
//
// 描述（MarkerImages::stopId / clusterId / glowId 生成，QML 不用自己拼）：
//   s/<外形>/<颜色>/<想去 0|1>/<圆点 0|1>/<图标码位十六进制或 ->/<文字或 ->/<倍率>
//   c/<大小>/<背景色>/<字色>/<数字>/<倍率>
//   g/<宽>/<高>/<水滴 0|1>/<倍率>        聚焦站点底下的光晕（只是渐变，呼吸动画在 QML）
//   v/<交通方式>/<颜色>/<倍率>            0.9.5 地球上选中计划时走的交通小图标（飞行：俯视飞机，朝右；其他：白色小圆牌 + 图标）
// 颜色、文字都做了百分号编码。图片四周留 kPad 逻辑像素给阴影，QML 摆放时减掉。
#pragma once

#include "markers.h"

#include <QImage>
#include <QQuickImageProvider>
#include <QString>

namespace tp::map {

class MarkerImages : public QQuickImageProvider {
public:
    static constexpr int kPad = 8;          // 阴影留白（逻辑像素）
    static constexpr int kGlowInset = 16;   // .mk-focus::after inset: -16px（水滴：上 12、左右 16、下 4）

    MarkerImages();
    // 数字用的字体（雾蓝：--sans = 思源宋体）；图标字体（Material Symbols Rounded）
    static void setFonts(const QString &textFamily, const QString &iconFamily);
    static QString iconFamily();
    static bool iconReady();

    static QString stopId(const markers::Style &st, bool wish, double dpr, bool draft = false);   // draft：编辑中（虚线边，0.8.9）
    static QString clusterId(int size, const QString &bg, const QString &fg, int n, double dpr);
    static QString glowId(double w, double h, bool pin, double dpr);
    // 0.9.5：交通小图标（没有阴影留白；飞机 26×26、圆牌 24×24 逻辑像素）
    static constexpr int kVehicleSize = 26, kBadgeSize = 24;
    static QString vehicleId(const QString &mode, const QString &color, double dpr);
    static QImage paintVehicle(const QString &mode, const QString &color, double dpr);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    // 单元测试 / 自检也能直接用
    static QImage paintStop(const markers::Style &st, bool wish, double dpr, bool draft = false);
    static QImage paintCluster(int size, const QString &bg, const QString &fg, int n, double dpr);
    static QImage paintGlow(double w, double h, bool pin, double dpr);
};

} // namespace tp::map
