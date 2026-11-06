// 原型用的玻璃面板按钮：选中时是一颗白色胶囊（正式样式在阶段 ⑥ 统一做）
import QtQuick

Item {
    id: pill
    property alias text: label.text
    property bool selected: false
    signal clicked()
    implicitWidth: label.implicitWidth + 30
    implicitHeight: 38

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: pill.selected ? "#fdfefe" : (area.containsMouse ? "#80ffffff" : "transparent")
        border.color: pill.selected ? "#dbe3e9" : "transparent"
    }
    Text {
        id: label
        anchors.centerIn: parent
        font.family: serifFamily
        font.pixelSize: 15
        font.bold: pill.selected
        color: pill.selected ? "#1f4a70" : "#16202a"
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: pill.clicked()
    }
}
