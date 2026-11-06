// 选项卡片：粗体标题 + 一行后果说明。danger = 会覆盖数据的那个选项（淡红底、红字标题）
import QtQuick
import TPApp

Item {
    id: root
    property string title: ""
    property string detail: ""
    property bool danger: false
    signal clicked()
    activeFocusOnTab: true
    implicitHeight: col.implicitHeight + Theme.px(28)
    Accessible.role: Accessible.Button
    Accessible.name: title
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()

    Rectangle {
        anchors.fill: parent
        radius: Theme.px(14)
        color: root.danger ? Qt.rgba(168 / 255, 64 / 255, 28 / 255, area.containsMouse ? 0.10 : 0.06)
                           : (area.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.75))
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.accent : (root.danger ? Qt.rgba(168 / 255, 64 / 255, 28 / 255, 0.35) : Theme.lineStrong)
    }
    Column {
        id: col
        x: Theme.px(16)
        y: Theme.px(14)
        width: root.width - 2 * x
        spacing: Theme.px(4)
        Text {
            width: parent.width
            text: root.title
            wrapMode: Text.Wrap
            font.family: serifFamily
            font.pixelSize: Theme.px(15)
            font.bold: true
            color: root.danger ? Theme.dangerInk : Theme.text
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: root.detail
            wrapMode: Text.Wrap
            lineHeight: 1.25
            font.family: serifFamily
            font.pixelSize: Theme.px(13)
            color: Theme.muted
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
