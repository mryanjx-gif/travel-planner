// 计划页的"游记"标签（0.10.4，P-15；照 Electron pages-plan.js journalHtml）：本计划的游记列表（首图 + 120 字）。0.11.2 加"写一篇"和打开全文（游记整页）。
import QtQuick
import TPApp

Column {
    id: root
    property var rows: []
    property bool readOnly: false
    signal writeRequested()
    signal openRequested(string id)
    spacing: Theme.px(10)

    Item {
        width: parent.width
        height: Theme.px(32)
        Text {
            objectName: "journalCount"
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 篇").arg(root.rows.length)
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
        }
        TpButton {
            objectName: "journalWrite"
            visible: !root.readOnly
            anchors.right: parent.right
            compact: true
            kind: "primary"
            text: qsTr("写一篇")
            onClicked: root.writeRequested()
        }
    }
    Text {
        objectName: "journalEmpty"
        visible: root.rows.length === 0
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("还没有游记。")
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
    }
    Repeater {
        model: root.rows
        delegate: Rectangle {
            required property var modelData
            objectName: "journalRow"
            width: root.width
            height: Math.max(pic.visible ? pic.height : 0, jCol.implicitHeight) + Theme.px(24)
            radius: Theme.px(16)
            color: "#ffffff"
            border.width: 1; border.color: rowMa.containsMouse ? Theme.lineStrong : Theme.line
            Accessible.role: Accessible.Button
            Accessible.name: modelData.title || qsTr("无标题")
            MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openRequested(parent.modelData.id) }
            Image {
                id: pic
                visible: !!parent.modelData.thumb
                x: Theme.px(12); y: Theme.px(12)
                width: Theme.px(112); height: Theme.px(84)
                source: parent.modelData.thumb || ""
                fillMode: Image.PreserveAspectCrop
                clip: true
                asynchronous: true
            }
            Column {
                id: jCol
                x: pic.visible ? pic.x + pic.width + Theme.px(12) : Theme.px(14)
                y: Theme.px(12)
                width: parent.width - x - Theme.px(14)
                spacing: Theme.px(3)
                Text { width: parent.width; elide: Text.ElideRight; text: modelData.title || qsTr("无标题"); font.family: serifFamily; font.pixelSize: Theme.px(14.5); font.bold: true; color: Theme.text }
                Text { visible: !!modelData.date; text: modelData.date || ""; font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted }
                Text { width: parent.width; wrapMode: Text.Wrap; text: modelData.text || ""; font.family: serifFamily; font.pixelSize: Theme.px(13); lineHeight: 1.3; color: Theme.textSoft }
            }
        }
    }
}
