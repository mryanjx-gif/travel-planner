// 内容卡片（0.12.2）：连续轻圆角（超椭圆 n = 2.6，延伸约 1.25 倍半径），没有边框，靠底色和面板分层。
// 用法和 Rectangle 一样（color、radius、放子项）。形状是预先算好的贴图（src/map/glasssurface.cpp 的 CardImages），长列表开销和普通图片一样。
import QtQuick
import TPApp

Item {
    id: root
    property color color: Theme.card
    property real radius: Theme.radiusSmall
    // 贴图按 S 倍画再缩回来，高分屏上转角也清楚
    readonly property int s: Math.max(1, Math.ceil(Screen.devicePixelRatio))
    readonly property int edge: Math.ceil(Math.round(radius) * 1.25) * s + 1

    BorderImage {
        z: -1
        width: root.width * root.s
        height: root.height * root.s
        scale: 1 / root.s
        transformOrigin: Item.TopLeft
        source: "image://card/" + Math.round(root.radius) + "/" + root.s + "/" + String(root.color).substring(1)
        border { left: root.edge; right: root.edge; top: root.edge; bottom: root.edge }
    }
}
