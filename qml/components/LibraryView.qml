// 计划库（0.11.0，L-01–L-08）："我的旅程"列表展开后的内容。照 Electron pages-library.js：三种视图（画廊 / 表格 / 按区域，记住选择）、
// 搜索、状态和类型筛选、排序、置顶、在列表里隐藏、批量（选择模式：改状态、隐藏、删除）。数据是 plans.library（C++ plans::libraryInfo）。
// 原生版：鼠标放到哪个计划，地图上那条高亮、其余变淡（O1）；点一个打开完整计划页。新建用顶栏一直在的按钮；"导入 .trip"和批量导出到阶段 ⑧。
import QtQuick
import TPApp

Item {
    id: root
    signal openPlan(string id)
    signal deleteRequested(var ids)

    readonly property var lib: plans.library
    readonly property var rows: lib.rows || []
    readonly property string view: lib.view || "gallery"
    readonly property bool wide: width >= Theme.px(600)

    // ---- 选择模式 ----
    property bool selMode: false
    property var selIds: []
    function isSel(id) { return selIds.indexOf(id) >= 0 }
    function toggleSel(id) { selIds = isSel(id) ? selIds.filter(x => x !== id) : selIds.concat([id]) }
    readonly property bool allOn: rows.length > 0 && rows.every(r => isSel(r.id))
    readonly property bool allHid: selIds.length > 0 && selIds.every(id => { const r = rows.find(x => x.id === id); return r && r.hidden })
    function clearSel() { selIds = [] }
    function leaveSel() { selMode = false; selIds = [] }
    function clicked(id) { if (selMode) toggleSel(id); else root.openPlan(id) }
    function hover(id, on) { mapc.markers.hoverFromList(on ? id : "") }

    // 模型换了视图会回到顶上：置顶、隐藏、折叠、改状态之后留在原来的地方（Electron keep）
    function keep(fn) {
        const v = root.view === "gallery" ? gallery : root.view === "table" ? table : regions
        const y = v.contentY
        fn()
        Qt.callLater(() => { v.contentY = Math.max(0, Math.min(y, v.contentHeight - v.height)) })
    }
    function statusLabel(s) { return s === "wish" ? qsTr("想去") : s === "completed" ? qsTr("已完成") : qsTr("计划中") }
    function statusTone(s) { return s === "wish" ? "#7a5a1c" : s === "completed" ? Theme.ok : Theme.accent }
    function statusBg(s) { return s === "wish" ? "#fbf1dc" : s === "completed" ? "#e3f0e6" : Theme.accentSoft }
    function dateText(r) {
        if (!r.startDate && !r.endDate) return qsTr("未定") + (r.estDays >= 1 ? " · " + qsTr("约 %1 天").arg(r.estDays) : "")
        return (r.startDate || r.endDate) + (r.days > 1 ? " · " + qsTr("%1 天").arg(r.days) : "")
    }
    function cardMeta(r) {
        const d = r.startDate || r.endDate ? Fmt.range(r.startDate, r.endDate) : qsTr("未定日期") + (r.estDays >= 1 ? " · " + qsTr("约 %1 天").arg(r.estDays) : "")
        return d + " · " + (r.type === "route" ? Fmt.one(qsTr("%1 站").arg(r.stops)) : qsTr("单一目的地"))
    }
    // 按区域：已置顶 / 各区域 / 未分类 / 已隐藏（可以折叠）；一个计划经过几个区域就在几组里
    readonly property var regionFlat: {
        const closed = lib.closed || [], out = []
        const push = (key, title, list) => {
            if (!list.length) return
            out.push({ head: true, key: key, title: title, count: list.length, open: closed.indexOf(key) < 0 })
            if (closed.indexOf(key) < 0) for (const r of list) out.push({ head: false, row: r })
        }
        push("__pinned", qsTr("已置顶"), rows.filter(r => !r.hidden && r.pinned))
        const vis = rows.filter(r => !r.hidden && !r.pinned), regs = plans.regions
        for (let i = 0; i < regs.length; ++i)
            push(regs[i], qsTr(regs[i]), vis.filter(r => i === regs.length - 1 ? r.regions.length === 0 : r.regions.indexOf(regs[i]) >= 0))
        push("__hidden", qsTr("已隐藏"), rows.filter(r => r.hidden))
        return out
    }

    component SmallTag: Rectangle {
        property string text: ""
        property color tone: Theme.textSoft
        width: tagText.implicitWidth + Theme.px(12); height: Theme.px(18); radius: height / 2
        color: Theme.panel2
        Text { id: tagText; anchors.centerIn: parent; text: parent.text; font.family: serifFamily; font.pixelSize: Theme.px(11); color: parent.tone }
    }
    component ToolBtn: Item {
        property string icon: ""
        property bool on: false
        property string tip: ""
        signal clicked()
        width: Theme.px(26); height: Theme.px(26)
        Accessible.role: Accessible.Button
        Accessible.name: tip
        Rectangle { anchors.fill: parent; radius: width / 2; color: toolMa.containsMouse ? "#ffffff" : parent.on ? Qt.rgba(1, 1, 1, 0.7) : "transparent" }
        TpIcon { anchors.centerIn: parent; name: parent.icon; width: Theme.px(14); height: Theme.px(14); color: parent.on ? Theme.accent : parent.icon === "pin" ? Theme.faint : Theme.muted }
        MouseArea { id: toolMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
        Rectangle {
            visible: toolMa.containsMouse
            z: 10
            y: parent.height + Theme.px(2)
            x: Math.min(0, (parent.width - width) / 2)
            width: tipText.implicitWidth + 12; height: tipText.implicitHeight + 6; radius: 6
            color: "#26323c"
            Text { id: tipText; anchors.centerIn: parent; text: parent.parent.tip; font.family: serifFamily; font.pixelSize: Theme.px(11); color: "#ffffff" }
        }
    }
    component Th: Text { font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted; elide: Text.ElideRight }
    component SelCircle: Rectangle {
        property bool on: false
        width: Theme.px(18); height: width; radius: width / 2
        color: on ? Theme.accent : "#ffffff"
        border.width: 1.5; border.color: on ? Theme.accent : Theme.lineStrong
        TpIcon { anchors.centerIn: parent; visible: parent.on; name: "check"; width: Theme.px(12); height: Theme.px(12); color: "#ffffff" }
    }

    // ---- 表格一行（表格和按区域共用；showRegion = false 时没有区域列） ----
    component TableRow: Item {
        id: tr
        property var r: ({})
        property bool showRegion: true
        readonly property bool sel: root.isSel(r.id)
        height: Theme.px(40)
        objectName: "libRow-" + (r.id || "")
        Rectangle { anchors.fill: parent; radius: Theme.px(10); color: tr.sel ? Theme.accentSoft : trMa.containsMouse ? Qt.rgba(1, 1, 1, 0.5) : "transparent" }
        MouseArea {
            id: trMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            Accessible.role: Accessible.ListItem
            Accessible.name: tr.r.name || ""
            onClicked: root.clicked(tr.r.id)
            onContainsMouseChanged: root.hover(tr.r.id, containsMouse)
        }
        Row {
            anchors.fill: parent
            anchors.leftMargin: Theme.px(6)
            opacity: tr.r.hidden ? 0.5 : 1
            spacing: 0
            // 圆圈滑入（Electron .lib-ck：不在选择模式时宽度为 0）
            Item {
                width: root.selMode ? Theme.px(26) : 0; height: parent.height; clip: true
                Behavior on width { enabled: Theme.motion; NumberAnimation { duration: 160 } }
                SelCircle { anchors.verticalCenter: parent.verticalCenter; on: tr.sel }
            }
            ToolBtn {
                objectName: "libPin-" + (tr.r.id || "")
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.selMode
                icon: "pin"; on: tr.r.pinned === true
                tip: tr.r.pinned ? qsTr("取消置顶") : qsTr("置顶")
                onClicked: root.keep(() => plans.togglePin(tr.r.id))
            }
            ToolBtn {
                objectName: "libHide-" + (tr.r.id || "")
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.selMode
                icon: tr.r.hidden ? "eyeOff" : "eye"; on: tr.r.hidden === true
                tip: tr.r.hidden ? qsTr("取消隐藏") : qsTr("在列表里隐藏（排到最后、变灰）")
                onClicked: root.keep(() => plans.setListHidden([tr.r.id], !tr.r.hidden))
            }
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: Theme.px(9); height: width; radius: width / 2; color: tr.r.color || Theme.accent }
            Item { width: Theme.px(8); height: 1 }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: tr.width - colsRow.width - x - Theme.px(12)
                text: tr.r.name || qsTr("未命名计划")
                elide: Text.ElideRight
                font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.text
            }
        }
        Row {
            id: colsRow
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(6)
            anchors.verticalCenter: parent.verticalCenter
            opacity: tr.r.hidden ? 0.5 : 1
            Item {
                width: root.colStatus; height: Theme.px(18)
                SmallTag { text: root.statusLabel(tr.r.status); tone: root.statusTone(tr.r.status); color: root.statusBg(tr.r.status) }
            }
            Text {
                visible: tr.showRegion && root.wide
                width: root.colRegion
                text: (tr.r.regions || []).length ? tr.r.regions.map(x => qsTr(x)).join(qsTr("、")) : qsTr("未分类")
                elide: Text.ElideRight
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: (tr.r.regions || []).length ? Theme.textSoft : Theme.muted
            }
            Text {
                visible: root.wide
                width: root.colDate
                text: tr.r.id ? root.dateText(tr.r) : ""
                elide: Text.ElideRight
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: tr.r.startDate || tr.r.endDate ? Theme.textSoft : Theme.muted
            }
            Text { width: root.colNum; text: tr.r.stops === undefined ? "" : tr.r.stops; horizontalAlignment: Text.AlignRight; font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft }
            Text { width: root.colNum; text: tr.r.photos || "—"; horizontalAlignment: Text.AlignRight; font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: tr.r.photos ? Theme.textSoft : Theme.muted }
        }
    }
    readonly property real colStatus: Theme.px(Fmt.en ? 84 : 66)
    readonly property real colRegion: Theme.px(Fmt.en ? 130 : 120)
    readonly property real colDate: Theme.px(150)
    readonly property real colNum: Theme.px(Fmt.en ? 52 : 40)

    // ---- 工具条 ----
    Flow {
        id: bar1
        x: Theme.px(10)
        width: parent.width - Theme.px(20)
        spacing: Theme.px(8)
        TpField {
            id: search
            objectName: "libSearch"
            width: Math.min(Theme.px(260), bar1.width)
            placeholder: qsTr("搜索计划或站点")
            value: root.lib.q || ""
            onEdited: (t) => plans.setLibraryPref("q", t)
        }
        Segmented {
            objectName: "libViews"
            height: Theme.px(32)
            current: root.view
            options: [{ key: "gallery", text: qsTr("画廊") }, { key: "table", text: qsTr("表格") }, { key: "region", text: qsTr("按区域") }]
            onPicked: (k) => plans.setLibraryPref("view", k)
        }
        TpButton {
            objectName: "libSelect"
            compact: true
            text: root.selMode ? qsTr("取消选择") : qsTr("选择")
            onClicked: { if (root.selMode) root.leaveSel(); else root.selMode = true }
        }
    }
    Flow {
        id: bar2
        x: Theme.px(10)
        y: bar1.y + bar1.height + Theme.px(8)
        width: parent.width - Theme.px(20)
        spacing: Theme.px(6)
        Repeater {
            model: [{ k: "all", t: qsTr("全部 %1").arg(root.lib.total || 0) }, { k: "planning", t: qsTr("计划中 %1").arg(root.lib.planning || 0) },
                    { k: "wish", t: qsTr("想去 %1").arg(root.lib.wish || 0) }, { k: "completed", t: qsTr("已完成 %1").arg(root.lib.completed || 0) }]
            delegate: TpChip {
                required property var modelData
                objectName: "libStatus-" + modelData.k
                text: modelData.t
                on: root.lib.status === modelData.k
                onClicked: plans.setLibraryPref("status", modelData.k)
            }
        }
        TpSelect {
            objectName: "libType"
            implicitHeight: Theme.px(28)
            width: Theme.px(Fmt.en ? 130 : 110)
            current: root.lib.type || "all"
            options: [{ key: "all", text: qsTr("全部类型") }, { key: "route", text: qsTr("路线") }, { key: "single", text: qsTr("单一目的地") }]
            onPicked: (k) => plans.setLibraryPref("type", k)
        }
        TpSelect {
            objectName: "libSort"
            implicitHeight: Theme.px(28)
            width: Theme.px(Fmt.en ? 130 : 110)
            current: root.lib.sort || "default"
            options: [{ key: "default", text: qsTr("默认排序") }, { key: "date", text: qsTr("按开始日期") }, { key: "name", text: qsTr("按名称") }, { key: "stops", text: qsTr("按站点数") }]
            onPicked: (k) => plans.setLibraryPref("sort", k)
        }
    }

    // ---- 内容 ----
    Item {
        id: body
        x: Theme.px(8)
        y: bar2.y + bar2.height + Theme.px(8)
        width: parent.width - Theme.px(16)
        height: parent.height - y - (selBar.visible ? selBar.height + Theme.px(8) : 0) - Theme.px(8)

        Text {
            visible: root.rows.length === 0
            x: Theme.px(12); y: Theme.px(16)
            text: qsTr("没有符合条件的计划。")
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
        }

        // 画廊
        GridView {
            id: gallery
            objectName: "libGallery"
            visible: root.view === "gallery"
            anchors.fill: parent
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 200
            readonly property int cols: Math.max(1, Math.floor(width / Theme.px(205)))
            cellWidth: width / cols
            cellHeight: cellWidth * 0.62 + Theme.px(84)
            model: visible ? root.rows : []
            delegate: Item {
                id: card
                required property var modelData
                readonly property var r: modelData
                readonly property bool sel: root.isSel(r.id)
                width: gallery.cellWidth
                height: gallery.cellHeight
                objectName: "libCard-" + r.id
                Rectangle {
                    id: cardBg
                    anchors.fill: parent
                    anchors.margins: Theme.px(5)
                    radius: Theme.px(12)
                    color: cardMa.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.72)
                    border.width: card.sel ? 2 : 1
                    border.color: card.sel ? Theme.accent : Theme.line
                    opacity: card.r.hidden ? 0.5 : 1
                    clip: true
                    Item {
                        id: cov
                        x: 1; y: 1
                        width: parent.width - 2
                        height: width * 0.6
                        RouteSketch {
                            anchors.fill: parent
                            visible: card.r.coverFile === ""
                            stops: card.r.sketch
                            route: card.r.type === "route"
                            tone: card.r.color
                        }
                        Image {
                            id: coverImg
                            anchors.fill: parent
                            visible: status === Image.Ready
                            source: card.r.coverThumb
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectCrop
                            sourceSize.width: 640
                            clip: true
                        }
                        Rectangle {
                            anchors.fill: parent
                            visible: card.r.coverFile !== "" && coverImg.status !== Image.Ready
                            color: Theme.panel2
                            Text {
                                anchors.centerIn: parent
                                visible: coverImg.status === Image.Error || card.r.coverThumb === ""
                                objectName: "libCoverMissing-" + card.r.id
                                text: qsTr("照片找不到")
                                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                            }
                        }
                    }
                    Column {
                        x: Theme.px(10); y: cov.height + Theme.px(7)
                        width: parent.width - Theme.px(20)
                        spacing: Theme.px(3)
                        Text { width: parent.width; text: card.r.name || qsTr("未命名计划"); elide: Text.ElideRight; font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: Theme.text }
                        Text { width: parent.width; text: root.cardMeta(card.r); elide: Text.ElideRight; font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted }
                        Row {
                            width: parent.width
                            spacing: Theme.px(4)
                            clip: true
                            SmallTag { text: root.statusLabel(card.r.status); tone: root.statusTone(card.r.status); color: root.statusBg(card.r.status) }
                            Repeater {
                                model: card.r.regions
                                delegate: SmallTag { required property var modelData; text: qsTr(modelData) }
                            }
                        }
                    }
                }
                MouseArea {
                    id: cardMa
                    anchors.fill: cardBg
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: card.r.name || ""
                    onClicked: root.clicked(card.r.id)
                    onContainsMouseChanged: root.hover(card.r.id, containsMouse)
                }
                SelCircle { visible: root.selMode; x: cardBg.x + Theme.px(8); y: cardBg.y + Theme.px(8); on: card.sel }
                Row {
                    visible: !root.selMode
                    x: cardBg.x + cardBg.width - width - Theme.px(6); y: cardBg.y + Theme.px(6)
                    spacing: Theme.px(3)
                    ToolBtn {
                        objectName: "libPin-" + card.r.id
                        icon: "pin"; on: card.r.pinned === true
                        tip: card.r.pinned ? qsTr("取消置顶") : qsTr("置顶")
                        opacity: on || cardMa.containsMouse || containsTools.hovered ? 1 : 0
                        onClicked: root.keep(() => plans.togglePin(card.r.id))
                    }
                    ToolBtn {
                        objectName: "libHide-" + card.r.id
                        icon: card.r.hidden ? "eyeOff" : "eye"; on: card.r.hidden === true
                        tip: card.r.hidden ? qsTr("取消隐藏") : qsTr("在列表里隐藏（排到最后、变灰）")
                        opacity: on || cardMa.containsMouse || containsTools.hovered ? 1 : 0
                        onClicked: root.keep(() => plans.setListHidden([card.r.id], !card.r.hidden))
                    }
                    HoverHandler { id: containsTools }
                }
            }
        }

        // 表格
        Item {
            visible: root.view === "table"
            anchors.fill: parent
            Row {
                id: thead
                anchors.right: parent.right
                anchors.rightMargin: Theme.px(6)
                height: Theme.px(24)
                Th { width: root.colStatus; text: qsTr("状态") }
                Th { visible: root.wide; width: root.colRegion; text: qsTr("区域") }
                Th { visible: root.wide; width: root.colDate; text: qsTr("日期") }
                Th { width: root.colNum; text: qsTr("站点"); horizontalAlignment: Text.AlignRight }
                Th { width: root.colNum; text: qsTr("照片"); horizontalAlignment: Text.AlignRight }
            }
            Text { x: Theme.px(10); height: Theme.px(24); text: qsTr("名称"); font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted }
            ListView {
                id: table
                objectName: "libTable"
                y: thead.height
                width: parent.width
                height: parent.height - y
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: parent.visible ? root.rows : []
                delegate: TableRow {
                    required property var modelData
                    width: table.width
                    r: modelData
                }
            }
        }

        // 按区域
        ListView {
            id: regions
            objectName: "libRegions"
            visible: root.view === "region"
            anchors.fill: parent
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: visible ? root.regionFlat : []
            delegate: Item {
                id: regionCell
                required property var modelData
                width: regions.width
                height: modelData.head ? Theme.px(34) : Theme.px(40)
                Item {
                    visible: regionCell.modelData.head === true
                    objectName: regionCell.modelData.head ? "libReg-" + regionCell.modelData.key : ""
                    anchors.fill: parent
                    Row {
                        x: Theme.px(10)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Theme.px(5)
                        spacing: Theme.px(6)
                        TpIcon { anchors.verticalCenter: parent.verticalCenter; name: regionCell.modelData.open ? "down" : "up"; width: Theme.px(13); height: Theme.px(13); color: Theme.muted }
                        Text { text: regionCell.modelData.title || ""; font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.text }
                        Text { text: regionCell.modelData.count || ""; font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.keep(() => plans.toggleLibraryRegion(regionCell.modelData.key)) }
                }
                Loader {
                    active: regionCell.modelData.head !== true
                    anchors.fill: parent
                    sourceComponent: TableRow { r: regionCell.modelData.row; showRegion: false }
                }
            }
        }
    }

    // ---- 批量操作条（Electron selBarHtml；导出到阶段 ⑧） ----
    Rectangle {
        id: selBar
        objectName: "libSelBar"
        visible: root.selMode
        x: Theme.px(10)
        width: parent.width - Theme.px(20)
        y: parent.height - height - Theme.px(8)
        height: selFlow.height + Theme.px(16)
        radius: Theme.px(12)
        color: Qt.rgba(1, 1, 1, 0.88)
        border.width: 1
        border.color: Theme.line
        Flow {
            id: selFlow
            x: Theme.px(10); y: Theme.px(8)
            width: parent.width - Theme.px(20)
            spacing: Theme.px(8)
            Text {
                objectName: "libSelCount"
                height: Theme.px(32)
                verticalAlignment: Text.AlignVCenter
                text: qsTr("已选 %1 个").arg(root.selIds.length)
                font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.text
            }
            TpButton {
                objectName: "libSelAll"
                compact: true
                text: root.allOn ? qsTr("全不选") : qsTr("全选")
                onClicked: {
                    const ids = root.rows.map(r => r.id)
                    root.selIds = root.allOn ? root.selIds.filter(x => ids.indexOf(x) < 0) : Array.from(new Set(root.selIds.concat(ids)))
                }
            }
            TpSelect {
                objectName: "libSelStatus"
                width: Theme.px(Fmt.en ? 140 : 110)
                enabled: root.selIds.length > 0
                current: ""
                placeholder: qsTr("改状态…")
                options: [{ key: "planning", text: qsTr("计划中") }, { key: "wish", text: qsTr("想去") }, { key: "completed", text: qsTr("已完成") }]
                onPicked: (k) => {
                    root.keep(() => { if (plans.setPlansStatus(root.selIds, k)) toast.show(qsTr("%1 个计划已改成“%2”").arg(root.selIds.length).arg(root.statusLabel(k)), false, qsTr("撤销"), () => app.undo()) })
                }
            }
            TpButton {
                objectName: "libSelHide"
                compact: true
                enabled: root.selIds.length > 0
                text: root.allHid ? qsTr("取消隐藏") : qsTr("隐藏")
                onClicked: root.keep(() => plans.setListHidden(root.selIds, !root.allHid))
            }
            TpButton {
                objectName: "libSelDelete"
                compact: true
                kind: "dangerOutline"
                enabled: root.selIds.length > 0
                text: qsTr("删除")
                onClicked: root.deleteRequested(root.selIds)
            }
        }
    }
}
