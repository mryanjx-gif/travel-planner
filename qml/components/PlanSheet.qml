// 简易计划面板（0.8.7，M-21、M-22；PO 2026-11-05 选方案 C"一次看一站"）：地图上选中计划时从右边滑进来。
// 内容照 Electron map-app.js renderDetail（状态、类型 · N 站、区域、日期、封面、备注、站点的日期 / 安排 / 去下一站 / 照片），版式改成：
//   头部：计划名 + 一行摘要；◎ 定位（飞过去、让开面板）、⋯（置顶、设为下次出发、删除）、× 关闭
//   路线：顶部一排站点条（点一下聚焦那一站，和地图标记、← → 同步）；没有聚焦时显示概览（封面、备注、全部站点的简表）；
//   聚焦时只显示这一站（日期、住几晚、安排、照片、备注、下一段怎么走）
//   单一目的地：直接显示那一站。
// ⋯ 里的计划库 / 日历 / 导出在对应页面做好以前不显示；0.10.0 起底部有"打开完整计划"；0.8.9 起头部有"编辑"（铅笔，打开站点编辑器）。
// source = plans.sheet（plans.cpp sheetInfo + 缩略图地址）。
import QtQuick
import TPApp

Item {
    id: root
    property var source: ({})                   // plans.sheet；关掉（变空）时面板滑出去，内容留着
    property var sheet: ({})
    onSourceChanged: if (source && source.id) sheet = source
    property int focusIndex: -1                      // 聚焦的站点（mapc.markers.focusStop）
    signal closeRequested()
    signal locate(string planId)                // ◎：回到整条路线（取消聚焦、框住整个计划）
    signal overview(string planId)              // 站点条上看某一站时的 ↩ 全部路线：同上（0.8.7 PO：点进某一站后回不到整条路线）
    signal pickStop(int index)
    signal togglePin(string planId)
    signal toggleNext(string planId, bool on)
    signal deleteRequested(string planId, string name)
    signal editRequested(string planId)         // 0.8.9：打开站点编辑器（Electron dtEdit）
    signal fullRequested(string planId)         // 0.10.0：打开完整计划
    signal photoViewRequested(var list, int index)   // 0.10.5：看大图
    readonly property var stops: sheet.stops || []
    readonly property bool route: sheet.type === "route" && stops.length > 1
    readonly property int shown: route ? focusIndex : (stops.length ? 0 : -1)   // 现在显示哪一站（-1 = 概览）
    readonly property bool atOverview: shown < 0 || !route   // 计划的封面和备注：路线在概览时、单一目的地一直显示（想去多半是单一目的地）
    readonly property var stop: shown >= 0 && shown < stops.length ? stops[shown] : null

    function statusLabel(s) { return s === "completed" ? qsTr("已完成") : s === "wish" ? qsTr("想去") : qsTr("计划中") }
    function statusColor(s) { return s === "completed" ? "#2b8a3e" : s === "wish" ? "#7b61b5" : "#e8590c" }
    function legLabel(m) { return m === "train" ? qsTr("火车") : m === "ferry" ? qsTr("船") : m === "drive" ? qsTr("自驾") : m === "walk" ? qsTr("徒步") : qsTr("飞机") }
    // 0.12.11 换行只在" · "和"、"处，不把"未定日期"拆成两行（字之间加 WORD JOINER，空格换不换行的空格）
    function keep(t) { return String(t).replace(/ /g, " ").split("").join("⁠") }
    function summary() {
        const bits = [statusLabel(sheet.status)]
        bits.push(route ? qsTr("%1 站").arg(stops.length) : qsTr("单一目的地"))
        const regs = (sheet.regions || []).map(r => keep(qsTr(r)))
        const d = Fmt.range(sheet.startDate, sheet.endDate)
        bits.push(d !== "" ? d : qsTr("未定日期"))
        const out = bits.map(keep)
        if (regs.length) out.splice(2, 0, regs.join("、"))
        return out.join(" · ")
    }
    function legText(leg) {
        if (!leg) return ""
        const bits = [legLabel(leg.mode)]
        if (leg.dist > 0) bits.push(Fmt.dist(leg.dist))
        if (leg.time > 0) bits.push(qsTr("约 %1").arg(Fmt.dur(leg.time)))
        return bits.join(" · ")
    }
    function containsGlobal(gx, gy) {
        const p = root.mapFromItem(null, gx, gy)
        return visible && p.x >= 0 && p.y >= 0 && p.x <= width && p.y <= height
    }
    onSheetChanged: { moreMenu.open = false; notesOpen = false; stopNotesOpen = false }
    onShownChanged: { stopNotesOpen = false; body.contentY = 0 }
    // 1.4.0 聚焦某站时才查这一站的介绍（有缓存就不联网）
    onStopChanged: if (stop) place.show(stop.name || "", stop.lat, stop.lng, stop.arriveDate || "", stop.departDate || "", stop.tz || ""); else place.clear()
    property bool notesOpen: false
    property bool stopNotesOpen: false

    GlassPanel {
        anchors.fill: parent
        radius: Theme.radiusLarge
    }
    MouseArea { anchors.fill: parent; onClicked: moreMenu.open = false; onWheel: (w) => w.accepted = true }

    // ---- 头部 ----
    Item {
        id: head
        x: Theme.px(22)
        y: Theme.px(18)
        width: parent.width - Theme.px(22) - Theme.px(12)
        height: Math.max(titleCol.implicitHeight, Theme.px(36))
        Column {
            id: titleCol
            width: parent.width - btns.width - Theme.px(8)
            spacing: Theme.px(4)
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.family: serifFamily; font.pixelSize: Theme.px(20); font.bold: true; color: Theme.text
                text: root.sheet.name || qsTr("未命名计划")
            }
            Row {
                width: parent.width
                spacing: Theme.px(6)
                Rectangle {
                    width: Theme.px(8); height: width; radius: width / 2
                    color: root.statusColor(root.sheet.status)
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    width: parent.width - Theme.px(14)
                    wrapMode: Text.Wrap
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                    text: root.summary() + (root.sheet.pinned ? " · " + root.keep(qsTr("已置顶")) : "")
                }
            }
        }
        Row {
            id: btns
            anchors.right: parent.right
            spacing: Theme.px(6)
            RoundIcon { objectName: "sheetEdit"; kind: "edit"; label: qsTr("编辑"); onClicked: root.editRequested(root.sheet.id) }
            RoundIcon { objectName: "sheetLocate"; kind: "locate"; label: qsTr("在地图上定位"); onClicked: root.locate(root.sheet.id) }
            RoundIcon { kind: "more"; label: qsTr("更多"); active: moreMenu.open; onClicked: moreMenu.open = !moreMenu.open }
            RoundIcon { objectName: "sheetClose"; kind: "close"; label: qsTr("关闭"); plain: true; onClicked: root.closeRequested() }
        }
    }

    // ---- 站点条（路线） ----
    ListView {
        id: strip
        visible: root.route
        x: Theme.px(18)
        y: head.y + head.height + Theme.px(14)
        width: parent.width - Theme.px(36)
        height: visible ? Theme.px(36) : 0
        orientation: ListView.Horizontal
        spacing: Theme.px(6)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.route ? root.stops : []
        currentIndex: root.focusIndex
        highlightFollowsCurrentItem: false
        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain); else positionViewAtBeginning()
        delegate: Rectangle {
            id: chip
            required property var modelData
            required property int index
            readonly property bool on: index === root.focusIndex
            width: chipRow.implicitWidth + Theme.px(18)
            height: Theme.px(32)
            y: Theme.px(2)
            radius: height / 2
            color: on ? "#ffffff" : (chipMa.containsMouse ? Qt.rgba(1, 1, 1, 0.8) : Qt.rgba(1, 1, 1, 0.55))
            border.width: 1
            border.color: on ? Theme.accent : Theme.line
            Row {
                id: chipRow
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.px(6)
                spacing: Theme.px(6)
                Rectangle {
                    width: Theme.px(20); height: width; radius: width / 2
                    color: chip.modelData.color || root.sheet.color
                    anchors.verticalCenter: parent.verticalCenter
                    Text { anchors.centerIn: parent; text: chip.index + 1; font.family: serifFamily; font.pixelSize: Theme.px(11); font.bold: true; color: "#ffffff" }
                }
                Text {
                    text: chip.modelData.name || qsTr("未命名地点")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: chip.on; color: Theme.text
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea { id: chipMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.pickStop(chip.index) }
        }
    }

    // ---- 内容 ----
    Flickable {
        id: body
        objectName: "sheetBody"
        x: Theme.px(22)
        y: (root.route ? strip.y + strip.height : head.y + head.height) + Theme.px(14)
        width: parent.width - Theme.px(44)
        height: fullBtn.y - y - Theme.px(8)
        contentHeight: content.implicitHeight + Theme.px(8)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: body.width
            spacing: Theme.px(10)

            // 概览（路线没有聚焦；单一目的地这一块也显示，下面接着站点内容）
            Image {
                objectName: "sheetCover"
                visible: root.atOverview && (root.sheet.coverThumb || "") !== "" && status !== Image.Error
                width: parent.width
                height: visible ? Math.round(width * 0.42) : 0
                source: root.atOverview ? (root.sheet.coverThumb || "") : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 640
                MouseArea {   // 0.10.5 看大图
                    anchors.fill: parent
                    enabled: !!root.sheet.coverFile
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.photoViewRequested([{ file: root.sheet.coverFile, thumb: root.sheet.coverThumb }], 0)
                }
            }
            NotesBlock { objectName: "sheetNotes"; visible: root.atOverview && (root.sheet.notes || "") !== ""; text: root.sheet.notes || ""; open: root.notesOpen; onToggled: root.notesOpen = !root.notesOpen }
            Text {
                visible: root.shown < 0
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                text: qsTr("共 %1 站 · 点上面的站名或按 ← → 一站一站看").arg(root.stops.length)
            }
            Repeater {
                model: root.shown < 0 ? root.stops : []
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    width: content.width
                    height: Theme.px(30)
                    Rectangle { anchors.fill: parent; radius: Theme.radiusControl; color: rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.7) : "transparent" }
                    Rectangle {
                        x: Theme.px(4)
                        width: Theme.px(20); height: width; radius: width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        color: row.modelData.color || root.sheet.color
                        Text { anchors.centerIn: parent; text: row.index + 1; font.family: serifFamily; font.pixelSize: Theme.px(11); font.bold: true; color: "#ffffff" }
                    }
                    Text {
                        x: Theme.px(32)
                        width: parent.width - x - dates.implicitWidth - Theme.px(8)
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.text
                        text: row.modelData.name || qsTr("未命名地点")
                    }
                    Text {
                        id: dates
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.px(4)
                        anchors.verticalCenter: parent.verticalCenter
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                        text: Fmt.range(row.modelData.arriveDate, row.modelData.departDate)
                    }
                    MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.pickStop(row.index) }
                }
            }

            // 一站
            Item {
                visible: root.stop !== null
                width: parent.width
                height: stopHead.implicitHeight
                Column {
                    id: stopHead
                    width: parent.width - (root.route ? steps.width + Theme.px(8) : 0)
                    spacing: Theme.px(3)
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
                        text: root.stop ? (root.stop.name || qsTr("未命名地点")) : ""
                    }
                    Text {
                        visible: text !== ""
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                        text: {
                            if (!root.stop) return ""
                            const d = Fmt.range(root.stop.arriveDate, root.stop.departDate)
                            const n = Fmt.nights(root.stop.arriveDate, root.stop.departDate)
                            return d + (n > 0 ? " · " + (n === 1 && Fmt.en ? "1 night" : qsTr("住 %1 晚").arg(n)) : "")
                        }
                    }
                }
                Row {
                    id: steps
                    visible: root.route && root.focusIndex >= 0
                    anchors.right: parent.right
                    spacing: Theme.px(4)
                    RoundIcon { objectName: "sheetOverview"; kind: "uturn"; label: qsTr("全部路线"); small: true; showTip: true; onClicked: root.overview(root.sheet.id) }
                    RoundIcon { kind: "prev"; label: qsTr("上一站"); small: true; enabled: root.focusIndex > 0; onClicked: root.pickStop(root.focusIndex - 1) }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                        text: qsTr("%1 / %2").arg(root.focusIndex + 1).arg(root.stops.length)
                    }
                    RoundIcon { kind: "next"; label: qsTr("下一站"); small: true; enabled: root.focusIndex < root.stops.length - 1; onClicked: root.pickStop(root.focusIndex + 1) }
                }
            }
            Column {
                visible: root.stop !== null
                width: parent.width
                spacing: Theme.px(6)
                Text {
                    visible: !root.stop || (root.stop.schedule || []).length === 0
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                    text: qsTr("暂无行程安排")
                }
                Repeater {
                    model: root.stop ? (root.stop.schedule || []) : []
                    delegate: Row {
                        required property var modelData
                        width: content.width
                        spacing: Theme.px(10)
                        Text {
                            id: when
                            visible: text !== ""
                            font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
                            text: (modelData.date ? Fmt.md(modelData.date) + (modelData.time ? " " : "") : "") + (modelData.time || "") + (modelData.end ? "–" + modelData.end : "")
                        }
                        Text {
                            width: content.width - (when.visible ? when.implicitWidth + Theme.px(10) : 0)
                            wrapMode: Text.Wrap
                            font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.text
                            text: modelData.text || ""
                        }
                    }
                }
            }
            Grid {
                visible: root.stop !== null && (root.stop.thumbs || []).length > 0
                width: parent.width
                columns: 3
                spacing: Theme.px(6)
                Repeater {
                    model: root.stop ? (root.stop.thumbs || []).slice(0, 6) : []
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        width: (content.width - 2 * Theme.px(6)) / 3
                        height: Math.round(width * 0.72)
                        radius: Theme.px(10)
                        color: Theme.panel2
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: modelData
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 320
                        }
                        MouseArea {   // 0.10.5 看大图
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.photoViewRequested(photos.planPhotos(root.sheet.id).filter(p => p.stopId === root.stop.id && p.thumb), parent.index)
                        }
                    }
                }
            }
            Text {
                visible: root.stop !== null && (root.stop.thumbs || []).length > 6
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                text: root.stop ? Fmt.one(qsTr("照片 %1 张").arg((root.stop.thumbs || []).length)) : ""
            }
            NotesBlock { visible: root.stop !== null && (root.stop.notes || "") !== ""; text: root.stop ? (root.stop.notes || "") : ""; open: root.stopNotesOpen; onToggled: root.stopNotesOpen = !root.stopNotesOpen }
            Text {   // 下一段怎么走
                visible: root.route && root.stop !== null && root.stop.leg !== undefined && root.shown < root.stops.length - 1
                width: parent.width
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
                text: root.stop && root.stop.leg && root.shown + 1 < root.stops.length
                      ? qsTr("下一段：%1 → <b>%2</b>").arg(root.legText(root.stop.leg)).arg(root.stops[root.shown + 1].name || qsTr("未命名地点")) : ""
            }

            // ---- 1.4.0 地点介绍：横杠下面是现在这一站的介绍（place，src/services/placeinfo.h），切站跟着换 ----
            Rectangle {
                objectName: "sheetIntroLine"
                visible: root.stop !== null
                width: parent.width
                height: visible ? 1 : 0
                color: Theme.line
            }
            Column {
                id: intro
                objectName: "sheetIntro"
                visible: root.stop !== null
                width: parent.width
                spacing: Theme.px(8)
                readonly property var info: place.info
                readonly property var pics: info.photos || []
                Row {   // 照片 1–3 张，点开看大图（大图下面写作者和授权）
                    visible: intro.pics.length > 0
                    width: parent.width
                    spacing: Theme.px(6)
                    Repeater {
                        model: intro.pics
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: (intro.width - (intro.pics.length - 1) * Theme.px(6)) / intro.pics.length
                            height: Math.round(intro.width * (intro.pics.length === 1 ? 0.5 : 0.32))
                            radius: Theme.px(10)
                            color: Theme.panel2
                            clip: true
                            Image { anchors.fill: parent; source: modelData.thumb; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 640 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.photoViewRequested(intro.pics, parent.index) }
                        }
                    }
                }
                Text {
                    visible: text !== ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.family: serifFamily; font.pixelSize: Theme.px(16); font.bold: true; color: Theme.text
                    text: intro.info.title || ""
                }
                Text {
                    objectName: "sheetIntroText"
                    visible: text !== ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    lineHeight: 1.3
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
                    text: intro.info.text || ""
                }
                LinkText { visible: !!intro.info.wikiUrl; small: true; text: qsTr("维基百科 ›"); onClicked: Qt.openUrlExternally(intro.info.wikiUrl) }
                Repeater {   // 开放时间、门票、官网、电话：有就显示
                    model: [
                        { k: qsTr("开放时间"), v: intro.info.hours || "" },
                        { k: qsTr("门票"), v: intro.info.fee === "yes" ? qsTr("收费") : intro.info.fee === "no" ? qsTr("免费") : (intro.info.fee || "") },
                        { k: qsTr("官网"), v: intro.info.website || "", url: intro.info.website || "" },
                        { k: qsTr("电话"), v: intro.info.phone || "" }
                    ].filter(r => r.v !== "")
                    delegate: Row {
                        required property var modelData
                        width: intro.width
                        spacing: Theme.px(10)
                        Text { id: fk; text: modelData.k; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
                        Text {
                            width: intro.width - fk.implicitWidth - Theme.px(10)
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            font.family: serifFamily; font.pixelSize: Theme.px(13)
                            color: modelData.url ? Theme.accentInk : Theme.text
                            text: modelData.url ? String(modelData.v).replace(/^https?:\/\/(www\.)?/, "").replace(/\/$/, "") : modelData.v
                            MouseArea { anchors.fill: parent; enabled: !!modelData.url; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally(modelData.url) }
                        }
                    }
                }
                Text {   // 当天天气（16 天内预报，更远往年同月平均）和日出日落
                    objectName: "sheetIntroWx"
                    visible: text !== ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text
                    text: {
                        const a = [], w = intro.info.wx, c = intro.info.clim, s = intro.info.sun
                        if (w) a.push(qsTr("当天 %1").arg(Fmt.wxText(w)) + (w.pop !== undefined ? " · " + qsTr("降水 %1%").arg(Math.round(w.pop)) : ""))
                        else if (c) a.push(qsTr("往年 %1 月 %2–%3°C · 约 %4 天下雨").arg(c.month).arg(c.lo).arg(c.hi).arg(c.rainDays))
                        if (s) a.push(s.polar === "day" ? qsTr("极昼") : s.polar === "night" ? qsTr("极夜") : qsTr("日出 %1 · 日落 %2").arg(s.rise).arg(s.set))
                        return a.join("\n")
                    }
                }
                Text {
                    objectName: "sheetIntroState"
                    visible: text !== ""
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                    text: intro.info.state === "loading" ? qsTr("正在查介绍…") : intro.info.state === "offline" ? qsTr("没联网") : intro.info.state === "none" ? qsTr("没找到这个地方的介绍") : ""
                }
                Flow {   // 外部链接：只在浏览器里打开
                    id: extLinks
                    width: parent.width
                    spacing: Theme.px(6)
                    readonly property string q: root.stop ? (root.stop.name || "") : ""
                    TpButton { compact: true; fontSize: 13; text: qsTr("看点评"); onClicked: Qt.openUrlExternally("https://www.google.com/maps/search/?api=1&query=" + encodeURIComponent(extLinks.q + " " + root.stop.lat.toFixed(5) + "," + root.stop.lng.toFixed(5))) }
                    TpButton { compact: true; fontSize: 13; text: qsTr("搜门票"); onClicked: Qt.openUrlExternally("https://www.bing.com/search?q=" + encodeURIComponent(extLinks.q + " " + qsTr("门票"))) }
                    TpButton { compact: true; fontSize: 13; text: qsTr("旅行指南"); visible: !!intro.info.voyageUrl; onClicked: Qt.openUrlExternally(intro.info.voyageUrl) }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.family: serifFamily; font.pixelSize: Theme.px(11); color: Theme.muted
                    text: qsTr("资料：维基百科（CC BY-SA）· Wikimedia Commons · OpenStreetMap · Wikidata · Open-Meteo")
                }
            }
        }
    }

    TpButton {
        id: fullBtn
        objectName: "sheetFull"
        x: Theme.px(22)
        y: parent.height - height - Theme.px(14)
        width: parent.width - Theme.px(44)
        compact: true
        kind: "primary"
        text: qsTr("打开完整计划")
        onClicked: root.fullRequested(root.sheet.id)
    }

    // ---- ⋯ 菜单 ----
    Rectangle {
        id: moreMenu
        property bool open: false
        visible: open
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(18)
        y: head.y + Theme.px(42)
        width: menuCol.implicitWidth + Theme.px(12)
        height: menuCol.implicitHeight + Theme.px(12)
        radius: Theme.radiusSmall
        color: "#fbfcfd"
        border.width: 1; border.color: Theme.line
        z: 5
        Column {
            id: menuCol
            x: Theme.px(6); y: Theme.px(6)
            MenuRow { text: root.sheet.pinned ? qsTr("取消置顶") : qsTr("置顶"); onClicked: { moreMenu.open = false; root.togglePin(root.sheet.id) } }
            MenuRow {
                visible: root.sheet.status !== "wish"
                text: root.sheet.isNext ? qsTr("取消下次出发") : qsTr("设为下次出发")
                onClicked: { moreMenu.open = false; root.toggleNext(root.sheet.id, !root.sheet.isNext) }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.line; visible: !root.sheet.readOnly }
            MenuRow { objectName: "sheetExport"; text: qsTr("导出…"); onClicked: { moreMenu.open = false; win.openExport(root.sheet.id, "itinerary", "pdf") } }   // 0.14.0
            MenuRow { visible: !root.sheet.readOnly; text: qsTr("删除计划…"); danger: true; onClicked: { moreMenu.open = false; root.deleteRequested(root.sheet.id, root.sheet.name || "") } }
        }
    }

    component NotesBlock: Column {
        id: nb
        property string text: ""
        property bool open: false
        signal toggled()
        width: parent ? parent.width : 0
        spacing: Theme.px(4)
        LinkText { text: (nb.open ? "▾ " : "▸ ") + qsTr("备注"); muted: true; onClicked: nb.toggled() }
        Text {
            visible: nb.open
            width: nb.width
            wrapMode: Text.Wrap
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
            lineHeight: 1.3
            text: nb.text
        }
    }
}
