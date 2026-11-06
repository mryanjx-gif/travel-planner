// 计划页的"游记"标签（0.10.4，P-15；照 Electron pages-plan.js journalHtml）：本计划的游记列表（首图 + 120 字）。"写一篇"和打开全文到阶段 ⑤，现在只能看。
import QtQuick
import TPApp

Column {
    id: root
    property var rows: []
    spacing: Theme.px(10)

    Text {
        objectName: "journalCount"
        visible: root.rows.length > 0
        text: qsTr("%1 篇").arg(root.rows.length)
        font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
    }
    Text {
        objectName: "journalEmpty"
        visible: root.rows.length === 0
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("这个计划还没有游记。写游记和看全文要等后面的版本（阶段 ⑤）。")
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
            border.width: 1; border.color: Theme.line
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
