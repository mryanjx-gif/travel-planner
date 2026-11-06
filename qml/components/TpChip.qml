// 小标签按钮（0.10.2）：分账里选人、同行人列表
import QtQuick
import TPApp

Rectangle {
    id: root
    property string text: ""
    property bool on: false
    signal clicked()
    implicitWidth: label.implicitWidth + Theme.px(20)
    implicitHeight: Theme.px(28)
    radius: height / 2
    color: on ? Theme.accent : ma.containsMouse ? Theme.panel2 : "#ffffff"
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? Theme.accentInk : on ? Theme.accent : Theme.line
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.checked: on
    Keys.onSpacePressed: clicked()
    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.family: serifFamily; font.pixelSize: Theme.px(13); color: root.on ? "#ffffff" : Theme.text
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.clicked() }
}
