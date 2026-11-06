// 设置面板里的一节（白色半透明圆角卡片 + 标题）
import QtQuick
import TPApp

Rectangle {
    id: root
    property string title: ""
    default property alias content: col.data
    implicitHeight: col.implicitHeight + Theme.px(28)
    radius: Theme.radius
    color: Qt.rgba(1, 1, 1, 0.62)
    border.width: 1
    border.color: Qt.rgba(219 / 255, 227 / 255, 233 / 255, 0.9)
    Column {
        id: col
        x: Theme.px(16)
        y: Theme.px(14)
        width: root.width - 2 * x
        spacing: Theme.px(14)
        Text {
            text: root.title
            font.family: serifFamily
            font.pixelSize: Theme.px(15)
            font.bold: true
            color: Theme.text
        }
    }
}
