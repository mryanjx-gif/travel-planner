// 加 / 改一笔消费（0.10.1，P-11；PO 选 2B 弹出表单，照 Electron split.js editor）。0.10.2 分账（P-13）；0.11.5 改成：有同行人时直接显示，
// "和大家 A 钱"开关，每人一行勾选和金额：填了的固定，没填的平分剩下的（标"自动"）。加人在同行人窗口（PeopleDialog）。
// show(row, draft)：row = 改这一笔；draft = 一行快记"展开更多"带过来的内容。
import QtQuick
import TPApp

ModalLayer {
    id: root
    property string planCurrency: ""
    property string today: ""
    property string expenseId: ""
    property string titleText: ""
    property string amount: ""
    property string cur: ""
    property string cat: "food"
    property string date: ""
    property string notes: ""
    property string err: ""
    // 分账：share = 和大家 A 钱（关 = 只算付款人）；amounts 只有填了的人（id → 文字）
    property var people: []
    property string payer: ""
    property bool share: true
    property var among: []
    property var amounts: ({})
    property bool splitTouched: false   // 这次动过分摊：同行人窗口加了人时不覆盖
    property string lastIds: ""
    readonly property real amountNum: num(amount)
    readonly property bool multi: people.length >= 2
    readonly property var fixedIds: among.filter(id => amounts[id] !== undefined)
    readonly property real fixedSum: fixedIds.reduce((k, id) => k + num(amounts[id]), 0)
    readonly property real autoEach: among.length > fixedIds.length ? (amountNum - fixedSum) / (among.length - fixedIds.length) : 0
    // 不能保存的原因（写清差多少）
    readonly property string splitProblem: {
        if (!multi || !share || fixedIds.length === 0) return ""
        if (fixedIds.some(id => isNaN(Number(String(amounts[id]).replace(/,/g, ""))))) return qsTr("金额要填数字")
        const gap = Math.abs(fixedSum - amountNum)
        if (fixedIds.length === among.length && gap > 0.005)
            return qsTr("每个人都填了：加起来 %1，总额 %2，差 %3").arg(Fmt.money(fixedSum)).arg(Fmt.money(amountNum)).arg(Fmt.money(gap))
        const over = Math.abs(fixedSum) - Math.abs(amountNum)
        return over > 0.005 ? qsTr("填的金额加起来 %1，超过总额 %2，多了 %3").arg(Fmt.money(fixedSum)).arg(Fmt.money(amountNum)).arg(Fmt.money(over)) : ""
    }
    signal peopleRequested()
    cardWidth: 480
    onDismissed: open = false

    function show(row, draft) {
        root.forceActiveFocus()   // 上次的输入框可能还留着焦点，那样新内容写不进去（TpField）
        const x = row || draft || {}
        expenseId = row ? row.id : ""
        titleText = x.title || ""
        amount = x.amount !== undefined && x.amount !== "" ? String(x.amount) : ""
        cur = x.cur || planCurrency
        cat = x.cat || "food"
        date = row ? (row.date || "") : today
        notes = x.notes || ""
        err = ""
        loadSplit(row || {})
        open = true
        titleField.focusAll()
    }
    function num(t) { return parseFloat(String(t === undefined ? "" : t).replace(/,/g, "")) || 0 }
    // 从这一笔读出分摊（新的一笔：第一个人付、所有人平摊）。旧的按金额数据显示成全部已填
    function loadSplit(r) {
        const ids = people.map(p => p.id)
        lastIds = JSON.stringify(ids)
        payer = r.payer || (ids.length ? ids[0] : "")
        const onlyPayer = !!r.among && r.among.length === 1 && r.among[0] === payer
        share = !onlyPayer
        among = r.among && !onlyPayer ? r.among.slice() : ids
        const am = {}
        if (!onlyPayer) for (const id in (r.amounts || {})) am[id] = String(r.amounts[id])
        amounts = am
        splitTouched = false
    }
    // 窗口开着时同行人变了（同行人窗口里加、删）：没动过就重读这一笔（可能刚被自动平摊），动过就只去掉不在了的人
    onPeopleChanged: {
        const ids = people.map(p => p.id)
        if (!open || JSON.stringify(ids) === lastIds) return
        if (!splitTouched) {
            loadSplit(expenseId === "" ? {} : (((planPage.info.money || {}).rows || []).find(x => x.id === expenseId) || {}))
            return
        }
        lastIds = JSON.stringify(ids)
        if (ids.indexOf(payer) < 0) payer = ids.length ? ids[0] : ""
        const left = among.filter(id => ids.indexOf(id) >= 0)
        among = left.length ? left : ids
        const am = {}
        for (const id in amounts) if (ids.indexOf(id) >= 0) am[id] = amounts[id]
        amounts = am
    }
    function setShare(c) { share = c; splitTouched = true }
    function setPayer(id) { payer = id; splitTouched = true }
    function toggleAmong(id) {
        splitTouched = true
        const i = among.indexOf(id)
        if (i < 0) { among = among.concat(id); return }
        if (among.length <= 1) return
        among = among.filter(x => x !== id)
        setAmount(id, "")
    }
    function setAmount(id, t) {
        splitTouched = true
        const am = Object.assign({}, amounts)
        if (String(t).trim() === "") delete am[id]
        else am[id] = t
        amounts = am
    }
    function shareOf(id) { return among.indexOf(id) < 0 ? 0 : amounts[id] !== undefined ? num(amounts[id]) : autoEach }
    // 这一笔对每个人的影响（Electron rowNet；每笔一行时要点开才看得到，PO 选 5A）
    function netText() {
        if (!multi || !share || amountNum === 0) return ""
        return people.map(p => {
            const v = (p.id === payer ? amountNum : 0) - shareOf(p.id)
            return p.name + " " + (v > 0 ? "+" : "") + Fmt.money(v)
        }).join(Fmt.en ? ", " : "、")
    }
    function save() {
        if (splitProblem !== "") return
        const f = { title: titleText, amount: amount, currency: cur, cat: cat, date: date, notes: notes }
        if (multi) {
            const who = people.filter(p => among.indexOf(p.id) >= 0)
            f.paidBy = payer
            f.split = !share ? [{ who: payer }]
                    : who.length === people.length && fixedIds.length === 0 ? []
                    : who.map(p => amounts[p.id] !== undefined ? { who: p.id, amount: amounts[p.id] } : { who: p.id })
        } else if (people.length > 0) {
            f.paidBy = people[0].id
            f.split = []
        }
        err = planPage.saveExpense(expenseId, f) || ""
        if (err === "") open = false
    }

    Text {
        width: parent.width
        text: root.expenseId === "" ? qsTr("记一笔") : qsTr("修改")
        font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
    }
    TpField {
        id: titleField
        objectName: "expTitle"
        width: parent.width
        placeholder: qsTr("内容，如 晚饭、出租车")
        value: root.titleText
        onEdited: (t) => { root.titleText = t }
        onAccepted: root.save()
        onEscaped: root.open = false
    }
    Row {
        width: parent.width
        spacing: Theme.px(10)
        TpField {
            objectName: "expAmount"
            width: parent.width - Theme.px(100) - parent.spacing
            placeholder: qsTr("金额（退款填负数）")
            value: root.amount
            bad: root.err === "badAmount"
            onEdited: (t) => { root.amount = t }
            onAccepted: root.save()
            onEscaped: root.open = false
        }
        TpSelect {
            objectName: "expCur"
            width: Theme.px(100)
            options: (rates.currencies.indexOf(root.planCurrency) < 0 && root.planCurrency !== "" ? [root.planCurrency] : []).concat(rates.currencies).map(c => ({ key: c, text: c }))
            current: root.cur
            onPicked: (k) => { root.cur = k }
        }
    }
    Row {
        width: parent.width
        spacing: Theme.px(10)
        TpSelect {
            objectName: "expCat"
            width: (parent.width - parent.spacing) / 2
            options: ["transport", "stay", "food", "ticket", "shop", "other"].map(k => ({ key: k, text: Fmt.catLabel(k) }))
            current: root.cat
            onPicked: (k) => { root.cat = k }
        }
        DateField { width: (parent.width - parent.spacing) / 2; value: root.date; onEdited: (v) => { root.date = v } }
    }
    // ---- 分账：窗口矮时这一块自己滚动 ----
    Flickable {
        visible: root.multi
        width: parent.width
        height: Math.min(splitCol.implicitHeight, Math.max(Theme.px(110), root.height - Theme.px(460)))
        contentHeight: splitCol.implicitHeight
        interactive: contentHeight > height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: splitCol
            objectName: "expSplit"
            width: parent.width
            spacing: Theme.px(8)
            TpCheck {
                objectName: "expShare"
                asSwitch: true
                width: parent.width
                text: qsTr("和大家 A 钱")
                checked: root.share
                onToggled: (c) => root.setShare(c)
            }
            // 开关关着也要能改付款人（关 = 只算付款人）
            Text { text: qsTr("谁付的"); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted }
            Flow {
                width: parent.width
                spacing: Theme.px(6)
                Repeater {
                    model: root.people
                    delegate: TpChip {
                        required property var modelData
                        objectName: "expPayer-" + modelData.name
                        text: modelData.name
                        on: root.payer === modelData.id
                        onClicked: root.setPayer(modelData.id)
                    }
                }
            }
            Column {
                visible: root.share
                width: parent.width
                spacing: Theme.px(6)
                Text { text: qsTr("为谁付"); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted }
                Repeater {
                    model: root.people
                    delegate: Item {
                        id: amtRow
                        required property var modelData
                        readonly property bool joined: root.among.indexOf(modelData.id) >= 0
                        readonly property bool fixed: root.amounts[modelData.id] !== undefined
                        width: parent.width
                        height: Theme.px(32)
                        TpCheck {
                            objectName: "expAmong-" + amtRow.modelData.name
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - amtTail.width - Theme.px(8)
                            text: amtRow.modelData.name
                            checked: amtRow.joined
                            onToggled: root.toggleAmong(amtRow.modelData.id)
                        }
                        Row {
                            id: amtTail
                            visible: amtRow.joined
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.px(6)
                            TpField {
                                id: amtField
                                objectName: "expAmt-" + amtRow.modelData.name
                                width: Theme.px(110)
                                placeholder: Fmt.money(root.autoEach)
                                value: root.amounts[amtRow.modelData.id] || ""
                                onEdited: (t) => root.setAmount(amtRow.modelData.id, t)
                                onAccepted: root.save()
                                onEscaped: root.open = false
                            }
                            Item {
                                width: clearBtn.implicitWidth; height: Theme.px(32)   // 两种状态一样宽，金额框才对齐
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    objectName: "expAuto-" + amtRow.modelData.name
                                    visible: !amtRow.fixed
                                    anchors.centerIn: parent
                                    text: qsTr("自动")
                                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                                }
                                TpButton {
                                    id: clearBtn
                                    objectName: "expClear-" + amtRow.modelData.name
                                    visible: amtRow.fixed
                                    anchors.fill: parent
                                    kind: "ghost"
                                    compact: true
                                    text: qsTr("恢复平摊 ×")
                                    Accessible.name: qsTr("恢复平摊")
                                    onClicked: { root.setAmount(amtRow.modelData.id, ""); amtField.clear() }
                                }
                            }
                        }
                    }
                }
                Text {
                    objectName: "expNet"
                    visible: text !== ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: root.netText() === "" ? "" : qsTr("这一笔：%1").arg(root.netText())
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                }
            }
        }
    }
    TpField {
        width: parent.width
        multiline: true
        maxLines: 4
        placeholder: qsTr("备注")
        value: root.notes
        onEdited: (t) => { root.notes = t }
        onEscaped: root.open = false
    }
    Text {
        objectName: "expErr"
        visible: root.splitProblem !== "" || root.err !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: root.splitProblem !== "" ? root.splitProblem
              : root.err === "badAmount" ? qsTr("金额要填数字") : root.err === "badDate" ? qsTr("日期不对")
              : qsTr("没有保存：这一笔可能已经被删了")
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
    }
    Item {
        width: parent.width
        height: footRow.implicitHeight
        TpButton {
            objectName: "expPeople"
            compact: true
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("管理同行人")
            onClicked: root.peopleRequested()
        }
        Row {
            id: footRow
            anchors.right: parent.right
            spacing: Theme.px(8)
            TpButton { text: qsTr("取消"); onClicked: root.open = false }
            TpButton { objectName: "expOk"; kind: "primary"; text: qsTr("保存"); onClicked: root.save() }
        }
    }
}
