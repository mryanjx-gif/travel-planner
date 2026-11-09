// 日历整页（0.11.2，C-01–C-03、C-08–C-10；照 Electron pages-calendar.js）。数据在 C++（calendar.info，plans::calYear / calMonth / calSide）。
// 周（0.11.3，C-04）、月、年（默认）、季节（0.11.3，CalSeasonView）四个视图；翻页、今天；点横条弹卡（CalPlanCard）；拖动：横条平移（站点日期跟着，交通住宿不跟，PO 第 9 题 A）、
// 没定日期的拖到某一天、拖到右边去掉日期，Esc 取消。右边常驻"还没定日期"（PO 第 8 题 A，按状态分组，可以收起并记住）。
// 改完下面一条提示（notice）：撤销一步、撞期（R2）、交通住宿没跟（"打开交通住宿 / 知道了"）。
import QtQuick
import TPApp

Item {
    id: root
    property bool shown: false
    signal openPlan(string id, string tab)
    signal toast(string text, bool warn)
    signal newPlan()

    readonly property bool narrow: width < Theme.px(760)
    readonly property bool isYear: calendar.view === "year"
    readonly property bool isMonth: calendar.view === "month"
    readonly property bool isWeek: calendar.view === "week"
    readonly property bool isSeason: calendar.view === "season"
    readonly property var weekInfo: calendar.info.week || ({})
    readonly property var yearInfo: calendar.info.year || ({})
    readonly property var monthInfo: calendar.info.month || ({})
    readonly property var side: calendar.info.side || ({})
    readonly property var planning: side.planning || []
    readonly property var wishes: side.wish || []
    readonly property var hiddenRows: side.hidden || []
    readonly property int undatedCount: planning.length + wishes.length
    readonly property bool noDated: (plans.list.total || 0) === undatedCount   // 没有一个计划定了日期（已完成的不会在"还没定日期"里）
    readonly property real gutter: Theme.px(16)
    readonly property real sideW: Theme.px(250)
    // 窄窗口：右边盖在日历上，默认收起（这时点开不改记住的值）
    property bool narrowSide: false
    readonly property bool sideShown: !isSeason && (narrow ? narrowSide : calendar.sideOpen)
    function toggleSide() { if (narrow) narrowSide = !narrowSide; else calendar.sideOpen = !calendar.sideOpen }

    visible: opacity > 0.01
    opacity: shown ? 1 : 0
    scale: shown || !Theme.motion ? 1 : 0.96
    transformOrigin: Item.TopLeft
    Behavior on scale { TpAnim { shown: root.shown; ms: 280 } }
    Behavior on opacity { enabled: Theme.motion; TpAnim { shown: root.shown } }
    onShownChanged: { calendar.active = shown; if (!shown) { card.open = false; picker.open = false; hideNotice(); cancelDrag() } }

    function periodLabel() {
        const y = calendar.year, m = calendar.month
        if (isYear) { const e = new Date(y, m + 10, 1); return y + "." + m + " – " + e.getFullYear() + "." + (e.getMonth() + 1) }
        if (isWeek) {
            const a = Fmt.dt(calendar.weekFirst), b = Fmt.dt(Fmt.addDays(calendar.weekFirst, 6))
            return Fmt.en ? Fmt.monEn[a.getMonth()] + " " + a.getDate() + " – " + Fmt.monEn[b.getMonth()] + " " + b.getDate() + ", " + a.getFullYear()
                          : a.getFullYear() + " 年 " + (a.getMonth() + 1) + "/" + a.getDate() + " – " + (b.getMonth() + 1) + "/" + b.getDate()
        }
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
        if (isWeek) {   // 周视图：按列算，全天区和时间轴都一样
            if (weekInfo.start === undefined) return ""
            const q = weekCols.mapFromItem(root, px, py)
            return Fmt.addDays(weekInfo.start, Math.max(0, Math.min(6, Math.floor(q.x / weekCols.width * 7))))
        }
        if (!isMonth) return ""
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
        // 一段日期里不断行（NBSP），破折号两边可以
        const when = (r.start === r.end ? r.start : Fmt.rangeY(r.start, r.end)).split(" — ").map(s => s.replace(/ /g, "\u00a0")).join(" — ")
        const name = r.name || qsTr("未命名计划")
        const line = (r.placed ? qsTr("「%1」定在 %2").arg(name).arg(when) : qsTr("「%1」挪到了 %2").arg(name).arg(when))
                     + (r.toPlanning ? qsTr("，状态改成了「计划中」") : "")
        note = { id: r.id, line: line, bookings: r.bookings || 0, overlaps: r.overlaps || 0, undo: true }
        // 有要处理的（撞期、交通住宿）一直留着，等人点；否则过一会儿自己收
        if (note.bookings === 0 && note.overlaps === 0) noteTimer.restart(); else noteTimer.stop()
    }
    function showRemoved(r) {
        if (!r.ok) return
        note = { id: r.id, line: qsTr("「%1」已去掉日期（预计 %2 天）").arg(r.name || qsTr("未命名计划")).arg(r.days), bookings: 0, overlaps: 0, undo: true }
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
    // 截图：滚到这一周最晚的事件
    function scrollWeekForShot() {
        let a = 0
        for (const l of weekInfo.events || []) for (const e of l) a = Math.max(a, e.a)
        weekFlick.contentY = Math.min(weekFlick.contentHeight - weekFlick.height, Math.max(0, a / 60 * weekView.hourPx - Theme.px(120)))
    }
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
        radius: Theme.px(16)   // 1.3.1：和相册展开时一样，圆角液态玻璃浮在地图上
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.92 : 0.98)   // 1.3.1：92% 白（相册 84%）
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }   // 不穿透到地图

    // ==================== 页头 ====================
    BarHead {
        id: head
        leftW: headLeft.implicitWidth
        rightW: headRight.implicitWidth
        Row {
            id: headLeft
            height: Theme.px(36)
            spacing: Theme.px(2)
            Text {
                objectName: "calSeasonSub"
                visible: root.isSeason
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("什么时候去哪里合适")
                font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.muted
            }
            NavBtn { objectName: "calPrev"; visible: !root.isSeason; text: "‹"; onClicked: calendar.step(-1) }
            Rectangle {
                id: labelBtn
                objectName: "calLabel"
                visible: !root.isSeason
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
            NavBtn { objectName: "calNext"; visible: !root.isSeason; text: "›"; onClicked: calendar.step(1) }
            Item { width: Theme.px(6); height: 1 }
            TpButton { objectName: "calToday"; visible: !root.isSeason; anchors.verticalCenter: parent.verticalCenter; compact: true; text: qsTr("今天"); onClicked: calendar.goToday() }
        }
        Row {
            id: headRight
            x: head.twoRows ? 0 : parent.width - width   // 不用 anchors.right：去掉锚以后 x 不会回 0
            anchors.bottom: parent.bottom
            height: Theme.px(36)
            spacing: Theme.px(8)
            TpChip {
                objectName: "calSideToggle"
                visible: !root.isSeason
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("还没定日期") + " " + root.undatedCount
                on: root.sideShown
                onClicked: root.toggleSide()
            }
            Segmented {
                objectName: "calViews"
                anchors.verticalCenter: parent.verticalCenter
                options: [{ key: "week", text: qsTr("周") }, { key: "month", text: qsTr("月") }, { key: "year", text: qsTr("年") }, { key: "season", text: qsTr("季节") }]
                current: calendar.view
                onPicked: (k) => calendar.view = k
            }
        }
    }

    // 没有一个计划定了日期（0.12.7 空状态）：叠在主体上，不挡从右边拖计划进来；拖的时候让开。季节表不受影响
    // 0.12.11 底下的格子线先盖住（不挡鼠标），卡片不压在一片竖线上
    Rectangle {
        z: 5
        visible: root.noDated && !root.isSeason && !root.dragging
        anchors.fill: mainCard
        radius: mainCard.radius
        color: Qt.rgba(Theme.card.r, Theme.card.g, Theme.card.b, 0.9)
    }
    Rectangle {
        z: 5
        visible: root.noDated && !root.isSeason && !root.dragging
        anchors.centerIn: mainCard
        width: emptyCal.width + Theme.px(40)
        height: emptyCal.height + Theme.px(32)
        radius: Theme.radius
        color: Theme.panel
        EmptyState {
            id: emptyCal
            objectName: "emptyCalendar"
            anchors.centerIn: parent
            width: Math.min(mainCard.width - Theme.px(64), Theme.px(360))
            art: "calendar"
            title: qsTr("日历上还没有行程")
            detail: qsTr("定了日期的计划排在这里。")
            action: qsTr("新建计划")
            onActed: root.newPlan()
        }
    }

    // ==================== 主体 ====================
    TpCard {
        id: mainCard
        objectName: "calMain"
        x: root.gutter
        y: head.contentTop
        width: parent.width - 2 * root.gutter - (root.sideShown && !root.narrow ? root.sideW + Theme.px(12) : 0)
        height: parent.height - y - root.gutter
        radius: Theme.px(16)
        color: Theme.card
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
                                elide: Text.ElideRight
                                text: yb.title
                                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: yb.modelData.done ? Theme.muted : Theme.textSoft
                                BarArea { anchors.fill: parent; bar: yb.modelData; srcItem: sideLabel }
                            }
                        }
                    }
                    Text {
                        objectName: "calYearEmpty"
                        visible: (root.yearInfo.bars || []).length === 0 && !root.noDated
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Theme.px(32), Theme.px(420))
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("这 12 个月没有定了日期的计划。")
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
                    }
                }
            }
        }

        // ---- 周（Electron weekHtml：全天区 + 0–24 时间轴）----
        Item {
            id: weekView
            objectName: "calWeekView"
            visible: root.isWeek
            anchors.fill: parent
            anchors.margins: Theme.px(12)
            readonly property real gut: Theme.px(Fmt.en ? 50 : 44)
            readonly property real hourPx: Theme.px(44)
            readonly property var days: root.weekInfo.days || []
            readonly property real colW: weekCols.width / 7
            function hit(date) { return root.dropA !== "" && date >= root.dropA && date <= root.dropB }
            // 打开一周时滚到最早的事件或 7 点；同一周里数据变了不动
            property string scrolledFor: ""
            function scrollIfNew() {
                if (!root.isWeek || root.weekInfo.start === undefined || scrolledFor === root.weekInfo.start) return
                scrolledFor = root.weekInfo.start
                weekTop.contentY = 0
                Qt.callLater(() => { weekFlick.contentY = Math.max(0, Math.min(weekFlick.contentHeight - weekFlick.height, root.weekInfo.first / 60 * hourPx - hourPx / 2 - Theme.px(10))) })
            }
            Connections { target: root; function onWeekInfoChanged() { weekView.scrollIfNew() } }
            onVisibleChanged: { if (!visible) scrolledFor = ""; else scrollIfNew() }
            // 给拖动算落点、给自检找列：只占位，不画
            Item { id: weekCols; objectName: "calWeekCols"; x: weekView.gut; width: parent.width - weekView.gut; height: parent.height }
            Column {
                anchors.fill: parent
                Item {   // 表头
                    width: parent.width
                    height: Theme.px(44)
                    Repeater {
                        model: weekView.days
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            objectName: "calWeekHead-" + modelData.date
                            x: weekView.gut + index * weekView.colW
                            width: weekView.colW; height: parent.height
                            radius: Theme.px(8)
                            color: weekView.hit(modelData.date) ? Theme.accentSoft : modelData.today ? Theme.panel2 : "transparent"
                            Column {
                                anchors.centerIn: parent
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: Fmt.en ? Fmt.wdEn[parent.parent.modelData.wd] : "周" + Fmt.wdZh[parent.parent.modelData.wd]; font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: (Fmt.en ? Fmt.monEn[parent.parent.modelData.m - 1] + " " : parent.parent.modelData.m + "/") + parent.parent.modelData.d; font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: parent.parent.modelData.today ? Theme.accent : Theme.text }
                            }
                        }
                    }
                }
                Flickable {   // 全天区 + 站：最多占这一视图的 40%，多的自己滚，时间轴总留得够
                    id: weekTop
                    objectName: "calWeekTop"
                    width: parent.width
                    height: Math.min(contentHeight, weekView.height * 0.4)
                    contentWidth: width
                    contentHeight: allDay.height + stopRow.height
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    interactive: contentHeight > height && !root.drag
                    Item {   // 全天：计划和住宿的横条
                        id: allDay
                        width: weekTop.width
                        height: Math.max(1, root.weekInfo.lanes || 0) * Theme.px(26) + Theme.px(4)
                        Text { width: weekView.gut - Theme.px(6); y: Theme.px(5); horizontalAlignment: Text.AlignRight; text: qsTr("全天"); font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted }
                        Text {
                            objectName: "calWeekEmpty"
                            visible: root.weekInfo.empty === true
                            x: weekView.gut + Theme.px(8); y: Theme.px(5)
                            text: qsTr("这一周没有安排")
                            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                        }
                        Repeater {
                            model: root.weekInfo.bars || []
                            delegate: Rectangle {
                                id: wb
                                required property var modelData
                                readonly property bool hotel: modelData.kind === "hotel"
                                objectName: hotel ? "calWeekHotel-" + modelData.id + "-" + modelData.a : "calWeekBar-" + modelData.id
                                x: weekView.gut + modelData.a * weekView.colW + Theme.px(3)
                                y: Theme.px(2) + modelData.lane * Theme.px(26)
                                width: (modelData.b - modelData.a + 1) * weekView.colW - Theme.px(6)
                                height: Theme.px(22)
                                radius: Theme.px(6)
                                topLeftRadius: modelData.cutL ? 0 : radius; bottomLeftRadius: topLeftRadius
                                topRightRadius: modelData.cutR ? 0 : radius; bottomRightRadius: topRightRadius
                                color: hotel ? Qt.alpha(modelData.color, 0.14) : modelData.color
                                border.width: hotel ? 1 : 0; border.color: Qt.alpha(modelData.color, 0.5)
                                opacity: (root.drag && root.drag.id === modelData.id && root.dragging && !hotel ? 0.35 : modelData.done ? 0.62 : 1)
                                Text {
                                    x: Theme.px(7)
                                    width: parent.width - Theme.px(12)
                                    anchors.verticalCenter: parent.verticalCenter
                                    elide: Text.ElideRight
                                    text: wb.hotel ? (wb.modelData.name && weekView.colW < Theme.px(90) ? wb.modelData.name : qsTr("住宿") + (wb.modelData.name ? " · " + wb.modelData.name : "")) : (wb.modelData.name || qsTr("未命名计划"))
                                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); font.bold: !wb.hotel; color: wb.hotel ? Theme.textSoft : "#ffffff"
                                }
                                BarArea { visible: !wb.hotel; enabled: !wb.hotel; anchors.fill: parent; bar: wb.modelData; srcItem: wb }
                                MouseArea { visible: wb.hotel; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.openPlan(wb.modelData.id, "bookings") }
                            }
                        }
                    }
                    Item {   // 当天在哪一站（没写时间的日程写在站名下面）
                        id: stopRow
                        y: allDay.height
                        width: weekTop.width
                        height: stopCols.maxH
                        Item {
                            id: stopCols
                            readonly property real maxH: {
                                let n = 0
                                for (const l of root.weekInfo.stops || []) {
                                    let h = 0
                                    for (const s of l) h += (s.loose.length ? Theme.px(38) : Theme.px(24)) + Theme.px(2)
                                    n = Math.max(n, h)
                                }
                                return n + (n > 0 ? Theme.px(2) : 0)
                            }
                            Repeater {
                                model: root.weekInfo.stops || []
                                delegate: Column {
                                    required property var modelData
                                    required property int index
                                    x: weekView.gut + index * weekView.colW + Theme.px(3)
                                    width: weekView.colW - Theme.px(6)
                                    spacing: Theme.px(2)
                                    Repeater {
                                        model: parent.modelData
                                        delegate: Rectangle {
                                            id: sc
                                            required property var modelData
                                            objectName: "calWeekStop"
                                            width: parent.width
                                            height: modelData.loose.length ? Theme.px(38) : Theme.px(24)
                                            radius: Theme.px(6)
                                            color: Qt.alpha(modelData.color, 0.1)
                                            Rectangle { width: Theme.px(3); height: parent.height; radius: width / 2; color: sc.modelData.color }
                                            Column {
                                                x: Theme.px(8); width: parent.width - Theme.px(10)
                                                anchors.verticalCenter: parent.verticalCenter
                                                Text { width: parent.width; elide: Text.ElideRight; text: (sc.modelData.n ? sc.modelData.n + " " : "") + (sc.modelData.name || qsTr("未命名地点")); font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.text }
                                                Text {
                                                    visible: sc.modelData.loose.length > 0
                                                    width: parent.width
                                                    elide: Text.ElideRight
                                                    text: sc.modelData.loose.length > 1 ? qsTr("%1 等 %2 条").arg(sc.modelData.loose[0]).arg(sc.modelData.loose.length) : sc.modelData.loose[0] || ""
                                                    font.family: serifFamily; font.pixelSize: Theme.px(11); color: Theme.muted
                                                }
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.openPlan(sc.modelData.id, "")
                                                onContainsMouseChanged: root.hoverTip(containsMouse, sc, sc.modelData.plan + " · " + sc.modelData.name + (sc.modelData.loose.length ? "\n" + sc.modelData.loose.join("\n") : ""))
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {   // 放不下时的位置条
                        parent: weekTop
                        visible: weekTop.contentHeight > weekTop.height
                        x: weekTop.width - width
                        y: weekTop.visibleArea.yPosition * weekTop.height
                        width: Theme.px(3)
                        height: weekTop.visibleArea.heightRatio * weekTop.height
                        radius: width / 2
                        color: Theme.lineStrong
                    }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.lineStrong }
                Flickable {   // 时间轴
                    id: weekFlick
                    objectName: "calWeekFlick"
                    width: parent.width
                    height: parent.height - y
                    contentHeight: 24 * weekView.hourPx
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    interactive: !root.drag
                    Item {
                        width: weekFlick.width
                        height: weekFlick.contentHeight
                        Repeater {
                            model: 24
                            delegate: Item {
                                required property int index
                                y: index * weekView.hourPx
                                width: parent.width
                                Rectangle { x: weekView.gut; width: parent.width - x; height: 1; color: Theme.line; visible: parent.index > 0 }
                                Text { visible: parent.index > 0 && parent.y - height / 2 >= weekFlick.contentY && parent.y + height / 2 <= weekFlick.contentY + weekFlick.height; width: weekView.gut - Theme.px(6); y: -height / 2; horizontalAlignment: Text.AlignRight; text: Fmt.pad(parent.index) + ":00"; font.family: serifFamily; font.pixelSize: Theme.px(11); color: Theme.muted }
                            }
                        }
                        Repeater {
                            model: weekView.days
                            delegate: Rectangle {
                                id: tcol
                                required property var modelData
                                required property int index
                                objectName: "calWeekCol-" + modelData.date
                                x: weekView.gut + index * weekView.colW
                                width: weekView.colW
                                height: parent.height
                                color: weekView.hit(modelData.date) ? Theme.accentSoft : modelData.today ? Qt.alpha(Theme.accent, 0.04) : "transparent"
                                Rectangle { width: 1; height: parent.height; color: Theme.line }
                                Repeater {
                                    model: (root.weekInfo.events || [])[tcol.index] || []
                                    delegate: Rectangle {
                                        id: ev
                                        required property var modelData
                                        required property int index
                                        objectName: "calWeekEvent-" + tcol.modelData.date + "-" + index
                                        readonly property real h: Math.max(Theme.px(22), (modelData.b - modelData.a) / 60 * weekView.hourPx - 2)
                                        x: modelData.col / modelData.n * tcol.width + 1
                                        y: modelData.a / 60 * weekView.hourPx
                                        width: tcol.width / modelData.n - 3
                                        height: h
                                        radius: Theme.px(5)
                                        clip: true
                                        color: Qt.tint("#ffffff", Qt.alpha(modelData.color, 0.18))
                                        Rectangle { width: Theme.px(3); height: parent.height; color: ev.modelData.color }
                                        Column {
                                            x: Theme.px(7); y: Theme.px(2)
                                            width: parent.width - Theme.px(9)
                                            Text {
                                                objectName: "calWeekEventTitle"
                                                width: parent.width
                                                elide: Text.ElideRight
                                                text: ev.modelData.kind ? Fmt.kindLabel(ev.modelData.kind) + (ev.modelData.title ? " · " + ev.modelData.title : "") : ev.modelData.title
                                                font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.text
                                            }
                                            Text {
                                                visible: ev.h > Theme.px(32)
                                                width: parent.width
                                                elide: Text.ElideRight
                                                text: ev.modelData.time + (ev.h > Theme.px(40) ? " · " + ev.modelData.sub : "")
                                                font.family: serifFamily; font.pixelSize: Theme.px(11); color: Theme.textSoft
                                            }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.openPlan(ev.modelData.id, ev.modelData.tab)
                                            onContainsMouseChanged: root.hoverTip(containsMouse, ev, ev.modelData.time + " " + (ev.modelData.title || Fmt.kindLabel(ev.modelData.kind)) + "\n" + ev.modelData.sub + "\n" + ev.modelData.plan)
                                        }
                                    }
                                }
                                Rectangle {   // 当前时间线
                                    objectName: "calWeekNow"
                                    visible: tcol.modelData.today
                                    y: root.nowMin / 60 * weekView.hourPx
                                    width: parent.width
                                    height: Theme.px(2)
                                    color: Theme.danger
                                    Rectangle { x: -Theme.px(3); y: -Theme.px(2); width: Theme.px(6); height: width; radius: width / 2; color: Theme.danger }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- 季节 ----
        CalSeasonView {
            objectName: "calSeasonView"
            visible: root.isSeason
            anchors.fill: parent
            anchors.margins: Theme.px(12)
            info: calendar.info.season || ({})
            narrow: root.narrow
            onOpenPlan: (id) => root.openPlan(id, "")
            onHoverText: (on, item, text) => root.hoverTip(on, item, text)
        }

        // ---- 月 ----
        Column {
            visible: root.isMonth
            anchors.fill: parent
            anchors.margins: Theme.px(12)
            Row {
                id: wdHead
                width: parent.width
                height: Theme.px(26)
                Repeater {
                    model: [0, 1, 2, 3, 4, 5, 6].map(i => { const w = (i + calendar.weekStart) % 7; return Fmt.en ? Fmt.wdEn[w] : "周" + Fmt.wdZh[w] })
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
                                            MouseArea {   // 点日期：看这一周
                                                objectName: "calDayNum-" + parent.parent.modelData.date
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: calendar.showWeek(parent.parent.modelData.date)
                                            }
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
    TpCard {
        id: sidePanel
        objectName: "calSide"
        visible: root.sideShown
        z: 2
        x: parent.width - width - root.gutter
        y: mainCard.y
        width: root.sideW
        height: mainCard.height
        radius: Theme.px(16)
        color: root.overSide ? Theme.accentSoft : Theme.card
        // 拖到这里时才有边（松手去掉日期）
        Rectangle { anchors.fill: parent; visible: root.overSide; radius: parent.radius; color: "transparent"; border.width: 2; border.color: Theme.accent }
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
                    text: (plans.list.total || 0) === 0 ? qsTr("还没有计划。") : qsTr("全部都定了日期。")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                }
                SideGroup { title: qsTr("计划中"); rows: root.planning }
                SideGroup { title: qsTr("想去"); rows: root.wishes }
                Text {
                    objectName: "calSideHint"
                    width: parent.width
                    wrapMode: Text.Wrap
                    topPadding: Theme.px(4)
                    text: qsTr("拖到某天定日期，拖回来去掉日期")
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

    // 当前时间线：一分钟走一次
    property int nowMin: { const d = new Date(); return d.getHours() * 60 + d.getMinutes() }
    Timer { interval: 60000; repeat: true; running: root.shown && root.isWeek; onTriggered: { const d = new Date(); root.nowMin = d.getHours() * 60 + d.getMinutes() } }
    // 悬停提示（周视图的事件和站、季节表）
    function hoverTip(on, item, text) {
        if (!on || root.drag) { tip.visible = false; return }
        tip.text = text
        const p = item.mapToItem(root, 0, item.height + Theme.px(4))
        tip.x = Math.max(Theme.px(6), Math.min(root.width - tip.width - Theme.px(6), p.x))
        tip.y = Math.min(root.height - tip.height - Theme.px(6), p.y)
        tip.visible = true
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
        onHideRequested: (id, name) => { calendar.setHidden(id, true); root.toast(qsTr("「%1」已在日历里隐藏，右边可恢复").arg(name), false) }
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
                Behavior on color { enabled: Theme.motion; ColorAnimation { duration: 300; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curveMove } }
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
        TpTip { text: nb.text === "‹" ? qsTr("往前") : qsTr("往后"); show: nbMa.containsMouse }
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
