// 带一个输入框的对话框（修改窗口标题）。确定发 accepted(text)，取消 / Esc 发 canceled()
import QtQuick
import TPApp

ModalLayer {
    id: root
    property string title: ""
    property string hint: ""
    property alias text: input.text
    property int maxLength: 60
    signal accepted(string text)
    signal canceled()
    cardWidth: 440
    onDismissed: canceled()
    onOpenChanged: if (open) { input.forceActiveFocus(); input.selectAll() }

    Text {
        width: parent.width
        text: root.title
        font.family: serifFamily
        font.pixelSize: Theme.px(19)
        font.bold: true
        color: Theme.text
    }
    Text {
        width: parent.width
        visible: text !== ""
        text: root.hint
        wrapMode: Text.Wrap
        font.family: serifFamily
        font.pixelSize: Theme.px(13)
        color: Theme.muted
    }
    Rectangle {
        width: parent.width
        height: Theme.px(40)
        radius: Theme.radiusControl
        color: "#ffffff"
        border.width: input.activeFocus ? 2 : 1
        border.color: input.activeFocus ? Theme.accent : Theme.lineStrong
        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: Theme.px(12)
            anchors.rightMargin: Theme.px(12)
            verticalAlignment: TextInput.AlignVCenter
            maximumLength: root.maxLength
            clip: true
            selectByMouse: true
            selectionColor: Theme.accentSoft
            selectedTextColor: Theme.text
            font.family: serifFamily
            font.pixelSize: Theme.px(14)
            color: Theme.text
            Keys.onReturnPressed: root.accepted(text)
            Keys.onEnterPressed: root.accepted(text)
            Keys.onEscapePressed: root.canceled()
        }
    }
    Row {
        anchors.right: parent.right
        spacing: Theme.px(10)
        TpButton { text: qsTr("取消"); onClicked: root.canceled() }
        TpButton { text: qsTr("确定"); kind: "primary"; onClicked: root.accepted(input.text) }
    }
}
