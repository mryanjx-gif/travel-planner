// 加 / 改一笔消费（0.10.1，P-11；PO 选 2B 弹出表单，照 Electron split.js editor）。0.10.2 分账（P-13）：谁付的、为谁付、平摊或按金额、添加同行人。
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
    // 分账：ps = 现有的同行人 + 这次新加的（保存时才写进数据）
    property var people: []
    property var ps: []
    property var newPeople: []
    property string payer: ""
    property var among: []
    property bool custom: false
    property var amounts: ({})
    property string addName: ""
    readonly property real amountNum: num(amount)
    readonly property real splitSum: among.reduce((k, id) => k + num(amounts[id]), 0)
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
        ps = people.slice()
        newPeople = []
        const r = row || {}
        payer = r.payer || (ps.length ? ps[0].id : "")
        among = r.among ? r.among.slice() : ps.map(p => p.id)
        custom = !!r.custom
        const am = {}
        for (const id in (r.amounts || {})) am[id] = String(r.amounts[id])
        amounts = am
        addName = ""
        addField.clear()
        open = true
        titleField.focusAll()
    }
    function num(t) { return parseFloat(String(t === undefined ? "" : t).replace(/,/g, "")) || 0 }
    function newId() {   // 和 Electron TP.uid 一样的样子
        let s = "pp_"
        for (let i = 0; i < 13; ++i) s += "0123456789abcdefghijklmnopqrstuvwxyz".charAt(Math.floor(Math.random() * 36))
        return s
    }
    function addPerson() {
        const name = addName.trim().slice(0, 20)
        addName = ""
        addField.clear()
        if (name === "" || ps.some(p => p.name === name)) return
        const list = ps.slice(), fresh = newPeople.slice(), who = among.slice()
        // 第一次加同行人时，先把"我"放在第一位：以前记的账都算"我"付的、只算我
        if (list.length === 0 && name !== qsTr("我")) {
            const me = { id: newId(), name: qsTr("我") }
            list.push(me); fresh.push(me); who.push(me.id)
            payer = me.id
        }
        const p = { id: newId(), name: name }
        list.push(p); fresh.push(p); who.push(p.id)
        if (payer === "") payer = p.id
        ps = list; newPeople = fresh; among = who
    }
    function toggleAmong(id) {
        const i = among.indexOf(id)
        if (i < 0) among = among.concat(id)
        else if (among.length > 1) among = among.filter(x => x !== id)
    }
    function setAmount(id, t) {
        const am = Object.assign({}, amounts)
        am[id] = t
        amounts = am
    }
    // 这一笔对每个人的影响（Electron rowNet；每笔一行时要点开才看得到，PO 选 5A）
    function netText() {
        const n = among.length
        if (ps.length < 2 || amountNum === 0 || n === 0) return ""
        return ps.map(p => {
            let v = p.id === payer ? amountNum : 0
            if (among.indexOf(p.id) >= 0) v -= custom ? num(amounts[p.id]) : amountNum / n
            return p.name + " " + (v > 0 ? "+" : "") + Fmt.money(v)
        }).join(Fmt.en ? ", " : "、")
    }
    function save() {
        const f = { title: titleText, amount: amount, currency: cur, cat: cat, date: date, notes: notes }
        if (newPeople.length > 0) f.newPeople = newPeople
        if (ps.length > 0) {
            const who = ps.filter(p => among.indexOf(p.id) >= 0)
            f.paidBy = payer
            f.split = custom && ps.length >= 2 ? who.map(p => ({ who: p.id, amount: amounts[p.id] || "" }))
                    : who.length === ps.length ? [] : who.map(p => ({ who: p.id }))
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
        width: parent.width
        height: Math.min(splitCol.implicitHeight, Math.max(Theme.px(110), root.height - Theme.px(430)))
        contentHeight: splitCol.implicitHeight
        interactive: contentHeight > height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: splitCol
            objectName: "expSplit"
            width: parent.width
            spacing: Theme.px(8)
            Column {
                visible: root.ps.length >= 2
                width: parent.width
                spacing: Theme.px(8)
                Text { text: qsTr("谁付的"); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted }
                Flow {
                    width: parent.width
                    spacing: Theme.px(6)
                    Repeater {
                        model: root.ps
                        delegate: TpChip {
                            required property var modelData
                            objectName: "expPayer-" + modelData.name
                            text: modelData.name
                            on: root.payer === modelData.id
                            onClicked: root.payer = modelData.id
                        }
                    }
                }
                Text {
                    objectName: "expEach"
                    text: qsTr("为谁付") + (!root.custom && root.among.length >= 2 && root.amountNum !== 0 ? " · " + qsTr("每人 %1").arg(Fmt.money(root.amountNum / root.among.length)) : "")
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                }
                Flow {
                    width: parent.width
                    spacing: Theme.px(6)
                    Repeater {
                        model: root.ps
                        delegate: TpChip {
                            required property var modelData
                            objectName: "expAmong-" + modelData.name
                            text: modelData.name
                            on: root.among.indexOf(modelData.id) >= 0
                            onClicked: root.toggleAmong(modelData.id)
                        }
                    }
                }
                TpCheck {
                    objectName: "expCustom"
                    width: parent.width
                    text: qsTr("不平摊，按金额分")
                    checked: root.custom
                    onToggled: (c) => { root.custom = c }
                }
                Column {
                    visible: root.custom
                    width: parent.width
                    spacing: Theme.px(4)
                    Repeater {
                        model: root.custom ? root.ps.filter(p => root.among.indexOf(p.id) >= 0) : []
                        delegate: Row {
                            id: amtRow
                            required property var modelData
                            spacing: Theme.px(8)
                            Text { width: Theme.px(110); anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight; text: amtRow.modelData.name; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text }
                            TpField {
                                objectName: "expAmt-" + amtRow.modelData.name
                                width: Theme.px(110)
                                value: root.amounts[amtRow.modelData.id] || ""
                                onEdited: (t) => root.setAmount(amtRow.modelData.id, t)
                                onAccepted: root.save()
                                onEscaped: root.open = false
                            }
                        }
                    }
                    Text {
                        objectName: "expSum"
                        text: qsTr("已分 %1 / %2").arg(Fmt.money(root.splitSum)).arg(Fmt.money(root.amountNum))
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Math.abs(root.splitSum - root.amountNum) > 0.005 ? Theme.danger : Theme.muted
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
            Row {
                width: parent.width
                spacing: Theme.px(8)
                TpField {
                    id: addField
                    objectName: "expAddName"
                    width: parent.width - addBtn.width - parent.spacing
                    placeholder: qsTr("添加同行人的名字，回车（一起出去时用来分账）")
                    value: root.addName
                    onEdited: (t) => { root.addName = t }
                    onAccepted: root.addPerson()
                    onEscaped: root.open = false
                }
                TpButton { id: addBtn; objectName: "expAddBtn"; compact: true; anchors.verticalCenter: parent.verticalCenter; text: qsTr("添加"); onClicked: root.addPerson() }
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
        visible: root.err !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: root.err === "badAmount" ? qsTr("金额要填数字") : root.err === "badDate" ? qsTr("日期不对")
              : root.err === "badSplit" ? qsTr("分摊加起来是 %1，和金额 %2 对不上").arg(Fmt.money(root.splitSum)).arg(Fmt.money(root.amountNum))
              : qsTr("没有保存：这一笔可能已经被删了")
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
    }
    Row {
        anchors.right: parent.right
        spacing: Theme.px(8)
        TpButton { text: qsTr("取消"); onClicked: root.open = false }
        TpButton { objectName: "expOk"; kind: "primary"; text: qsTr("保存"); onClicked: root.save() }
    }
}
