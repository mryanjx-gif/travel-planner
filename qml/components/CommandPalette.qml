// Ctrl K 命令面板（0.8.8，功能清单 S-12）。照 Electron v6.3.0 palette.js：屏幕中间一个玻璃搜索框，背后压暗；
// 输入时实时过滤并高亮匹配的文字（不分大小写，只标第一处）；↑ ↓ 选择，Enter 打开，Esc 关闭，点外面关闭；
// 打分规则在 C++（src/app/commands.*，和 palette.js filter 一样），最多 40 条。
// 原生版（PO 0.8.8 选方案 B）：结果按 最近 / 计划 / 站点 / 页面 / 操作 分组显示（有输入时组的先后看组里最好的分数）；
// 没输入时先列最近打开的计划；操作补上了原来系统菜单里的命令，带快捷键提示（条目由 Main 给）。
import QtQuick
import TPApp

Item {
    id: root
    property bool open: false
    property var source: function () { return [] }   // Main 给：() → [{ kind, label, sub, id, key, kbd, color, … }]
    property var recent: []
    signal run(var item)

    visible: opacity > 0.01
    opacity: open ? 1 : 0
    Behavior on opacity { enabled: Theme.motion; NumberAnimation { duration: 140 } }

    property var items: []
    property var results: []
    property int sel: 0
    readonly property string query: input.text

    function show(q) {
        items = source()
        input.text = q || ""
        refresh()
        open = true
        input.forceActiveFocus()
    }
    function hide() {
        if (!open) return
        open = false
        input.focus = false
    }
    function refresh() {
        results = commands.rank(items, input.text, recent)
        sel = 0
        list.positionViewAtBeginning()
    }
    function runAt(i) {
        const x = results[i]
        if (!x) return
        hide()
        root.run(x)
    }
    function groupLabel(g) {
        return g === "recent" ? qsTr("最近") : g === "plan" ? qsTr("计划") : g === "stop" ? qsTr("站点") : g === "page" ? qsTr("页面") : qsTr("操作")
    }
    // 高亮第一处匹配（palette.js hi）：转义后用 RichText 包一层底色
    function esc(s) { return String(s === undefined || s === null ? "" : s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
    function hi(text, q) {
        text = String(text || "")
        q = (q || "").trim()
        if (!q) return esc(text)
        const i = text.toLowerCase().indexOf(q.toLowerCase())
        if (i < 0) return esc(text)
        return esc(text.slice(0, i)) + "<span style=\"background-color:#f6e7a8\">" + esc(text.slice(i, i + q.length)) + "</span>" + esc(text.slice(i + q.length))
    }
    // 显示顺序：组的第一项前面加一行小标题。flat[k] = { header, group } 或 { at: results 的下标 }
    readonly property var flat: {
        const out = []
        let g = ""
        for (let i = 0; i < results.length; ++i) {
            if (results[i].group !== g) { g = results[i].group; out.push({ header: true, group: g }) }
            out.push({ header: false, at: i })
        }
        return out
    }
    function flatIndexOf(i) { for (let k = 0; k < flat.length; ++k) if (!flat[k].header && flat[k].at === i) return k; return -1 }
    onSelChanged: { const k = flatIndexOf(sel); if (k >= 0) list.positionViewAtIndex(k, ListView.Contain) }

    // 背景压暗；点外面关闭
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.22)
        MouseArea { anchors.fill: parent; onClicked: root.hide(); onWheel: (w) => w.accepted = true }
    }

    Item {
        id: box
        objectName: "paletteBox"
        width: Math.min(Theme.px(620), root.width - Theme.px(28))
        height: col.height
        x: Math.round((root.width - width) / 2)
        y: Math.max(Theme.px(70), Math.round(root.height * 0.14))
        scale: root.open ? 1 : 0.97
        Behavior on scale { enabled: Theme.motion; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        GlassPanel {
            anchors.fill: parent
            radius: Theme.px(18)
            tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.92 : 0.98)
        }
        MouseArea { anchors.fill: parent }

        Column {
            id: col
            width: parent.width
            // 输入框
            Item {
                width: parent.width
                height: Theme.px(56)
                TpIcon { id: sIcon; x: Theme.px(18); anchors.verticalCenter: parent.verticalCenter; name: "search"; color: Theme.muted; width: Theme.px(19); height: Theme.px(19) }
                TextInput {
                    id: input
                    objectName: "paletteInput"
                    x: sIcon.x + sIcon.width + Theme.px(12)
                    width: parent.width - x - escKey.width - Theme.px(30)
                    anchors.verticalCenter: parent.verticalCenter
                    font.family: serifFamily; font.pixelSize: Theme.px(16)
                    color: Theme.text
                    selectionColor: Theme.accentSoft
                    selectedTextColor: Theme.text
                    clip: true
                    onTextChanged: root.refresh()
                    Keys.onDownPressed: root.sel = Math.min(root.results.length - 1, root.sel + 1)
                    Keys.onUpPressed: root.sel = Math.max(0, root.sel - 1)
                    Keys.onReturnPressed: root.runAt(root.sel)
                    Keys.onEnterPressed: root.runAt(root.sel)
                    Text {
                        visible: input.text === "" && !input.inputMethodComposing
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("搜计划、站点、页面，或者输入要做的事")
                        font: input.font
                        color: Theme.faint
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }
                Rectangle {
                    id: escKey
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.px(16)
                    anchors.verticalCenter: parent.verticalCenter
                    width: et.implicitWidth + Theme.px(12); height: Theme.px(20); radius: Theme.px(5)
                    color: "transparent"; border.color: Theme.lineStrong
                    Text { id: et; anchors.centerIn: parent; text: "Esc"; font.pixelSize: Theme.px(11); color: Theme.muted }
                }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.line }

            ListView {
                id: list
                objectName: "paletteList"
                width: parent.width
                height: Math.min(contentHeight + Theme.px(12), Math.max(Theme.px(160), root.height - box.y - Theme.px(140)), Theme.px(440))
                topMargin: Theme.px(6); bottomMargin: Theme.px(6)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.flat
                visible: root.results.length > 0
                delegate: Item {
                    id: cell
                    required property var modelData
                    required property int index
                    readonly property bool header: modelData.header
                    readonly property var x0: header ? ({}) : root.results[modelData.at]
                    readonly property bool on: !header && modelData.at === root.sel
                    width: list.width
                    height: header ? Theme.px(index === 0 ? 24 : 30) : Theme.px(40)
                    Text {
                        visible: cell.header
                        x: Theme.px(20)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Theme.px(3)
                        text: cell.header ? root.groupLabel(cell.modelData.group) : ""
                        font.family: serifFamily; font.pixelSize: Theme.px(11); font.letterSpacing: 1.2; color: Theme.muted
                    }
                    Rectangle {
                        visible: !cell.header && (cell.on || ma.containsMouse)
                        x: Theme.px(8); width: parent.width - Theme.px(16); height: parent.height
                        radius: Theme.px(10)
                        color: cell.on ? Qt.rgba(1, 1, 1, 0.85) : Qt.rgba(1, 1, 1, 0.4)
                        border.width: cell.on ? 1 : 0
                        border.color: Theme.line
                    }
                    TpIcon {
                        id: kIcon
                        visible: !cell.header
                        x: Theme.px(20)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.px(17); height: Theme.px(17)
                        name: cell.header ? "" : (cell.x0.kind === "plan" ? "plan" : cell.x0.kind === "stop" ? "stop" : cell.x0.kind === "page" ? "page" : "act")
                        color: !cell.header && cell.x0.color ? cell.x0.color : Theme.muted
                    }
                    Text {
                        id: lab
                        visible: !cell.header
                        x: kIcon.x + kIcon.width + Theme.px(12)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width - x - subBox.width - Theme.px(30))
                        textFormat: Text.RichText
                        text: cell.header ? "" : root.hi(cell.x0.label || "", root.query)
                        elide: Text.ElideRight
                        font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.text
                    }
                    Row {
                        id: subBox
                        visible: !cell.header
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.px(20)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.px(8)
                        Text {
                            width: Math.min(implicitWidth, cell.width * 0.45)
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.RichText
                            // 副标题和组名一样时（页面 / 操作）不重复显示；打分照样算它（输入"页面"能列出全部页面）
                            text: cell.header || cell.x0.sub === root.groupLabel(cell.x0.group) ? "" : root.hi(cell.x0.sub || "", root.query)
                            elide: Text.ElideRight
                            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                        }
                        Rectangle {
                            visible: !cell.header && (cell.x0.kbd || "") !== ""
                            anchors.verticalCenter: parent.verticalCenter
                            width: kb.implicitWidth + Theme.px(10); height: Theme.px(18); radius: Theme.px(5)
                            color: "transparent"; border.color: Theme.lineStrong
                            Text { id: kb; anchors.centerIn: parent; text: cell.header ? "" : (cell.x0.kbd || ""); font.pixelSize: Theme.px(11); color: Theme.muted }
                        }
                    }
                    MouseArea {
                        id: ma
                        anchors.fill: parent
                        enabled: !cell.header
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.runAt(cell.modelData.at)
                    }
                }
            }
            Text {
                visible: root.results.length === 0
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                topPadding: Theme.px(22); bottomPadding: Theme.px(22)
                text: qsTr("没有找到。可以换个词，或者在地图上找。")
                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
            }
            Rectangle { width: parent.width; height: 1; color: Theme.line }
            Row {
                x: Theme.px(18)
                height: Theme.px(34)
                spacing: Theme.px(16)
                Repeater {
                    model: [qsTr("↑ ↓ 选择"), qsTr("Enter 打开"), qsTr("Esc 关闭")]
                    delegate: Text {
                        required property string modelData
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                }
            }
        }
    }
}
