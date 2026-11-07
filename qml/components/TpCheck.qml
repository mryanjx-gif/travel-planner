// 勾选框（0.7.0）：方框 + 文字，点哪都能切换。asSwitch（0.11.5）= 画成开关，用法和键盘操作不变
import QtQuick
import TPApp

Item {
    id: root
    property bool checked: false
    property string text: ""
    property bool asSwitch: false
    signal toggled(bool checked)
    implicitHeight: Math.max(Theme.px(24), label.visible ? label.contentHeight + Theme.px(4) : 0)
    implicitWidth: box.width + (text !== "" ? Theme.px(7) + label.implicitWidth : 0)
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.5
    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checked: checked
    Keys.onSpacePressed: toggled(!checked)

    Rectangle {
        id: box
        width: Theme.px(root.asSwitch ? 34 : 18)
        height: Theme.px(root.asSwitch ? 20 : 18)
        y: label.lineCount > 1 ? Theme.px(3) : (root.height - height) / 2
        radius: root.asSwitch ? height / 2 : Theme.px(5)
        color: root.checked ? Theme.accent : root.asSwitch ? Theme.lineStrong : Qt.rgba(1, 1, 1, 0.85)
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.accentInk : (root.checked ? Theme.accent : Theme.lineStrong)
        Text {
            anchors.centerIn: parent
            visible: root.checked && !root.asSwitch
            text: "✓"
            font.pixelSize: Theme.px(13)
            font.bold: true
            color: "#ffffff"
        }
        Rectangle {
            visible: root.asSwitch
            width: parent.height - Theme.px(4)
            height: width
            radius: width / 2
            x: root.checked ? parent.width - width - Theme.px(2) : Theme.px(2)
            y: Theme.px(2)
            color: "#ffffff"
            Behavior on x { enabled: Theme.motion; TpSpring {} }
        }
    }
    Text {
        id: label
        visible: root.text !== ""
        anchors.left: box.right
        anchors.leftMargin: Theme.px(7)
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, root.width - box.width - Theme.px(7))
        wrapMode: Text.Wrap
        text: root.text
        font.family: serifFamily
        font.pixelSize: Theme.px(13)
        color: Theme.text
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
