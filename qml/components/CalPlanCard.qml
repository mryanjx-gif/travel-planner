// 日历的弹出卡（0.11.2，C-09；照 Electron pages-calendar.js openPop）：改名、开始日期 + 天数、状态、8 色 + 自定义色、隐藏、移除日期、打开计划页。Enter 保存。
// 卡片放在日历页最上层；点别处或 Esc 收起（Esc 在 Main.qml 的 escChain）。
import QtQuick
import TPApp

Item {
    id: root
    property bool open: false
    property var plan: ({})                 // calendar.planFields
    property string name: ""
    property string start: ""
    property string days: "1"
    property string status: "planning"
    property string color: ""
    property bool pickedColor: false
    property bool customOpen: false
    signal saved(var result)
    signal hideRequested(string id, string name)
    signal removeDatesRequested(string id)
    signal openPlanRequested(string id)
    signal warn(string text)
    readonly property var presets: ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
    readonly property string endText: {
        const n = Math.round(Number(days))
        return Fmt.ok(start) && n >= 1 && n <= 999 ? qsTr("结束：%1").arg(Fmt.addDays(start, n - 1)) : ""
    }
    anchors.fill: parent
    visible: open

    function show(id, ax, ay, aw, ah) {
        plan = calendar.planFields(id)
        if (plan.id === undefined) return
        name = plan.name; start = plan.start; days = String(plan.days); status = plan.status; color = plan.color
        pickedColor = false
        customOpen = false
        nameField.value = name
        open = true
        // 放在点的东西下面，放不下放上面（Electron showPop）
        card.x = Math.max(Theme.px(10), Math.min(root.width - card.width - Theme.px(10), ax + Math.min(aw, Theme.px(240)) / 2 - card.width / 2))
        const below = ay + ah + Theme.px(8)
        card.y = below + card.implicitHeight > root.height - Theme.px(10) ? Math.max(Theme.px(10), ay - card.implicitHeight - Theme.px(8)) : below
    }
    function save() {
        const r = calendar.savePlan(plan.id, { name: nameField.text, start: start, days: days, status: status, color: pickedColor ? color : "" })
        if (r.error === "name") { warn(qsTr("名称不能为空")); return }
        if (r.error === "days") { warn(qsTr("天数要在 1 到 999 之间")); return }
        if (r.error === "date") { warn(qsTr("日期不对")); return }
        open = false
        root.saved(r)
    }

    MouseArea { anchors.fill: parent; onPressed: root.open = false; onWheel: (w) => w.accepted = true }   // 点别处收起
    Rectangle {
        id: card
        objectName: "calCard"
        width: Theme.px(340)
        implicitHeight: col.implicitHeight + Theme.px(28)
        height: implicitHeight
        radius: Theme.px(14)
        color: "#fbfcfd"
        border.width: 1; border.color: Theme.lineStrong
        MouseArea { anchors.fill: parent; onWheel: (w) => w.accepted = true }   // 卡片上的点不算"别处"
        Column {
            id: col
            x: Theme.px(14); y: Theme.px(14)
            width: parent.width - Theme.px(28)
            spacing: Theme.px(10)
            Row {
                width: parent.width
                spacing: Theme.px(8)
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: Theme.px(12); height: width; radius: width / 2; color: root.color }
                TpField {
                    id: nameField
                    objectName: "calCardName"
                    width: parent.width - Theme.px(20)
                    bold: true
                    fontSize: 15
                    placeholder: qsTr("计划名称")
                    onAccepted: root.save()
                }
            }
            Row {
                width: parent.width
                spacing: Theme.px(10)
                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: Theme.px(3)
                    Text { text: qsTr("开始"); font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.muted }
                    DateField { objectName: "calCardStart"; width: parent.width; value: root.start; onEdited: (v) => root.start = v }
                }
                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: Theme.px(3)
                    Text { text: qsTr("天数"); font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.muted }
                    TpField { id: daysField; objectName: "calCardDays"; width: parent.width; value: root.days; onEdited: (t) => root.days = t; onAccepted: root.save() }
                }
            }
            Row {
                width: parent.width
                spacing: Theme.px(10)
                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: Theme.px(3)
                    Text { text: qsTr("状态"); font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.muted }
                    TpSelect {
                        objectName: "calCardStatus"
                        width: parent.width
                        options: [{ key: "planning", text: qsTr("计划中") }, { key: "wish", text: qsTr("想去") }, { key: "completed", text: qsTr("已完成") }]
                        current: root.status
                        onPicked: (k) => root.status = k
                    }
                }
                Text {
                    objectName: "calCardEnd"
                    width: (parent.width - parent.spacing) / 2
                    height: Theme.px(52)
                    verticalAlignment: Text.AlignBottom
                    bottomPadding: Theme.px(7)
                    text: root.endText
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
                }
            }
            Row {
                spacing: Theme.px(6)
                Text { anchors.verticalCenter: parent.verticalCenter; rightPadding: Theme.px(4); text: qsTr("颜色"); font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.muted }
                Repeater {
                    model: root.presets
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        objectName: "calCardColor-" + index
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.px(20); height: width; radius: width / 2
                        color: modelData
                        border.width: String(root.color).toLowerCase() === modelData.toLowerCase() ? 2 : 0
                        border.color: Theme.text
                        MouseArea { anchors.fill: parent; anchors.margins: -2; cursorShape: Qt.PointingHandCursor; onClicked: { root.color = parent.modelData; root.pickedColor = true } }
                    }
                }
                LinkText { anchors.verticalCenter: parent.verticalCenter; leftPadding: Theme.px(4); small: true; muted: true; text: qsTr("自定义"); onClicked: root.customOpen = !root.customOpen }
            }
            ColorWheel {
                visible: root.customOpen
                width: parent.width
                color: root.color || "#2a78d6"
                onPicked: (c) => { root.color = c; root.pickedColor = true }
            }
            Row {
                spacing: Theme.px(6)
                TpButton { objectName: "calCardHide"; compact: true; text: qsTr("在日历里隐藏"); onClicked: { root.open = false; root.hideRequested(root.plan.id, root.name) } }
                TpButton { objectName: "calCardUndate"; compact: true; visible: Fmt.ok(root.plan.start); text: qsTr("移除日期"); onClicked: { root.open = false; root.removeDatesRequested(root.plan.id) } }
            }
            Item {
                width: parent.width
                height: Theme.px(32)
                LinkText { objectName: "calCardOpen"; anchors.verticalCenter: parent.verticalCenter; text: qsTr("打开计划页 →"); onClicked: { root.open = false; root.openPlanRequested(root.plan.id) } }
                Row {
                    anchors.right: parent.right
                    spacing: Theme.px(6)
                    TpButton { compact: true; text: qsTr("取消"); onClicked: root.open = false }
                    TpButton { objectName: "calCardSave"; compact: true; kind: "primary"; text: qsTr("保存"); onClicked: root.save() }
                }
            }
        }
    }
}
