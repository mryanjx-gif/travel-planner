// 计划页的"消费"标签（0.10.1，P-11、P-12；照 Electron pages-plan.js budgetHtml）。PO 选：1A 交通住宿的费用合在一起算、分开显示，
// 3A 页头直接选主货币，J3 一行快记。每笔一行，宽窄窗口同一种版式（点一行改，弹出表单 ExpenseDialog）。
// money = planPage.info.money（plans::spendInfo）。分账 0.10.2 才有。
import QtQuick
import TPApp

Column {
    id: root
    property var money: ({})
    property bool readOnly: false
    signal edit(var row)
    signal remove(var row)
    signal more(var draft)              // "展开更多"：把一行快记里已经填的带到完整表单
    signal budgetRequested()
    spacing: Theme.px(12)

    readonly property string cur: money.currency || ""
    readonly property var rows: money.rows || []
    readonly property var cats: money.cats || []
    readonly property var missing: money.missing || []
    readonly property var mainMissing: money.mainMissing || []
    readonly property bool hasBoth: (money.bookTotal || 0) !== 0 && (money.expTotal || 0) !== 0

    // 一行快记（J3）
    property string qCat: "food"
    property string qTitle: ""
    property string qAmount: ""
    property bool qBad: false
    function focusQuick() { quickTitle.focusAll() }
    function submitQuick() {
        const err = planPage.saveExpense("", { title: qTitle, amount: qAmount, cat: qCat, date: plans.today })
        qBad = !!err
        if (qBad) { quickAmount.focusAll(); return }
        qTitle = ""; qAmount = ""
        quickTitle.clear(); quickAmount.clear()
        quickTitle.focusAll()
    }
    function catOptions() { return ["transport", "stay", "food", "ticket", "shop", "other"].map(k => ({ key: k, text: Fmt.catLabel(k) })) }

    // ---- 合计 ----
    Rectangle {
        width: parent.width
        height: sumCol.implicitHeight + Theme.px(24)
        radius: Theme.px(16)
        color: "#ffffff"
        border.width: 1; border.color: Theme.line
        Column {
            id: sumCol
            x: Theme.px(14); y: Theme.px(12)
            width: parent.width - Theme.px(28)
            spacing: Theme.px(6)
            Item {
                width: parent.width
                height: Math.max(bigCol.implicitHeight, mainSel.height)
                Column {
                    id: bigCol
                    width: parent.width - mainSel.width - Theme.px(10)
                    spacing: Theme.px(1)
                    Text { text: qsTr("已花"); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted }
                    Text {
                        objectName: "moneyTotal"
                        text: Fmt.money(root.money.total, root.cur)
                        font.family: serifFamily; font.pixelSize: Theme.px(24); font.bold: true; color: Theme.text
                    }
                }
                Row {
                    id: mainSel
                    anchors.right: parent.right
                    spacing: Theme.px(6)
                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("换算成"); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted }
                    TpSelect {
                        objectName: "moneyMain"
                        width: Theme.px(84)
                        options: rates.currencies.map(c => ({ key: c, text: c }))
                        current: rates.main
                        onPicked: (k) => rates.main = k
                    }
                }
            }
            Text {
                objectName: "moneyMainTotal"
                visible: rates.main !== root.cur
                width: parent.width
                wrapMode: Text.Wrap
                text: "≈ " + Fmt.money(root.money.mainTotal, rates.main)
                font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: Theme.textSoft
            }
            Text {
                objectName: "moneySplit"
                visible: root.hasBoth || (root.money.bookTotal || 0) !== 0
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("预订 %1 · 另外记账 %2").arg(Fmt.money(root.money.bookTotal, root.cur)).arg(Fmt.money(root.money.expTotal, root.cur))
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
            }
            // 预算
            Rectangle {
                visible: (root.money.budget || 0) > 0
                width: parent.width; height: Theme.px(7); radius: height / 2
                color: Theme.panel2
                Rectangle {
                    objectName: "moneyBar"
                    width: parent.width * (root.money.pct || 0); height: parent.height; radius: parent.radius
                    color: root.money.over ? Theme.danger : Theme.accent
                }
            }
            Text {
                objectName: "moneyBudget"
                width: parent.width
                wrapMode: Text.Wrap
                text: (root.money.budget || 0) > 0
                      ? qsTr("预算 %1").arg(Fmt.money(root.money.budget, root.cur)) + " · "
                        + (root.money.over ? qsTr("超出 %1").arg(Fmt.money(-root.money.remain)) : qsTr("还剩 %1").arg(Fmt.money(root.money.remain)))
                      : qsTr("还没设预算")
                font.family: serifFamily; font.pixelSize: Theme.px(13); color: root.money.over ? Theme.danger : Theme.textSoft
            }
            Text {
                visible: (root.money.days || 0) > 0 && (root.money.total || 0) !== 0
                text: qsTr("%1 天，平均每天 %2").arg(root.money.days || 0).arg(Fmt.money(root.money.daily, root.cur))
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
            }
            Text {
                objectName: "moneyMissing"
                visible: root.missing.length > 0 || root.mainMissing.length > 0
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("缺少 %1 的汇率，这些消费没算进合计。").arg(root.missing.concat(root.mainMissing.filter(c => root.missing.indexOf(c) < 0)).join(Fmt.en ? ", " : "、"))
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
            }
            Text {
                objectName: "moneyRates"
                visible: rates.main !== root.cur || rates.date !== ""
                width: parent.width
                wrapMode: Text.Wrap
                text: rates.date !== "" ? qsTr("汇率日期 %1").arg(rates.date) + (rates.failed ? " · " + qsTr("更新失败，用的是上次的") : "")
                                        : rates.failed ? qsTr("还没拿到汇率，联网后重新打开计划试试") : qsTr("正在取汇率…")
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
            }
            TpButton {
                objectName: "moneyBudgetBtn"
                visible: !root.readOnly
                compact: true
                text: (root.money.budget || 0) > 0 ? qsTr("修改预算 / 币种") : qsTr("设预算 / 币种")
                onClicked: root.budgetRequested()
            }
        }
    }

    // ---- 6 类色条 ----
    Column {
        objectName: "moneyCats"
        visible: root.cats.length > 0
        width: parent.width
        spacing: Theme.px(6)
        Row {
            id: catBar
            width: parent.width
            spacing: 1
            Repeater {
                model: root.cats
                delegate: Rectangle {
                    required property var modelData
                    width: (catBar.width - (root.cats.length - 1)) * modelData.share
                    height: Theme.px(9)
                    color: Fmt.catColors[modelData.k]
                }
            }
        }
        Flow {
            width: parent.width
            spacing: Theme.px(12)
            Repeater {
                model: root.cats
                delegate: Row {
                    required property var modelData
                    spacing: Theme.px(5)
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: Theme.px(9); height: width; radius: width / 2; color: Fmt.catColors[parent.modelData.k] }
                    Text {
                        text: Fmt.catLabel(parent.modelData.k) + " " + Fmt.money(parent.modelData.v) + " · " + parent.modelData.pct + "%"
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.textSoft
                    }
                }
            }
        }
    }

    // ---- 一行快记（J3）----
    Column {
        objectName: "moneyQuick"
        visible: !root.readOnly
        width: parent.width
        spacing: Theme.px(4)
        Row {
            id: quickRow
            width: parent.width
            spacing: Theme.px(6)
            TpSelect {
                objectName: "quickCat"
                width: Theme.px(92)
                options: root.catOptions()
                current: root.qCat
                onPicked: (k) => { root.qCat = k }
            }
            TpField {
                id: quickTitle
                objectName: "quickTitle"
                width: quickRow.width - Theme.px(92 + 84 + 52) - quickCur.width - 4 * quickRow.spacing
                placeholder: qsTr("内容")
                value: root.qTitle
                onEdited: (t) => { root.qTitle = t }
                onAccepted: root.qAmount === "" ? quickAmount.focusAll() : root.submitQuick()
            }
            TpField {
                id: quickAmount
                objectName: "quickAmount"
                width: Theme.px(84)
                placeholder: qsTr("金额")
                value: root.qAmount
                bad: root.qBad
                onEdited: (t) => { root.qAmount = t; root.qBad = false }
                onAccepted: root.submitQuick()
            }
            Text {
                id: quickCur
                anchors.verticalCenter: parent.verticalCenter
                text: root.cur
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
            }
            TpButton { objectName: "quickAdd"; compact: true; width: Theme.px(52); kind: "primary"; text: qsTr("记"); onClicked: root.submitQuick() }
        }
        Row {
            spacing: Theme.px(8)
            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Enter 记下，日期默认今天"); font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted }
            LinkText {
                objectName: "quickMore"
                small: true
                text: qsTr("展开更多")
                onClicked: {
                    const d = { title: root.qTitle, amount: root.qAmount, cat: root.qCat }
                    root.qTitle = ""; root.qAmount = ""
                    quickTitle.clear(); quickAmount.clear()
                    root.more(d)
                }
            }
        }
    }

    Text {
        visible: root.rows.length === 0
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("还没有记账。旅途中随手记，回来就知道钱花在哪了。")
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
    }

    // ---- 每笔一行 ----
    Column {
        visible: root.rows.length > 0
        width: parent.width
        spacing: Theme.px(6)
        Repeater {
            model: root.rows
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool refund: modelData.amount < 0
                objectName: modelData.fromBooking ? "moneyRowBooking" : "moneyRow"
                width: root.width
                height: rowCol.implicitHeight + Theme.px(18)
                radius: Theme.px(12)
                color: rowMa.containsMouse ? "#f7fafc" : "#ffffff"
                border.width: 1; border.color: Theme.line
                MouseArea { id: rowMa; anchors.fill: parent; enabled: !root.readOnly; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.edit(row.modelData) }
                Rectangle { x: Theme.px(10); anchors.verticalCenter: parent.verticalCenter; width: Theme.px(4); height: parent.height - Theme.px(20); radius: width / 2; color: Fmt.catColors[row.modelData.cat] || Theme.line }
                Column {
                    id: rowCol
                    x: Theme.px(22); y: Theme.px(9)
                    width: parent.width - x - amount.width - Theme.px(root.readOnly || row.modelData.fromBooking ? 18 : 48)
                    spacing: Theme.px(2)
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: row.modelData.title || (row.modelData.fromBooking ? Fmt.kindLabel(row.modelData.kind) : Fmt.catLabel(row.modelData.cat))
                        font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.text
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: [row.modelData.date, Fmt.catLabel(row.modelData.cat), row.modelData.notes,
                               row.modelData.fromBooking ? qsTr("来自交通住宿，在那边改") : row.modelData.linked ? qsTr("来自交通住宿") : ""].filter(x => x).join(" · ")
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                }
                Text {
                    id: amount
                    objectName: "moneyAmount"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.px(root.readOnly || row.modelData.fromBooking ? 12 : 42)
                    anchors.verticalCenter: parent.verticalCenter
                    text: Fmt.money(row.modelData.amount, row.modelData.foreign ? row.modelData.cur : "") + (row.refund ? " " + qsTr("退款") : "")
                    font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: row.refund ? "#2b8a3e" : Theme.text
                }
                Item {
                    objectName: "moneyDel"
                    visible: !root.readOnly && !row.modelData.fromBooking
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.px(10)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(26); height: width
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("删除")
                    Rectangle { anchors.fill: parent; radius: width / 2; color: delMa.containsMouse ? Theme.panel2 : "transparent" }
                    TpIcon { anchors.centerIn: parent; width: Theme.px(15); height: width; name: "trash"; color: Theme.muted }
                    MouseArea { id: delMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.remove(row.modelData) }
                }
            }
        }
    }
}
