// 计划页的"攻略"标签（0.10.4，P-16、P-17；照 Electron pages-plan.js guideHtml、bindGuide）。
// 写（MdEditor）：工具栏 8 个、字数、停手 250 ms 预览和保存。PO 选 3A：左写右预览，窄窗口"写 / 看"切换。空攻略直接进编辑。
// 读：勾选项能打勾、写回原文。PO 选 4A：目录在右侧竖列，滚动时留在原地，长标题截断、悬停看全文；窄窗口收到正文上方。
// viewTop = 列表可见区域的上边（收起的页头下面）相对这个标签的位置，目录靠它留在原地。viewH = 写的时候两栏的高度。
import QtQuick
import TPApp

Column {
    id: root
    property string planId: ""
    property string guide: ""
    property var view: ({})                 // { blocks, heads }
    property bool readOnly: false
    property real viewTop: 0
    property real viewH: Theme.px(420)
    signal scrollRequested(real y, bool animate)   // 滚到这个标签里的 y
    signal linkRequested(string selected)
    signal photoRequested()
    signal photoViewRequested(var blocks, int block)
    spacing: Theme.px(10)

    property bool wantEdit: false
    readonly property bool blank: guide.trim() === ""
    readonly property bool editing: !readOnly && (wantEdit || blank)
    readonly property bool wide: width >= Theme.px(620)
    readonly property var heads: view.heads || []
    property bool tocOpen: false
    property string savePlan: ""            // 正在写的是哪个计划
    readonly property alias previewBlocks: ed.previewBlocks
    property alias pane: ed.pane            // 窄窗口：write / read

    function load() {
        savePlan = planId
        ed.setText(guide)
    }
    function flush() { ed.flush() }
    function beginEdit() { wantEdit = true; ed.focusEdit(); editScroll.restart() }
    Timer { id: editScroll; interval: 80; onTriggered: root.scrollRequested(0, true) }   // 从读切到写，页头变矮：列表排好以后再算位置，否则滚过头、工具栏被盖住
    function done() { flush(); wantEdit = false; ed.pane = "write" }
    function replaceSelection(t) { ed.replaceSelection(t) }
    function insertPhoto(ph) { ed.insertPhoto(ph) }

    onPlanIdChanged: { flush(); wantEdit = false; ed.pane = "write"; tocOpen = false; Qt.callLater(load) }
    onEditingChanged: if (editing) load(); else flush()
    onGuideChanged: if (editing && !ed.focused && !ed.pending && ed.text !== guide) load()   // 撤销之类从外面改的
    Component.onCompleted: load()

    // ---- 写 ----
    MdEditor {
        id: ed
        visible: root.editing
        live: root.editing
        width: parent.width
        viewH: root.viewH
        hint: qsTr("%1 字 · 停手后自动保存").arg(text.length)
        placeholder: qsTr("写攻略…")
        // 空攻略进来写了字：存进去以后不再是"空的"，要记住还在写，否则第一次保存就跳到读
        onTyping: root.wantEdit = true
        onEdited: if (root.savePlan !== "") planPage.setGuide(root.savePlan, text)
        onDoneClicked: root.done()
        onLinkRequested: (selected) => root.linkRequested(selected)
        onPhotoRequested: root.photoRequested()
        onPhotoViewRequested: (blocks, k) => root.photoViewRequested(blocks, k)
    }

    // ---- 只读又没写过 ----
    Text {
        objectName: "guideEmpty"
        visible: root.readOnly && root.blank
        width: parent.width
        text: qsTr("（还没有内容）")
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
    }

    // ---- 读：窄窗口的目录在正文上方 ----
    Column {
        visible: !root.editing && !root.blank && !root.wide
        width: parent.width
        spacing: Theme.px(6)
        Item {
            width: parent.width
            height: Theme.px(32)
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.px(10)
                TpChip {
                    objectName: "guideTocToggle"
                    visible: root.heads.length >= 2
                    text: qsTr("目录") + " " + root.heads.length + (root.tocOpen ? " ▴" : " ▾")
                    on: root.tocOpen
                    onClicked: root.tocOpen = !root.tocOpen
                }
                Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("%1 字").arg(root.guide.length); font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted }
            }
            TpButton { visible: !root.readOnly; anchors.right: parent.right; compact: true; text: qsTr("编辑攻略"); onClicked: root.beginEdit() }
        }
        TocList {
            visible: root.tocOpen && root.heads.length >= 2
            width: parent.width
            maxH: Theme.px(260)
        }
    }

    // ---- 读：正文 + 右侧竖列 ----
    Item {
        id: readArea
        objectName: "guideRead"
        visible: !root.editing && !root.blank
        width: parent.width
        height: Math.max(bodyCard.height, root.wide ? side.height : 0)
        TpCard {
            id: bodyCard
            objectName: "guideBody"
            width: root.wide ? parent.width - side.width - Theme.px(16) : parent.width
            height: readBlocks.height + Theme.px(32)
            radius: Theme.px(16)
            color: Theme.card
            GuideBlocks {
                id: readBlocks
                x: Theme.px(18); y: Theme.px(16)
                width: parent.width - Theme.px(36)
                blocks: root.view.blocks || []
                tasks: !root.readOnly
                onTaskToggled: (line, on) => planPage.toggleGuideTask(line, on)
                onPhotoClicked: (k) => root.photoViewRequested(root.view.blocks || [], k)
            }
        }
        Column {
            id: side
            objectName: "guideSide"
            visible: root.wide
            x: parent.width - width
            width: Theme.px(150)
            // 留在原地：不超出正文的上下
            y: Math.max(0, Math.min(root.viewTop + Theme.px(8) - parent.y, parent.height - height))
            spacing: Theme.px(8)
            TpButton { objectName: "guideEditBtn"; visible: !root.readOnly; compact: true; text: qsTr("编辑攻略"); onClicked: root.beginEdit() }
            Text { text: qsTr("%1 字").arg(root.guide.length); font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted }
            Text { visible: root.heads.length >= 2; text: qsTr("目录"); font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.text }
            TocList {
                objectName: "guideToc"
                visible: root.heads.length >= 2
                width: parent.width
                maxH: root.viewH
            }
        }
        // 目录的长标题：悬停看全文（盖在正文上）
        Rectangle {
            id: tip
            objectName: "guideTocTip"
            visible: false
            z: 20
            width: Math.min(tipText.implicitWidth + Theme.px(14), Theme.px(320))
            height: tipText.implicitHeight + Theme.px(8)
            radius: Theme.px(6)
            color: "#26323c"
            property alias text: tipText.text
            Text {
                id: tipText
                x: Theme.px(7); y: Theme.px(4)
                width: Math.min(implicitWidth, Theme.px(306))
                wrapMode: Text.Wrap
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: "#ffffff"
            }
        }
    }

    function scrollToHead(h) { root.scrollRequested(readArea.y + readBlocks.y + readBlocks.blockY(h.block) - Theme.px(4), true) }

    component TocList: Flickable {
        id: toc
        property real maxH: Theme.px(300)
        height: Math.min(tocCol.implicitHeight, maxH)
        contentHeight: tocCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: tocCol
            width: toc.width
            spacing: Theme.px(1)
            Repeater {
                model: root.heads
                delegate: Item {
                    required property var modelData
                    required property int index
                    objectName: "guideTocRow-" + index
                    width: tocCol.width
                    height: Theme.px(24)
                    Rectangle { anchors.fill: parent; radius: Theme.px(6); color: headMa.containsMouse ? Theme.accentSoft : "transparent" }
                    Text {
                        id: headText
                        x: Theme.px(modelData.level > 1 ? 14 : 4)
                        width: parent.width - x - Theme.px(4)
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        text: modelData.text
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                    }
                    MouseArea {
                        id: headMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.scrollToHead(modelData)
                        onContainsMouseChanged: {
                            if (containsMouse && headText.truncated) {
                                const p = mapToItem(readArea, 0, 0)
                                tip.text = modelData.text
                                tip.x = root.wide ? Math.max(0, p.x - tip.width - Theme.px(6)) : 0
                                tip.y = p.y
                                tip.visible = true
                            } else tip.visible = false
                        }
                    }
                }
            }
        }
    }
}
