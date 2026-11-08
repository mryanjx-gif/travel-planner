// 输入框（0.8.9，站点编辑器用）：单行或多行（multiline，跟着内容变高，最高 maxLines 行后滚动；Electron autoGrow 最高 260 px）。
// value 是外面的值；用户打字引起的 value 变化不写回（免得光标跳，外面改写的离开时对上），别处改的马上显示。每打一个字发 edited(文字)；Enter（单行）发 accepted()。
// 0.12.10：只有用户打字才发 edited。以前 text 绑着 value：外面改 value（如"恢复平摊"）→ 文字变 → 有焦点就发 edited → 外面再改 value，value 的绑定算到一半又被触发（binding loop）。
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
    property int maxLength: 0              // > 0：最多几个字（0.12.3 设置里的名字）
    property real rightPad: 0              // 1.1.0 右边留给放在框里的按钮（地名搜索的发送键）
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
    function focusEnd() { edit.forceActiveFocus(); edit.cursorPosition = edit.text.length }
    property bool syncing: false           // 正在把 value 写进输入框：这次文字变化不算用户打的
    property bool echoing: false           // 正在发 edited：这次 value 变化是打字的回音，不写回
    function syncText() { syncing = true; edit.text = value; syncing = false }
    onValueChanged: if (!echoing && edit.text !== value) syncText()
    Component.onCompleted: syncText()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusControl
        color: "#ffffff"
        border.width: 1
        border.color: root.bad ? Theme.danger : edit.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.6) : Theme.lineStrong
        FocusRing { target: edit }
    }
    Flickable {
        id: flick
        x: Theme.px(10); y: root.multiline ? Theme.px(7) : 0
        width: parent.width - Theme.px(20) - root.rightPad
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
            verticalAlignment: root.multiline ? TextEdit.AlignTop : TextEdit.AlignVCenter
            wrapMode: root.multiline ? TextEdit.Wrap : TextEdit.NoWrap
            font.family: serifFamily; font.pixelSize: Theme.px(root.fontSize); font.bold: root.bold
            color: Theme.text
            selectByMouse: true
            selectionColor: Theme.accentSoft
            selectedTextColor: Theme.text
            onTextChanged: {
                if (root.maxLength > 0 && text.length > root.maxLength) { text = text.slice(0, root.maxLength); cursorPosition = text.length; return }
                if (activeFocus && !root.syncing) { root.echoing = true; root.edited(text); root.echoing = false }
            }
            onCursorRectangleChanged: flick.ensureVisible(cursorRectangle)
            function enter(e) {
                if (inputMethodComposing) { e.accepted = false; return }   // 0.13.0：输入法还在组字时 Enter 交给输入法（确认候选），不发出去
                if (root.multiline && (!root.submitOnEnter || (e.modifiers & Qt.ShiftModifier))) e.accepted = false
                else { root.accepted(); e.accepted = true }
            }
            Keys.onReturnPressed: (e) => enter(e)
            Keys.onEnterPressed: (e) => enter(e)
            Keys.onEscapePressed: (e) => { focus = false; root.escaped(); e.accepted = true }
            onActiveFocusChanged: if (!activeFocus && text !== root.value) root.syncText()
            Text {
                visible: edit.text === "" && edit.preeditText === ""
                width: parent.width
                elide: Text.ElideRight
                y: root.multiline ? 0 : (parent.height - height) / 2
                text: root.placeholder
                font: edit.font
                color: "#7f8e9a"   // 比 Theme.faint 深：要读得出来
            }
        }
    }
}
