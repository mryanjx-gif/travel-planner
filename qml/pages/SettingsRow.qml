// 设置面板的一行：左边名称（可带一行说明），右边控件
import QtQuick
import TPApp

Item {
    id: root
    property string label: ""
    property string sub: ""
    default property alias control: right.data
    implicitHeight: Math.max(left.implicitHeight, right.childrenRect.height)
    Column {
        id: left
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(Theme.px(80), root.width - right.childrenRect.width - Theme.px(12))
        spacing: Theme.px(2)
        Text {
            width: parent.width
            text: root.label
            elide: Text.ElideRight
            font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
        }
        Text {
            width: parent.width
            visible: root.sub !== ""
            text: root.sub
            wrapMode: Text.Wrap
            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
        }
    }
    Item {
        id: right
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width
        height: childrenRect.height
    }
}
