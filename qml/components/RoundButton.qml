// 圆形玻璃按钮（0.8.8）：顶栏的 ↩ 回到地图、右上的 地球 / 平面。Electron .mf-round（40 px 圆、细边、轻阴影）。
import QtQuick
import TPApp

Item {
    id: root
    property string icon: ""
    property color iconColor: Theme.text
    property bool on: false            // 选中 / 打开时图标用强调色
    property string tip: ""
    property string label: ""          // 不用图标，显示一两个字（AI 泡泡）
    property bool solid: false         // 下面不是地图时（整页上）不折射、接近实底
    signal clicked()
    width: Theme.px(40)
    height: width
    Accessible.role: Accessible.Button
    Accessible.name: tip

    GlassPanel {
        anchors.fill: parent
        radius: width / 2
        liquid: !root.solid
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass && !root.solid ? 0.69 : 0.96)
        edge: ma.containsMouse ? Theme.lineStrong : Theme.glassEdge
        scale: ma.pressed ? 0.9 : 1
        Behavior on scale { TpSpring {} }
        TpIcon {
            visible: root.label === ""
            anchors.centerIn: parent
            name: root.icon
            color: root.on ? Theme.accent : root.iconColor
        }
        Text {
            visible: root.label !== ""
            anchors.centerIn: parent
            text: root.label
            font.family: serifFamily; font.pixelSize: Theme.px(15); font.bold: true
            color: root.iconColor
        }
    }
    TpTip { text: root.tip; show: ma.containsMouse }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
