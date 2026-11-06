// 文字按钮（0.8.7）：主色加粗的一小段字，悬停加下划线（Electron .mf-link / .f-only）
import QtQuick
import TPApp

Text {
    id: root
    property bool muted: false
    property bool small: false
    property color tone: muted ? Theme.muted : Theme.accentInk
    signal clicked()
    font.family: serifFamily
    font.pixelSize: Theme.px(small ? 12 : 13)
    font.bold: true
    font.underline: ma.containsMouse
    color: tone
    Accessible.role: Accessible.Button
    Accessible.name: text
    activeFocusOnTab: true
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()
    MouseArea {
        id: ma
        anchors.fill: parent
        anchors.margins: -Theme.px(4)
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
