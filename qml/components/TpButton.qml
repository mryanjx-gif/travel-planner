// 按钮：primary 主色实底 / secondary 白底细边 / dangerOutline 红字细边 / danger 红色实底 / ghost 透明
import QtQuick
import TPApp

Item {
    id: root
    property string text: ""
    property string kind: "secondary"
    property real fontSize: 14
    property bool compact: false
    property bool done: false     // 文字前面画一笔对勾
    property string tip: ""       // 只有符号的按钮（×）用
    signal clicked()
    activeFocusOnTab: true
    implicitHeight: Theme.px(compact ? 32 : 40)
    implicitWidth: label.implicitWidth + Theme.px(compact ? 24 : 34) + (done ? doneMark.width + Theme.px(5) : 0)
    opacity: enabled ? 1 : 0.5
    Accessible.role: Accessible.Button
    Accessible.name: text
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusControl
        color: root.kind === "primary" ? (area.pressed ? Theme.accentInk : Theme.accent)
             : root.kind === "danger" ? (area.pressed ? Theme.dangerInk : Theme.danger)
             : root.kind === "ghost" ? (area.containsMouse ? Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.06) : "transparent")
             : (area.pressed ? "#eef2f5" : (area.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.72)))
        border.width: (root.kind === "primary" || root.kind === "danger" || root.kind === "ghost") ? 0 : 1
        border.color: root.kind === "dangerOutline" ? Qt.rgba(168 / 255, 64 / 255, 28 / 255, 0.4) : Theme.lineStrong
        Rectangle {   // 键盘焦点
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            opacity: root.activeFocus ? 0.5 : 0
        }
    }
    Text {
        id: label
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: root.done ? (doneMark.width + Theme.px(5)) / 2 : 0
        text: root.text
        font.family: serifFamily
        font.pixelSize: Theme.px(root.compact ? 13 : root.fontSize)
        color: (root.kind === "primary" || root.kind === "danger") ? "#ffffff"
             : root.kind === "dangerOutline" ? Theme.dangerInk : Theme.text
    }
    TpDone {
        id: doneMark
        anchors.right: label.left
        anchors.rightMargin: Theme.px(5)
        anchors.verticalCenter: parent.verticalCenter
        on: root.done
        stroke: root.kind === "primary" || root.kind === "danger" ? "#ffffff" : Theme.accent
    }
    TpTip { text: root.tip; show: area.containsMouse }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
