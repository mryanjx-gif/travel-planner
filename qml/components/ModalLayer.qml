// 对话框的外壳：铺满窗口的浅色遮罩 + 居中的玻璃卡片，进场淡入 + 轻微放大（遵守"动效"设置）。
// 打开时拦住下面的鼠标；Esc 触发 dismissed（由使用方决定 Esc 等于哪个按钮）。
import QtQuick
import TPApp

Item {
    id: root
    property bool open: false
    property Item backdrop: null
    property real cardWidth: 460
    property bool dismissible: true     // Esc / 点遮罩能不能关
    default property alias content: col.data
    signal dismissed()

    anchors.fill: parent
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    z: 100
    Behavior on opacity { NumberAnimation { duration: Theme.durPanel; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curvePanel } }

    function takeFocus() { focusScope.forceActiveFocus() }   // 上面一层关了时拿回焦点（和 Esc）
    onOpenChanged: if (open) focusScope.forceActiveFocus()

    Rectangle { anchors.fill: parent; color: Theme.scrim }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: if (root.dismissible) root.dismissed()
        onWheel: (wheel) => wheel.accepted = true
    }

    FocusScope {
        id: focusScope
        anchors.fill: parent
        Keys.onEscapePressed: if (root.dismissible) root.dismissed()

        GlassPanel {
            id: card
            backdrop: root.backdrop
            liquid: false
            tint: Qt.rgba(Theme.glassTint.r, Theme.glassTint.g, Theme.glassTint.b, 0.97)
            width: Math.min(Theme.px(root.cardWidth), root.width - 32)
            height: col.implicitHeight + Theme.px(48)
            anchors.centerIn: parent
            scale: root.open ? 1 : (Theme.motion ? 0.96 : 1)
            Behavior on scale { NumberAnimation { duration: Theme.durPanel; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curvePanel } }
            MouseArea { anchors.fill: parent }   // 点卡片本身不关

            Column {
                id: col
                x: Theme.px(26)
                y: Theme.px(24)
                width: card.width - 2 * x
                spacing: Theme.px(14)
            }
        }
    }
}
