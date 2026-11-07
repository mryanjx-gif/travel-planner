// 站点编辑器（0.8.9，M-23；PO 选方案 B：计划信息收成一张卡片、站点排成时间线；保存方案 B：边改边存）。
// 数据和逻辑在 C++ 的 editor（src/app/planeditor.*），这里只是界面。字段照 Electron v6.3.0 renderEditor / renderEditorStops：
//   计划：名称、类型、状态、区域（自动建议）、开始 / 结束日期、地图标记样式（+ 路线标记：数字 / 图标 / 圆点）、备注；
//   站点：名称、到达 / 离开、备注、坐标、样式（路线）、去下一站的交通方式（自驾 / 徒步自动按道路算）；
//   加站点：搜地名（结果同时标在地图上，方案 B）、在地图上点选（改进 a 的预览在 FlatMap）、手动输入坐标。
// PO 0.8.9 选的范围 B：照片、封面当时不在这里改。0.10.0 起可以改行程安排（SchedList，和计划页同一个组件）；0.10.5 起能改站点的照片和封面（对话框、导入、看大图在 Main.qml）。
// 改进 e：顶上一行总里程；改进 d：站点可以拖住把手排序（保留 ↑ ↓）；改进 g：新建时加到第二站自动改成路线（提示一句）。
// drawer = 窄窗口（≤ 768）时变成底部抽屉（PO 选方案 B）：顶上多一个把手，Main.qml 管高度。
import QtQuick
import TPApp

