// 输入框（0.8.9，站点编辑器用）：单行或多行（multiline，跟着内容变高，最高 maxLines 行后滚动；Electron autoGrow 最高 260 px）。
// value 是外面的值；正在输入时外面的值变了不覆盖（免得光标跳）。每打一个字发 edited(文字)；Enter（单行）发 accepted()。
import QtQuick
import TPApp

Item {
    id: root
    property string value: ""
    property string placeholder: ""
    property bool multiline: false
    property int maxLines: 8
    property bool bad: false
    property real fontSize: 13.5
    property bool bold: false
    property bool submitOnEnter: false     // 多行时 Enter 发 accepted()、Shift + Enter 换行（0.10.0 就地改日程）
    readonly property alias text: edit.text
    readonly property bool editing: edit.activeFocus
    signal edited(string text)
    signal accepted()
    signal escaped()
    implicitHeight: multiline ? Math.min(edit.contentHeight, edit.font.pixelSize * 1.5 * maxLines) + Theme.px(14) : Theme.px(32)
    implicitWidth: Theme.px(200)
    function focusAll() { edit.forceActiveFocus(); edit.selectAll() }
    function clear() { edit.text = "" }
    onValueChanged: if (!edit.activeFocus && edit.text !== value) edit.text = value

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusControl
        color: "#ffffff"
        border.width: edit.activeFocus ? 2 : 1
        border.color: root.bad ? Theme.danger : edit.activeFocus ? Theme.accent : Theme.lineStrong
    }
    Flickable {
        id: flick
        x: Theme.px(10); y: root.multiline ? Theme.px(7) : 0
        width: parent.width - Theme.px(20)
        height: root.multiline ? parent.height - Theme.px(14) : parent.height
        contentWidth: width
        contentHeight: edit.contentHeight
        clip: true
        interactive: root.multiline && contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        function ensureVisible(r) {
            if (contentY >= r.y) contentY = r.y
            else if (contentY + height <= r.y + r.height) contentY = r.y + r.height - height
        }
        TextEdit {
            id: edit
            width: flick.width
            height: root.multiline ? Math.max(contentHeight, flick.height) : flick.height
            text: root.value
            verticalAlignment: root.multiline ? TextEdit.AlignTop : TextEdit.AlignVCenter
            wrapMode: root.multiline ? TextEdit.Wrap : TextEdit.NoWrap
            font.family: serifFamily; font.pixelSize: Theme.px(root.fontSize); font.bold: root.bold
            color: Theme.text
            selectByMouse: true
            selectionColor: Theme.accentSoft
            selectedTextColor: Theme.text
            onTextChanged: if (activeFocus) root.edited(text)
            onCursorRectangleChanged: flick.ensureVisible(cursorRectangle)
            function enter(e) {
                if (root.multiline && (!root.submitOnEnter || (e.modifiers & Qt.ShiftModifier))) e.accepted = false
                else { root.accepted(); e.accepted = true }
            }
            Keys.onReturnPressed: (e) => enter(e)
            Keys.onEnterPressed: (e) => enter(e)
            Keys.onEscapePressed: (e) => { focus = false; root.escaped(); e.accepted = true }
            onActiveFocusChanged: if (!activeFocus && text !== root.value) text = root.value
            Text {
                visible: edit.text === "" && edit.preeditText === ""
                width: parent.width
                elide: Text.ElideRight
                y: root.multiline ? 0 : (parent.height - height) / 2
                text: root.placeholder
                font: edit.font
                color: Theme.faint
            }
        }
    }
}
