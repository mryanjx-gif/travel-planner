// 空状态（0.12.7）：插画、这里会有什么、一个能点的动作。按钮的 objectName = 自己的 objectName + "Btn"（自检用）。
import QtQuick
import TPApp

Item {
    id: root
    property string art: ""
    property string title: ""
    property string detail: ""
    property string action: ""
    signal acted()
    implicitHeight: col.implicitHeight

    Column {
        id: col
        width: Math.min(root.width, Theme.px(360))
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.px(6)
        EmptyArt { name: root.art; anchors.horizontalCenter: parent.horizontalCenter }
        Text {
            width: parent.width
            topPadding: Theme.px(4)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.title
            font.family: serifFamily; font.pixelSize: Theme.px(16); font.bold: true; color: Theme.text
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            lineHeight: 1.35
            text: root.detail
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
        }
        Item { width: 1; height: Theme.px(6) }
        TpButton {
            objectName: root.objectName + "Btn"
            anchors.horizontalCenter: parent.horizontalCenter
            kind: "primary"
            compact: true
            text: root.action
            onClicked: root.acted()
        }
    }
}
