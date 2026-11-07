// 玻璃面板。按真实需求分档（PO 2026-11-07）：
// liquid = true：真液态玻璃（GlassSurface，折射、高光），浮在地图上的面板和泡泡用；遮盖度看 tint。
// liquid = false：整页和带暗幕的弹窗，近乎不透明，不算折射。
// "透明效果"关掉时（Theme.glass = false）都是接近不透明的实底。
import QtQuick
import QtQuick.Effects
import TPApp

Item {
    id: root
    property real radius: Theme.radius
    property color tint: Theme.glassTint
    property color edge: Theme.glassEdge
    property bool shadow: true
    property bool liquid: true
    readonly property bool useLiquid: liquid && Theme.glass && Theme.glassSource !== null
    default property alias content: inner.data

    RectangularShadow {
        anchors.fill: parent
        visible: root.shadow
        radius: root.radius
        blur: 32
        offset.y: 12
        color: Theme.shadow
        spread: 0
    }

    GlassSurface {
        objectName: "glassSurface"
        anchors.fill: parent
        source: Theme.glassSource
        radius: root.radius
        tint: root.tint
        plain: root.useLiquid ? (Theme.liquidOn ? 0 : 1) : 2
    }

    // 玻璃自带亮线：这里只画特别的边（鼠标移上去、警告）
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        visible: !Qt.colorEqual(root.edge, Theme.glassEdge)
        border.width: 1
        border.color: root.edge
    }

    Item {
        id: inner
        anchors.fill: parent
    }
}
