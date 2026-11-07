// 圆形玻璃按钮（0.8.8）：顶栏的 ↩ 回到地图、右上的 地球 / 平面。Electron .mf-round（40 px 圆、细边、轻阴影）。
import QtQuick
import TPApp

Item {
    id: root
    property string icon: ""
    property color iconColor: Theme.text
    property bool on: false            // 选中 / 打开时图标用强调色
    property string tip: ""
    signal clicked()
    width: Theme.px(40)
    height: width
    Accessible.role: Accessible.Button
    Accessible.name: tip

    GlassPanel {
        anchors.fill: parent
        radius: width / 2
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.69 : 0.96)
        edge: ma.containsMouse ? Theme.lineStrong : Theme.glassEdge
        scale: ma.pressed ? 0.9 : 1
        Behavior on scale { TpSpring {} }
        TpIcon {
            anchors.centerIn: parent
            name: root.icon
            color: root.on ? Theme.accent : root.iconColor
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
