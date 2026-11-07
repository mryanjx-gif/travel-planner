// 一站的日程（0.10.0，P-06；PO 选"在卡片里就地展开"和 I2 按天分组）。计划页的站点卡片和编辑器的站点共用。
// 就地表单：Enter 保存（Shift + Enter 换行）、Esc 取消。
// 编辑状态放在外面的 ctl（{ stop, j, draft, err }；j：-2 没在改，-1 新加），不放这里：
// 数据一变日程行会重建，计划页滚远了卡片也会销毁，放外面改到一半的内容都不丢。一个面板同一时间只改一条。
// 0.12.9 时间线：一天内用竖线连起来，有时间的在前（plans::schedDays 排好），未定时间的在后、圆点是空心的。
// saver(j, draft) 返回空 = 改好了，否则是原因的键（plans::putSchedule）；deleter(j) 删除这一条。
import QtQuick
import TPApp

Column {
    id: root
    property var ctl: null
    property int stopIndex: -1
    property var days: []
    property bool canEdit: true
    property string defaultDate: ""
    property var saver: null
    property var deleter: null
    readonly property int editJ: ctl && ctl.stop === stopIndex ? ctl.j : -2
    readonly property bool anyTime: {
        for (const d of days) for (const it of d.items) if (it.time) return true
        return false
    }
    spacing: Theme.px(3)
    readonly property real railW: Theme.px(16)

    function begin(j, it) {
        ctl.err = ""
        ctl.draft = j < 0 ? { date: root.defaultDate, time: "", end: "", text: "" }
                            : { date: it.date || "", time: it.time || "", end: it.end || "", text: it.text || "" }
        ctl.stop = root.stopIndex
        ctl.j = j
    }
    function errText(k) {
        return k === "badTime" ? qsTr("时间要写成 09:30 这样")
             : k === "badDate" ? qsTr("日期不对")
             : k === "endNoStart" ? qsTr("填了结束时间，也要填开始时间")
             : k === "readOnly" ? qsTr("数据现在只能看、不能改") : ""
    }
    function save() {
        const k = root.saver(ctl.j, ctl.draft)
        if (k !== "" && k !== "empty") { ctl.err = errText(k); return }
        ctl.j = -2
    }
    function cancel() { ctl.j = -2 }

    Repeater {
        model: root.days
        delegate: Column {
            id: dayCol
            required property var modelData
            width: root.width
            spacing: Theme.px(2)
            Text {
                // 全都没写日期时不用标题；有的写了、有的没写时，没写的放最后并标出来
                visible: modelData.date !== "" || root.days.length > 1
                topPadding: Theme.px(3)
                text: modelData.date !== "" ? Fmt.mdw(modelData.date) : qsTr("未定日期")
                font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.muted
            }
            Repeater {
                model: dayCol.modelData.items
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool first: index === 0
                    readonly property bool last: index === dayCol.modelData.items.length - 1
                    readonly property real dotY: Theme.px(11)
                    readonly property bool editing: root.editJ === modelData.j
                    width: root.width
                    height: editing ? formL.height : Math.max(rowText.implicitHeight, Theme.px(22)) + Theme.px(3)
                    Rectangle {
                        visible: !row.editing && row.modelData.today
                        anchors.fill: parent
                        anchors.leftMargin: -Theme.px(4)
                        radius: Theme.px(5)
                        color: "#fbf1dc"
                    }
                    RowAct {
                        id: schedAct
                        objectName: "schedAct"
                        enabled: root.canEdit && !row.editing
                        editText: qsTr("修改")
                        onActivated: root.begin(row.modelData.j, row.modelData)
                        onDeleteRequested: win.deleteWithUndo(() => root.deleter(row.modelData.j))
                    }
                    // 时间线：竖线从上一条接到下一条（行距也接上），每条一个圆点
                    Rectangle {
                        objectName: "schedRail"
                        visible: !row.editing && !(row.first && row.last)
                        x: Theme.px(4) - width / 2 + Theme.px(1)
                        y: row.first ? row.dotY : 0
                        width: Math.max(1, Theme.px(1.5))
                        height: (row.last ? row.dotY : row.height + dayCol.spacing) - y
                        color: Theme.lineStrong
                    }
                    Rectangle {
                        objectName: "schedDot"
                        visible: !row.editing
                        x: Theme.px(1)
                        y: row.dotY - height / 2
                        width: Theme.px(8); height: width; radius: width / 2
                        color: row.modelData.time ? Theme.accent : Theme.card
                        border.width: row.modelData.time ? 0 : Math.max(1, Theme.px(1.5))
                        border.color: Theme.lineStrong
                    }
                    Text {
                        id: when
                        visible: !row.editing && root.anyTime
                        x: root.railW
                        width: Theme.px(Fmt.en ? 92 : 88)
                        y: Theme.px(2)
                        text: row.modelData.time ? row.modelData.time + (row.modelData.end ? "–" + row.modelData.end : "") : qsTr("未定时间")
                        font.family: serifFamily; font.pixelSize: Theme.px(row.modelData.time ? 13 : 12); color: row.modelData.time ? Theme.muted : Theme.faint
                    }
                    Text {
                        id: rowText
                        objectName: "schedText"
                        visible: !row.editing
                        x: root.railW + (when.visible ? when.width : 0)
                        y: Theme.px(2)
                        width: row.width - x - (root.canEdit ? Theme.px(28) : 0)
                        wrapMode: Text.Wrap
                        text: row.modelData.text
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.text
                        lineHeight: 1.25
                    }
                    TpIcon {
                        visible: !row.editing && root.canEdit
                        anchors.right: parent.right
                        y: Theme.px(2)
                        width: Theme.px(18); height: width
                        name: "edit"
                        color: editMa.containsMouse ? Theme.accentInk : Theme.faint
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("修改这一条（日期、时间、内容）")
                        MouseArea {
                            id: editMa
                            objectName: "schedEdit"
                            anchors.fill: parent
                            anchors.margins: -Theme.px(4)
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.begin(row.modelData.j, row.modelData)
                        }
                    }
                    Loader {
                        id: formL
                        active: row.editing
                        width: root.width
                        sourceComponent: formC
                    }
                }
            }
        }
    }
    Loader {
        active: root.editJ === -1
        visible: active
        width: root.width
        sourceComponent: formC
    }
    LinkText {
        objectName: "schedAdd"
        visible: root.canEdit && root.editJ !== -1
        topPadding: Theme.px(3)
        text: "＋ " + qsTr("加一条行程")
        small: true
        onClicked: root.begin(-1, null)
    }

    Component {
        id: formC
        Rectangle {
            objectName: "schedForm"
            height: formCol.implicitHeight + Theme.px(16)
            radius: Theme.radiusControl
            color: "#f7fafc"
            border.width: 1
            border.color: Theme.accent
            Component.onCompleted: textIn.focusAll()
            Column {
                id: formCol
                x: Theme.px(8); y: Theme.px(8)
                width: parent.width - Theme.px(16)
                spacing: Theme.px(6)
                Flow {
                    width: parent.width
                    spacing: Theme.px(6)
                    DateField {
                        width: Theme.px(140)
                        value: root.ctl.draft.date
                        onEdited: (v) => { root.ctl.draft.date = v }
                    }
                    Row {
                        spacing: Theme.px(4)
                        TpField {
                            objectName: "schedTime"
                            width: Theme.px(68)
                            value: root.ctl.draft.time
                            placeholder: qsTr("开始")
                            onEdited: (t) => { root.ctl.draft.time = t }
                            onAccepted: root.save()
                            onEscaped: root.cancel()
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "–"; color: Theme.muted; font.family: serifFamily; font.pixelSize: Theme.px(13) }
                        TpField {
                            width: Theme.px(68)
                            value: root.ctl.draft.end
                            placeholder: qsTr("结束")
                            onEdited: (t) => { root.ctl.draft.end = t }
                            onAccepted: root.save()
                            onEscaped: root.cancel()
                        }
                    }
                }
                TpField {
                    id: textIn
                    objectName: "schedTextIn"
                    width: parent.width
                    multiline: true
                    submitOnEnter: true
                    value: root.ctl.draft.text
                    placeholder: qsTr("内容")
                    onEdited: (t) => { root.ctl.draft.text = t }
                    onAccepted: root.save()
                    onEscaped: root.cancel()
                }
                Text {
                    visible: root.ctl.err !== ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: root.ctl.err
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.danger
                }
                Item {
                    width: parent.width
                    height: Theme.px(32)
                    Row {
                        spacing: Theme.px(6)
                        TpButton { objectName: "schedSave"; compact: true; kind: "primary"; text: qsTr("好"); onClicked: root.save() }
                        TpButton { compact: true; kind: "ghost"; text: qsTr("取消"); onClicked: root.cancel() }
                    }
                    TpButton {
                        objectName: "schedDelete"
                        visible: root.editJ >= 0
                        anchors.right: parent.right
                        compact: true
                        kind: "dangerOutline"
                        text: qsTr("删除")
                        onClicked: { const j = root.editJ; root.cancel(); root.deleter(j) }
                    }
                }
            }
        }
    }
}
