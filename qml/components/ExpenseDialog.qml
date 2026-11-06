// 加 / 改一笔消费（0.10.1，P-11；PO 选 2B 弹出表单，照 Electron split.js editor 去掉分账，分账 0.10.2 加在这里）。
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
        open = true
        titleField.focusAll()
    }
    function save() {
        err = planPage.saveExpense(expenseId, { title: titleText, amount: amount, currency: cur, cat: cat, date: date, notes: notes }) || ""
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
        text: root.err === "badAmount" ? qsTr("金额要填数字") : root.err === "badDate" ? qsTr("日期不对") : qsTr("没有保存：这一笔可能已经被删了")
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
    }
    Row {
        anchors.right: parent.right
        spacing: Theme.px(8)
        TpButton { text: qsTr("取消"); onClicked: root.open = false }
        TpButton { objectName: "expOk"; kind: "primary"; text: qsTr("保存"); onClicked: root.save() }
    }
}
