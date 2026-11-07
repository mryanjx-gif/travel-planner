// 账本（0.10.2，B-01；PO 选 4A：上面总览，下面一张表；3A：页头选主货币）。数字都来自 plans::ledgerInfo（planPage.ledger）。
// 点一行跳到那个计划的消费标签。
import QtQuick
import TPApp

Item {
    id: root
    property bool shown: false
    // 0.12.9 按列排序：点表头，再点一下反过来。记在设置里
    property string sortBy: { const v = app.listPref("ledgerSort").split(":")[0]; return v || "spent" }
    property bool sortAsc: app.listPref("ledgerSort").split(":")[1] === "asc"
    function setSort(k, asc) { sortBy = k; sortAsc = asc; app.setListPref("ledgerSort", k + ":" + (asc ? "asc" : "desc")) }
    function clickHead(k) {
        if (sortBy === k) setSort(k, !sortAsc)
        else setSort(k, k === "name")   // 名字默认 A→Z，数字默认大的在前
    }
    signal openPlan(string id)
    signal openLibrary()
    readonly property var info: planPage.ledger
    readonly property var trips: info.trips || []
    readonly property var sorted: {
        const key = sortBy === "name" ? t => t.name || ""
                  : sortBy === "date" ? t => t.start || t.end || ""
                  : sortBy === "days" ? t => t.days || 0
                  : sortBy === "budget" ? t => t.budget > 0 ? (t.budgetMain >= 0 ? t.budgetMain : t.budget) : -1
                  : t => t.spent || 0
        const dir = sortAsc ? 1 : -1
        return trips.slice().sort((a, b) => {
            const x = key(a), y = key(b)
            return dir * (typeof x === "string" ? x.localeCompare(y) : x - y)
        })
    }
    readonly property var missing: info.missing || []
    readonly property var years: info.years || []
    readonly property bool narrow: width < Theme.px(620)
    readonly property real colDays: narrow ? 0 : Theme.px(64)
    readonly property real colBudget: narrow ? 0 : Theme.px(120)
    readonly property real colSpent: Theme.px(120)

    visible: opacity > 0.01
    opacity: shown ? 1 : 0
    scale: shown || !Theme.motion ? 1 : 0.96
    transformOrigin: Item.TopLeft
    Behavior on scale { TpAnim { shown: root.shown; ms: 280 } }
    Behavior on opacity { enabled: Theme.motion; TpAnim { shown: root.shown } }
    onShownChanged: planPage.showLedger(shown)

    function whole(v) { return Fmt.money(Math.round(v || 0)) }   // 都是换算出来的钱，小数没有意义
    function statusText(s) { return s === "wish" ? qsTr("想去") : s === "completed" ? qsTr("已完成") : s === "live" ? qsTr("进行中") : qsTr("计划中") }
    function budgetText(t) { return t.budget > 0 ? (t.budgetMain >= 0 ? whole(t.budgetMain) : Fmt.money(t.budget, t.budgetCur)) : "—" }
    function subText(t) {
        const a = []
        if (t.start || t.end) a.push((t.start || t.end).slice(0, 4) + " · " + Fmt.range(t.start, t.end))
        if (root.narrow && t.days > 0) a.push(qsTr("%1 天").arg(t.days))
        if (root.narrow && t.budget > 0) a.push(qsTr("预算 %1").arg(budgetText(t)))
        a.push(statusText(t.state))   // 0.10.5：每行都写状态
        return a.join(" · ")
    }

    GlassPanel {
        anchors.fill: parent
        radius: Theme.radius
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.985 : 0.99)
        liquid: false
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }   // 不穿透到地图

    Flickable {
        id: flick
        objectName: "ledgerFlick"
        anchors.fill: parent
        contentHeight: col.implicitHeight + Theme.px(44)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Column {
            id: col
            y: Theme.px(22)
            width: Math.min(flick.width - Theme.px(32), Theme.px(860))
            x: (flick.width - width) / 2
            spacing: Theme.px(14)

            // ---- 总览 ----
            Item {
                width: parent.width
                height: Math.max(bigCol.implicitHeight, mainSel.height)
                Column {
                    id: bigCol
                    width: parent.width - mainSel.width - Theme.px(10)
                    spacing: Theme.px(1)
                    Text { text: qsTr("已花"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
                    Text {
                        objectName: "ledgerTotal"
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: rates.main + " " + root.whole(root.info.total)
                        font.family: serifFamily; font.pixelSize: Theme.px(30); font.bold: true; color: (root.info.spentCount || 0) > 0 ? Theme.text : Theme.muted
                    }
                    Text {
                        objectName: "ledgerNoSpent"
                        visible: (root.info.spentCount || 0) === 0 && root.trips.length > 0
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("还没有开始的旅程花钱。计划到了开始日期或标记为已完成后，它的花费才算进这里。")
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                    }
                    Text {
                        objectName: "ledgerFuture"
                        visible: (root.info.futureCount || 0) > 0
                        topPadding: Theme.px(4)
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("还没去的预计 %1 %2（%3 趟）").arg(rates.main).arg(root.whole(root.info.future)).arg(root.info.futureCount || 0)
                        font.family: serifFamily; font.pixelSize: Theme.px(15); color: Theme.textSoft
                    }
                }
                Row {
                    id: mainSel
                    anchors.right: parent.right
                    spacing: Theme.px(6)
                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("换算成"); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted }
                    TpSelect {
                        objectName: "ledgerMain"
                        width: Theme.px(84)
                        options: rates.currencies.map(c => ({ key: c, text: c }))
                        current: rates.main
                        onPicked: (k) => rates.main = k
                    }
                }
            }
            Text {
                objectName: "ledgerStats"
                visible: (root.info.spentCount || 0) > 0
                width: parent.width
                wrapMode: Text.Wrap
                text: [qsTr("%1 趟").arg(root.info.spentCount || 0), qsTr("平均每趟 %1").arg(root.whole(root.info.perTrip))]
                      .concat((root.info.perDay || 0) !== 0 ? [qsTr("平均每天 %1").arg(root.whole(root.info.perDay))] : []).join(" · ")
                font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.textSoft
            }
            Text {
                objectName: "ledgerMissing"
                visible: root.missing.length > 0
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("缺少 %1 的汇率，这些消费没算进合计。").arg(root.missing.join(Fmt.en ? ", " : "、"))
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
            }
            LinkText { objectName: "ledgerRatesRetry"; visible: root.missing.length > 0 && !rates.netOff; small: true; text: rates.busy ? qsTr("正在取汇率…") : qsTr("重新取汇率"); onClicked: if (!rates.busy) rates.refreshNow() }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: rates.date !== "" ? qsTr("汇率日期 %1").arg(rates.date) + (rates.failed ? " · " + qsTr("更新失败，用的是上次的") : "")
                                        : rates.netOff ? qsTr("汇率的联网已关闭，现在没有汇率") : rates.failed ? qsTr("还没拿到汇率，联网后重新打开计划试试") : qsTr("正在取汇率…")
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
            }
            CatBar { objectName: "ledgerCats"; width: parent.width; whole: true; cats: root.info.cats || [] }

            // ---- 按年份 ----
            Column {
                visible: root.years.length > 0
                width: parent.width
                spacing: Theme.px(4)
                Repeater {
                    model: root.years
                    delegate: Item {
                        id: yearRow
                        required property var modelData
                        objectName: "ledgerYear"
                        width: col.width
                        height: Theme.px(20)
                        Text {
                            id: yearLabel
                            width: Theme.px(64)
                            anchors.verticalCenter: parent.verticalCenter
                            text: yearRow.modelData.y !== "" ? yearRow.modelData.y : qsTr("没写日期")
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                        }
                        Rectangle {
                            x: yearLabel.width
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(2, (parent.width - x - yearValue.width - Theme.px(8)) * yearRow.modelData.share)
                            height: Theme.px(8)
                            radius: height / 2
                            color: "#7ea3c4"
                        }
                        Text {
                            id: yearValue
                            width: Theme.px(110)
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            horizontalAlignment: Text.AlignRight
                            text: root.whole(yearRow.modelData.v)
                            font.features: Theme.tnum
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                        }
                    }
                }
            }

            // ---- 每趟一行 ----
            EmptyState {
                objectName: "ledgerEmptyAll"
                visible: root.trips.length === 0
                width: parent.width
                art: "ledger"
                title: qsTr("还没有花费")
                detail: qsTr("在计划的“消费”或“交通住宿”里记下来，这里就会汇总。")
                action: qsTr("打开计划库")
                onActed: root.openLibrary()
            }
            Segmented {
                objectName: "ledgerSort"
                visible: root.trips.length > 1
                options: [{ key: "spent", text: qsTr("按花费") }, { key: "date", text: qsTr("按日期") }]
                current: root.sortBy
                onPicked: (k) => root.setSort(k, false)
            }
            Column {
                visible: root.trips.length > 0
                width: parent.width
                spacing: -1   // 相邻两行的边框叠成一条
                TripsHead { id: tripsHead; width: parent.width }
                Repeater {
                    model: root.sorted
                    delegate: Rectangle {
                        id: tripRow
                        required property var modelData
                        objectName: "ledgerRow"
                        width: col.width
                        height: nameCol.implicitHeight + Theme.px(16)
                        color: rowMa.containsMouse ? "#f2f5f8" : "#ffffff"
                        border.width: 1; border.color: Theme.line
                        Accessible.role: Accessible.Button
                        Accessible.name: nameText.text
                        MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openPlan(tripRow.modelData.id) }
                        Column {
                            id: nameCol
                            x: Theme.px(12); y: Theme.px(8)
                            width: parent.width - x - Theme.px(12) - root.colDays - root.colBudget - root.colSpent
                            spacing: Theme.px(1)
                            Text {
                                id: nameText
                                width: parent.width
                                elide: Text.ElideRight
                                text: tripRow.modelData.name || qsTr("未命名计划")
                                font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.text
                            }
                            Text {
                                visible: text !== ""
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: root.subText(tripRow.modelData)
                                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                            }
                        }
                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.px(12)
                            anchors.verticalCenter: parent.verticalCenter
                            Text { visible: !root.narrow; width: root.colDays; horizontalAlignment: Text.AlignRight; text: tripRow.modelData.days > 0 ? tripRow.modelData.days : "—"; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft }
                            Text { visible: !root.narrow; width: root.colBudget; horizontalAlignment: Text.AlignRight; text: root.budgetText(tripRow.modelData); font.features: Theme.tnum; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft }
                            Text {
                                objectName: "ledgerSpent"
                                width: root.colSpent
                                horizontalAlignment: Text.AlignRight
                                text: root.whole(tripRow.modelData.spent)
                                font.features: Theme.tnum
                                font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: tripRow.modelData.done
                                color: !tripRow.modelData.done ? Theme.textSoft : tripRow.modelData.budget > 0 && tripRow.modelData.budgetMain >= 0 && tripRow.modelData.spent > tripRow.modelData.budgetMain ? Theme.danger : Theme.text
                            }
                        }
                    }
                }
            }
            Text {
                objectName: "ledgerEmpty"
                visible: (root.info.emptyCount || 0) > 0 && root.trips.length > 0
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("还有 %1 趟没有花费").arg(root.info.emptyCount || 0)
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
            }
        }
    }
    // 0.12.8 长表固定表头：滚过表头以后，上面钉一份
    Rectangle {
        objectName: "ledgerPinnedHead"
        visible: flick.contentY > tripsHead.mapToItem(flick.contentItem, 0, 0).y && root.trips.length > 0
        x: col.x
        width: col.width
        height: pinHead.height
        color: "#fafcfd"
        TripsHead { id: pinHead; width: parent.width }
    }
    component TripsHead: Item {
        height: Theme.px(26)
        HeadCell { x: Theme.px(12); key: "name"; label: qsTr("旅程"); alignRight: false; anchors.verticalCenter: parent.verticalCenter }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(12)
            anchors.verticalCenter: parent.verticalCenter
            HeadCell { visible: !root.narrow; width: root.colDays; key: "days"; label: qsTr("天数") }
            HeadCell { visible: !root.narrow; width: root.colBudget; key: "budget"; label: qsTr("预算") }
            HeadCell { width: root.colSpent; key: "spent"; label: qsTr("花费") }
        }
    }
    component HeadCell: Item {
        id: hc
        property string key: ""
        property string label: ""
        property bool alignRight: true
        readonly property bool on: root.sortBy === key
        objectName: "ledgerHead-" + key
        width: hcText.implicitWidth
        height: Theme.px(24)
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: label
        Keys.onReturnPressed: root.clickHead(key)
        Keys.onSpacePressed: root.clickHead(key)
        Text {
            id: hcText
            anchors.right: hc.alignRight ? parent.right : undefined
            anchors.verticalCenter: parent.verticalCenter
            text: hc.label + (hc.on ? (root.sortAsc ? " ↑" : " ↓") : "")
            font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: hc.on || hc.activeFocus
            color: hc.on || hcMa.containsMouse ? Theme.text : Theme.muted
        }
        MouseArea { id: hcMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.clickHead(hc.key) }
    }
}
