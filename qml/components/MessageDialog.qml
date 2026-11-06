// 普通对话框：标题 + 几段说明 + 右下角按钮（关闭时没能保存、恢复历史版本的确认等）。
// paragraphs: [字符串]（可以用 <b>…</b>）；buttons: [{ key, text, kind }]，从左到右排，最后一个是默认按钮（Enter）
import QtQuick
import TPApp

ModalLayer {
    id: root
    property string title: ""
    property var paragraphs: []
    property var buttons: []
    property string cancelKey: "cancel"
    signal chosen(string key)
    cardWidth: 470
    onDismissed: chosen(cancelKey)

    Text {
        width: parent.width
        text: root.title
        wrapMode: Text.Wrap
        font.family: serifFamily
        font.pixelSize: Theme.px(19)
        font.bold: true
        color: Theme.text
    }
    Repeater {
        model: root.paragraphs
        delegate: Text {
            required property var modelData
            width: parent ? parent.width : 0
            text: modelData
            textFormat: Text.StyledText
            wrapMode: Text.Wrap
            lineHeight: 1.3
            font.family: serifFamily
            font.pixelSize: Theme.px(13.5)
            color: Theme.textSoft
        }
    }
    Item {
        width: parent.width
        height: btnFlow.implicitHeight + Theme.px(6)
        Flow {
            id: btnFlow
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: parent.width
            layoutDirection: Qt.RightToLeft
            spacing: Theme.px(10)
            Repeater {
                // Flow 从右往左排：倒过来放，显示顺序和 buttons 一样
                model: root.buttons.slice().reverse()
                delegate: TpButton {
                    required property var modelData
                    required property int index
                    text: modelData.text
                    kind: modelData.kind || "secondary"
                    onClicked: root.chosen(modelData.key)
                    Component.onCompleted: if (index === 0 && root.open) forceActiveFocus()
                }
            }
        }
    }
}
