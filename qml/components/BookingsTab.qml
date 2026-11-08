// 计划页的"交通住宿"标签（0.10.1，P-08；照 Electron pages-plan.js bookingsHtml）。按天分组、合计；改和加都是弹出表单（PO 选 2B，BookingDialog）。
// J1：顶上列出还没有住宿记录的晚上，点"这几晚不用"关掉（存在 settings.json，不改数据）。
// info = planPage.info（bookings、bookCount、bookCost、nights、nightsUndated、money.currency）。
import QtQuick
import TPApp

Column {
    id: root
    property var info: ({})
    property bool readOnly: false
    signal add(string kind)
    signal edit(var row)
    signal remove(var row)
    spacing: Theme.px(10)

    readonly property string cur: (info.money || {}).currency || ""
    readonly property var nights: info.nights || []
    readonly property int count: info.bookCount || 0

    function dt(v) { return String(v || "").replace("T", " ") }
    function timeText(b) {
        const day = (b.start || "").slice(0, 10)
        if (b.kind === "hotel")
            return [b.start ? qsTr("入住 %1").arg(dt(b.start)) : "", b.end ? qsTr("退房 %1").arg(dt(b.end)) : ""].filter(x => x).join(" · ")
        return [dt(b.start), dt(b.end).replace(day + " ", "")].filter(x => x).join(" – ")
    }
    function nightsText() {
        const shown = nights.slice(0, 6).map(d => Fmt.md(d)).join(Fmt.en ? ", " : "、")
        return nights.length > 6 ? qsTr("%1 等 %2 晚还没有住宿记录").arg(shown).arg(nights.length)
             : nights.length === 1 && Fmt.en ? "No stay booked for the night of " + shown
             : qsTr("%1 这 %2 晚还没有住宿记录").arg(shown).arg(nights.length)
    }

    // ---- J1 ----
    Rectangle {
        objectName: "tripNights"
        visible: root.nights.length > 0
        width: parent.width
        height: visible ? nightsCol.implicitHeight + Theme.px(16) : 0
        radius: Theme.radiusControl
        color: "#fbf1dc"
        Column {
            id: nightsCol
            x: Theme.px(12); y: Theme.px(8)
            width: parent.width - Theme.px(24)
            spacing: Theme.px(3)
            Text {
                objectName: "tripNightsText"
                width: parent.width
                wrapMode: Text.Wrap
                text: root.nights.length ? root.nightsText() : ""
                font.family: serifFamily; font.pixelSize: Theme.px(13); color: "#7a5a1c"
            }
            Text {
                visible: (root.info.nightsUndated || 0) > 0
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("另有 %1 条住宿没填日期，没算进去。").arg(root.info.nightsUndated || 0)
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: "#7a5a1c"
            }
            LinkText {
                objectName: "tripNightsOff"
                visible: !root.readOnly
                small: true
                tone: "#7a5a1c"
                text: qsTr("这几晚不用")
                onClicked: planPage.dismissNights()
            }
        }
    }

    Text {
        objectName: "tripBookSum"
        visible: root.count > 0
        text: qsTr("%1 项").arg(root.count) + ((root.info.bookCost || 0) !== 0 ? " · " + qsTr("合计 %1").arg(Fmt.money(root.info.bookCost, root.cur)) : "")
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
    }
    Flow {
        visible: !root.readOnly
        width: parent.width
        spacing: Theme.px(6)
        Repeater {
            model: ["flight", "train", "bus", "ferry", "car", "hotel", "other"]
            delegate: TpButton {
                required property string modelData
                objectName: "bookAdd-" + modelData
                compact: true
                text: "+ " + Fmt.kindLabel(modelData)
                onClicked: root.add(modelData)
            }
        }
    }
    Text {
        visible: root.count === 0
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("还没有交通住宿。")
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
    }

    Repeater {
        model: root.info.bookings || []
        delegate: Column {
            required property var modelData
            width: root.width
            spacing: Theme.px(6)
            Text {
                topPadding: Theme.px(4)
                text: modelData.day ? Fmt.dayLabel(modelData.day) : qsTr("没填时间")
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); font.bold: true; color: Theme.muted
            }
            Repeater {
                model: modelData.rows
                delegate: TpCard {
                    id: card
                    required property var modelData
                    objectName: "bookRow"
                    width: root.width
                    height: col.implicitHeight + Theme.px(20)
                    radius: Theme.px(14)
                    color: Theme.card
                    RowAct {
                        id: cardAct
                        objectName: "bookAct"
                        enabled: !root.readOnly
                        editText: qsTr("修改")
                        onActivated: root.edit(card.modelData)
                        onDeleteRequested: root.remove(card.modelData)
                    }
                    Column {
                        id: col
                        x: Theme.px(12); y: Theme.px(10)
                        width: parent.width - Theme.px(24) - (root.readOnly ? 0 : Theme.px(60))
                        spacing: Theme.px(3)
                        Flow {
                            width: parent.width
                            spacing: Theme.px(6)
                            Rectangle {
                                width: kindText.implicitWidth + Theme.px(12); height: Theme.px(20); radius: height / 2
                                color: Theme.accentSoft
                                Text { id: kindText; anchors.centerIn: parent; text: Fmt.kindLabel(card.modelData.kind); font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.accentInk }
                            }
                            Text {
                                text: card.modelData.title || Fmt.kindLabel(card.modelData.kind)
                                font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: Theme.text
                            }
                            Text {
                                visible: !!(card.modelData.from || card.modelData.to)
                                text: (card.modelData.from ? card.modelData.from + " → " : "") + (card.modelData.to || "?")
                                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
                            }
                        }
                        Text {
                            readonly property string line: [root.timeText(card.modelData), card.modelData.ref ? qsTr("订单号 %1").arg(card.modelData.ref) : "",
                                                            card.modelData.cost ? Fmt.money(card.modelData.cost, root.cur) : ""].filter(x => x).join(" · ")
                            visible: line !== ""
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: line
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                        }
                        Text {
                            visible: !!card.modelData.notes
                            width: parent.width
                            wrapMode: Text.Wrap
                            lineHeight: 1.25
                            text: card.modelData.notes || ""
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                        }
                    }
                    Row {
                        visible: !root.readOnly
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.px(8)
                        y: Theme.px(8)
                        spacing: Theme.px(4)
                        RowIcon { objectName: "bookEdit"; name: "edit"; label: qsTr("修改"); onClicked: root.edit(card.modelData) }
                        RowIcon { objectName: "bookDel"; opacity: cardAct.active ? 1 : 0; name: "trash"; label: qsTr("删除"); onClicked: root.remove(card.modelData) }
                    }
                }
            }
        }
    }

    component RowIcon: Item {
        id: ic
        property string name: ""
        property string label: ""
        signal clicked()
        width: Theme.px(26); height: width
        Accessible.role: Accessible.Button
        Accessible.name: label
        Rectangle { anchors.fill: parent; radius: width / 2; color: icMa.containsMouse ? Theme.panel2 : "transparent" }
        TpIcon { anchors.centerIn: parent; width: Theme.px(15); height: width; name: ic.name; color: Theme.muted }
        TpTip { text: ic.label; show: icMa.containsMouse }
        MouseArea { id: icMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ic.clicked() }
    }
}
