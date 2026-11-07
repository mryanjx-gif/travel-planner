// 出错小框（0.5.1，D-23；PO 2026-11-03 选方案 B）：右下角紧凑卡片"出了点问题 · 错误码"，"详情 ▾ / 复制"，
// 和"数据在别处更新了"卡片在同一个位置，多张往上叠（Main.qml 的 bottomCards）。
// 文字照 Electron errors.js（标题、说明、"请把错误码发给 Jason。"）；不会自己消失（Electron 也不会）。
import QtQuick
import TPApp

GlassPanel {
    id: root
    property string code: ""
    property string message: ""
    property string report: ""
    property bool expanded: false
    property bool copied: false
    signal closeClicked()
    signal copyClicked()

    width: Math.min(Theme.px(340), parent ? parent.width : 340)
    height: col.implicitHeight + Theme.px(26)

    Column {
        id: col
        x: Theme.px(16)
        y: Theme.px(12)
        width: root.width - 2 * x
        spacing: Theme.px(8)

        // 第一行：! 出了点问题 · TP-Q-xxxx        ×
        Item {
            width: parent.width
            height: Math.max(closeBtn.height, head.height)
            Rectangle {   // 红色圆圈 !（Electron 是线框圆 + !）
                id: icon
                width: Theme.px(18); height: width; radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: "transparent"
                border.width: Math.max(1.5, Theme.px(1.8))
                border.color: Theme.danger
                Text {
                    anchors.centerIn: parent
                    text: "!"
                    font.pixelSize: Theme.px(12); font.bold: true
                    color: Theme.danger
                }
            }
            // 标题太长时省略标题，错误码总是完整显示（英文界面）
            Row {
                id: head
                anchors.left: icon.right
                anchors.leftMargin: Theme.px(8)
                anchors.verticalCenter: parent.verticalCenter
                readonly property real avail: closeBtn.x - Theme.px(6) - x
                spacing: Theme.px(7)
                Text {
                    width: Math.min(implicitWidth, head.avail - dot.width - codeText.implicitWidth - 2 * head.spacing)
                    text: qsTr("出了点问题")
                    font.family: serifFamily; font.pixelSize: Theme.px(14.5); font.bold: true
                    color: Theme.text
                    elide: Text.ElideRight
                }
                Text {
                    id: dot
                    text: "·"
                    font.family: serifFamily; font.pixelSize: Theme.px(14.5)
                    color: Theme.faint
                }
                Text {
                    id: codeText
                    text: root.code
                    font.family: "Consolas"; font.pixelSize: Theme.px(14)
                    color: Theme.textSoft
                }
            }
            TpButton {
                id: closeBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "×"
                kind: "ghost"
                compact: true
                width: Theme.px(30)
                Accessible.name: qsTr("关闭")
                tip: qsTr("关闭")
                onClicked: root.closeClicked()
            }
        }

        // 详情（展开时）
        Column {
            width: parent.width
            spacing: Theme.px(6)
            visible: root.expanded
            Text {
                width: parent.width
                text: qsTr("旅行计划刚才遇到了一个错误，有些功能可能不正常。")
                wrapMode: Text.Wrap
                lineHeight: 1.25
                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
            }
            Text {   // 错误原文（不翻译），最多 140 字
                width: parent.width
                text: root.message.length > 140 ? root.message.slice(0, 140) + "…" : root.message
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
            }
            Text {
                width: parent.width
                text: qsTr("请把错误码发给 Jason。")
                wrapMode: Text.Wrap
                font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.text
            }
        }

        // 按钮：详情 ▾ / 复制
        Row {
            anchors.right: parent.right
            spacing: Theme.px(8)
            TpButton {
                compact: true
                kind: "ghost"
                text: qsTr("详情") + (root.expanded ? " ▴" : " ▾")
                onClicked: root.expanded = !root.expanded
            }
            TpButton {
                compact: true
                done: root.copied
                text: root.copied ? qsTr("已复制") : qsTr("复制")
                Accessible.name: qsTr("复制错误信息")
                onClicked: { root.copied = true; copiedTimer.restart(); root.copyClicked() }
                Timer { id: copiedTimer; interval: 2500; onTriggered: root.copied = false }
            }
        }
    }
}
