// 日历整页（0.11.2，C-01–C-03、C-08–C-10；照 Electron pages-calendar.js）。数据在 C++（calendar.info，plans::calYear / calMonth / calSide）。
// 年视图（默认）、月视图、翻页、今天；点横条弹卡（CalPlanCard）；拖动：横条平移（站点日期跟着，交通住宿不跟，PO 第 9 题 A）、
// 没定日期的拖到某一天、拖到右边去掉日期，Esc 取消。右边常驻"还没定日期"（PO 第 8 题 A，按状态分组，可以收起并记住）。
// 改完下面一条提示（notice）：撤销一步、撞期（R2）、交通住宿没跟（"打开交通住宿 / 知道了"）。周视图、季节表在 0.11.3，这版没有入口。
import QtQuick
import TPApp

Item {
    id: root
    property Item backdrop: null
    property bool shown: false
    signal openPlan(string id, string tab)
    signal toast(string text, bool warn)

    readonly property bool narrow: width < Theme.px(760)
    readonly property bool isYear: calendar.view === "year"
    readonly property var yearInfo: calendar.info.year || ({})
    readonly property var monthInfo: calendar.info.month || ({})
    readonly property var side: calendar.info.side || ({})
    readonly property var planning: side.planning || []
    readonly property var wishes: side.wish || []
    readonly property var hiddenRows: side.hidden || []
    readonly property int undatedCount: planning.length + wishes.length
    readonly property real gutter: Theme.px(16)
    readonly property real sideW: Theme.px(250)
    // 窄窗口：右边盖在日历上，默认收起（这时点开不改记住的值）
    property bool narrowSide: false
    readonly property bool sideShown: narrow ? narrowSide : calendar.sideOpen
    function toggleSide() { if (narrow) narrowSide = !narrowSide; else calendar.sideOpen = !calendar.sideOpen }

    visible: opacity > 0.01
    opacity: shown ? 1 : 0
    Behavior on opacity { enabled: Theme.motion; NumberAnimation { duration: 180 } }
    onShownChanged: { calendar.active = shown; if (!shown) { card.open = false; picker.open = false; hideNotice(); cancelDrag() } }

    function periodLabel() {
        const y = calendar.year, m = calendar.month
        if (isYear) { const e = new Date(y, m + 10, 1); return y + "." + m + " – " + e.getFullYear() + "." + (e.getMonth() + 1) }
        return Fmt.en ? Fmt.monEn[m - 1] + " " + y : y + " 年 " + m + " 月"
    }

    // ---- 拖动（Electron bindDrag：超过 5 px 才算；没动就是点击）----
    property var drag: null                 // { id, name, color, undated, grab, x0, y0 }
    property bool dragging: false
    property bool overSide: false
    property string dropA: ""               // 松手后占的日子（高亮）
    property string dropB: ""
    property int dropOverlaps: 0
    property real ghostX: 0
    property real ghostY: 0
    property string flashId: ""
    function press(bar, undated, px, py) {
        if (drag) return
        drag = { id: bar.id, name: bar.name || qsTr("未命名计划"), color: bar.color, undated: undated, grab: undated ? "" : dayAt(px, py), x0: px, y0: py, src: bar }
        dragging = false
    }
    function move(px, py) {
        if (!drag) return
        if (!dragging) {
            if (Math.hypot(px - drag.x0, py - drag.y0) < 5) return
            dragging = true
            tip.visible = false
            hideNotice()
            if (root.narrow && drag.undated) narrowSide = false   // 窄窗口：右边盖着日历，先收起来
        }
        ghostX = px + Theme.px(14)
        ghostY = py + Theme.px(12)
        overSide = !drag.undated && sideShown && inItem(sidePanel, px, py)
        const d = overSide ? "" : dayAt(px, py)
        setDrop(d)
    }
    function setDrop(day) {
        if (day === "") { dropA = ""; dropB = ""; dropOverlaps = 0; return }
        const r = calendar.dropRange(drag.id, day, drag.grab)
        dropA = r.start || ""; dropB = r.end || ""; dropOverlaps = r.overlaps || 0
    }
    function release(px, py) {
        const d = drag
        if (!d) return
        const wasDrag = dragging, side = overSide
        const day = wasDrag && !side ? dayAt(px, py) : ""
        cancelDrag()
        if (!wasDrag) { openCard(d.src, px, py); return }
        if (side) { showRemoved(calendar.removeDates(d.id)); return }
        if (day === "") return   // 拖到日历外面松手 = 取消
        const r = calendar.drop(d.id, day, d.grab)
        if (r.ok) showNotice(r)
    }
    function cancelDrag() { drag = null; dragging = false; overSide = false; dropA = ""; dropB = ""; dropOverlaps = 0 }
    function inItem(it, px, py) { const p = it.mapFromItem(root, px, py); return p.x >= 0 && p.y >= 0 && p.x < it.width && p.y < it.height }
    // 屏幕（这一页）上 (px, py) 是哪一天；不在日历上给空
    function dayAt(px, py) {
        if (!inItem(mainCard, px, py)) return ""
        if (isYear) {
            const p = yearBody.mapFromItem(root, px, py)
            if (p.y < -Theme.px(40) || yearInfo.start === undefined) return ""
            const f = Math.max(0, Math.min(0.99999, p.x / yearBody.width))
            return Fmt.addDays(yearInfo.start, Math.floor(f * yearInfo.spanDays))
        }
        for (let i = 0; i < weekRep.count; ++i) {
            const it = weekRep.itemAt(i)
            const p = it.mapFromItem(root, px, py)
            if (p.y >= 0 && p.y < it.height) {
                const col = Math.max(0, Math.min(6, Math.floor(p.x / it.width * 7)))
                return Fmt.addDays(it.modelData.start, col)
            }
        }
        return ""
    }

    // ---- 弹出卡 ----
    function openCard(bar, px, py) {
        picker.open = false
        const a = pressed.src
        if (a) { const p = a.mapToItem(root, 0, 0); card.show(bar.id, p.x, p.y, a.width, a.height) }
        else card.show(bar.id, px, py, 0, 0)
    }
    QtObject { id: pressed; property Item src: null }

    // ---- 改完的提示 ----
    property var note: null                 // { id, line, warn, bookings }
    function showNotice(r) {
        const when = r.start === r.end ? r.start : Fmt.rangeY(r.start, r.end)
        const name = r.name || qsTr("未命名计划")
        const line = (r.placed ? qsTr("「%1」定在 %2").arg(name).arg(when) : qsTr("「%1」挪到了 %2").arg(name).arg(when))
                     + (r.toPlanning ? qsTr("，状态改成了「计划中」") : "")
        note = { id: r.id, line: line, bookings: r.bookings || 0, overlaps: r.overlaps || 0, undo: true }
        // 有要处理的（撞期、交通住宿）一直留着，等人点；否则过一会儿自己收
        if (note.bookings === 0 && note.overlaps === 0) noteTimer.restart(); else noteTimer.stop()
    }
    function showRemoved(r) {
        if (!r.ok) return
        note = { id: r.id, line: qsTr("「%1」去掉了日期，放回「还没定日期」（预计 %2 天）").arg(r.name || qsTr("未命名计划")).arg(r.days), bookings: 0, overlaps: 0, undo: true }
        noteTimer.restart()
    }
    function hideNotice() { note = null; noteTimer.stop() }
    Timer { id: noteTimer; interval: 7000; onTriggered: root.hideNotice() }

    // 计划页"在日历里看"，这个计划没定日期：打开右边、闪一下（隐藏了的在隐藏列表里闪）
    function focusUndated(id) {
        calendar.sideOpen = true
        narrowSide = true
        hiddenOpen = calendar.isHidden(id) || hiddenOpen
        flashId = id
        flashTimer.restart()
    }
    Timer { id: flashTimer; interval: 1800; onTriggered: root.flashId = "" }
    property bool hiddenOpen: false
    // 给 Main.qml：Esc、截图
    readonly property bool popupOpen: card.open || picker.open
    function closePopups() { card.open = false; picker.open = false }
    function showCardAt(id, x, y) { card.show(id, x, y, Theme.px(200), Theme.px(32)) }
    function showPickerAt(x, y) { picker.show(x, y) }
    // 截图：拖到一半的样子
    function dragForShot(id, day, grab, px, py) {
        const f = calendar.planFields(id)
        drag = { id: id, name: f.name, color: f.color, undated: grab === "", grab: grab, x0: 0, y0: 0, src: null }
        dragging = true
        ghostX = px; ghostY = py
        setDrop(day)
    }

    GlassPanel {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: Theme.radius
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.985 : 0.99)
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }   // 不穿透到地图

    // ==================== 页头 ====================
    Item {
        id: head
        x: root.gutter; y: Theme.px(16)
        width: parent.width - 2 * root.gutter
        height: root.narrow ? Theme.px(76) : Theme.px(36)
        Row {
            height: Theme.px(36)
            spacing: Theme.px(2)
            Text { anchors.verticalCenter: parent.verticalCenter; rightPadding: Theme.px(10); text: qsTr("日历"); font.family: serifFamily; font.pixelSize: Theme.px(24); font.bold: true; color: Theme.text }
            NavBtn { objectName: "calPrev"; text: "‹"; onClicked: calendar.step(-1) }
            Rectangle {
                id: labelBtn
                objectName: "calLabel"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(labelText.implicitWidth + Theme.px(20), Theme.px(150))
                height: Theme.px(32)
                radius: Theme.px(8)
                color: labelMa.containsMouse ? Theme.panel2 : "transparent"
                Text { id: labelText; objectName: "calLabelText"; anchors.centerIn: parent; text: root.periodLabel() + " ▾"; font.family: serifFamily; font.pixelSize: Theme.px(16); font.bold: true; color: Theme.text }
                MouseArea {
                    id: labelMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { const p = labelBtn.mapToItem(root, labelBtn.width / 2, labelBtn.height); picker.show(p.x, p.y) }
                }
            }
            NavBtn { objectName: "calNext"; text: "›"; onClicked: calendar.step(1) }
            Item { width: Theme.px(6); height: 1 }
            TpButton { objectName: "calToday"; anchors.verticalCenter: parent.verticalCenter; compact: true; text: qsTr("今天"); onClicked: calendar.goToday() }
        }
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.px(36)
            spacing: Theme.px(8)
            TpChip {
                objectName: "calSideToggle"
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("还没定日期") + " " + root.undatedCount
                on: root.sideShown
                onClicked: root.toggleSide()
            }
            Segmented {
                objectName: "calViews"
                anchors.verticalCenter: parent.verticalCenter
                options: [{ key: "month", text: qsTr("月") }, { key: "year", text: qsTr("年") }]
                current: calendar.view
                onPicked: (k) => calendar.view = k
            }
        }
    }

    // ==================== 主体 ====================
    Rectangle {
        id: mainCard
        objectName: "calMain"
        x: root.gutter
        y: head.y + head.height + Theme.px(10)
        width: parent.width - 2 * root.gutter - (root.sideShown && !root.narrow ? root.sideW + Theme.px(12) : 0)
        height: parent.height - y - root.gutter
        radius: Theme.px(16)
        color: "#ffffff"
        border.width: 1; border.color: Theme.line
        clip: true

        // ---- 年 ----
        Item {
            visible: root.isYear
            anchors.fill: parent
            anchors.margins: Theme.px(12)
            Item {
                id: yearHead
                width: parent.width
                height: Theme.px(40)
                Repeater {
                    model: root.yearInfo.months || []
                    delegate: Column {
                        required property var modelData
                        required property int index
                        x: modelData.l * yearHead.width + Theme.px(4)
                        y: 0
                        Text {
                            text: parent.index === 0 || parent.modelData.m === 1 ? String(parent.modelData.y) : " "
                            font.family: serifFamily; font.pixelSize: Theme.px(11); color: Theme.muted
                        }
                        Text {
                            objectName: "calMonthHead-" + parent.index
                            text: Fmt.en ? Fmt.monEn[parent.modelData.m - 1] : parent.modelData.m + " 月"
                            font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: parent.modelData.cur
                            color: parent.modelData.cur ? Theme.accent : Theme.textSoft
                        }
                    }
                }
            }
            Flickable {
                id: yearFlick
                y: yearHead.height
                width: parent.width
                height: parent.height - y
                contentHeight: yearBody.height
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                interactive: !root.drag
                Item {
                    id: yearBody
                    objectName: "calYearBody"
                    width: yearFlick.width
                    height: Math.max(yearFlick.height, (root.yearInfo.lanes || 0) * Theme.px(40) + Theme.px(10))
                    Repeater {   // 月份竖线
                        model: root.yearInfo.months || []
                        delegate: Rectangle {
                            required property var modelData
                            x: modelData.l * yearBody.width
                            width: 1; height: yearBody.height
                            color: Theme.line
                        }
                    }
                    Rectangle {   // 拖动时要占的那几天
                        objectName: "calDropBand"
                        readonly property real a: root.dropA !== "" && root.yearInfo.start ? root.fracOf(root.dropA) : -1
                        readonly property real b: root.dropB !== "" && root.yearInfo.start ? root.fracOf(Fmt.addDays(root.dropB, 1)) : -1
                        visible: a >= 0 || b >= 0
                        x: Math.max(0, a) * yearBody.width
                        width: Math.max(Theme.px(3), (Math.min(1, b) - Math.max(0, a)) * yearBody.width)
                        height: yearBody.height
                        color: Theme.accentSoft
                    }
                    Rectangle {
                        objectName: "calTodayLine"
                        visible: (root.yearInfo.today === undefined ? -1 : root.yearInfo.today) >= 0
                        x: (root.yearInfo.today || 0) * yearBody.width
                        width: 2; height: yearBody.height
                        color: Theme.danger
                        opacity: 0.7
                    }
                    Repeater {
                        model: root.yearInfo.bars || []
                        delegate: Item {
                            id: yb
                            required property var modelData
                            required property int index
                            readonly property real barX: modelData.l * yearBody.width
                            readonly property real barW: Math.max(0.006 * yearBody.width, (modelData.r - modelData.l) * yearBody.width - 2)
                            readonly property real rowY: modelData.lane * Theme.px(40) + Theme.px(4)
                            readonly property string title: modelData.name || qsTr("未命名计划")
                            Rectangle {
                                id: barRect
                                objectName: "calBar-" + yb.modelData.id
                                x: yb.barX; y: yb.rowY
                                width: yb.barW; height: Theme.px(32)
                                radius: Theme.px(7)
                                color: yb.modelData.color
                                opacity: (root.drag && root.drag.id === yb.modelData.id && root.dragging ? 0.35 : yb.modelData.done ? 0.62 : 1)
                                Text {
                                    visible: !yb.modelData.narrow
                                    x: Theme.px(8)
                                    width: parent.width - Theme.px(14)
                                    anchors.verticalCenter: parent.verticalCenter
                                    elide: Text.ElideRight
                                    text: yb.title
                                    font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: "#ffffff"
                                }
                                BarArea { anchors.fill: parent; anchors.topMargin: -2; anchors.bottomMargin: -2; bar: yb.modelData; srcItem: barRect }
                            }
                            Text {   // 放不下名字：写旁边（靠右时写左边）
                                id: sideLabel
                                visible: yb.modelData.narrow
                                x: yb.modelData.labelLeft ? yb.barX - width - Theme.px(6) : yb.barX + yb.barW + Theme.px(4)
                                y: yb.rowY
                                width: Math.min(implicitWidth, 0.165 * yearBody.width)
                                height: Theme.px(32)
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: yb.modelData.labelLeft ? Text.AlignRight : Text.AlignLeft
                                elide: yb.modelData.labelLeft ? Text.ElideLeft : Text.ElideRight
                                text: yb.title
                                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: yb.modelData.done ? Theme.muted : Theme.textSoft
                                BarArea { anchors.fill: parent; bar: yb.modelData; srcItem: sideLabel }
                            }
                        }
                    }
                    Text {
                        objectName: "calYearEmpty"
                        visible: (root.yearInfo.bars || []).length === 0
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Theme.px(32), Theme.px(420))
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("这 12 个月里没有定了日期的计划。可以把右边的计划拖进来。")
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
                    }
                }
            }
        }

        // ---- 月 ----
        Column {
            visible: !root.isYear
            anchors.fill: parent
            anchors.margins: Theme.px(12)
            Row {
                id: wdHead
                width: parent.width
                height: Theme.px(26)
                Repeater {
                    model: Fmt.en ? ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"] : ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]
                    delegate: Text {
                        required property string modelData
                        width: wdHead.width / 7
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                    }
                }
            }
            Flickable {
                id: monthFlick
                width: parent.width
                height: parent.height - wdHead.height
                contentHeight: weekCol.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                interactive: !root.drag
                Column {
                    id: weekCol
                    readonly property real freeH: monthFlick.height / Math.max(1, (root.monthInfo.weeks || []).length)
                    width: monthFlick.width
                    Repeater {
                        id: weekRep
                        model: root.monthInfo.weeks || []
                        delegate: Item {
                            id: week
                            required property var modelData
                            required property int index
                            objectName: "calWeek-" + index
                            width: weekCol.width
                            height: Math.max(weekCol.freeH, Theme.px(28) + modelData.lanes * Theme.px(26) + Theme.px(4))
                            Row {
                                anchors.fill: parent
                                Repeater {
                                    model: week.modelData.days
                                    delegate: Rectangle {
                                        required property var modelData
                                        required property int index
                                        readonly property bool hit: root.dropA !== "" && modelData.date >= root.dropA && modelData.date <= root.dropB
                                        objectName: "calDay-" + modelData.date
                                        width: week.width / 7
                                        height: week.height
                                        color: hit ? Theme.accentSoft : modelData.out ? Theme.panel : "transparent"
                                        border.width: 1; border.color: Theme.line
                                        Rectangle {
                                            visible: parent.modelData.today
                                            x: Theme.px(4); y: Theme.px(3)
                                            width: Theme.px(22); height: Theme.px(20)
                                            radius: Theme.px(6)
                                            color: Theme.accent
                                        }
                                        Text {
                                            x: Theme.px(4); y: Theme.px(3)
                                            width: Theme.px(22); height: Theme.px(20)
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            text: parent.modelData.d
                                            font.family: serifFamily; font.pixelSize: Theme.px(12.5)
                                            color: parent.modelData.today ? "#ffffff" : parent.modelData.out ? Theme.faint : Theme.textSoft
                                        }
                                    }
                                }
                            }
                            Repeater {
                                model: week.modelData.bars
                                delegate: Rectangle {
                                    id: mb
                                    required property var modelData
                                    objectName: "calBar-" + modelData.id + "-" + week.index
                                    x: modelData.a / 7 * week.width + Theme.px(3)
                                    y: Theme.px(26) + modelData.lane * Theme.px(26)
                                    width: (modelData.b - modelData.a + 1) / 7 * week.width - Theme.px(6)
                                    height: Theme.px(22)
                                    radius: Theme.px(6)
                                    color: modelData.color
                                    opacity: (root.drag && root.drag.id === modelData.id && root.dragging ? 0.35 : modelData.done ? 0.62 : 1)
                                    Text {
                                        x: Theme.px(7)
                                        width: parent.width - Theme.px(12)
                                        anchors.verticalCenter: parent.verticalCenter
                                        elide: Text.ElideRight
                                        text: mb.modelData.name || qsTr("未命名计划")
                                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); font.bold: true; color: "#ffffff"
                                    }
                                    BarArea { anchors.fill: parent; bar: mb.modelData; srcItem: mb }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    function fracOf(day) {
        // 从开始算的第几天 / 总天数；可以超出 0–1（画的时候夹住）
        const a = Fmt.dt(yearInfo.start), b = Fmt.dt(day)
        return Math.round((Date.UTC(b.getFullYear(), b.getMonth(), b.getDate()) - Date.UTC(a.getFullYear(), a.getMonth(), a.getDate())) / 86400000) / yearInfo.spanDays
    }

    // ==================== 右边：还没定日期、隐藏的 ====================
    Rectangle {
        id: sidePanel
        objectName: "calSide"
        visible: root.sideShown
        z: 2
        x: parent.width - width - root.gutter
        y: mainCard.y
        width: root.sideW
        height: mainCard.height
        radius: Theme.px(16)
        color: root.overSide ? Theme.accentSoft : "#ffffff"
        border.width: root.overSide ? 2 : 1
        border.color: root.overSide ? Theme.accent : Theme.line
        Flickable {
            id: sideFlick
            objectName: "calSideFlick"
            anchors.fill: parent
            anchors.margins: Theme.px(12)
            contentHeight: sideCol.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            interactive: !root.drag
            Column {
                id: sideCol
                width: sideFlick.width
                spacing: Theme.px(6)
                Text { objectName: "calSideTitle"; text: qsTr("还没定日期") + " " + root.undatedCount; font.family: serifFamily; font.pixelSize: Theme.px(15); font.bold: true; color: Theme.text }
                Text {
                    visible: root.undatedCount === 0
                    text: qsTr("全部都定了日期。")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                }
                SideGroup { title: qsTr("计划中"); rows: root.planning }
                SideGroup { title: qsTr("想去"); rows: root.wishes }
                Text {
                    objectName: "calSideHint"
                    width: parent.width
                    wrapMode: Text.Wrap
                    topPadding: Theme.px(4)
                    text: qsTr("把这里的计划拖到某一天；把日历上的计划拖到这里，可以去掉日期")
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: root.overSide ? Theme.accentInk : Theme.muted
                }
                // ---- 隐藏的 ----
                Column {
                    visible: root.hiddenRows.length > 0
                    width: parent.width
                    topPadding: Theme.px(8)
                    spacing: Theme.px(4)
                    Rectangle { width: parent.width; height: 1; color: Theme.line }
                    Text {
                        objectName: "calHiddenToggle"
                        topPadding: Theme.px(4)
                        text: qsTr("在日历里隐藏的") + " " + root.hiddenRows.length + (root.hiddenOpen ? " ▴" : " ▾")
                        font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.textSoft
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.hiddenOpen = !root.hiddenOpen }
                    }
                    Repeater {
                        model: root.hiddenOpen ? root.hiddenRows : []
                        delegate: Rectangle {
                            required property var modelData
                            objectName: "calHiddenRow-" + modelData.id
                            width: parent.width
                            height: Theme.px(28)
                            radius: Theme.px(6)
                            color: root.flashId === modelData.id ? Theme.accentSoft : "transparent"
                            Rectangle { x: Theme.px(2); anchors.verticalCenter: parent.verticalCenter; width: Theme.px(10); height: width; radius: width / 2; color: parent.modelData.color }
                            Text {
                                x: Theme.px(18)
                                width: parent.width - x - showLink.width - Theme.px(6)
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                text: parent.modelData.name || qsTr("未命名计划")
                                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
                            }
                            LinkText { id: showLink; objectName: "calUnhide-" + parent.modelData.id; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; small: true; text: qsTr("显示"); onClicked: calendar.setHidden(parent.modelData.id, false) }
                        }
                    }
                }
            }
        }
    }

    // ---- 改完的提示 ----
    Rectangle {
        id: noteBar
        objectName: "calNotice"
        visible: root.note !== null
        anchors.horizontalCenter: mainCard.horizontalCenter
        anchors.bottom: mainCard.bottom
        anchors.bottomMargin: Theme.px(14)
        width: Math.min(mainCard.width - Theme.px(24), Theme.px(560))
        height: noteCol.implicitHeight + Theme.px(22)
        radius: Theme.px(14)
        color: "#f4162a36"
        z: 5
        MouseArea { anchors.fill: parent }
        Column {
            id: noteCol
            x: Theme.px(16); y: Theme.px(11)
            width: parent.width - Theme.px(32)
            spacing: Theme.px(5)
            Text { objectName: "calNoticeLine"; width: parent.width; wrapMode: Text.Wrap; text: root.note ? root.note.line : ""; font.family: serifFamily; font.pixelSize: Theme.px(14); color: "#ffffff" }
            Text {
                objectName: "calNoticeOverlap"
                visible: !!root.note && root.note.overlaps > 0
                width: parent.width
                wrapMode: Text.Wrap
                text: root.note ? Fmt.one(qsTr("和 %1 个计划日期重叠").arg(root.note.overlaps)) : ""
                font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: "#f6c9a8"
            }
            Text {
                objectName: "calNoticeBookings"
                visible: !!root.note && root.note.bookings > 0
                width: parent.width
                wrapMode: Text.Wrap
                text: root.note ? Fmt.one(qsTr("日期改了，%1 条交通住宿还是原来的日期，要去看看吗？").arg(root.note.bookings)) : ""
                font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: "#f6c9a8"
            }
            Row {
                topPadding: Theme.px(3)
                spacing: Theme.px(16)
                NoteLink { objectName: "calNoticeUndo"; text: qsTr("撤销"); onClicked: { root.hideNotice(); app.undo() } }
                NoteLink {
                    objectName: "calNoticeBookingsBtn"
                    visible: !!root.note && root.note.bookings > 0
                    text: qsTr("打开交通住宿")
                    onClicked: { const id = root.note.id; root.hideNotice(); root.openPlan(id, "bookings") }
                }
                NoteLink { objectName: "calNoticeOk"; text: qsTr("知道了"); onClicked: root.hideNotice() }
            }
        }
    }

    // ---- 拖动的影子 ----
    Rectangle {
        objectName: "calGhost"
        visible: root.dragging
        x: Math.min(root.ghostX, root.width - width - Theme.px(6))
        y: Math.min(root.ghostY, root.height - height - Theme.px(6))
        width: Math.min(Math.max(ghostName.implicitWidth, ghostWhen.implicitWidth, ghostWarn.implicitWidth) + Theme.px(36), Theme.px(320))
        height: ghostCol.implicitHeight + Theme.px(14)
        radius: Theme.px(10)
        color: "#fbfcfd"
        border.width: 1; border.color: Theme.lineStrong
        z: 20
        Rectangle { x: Theme.px(10); y: Theme.px(13); width: Theme.px(10); height: width; radius: width / 2; color: root.drag ? root.drag.color : "transparent" }
        Column {
            id: ghostCol
            x: Theme.px(26); y: Theme.px(7)
            width: parent.width - Theme.px(32)
            Text { id: ghostName; width: parent.width; elide: Text.ElideRight; text: root.drag ? root.drag.name : ""; font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.text }
            Text {
                id: ghostWhen
                objectName: "calGhostWhen"
                text: root.overSide ? qsTr("松手：去掉日期，放回“还没定日期”")
                    : root.dropA === "" ? qsTr("拖到日历上的某一天")
                    : root.dropA === root.dropB ? qsTr("%1（1 天）").arg(root.dropA) : Fmt.rangeY(root.dropA, root.dropB)
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: root.dropA === "" && !root.overSide ? Theme.muted : Theme.textSoft
            }
            Text {
                id: ghostWarn
                objectName: "calGhostWarn"
                visible: root.dropOverlaps > 0 && !root.overSide
                text: Fmt.one(qsTr("和 %1 个计划日期重叠").arg(root.dropOverlaps))
                font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.warn
            }
        }
    }

    // ---- 悬停看全名和日期 ----
    Rectangle {
        id: tip
        objectName: "calTip"
        visible: false
        z: 15
        width: Math.min(tipText.implicitWidth + Theme.px(16), Theme.px(340))
        height: tipText.implicitHeight + Theme.px(10)
        radius: Theme.px(6)
        color: "#26323c"
        property alias text: tipText.text
        Text { id: tipText; x: Theme.px(8); y: Theme.px(5); width: Math.min(implicitWidth, Theme.px(324)); wrapMode: Text.Wrap; font.family: serifFamily; font.pixelSize: Theme.px(12); color: "#ffffff" }
    }

    CalPlanCard {
        id: card
        objectName: "calCardLayer"
        z: 30
        onSaved: (r) => { if (r.moved) root.showNotice(r) }
        onHideRequested: (id, name) => { calendar.setHidden(id, true); root.toast(qsTr("「%1」在日历里隐藏了，右边“在日历里隐藏的”里可以恢复").arg(name), false) }
        onRemoveDatesRequested: (id) => root.showRemoved(calendar.removeDates(id))
        onOpenPlanRequested: (id) => root.openPlan(id, "")
        onWarn: (t) => root.toast(t, true)
    }
    CalPicker {
        id: picker
        objectName: "calPickerLayer"
        z: 30
        onPicked: (y, m) => calendar.goTo(y, m)
        onTodayClicked: calendar.goToday()
    }

    // 横条和列表行共用：按下、拖动、松手（没动 = 点开弹出卡）
    component BarArea: MouseArea {
        property var bar: ({})
        property Item srcItem: null
        property bool undated: false
        hoverEnabled: true
        preventStealing: true
        cursorShape: root.dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor
        onPressed: (m) => { const p = mapToItem(root, m.x, m.y); pressed.src = srcItem; root.press(bar, undated, p.x, p.y) }
        onPositionChanged: (m) => { if (containsPress || root.drag) { const p = mapToItem(root, m.x, m.y); root.move(p.x, p.y) } }
        onReleased: (m) => { const p = mapToItem(root, m.x, m.y); root.release(p.x, p.y) }
        onCanceled: root.cancelDrag()
        onContainsMouseChanged: {
            if (containsMouse && !root.drag && bar.start) {
                const p = mapToItem(root, 0, height + Theme.px(4))
                tip.text = (bar.name || qsTr("未命名计划")) + " · " + Fmt.rangeY(bar.start, bar.end)
                tip.x = Math.max(Theme.px(6), Math.min(root.width - tip.width - Theme.px(6), p.x))
                tip.y = Math.min(root.height - tip.height - Theme.px(6), p.y)
                tip.visible = true
            } else tip.visible = false
        }
    }
    component SideGroup: Column {
        id: grp
        property string title: ""
        property var rows: []
        visible: rows.length > 0
        width: parent.width
        spacing: Theme.px(3)
        Text { topPadding: Theme.px(4); text: grp.title + " " + grp.rows.length; font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.muted }
        Repeater {
            model: grp.rows
            delegate: Rectangle {
                id: row
                required property var modelData
                objectName: "calUndated-" + modelData.id
                width: grp.width
                height: Theme.px(32)
                radius: Theme.px(8)
                color: root.flashId === modelData.id ? Theme.accentSoft : rowArea.containsMouse ? Theme.panel2 : Theme.panelLight
                border.width: 1; border.color: root.flashId === modelData.id ? Theme.accent : Theme.line
                opacity: root.drag && root.drag.id === modelData.id && root.dragging ? 0.4 : 1
                Behavior on color { enabled: Theme.motion; ColorAnimation { duration: 300 } }
                TpIcon { x: Theme.px(4); anchors.verticalCenter: parent.verticalCenter; width: Theme.px(14); height: Theme.px(14); name: "grip"; color: Theme.faint }
                Rectangle { x: Theme.px(20); anchors.verticalCenter: parent.verticalCenter; width: Theme.px(10); height: width; radius: width / 2; color: row.modelData.color }
                Text {
                    x: Theme.px(36)
                    width: parent.width - x - Theme.px(8)
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: row.modelData.name || qsTr("未命名计划")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text
                }
                BarArea { id: rowArea; anchors.fill: parent; bar: row.modelData; srcItem: row; undated: true }
            }
        }
    }
    component NavBtn: Rectangle {
        id: nb
        property string text: ""
        signal clicked()
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.px(32); height: width; radius: Theme.px(8)
        color: nbMa.containsMouse ? Theme.panel2 : "transparent"
        Accessible.role: Accessible.Button
        Accessible.name: text === "‹" ? qsTr("往前") : qsTr("往后")
        Text { anchors.centerIn: parent; text: nb.text; font.family: serifFamily; font.pixelSize: Theme.px(22); color: Theme.textSoft }
        MouseArea { id: nbMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: nb.clicked() }
    }
    component NoteLink: Text {
        id: nl
        signal clicked()
        font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; font.underline: nlMa.containsMouse
        color: "#bcd6ee"
        Accessible.role: Accessible.Button
        Accessible.name: text
        MouseArea { id: nlMa; anchors.fill: parent; anchors.margins: -Theme.px(4); hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: nl.clicked() }
    }
}
