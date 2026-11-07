// "我的旅程"列表（0.8.8，功能清单 S-08）：顶栏下面、左边的浮动玻璃面板；顶栏的列表按钮或 Ctrl B 打开 / 关闭，✕ 关闭。
// 照 Electron v6.3.0 index.html #sidebar + map-app.js renderList / placeSel：
//   - 列表头"我的旅程" + 筛选（打开和左下状态胶囊同一个下拉）+ ✕；
//   - 每一行：颜色点、置顶标记、名称、"路线 · N 站 / 单一目的地 · 日期"、眼睛（在地图上隐藏 / 显示；隐藏的那一行变淡）；
//   - 点一行 = 选中这个计划、飞过去（selectPlan(id, true)）；当前项垫一颗玻璃胶囊，换计划时先沿移动方向拉长盖住起点和终点、
//     再收成终点大小（520 ms，弹性回弹）；
//   - 底部"共 N 个计划 · 计划中 · 想去 · 已完成"；没有计划 / 当前筛选下没有计划时各一句提示。
// 原生版（PO 0.8.8 选方案 B）：分界处加一行小标题和数量（置顶 / 计划中 / 想去 / 已完成；排序本来就是这个顺序），
// 下次出发的那一趟名字后面带倒计时。
// 0.8.10：标题跟着地图模式变（title 由 Main 给）；足迹模式按年份分组（group = y2025 / undated）。
// 0.11.0：计划模式列表头"展开" = 计划库（LibraryView，面板宽度由 Main 给）；想去模式每行加季节条、开始计划、关联季节，
// 分组 fit / other / noref（plans::wishInfo），列表头"新建想去"；足迹模式列表头下面一行统计；"在列表里隐藏"的排到最后、变灰。
// 0.11.1：照片模式的内容换成相册（AlbumView），列表头"展开" = 铺满成整页（albumOpen 由 Main 给）。
import QtQuick
import TPApp

