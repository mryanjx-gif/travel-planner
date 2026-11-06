// 一张清单（0.10.4，P-09；照 Electron pages-plan.js checkList）：标题、进度、每行勾选 / 删除、待办的截止日期、Enter 添加。
// M1 已勾的沉到底：顺序由 plans::kitInfo 排好，这里用 ListModel 一行行挪（不重建），勾的时候才有动画（遵守"动效"）。
// M2 快到期和已过期的日期变色。M4 粘贴多行时输入框变高、按钮变成"加 N 条 / 取消"，确认后一次加完。
import QtQuick
import TPApp

Rectangle {
    id: root
    property string listKey: "packing"      // packing / todos
    property string title: ""
    property string placeholder: ""
    property var rows: []
    property int doneCount: 0
    property bool readOnly: false
    property string draft: ""               // 输入框里的字（M4 看它有几行）
    default property alias tools: slot.data
    property bool hasTools: false
    readonly property bool todos: listKey === "todos"
    readonly property var lines: draft.indexOf("\n") >= 0 ? planPage.splitLines(draft) : []
    readonly property bool pasted: lines.length > 1

    objectName: "kitCard-" + listKey
    height: col.implicitHeight + Theme.px(24)
    radius: Theme.px(16)
    color: "#ffffff"
    border.width: 1; border.color: Theme.line

    function add() {
        if (planPage.addChecks(listKey, draft) < 0) return
        draft = ""
        field.clear()
    }
    function cancelPaste() { draft = ""; field.clear() }

    // ---- 把 rows 同步进模型：挪、插、删，已有的行不重建 ----
    ListModel { id: rowModel }
    function sync() {
        for (let i = 0; i < rows.length; ++i) {
            const r = rows[i]
            const m = { rowId: r.id, rowText: r.text, done: r.done, due: r.due, dueState: r.dueState }
            let j = -1
            for (let k = i; k < rowModel.count; ++k) if (rowModel.get(k).rowId === r.id) { j = k; break }
            if (j < 0) rowModel.insert(i, m)
            else { if (j !== i) rowModel.move(j, i, 1); rowModel.set(i, m) }
        }
        if (rowModel.count > rows.length) rowModel.remove(rows.length, rowModel.count - rows.length)
    }
    onRowsChanged: sync()
    Component.onCompleted: sync()

    property bool animating: false
    property string dueEdit: ""             // 正在改日期的那一行
    Timer { id: animOff; interval: Theme.durPanel + 60; onTriggered: root.animating = false }
    function toggle(id, done) {
        animating = Theme.motion
        animOff.restart()
        planPage.setCheckDone(listKey, id, done)
    }
    Transition { id: sink; NumberAnimation { properties: "y"; duration: Theme.durPanel; easing.type: Easing.OutCubic } }

    Column {
        id: col
        x: Theme.px(14); y: Theme.px(12)
        width: parent.width - Theme.px(28)
        spacing: Theme.px(8)
        Item {
            width: parent.width
            height: titleText.implicitHeight
            Text {
                id: titleText
                text: root.title
                font.family: serifFamily; font.pixelSize: Theme.px(15); font.bold: true; color: Theme.text
            }
            Text {
                objectName: "kitCount-" + root.listKey
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.rows.length > 0
                text: root.doneCount + " / " + root.rows.length
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
            }
        }
        Column {   // 标题下面的东西（模板按钮、到期提醒）
            id: slot
            width: parent.width
            spacing: Theme.px(6)
            visible: root.hasTools
        }
        Rectangle {
            visible: root.rows.length > 0
            width: parent.width; height: Theme.px(6); radius: height / 2
            color: Theme.panel2
            Rectangle {
                objectName: "kitBar-" + root.listKey
                width: parent.width * root.doneCount / Math.max(1, root.rows.length); height: parent.height; radius: parent.radius
                color: Theme.accent
            }
        }
        Column {
            id: rowsCol
            width: parent.width
            spacing: Theme.px(2)
            move: root.animating ? sink : null
            Repeater {
                model: rowModel
                delegate: Item {
                    id: rowItem
                    required property int index
                    required property string rowId
                    required property string rowText
                    required property bool done
                    required property string due
                    required property string dueState
                    readonly property bool editingDue: root.dueEdit === rowId
                    objectName: "kitRow-" + rowId
                    width: rowsCol.width
                    height: main.height + Theme.px(4) + (editingDue ? dateBox.height + Theme.px(4) : 0)
                    HoverHandler { id: hover }
                    Item {
                        id: main
                        y: Theme.px(2)
                        width: parent.width
                        height: Math.max(Theme.px(24), rowLabel.contentHeight + Theme.px(4))
                        TpCheck {
                            objectName: "kitBox-" + rowItem.rowId
                            y: (Theme.px(24) - height) / 2
                            width: Theme.px(18)
                            enabled: !root.readOnly
                            checked: rowItem.done
                            Accessible.name: rowItem.rowText
                            onToggled: (c) => root.toggle(rowItem.rowId, c)
                        }
                        Text {
                            id: rowLabel
                            x: Theme.px(25)
                            y: Theme.px(2)
                            width: parent.width - x - tail.width - Theme.px(4)
                            wrapMode: Text.Wrap
                            text: rowItem.rowText
                            font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.strikeout: rowItem.done
                            color: rowItem.done ? Theme.muted : Theme.text
                            MouseArea {
                                anchors.fill: parent
                                enabled: !root.readOnly
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggle(rowItem.rowId, !rowItem.done)
                            }
                        }
                        Row {
                            id: tail
                            anchors.right: parent.right
                            y: (Theme.px(24) - Theme.px(20)) / 2
                            spacing: Theme.px(4)
                            Rectangle {   // 截止日期：有日期时一直显示；没有时悬停才出现
                                objectName: "kitDue-" + rowItem.rowId
                                visible: root.todos && (rowItem.due !== "" || (hover.hovered && !root.readOnly))
                                width: dueText.implicitWidth + Theme.px(12)
                                height: Theme.px(20)
                                radius: Theme.px(7)
                                color: rowItem.dueState === "over" ? "#f8e4dc" : rowItem.dueState === "soon" ? "#fbf1dc" : "transparent"
                                Text {
                                    id: dueText
                                    anchors.centerIn: parent
                                    text: rowItem.due !== "" ? Fmt.md(rowItem.due) : qsTr("截止日期")
                                    font.family: serifFamily; font.pixelSize: Theme.px(12)
                                    color: rowItem.dueState === "over" ? Theme.dangerInk : rowItem.dueState === "soon" ? "#7a5a1c" : rowItem.due !== "" ? Theme.muted : Theme.faint
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !root.readOnly
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.dueEdit = rowItem.editingDue ? "" : rowItem.rowId
                                        if (rowItem.editingDue) Qt.callLater(dateBox.openCal)
                                    }
                                }
                            }
                            Item {   // 删除：悬停才显示，长清单不吵
                                width: Theme.px(20); height: width
                                visible: !root.readOnly
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("删除")
                                TpIcon { anchors.centerIn: parent; width: Theme.px(13); height: width; name: "close"; color: delMa.containsMouse ? Theme.danger : Theme.muted; visible: hover.hovered }
                                MouseArea {
                                    id: delMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: planPage.removeCheck(root.listKey, rowItem.rowId)
                                }
                            }
                        }
                    }
                    DateField {
                        id: dateBox
                        visible: rowItem.editingDue
                        x: Theme.px(25)
                        y: main.height + Theme.px(4)
                        width: Math.min(Theme.px(170), parent.width - x)
                        value: rowItem.due
                        onEdited: (v) => { planPage.setCheckDue(rowItem.rowId, v); root.dueEdit = "" }
                    }
                }
            }
        }
        // ---- 添加 ----
        Item {
            visible: !root.readOnly
            width: parent.width
            height: field.height
            TpField {
                id: field
                objectName: "kitField-" + root.listKey
                width: parent.width - addRow.width - Theme.px(8)
                placeholder: root.placeholder
                multiline: root.pasted
                maxLines: 6
                submitOnEnter: true
                value: root.draft
                onEdited: (t) => { root.draft = t }
                onAccepted: root.add()
                onEscaped: root.cancelPaste()
            }
            Row {
                id: addRow
                anchors.right: parent.right
                spacing: Theme.px(6)
                TpButton {
                    objectName: "kitAdd-" + root.listKey
                    compact: true
                    kind: root.pasted ? "primary" : "secondary"
                    text: root.pasted ? qsTr("加 %1 条").arg(root.lines.length) : qsTr("添加")
                    onClicked: root.add()
                }
                TpButton {
                    objectName: "kitCancel-" + root.listKey
                    visible: root.pasted
                    compact: true
                    text: qsTr("取消")
                    onClicked: root.cancelPaste()
                }
            }
        }
    }
}
