// 相册（0.11.1，A-01–A-03，照 Electron pages-album.js）：照片模式左边的列表和展开后的整页是同一个组件。
// 视图：按旅程 / 按年份 / 精选 / 未归类，记住上次选的（没有单独的"地图"视图，照片模式本身就是地图）。
// 窄列表（compact）按旅程时每趟一行，点一趟展开这趟的照片、地图飞过去（P1）；整页左边一列选旅程，不飞。
// 行在 C++（photos.album），只建屏幕上的行；看大图翻页的范围是点开时所在的那一组。
import QtQuick
import TPApp

Item {
    id: root
    property bool compact: true
    property real targetWidth: width      // 展开动画的终点宽度：按它算列数，动画途中不重排
    signal openPlan(string id)
    signal importClicked()
    signal showPhotos(var list, int index)
    signal assignRequested(var ids)
    signal flyTo(string planId)           // P1

    readonly property var album: photos.album
    property var selIds: ({})             // 未归类：勾选的
    property real keepY: 0
    readonly property int selCount: Object.keys(selIds).length
    readonly property bool noCodec: album.heic > 0 && photos.revision >= 0 && !photos.heicReady()
    readonly property real asideW: !compact && album.view === "trip" ? Theme.px(210) : 0
    readonly property real gap: Theme.px(8)
    readonly property real tileTarget: compact ? Theme.px(92) : Theme.px(168)
    readonly property int columns: Math.max(1, Math.floor((targetWidth - asideW - Theme.px(32) + gap) / (tileTarget + gap)))
    readonly property real tile: (grid.width - gap * (columns - 1)) / columns

    // 0.12.8 打开相册和照片库变了时数一次（不是每张格子都查）
    property int lostCount: 0
    function recountLost() { if (visible) lostCount = photos.missingCount() }
    onVisibleChanged: recountLost()
    Connections { target: photos; function onLibraryChanged() { root.recountLost() } }
    // 0.12.8 分步提示：缩略图第一次做要时间（后台最多 2 张同时做），告诉人还剩几张
    property int thumbLeft: 0
    Timer { interval: 500; repeat: true; running: root.visible; onTriggered: root.thumbLeft = photos.thumbPending() }

    onCompactChanged: album.compact = compact
    onColumnsChanged: album.columns = columns
    Component.onCompleted: { album.compact = compact; album.columns = columns }

    function setSel(id, on) {
        const s = Object.assign({}, selIds)
        if (on) s[id] = true; else delete s[id]
        selIds = s
    }
    function clearSel() { selIds = ({}) }
    function selAll() { const s = {}; for (const id of album.inboxIds) s[id] = true; selIds = s }
    function pickTrip(id) {
        if (compact) {
            const closing = album.plan === id
            album.plan = closing ? "" : id
            if (closing) grid.positionViewAtIndex(album.tripRow(id), ListView.Contain)   // 收起时也别跳回顶上
            if (!closing && id !== "__nopos") root.flyTo(id)
        } else {
            album.plan = id
        }
    }
    function open(group, id) {
        const r = album.openGroup(group, id)
        root.showPhotos(r.list, r.index)
    }
    function assignSelected() {
        if (selCount === 0) return
        root.assignRequested(Object.keys(selIds))
    }
    function labelOf(p) {
        if (album.view === "inbox") return p.takenAt ? String(p.takenAt).slice(0, 16).replace("T", " ") : qsTr("没有拍摄时间")
        if (album.view === "year" || album.view === "fav") return p.planName || qsTr("未归类")
        return p.caption || ""
    }
    // 退回或数据变了：列表里不再有的勾选去掉
    Connections {
        target: root.album
        function onInfoChanged() {
            const have = {}
            for (const id of root.album.inboxIds) have[id] = true
            let changed = false
            const s = {}
            for (const id in root.selIds) { if (have[id]) s[id] = true; else changed = true }
            if (changed) root.selIds = s
        }
        // 展开的一趟（包括从此刻卡"整理照片"进来的）：滚到看得见
        function onStateChanged() {
            if (!root.compact || !root.album.plan) return
            const i = root.album.tripRow(root.album.plan)
            if (i >= 0) grid.positionViewAtIndex(i, ListView.Beginning)
        }
        function onAboutToRebuild() { root.keepY = grid.contentY }
        function onRebuilt() { const y = root.keepY; Qt.callLater(() => { grid.contentY = Math.max(0, Math.min(y, grid.contentHeight - grid.height)) }) }
    }

    // ---- 上面：统计、视图、导入 ----
    Column {
        id: top
        x: Theme.px(14)
        width: parent.width - 2 * x
        spacing: Theme.px(6)
        Item {
            width: parent.width
            height: Math.max(stat.implicitHeight, importBtn.height)
            Text {
                id: stat
                objectName: "albumStat"
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - importBtn.width - Theme.px(8)
                wrapMode: Text.Wrap
                // 地图上 N 张 · 没有位置 N 张（PO 第 4 题 A）；整页加未归类
                text: (root.compact ? qsTr("地图上 %1 张").arg(root.album.pinned)
                                    : Fmt.one(qsTr("%1 张").arg(root.album.total)) + (root.album.inbox > 0 ? " · " + qsTr("%1 张未归类").arg(root.album.inbox) : ""))
                      + (root.album.noPos > 0 ? " · " + qsTr("没有位置 %1 张").arg(root.album.noPos) : "")
                font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.accent
            }
            TpButton {
                id: importBtn
                objectName: "albumImport"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                compact: true
                kind: "primary"
                text: qsTr("导入照片")
                onClicked: root.importClicked()
            }
        }
        Segmented {
            objectName: "albumViews"
            width: parent.width
            fill: root.compact
            current: root.album.view
            options: [
                { key: "trip", text: qsTr("按旅程") },
                { key: "year", text: qsTr("按年份") },
                { key: "fav", text: root.album.favs > 0 ? qsTr("精选") + " " + root.album.favs : qsTr("精选") },
                { key: "inbox", text: root.album.inbox > 0 ? qsTr("未归类") + " " + root.album.inbox : qsTr("未归类") }
            ]
            onPicked: (k) => { root.clearSel(); root.album.view = k; grid.contentY = 0 }
        }
        // 没装扩展（PO 第 6 题 A）：说清楚去哪装
        Rectangle {
            objectName: "albumHeicHint"
            visible: root.noCodec
            width: parent.width
            height: visible ? hintCol.implicitHeight + Theme.px(14) : 0
            radius: Theme.px(9)
            color: "#fbf1dc"
            Column {
                id: hintCol
                x: Theme.px(10); y: Theme.px(7)
                width: parent.width - 2 * x
                spacing: Theme.px(3)
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: Fmt.one(qsTr("%1 张 HEIC 照片显示不了：这台电脑要装 Microsoft Store 里的两个扩展。HEVC 扩展有的电脑要付费。").arg(root.album.heic))
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: "#7a5a1c"
                }
                Row {
                    spacing: Theme.px(14)
                    LinkText { small: true; text: qsTr("HEIF 图像扩展"); onClicked: photos.openStore("heif") }
                    LinkText { small: true; text: qsTr("HEVC 视频扩展"); onClicked: photos.openStore("hevc") }
                }
            }
        }
        // 0.12.8 东西不见了：说清楚有几张、为什么、怎么办
        Rectangle {
            objectName: "albumLost"
            visible: root.lostCount > 0
            width: parent.width
            height: visible ? lostCol.implicitHeight + Theme.px(16) : 0
            radius: Theme.radiusControl
            color: "#fdf6f0"
            border.width: 1
            border.color: Qt.rgba(194 / 255, 86 / 255, 15 / 255, 0.28)
            Column {
                id: lostCol
                x: Theme.px(10); y: Theme.px(8)
                width: parent.width - 2 * x
                spacing: Theme.px(6)
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: Fmt.one(qsTr("有 %1 张照片找不到文件。记录还在，可能是数据文件夹里的 photos 被移动或删除了，放回原位置就能显示。").arg(root.lostCount))
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.text
                }
                Row {
                    spacing: Theme.px(6)
                    TpButton { compact: true; text: qsTr("打开数据文件夹"); onClicked: app.openDataFolder() }
                    TpButton { objectName: "albumLostCheck"; compact: true; text: qsTr("重新检查"); onClicked: root.lostCount = photos.missingCount() }
                }
            }
        }
        // 未归类：全选、清空、归到计划
        Item {
            objectName: "albumInboxBar"
            visible: root.album.view === "inbox" && root.album.inbox > 0
            width: parent.width
            height: visible ? Theme.px(32) : 0
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("已选 %1 张").arg(root.selCount)
                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.px(6)
                TpButton { objectName: "albumSelAll"; compact: true; text: qsTr("全选"); onClicked: root.selAll() }
                TpButton { compact: true; text: qsTr("清空"); onClicked: root.clearSel() }
                TpButton { objectName: "albumAssign"; compact: true; kind: "primary"; enabled: root.selCount > 0; text: qsTr("归到计划…"); onClicked: root.assignSelected() }
            }
        }
    }

    // ---- 整页左边：选旅程 ----
    ListView {
        id: aside
        objectName: "albumAside"
        visible: root.asideW > 0
        x: Theme.px(10)
        y: top.y + top.height + Theme.px(8)
        width: root.asideW - Theme.px(8)
        height: parent.height - y - Theme.px(10)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        readonly property var entries: {
            const a = [{ id: "", name: qsTr("全部"), count: root.album.total }].concat(root.album.trips)
            if (root.album.noPos > 0) a.push({ id: "__nopos", name: qsTr("没有位置"), count: root.album.noPos })
            return a
        }
        model: entries
        delegate: Item {
            required property var modelData
            width: aside.width
            height: Theme.px(34)
            objectName: "albumAside-" + modelData.id
            readonly property bool on: root.album.plan === modelData.id
            Rectangle { anchors.fill: parent; radius: Theme.px(9); color: parent.on ? Qt.rgba(1, 1, 1, 0.72) : asideMa.containsMouse ? Qt.rgba(1, 1, 1, 0.32) : "transparent" }
            Text {
                x: Theme.px(10)
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - x - asideN.implicitWidth - Theme.px(16)
                elide: Text.ElideRight
                text: modelData.name
                font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: parent.on; color: Theme.text
            }
            Text { id: asideN; anchors.right: parent.right; anchors.rightMargin: Theme.px(10); anchors.verticalCenter: parent.verticalCenter; text: modelData.count; font.pixelSize: Theme.px(12); color: Theme.muted }
            MouseArea { id: asideMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.pickTrip(modelData.id) }
        }
    }

    // ---- 照片 ----
    ListView {
        id: grid
        objectName: "albumGrid"
        x: Theme.px(14) + root.asideW
        y: top.y + top.height + Theme.px(8)
        width: parent.width - x - Theme.px(14)
        height: parent.height - y - Theme.px(8)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        cacheBuffer: Theme.px(200)
        model: root.album
        delegate: Item {
            id: rowItem
            required property int index
            required property string kind
            required property string title
            required property string sub
            required property string planId
            required property int stopNo
            required property int count
            required property int group
            required property var items
            required property bool open
            required property string thumb
            readonly property bool isPhotos: kind === "photos"
            readonly property bool hasLabel: isPhotos && (root.album.view !== "trip" || items.some(p => !!p.caption))
            width: grid.width
            height: isPhotos ? root.tile + (hasLabel ? Theme.px(20) : 0) + root.gap
                  : kind === "trip" ? Theme.px(54)
                  : kind === "plan" ? Theme.px(44)
                  : kind === "year" ? Theme.px(38)
                  : Theme.px(30)

            // 窄列表的一趟（P1：点一下地图飞过去、展开照片）
            Item {
                visible: rowItem.kind === "trip"
                objectName: rowItem.kind === "trip" ? "albumTrip-" + rowItem.planId : ""
                anchors.fill: parent
                Rectangle { anchors.fill: parent; anchors.bottomMargin: Theme.px(2); radius: Theme.px(10); color: rowItem.open ? Qt.rgba(1, 1, 1, 0.72) : tripMa.containsMouse ? Qt.rgba(1, 1, 1, 0.32) : "transparent" }
                Rectangle {
                    id: tripPic
                    x: Theme.px(4)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(44); height: width; radius: Theme.px(8)
                    color: Theme.panel2
                    Image {
                        anchors.fill: parent
                        anchors.margins: Theme.px(1)
                        source: rowItem.kind === "trip" ? rowItem.thumb : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        clip: true
                    }
                }
                Column {
                    x: tripPic.x + tripPic.width + Theme.px(10)
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - x - Theme.px(8)
                    spacing: Theme.px(1)
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: rowItem.planId === "__nopos" ? qsTr("没有位置") : rowItem.title
                        font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: rowItem.open; color: Theme.text
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: Fmt.one(rowItem.planId === "__nopos" ? qsTr("%1 张，地图上没有").arg(rowItem.count) : qsTr("%1 张").arg(rowItem.count))
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                }
                MouseArea { id: tripMa; anchors.fill: parent; enabled: rowItem.kind === "trip"; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.pickTrip(rowItem.planId) }
            }
            // 整页：计划的标题 + 打开计划
            Item {
                visible: rowItem.kind === "plan"
                anchors.fill: parent
                Text {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.px(6)
                    width: parent.width - planLink.width - Theme.px(12)
                    elide: Text.ElideRight
                    text: rowItem.title
                    font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
                }
                LinkText {
                    id: planLink
                    objectName: rowItem.kind === "plan" ? "albumOpenPlan-" + rowItem.planId : ""
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.px(8)
                    small: true
                    text: (rowItem.sub || qsTr("未定日期")) + " · " + qsTr("打开计划 →")
                    onClicked: root.openPlan(rowItem.planId)
                }
            }
            // 站点、年、月
            Text {
                visible: rowItem.kind === "stop" || rowItem.kind === "year" || rowItem.kind === "month"
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.px(5)
                width: parent.width
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: rowItem.kind === "year" ? (rowItem.title ? qsTr("%1 年").arg(rowItem.title) : qsTr("没有日期"))
                    : rowItem.kind === "month" ? (Fmt.en ? Fmt.monEn[Number(rowItem.sub) - 1] : Number(rowItem.sub) + " 月") + "  ·  " + Fmt.one(qsTr("%1 张").arg(rowItem.count))
                    : (rowItem.stopNo === 0 ? qsTr("还没归到站点") : rowItem.sub === "route" ? qsTr("第 %1 站").arg(rowItem.stopNo) + " · " + rowItem.title : rowItem.title) + "  ·  " + Fmt.one(qsTr("%1 张").arg(rowItem.count))
                font.family: serifFamily
                font.pixelSize: Theme.px(rowItem.kind === "year" ? 19 : 13)
                font.bold: rowItem.kind === "year"
                color: rowItem.kind === "year" ? Theme.text : Theme.muted
            }
            // 一行照片
            Row {
                visible: rowItem.isPhotos
                spacing: root.gap
                Repeater {
                    model: rowItem.isPhotos ? rowItem.items : []
                    delegate: Item {
                        id: cell
                        required property var modelData
                        required property int index
                        readonly property var p: modelData
                        objectName: "albumPhoto-" + p.id
                        width: root.tile
                        height: root.tile + (rowItem.hasLabel ? Theme.px(20) : 0)
                        readonly property bool sel: root.selIds[p.id] === true
                        Rectangle {
                            id: pic
                            width: root.tile
                            height: root.tile
                            radius: Theme.px(9)
                            color: Theme.panel2
                            Image {
                                id: img
                                objectName: "albumImg"
                                anchors.fill: parent
                                source: photos.revision >= 0 && String(cell.p.file).startsWith("photos/") ? photos.thumbUrl(cell.p.file, root.tile * Screen.devicePixelRatio > 320 ? 640 : 320) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                clip: true
                                smooth: true
                            }
                            // 缺图、HEIC 没装扩展（A-09；mockup "要装扩展"）
                            Text {
                                objectName: "albumMissing"
                                anchors.centerIn: parent
                                width: parent.width - Theme.px(10)
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                visible: img.status === Image.Error || img.source.toString() === ""
                                text: cell.p.heic && root.noCodec ? qsTr("要装扩展") : qsTr("照片找不到")
                                font.family: serifFamily; font.pixelSize: Theme.px(12); color: cell.p.heic && root.noCodec ? "#7a5a1c" : Theme.muted
                            }
                            MouseArea {
                                id: picMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("看大图")
                                onClicked: root.open(rowItem.group, cell.p.id)
                            }
                            // 未归类：勾选（左上）
                            TpCheck {
                                objectName: "albumSel-" + cell.p.id
                                visible: root.album.view === "inbox"
                                x: Theme.px(6); y: Theme.px(6)
                                checked: cell.sel
                                onToggled: (on) => root.setSel(cell.p.id, on)
                            }
                            // 精选（右上）：精选了一直显示，没有的鼠标放上去才显示
                            Rectangle {
                                objectName: "albumFav-" + cell.p.id
                                visible: cell.p.fav || picMa.containsMouse || favMa.containsMouse
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: Theme.px(5)
                                width: Theme.px(26); height: width; radius: width / 2
                                color: Qt.rgba(0, 0, 0, 0.42)
                                Text { anchors.centerIn: parent; text: cell.p.fav ? "★" : "☆"; font.pixelSize: Theme.px(15); color: cell.p.fav ? "#ffd24a" : "#ffffff" }
                                MouseArea {
                                    id: favMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    Accessible.role: Accessible.Button
                                    Accessible.name: cell.p.fav ? qsTr("取消精选") : qsTr("设为精选")
                                    onClicked: photos.setFav(cell.p.id, !cell.p.fav)
                                }
                            }
                        }
                        Text {
                            visible: rowItem.hasLabel
                            y: pic.height + Theme.px(2)
                            width: parent.width
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: root.labelOf(cell.p)
                            font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
                        }
                    }
                }
            }
        }
    }

    // 一张照片都没有（0.12.7 空状态）
    EmptyState {
        objectName: "emptyAlbum"
        visible: grid.count === 0 && root.album.total === 0
        x: grid.x + Theme.px(8)
        y: grid.y + Math.max(Theme.px(16), (grid.height - height) / 2)
        width: grid.width - Theme.px(16)
        art: "album"
        title: qsTr("还没有照片")
        detail: qsTr("导入照片后，会按拍摄日期和位置自动归到计划里。")
        action: qsTr("导入照片")
        onActed: root.importClicked()
    }
    // 这个视图里没有（照 Electron 的文案）
    Text {
        objectName: "albumEmpty"
        visible: grid.count === 0 && root.album.total !== 0
        x: grid.x + Theme.px(4)
        y: grid.y + Theme.px(14)
        width: grid.width - Theme.px(8)
        wrapMode: Text.Wrap
        lineHeight: 1.4
        text: root.album.view === "fav" ? qsTr("还没有精选。鼠标放在照片上，点右上角的星星。")
            : root.album.view === "inbox" ? qsTr("没有未归类的照片。导入时认不出属于哪个计划的照片会放在这里。")
            : qsTr("还没有归到计划的照片。")
        font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
    }

    Rectangle {
        objectName: "albumThumbProgress"
        visible: root.thumbLeft > 2
        anchors.horizontalCenter: grid.horizontalCenter
        anchors.bottom: grid.bottom
        anchors.bottomMargin: Theme.px(8)
        width: thumbText.implicitWidth + Theme.px(20)
        height: Theme.px(26)
        radius: height / 2
        color: "#e8162a36"
        Text {
            id: thumbText
            anchors.centerIn: parent
            text: qsTr("正在生成缩略图，还有 %1 张").arg(root.thumbLeft)
            font.family: serifFamily; font.pixelSize: Theme.px(12); color: "#ffffff"
        }
    }
}