Item {
    id: root
    property bool open: false
    property var info: ({})               // plans.list
    property string selectedId: ""
    property string nextBadge: ""         // 下次出发那一趟的角标（倒计时 / 第几天 / 几天前）
    property bool filterOpen: false
    property string title: qsTr("我的旅程")
    signal closeRequested()
    signal filterClicked()
    signal planClicked(string id)
    signal eyeClicked(string id, bool hidden)
    signal libraryOpenPlan(string id)
    signal newWishClicked()
    signal newPlanClicked()
    signal newFootClicked()
    signal libraryDelete(var ids)
    signal wishStart(string id)
    signal wishSeason(string id)
    signal albumExpand(bool on)
    signal albumOpenPlan(string id)
    signal albumImport()
    signal albumShow(var list, int index)
    signal albumAssign(var ids)
    signal albumFly(string planId)
    property bool albumOpen: false
    property real targetWidth: width
    readonly property bool photoMode: plans.mode === "photo"
    readonly property bool expanded: plans.libraryOpen || albumOpen
    readonly property bool wishMode: plans.mode === "wish"
    readonly property bool footMode: plans.mode === "foot"

    visible: opacity > 0.01
    opacity: open ? 1 : 0
    transform: Translate { x: root.open ? 0 : -Theme.px(14); Behavior on x { enabled: Theme.motion; TpAnim { shown: root.open } } }
    Behavior on opacity { enabled: Theme.motion; TpAnim { shown: root.open } }

    readonly property var rows: info.rows || []
    // 加上小标题的显示顺序：{ header: true, group, count } 或 { row }
    readonly property var flat: {
        const out = []
        let g = ""
        for (const r of rows) {
            if (r.group !== g) { g = r.group; out.push({ header: true, group: g, count: r.groupCount }) }
            out.push({ header: false, row: r })
        }
        return out
    }
    function groupLabel(g) {
        if (g.length === 5 && g[0] === "y") return g.slice(1)
        if (g === "undated") return qsTr("没有日期")
        if (g === "listHidden") return qsTr("已隐藏")
        if (g === "fit") return qsTr("%1合适").arg(Fmt.en ? Fmt.monEn[plans.wishMonth - 1] : plans.wishMonth + " 月")
        if (g === "other") return qsTr("其他月份更合适")
        if (g === "noref") return qsTr("还没关联季节")
        return g === "pin" ? qsTr("已置顶") : g === "wish" ? qsTr("想去") : g === "completed" ? qsTr("已完成") : qsTr("计划中")
    }
    function dateRange(r) {
        // map-app.js dateRange：start → end（N 天）；只有一个日期就显示它；都没有 = 未定日期
        if (r.startDate && r.endDate) return r.startDate + " → " + (r.startDate.slice(0, 4) === r.endDate.slice(0, 4) ? r.endDate.slice(5) : r.endDate) + (r.days > 0 && r.days < 100 ? qsTr("（%1 天）").arg(r.days) : "")
        return r.startDate || r.endDate || qsTr("未定日期")
    }
    function metaOf(r) {
        if (root.footMode) return (r.type === "route" ? Fmt.one(qsTr("%1 站").arg(r.stops)) + " · " : "") + dateRange(r)   // 足迹按年分组，一行放得下日期最要紧
        const kind = r.type === "route" ? Fmt.one(qsTr("路线 · %1 站").arg(r.stops)) : qsTr("单一目的地")
        if (root.wishMode && r.regions && r.regions.length) return kind + " · " + r.regions.map(x => qsTr(x)).join(qsTr("、"))   // Electron 想去卡片：区域 + 路线 · N 站
        return kind + " · " + dateRange(r)
    }
    function containsGlobal(gx, gy) {
        if (!visible) return false
        const p = root.mapFromItem(null, gx, gy)
        return p.x >= 0 && p.y >= 0 && p.x <= width && p.y <= height
    }
    function filterButtonContains(gx, gy) {
        const p = filterBtn.mapFromItem(null, gx, gy)
        return filterBtn.visible && p.x >= 0 && p.y >= 0 && p.x <= filterBtn.width && p.y <= filterBtn.height
    }
    readonly property alias filterButton: filterBtn
    component HeadPill: Item {
        id: pill
        property string text: ""
        signal clicked()
        width: pillText.implicitWidth + Theme.px(20)
        height: Theme.px(28)
        anchors.verticalCenter: parent.verticalCenter
        Accessible.role: Accessible.Button
        Accessible.name: text
        Rectangle { anchors.fill: parent; radius: height / 2; color: pillMa.containsMouse ? Qt.rgba(1, 1, 1, 0.7) : Qt.rgba(1, 1, 1, 0.45); border.color: Theme.line }
        Text { id: pillText; anchors.centerIn: parent; text: pill.text; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text }
        MouseArea { id: pillMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pill.clicked() }
    }

    GlassPanel {
        anchors.fill: parent
        radius: Theme.px(16)
    }
    MouseArea { anchors.fill: parent; onWheel: (w) => w.accepted = true }   // 面板里的空白处不穿透到地图

    // ---- 列表头 ----
    Item {
        id: head
        x: Theme.px(16); y: Theme.px(10)
        width: parent.width - Theme.px(26)
        height: Theme.px(36)
        Text {
            anchors.verticalCenter: parent.verticalCenter
            objectName: "listTitle"
            text: root.title
            font.family: serifFamily; font.pixelSize: Theme.px(16); font.bold: true; color: Theme.text
        }
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(6)
            HeadPill {
                objectName: "listExpandBtn"
                visible: plans.mode === "plan" || root.photoMode
                text: root.expanded ? qsTr("收起") : qsTr("展开")
                onClicked: root.photoMode ? root.albumExpand(!root.albumOpen) : plans.setLibraryOpen(!root.expanded)
            }
            HeadPill {
                objectName: "wishNewBtn"
                visible: root.wishMode
                text: qsTr("＋ 新建想去")
                onClicked: root.newWishClicked()
            }
            Item {
                id: filterBtn
                objectName: "listFilterBtn"
                visible: !root.expanded && !root.photoMode
                width: fRow.implicitWidth + Theme.px(20)
                height: Theme.px(28)
                anchors.verticalCenter: parent.verticalCenter
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("筛选")
                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: root.filterOpen ? "#ffffff" : (fMa.containsMouse ? Qt.rgba(1, 1, 1, 0.7) : Qt.rgba(1, 1, 1, 0.45))
                    border.color: root.filterOpen ? Theme.accent : Theme.line
                }
                Row {
                    id: fRow
                    anchors.centerIn: parent
                    spacing: Theme.px(4)
                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("筛选"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text }
                    Rectangle {
                        visible: plans.excludedCount > 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(height, ec.implicitWidth + Theme.px(10)); height: Theme.px(17); radius: height / 2
                        color: Theme.accent
                        Text { id: ec; anchors.centerIn: parent; text: plans.excludedCount; font.pixelSize: Theme.px(11); font.bold: true; color: "#ffffff" }
                    }
                    TpIcon { anchors.verticalCenter: parent.verticalCenter; name: "down"; width: Theme.px(13); height: Theme.px(13); color: Theme.muted }
                }
                MouseArea { id: fMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.filterClicked() }
            }
            Item {
                width: Theme.px(28); height: Theme.px(28)
                anchors.verticalCenter: parent.verticalCenter
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("关闭列表")
                Rectangle { anchors.fill: parent; radius: width / 2; color: xMa.containsMouse ? Qt.rgba(1, 1, 1, 0.6) : "transparent" }
                TpIcon { anchors.centerIn: parent; name: "close"; width: Theme.px(15); height: Theme.px(15); color: Theme.muted }
                MouseArea { id: xMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
            }
        }
    }

    // 足迹的统计（PO 第 3 题 A）：地区数和足迹框同一个数（手动标记的 + 已完成计划经过的，不去重到国家）；省界模型第一次要用时才建
    Text {
        id: footStat
        objectName: "footStat"
        visible: root.footMode && root.open
        x: Theme.px(18)
        y: head.y + head.height
        width: root.width - Theme.px(36)
        elide: Text.ElideRight
        text: Fmt.one(qsTr("%1 趟").arg(root.info.completed || 0) + (mapc.unlockReady ? " · " + qsTr("%1 个地区").arg(mapc.unlockProvinces) : ""))
        font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.accent
        onVisibleChanged: if (visible) mapc.needUnlockCount()
    }

    // ---- 相册（照片模式，窄列表和整页同一个组件）----
    Loader {
        id: albumLoader
        active: root.photoMode && root.open
        x: 0
        y: head.y + head.height + Theme.px(4)
        width: parent.width
        height: parent.height - y
        sourceComponent: Component {
            AlbumView {
                objectName: "albumView"
                compact: !root.albumOpen || root.targetWidth < Theme.px(620)
                targetWidth: root.targetWidth
                onOpenPlan: (id) => root.albumOpenPlan(id)
                onImportClicked: root.albumImport()
                onShowPhotos: (l, i) => root.albumShow(l, i)
                onAssignRequested: (ids) => root.albumAssign(ids)
                onFlyTo: (id) => root.albumFly(id)
            }
        }
    }

    // ---- 计划库 ----
    Loader {
        id: libraryLoader
        active: plans.libraryOpen
        x: 0
        y: head.y + head.height + Theme.px(4)
        width: parent.width
        height: parent.height - y
        sourceComponent: Component {
            LibraryView {
                objectName: "libraryView"
                onOpenPlan: (id) => root.libraryOpenPlan(id)
                onDeleteRequested: (ids) => root.libraryDelete(ids)
                onNewPlan: root.newPlanClicked()
            }
        }
    }
    function leaveLibrarySelection() { if (libraryLoader.item) libraryLoader.item.leaveSel() }

    // ---- 列表 ----
    ListView {
        id: list
        objectName: "planListView"
        visible: !root.expanded && !root.photoMode
        x: Theme.px(8)
        y: head.y + head.height + Theme.px(4) + (root.footMode ? footStat.implicitHeight + Theme.px(4) : 0)
        width: parent.width - Theme.px(16)
        height: parent.height - y - foot.height - Theme.px(24)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.flat
        spacing: 0
        currentIndex: -1

        // 当前项的玻璃胶囊（#plSel）：放在内容里、垫在行下面
        Rectangle {
            id: cap
            parent: list.contentItem
            z: -1
            x: 0
            width: list.width
            radius: Theme.px(12)
            color: Qt.rgba(1, 1, 1, 0.62)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.95)
            visible: false
            property real toY: 0
            property real toH: 0
            SequentialAnimation {
                id: stretch
                ParallelAnimation {
                    NumberAnimation { id: s1y; target: cap; property: "y"; duration: 218; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.4, 0, 0.6, 1, 1, 1] }
                    NumberAnimation { id: s1h; target: cap; property: "height"; duration: 218; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.4, 0, 0.6, 1, 1, 1] }
                }
                ParallelAnimation {
                    NumberAnimation { id: s2y; target: cap; property: "y"; duration: 302; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.34, 1.56, 0.64, 1, 1, 1] }
                    NumberAnimation { id: s2h; target: cap; property: "height"; duration: 302; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.34, 1.56, 0.64, 1, 1, 1] }
                }
            }
            // 换到 (y, h)：沿移动方向先拉长盖住起点和终点（0.42 处），再收成终点大小
            function moveTo(y, h) {
                const from = visible ? { y: cap.y, h: cap.height } : null
                visible = true
                stretch.stop()
                if (!from || (from.y === y && from.h === h) || !Theme.motion) { cap.y = y; cap.height = h; return }
                const lo = Math.min(from.y, y), hi = Math.max(from.y + from.h, y + h)
                s1y.to = lo; s1h.to = hi - lo
                s2y.to = y; s2h.to = h
                stretch.start()
            }
        }
        function placeCap() {
            for (let i = 0; i < count; ++i) {
                const d = root.flat[i]
                if (d && !d.header && d.row.id === root.selectedId) {
                    const it = itemAtIndex(i)
                    if (it) { cap.moveTo(it.y, it.height); return }
                    positionViewAtIndex(i, ListView.Contain)
                    capLater.restart()
                    return
                }
            }
            cap.visible = false
            stretch.stop()
        }
        Timer { id: capLater; interval: 0; onTriggered: list.placeCap() }

        delegate: Item {
            id: cell
            required property var modelData
            required property int index
            readonly property bool header: modelData.header
            readonly property var r: header ? ({}) : modelData.row
            width: list.width
            height: header ? Theme.px(index === 0 ? 24 : 32) : root.wishMode ? Theme.px(52) + (r.kinds && r.kinds.length ? Theme.px(10) : 0) + Theme.px(24) : Theme.px(52)
            objectName: header ? "" : "planRow-" + r.id

            // 小标题（方案 B）
            Text {
                visible: cell.header
                x: Theme.px(10)
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.px(3)
                text: cell.header ? root.groupLabel(cell.modelData.group) + (root.wishMode ? " · " : "  ") + cell.modelData.count : ""
                font.family: serifFamily; font.pixelSize: Theme.px(11); font.letterSpacing: 1.2; color: Theme.muted
            }

            Rectangle {
                visible: !cell.header && rowMa.containsMouse && cell.r.id !== root.selectedId
                anchors.fill: parent
                radius: Theme.px(12)
                color: Qt.rgba(1, 1, 1, 0.32)
            }
            Item {
                visible: !cell.header
                anchors.fill: parent
                opacity: cell.r.hidden || cell.r.listHidden ? 0.45 : 1
                Rectangle {
                    id: dot
                    x: Theme.px(12)
                    y: Theme.px(26) - height / 2
                    width: Theme.px(9); height: width; radius: width / 2
                    color: cell.r.color || Theme.accent
                }
                Column {
                    x: dot.x + dot.width + Theme.px(10)
                    width: parent.width - x - eye.width - Theme.px(10)
                    y: Theme.px(8)
                    spacing: Theme.px(1)
                    Row {
                        width: parent.width
                        spacing: Theme.px(5)
                        TpIcon { visible: cell.r.pinned === true; name: "pin"; width: Theme.px(12); height: Theme.px(12); color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            id: nameText
                            width: Math.min(implicitWidth, parent.width - (cell.r.pinned ? Theme.px(17) : 0) - (badge.visible ? badge.width + Theme.px(5) : 0))
                            text: cell.r.name || qsTr("未命名计划")
                            elide: Text.ElideRight
                            font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: cell.r.id === root.selectedId; color: Theme.text
                        }
                        Rectangle {
                            id: badge
                            readonly property bool now: cell.r.nowFit === true   // 想去：今天这个月合适（H-02）
                            visible: now || (cell.r.isNext === true && root.nextBadge !== "")
                            anchors.verticalCenter: parent.verticalCenter
                            width: bt.implicitWidth + Theme.px(12); height: Theme.px(17); radius: height / 2
                            color: now ? "#e3f0e6" : "#f3e3d9"
                            Text { id: bt; anchors.centerIn: parent; text: badge.now ? qsTr("现在去正好") : root.nextBadge; font.family: serifFamily; font.pixelSize: Theme.px(11); font.bold: true; color: badge.now ? "#2f6b45" : "#8a4210" }
                        }
                    }
                    Text {
                        width: parent.width
                        text: cell.header ? "" : root.metaOf(cell.r)
                        elide: Text.ElideRight
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                    // 想去：季节条，选中的月份描一圈
                    Row {
                        objectName: cell.header ? "" : "wishStrip-" + cell.r.id
                        visible: root.wishMode && !cell.header && (cell.r.kinds || []).length > 0
                        topPadding: Theme.px(4)
                        spacing: Theme.px(2)
                        Repeater {
                            model: parent.visible ? cell.r.kinds : []
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: Theme.px(16); height: Theme.px(7); radius: Theme.px(2)
                                color: Theme.seasonColor(modelData)
                                border.width: index + 1 === plans.wishMonth ? 1.5 : 0
                                border.color: Theme.text
                            }
                        }
                    }
                }
            }
            MouseArea {
                id: rowMa
                anchors.fill: parent
                enabled: !cell.header
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.ListItem
                Accessible.name: cell.r.name || ""
                onClicked: root.planClicked(cell.r.id)
            }
            // 想去：开始计划、关联季节（放在行的 MouseArea 上面）
            Row {
                visible: root.wishMode && !cell.header
                x: dot.x + dot.width + Theme.px(10)
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.px(6)
                spacing: Theme.px(14)
                LinkText { objectName: cell.header ? "" : "wishStartBtn-" + cell.r.id; small: true; text: qsTr("开始计划"); onClicked: root.wishStart(cell.r.id) }
                LinkText {
                    objectName: cell.header ? "" : "wishSeasonBtn-" + cell.r.id
                    small: true; muted: true
                    text: (cell.r.kinds || []).length ? qsTr("改关联的季节") : qsTr("关联季节")
                    onClicked: root.wishSeason(cell.r.id)
                }
            }
            // 眼睛：在地图上隐藏 / 显示（不变淡，好找）
            Item {
                id: eye
                objectName: cell.header ? "" : "eye-" + cell.r.id
                visible: !cell.header
                anchors.right: parent.right
                anchors.rightMargin: Theme.px(6)
                y: Theme.px(10)
                width: Theme.px(32); height: Theme.px(32)
                Accessible.role: Accessible.Button
                Accessible.name: cell.r.hidden ? qsTr("在地图上显示") : qsTr("在地图上隐藏")
                Rectangle { anchors.fill: parent; radius: width / 2; color: eyeMa.containsMouse ? Qt.rgba(1, 1, 1, 0.7) : "transparent" }
                TpIcon { anchors.centerIn: parent; name: cell.r.hidden ? "eyeOff" : "eye"; width: Theme.px(16); height: Theme.px(16); color: cell.r.hidden ? Theme.faint : Theme.muted }
                MouseArea { id: eyeMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.eyeClicked(cell.r.id, !cell.r.hidden) }
            }
        }

        onCountChanged: capLater.restart()
    }
    onSelectedIdChanged: list.placeCap()
    onFlatChanged: capLater.restart()
    onOpenChanged: if (open) capLater.restart()

    // 这个模式一个计划都没有（0.12.7 空状态）/ 当前筛选下没有计划
    readonly property int modeCount: wishMode ? (info.wish || 0) : footMode ? (info.completed || 0) : (info.total || 0)
    EmptyState {
        objectName: root.wishMode ? "emptyWish" : root.footMode ? "emptyFoot" : "emptyPlanList"
        visible: root.rows.length === 0 && !root.expanded && !root.photoMode && root.modeCount === 0
        x: Theme.px(16); y: list.y + Theme.px(28)
        width: root.width - Theme.px(32)
        art: root.wishMode ? "wish" : root.footMode ? "foot" : "plan"
        title: root.wishMode ? qsTr("还没有想去的地方") : root.footMode ? qsTr("还没有走过的足迹") : qsTr("还没有计划")
        detail: root.wishMode ? qsTr("看到喜欢的目的地，先记在这里，以后再排日期。")
              : root.footMode ? qsTr("旅程完成后，路线和去过的地方都会留在这里。")
              : qsTr("把想走的路线记下来：站点、日期、预算和行李都放在一个计划里。")
        action: root.wishMode ? qsTr("添加想去的地方") : root.footMode ? qsTr("记一趟走过的旅程") : qsTr("新建计划")
        onActed: root.wishMode ? root.newWishClicked() : root.footMode ? root.newFootClicked() : root.newPlanClicked()
    }
    Text {
        visible: root.rows.length === 0 && !root.expanded && !root.photoMode && root.modeCount > 0
        x: Theme.px(20); y: list.y + Theme.px(16)
        width: root.width - Theme.px(40)
        wrapMode: Text.Wrap
        lineHeight: 1.4
        text: qsTr("当前筛选下没有计划。") + "\n" + qsTr("可以在「筛选」里点「全选」恢复。")
        font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
    }

    // ---- 底部 ----
    Rectangle { visible: !root.expanded && !root.photoMode; x: Theme.px(14); width: root.width - Theme.px(28); height: 1; color: Theme.line; y: foot.y - Theme.px(8) }
    Text {
        id: foot
        visible: !root.expanded && !root.photoMode
        x: Theme.px(18)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.px(12)
        width: root.width - Theme.px(36)
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        text: qsTr("共 %1 个计划 · 计划中 %2 · 想去 %3 · 已完成 %4").arg(root.info.total || 0).arg(root.info.planning || 0).arg(root.info.wish || 0).arg(root.info.completed || 0)
        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
    }
}
