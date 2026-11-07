// 写 Markdown：工具栏 + 左写右预览（窄窗口"写 / 看"切换）。0.11.2 从攻略（GuideTab）抽出来，游记也用它。
// 停手 delay 毫秒发 edited（这时候存）并刷新预览。namePrefix 是元素名字的前缀（guide / journal），自检按名字找。
import QtQuick
import TPApp

Column {
    id: root
    property string namePrefix: "guide"
    property var tools: ["h1", "h2", "b", "ul", "task", "quote", "link", "pic"]
    property string placeholder: ""
    property string hint: ""
    property bool live: true                // 现在是不是真的在写（否则外面设文字不算改动）
    property bool showDone: true
    property int delay: 250
    property real viewH: Theme.px(420)
    property real bodyFont: 13.5
    signal typing()                         // 每打一个字
    signal edited()                         // 停手以后
    signal doneClicked()
    signal linkRequested(string selected)
    signal photoRequested()
    signal fileRequested()
    signal photoViewRequested(var blocks, int block)
    spacing: Theme.px(10)

    readonly property alias text: edit.text
    readonly property bool focused: edit.activeFocus
    readonly property bool pending: saveTimer.running
    readonly property bool wide: width >= Theme.px(620)
    property string pane: "write"           // 窄窗口：write / read
    property bool loading: false
    property var previewBlocks: []
    property int selA: 0
    property int selZ: 0

    // 从外面换内容：不算改动
    function setText(t) {
        saveTimer.stop()
        loading = true
        edit.text = t
        loading = false
        previewBlocks = planPage.guideBlocks(t).blocks
    }
    function flush() { if (saveTimer.running) { saveTimer.stop(); commit() } }
    function commit() {
        root.edited()
        previewBlocks = planPage.guideBlocks(edit.text).blocks
        Qt.callLater(followCursor)
    }
    // 预览跟着写的位置走（按比例，够用）
    function followCursor() {
        const ratio = edit.contentHeight > 0 ? edit.cursorRectangle.y / edit.contentHeight : 0
        prevFlick.contentY = Math.max(0, ratio * (prevFlick.contentHeight - prevFlick.height))
    }
    function focusEdit() { pane = "write"; edit.forceActiveFocus() }

    // ---- 工具栏 ----
    function prefixLine(p) {
        const a = edit.selectionStart
        const ls = a === 0 ? 0 : edit.text.lastIndexOf("\n", a - 1) + 1
        edit.insert(ls, p)
        edit.cursorPosition = a + p.length
        edit.forceActiveFocus()
    }
    function tool(k) {
        const a = edit.selectionStart, z = edit.selectionEnd
        if (k === "h1") prefixLine("# ")
        else if (k === "h2") prefixLine("## ")
        else if (k === "ul") prefixLine("- ")
        else if (k === "task") prefixLine("- [ ] ")
        else if (k === "quote") prefixLine("> ")
        else if (k === "b") {
            edit.insert(z, "**"); edit.insert(a, "**")
            edit.select(a + 2, z + 2)
            edit.forceActiveFocus()
        } else if (k === "link") {
            selA = a; selZ = z
            root.linkRequested(edit.text.slice(a, z))
        } else if (k === "pic") {
            selA = a; selZ = z
            root.photoRequested()
        } else if (k === "file") {
            selA = a; selZ = z
            root.fileRequested()
        }
    }
    // 链接：换掉选中的字
    function replaceSelection(t) {
        edit.remove(selA, selZ)
        edit.insert(selA, t)
        edit.cursorPosition = selA + t.length
        edit.forceActiveFocus()
    }
    // 照片单独一行（Electron：前面不是行首就先换行）
    function insertPhoto(ph) {
        const pre = edit.text.slice(0, selA)
        const cap = String(ph.caption || "").replace(/[\[\]\n]/g, " ")
        selZ = selA
        replaceSelection((pre !== "" && !pre.endsWith("\n") ? "\n" : "") + "![" + cap + "](photo:" + ph.id + ")\n")
    }
    // R1：点照片条，插在光标这里
    function insertAtCursor(ph) { selA = edit.cursorPosition; insertPhoto(ph) }

    readonly property var toolNames: ({ h1: qsTr("大标题"), h2: qsTr("小标题"), b: qsTr("加粗"), ul: qsTr("列表"), task: qsTr("勾选项"), quote: qsTr("引用"),
                                        link: qsTr("链接"), pic: qsTr("插入照片"), file: qsTr("从电脑添加照片") })
    Flow {
        width: parent.width
        spacing: Theme.px(6)
        Repeater {
            model: root.tools
            delegate: TpChip {
                required property string modelData
                objectName: root.namePrefix + "Tool-" + modelData
                text: root.toolNames[modelData]
                onClicked: root.tool(modelData)
            }
        }
    }
    Item {
        width: parent.width
        height: Theme.px(32)
        visible: !root.wide || root.showDone || root.hint !== ""
        Segmented {
            visible: !root.wide
            anchors.verticalCenter: parent.verticalCenter
            height: Theme.px(30)
            options: [{ key: "write", text: qsTr("写") }, { key: "read", text: qsTr("看") }]
            current: root.pane
            onPicked: (k) => { root.pane = k; if (k === "read") root.flush() }
        }
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(10)
            Text {
                objectName: root.namePrefix + "Hint"
                anchors.verticalCenter: parent.verticalCenter
                visible: edit.text !== "" && root.hint !== ""
                text: root.hint
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
            }
            TpButton { visible: root.showDone; objectName: root.namePrefix + "Done"; compact: true; kind: "primary"; text: qsTr("完成"); onClicked: root.doneClicked() }
        }
    }
    Row {
        width: parent.width
        spacing: root.wide ? Theme.px(10) : 0
        Rectangle {
            objectName: root.namePrefix + "Write"
            visible: root.wide || root.pane === "write"
            width: root.wide ? (parent.width - parent.spacing) / 2 : parent.width
            height: root.viewH
            radius: Theme.px(12)
            color: "#ffffff"
            border.width: 1
            border.color: edit.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.6) : Theme.lineStrong
            FocusRing { target: edit }
            Flickable {
                id: editFlick
                x: Theme.px(12); y: Theme.px(10)
                width: parent.width - Theme.px(24)
                height: parent.height - Theme.px(20)
                contentHeight: Math.max(edit.contentHeight, height)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                function ensureVisible(r) {
                    if (contentY >= r.y) contentY = r.y
                    else if (contentY + height <= r.y + r.height) contentY = r.y + r.height - height
                }
                TextEdit {
                    id: edit
                    objectName: root.namePrefix + "Edit"
                    width: editFlick.width
                    height: Math.max(contentHeight, editFlick.height)
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    selectionColor: Theme.accentSoft
                    selectedTextColor: Theme.text
                    font.family: serifFamily; font.pixelSize: Theme.px(root.bodyFont)
                    color: Theme.text
                    onTextChanged: if (!root.loading && root.live) { root.typing(); saveTimer.restart() }
                    onCursorRectangleChanged: editFlick.ensureVisible(cursorRectangle)
                    Text {
                        visible: edit.text === "" && edit.preeditText === ""
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.placeholder
                        font: edit.font
                        color: Theme.faint
                    }
                }
            }
        }
        Rectangle {
            objectName: root.namePrefix + "Preview"
            visible: root.wide || root.pane === "read"
            width: root.wide ? (parent.width - parent.spacing) / 2 : parent.width
            height: root.viewH
            radius: Theme.px(12)
            color: "#ffffff"
            border.width: 1; border.color: Theme.line
            Accessible.name: qsTr("预览")
            Flickable {
                objectName: root.namePrefix + "PreviewFlick"
                id: prevFlick
                x: Theme.px(12); y: Theme.px(10)
                width: parent.width - Theme.px(24)
                height: parent.height - Theme.px(20)
                contentHeight: prevBlocks.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                GuideBlocks { id: prevBlocks; width: prevFlick.width; blocks: root.previewBlocks; onPhotoClicked: (k) => root.photoViewRequested(root.previewBlocks, k) }
            }
        }
    }
    Timer { id: saveTimer; interval: root.delay; onTriggered: root.commit() }
}
