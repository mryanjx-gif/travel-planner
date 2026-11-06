// 攻略：插入链接（0.10.4，P-16；Electron bindGuide 的 link）。网址只认 http:// 和 https://。
import QtQuick
import TPApp

ModalLayer {
    id: root
    property string linkText: ""
    property string url: ""
    property bool bad: false
    signal accepted(string markdown)
    cardWidth: 460
    onDismissed: open = false

    function show(selected) {
        root.forceActiveFocus()
        linkText = selected
        url = ""
        bad = false
        open = true
        urlField.focusAll()
    }
    function save() {
        const u = url.trim()
        if (!/^https?:\/\/\S+$/i.test(u)) { bad = true; return }
        open = false
        root.accepted(linkText.trim() ? "[" + linkText.trim() + "](" + u + ")" : u)
    }

    Text { text: qsTr("插入链接"); font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text }
    TpField { width: parent.width; placeholder: qsTr("显示的文字，可以不填"); value: root.linkText; onEdited: (t) => { root.linkText = t }; onAccepted: root.save(); onEscaped: root.open = false }
    TpField { id: urlField; objectName: "guideLinkUrl"; width: parent.width; placeholder: "https://…"; bad: root.bad; value: root.url; onEdited: (t) => { root.url = t; root.bad = false }; onAccepted: root.save(); onEscaped: root.open = false }
    Text {
        objectName: "guideLinkErr"
        visible: root.bad
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("网址要以 http:// 或 https:// 开头")
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
    }
    Row {
        anchors.right: parent.right
        spacing: Theme.px(8)
        TpButton { text: qsTr("取消"); onClicked: root.open = false }
        TpButton { objectName: "guideLinkOk"; kind: "primary"; text: qsTr("插入"); onClicked: root.save() }
    }
}
