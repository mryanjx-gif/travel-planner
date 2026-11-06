// 用选项卡片回答的对话框（PO 2026-11-02 选的冲突弹框方案 B；数据文件夹的选择也用它）。
// options: [{ key, title, detail, danger }]；选了发 chosen(key)；Esc / 点遮罩发 chosen(cancelKey)
import QtQuick
import TPApp

ModalLayer {
    id: root
    property string title: ""
    property string message: ""
    property var options: []
    property string cancelKey: ""
    signal chosen(string key)
    cardWidth: 440
    dismissible: cancelKey !== ""
    onDismissed: chosen(cancelKey)

    Column {
        width: parent.width
        spacing: Theme.px(6)
        Text {
            width: parent.width
            text: root.title
            wrapMode: Text.Wrap
            font.family: serifFamily
            font.pixelSize: Theme.px(19)
            font.bold: true
            color: Theme.text
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: root.message
            wrapMode: Text.Wrap
            lineHeight: 1.25
            font.family: serifFamily
            font.pixelSize: Theme.px(13.5)
            color: Theme.muted
        }
    }
    Repeater {
        model: root.options
        delegate: ChoiceCard {
            required property var modelData
            width: parent ? parent.width : 0
            title: modelData.title
            detail: modelData.detail || ""
            danger: !!modelData.danger
            onClicked: root.chosen(modelData.key)
        }
    }
}
