// 玻璃面板（0.4.0 的简化版）：背后的地图模糊 + 白色半透明 + 白色细边 + 柔和阴影。
// 真正的液态玻璃（折射、高光，移植 Electron liquidglass.js）在阶段 ⑥ 替换这里的实现，用法不变。
// "透明效果"关掉时（Theme.glass = false）改成接近不透明的实底。
import QtQuick
import QtQuick.Effects
import TPApp

Item {
    id: root
    property Item backdrop: null          // 被模糊的那一层（Main.qml 的 scene）
    property real radius: Theme.radius
    property color tint: Theme.glassTint
    property color edge: Theme.glassEdge
    property bool shadow: true
    default property alias content: inner.data

    // 阴影
    RectangularShadow {
        anchors.fill: parent
        visible: root.shadow
        radius: root.radius
        blur: 32
        offset.y: 12
        color: Theme.shadow
        spread: 0
    }

    // 背后的模糊
    Item {
        id: blurBox
        anchors.fill: parent
        visible: Theme.glass && root.backdrop !== null
        layer.enabled: visible
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: maskRect
        }
        ShaderEffectSource {
            id: src
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: false
            sourceRect: {
                // 依赖这些属性，位置变化时重新计算
                root.x; root.y; root.width; root.height; root.parent;
                if (!root.backdrop) return Qt.rect(0, 0, 0, 0)
                const p = root.mapToItem(root.backdrop, 0, 0)
                return Qt.rect(p.x, p.y, root.width, root.height)
            }
        }
        MultiEffect {
            anchors.fill: parent
            source: src
            blurEnabled: true
            blur: 1.0
            blurMax: 48
            saturation: 0.35
        }
    }
    Rectangle {
        id: maskRect
        anchors.fill: parent
        radius: root.radius
        visible: false
        layer.enabled: true
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.tint
        border.width: 1
        border.color: root.edge
    }

    Item {
        id: inner
        anchors.fill: parent
    }
}
