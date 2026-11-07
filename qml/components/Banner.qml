// 顶部横幅：只读模式（不能关）/ 更新版本的 app 写的（能关）/ 底图瓦片连续失败（能关，0.8.5）
import QtQuick
import TPApp

GlassPanel {
    id: root
    property string title: ""
    property string detail: ""
    property bool warning: false
    property bool closable: true
    default property alias actions: actionRow.data
    signal closeClicked()

    height: Math.max(row.implicitHeight, actionRow.implicitHeight) + Theme.px(28)
    tint: warning ? Qt.rgba(253 / 255, 246 / 255, 240 / 255, Theme.glass && liquid ? 0.9 : 0.99) : Theme.glassTint
    edge: warning ? Qt.rgba(194 / 255, 86 / 255, 15 / 255, 0.28) : Theme.glassEdge

    Row {
        id: row
        x: Theme.px(18)
        anchors.verticalCenter: parent.verticalCenter
        width: root.width - x - Theme.px(14) - actionRow.width - Theme.px(12)
        spacing: Theme.px(12)
        Text {
            text: root.warning ? "⚠" : "ⓘ"
            font.pixelSize: Theme.px(18)
            color: root.warning ? Theme.warn : Theme.accent
        }
        Column {
            width: parent.width - Theme.px(30)
            spacing: Theme.px(3)
            Text {
                width: parent.width
                text: root.title
                wrapMode: Text.Wrap
                font.family: serifFamily
                font.pixelSize: Theme.px(15)
                font.bold: true
                color: Theme.text
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
    }
    Row {
        id: actionRow
        anchors.right: parent.right
        // 能关的横幅右边还有 ×（0.8.5：瓦片失败横幅第一次同时有按钮和 ×，以前两个叠在一起）
        anchors.rightMargin: root.closable ? Theme.px(10 + 34 + 6) : Theme.px(14)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.px(8)
    }
    TpButton {
        visible: root.closable
        text: "×"
        kind: "ghost"
        compact: true
        width: Theme.px(34)
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(10)
        anchors.verticalCenter: parent.verticalCenter
        Accessible.name: qsTr("关闭")
        tip: qsTr("关闭")
        onClicked: root.closeClicked()
    }
}
