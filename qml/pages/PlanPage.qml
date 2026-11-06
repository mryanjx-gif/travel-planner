// 完整计划页（0.10.0，S-02、P-01–P-06）：右边的面板，连续圆角 30；主地图跟着站点走（PO 选小地图 A：不做小地图）。
// 照 Electron pages-plan.js render / itineraryHtml / bindItinerary / setFocus，PO 选的改进：
//   I1 交通画成站点之间的连接线、I2 日程按天分组（SchedList）、I3 在路上时高亮今天、I4 往下滚时页头收成一行。
// 站点列表按需加载（最长的路线 48 站）：ListView，模型只是站点个数，数据变了不重建卡片、滚动位置不动（P-04）。
// 标签（PO：没做的不显示，后面每批加上去）：行程、交通住宿、行李与待办（0.10.4）、消费（0.10.1）、游记、攻略（0.10.4）。放不下时能左右拖。别的标签的内容放在列表的页头里，模型变成 0，页头收起和滚动都一样。
// source = planPage.info（plans::pageInfo）。
import QtQuick
import TPApp

Item {
    id: root
    property Item backdrop: null
    property var source: ({})
    property var info: ({})                 // 关掉（变空）时面板滑出去，内容留着
    onSourceChanged: if (source && source.id) {
        if (source.id !== info.id) { altOpen = false; elevEdit = -1 }
        info = source
    }
    property bool shown: false
    property int focusIndex: -1             // 聚焦的站点（mapc.markers.focusStop）
    signal closeRequested()
    signal simpleRequested()                // 简易版
    signal listRequested()                  // ☰ 我的旅程
    signal editRequested(string planId)
    signal daysRequested(string current)
    signal startRequested()                 // 开始计划（想去）
    signal togglePin(string planId)
    signal toggleNext(string planId, bool on)
    signal pickStop(int index)
    signal addBooking(string kind)
    signal editBooking(var row)
    signal deleteBooking(var row)
    signal addExpense(var draft)
    signal editExpense(var row)
    signal deleteExpense(var row)
    signal budgetRequested()
    signal editPerson(var person)
    signal notice(string text)              // 0.10.4 行李与待办的小提示
    signal saveTemplateRequested()
    signal editLocalEmergency()
    signal editEmergency()
    signal guideLinkRequested(string selected)
    signal guidePhotoRequested()
    // 0.10.5 照片标签、看大图（对话框和 Lightbox 在 Main.qml）
    signal photoAddRequested(string stopId)
    signal photoCaptionRequested(var photo)
    signal photoRemoveRequested(var photo)
    signal photoViewRequested(var list, int index)

    readonly property bool missing: shown && !(source && source.id)
    readonly property var stops: info.stops || []
    readonly property bool route: info.type === "route"
    readonly property bool stepping: route && stops.length > 1
    readonly property bool readOnly: info.readOnly === true
    readonly property real compactH: Theme.px(52)
    readonly property real navH: Theme.px(44)
    readonly property real margin: Theme.px(24)
    // 页头收成一行（I4）：标题滚出去以后
    readonly property Item hdr: list.headerItem
    readonly property real navNatural: hdr ? list.y + hdr.y + hdr.navY - list.contentY : 0
    readonly property bool collapsed: hdr ? hdr.y + hdr.titleBottom - list.contentY < 0 : false
    readonly property bool scheduleEditing: edit.j !== -2 || elevEdit >= 0
    property string tab: "itinerary"        // itinerary / bookings / packing / budget / photos / journal / guide
    property bool menuOpen: false
    property real menuY: 0
    property var openNotes: ({})
    property bool cardClick: false
    QtObject { id: edit; property int stop: -1; property int j: -2; property var draft: ({}); property string err: "" }
    function cancelSchedule() { edit.j = -2; elevEdit = -1 }
    // 0.10.3 海拔就地改。状态放外面：滚远了卡片销毁也不丢
    property int elevEdit: -1
    property string elevDraft: ""
    property string elevErr: ""
    property bool altOpen: false            // 高反提示超过 2 条时展开
    function beginElev(k) { edit.j = -2; elevErr = ""; elevDraft = String(Math.round(stops[k].elev)); elevEdit = k }
    function saveElev() {
        const e = planPage.setElev(elevEdit, elevDraft)
        if (e) { elevErr = e === "badElev" ? qsTr("海拔要写成 -500 到 9000 之间的数字") : qsTr("数据现在只能看、不能改"); return }
        elevEdit = -1
    }
    function altWarnText(w) {
        if (w.kind === "run") return qsTr("到第 %1 站时已经连续 %2 晚在 3000 m 以上往上走，中间没有休整日（建议每 3–4 天一天不往上）").arg(w.stop).arg(w.nights)
        const head = qsTr("第 %1 → %2 站：睡觉的海拔从 %3 m 升到 %4 m").arg(w.from).arg(w.to).arg(Fmt.num(w.fromE)).arg(Fmt.num(w.toE))
        return head + (w.days > 1 ? qsTr("（+%1 m，%2 天），超过每天 500 m").arg(Fmt.num(w.gain)).arg(w.days) : qsTr("（+%1 m），超过每天 500 m").arg(Fmt.num(w.gain)))
    }
    function altTagText(t) { return t === "rise" ? qsTr("上升太快") : t === "high" ? qsTr("3000 m 以上") : "" }
    function wxLine(w) {
        return qsTr("%1 预报：%2 <b>%3–%4°C</b>").arg(Fmt.md(w.date)).arg(Fmt.wxLabel(w.kind)).arg(Math.round(w.lo)).arg(Math.round(w.hi))
             + (w.pop !== undefined ? qsTr(" · 降水概率 %1%").arg(Math.round(w.pop)) : "")
    }
    function climLine(c) {
        return qsTr("往年 %1：<b>%2–%3°C</b> · 降水 %4 mm · %5 天下雨（%6）").arg(Fmt.en ? Fmt.monEn[c.month - 1] : qsTr("%1 月").arg(c.month))
            .arg(Math.round(c.lo)).arg(Math.round(c.hi)).arg(Math.round(c.mm)).arg(Math.round(c.rainDays)).arg(c.years)
    }
    readonly property bool hasOpenMeteo: stops.some(s => s.elev !== undefined) || (weather.stops || []).some(x => x.wx || x.clim)
    // 换标签。quick = 消费标签，光标放到一行快记（J3）
    function openTab(t) {
        edit.j = -2
        flushGuide()
        scrollAnim.stop()
        tab = t === "quick" ? "budget" : t
        list.contentY = list.originY
        Qt.callLater(() => { list.contentY = list.originY; if (t === "quick" && list.headerItem) list.headerItem.focusQuick() })
    }
    // 0.10.4 攻略
    function flushGuide() { if (list.headerItem) list.headerItem.guide.flush() }
    function guideInsertLink(md) { list.headerItem.guide.replaceSelection(md) }
    function guideInsertPhoto(ph) { list.headerItem.guide.insertPhoto(ph) }
    function guideBeginEdit() { list.headerItem.guide.beginEdit() }
    function pasteForShot(t) {   // 截图：粘贴的输入框和紧急信息卡都在清单下面
        list.headerItem.packing.paste(t)
        shotBottom.start()
    }
    Timer { id: shotBottom; interval: 200; onTriggered: list.contentY = list.originY + list.contentHeight - list.height }
    function scrollInGuide(y, animate) {
        const h = list.headerItem
        const to = Math.max(list.originY, Math.min(h.y + h.guide.y + y - compactH - Theme.px(8), list.originY + list.contentHeight - list.height))
        scrollAnim.stop()
        if (!animate || !Theme.motion) { list.contentY = to; return }
        scrollAnim.to = to
        scrollAnim.start()
    }
    // 消费标签里点"来自交通住宿"的一行：回到交通住宿，打开它的表单
    function editBookingById(id) {
        openTab("bookings")
        for (const d of info.bookings || []) for (const b of d.rows) if (b.id === id) { root.editBooking(b); return }
    }
    // 截图 trip-edit：改第一条日程
    function beginFirstEdit() {
        for (let k = 0; k < stops.length; ++k) {
            const d = stops[k].days || []
            if (!d.length) continue
            const it = d[0].items[0]
            edit.draft = { date: it.date, time: it.time, end: it.end, text: it.text }
            edit.err = ""; edit.stop = k; edit.j = it.j
            reveal(k, true)
            return
        }
    }

    function statusLabel(s) { return s === "completed" ? qsTr("已完成") : s === "wish" ? qsTr("想去") : qsTr("计划中") }
    function statusColor(s) { return s === "completed" ? "#2b8a3e" : s === "wish" ? "#7b61b5" : "#e8590c" }
    function legLabel(m) { return m === "train" ? qsTr("火车") : m === "ferry" ? qsTr("船") : m === "drive" ? qsTr("自驾") : m === "walk" ? qsTr("徒步") : qsTr("飞机") }
    function legText(leg) {
        if (!leg || !leg.set) return ""
        const bits = [legLabel(leg.mode)]
        if (leg.dist > 0) bits.push(Fmt.dist(leg.dist))
        if (leg.time > 0) bits.push(qsTr("约 %1").arg(Fmt.dur(leg.time)))
        return bits.join(" · ")
    }
    function sunText(x) {
        if (!x) return ""
        const d = Fmt.md(x.date)
        if (x.polar === "day") return qsTr("%1 极昼：太阳整天不落").arg(d)
        if (x.polar === "night") return qsTr("%1 极夜：太阳整天不升起").arg(d)
        return qsTr("%1 日出 <b>%2</b> · 日落 <b>%3</b> · 白天 %4 小时 %5 分").arg(d).arg(x.rise).arg(x.set).arg(Math.floor(x.dayMin / 60)).arg(x.dayMin % 60)
             + (x.approx ? qsTr("（按经度估算）") : "")
    }
    function warnText(w) {
        const kind = w.docKind === "passport" ? qsTr("护照") : w.docKind === "visa" ? qsTr("签证") : qsTr("其他证件")
        const name = [w.owner, kind, w.label].filter(x => x).join(" · ")
        if (w.kind === "passportExpired") return qsTr("%1 在 %2 已经过期，出发前要换新护照").arg(name).arg(w.expires)
        if (w.kind === "passport6m") return qsTr("%1 在 %2 到期，离回程（%3）不足 6 个月；很多国家要求护照有效期在 6 个月以上").arg(name).arg(w.expires).arg(w.end)
        return qsTr("%1 在 %2 到期，早于回程（%3）").arg(name).arg(w.expires).arg(w.end)
    }
    function seasonLabel(k) { return k === "best" ? qsTr("最适合") : k === "nice" ? qsTr("不错") : k === "meh" ? qsTr("有点热/人多") : k === "bad" ? qsTr("不推荐") : "" }
    function seasonColor(k) { return Theme.seasonColor(k) }
    function seasonNote(m) {
        const c = (info.season.cells || [])[m - 1] || {}
        const place = Fmt.en && info.season.nameEn ? info.season.nameEn : info.season.name
        const txt = Fmt.en && c.textEn ? c.textEn : (c.text || "")
        const what = c.kind ? seasonLabel(c.kind) + (txt ? " · " + txt : "") : qsTr("表里没写")
        return place + " · " + (Fmt.en ? Fmt.monEn[m - 1] : qsTr("%1 月").arg(m)) + (Fmt.en ? ": " : "：") + what
    }

    // ---- 站点切换 ----
    function step(d) {
        const n = stops.length
        if (n === 0) return
        const i = focusIndex < 0 ? 0 : Math.max(0, Math.min(n - 1, focusIndex + d))
        root.pickStop(i)
    }
    // 把这一站滚进可见区域（force = 一定滚到顶上，看今天的行程用）。上面让开收起的页头和切换条
    function reveal(i, force) {
        if (tab !== "itinerary") return
        const top = compactH + (stepping ? navH : 0) + Theme.px(6)
        const it = list.itemAtIndex(i)
        if (it && !force) {
            const a = it.y - list.contentY + list.y, b = a + it.height
            if (a >= top && b <= root.height) return
        }
        scrollAnim.stop()
        const from = list.contentY
        list.positionViewAtIndex(i, ListView.Beginning)
        const to = Math.max(list.originY, Math.min(list.contentY - top + list.y, list.originY + list.contentHeight - list.height))
        if (!Theme.motion) { list.contentY = to; return }
        list.contentY = from
        scrollAnim.to = to
        scrollAnim.start()
    }
    onFocusIndexChanged: if (focusIndex >= 0 && !cardClick && shown) reveal(focusIndex, false)
    function jumpTo(i) { Qt.callLater(() => reveal(i, true)) }
    function clickCard(i) { cardClick = true; root.pickStop(i); cardClick = false }
    function resetView() {
        edit.j = -2
        tab = "itinerary"
        menuOpen = false
        openNotes = ({})
        scrollAnim.stop()
        Qt.callLater(list.positionViewAtBeginning)
    }
    function openMenu(btn) { menuY = btn.mapToItem(root, 0, btn.height).y + Theme.px(6); menuOpen = !menuOpen }
    function containsGlobal(gx, gy) {
        const p = root.mapFromItem(null, gx, gy)
        return visible && p.x >= 0 && p.y >= 0 && p.x <= width && p.y <= height
    }

    GlassPanel {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: Theme.radiusLarge
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.95 : 0.99)
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onClicked: root.menuOpen = false; onWheel: (w) => w.accepted = true }

    NumberAnimation { id: scrollAnim; target: list; property: "contentY"; duration: 260; easing.type: Easing.OutCubic }

    ListView {
        id: list
        // 页头变高时（比如补完海拔才出现高反提示）：本来在顶上就留在顶上，标题不被挤出去
        property bool atTop: true
        onContentYChanged: atTop = contentY <= originY + 1
        onOriginYChanged: if (atTop) contentY = originY
        objectName: "tripList"
        anchors.fill: parent
        anchors.topMargin: Theme.px(6)
        anchors.bottomMargin: Theme.px(6)
        clip: true
        visible: !root.missing
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 1200
        model: root.tab === "itinerary" ? root.stops.length : 0
        onMovementStarted: { scrollAnim.stop(); root.menuOpen = false }

        header: Column {
            id: head
            readonly property real titleBottom: titleRow.y + titleRow.height
            readonly property real navY: navSpace.y
            readonly property alias guide: guideTab
            readonly property alias packing: packingTab
            function focusQuick() { budgetTab.focusQuick() }
            x: root.margin
            width: list.width - 2 * root.margin
            topPadding: Theme.px(14)
            spacing: Theme.px(12)

            // ---- 封面 + 标题 + chip（右边给 × 让位）----
            Row {
                id: titleRow
                width: parent.width - Theme.px(40)
                spacing: Theme.px(14)
                Rectangle {
                    id: coverBox
                    width: Theme.px(64); height: width
                    radius: Theme.px(12)
                    color: Theme.panel2
                    clip: true
                    RouteSketch {
                        visible: cover.status !== Image.Ready
                        anchors.fill: parent
                        stops: root.stops
                        route: root.route
                        tone: root.info.color || Theme.accent
                    }
                    Image {
                        id: cover
                        objectName: "tripCover"
                        anchors.fill: parent
                        source: root.info.coverThumb || ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: 320
                        MouseArea {   // 0.10.5 看大图：封面在照片列表里就从它开始翻（没设封面时显示的是第一张）
                            anchors.fill: parent
                            enabled: cover.status === Image.Ready
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const l = root.info.photos || [], i = l.findIndex(p => p.id === root.info.cover)
                                if (i >= 0 || !root.info.coverFile) root.photoViewRequested(l, Math.max(i, 0))
                                else root.photoViewRequested([{ file: root.info.coverFile, thumb: root.info.coverThumb }], 0)
                            }
                        }
                    }
                }
                Column {
                    width: parent.width - coverBox.width - parent.spacing
                    spacing: Theme.px(6)
                    Text {
                        objectName: "tripTitle"
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.info.name || qsTr("未命名计划")
                        font.family: serifFamily; font.pixelSize: Theme.px(23); font.bold: true; color: Theme.text
                    }
                    Flow {
                        width: parent.width
                        spacing: Theme.px(5)
                        Chip { text: root.statusLabel(root.info.status); tone: root.statusColor(root.info.status) }
                        Chip { text: root.route ? qsTr("路线") + " · " + qsTr("%1 站").arg(root.stops.length) : qsTr("单一目的地") }
                        Repeater { model: root.info.regions || []; delegate: Chip { required property string modelData; text: qsTr(modelData) } }
                        Chip { visible: text !== ""; plain: true; text: Fmt.range(root.info.startDate, root.info.endDate) }
                        Chip {
                            objectName: "tripDays"
                            visible: !root.info.startDate && !root.info.endDate
                            clickable: !root.readOnly
                            text: root.info.days >= 1 ? qsTr("预计 %1 天").arg(root.info.days) : qsTr("设预计天数")
                            onClicked: root.daysRequested(root.info.days >= 1 ? String(root.info.days) : "")
                        }
                    }
                }
            }
            // ---- 按钮 ----
            Flow {
                width: parent.width
                spacing: Theme.px(8)
                TpButton { objectName: "tripEdit"; compact: true; kind: "primary"; text: qsTr("编辑"); onClicked: root.editRequested(root.info.id) }
                TpButton { objectName: "tripSimple"; compact: true; text: qsTr("简易版"); onClicked: root.simpleRequested() }
                TpButton { objectName: "tripPlans"; compact: true; text: root.info.status === "wish" ? qsTr("愿望清单") : qsTr("计划库"); onClicked: root.listRequested() }
                TpButton {
                    id: bigMore
                    objectName: "tripMore"
                    compact: true
                    implicitWidth: Theme.px(40)
                    text: "⋯"
                    Accessible.name: qsTr("更多")
                    onClicked: root.openMenu(bigMore)
                }
            }
            // ---- 季节条 ----
            Column {
                objectName: "tripSeason"
                visible: !!(root.info.season && root.info.season.cells)
                width: parent.width
                spacing: Theme.px(4)
                readonly property int curMonth: new Date().getMonth() + 1
                property int hoverMonth: 0
                Row {
                    id: seasonRow
                    width: parent.width
                    spacing: Theme.px(2)
                    Repeater {
                        model: 12
                        delegate: Rectangle {
                            required property int index
                            readonly property var cell: ((root.info.season || {}).cells || [])[index] || {}
                            width: (seasonRow.width - 11 * seasonRow.spacing) / 12
                            height: Theme.px(10)
                            radius: Theme.px(3)
                            color: root.seasonColor(cell.kind)
                            border.width: index + 1 === seasonRow.parent.curMonth ? 2 : 0
                            border.color: Theme.text
                            HoverHandler { onHoveredChanged: seasonRow.parent.hoverMonth = hovered ? index + 1 : 0 }
                        }
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: parent.visible ? root.seasonNote(parent.hoverMonth || parent.curMonth) : ""
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                }
            }
            // ---- 证件警告（"管理证件"在阶段 ⑥ 有证件设置以后再加）----
            Rectangle {
                objectName: "tripWarn"
                visible: (root.info.warnings || []).length > 0
                width: parent.width
                height: visible ? warnCol.implicitHeight + Theme.px(16) : 0
                radius: Theme.radiusControl
                color: "#fbf1dc"
                Column {
                    id: warnCol
                    x: Theme.px(12); y: Theme.px(8)
                    width: parent.width - Theme.px(24)
                    spacing: Theme.px(4)
                    Repeater {
                        model: root.info.warnings || []
                        delegate: Text {
                            required property var modelData
                            width: warnCol.width
                            wrapMode: Text.Wrap
                            text: root.warnText(modelData)
                            font.family: serifFamily; font.pixelSize: Theme.px(13); color: "#7a5a1c"
                        }
                    }
                }
            }
            // ---- 高反提示（第 7 题 A：页头一条 + 站点上标记；超过 2 条折叠）----
            Rectangle {
                objectName: "tripAlt"
                readonly property var warns: root.info.altWarnings || []
                visible: warns.length > 0
                width: parent.width
                height: visible ? altCol.implicitHeight + Theme.px(16) : 0
                radius: Theme.radiusControl
                color: "#fbf1dc"
                Column {
                    id: altCol
                    x: Theme.px(12); y: Theme.px(8)
                    width: parent.width - Theme.px(24)
                    spacing: Theme.px(4)
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        textFormat: Text.StyledText
                        text: "<b>" + qsTr("高反提示") + "</b>" + (root.info.altApprox ? qsTr("（有的站点没有日期，按每站住一晚估算）") : "")
                        font.family: serifFamily; font.pixelSize: Theme.px(13); color: "#7a5a1c"
                    }
                    Repeater {
                        model: root.altOpen ? parent.parent.warns : parent.parent.warns.slice(0, 2)
                        delegate: Text {
                            required property var modelData
                            width: altCol.width
                            wrapMode: Text.Wrap
                            text: "· " + root.altWarnText(modelData)
                            font.family: serifFamily; font.pixelSize: Theme.px(13); color: "#7a5a1c"
                        }
                    }
                    LinkText {
                        objectName: "tripAltMore"
                        visible: altCol.parent.warns.length > 2
                        small: true
                        tone: "#7a5a1c"
                        text: root.altOpen ? qsTr("收起") : qsTr("还有 %1 项").arg(altCol.parent.warns.length - 2)
                        onClicked: root.altOpen = !root.altOpen
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("参照 Wilderness Medical Society 2019 年指南：3000 m 以上，睡觉的海拔每天升高不超过 500 m，每 3–4 天休整一天。只是提醒，不是医疗建议。海拔数据来自 90 m 网格的地形数据，可以点站点里的海拔手动改。")
                        font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: "#8f7440"
                    }
                }
            }
            // ---- 标签 ----
            Item {
                width: parent.width
                height: Theme.px(34)
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                Flickable {
                    id: tabsFlick
                    objectName: "tripTabs"
                    anchors.fill: parent
                    contentWidth: tabsRow.width + Theme.px(4)
                    contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    Row {
                        id: tabsRow
                        x: Theme.px(2)
                        height: parent.height
                        spacing: Theme.px(20)
                        Repeater {
                            model: ["itinerary", "bookings", "packing", "budget", "photos", "journal", "guide"]
                            delegate: Item {
                                required property string modelData
                                readonly property bool on: root.tab === modelData
                                readonly property var kit: root.info.kit || ({})
                                // 数字（Electron counts）：行李与待办写"勾了几条 / 一共"，攻略写了就是 ✓
                                readonly property string n: {
                                    switch (modelData) {
                                    case "bookings": return root.info.bookCount > 0 ? String(root.info.bookCount) : ""
                                    case "budget": { const c = ((root.info.money || {}).rows || []).length; return c > 0 ? String(c) : "" }
                                    case "packing": return kit.totalAll > 0 ? kit.doneAll + "/" + kit.totalAll : ""
                                    case "photos": return (root.info.photos || []).length > 0 ? String(root.info.photos.length) : ""
                                    case "journal": return (root.info.journal || []).length > 0 ? String(root.info.journal.length) : ""
                                    case "guide": return (root.info.guide || "") !== "" ? "✓" : ""
                                    }
                                    return ""
                                }
                                readonly property int due: modelData === "packing" ? (kit.dueSoon || 0) + (kit.dueOver || 0) : 0   // M2
                                objectName: "tripTab-" + modelData
                                width: tabLabel.implicitWidth + (due > 0 ? dueBadge.width + Theme.px(5) : 0)
                                height: parent.height
                                onOnChanged: if (on) tabsFlick.contentX = Math.max(0, Math.min(x - Theme.px(24), tabsFlick.contentWidth - tabsFlick.width))
                                Accessible.role: Accessible.PageTab
                                Accessible.name: tabLabel.text
                                Text {
                                    id: tabLabel
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: (parent.modelData === "bookings" ? qsTr("交通住宿") : parent.modelData === "packing" ? qsTr("行李与待办") : parent.modelData === "budget" ? qsTr("消费") : parent.modelData === "photos" ? qsTr("照片")
                                           : parent.modelData === "journal" ? qsTr("游记") : parent.modelData === "guide" ? qsTr("攻略") : qsTr("行程")) + (parent.n !== "" ? " " + parent.n : "")
                                    font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: parent.on; color: parent.on ? Theme.text : Theme.muted
                                }
                                Rectangle {
                                    id: dueBadge
                                    objectName: "tripTabDue"
                                    visible: parent.due > 0
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.max(height, dueCount.implicitWidth + Theme.px(8))
                                    height: Theme.px(17)
                                    radius: height / 2
                                    color: "#fbf1dc"
                                    Text { id: dueCount; anchors.centerIn: parent; text: parent.parent.due; font.family: serifFamily; font.pixelSize: Theme.px(11); font.bold: true; color: "#7a5a1c" }
                                }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.accent; visible: parent.on }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.openTab(parent.modelData) }
                            }
                        }
                    }
                }
            }
            BookingsTab {
                objectName: "tripBookings"
                visible: root.tab === "bookings"
                width: parent.width
                info: root.info
                readOnly: root.readOnly
                onAdd: (kind) => root.addBooking(kind)
                onEdit: (row) => root.editBooking(row)
                onRemove: (row) => root.deleteBooking(row)
            }
            BudgetTab {
                id: budgetTab
                objectName: "tripBudget"
                visible: root.tab === "budget"
                width: parent.width
                money: root.info.money || ({})
                readOnly: root.readOnly
                onEdit: (row) => row.fromBooking ? root.editBookingById(row.bookingId) : root.editExpense(row)
                onRemove: (row) => root.deleteExpense(row)
                onMore: (draft) => root.addExpense(draft)
                onBudgetRequested: root.budgetRequested()
                onPersonClicked: (p) => root.editPerson(p)
            }
            PackingTab {
                id: packingTab
                objectName: "tripPacking"
                visible: root.tab === "packing"
                width: parent.width
                kit: root.info.kit || ({})
                readOnly: root.readOnly
                onNotice: (t) => root.notice(t)
                onSaveTemplateRequested: root.saveTemplateRequested()
                onEditLocal: root.editLocalEmergency()
                onEditEmergency: root.editEmergency()
            }
            PhotosTab {
                objectName: "tripPhotosTab"
                visible: root.tab === "photos"
                width: parent.width
                rows: root.info.photos || []
                coverId: root.info.cover || ""
                coverThumb: root.info.cover ? root.info.coverThumb || "" : ""
                readOnly: root.readOnly
                onNotice: (t) => root.notice(t)
                onAddRequested: root.photoAddRequested(root.focusIndex >= 0 && root.stops[root.focusIndex] ? root.stops[root.focusIndex].id || "" : "")
                onCaptionRequested: (ph) => root.photoCaptionRequested(ph)
                onRemoveRequested: (ph) => root.photoRemoveRequested(ph)
                onViewRequested: (l, i) => root.photoViewRequested(l, i)
            }
            JournalTab {
                objectName: "tripJournal"
                visible: root.tab === "journal"
                width: parent.width
                rows: root.info.journal || []
            }
            GuideTab {
                id: guideTab
                objectName: "tripGuide"
                visible: root.tab === "guide"
                width: parent.width
                planId: root.info.id || ""
                guide: root.info.guide || ""
                view: root.info.guideView || ({})
                readOnly: root.readOnly
                viewH: Math.max(Theme.px(280), list.height - root.compactH - Theme.px(130))
                viewTop: list.contentY - head.y - y + root.compactH
                onScrollRequested: (yy, animate) => root.scrollInGuide(yy, animate)
                onLinkRequested: (s) => root.guideLinkRequested(s)
                onPhotoRequested: root.guidePhotoRequested()
                // 正文里的照片一组看大图；在这个计划里的带上站点和拍摄时间，说明以正文写的为准
                onPhotoViewRequested: (blocks, k) => {
                    const known = root.info.photos || [], list = []
                    let at = 0
                    for (let i = 0; i < blocks.length; ++i) {
                        const b = blocks[i]
                        if (b.type !== "img" || !b.file) continue
                        if (i === k) at = list.length
                        const hit = known.find(p => p.id === b.id) || {}
                        list.push(Object.assign({}, hit, { file: b.file, thumb: b.thumb, caption: b.caption || hit.caption || "" }))
                    }
                    root.photoViewRequested(list, at)
                }
            }
            // ---- 备注 ----
            Column {
                visible: root.tab === "itinerary" && (root.info.notes || "") !== ""
                width: parent.width
                spacing: Theme.px(4)
                Text { text: qsTr("备注"); font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: Theme.text }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    lineHeight: 1.3
                    text: root.info.notes || ""
                    font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.textSoft
                }
            }
            // 切换条停在这里，往下滚时贴在页头下面（画在列表外面）
            Item { id: navSpace; width: parent.width; height: root.stepping && root.tab === "itinerary" ? root.navH : 0 }
            Item {
                visible: root.tab === "itinerary"
                width: parent.width
                height: stopsTitle.implicitHeight
                Text {
                    id: stopsTitle
                    bottomPadding: Theme.px(2)
                    text: root.stops.length === 0 ? qsTr("没有有效的站点坐标。") : root.route ? qsTr("站点安排") : qsTr("行程安排")
                    font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: root.stops.length === 0 ? Theme.muted : Theme.text
                }
                LinkText {   // 第 6 题 A：点了才拉，这次打开里不再显示
                    objectName: "tripClimate"
                    visible: root.stops.length > 0 && weather.climateAvailable && !weather.climateShown
                    anchors.right: parent.right
                    small: true
                    text: weather.climateBusy ? qsTr("取数据中…") : qsTr("往年同月气候")
                    onClicked: if (!weather.climateBusy) weather.fetchClimate()
                }
            }
        }
        footer: Item {
            width: list.width
            height: attribution.visible ? attribution.implicitHeight + Theme.px(24) : Theme.px(24)
            Text {   // CC BY 署名（设置 → 关于要到阶段 ⑥，先放这里）
                id: attribution
                objectName: "tripAttribution"
                visible: root.tab === "itinerary" && root.hasOpenMeteo
                x: root.margin
                y: Theme.px(4)
                width: parent.width - 2 * root.margin
                wrapMode: Text.Wrap
                text: qsTr("天气、气候、海拔数据：Open-Meteo（CC BY 4.0）")
                font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
            }
        }

        delegate: Item {
            id: wrap
            required property int index
            readonly property var s: root.stops[index] || ({})
            readonly property bool focused: root.focusIndex === index
            readonly property bool today: root.info.todayStop === index
            readonly property string legLine: root.legText(s.leg)
            readonly property bool hasLeg: root.route && index < root.stops.length - 1
            width: list.width
            height: card.height + (hasLeg ? Theme.px(38) : Theme.px(12))

            Rectangle {
                id: card
                objectName: "tripCard" + wrap.index
                x: root.margin - Theme.px(6)
                width: parent.width - 2 * x
                height: col.implicitHeight + Theme.px(24)
                radius: Theme.px(16)
                color: "#ffffff"
                border.width: wrap.focused ? 2 : 1
                border.color: wrap.focused ? Theme.accent : Theme.line
                MouseArea {
                    anchors.fill: parent
                    enabled: root.route
                    cursorShape: root.route ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.clickCard(wrap.index)
                }
                Column {
                    id: col
                    x: Theme.px(14); y: Theme.px(12)
                    width: parent.width - Theme.px(28)
                    spacing: Theme.px(6)
                    Row {
                        width: parent.width
                        spacing: Theme.px(8)
                        Rectangle {
                            width: Theme.px(22); height: width; radius: width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: wrap.s.color || root.info.color || Theme.accent
                            Text { visible: root.route; anchors.centerIn: parent; text: wrap.index + 1; font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: "#ffffff" }
                        }
                        Text {
                            objectName: "tripStopName"
                            width: Math.min(implicitWidth, parent.width - Theme.px(30) - (todayChip.visible ? todayChip.width + Theme.px(8) : 0))
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                            text: wrap.s.name || qsTr("未命名地点")
                            font.family: serifFamily; font.pixelSize: Theme.px(16.5); font.bold: true; color: Theme.text
                        }
                        Rectangle {   // I3
                            id: todayChip
                            objectName: "tripToday"
                            visible: wrap.today
                            anchors.verticalCenter: parent.verticalCenter
                            width: todayText.implicitWidth + Theme.px(14); height: Theme.px(20); radius: height / 2
                            color: "#fbf1dc"
                            Text { id: todayText; anchors.centerIn: parent; text: qsTr("今天"); font.family: serifFamily; font.pixelSize: Theme.px(11.5); font.bold: true; color: "#7a5a1c" }
                        }
                    }
                    Text {
                        visible: text !== ""
                        text: {
                            const d = Fmt.range(wrap.s.arriveDate, wrap.s.departDate)
                            const n = Fmt.nights(wrap.s.arriveDate, wrap.s.departDate)
                            return d + (n > 0 ? " · " + (n === 1 && Fmt.en ? "1 night" : qsTr("住 %1 晚").arg(n)) : "")
                        }
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                    }
                    // 当天的交通住宿（J2），点了到交通住宿标签
                    Flow {
                        objectName: "tripStopBooks"
                        visible: (wrap.s.books || []).length > 0
                        width: parent.width
                        spacing: Theme.px(5)
                        Repeater {
                            model: wrap.s.books || []
                            delegate: Chip {
                                required property var modelData
                                clickable: true
                                text: Fmt.kindLabel(modelData.kind) + (modelData.title ? " " + modelData.title : "")
                                onClicked: root.openTab("bookings")
                            }
                        }
                    }
                    Row {
                        visible: !!wrap.s.sun
                        width: parent.width
                        spacing: Theme.px(5)
                        TpIcon {
                            width: Theme.px(15); height: width
                            name: wrap.s.sun && wrap.s.sun.polar === "night" ? "moon" : "sun"
                            color: Theme.muted
                        }
                        Text {
                            objectName: "tripSun"
                            width: parent.width - Theme.px(20)
                            wrapMode: Text.Wrap
                            textFormat: Text.StyledText
                            text: root.sunText(wrap.s.sun)
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                        }
                    }
                    // 海拔：点了就地改（Enter 保存、Esc 取消）；清空后重新取
                    Row {
                        objectName: "tripElev" + wrap.index
                        visible: wrap.s.elev !== undefined && root.elevEdit !== wrap.index
                        width: parent.width
                        spacing: Theme.px(5)
                        TpIcon { width: Theme.px(15); height: width; name: "mountain"; color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            objectName: "tripElevText"
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.StyledText
                            text: qsTr("海拔 <b>%1 m</b>").arg(Fmt.num(wrap.s.elev || 0))
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                            MouseArea {
                                objectName: "tripElevClick"
                                anchors.fill: parent
                                enabled: !root.readOnly
                                cursorShape: root.readOnly ? Qt.ArrowCursor : Qt.PointingHandCursor
                                onClicked: root.beginElev(wrap.index)
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("点一下修改海拔")
                            }
                        }
                        Rectangle {
                            objectName: "tripAltTag"
                            visible: (wrap.s.altTag || "") !== ""
                            anchors.verticalCenter: parent.verticalCenter
                            width: altTagText.implicitWidth + Theme.px(14); height: Theme.px(20); radius: height / 2
                            color: "#fbf1dc"
                            Text { id: altTagText; anchors.centerIn: parent; text: root.altTagText(wrap.s.altTag); font.family: serifFamily; font.pixelSize: Theme.px(11.5); font.bold: wrap.s.altTag === "rise"; color: "#7a5a1c" }
                        }
                    }
                    Column {
                        visible: root.elevEdit === wrap.index
                        width: parent.width
                        spacing: Theme.px(3)
                        Row {
                            spacing: Theme.px(6)
                            TpField {
                                id: elevField
                                objectName: "tripElevField"
                                width: Theme.px(90)
                                value: root.elevDraft
                                bad: root.elevErr !== ""
                                onEdited: (t) => root.elevDraft = t
                                onAccepted: root.saveElev()
                                onEscaped: root.cancelSchedule()
                                Connections {
                                    target: root
                                    function onElevEditChanged() { if (root.elevEdit === wrap.index) elevField.focusAll() }
                                }
                            }
                            TpButton { compact: true; kind: "primary"; text: qsTr("好"); onClicked: root.saveElev() }
                            TpButton { compact: true; kind: "ghost"; text: qsTr("取消"); onClicked: root.cancelSchedule() }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: root.elevErr !== "" ? root.elevErr : qsTr("海拔（米）。清空后联网时会重新自动取")
                            font.family: serifFamily; font.pixelSize: Theme.px(12); color: root.elevErr !== "" ? Theme.danger : Theme.muted
                        }
                    }
                    // 天气（16 天内）和往年同月气候
                    Row {
                        readonly property var w: ((weather.stops || [])[wrap.index] || {}).wx
                        objectName: "tripWx" + wrap.index
                        visible: !!w
                        width: parent.width
                        spacing: Theme.px(5)
                        TpIcon { width: Theme.px(15); height: width; name: parent.w ? Fmt.wxIcon(parent.w.kind) : "cloud"; color: Theme.muted }
                        Text {
                            objectName: "tripWxText"
                            width: parent.width - Theme.px(20)
                            wrapMode: Text.Wrap
                            textFormat: Text.StyledText
                            text: parent.w ? root.wxLine(parent.w) : ""
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                        }
                    }
                    Row {
                        readonly property var c: ((weather.stops || [])[wrap.index] || {}).clim
                        objectName: "tripClim" + wrap.index
                        visible: !!c
                        width: parent.width
                        spacing: Theme.px(5)
                        TpIcon { width: Theme.px(15); height: width; name: "plan"; color: Theme.muted }
                        Text {
                            objectName: "tripClimText"
                            width: parent.width - Theme.px(20)
                            wrapMode: Text.Wrap
                            textFormat: Text.StyledText
                            text: parent.c ? root.climLine(parent.c) : ""
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                        }
                    }
                    SchedList {
                        objectName: "tripSched" + wrap.index
                        width: parent.width
                        ctl: edit
                        stopIndex: wrap.index
                        days: wrap.s.days || []
                        canEdit: !root.readOnly
                        defaultDate: wrap.s.arriveDate || ""
                        saver: (j, d) => planPage.saveSched(wrap.index, j, d.date, d.time, d.end, d.text)
                        deleter: (j) => planPage.deleteSched(wrap.index, j)
                    }
                    Column {
                        visible: (wrap.s.notes || "") !== ""
                        width: parent.width
                        spacing: Theme.px(3)
                        readonly property bool open: root.openNotes[wrap.index] === true
                        LinkText {
                            text: (parent.open ? "▾ " : "▸ ") + qsTr("详细")
                            muted: true
                            small: true
                            onClicked: { const o = Object.assign({}, root.openNotes); o[wrap.index] = !parent.open; root.openNotes = o }
                        }
                        Text {
                            visible: parent.open
                            width: parent.width
                            wrapMode: Text.Wrap
                            lineHeight: 1.3
                            text: wrap.s.notes || ""
                            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
                        }
                    }
                    // 照片条（最多 12 张；滚轮上下滚改成左右翻，到头了才还给页面。点开看大图 0.10.5）
                    ListView {
                        id: strip
                        objectName: "tripPhotos"
                        readonly property var all: wrap.s.thumbs || []
                        visible: all.length > 0
                        width: parent.width
                        height: visible ? Theme.px(84) : 0
                        orientation: ListView.Horizontal
                        spacing: Theme.px(6)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: all.slice(0, 12)
                        delegate: Rectangle {
                            required property string modelData
                            required property int index
                            objectName: "tripPhoto-" + index
                            width: Theme.px(112); height: strip.height
                            radius: Theme.px(10)
                            color: Theme.panel2
                            clip: true
                            Image { anchors.fill: parent; source: modelData; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 320 }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.photoViewRequested((root.info.photos || []).filter(p => p.stopId === wrap.s.id && p.thumb), parent.index)
                            }
                        }
                        footer: Item {
                            width: strip.all.length > 12 ? Theme.px(52) : 0
                            height: strip.height
                            Text { anchors.centerIn: parent; visible: parent.width > 0; text: "+" + (strip.all.length - 12); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
                        }
                        WheelHandler {
                            orientation: Qt.Vertical
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: (e) => {
                                const max = strip.contentWidth - strip.width + strip.originX
                                const atEnd = e.angleDelta.y < 0 ? strip.contentX >= max - 1 : strip.contentX <= strip.originX
                                if (strip.contentWidth <= strip.width || atEnd) { e.accepted = false; return }
                                strip.contentX = Math.max(strip.originX, Math.min(max, strip.contentX - e.angleDelta.y / 2))
                            }
                        }
                    }
                }
            }
            // 到下一站（I1）：卡片之间的连接线
            Item {
                visible: wrap.hasLeg
                y: card.height
                width: parent.width
                height: Theme.px(38)
                Rectangle { x: card.x + Theme.px(24); y: 0; width: 2; height: parent.height; color: Theme.lineStrong }
                Text {
                    objectName: "tripLeg"
                    x: card.x + Theme.px(36)
                    width: parent.width - x - root.margin
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: wrap.legLine
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                }
            }
        }
    }

    // 切换条贴住时垫一层底，滚上去的内容不从它四周露出来
    Rectangle {
        visible: navBar.visible && root.navNatural < navBar.y - 0.5
        width: parent.width
        y: navBar.y - Theme.px(4)
        height: navBar.height + Theme.px(10)
        color: Qt.rgba(250 / 255, 252 / 255, 253 / 255, 0.99)
        z: 2
    }
    // ---- 站点切换条：贴在页头下面（Electron .stop-nav 吸顶）----
    Rectangle {
        id: navBar
        objectName: "tripNav"
        visible: root.stepping && !root.missing && root.tab === "itinerary"
        x: root.margin - Theme.px(6)
        width: parent.width - 2 * x
        y: Math.max(root.collapsed ? root.compactH : Theme.px(6), root.navNatural)
        height: root.navH - Theme.px(6)
        radius: Theme.radiusControl
        color: "#f2f5f8"
        border.width: 1; border.color: Theme.line
        z: 3
        MouseArea { anchors.fill: parent }
        RoundIcon {
            objectName: "tripPrev"
            x: Theme.px(5); anchors.verticalCenter: parent.verticalCenter
            kind: "prev"; small: true; label: qsTr("上一站（←）")
            enabled: root.focusIndex > 0
            onClicked: root.step(-1)
        }
        Column {
            anchors.centerIn: parent
            width: parent.width - Theme.px(80)
            Text {
                objectName: "tripNavLabel"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                textFormat: Text.StyledText
                text: root.focusIndex < 0
                      ? "<b>" + qsTr("共 %1 站").arg(root.stops.length) + "</b>　" + qsTr("点 › 从第 1 站开始")
                      : "<b>" + ((root.stops[root.focusIndex] || {}).name || qsTr("未命名地点")).replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</b>　" + qsTr("第 %1 / %2 站").arg(root.focusIndex + 1).arg(root.stops.length)
                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: if (root.focusIndex >= 0) root.reveal(root.focusIndex, true) }
            }
        }
        RoundIcon {
            objectName: "tripNext"
            anchors.right: parent.right; anchors.rightMargin: Theme.px(5); anchors.verticalCenter: parent.verticalCenter
            kind: "next"; small: true; label: qsTr("下一站（→）")
            enabled: root.focusIndex < root.stops.length - 1
            onClicked: root.step(1)
        }
    }

    // ---- 收起后的页头（I4）----
    Rectangle {
        id: compact
        objectName: "tripCompact"
        width: parent.width
        height: root.compactH
        topLeftRadius: Theme.radiusLarge; topRightRadius: Theme.radiusLarge
        color: Qt.rgba(250 / 255, 252 / 255, 253 / 255, 0.99)
        opacity: root.collapsed && !root.missing ? 1 : 0
        visible: opacity > 0.01
        z: 4
        Behavior on opacity { enabled: Theme.motion; NumberAnimation { duration: 160 } }
        MouseArea { anchors.fill: parent }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
        Text {
            x: root.margin
            width: compactBtns.x - x - Theme.px(10)
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: root.info.name || qsTr("未命名计划")
            font.family: serifFamily; font.pixelSize: Theme.px(16); font.bold: true; color: Theme.text
        }
        Row {
            id: compactBtns
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(56)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(6)
            TpButton { compact: true; kind: "primary"; text: qsTr("编辑"); onClicked: root.editRequested(root.info.id) }
            TpButton { id: smallMore; compact: true; implicitWidth: Theme.px(40); text: "⋯"; Accessible.name: qsTr("更多"); onClicked: root.openMenu(smallMore) }
        }
    }
    RoundIcon {
        objectName: "tripClose"
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(12)
        y: Theme.px(8)
        z: 5
        kind: "close"
        plain: true
        label: qsTr("返回（Esc）")
        onClicked: root.closeRequested()
    }

    // ---- ⋯ 菜单（"在日历里看"在阶段 ⑤、"导出"在阶段 ⑧）----
    Rectangle {
        objectName: "tripMenu"
        visible: root.menuOpen
        anchors.right: parent.right
        anchors.rightMargin: root.margin
        y: root.menuY
        width: menuCol.implicitWidth + Theme.px(12)
        height: menuCol.implicitHeight + Theme.px(12)
        radius: Theme.radiusSmall
        color: "#fbfcfd"
        border.width: 1; border.color: Theme.line
        z: 6
        Column {
            id: menuCol
            x: Theme.px(6); y: Theme.px(6)
            MenuRow {
                objectName: "tripStart"
                visible: root.info.status === "wish"
                text: qsTr("开始计划")
                onClicked: { root.menuOpen = false; root.startRequested() }
            }
            MenuRow {
                objectName: "tripPin"
                text: root.info.pinned ? qsTr("取消置顶") : qsTr("置顶")
                onClicked: { root.menuOpen = false; root.togglePin(root.info.id) }
            }
            MenuRow {
                objectName: "tripNextTrip"
                visible: root.info.status !== "wish"
                text: root.info.isNext ? qsTr("取消下次出发") : qsTr("设为下次出发")
                onClicked: { root.menuOpen = false; root.toggleNext(root.info.id, !root.info.isNext) }
            }
        }
    }

    // ---- 计划被删掉（比如撤销了新建、前进 / 后退回到这里）----
    Column {
        visible: root.missing
        anchors.centerIn: parent
        spacing: Theme.px(14)
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("找不到这个计划"); font.family: serifFamily; font.pixelSize: Theme.px(20); font.bold: true; color: Theme.text }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("可能已经被删除了。"); font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.muted }
    }

    component Chip: Rectangle {
        id: chip
        property string text: ""
        property color tone: Theme.textSoft
        property bool plain: false
        property bool clickable: false
        signal clicked()
        width: chipText.implicitWidth + Theme.px(plain ? 4 : 16)
        height: Theme.px(22)
        radius: height / 2
        color: plain ? "transparent" : chipMa.containsMouse ? Theme.accentSoft : Theme.panel2
        Text { id: chipText; anchors.centerIn: parent; text: chip.text; font.family: serifFamily; font.pixelSize: Theme.px(12); color: chip.plain ? Theme.muted : chip.tone }
        MouseArea { id: chipMa; anchors.fill: parent; enabled: chip.clickable; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: chip.clicked() }
    }
}