Item {
    id: root
    QtObject { id: schedEdit; property int stop: -1; property int j: -2; property var draft: ({}); property string err: "" }
    readonly property bool scheduleEditing: schedEdit.j !== -2
    function cancelSchedule() { schedEdit.j = -2 }
    property bool drawer: false
    property bool infoOpen: false
    property var expanded: ({})          // 站点 id → 展开着
    property var moreOpen: ({})          // 站点 id → "坐标和样式"展开着
    signal finishRequested()
    signal discardRequested()
    signal deletePlanRequested()
    signal deleteStopRequested(int index, string name)
    signal styleRequested(Item anchor, var opts)
    signal drawerDrag(real dy, bool done)
    signal drawerToggle()
    signal coverPickRequested(var rows)
    signal photoAddRequested(string stopId)
    signal photoCaptionRequested(var photo)
    signal photoRemoveRequested(var photo)
    signal photoViewRequested(var list, int index)
    // 计划存进数据以后才能加照片（新计划第一次保存之前，导入时找不到它）
    readonly property bool canPhoto: !editor.isNew || editor.saveState === "saved" || editor.saveState === "pending"
    readonly property var photoRows: { photos.revision; return editor.open ? photos.planPhotos(editor.planId) : [] }

    readonly property var plan: editor.plan
    readonly property var stops: plan.stops || []
    readonly property bool route: plan.type === "route"
    readonly property var problems: editor.problems
    readonly property var stats: editor.stats

    function statusLabel(s) { return s === "completed" ? qsTr("已完成") : s === "wish" ? qsTr("想去") : qsTr("计划中") }
    function shapeLabel(s) { return s === "square" ? qsTr("方形") : s === "pin" ? qsTr("水滴") : s === "diamond" ? qsTr("菱形") : qsTr("圆形") }
    function legLabel(m) { return m === "train" ? qsTr("火车") : m === "ferry" ? qsTr("船") : m === "drive" ? qsTr("自驾") : m === "walk" ? qsTr("徒步") : qsTr("飞机") }
    function setOpen(map, id, on) { const n = Object.assign({}, map); if (on) n[id] = true; else delete n[id]; return n }
    function isOpen(i) { const s = stops[i]; return s ? (expanded[s.id] === true || editor.activeStop === i) : false }
    function toggleStop(i) {
        const s = stops[i]; if (!s) return
        const on = !isOpen(i)
        expanded = setOpen(expanded, s.id, on)
        editor.activeStop = on ? i : (editor.activeStop === i ? -1 : editor.activeStop)
    }
    function summary() {
        const bits = [route ? qsTr("路线") : qsTr("单一目的地"), statusLabel(plan.status)]
        const d = Fmt.range(plan.startDate, plan.endDate)
        bits.push(d !== "" ? d : qsTr("未定日期"))
        const regs = (plan.regions || []).map(r => qsTr(r))
        if (regs.length) bits.push(regs.join("、"))
        return bits.join(" · ")
    }
    function stopSummary(s) {
        const bits = []
        if (s.arriveDate || s.departDate) bits.push(Fmt.range(s.arriveDate, s.departDate))
        const c = s.counts || {}
        if (c.schedule) bits.push(qsTr("行程 %1 条").arg(c.schedule))
        if (c.photos) bits.push(qsTr("照片 %1").arg(c.photos))
        if (s.notes) bits.push(qsTr("有备注"))
        return bits.length ? bits.join(" · ") : qsTr("点开填写日期、备注")
    }
    function legText(l) {
        if (!l) return ""
        if (l.state === "line") return qsTr("直线") + (l.km > 0 ? " · " + Fmt.dist(l.km * 1000) : "")
        if (l.state === "bad") return qsTr("坐标无效，按直线显示")
        if (l.state === "road") return qsTr("沿道路") + (l.dist ? " · " + Fmt.dist(l.dist) : "") + (l.time ? " · " + qsTr("约 %1").arg(Fmt.dur(l.time)) : "")
        if (l.state === "loading") return qsTr("正在按道路计算…")
        if (l.state === "fail") return "⚠ " + qsTr("%1，暂按直线显示").arg(l.msg || qsTr("没算出来"))
        return qsTr("等待计算…（暂按直线显示）")
    }
    function statsText() {
        const st = stats
        if (!st || !st.stops) return ""
        const bits = [qsTr("%1 站").arg(st.stops)]
        if (st.route && st.stops > 1) {
            bits.push(st.approx ? qsTr("约 %1").arg(Fmt.dist(st.km * 1000)) : Fmt.dist(st.km * 1000))
            if (st.hasDrive && st.driveSec > 0) bits.push((st.driveApprox ? qsTr("自驾至少 %1") : qsTr("自驾约 %1")).arg(Fmt.dur(st.driveSec)))
            if (st.hasWalk && st.walkSec > 0) bits.push((st.walkApprox ? qsTr("徒步至少 %1") : qsTr("徒步约 %1")).arg(Fmt.dur(st.walkSec)))
        }
        return bits.join(" · ")
    }
    function containsGlobal(gx, gy) {
        const p = root.mapFromItem(null, gx, gy)
        return visible && p.x >= 0 && p.y >= 0 && p.x <= width && p.y <= height
    }
    // 打开时：新建的计划展开信息卡片、名称框拿到焦点
    Connections {
        target: editor
        function onOpenChanged() {
            if (!editor.open) return
            root.infoOpen = editor.isNew
            root.expanded = ({}); root.moreOpen = ({})
            body.contentY = 0
            if (editor.isNew) Qt.callLater(() => nameField.focusAll())
        }
        function onActiveStopChanged() {
            const i = editor.activeStop
            if (i < 0 || i >= root.stops.length) return
            Qt.callLater(() => { const it = stopRep.itemAt(i); if (it) body.ensureVisible(it) })
        }
    }

    GlassPanel { anchors.fill: parent; radius: root.drawer ? Theme.radius : Theme.radiusLarge }
    MouseArea { anchors.fill: parent; onWheel: (w) => w.accepted = true }

    // ---- 抽屉的把手（窄窗口） ----
    Item {
        id: grab
        visible: root.drawer
        width: parent.width; height: visible ? Theme.px(18) : 0
        Rectangle { anchors.centerIn: parent; width: Theme.px(40); height: Theme.px(4); radius: 2; color: Theme.faint }
        DragHandler {
            target: null
            yAxis.enabled: true; xAxis.enabled: false
            property real startY: 0
            onActiveChanged: if (active) startY = centroid.scenePosition.y; else root.drawerDrag(centroid.scenePosition.y - startY, true)
            onCentroidChanged: if (active) root.drawerDrag(centroid.scenePosition.y - startY, false)
        }
        TapHandler { onDoubleTapped: root.drawerToggle() }
    }

    // ---- 头部：计划名 + ✕ ----
    Item {
        id: head
        x: Theme.px(18); y: grab.height + Theme.px(root.drawer ? 4 : 16)
        width: parent.width - Theme.px(30)
        height: Theme.px(40)
        TpField {
            id: nameField
            objectName: "edName"
            width: parent.width - closeBtn.width - Theme.px(8)
            height: parent.height
            fontSize: 18; bold: true
            value: root.plan.name || ""
            placeholder: qsTr("计划名称，例如：京都赏枫")
            bad: !!root.problems.name
            onEdited: (t) => editor.setField("name", t)
        }
        Rectangle {
            id: closeBtn
            objectName: "edClose"
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: Theme.px(34); height: width; radius: width / 2
            color: cbMa.containsMouse ? Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.06) : "transparent"
            TpIcon { anchors.centerIn: parent; name: "close" }
            TpTip { text: qsTr("完成"); show: cbMa.containsMouse }
            MouseArea { id: cbMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.finishRequested() }
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("完成")
        }
    }
    Text {
        id: nameErr
        x: head.x + Theme.px(4); y: head.y + head.height + Theme.px(2)
        visible: !!root.problems.name
        text: root.problems.name || ""
        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.danger
    }

    // ---- 可以滚动的正文 ----
    Flickable {
        id: body
        x: Theme.px(14)
        y: nameErr.y + (nameErr.visible ? nameErr.implicitHeight : 0) + Theme.px(8)
        width: parent.width - Theme.px(28)
        height: foot.y - y - Theme.px(6)
        contentHeight: content.implicitHeight + Theme.px(10)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        function ensureVisible(item) {
            const p = item.mapToItem(content, 0, 0)
            if (p.y < contentY) contentY = Math.max(0, p.y - Theme.px(8))
            else if (p.y + Math.min(item.height, height) > contentY + height) contentY = Math.min(contentHeight - height, p.y + Math.min(item.height, height) - height + Theme.px(8))
        }
        Column {
            id: content
            width: body.width
            spacing: Theme.px(10)

            // ---- 计划信息卡片：收起时一行摘要，点"改"展开 ----
            TpCard {
                id: infoCard
                objectName: "edInfo"
                width: parent.width
                height: infoCol.implicitHeight + Theme.px(18)
                radius: Theme.radiusSmall
                color: root.infoOpen ? Qt.rgba(1, 1, 1, 0.6) : Theme.accentSoft
                Column {
                    id: infoCol
                    x: Theme.px(10); y: Theme.px(9)
                    width: parent.width - Theme.px(20)
                    spacing: Theme.px(10)
                    Item {
                        width: parent.width; height: Math.max(sumText.implicitHeight, Theme.px(22))
                        MarkerShape { id: sumMk; color: root.plan.effColor || "#e8590c"; shape: root.plan.effShape || "circle"; icon: root.plan.icon || ""; size: Theme.px(18); anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            id: sumText
                            x: sumMk.width + Theme.px(8)
                            width: parent.width - x - toggleText.width - Theme.px(8)
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.summary()
                            wrapMode: Text.Wrap
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                        }
                        Text {
                            id: toggleText
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            text: root.infoOpen ? qsTr("收起 ▴") : qsTr("改 ▾")
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.accentInk
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.infoOpen = !root.infoOpen }
                    }
                    // 展开后的表单
                    Column {
                        visible: root.infoOpen
                        width: parent.width
                        spacing: Theme.px(10)
                        Label { text: qsTr("类型") }
                        Segmented {
                            objectName: "edType"
                            width: parent.width; fill: true
                            options: [{ key: "single", text: qsTr("单一目的地") }, { key: "route", text: qsTr("路线（多站点）") }]
                            current: root.plan.type || "single"
                            onPicked: (k) => editor.setField("type", k)
                        }
                        Text {
                            visible: editor.typeNote !== ""
                            width: parent.width; wrapMode: Text.Wrap
                            text: editor.typeNote
                            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.accentInk
                        }
                        Label { text: qsTr("状态") }
                        Segmented {
                            width: parent.width; fill: true
                            options: [{ key: "planning", text: qsTr("计划中") }, { key: "wish", text: qsTr("想去") }, { key: "completed", text: qsTr("已完成") }]
                            current: root.plan.status || "planning"
                            onPicked: (k) => editor.setField("status", k)
                        }
                        Label { text: qsTr("区域（可多选；跨区域的计划把涉及的都选上）") }
                        Flow {
                            width: parent.width; spacing: Theme.px(5)
                            Repeater {
                                model: plans.regions.filter(r => r !== "未分类")
                                delegate: Chip { required property var modelData; text: qsTr(modelData); on: (root.plan.regions || []).indexOf(modelData) >= 0; onClicked: editor.toggleRegion(modelData) }
                            }
                        }
                        Row {
                            width: parent.width; spacing: Theme.px(8)
                            visible: hint.text !== ""
                            Text {
                                id: hint
                                width: Math.min(implicitWidth, parent.width - applyLink.width - Theme.px(8))
                                wrapMode: Text.Wrap
                                text: root.plan.autoRegion ? ((root.plan.regionSuggest || []).length ? qsTr("已按站点位置自动选择，可手动调整") : qsTr("添加站点后会按位置自动建议"))
                                    : (!root.plan.regionSame && (root.plan.regionSuggest || []).length ? qsTr("按站点位置建议：%1").arg(root.plan.regionSuggest.map(r => qsTr(r)).join(qsTr("、"))) : "")
                                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                            }
                            LinkText { id: applyLink; visible: !root.plan.autoRegion && !root.plan.regionSame && (root.plan.regionSuggest || []).length > 0; text: qsTr("套用"); onClicked: editor.applyRegionSuggestion() }
                        }
                        Row {
                            width: parent.width; spacing: Theme.px(8)
                            Column {
                                width: (parent.width - Theme.px(8)) / 2; spacing: Theme.px(4)
                                Label { text: qsTr("开始日期") }
                                DateField { width: parent.width; value: root.plan.startDate || ""; bad: !!root.problems.date; onEdited: (v) => editor.setField("startDate", v) }
                            }
                            Column {
                                width: (parent.width - Theme.px(8)) / 2; spacing: Theme.px(4)
                                Label { text: qsTr("结束日期") }
                                DateField { width: parent.width; value: root.plan.endDate || ""; minDate: root.plan.startDate || ""; bad: !!root.problems.date; onEdited: (v) => editor.setField("endDate", v) }
                            }
                        }
                        Err { text: root.problems.date || "" }
                        Label { text: qsTr("地图标记（颜色、外形、图标；路线的站点可在站点的「坐标和样式」里单独改）") }
                        Flow {
                            width: parent.width; spacing: Theme.px(8)
                            MarkerShape { color: root.plan.effColor || "#e8590c"; shape: root.plan.effShape || "circle"; icon: root.plan.icon || ""; size: Theme.px(24) }
                            TpButton {
                                id: planStyleBtn
                                objectName: "edPlanStyle"
                                compact: true; text: qsTr("更改样式…")
                                onClicked: root.styleRequested(planStyleBtn, {
                                    title: qsTr("计划的地图标记"),
                                    value: { color: root.plan.color || "", shape: root.plan.shape || "", icon: root.plan.icon || "" },
                                    defaults: { color: editor.statusColor(root.plan.status), shape: root.route ? "circle" : "pin", icon: "" },
                                    inheritLabel: qsTr("恢复默认"),
                                    onApply: (v) => editor.setPlanStyle(v.color, v.shape, v.icon)
                                })
                            }
                            Segmented {
                                visible: root.route
                                options: [{ key: "number", text: qsTr("数字顺序") }, { key: "icon", text: qsTr("图标") }, { key: "dot", text: qsTr("圆点") }]
                                current: root.plan.markerMode || "number"
                                onPicked: (k) => editor.setField("markerMode", k)
                            }
                        }
                        Text {
                            visible: (root.plan.styleNote || "") !== ""
                            width: parent.width; wrapMode: Text.Wrap
                            text: root.plan.styleNote || ""
                            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.warn
                        }
                        Label { text: qsTr("备注") }
                        TpField { width: parent.width; multiline: true; value: root.plan.notes || ""; placeholder: qsTr("任何要记的事"); onEdited: (t) => editor.setField("notes", t) }
                        // 封面（0.10.5）：从这个计划的照片里挑，或从电脑选一张（导入进这个计划）
                        Label { text: qsTr("封面照片") }
                        Row {
                            spacing: Theme.px(8)
                            Rectangle {
                                objectName: "edCover"
                                visible: !!root.plan.cover
                                width: visible ? Theme.px(96) : 0; height: Theme.px(64)
                                radius: Theme.px(8)
                                color: Theme.panel2
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    source: root.plan.cover && photos.fileOf(root.plan.cover) ? photos.thumbUrl(photos.fileOf(root.plan.cover), 320) : ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.px(4)
                                TpButton { objectName: "edCoverPick"; visible: root.canPhoto; compact: true; text: root.plan.cover ? qsTr("换一张") : qsTr("选择封面照片"); onClicked: root.coverPickRequested(root.photoRows) }
                                TpButton { objectName: "edCoverClear"; visible: !!root.plan.cover; compact: true; kind: "ghost"; text: qsTr("去掉封面"); onClicked: editor.setField("cover", "") }
                            }
                            Text {
                                visible: !root.canPhoto
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("先填好名称和第一站，才能加照片")
                                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                            }
                        }
                        TpButton {
                            visible: !editor.isNew || editor.saveState === "saved" || editor.saveState === "pending"
                            objectName: "edDeletePlan"
                            compact: true; kind: "dangerOutline"; text: qsTr("删除计划")
                            onClicked: root.deletePlanRequested()
                        }
                    }
                }
            }

            // ---- 总里程（改进 e） ----
            Text {
                objectName: "edStats"
                visible: text !== ""
                width: parent.width
                text: root.statsText()
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
            }

            // ---- 加站点：搜地名 / 点选 / 坐标 ----
            Row {
                width: parent.width
                spacing: Theme.px(6)
                TpField {
                    id: searchField
                    objectName: "edSearch"
                    width: parent.width - pickBtn.width - coordBtn.width - Theme.px(12)
                    placeholder: qsTr("搜索地名添加，例如 Kyoto、Eiffel Tower")
                    onAccepted: editor.search(text)
                    onEdited: (t) => { if (t.trim() === "") editor.clearSearch() }
                    onEscaped: editor.clearSearch()
                }
                ToolBtn { id: pickBtn; objectName: "edPick"; icon: "pick"; text: qsTr("点选"); on: editor.pickMode; tip: editor.pickMode ? qsTr("点选中…（点此退出）") : qsTr("在地图上点选"); onClicked: editor.pickMode = !editor.pickMode }
                ToolBtn {
                    id: coordBtn; objectName: "edCoord"; icon: "coord"; text: qsTr("坐标"); tip: qsTr("手动输入坐标")
                    onClicked: {
                        const i = editor.addStopAtCenter()
                        if (i < 0) return
                        const s = root.stops[i]
                        if (s) root.moreOpen = root.setOpen(root.moreOpen, s.id, true)
                        Qt.callLater(() => { const it = stopRep.itemAt(i); if (it) it.focusName() })
                    }
                }
            }
            // 搜索结果（方案 B：带编号，地图上同时标出来；鼠标停在哪一行，地图上那个点变大）
            TpCard {
                objectName: "edResults"
                visible: editor.searchState !== "idle"
                width: parent.width
                height: resCol.implicitHeight + Theme.px(10)
                radius: Theme.radiusSmall
                color: Theme.card
                Column {
                    id: resCol
                    x: Theme.px(5); y: Theme.px(5)
                    width: parent.width - Theme.px(10)
                    Text {
                        visible: editor.searchState === "busy" || editor.searchState === "empty"
                        x: Theme.px(6); height: Theme.px(28); verticalAlignment: Text.AlignVCenter
                        text: editor.searchState === "busy" ? qsTr("搜索中…") : qsTr("没有结果")
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                    }
                    Text {
                        visible: editor.searchState === "error"
                        x: Theme.px(6); width: parent.width - Theme.px(12)
                        wrapMode: Text.Wrap
                        topPadding: Theme.px(4); bottomPadding: Theme.px(4)
                        text: qsTr("搜不到：%1。可以改用「点选」在地图上点一下，或者「坐标」手动输入。").arg(editor.searchError)
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
                    }
                    LinkText {
                        objectName: "edSearchRetry"
                        visible: editor.searchState === "error"
                        x: Theme.px(6)
                        small: true
                        text: qsTr("重试")
                        onClicked: editor.search(searchField.text)
                    }
                    Repeater {
                        model: editor.searchState === "done" ? editor.results : []
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: resCol.width
                            height: Math.max(Theme.px(40), rcol.implicitHeight + Theme.px(8))
                            radius: Theme.px(7)
                            color: editor.hoverResult === index ? Theme.accentSoft : "transparent"
                            Rectangle {
                                x: Theme.px(6); y: Theme.px(8)
                                width: Theme.px(18); height: width; radius: width / 2; color: Theme.accent
                                Text { anchors.centerIn: parent; text: index + 1; font.pixelSize: Theme.px(10.5); font.bold: true; color: "#ffffff" }
                            }
                            Column {
                                id: rcol
                                x: Theme.px(32); y: Theme.px(4)
                                width: parent.width - x - Theme.px(6)
                                Text { width: parent.width; elide: Text.ElideRight; text: modelData.name; font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text }
                                Text { width: parent.width; elide: Text.ElideRight; text: modelData.address; font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted }
                            }
                            MouseArea {
                                objectName: "edResultHit-" + index
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onContainsMouseChanged: editor.hoverResult = containsMouse ? index : (editor.hoverResult === index ? -1 : editor.hoverResult)
                                onClicked: { const f = searchField; editor.addResult(index); f.clear() }   // addResult 之后这一行已被销毁
                            }
                        }
                    }
                    // Nominatim 条款：署名（ODbL）
                    Text {
                        visible: editor.searchState === "done"
                        width: parent.width - Theme.px(6)
                        horizontalAlignment: Text.AlignRight
                        topPadding: Theme.px(2)
                        text: qsTr("地名数据 © OpenStreetMap 贡献者")
                        font.family: serifFamily; font.pixelSize: Theme.px(10.5); color: Theme.muted
                    }
                }
            }

            // ---- 站点时间线 ----
            Text {
                visible: root.stops.length === 0
                width: parent.width; wrapMode: Text.Wrap
                text: qsTr("还没有站点。用上面的搜索、地图点选或手动输入来添加。")
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
            }
            Err { text: (root.problems.stops || "") + (root.problems.coords ? (root.problems.stops ? "\n" : "") + root.problems.coords : "") }
            Item {
                id: timeline
                width: parent.width
                height: tlCol.implicitHeight
                property int dragFrom: -1
                property int dragTo: -1
                Rectangle {   // 时间线的竖线
                    visible: root.stops.length > 1 && root.route
                    x: Theme.px(17); y: Theme.px(18)
                    width: 2; height: Math.max(0, parent.height - Theme.px(36))
                    color: Theme.line
                }
                Column {
                    id: tlCol
                    width: parent.width
                    spacing: Theme.px(4)
                    Repeater {
                        id: stopRep
                        // 只按站点个数建行：内容每打一个字都会刷新，整份换掉会把行重建、输入框丢焦点
                        model: root.stops.length
                        delegate: Column {
                            id: stopItem
                            required property int index
                            readonly property var modelData: root.stops[index] || ({})
                            readonly property bool open: root.isOpen(index)
                            readonly property bool active: editor.activeStop === index
                            width: tlCol.width
                            spacing: Theme.px(4)
                            z: timeline.dragFrom === index ? 10 : 0
                            function focusName() { nameIn.focusAll() }
                            // 拖动排序时：被拖的这张跟着鼠标走，其余让位
                            transform: Translate {
                                y: timeline.dragFrom === stopItem.index ? gripDrag.dy
                                   : (timeline.dragFrom >= 0 && timeline.dragTo >= 0)
                                     ? (stopItem.index > timeline.dragFrom && stopItem.index <= timeline.dragTo ? -gripDrag.rowH
                                        : stopItem.index < timeline.dragFrom && stopItem.index >= timeline.dragTo ? gripDrag.rowH : 0) : 0
                            }
                            Rectangle {
                                id: card
                                width: parent.width
                                height: cardCol.implicitHeight + Theme.px(12)
                                radius: Theme.radiusSmall
                                color: "#ffffff"
                                border.width: stopItem.active || stopItem.modelData.bad ? 2 : 1
                                border.color: stopItem.modelData.bad ? Theme.danger : stopItem.active ? Theme.accent : Theme.line
                                Column {
                                    id: cardCol
                                    x: Theme.px(8); y: Theme.px(6)
                                    width: parent.width - Theme.px(16)
                                    spacing: Theme.px(8)
                                    Item {
                                        width: parent.width; height: Math.max(Theme.px(32), headCol.implicitHeight)
                                        Rectangle {
                                            id: badge
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Theme.px(22); height: width; radius: width / 2
                                            color: stopItem.modelData.effColor
                                            Text { anchors.centerIn: parent; text: root.route ? stopItem.modelData.num : "•"; font.pixelSize: Theme.px(11); font.bold: true; color: "#ffffff" }
                                        }
                                        Column {
                                            id: headCol
                                            x: badge.width + Theme.px(8)
                                            width: parent.width - x - tools.width - Theme.px(4)
                                            anchors.verticalCenter: parent.verticalCenter
                                            Text { width: parent.width; elide: Text.ElideRight; text: stopItem.modelData.name || qsTr("未命名地点"); font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: stopItem.modelData.name ? Theme.text : Theme.muted }
                                            Text { width: parent.width; elide: Text.ElideRight; text: root.stopSummary(stopItem.modelData); font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted }
                                        }
                                        MouseArea { x: 0; width: tools.x; height: parent.height; cursorShape: Qt.PointingHandCursor; onClicked: root.toggleStop(stopItem.index) }
                                        Row {
                                            id: tools
                                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                            spacing: Theme.px(2)
                                            MiniBtn { icon: "locate"; tip: qsTr("在地图上定位"); onClicked: editor.flyToStop(stopItem.index) }
                                            MiniBtn { icon: "up"; tip: qsTr("上移"); enabled: stopItem.index > 0; onClicked: editor.moveStop(stopItem.index, stopItem.index - 1) }
                                            MiniBtn { icon: "down"; tip: qsTr("下移"); enabled: stopItem.index < root.stops.length - 1; onClicked: editor.moveStop(stopItem.index, stopItem.index + 1) }
                                            MiniBtn { icon: "trash"; tip: qsTr("删除站点"); danger: true; onClicked: root.deleteStopRequested(stopItem.index, stopItem.modelData.name) }
                                            // 改进 d：拖住把手上下排序
                                            Item {
                                                width: Theme.px(22); height: Theme.px(28)
                                                visible: root.stops.length > 1
                                                TpIcon { anchors.centerIn: parent; name: "grip"; color: Theme.faint; width: Theme.px(16); height: width }
                                                HoverHandler { cursorShape: Qt.SizeVerCursor }
                                                DragHandler {
                                                    id: gripDrag
                                                    target: null
                                                    xAxis.enabled: false
                                                    property real dy: 0
                                                    property real rowH: stopItem.height + tlCol.spacing
                                                    property real startY: 0
                                                    onActiveChanged: {
                                                        if (active) { startY = centroid.scenePosition.y; dy = 0; timeline.dragFrom = stopItem.index; timeline.dragTo = stopItem.index }
                                                        else {
                                                            const f = timeline.dragFrom, t = timeline.dragTo
                                                            timeline.dragFrom = -1; timeline.dragTo = -1; dy = 0
                                                            if (f >= 0 && t >= 0 && f !== t) editor.moveStop(f, t)
                                                        }
                                                    }
                                                    onCentroidChanged: {
                                                        if (!active) return
                                                        dy = centroid.scenePosition.y - startY
                                                        timeline.dragTo = Math.max(0, Math.min(root.stops.length - 1, stopItem.index + Math.round(dy / rowH)))
                                                    }
                                                }
                                            }
                                            MiniBtn { icon: stopItem.open ? "up" : "down"; tip: qsTr("展开 / 收起"); onClicked: root.toggleStop(stopItem.index) }
                                        }
                                    }
                                    // 展开后：名称、到达 / 离开、备注、坐标和样式
                                    Column {
                                        visible: stopItem.open
                                        width: parent.width
                                        spacing: Theme.px(8)
                                        Label { text: qsTr("地点名称") }
                                        TpField { id: nameIn; width: parent.width; value: stopItem.modelData.name; placeholder: qsTr("地点名称"); onEdited: (t) => editor.setStopField(stopItem.index, "name", t) }
                                        Row {
                                            width: parent.width; spacing: Theme.px(8)
                                            Column {
                                                width: (parent.width - Theme.px(8)) / 2; spacing: Theme.px(4)
                                                Label { text: qsTr("到达") }
                                                DateField { width: parent.width; value: stopItem.modelData.arriveDate; minDate: root.plan.startDate || ""; onEdited: (v) => editor.setStopField(stopItem.index, "arriveDate", v) }
                                            }
                                            Column {
                                                width: (parent.width - Theme.px(8)) / 2; spacing: Theme.px(4)
                                                Label { text: qsTr("离开") }
                                                DateField { width: parent.width; value: stopItem.modelData.departDate; minDate: stopItem.modelData.arriveDate || root.plan.startDate || ""; onEdited: (v) => editor.setStopField(stopItem.index, "departDate", v) }
                                            }
                                        }
                                        Label { text: qsTr("备注") }
                                        TpField { width: parent.width; multiline: true; value: stopItem.modelData.notes; placeholder: qsTr("交通、门票、注意事项……"); onEdited: (t) => editor.setStopField(stopItem.index, "notes", t) }
                                        Label { text: qsTr("行程安排") }
                                        SchedList {
                                            objectName: "editorSched" + stopItem.index
                                            width: parent.width
                                            ctl: schedEdit
                                            stopIndex: stopItem.index
                                            days: stopItem.modelData.days || []
                                            defaultDate: stopItem.modelData.arriveDate || ""
                                            saver: (j, d) => editor.saveSched(stopItem.index, j, d.date, d.time, d.end, d.text)
                                            deleter: (j) => editor.deleteSched(stopItem.index, j)
                                        }
                                        // 照片（0.10.5）：缩略图、写说明、移除、添加。站点有效（已保存）才能加
                                        Column {
                                            id: stopPhotos
                                            objectName: "edStopPhotos-" + stopItem.index
                                            readonly property var rows: root.photoRows.filter(p => p.stopId === stopItem.modelData.id)
                                            width: parent.width
                                            spacing: Theme.px(4)
                                            Item {
                                                width: parent.width; height: Theme.px(26)
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: stopPhotos.rows.length > 0 ? Fmt.one(qsTr("照片 %1 张").arg(stopPhotos.rows.length)) : qsTr("照片")
                                                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                                                }
                                                TpButton {
                                                    objectName: "edPhotoAdd"
                                                    visible: root.canPhoto && !stopItem.modelData.bad
                                                    anchors.right: parent.right
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    compact: true; kind: "ghost"; text: qsTr("添加照片")
                                                    onClicked: root.photoAddRequested(stopItem.modelData.id)
                                                }
                                            }
                                            Grid {
                                                visible: stopPhotos.rows.length > 0
                                                width: parent.width
                                                columns: 3
                                                spacing: Theme.px(6)
                                                Repeater {
                                                    model: stopPhotos.rows
                                                    delegate: Rectangle {
                                                        id: edThumb
                                                        required property var modelData
                                                        required property int index
                                                        objectName: "edPhoto-" + index
                                                        width: (parent.width - 2 * Theme.px(6)) / 3
                                                        height: width * 0.72
                                                        radius: Theme.px(8)
                                                        color: Theme.panel2
                                                        clip: true
                                                        Image { anchors.fill: parent; source: edThumb.modelData.thumb || ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 320 }
                                                        MouseArea {
                                                            anchors.fill: parent
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: root.photoViewRequested(stopPhotos.rows, edThumb.index)
                                                        }
                                                        Row {
                                                            anchors.right: parent.right
                                                            anchors.bottom: parent.bottom
                                                            anchors.margins: Theme.px(3)
                                                            spacing: Theme.px(3)
                                                            EdPhotoAct { objectName: "edPhotoCaption"; icon: "edit"; label: qsTr("写说明"); onClicked: root.photoCaptionRequested(edThumb.modelData) }
                                                            EdPhotoAct { objectName: "edPhotoRemove"; icon: "trash"; label: qsTr("从计划里移除"); onClicked: root.photoRemoveRequested(edThumb.modelData) }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        // 坐标和样式（Electron details.ed-more）
                                        Item {
                                            width: parent.width; height: Theme.px(24)
                                            readonly property bool on: root.moreOpen[stopItem.modelData.id] === true || stopItem.modelData.bad
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: (parent.on ? "▾ " : "▸ ") + (root.route ? qsTr("坐标和样式") : qsTr("坐标"))
                                                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.accentInk
                                            }
                                            Text {
                                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                                text: Number(stopItem.modelData.lat).toFixed(4) + ", " + Number(stopItem.modelData.lng).toFixed(4)
                                                font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
                                            }
                                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.moreOpen = root.setOpen(root.moreOpen, stopItem.modelData.id, !parent.on) }
                                        }
                                        Column {
                                            visible: root.moreOpen[stopItem.modelData.id] === true || stopItem.modelData.bad
                                            width: parent.width
                                            spacing: Theme.px(8)
                                            Row {
                                                spacing: Theme.px(6)
                                                Text { text: qsTr("纬度"); anchors.verticalCenter: parent.verticalCenter; font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted }
                                                TpField { id: latIn; width: Theme.px(110); value: String(stopItem.modelData.lat); bad: stopItem.modelData.bad; onAccepted: editor.setStopCoord(stopItem.index, latIn.text, lngIn.text); onEditingChanged: if (!editing) editor.setStopCoord(stopItem.index, latIn.text, lngIn.text) }
                                                Text { text: qsTr("经度"); anchors.verticalCenter: parent.verticalCenter; font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted }
                                                TpField { id: lngIn; width: Theme.px(110); value: String(stopItem.modelData.lng); bad: stopItem.modelData.bad; onAccepted: editor.setStopCoord(stopItem.index, latIn.text, lngIn.text); onEditingChanged: if (!editing) editor.setStopCoord(stopItem.index, latIn.text, lngIn.text) }
                                            }
                                            Label { visible: root.route; text: qsTr("样式（颜色也决定从这一站出发那段线的颜色；不设置就跟随计划）") }
                                            Flow {
                                                visible: root.route
                                                width: parent.width; spacing: Theme.px(8)
                                                MarkerShape { color: stopItem.modelData.effColor; shape: stopItem.modelData.effShape; icon: stopItem.modelData.effIcon; text: stopItem.modelData.effIcon ? "" : String(stopItem.modelData.num); size: Theme.px(24) }
                                                TpButton {
                                                    id: stopStyleBtn
                                                    compact: true; text: qsTr("更改样式…")
                                                    onClicked: root.styleRequested(stopStyleBtn, {
                                                        title: qsTr("第 %1 站的地图标记").arg(stopItem.modelData.num),
                                                        value: { color: stopItem.modelData.color, shape: stopItem.modelData.shape, icon: stopItem.modelData.icon },
                                                        defaults: { color: root.plan.effColor, shape: root.plan.shape || "circle", icon: root.plan.icon || "" },
                                                        inheritLabel: qsTr("跟随计划"),
                                                        onApply: (v) => editor.setStopStyle(stopItem.index, v.color, v.shape, v.icon)
                                                    })
                                                }
                                                Text { height: Theme.px(32); verticalAlignment: Text.AlignVCenter; text: stopItem.modelData.own ? qsTr("已单独设置") : qsTr("跟随计划"); font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted }
                                                LinkText { visible: stopItem.modelData.own; height: Theme.px(32); verticalAlignment: Text.AlignVCenter; text: qsTr("全部跟随计划"); onClicked: editor.resetStopStyle(stopItem.index) }
                                            }
                                        }
                                    }
                                }
                            }
                            // 去下一站：交通方式 + 状态（自驾 / 徒步自动按道路算）
                            Row {
                                visible: !!stopItem.modelData.leg
                                x: Theme.px(28)
                                width: parent.width - x
                                spacing: Theme.px(8)
                                TpSelect {
                                    width: Theme.px(92)
                                    options: [{ key: "flight", text: qsTr("飞机") }, { key: "train", text: qsTr("火车") }, { key: "ferry", text: qsTr("船") },
                                              { key: "drive", text: qsTr("自驾") }, { key: "walk", text: qsTr("徒步") }]
                                    current: stopItem.modelData.leg ? stopItem.modelData.leg.mode : "flight"
                                    onPicked: (k) => editor.setLegMode(stopItem.index, k)
                                }
                                Text {
                                    width: parent.width - Theme.px(100) - (retry.visible ? retry.width + Theme.px(8) : 0)
                                    anchors.verticalCenter: parent.verticalCenter
                                    wrapMode: Text.Wrap
                                    text: root.legText(stopItem.modelData.leg)
                                    font.family: serifFamily; font.pixelSize: Theme.px(11.5)
                                    color: stopItem.modelData.leg && stopItem.modelData.leg.state === "fail" ? Theme.warn : Theme.muted
                                }
                                TpButton { id: retry; visible: !!stopItem.modelData.leg && stopItem.modelData.leg.state === "fail"; compact: true; text: qsTr("重试"); onClicked: editor.retryLeg(stopItem.index) }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- 底部：保存状态、放弃、完成 ----
    Item {
        id: foot
        x: Theme.px(16)
        y: parent.height - height - Theme.px(12)
        width: parent.width - Theme.px(32)
        height: Theme.px(40)
        Rectangle { width: parent.width; height: 1; color: Theme.line; y: -Theme.px(6) }
        Text {
            objectName: "edSaveState"
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - btnRow.width - Theme.px(8)
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.Wrap
            text: editor.saveState === "saved" ? "✓ " + qsTr("已保存") : editor.saveState === "pending" ? qsTr("正在保存…") : editor.saveNote
            font.family: serifFamily; font.pixelSize: Theme.px(12)
            color: editor.saveState === "invalid" || editor.saveState === "readonly" ? Theme.danger : Theme.muted
        }
        Row {
            id: btnRow
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(6)
            TpButton { objectName: "edDiscard"; compact: true; kind: "ghost"; text: qsTr("放弃这次的修改"); onClicked: root.discardRequested() }
            TpButton { objectName: "edDone"; compact: true; kind: "primary"; text: qsTr("完成"); onClicked: root.finishRequested() }
        }
    }

    // ---- 小部件 ----
    component EdPhotoAct: Rectangle {
        id: pa
        property string icon: ""
        property string label: ""
        signal clicked()
        width: Theme.px(22); height: width
        radius: width / 2
        color: paMa.containsMouse ? Qt.rgba(0, 0, 0, 0.72) : Qt.rgba(0, 0, 0, 0.5)
        Accessible.role: Accessible.Button
        Accessible.name: label
        TpIcon { anchors.centerIn: parent; width: Theme.px(13); height: width; name: pa.icon; color: "#ffffff" }
        TpTip { text: pa.label; show: paMa.containsMouse }
        MouseArea { id: paMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pa.clicked() }
    }
    component Label: Text {
        font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
        width: parent ? parent.width : implicitWidth; wrapMode: Text.Wrap
    }
    component Err: Text {
        visible: text !== ""
        width: parent ? parent.width : implicitWidth; wrapMode: Text.Wrap
        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.danger
    }
    component Chip: Rectangle {
        property string text: ""
        property bool on: false
        signal clicked()
        width: cl.implicitWidth + Theme.px(18); height: Theme.px(26); radius: height / 2
        color: on ? Theme.accent : (cMa.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.7))
        border.width: on ? 0 : 1; border.color: Theme.line
        Text { id: cl; anchors.centerIn: parent; text: parent.text; font.family: serifFamily; font.pixelSize: Theme.px(12); color: parent.on ? "#ffffff" : Theme.textSoft }
        MouseArea { id: cMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }
    component MiniBtn: Rectangle {
        property string icon: ""
        property string tip: ""
        property bool danger: false
        signal clicked()
        width: Theme.px(26); height: Theme.px(28); radius: Theme.px(7)
        opacity: enabled ? 1 : 0.3
        color: mMa.containsMouse && enabled ? (danger ? Qt.rgba(168 / 255, 64 / 255, 28 / 255, 0.08) : Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.06)) : "transparent"
        TpIcon { anchors.centerIn: parent; name: parent.icon; width: Theme.px(15); height: width; color: parent.danger ? Theme.danger : Theme.textSoft }
        TpTip { text: parent.tip; show: mMa.containsMouse }
        MouseArea { id: mMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
        Accessible.role: Accessible.Button
        Accessible.name: tip
    }
    component ToolBtn: Rectangle {
        property string icon: ""
        property string text: ""
        property string tip: ""
        property bool on: false
        signal clicked()
        width: tbRow.implicitWidth + Theme.px(16); height: Theme.px(32); radius: Theme.radiusControl
        color: on ? Theme.accent : (tbMa.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.72))
        border.width: on ? 0 : 1; border.color: Theme.lineStrong
        Row {
            id: tbRow
            anchors.centerIn: parent
            spacing: Theme.px(4)
            TpIcon { name: parent.parent.icon; width: Theme.px(15); height: width; color: parent.parent.on ? "#ffffff" : Theme.text; anchors.verticalCenter: parent.verticalCenter }
            Text { text: parent.parent.text; font.family: serifFamily; font.pixelSize: Theme.px(13); color: parent.parent.on ? "#ffffff" : Theme.text; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: tbMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
        Accessible.role: Accessible.Button
        Accessible.name: tip
    }
}
