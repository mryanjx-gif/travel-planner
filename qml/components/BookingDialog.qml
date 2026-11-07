// 加 / 改一条交通住宿（0.10.1，P-08；PO 选 2B 弹出表单，照 Electron pages-plan.js editBooking）。住宿没有出发地、目的地，"出发 / 到达"叫"入住 / 退房"。
// 保存走 planPage.saveBooking，数据在副本上试过，有问题时什么都不改。
import QtQuick
import TPApp

ModalLayer {
    id: root
    property string currency: ""
    property string bookingId: ""
    property string kind: "other"
    property string titleText: ""
    property string from: ""
    property string to: ""
    property string startDate: ""
    property string startTime: ""
    property string endDate: ""
    property string endTime: ""
    property string ref: ""
    property string cost: ""
    property string notes: ""
    property bool toExp: false
    property bool linked: false
    property string err: ""
    readonly property bool isNew: bookingId === ""
    readonly property bool hotel: kind === "hotel"
    cardWidth: 540
    onDismissed: open = false

    function split(v, part) {
        const s = String(v || "")
        const i = s.search(/[T ]/)
        return part === 0 ? (i < 0 ? s : s.slice(0, i)) : (i < 0 ? "" : s.slice(i + 1, i + 6))
    }
    function show(row, k) {
        root.forceActiveFocus()   // 上次的输入框可能还留着焦点，那样新内容写不进去（TpField）
        const b = row || {}
        bookingId = b.id || ""
        kind = b.kind || k || "other"
        titleText = b.title || ""
        from = b.from || ""
        to = b.to || ""
        startDate = split(b.start, 0); startTime = split(b.start, 1)
        endDate = split(b.end, 0); endTime = split(b.end, 1)
        ref = b.ref || ""
        cost = b.cost ? String(b.cost) : ""
        notes = b.notes || ""
        toExp = false
        linked = !!b.linked
        err = ""
        open = true
        titleField.focusAll()
    }
    function save() {
        err = planPage.saveBooking(bookingId, { kind: kind, title: titleText, from: from, to: to, startDate: startDate, startTime: startTime,
                                                endDate: endDate, endTime: endTime, ref: ref, cost: cost, notes: notes, toExp: toExp }) || ""
        if (err === "") open = false
    }
    function errText(e) {
        return e === "badCost" ? qsTr("费用要填数字")
             : e === "badStart" ? (hotel ? qsTr("入住时间不对") : qsTr("出发时间不对"))
             : e === "badEnd" ? (hotel ? qsTr("退房时间不对") : qsTr("到达时间不对"))
             : e === "endBeforeStart" ? (hotel ? qsTr("退房不能早于入住") : qsTr("到达不能早于出发"))
             : e === "" ? "" : qsTr("没有保存：这条记录可能已经被删了")
    }

    Text {
        width: parent.width
        text: root.isNew ? qsTr("添加%1").arg(Fmt.kindLabel(root.kind)) : qsTr("修改")
        font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
    }
    Flickable {
        width: parent.width
        height: Math.min(form.implicitHeight, root.height - Theme.px(190))
        contentHeight: form.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: form
            width: parent.width
            spacing: Theme.px(10)
            Row {
                width: parent.width
                spacing: Theme.px(10)
                TpSelect {
                    objectName: "bookKind"
                    width: Theme.px(110)
                    options: ["flight", "train", "bus", "ferry", "car", "hotel", "ticket", "other"].map(k => ({ key: k, text: Fmt.kindLabel(k) }))
                    current: root.kind
                    onPicked: (k) => { root.kind = k }
                }
                TpField {
                    id: titleField
                    objectName: "bookTitle"
                    width: parent.width - Theme.px(110) - parent.spacing
                    placeholder: root.hotel ? qsTr("酒店名称，如 Hilton Darwin") : qsTr("名称，如 QF 840 / 动车 D2655")
                    value: root.titleText
                    onEdited: (t) => { root.titleText = t }
                    onAccepted: root.save()
                    onEscaped: root.open = false
                }
            }
            Row {
                visible: !root.hotel
                width: parent.width
                spacing: Theme.px(10)
                TpField { width: (parent.width - parent.spacing) / 2; placeholder: qsTr("出发地"); value: root.from; onEdited: (t) => { root.from = t }; onAccepted: root.save(); onEscaped: root.open = false }
                TpField { width: (parent.width - parent.spacing) / 2; placeholder: qsTr("目的地"); value: root.to; onEdited: (t) => { root.to = t }; onAccepted: root.save(); onEscaped: root.open = false }
            }
            Row {
                width: parent.width
                spacing: Theme.px(8)
                Text { width: Theme.px(66); anchors.verticalCenter: parent.verticalCenter; text: root.hotel ? qsTr("入住") : qsTr("出发时间"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft }
                DateField { objectName: "bookStartDate"; width: parent.width - Theme.px(66 + 90) - 2 * parent.spacing; value: root.startDate; onEdited: (v) => { root.startDate = v } }
                TpField { objectName: "bookStartTime"; width: Theme.px(90); placeholder: qsTr("时:分"); value: root.startTime; bad: root.err === "badStart"; onEdited: (t) => { root.startTime = t }; onAccepted: root.save(); onEscaped: root.open = false }
            }
            Row {
                width: parent.width
                spacing: Theme.px(8)
                Text { width: Theme.px(66); anchors.verticalCenter: parent.verticalCenter; text: root.hotel ? qsTr("退房") : qsTr("到达时间"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft }
                DateField { objectName: "bookEndDate"; width: parent.width - Theme.px(66 + 90) - 2 * parent.spacing; value: root.endDate; minDate: root.startDate; bad: root.err === "endBeforeStart"; onEdited: (v) => { root.endDate = v } }
                TpField { objectName: "bookEndTime"; width: Theme.px(90); placeholder: qsTr("时:分"); value: root.endTime; bad: root.err === "badEnd"; onEdited: (t) => { root.endTime = t }; onAccepted: root.save(); onEscaped: root.open = false }
            }
            Row {
                width: parent.width
                spacing: Theme.px(10)
                TpField { width: (parent.width - parent.spacing) / 2; placeholder: qsTr("订单号 / 确认码"); value: root.ref; onEdited: (t) => { root.ref = t }; onAccepted: root.save(); onEscaped: root.open = false }
                TpField {
                    objectName: "bookCost"
                    width: (parent.width - parent.spacing) / 2
                    placeholder: qsTr("费用（%1）").arg(root.currency)
                    value: root.cost
                    bad: root.err === "badCost"
                    onEdited: (t) => { root.cost = t }
                    onAccepted: root.save()
                    onEscaped: root.open = false
                }
            }
            TpField {
                width: parent.width
                multiline: true
                maxLines: 5
                placeholder: qsTr("备注：座位、行李额、地址、取票方式……")
                value: root.notes
                onEdited: (t) => { root.notes = t }
                onEscaped: root.open = false
            }
            TpCheck {
                objectName: "bookToExp"
                visible: !root.linked && !(planPage.info.money || {}).multi   // 改旧的也能勾：要分摊就得先记到消费（0.10.2）；有同行人时自动记（0.11.5）
                text: root.isNew ? qsTr("同时单独记一笔到“消费”（可以单独改）") : qsTr("记一笔到“消费”（可以单独改、分摊）")
                checked: root.toExp
                onToggled: (c) => { root.toExp = c }
            }
        }
    }
    Text {
        objectName: "bookErr"
        visible: root.err !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: root.errText(root.err)
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
    }
    Row {
        anchors.right: parent.right
        spacing: Theme.px(8)
        TpButton { text: qsTr("取消"); onClicked: root.open = false }
        TpButton { objectName: "bookOk"; kind: "primary"; text: qsTr("保存"); onClicked: root.save() }
    }
}
