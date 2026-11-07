// 顶栏（0.8.8，功能清单 S-06、S-07、S-10）。照 Electron v6.3.0 mapfirst.js build / bindTop / setPages + mapfirst.css #mfTop：
//   - 高 64、左右边距 14、按钮 40；左边 ⋯ 页面胶囊（不在地图页时显示当前页名）+ ↩ 回到地图；
//   - ⋯ 点一下分裂成几颗泡泡（地图 + 各页面 + 下次出发），点一颗去那一页并收回，再点同一颗回地图；Esc / 点别处收回；
//     分裂时 ⋯ 本身缩到 .6 淡出（.mf-pages.open .mf-pill）；
//   - 右边：保存状态（只在有问题时出现）、计划列表按钮、新建计划（0.8.9 编辑器做好后再出现）、头像菜单。
// 原生版和 Electron 不一样的地方（PO 0.8.8 定：顶栏方案 C、⋯ 方案 A、新建计划方案 A）：
//   - ⋯ 上带"下次出发"的倒计时 / 第几天和待办数角标（不点开也看得到）；
//   - 右边的列表按钮和头像合成一颗胶囊；
//   - ⋯ 里只放已经做好的页面（现在是"地图"），计划库 / 日历 / 相册 / 游记 / 愿望清单做好一页加一颗（pages 由 Main 给）。
// 0.8.10（PO 定的页面结构）：⋯ 里 8 颗：计划、想去、足迹、照片（地图的 4 种模式）｜日历、游记、账本（整页）｜下次出发，
//   三组之间多空一点；一行放不下时排到第二行。⋯ 上显示当前模式 / 页名（"计划"是默认的，不显示）；↩ 只在整页、设置时出现。
// 0.11.6（PO 定）：⋯ 横条只有 5 颗：计划 ▾、照片 ▾ ｜ 日历、游记、账本；计划的竖列：计划库、想去、足迹、下次出发；照片的竖列：相册。
//   竖列排在那一颗的正下方，同一套分裂参数。点法（计划 / 照片这一组）在 Main.pickGroup。
// 液态玻璃和泡泡的粘连在阶段 ⑥。
import QtQuick
import TPApp

Item {
    id: root
    property Item backdrop: null
    property string page: "plan"
    property bool onMap: true              // 0.8.10：当前是地图的一种模式（不是整页 / 设置）
    property string pageLabel: ""          // 不在地图页时 ⋯ 旁边显示的页名（只有 ⋯ 里有的页面才有）
    property var pages: []                 // [{ key, name, g, sub }]：⋯ 里的泡泡（横条）；g = 组，组之间多空 10 px；sub = 带竖列（名字后面一个 ▾）
    property var subPages: []              // [{ key, name, owner }]：竖列里的泡泡，排在 owner 那颗的正下方（0.11.6）；"下次出发"由 nowKind 自动补在 plan 的竖列最后
    property var activeKeys: []            // 高亮的泡泡（横条和竖列一起用 key 对）
    property string subOpen: ""            // 哪一颗的竖列开着（空 = 没开）
    readonly property bool subIsOpen: subOpen !== ""
    property string nowKind: "none"        // none / live / next / back
    property string nowLabel: ""
    property string nowBadge: ""
    property int todoOpen: 0
    property bool nowOpen: false
    property bool listOpen: false
    property bool menuOpen: false
    property string saveText: ""           // 空 = 不显示
    property bool saveWarn: false
    readonly property bool bubblesOpen: bubbles.open
    signal pagePicked(string key)
    signal groupClicked(string key)        // 点带竖列的那颗（计划 / 照片）：开竖列还是去地图由 Main 定
    signal subPicked(string key)           // 选了竖列里的一项
    signal openNow()
    signal backClicked()
    signal listClicked()
    signal menuClicked()
    signal newClicked()                    // 0.8.9：新建计划（PO 0.8.8 选方案 C：在右边胶囊里，列表和头像中间）
    property bool showNew: true
    property bool compact: false           // ≤ 900（S-15）："新建计划"只剩 ＋、边距 10
    property bool hidePageLabel: false     // ≤ 1100（S-15）：⋯ 旁边不显示页名

    // 0.11.6：顶栏本身高 barH（Main 的 topH 用它）；Item 自己撑满窗口高，竖列才点得到——Qt 把落在父级范围外的点交不给子元素
    readonly property real barH: Theme.px(64)
    height: parent ? Math.max(barH, parent.height) : barH
    readonly property real budsMaxW: right.x - root.gapX - Theme.px(12)
    readonly property int gapX: Theme.px(compact ? 10 : 14)
    readonly property int btnH: Theme.px(40)

    function setBubbles(on, picked) { bubbles.setOpen(on, picked) }
    function openSub(g) { bubbles.openSub(g) }
    function closeSub(picked) { bubbles.closeSub(picked) }
    function inside(item, gx, gy) {
        if (!item || !item.visible) return false
        const p = item.mapFromItem(null, gx, gy)
        return p.x >= 0 && p.y >= 0 && p.x <= item.width && p.y <= item.height
    }
    // 点别处收回 ⋯ 用：点在 ⋯ 和泡泡上不算
    function bubblesContain(gx, gy) {
        if (inside(dotsPill, gx, gy)) return true
        for (let i = 0; i < buds.count; ++i) if (inside(buds.itemAt(i), gx, gy)) return true
        for (let i = 0; i < subBuds.count; ++i) { const b = subBuds.itemAt(i); if (b && b.interactive && inside(b, gx, gy)) return true }
        return false
    }
    function menuButtonContains(gx, gy) { return inside(meBtn, gx, gy) }
    function listButtonContains(gx, gy) { return inside(listBtn, gx, gy) }
    function nextBudContains(gx, gy) {
        for (let i = 0; i < subBuds.count; ++i) { const b = subBuds.itemAt(i); if (b && b.key === "next" && inside(b, gx, gy)) return true }
        return false
    }

    // ---------------- 左边：⋯ 页面胶囊、泡泡、↩ ----------------
    Item {
        id: left
        x: root.gapX
        y: (root.barH - root.btnH) / 2
        width: dotsPill.width
        height: root.btnH

        Item {
            id: dotsPill
            objectName: "dotsPill"
            width: pillRow.implicitWidth + (pillLabel.visible || pillBadges.visible ? Theme.px(28) : Theme.px(22))
            height: root.btnH
            opacity: bubbles.open ? 0 : 1
            scale: bubbles.open ? 0.6 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.motion ? 250 : 0 } }
            Behavior on scale { NumberAnimation { duration: Theme.motion ? 300 : 0; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curvePanel } }
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("页面")
            GlassPanel {
                anchors.fill: parent
                backdrop: root.backdrop
                radius: height / 2
                tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.82 : 0.96)
                edge: dotsMa.containsMouse ? Theme.lineStrong : Theme.glassEdge
            }
            Row {
                id: pillRow
                anchors.centerIn: parent
                spacing: Theme.px(8)
                TpIcon { name: "dots"; anchors.verticalCenter: parent.verticalCenter; color: root.pageLabel !== "" ? Theme.accentInk : Theme.text }
                Text {
                    id: pillLabel
                    objectName: "dotsLabel"
                    visible: root.pageLabel !== "" && !root.hidePageLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.pageLabel
                    font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.accentInk
                }
                // 下次出发的角标（方案 C）：倒计时 / 第几天 + 这趟没做完的待办数
                Row {
                    id: pillBadges
                    objectName: "dotsBadges"
                    visible: root.nowKind !== "none" && (root.nowBadge !== "" || root.todoOpen > 0)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.px(4)
                    Rectangle {
                        visible: root.nowBadge !== ""
                        width: daysText.implicitWidth + Theme.px(12); height: Theme.px(18); radius: height / 2
                        color: "#f3e3d9"
                        Text { id: daysText; anchors.centerIn: parent; text: root.nowBadge; font.family: serifFamily; font.pixelSize: Theme.px(11); font.bold: true; color: "#8a4210" }
                    }
                    Rectangle {
                        visible: root.todoOpen > 0
                        width: Math.max(height, todoText.implicitWidth + Theme.px(12)); height: Theme.px(18); radius: height / 2
                        color: Theme.accentSoft
                        Text { id: todoText; anchors.centerIn: parent; text: root.todoOpen; font.family: serifFamily; font.pixelSize: Theme.px(11); font.bold: true; color: Theme.accentInk }
                    }
                }
            }
            MouseArea {
                id: dotsMa
                anchors.fill: parent
                hoverEnabled: true
                enabled: !bubbles.open
                cursorShape: Qt.PointingHandCursor
                onClicked: bubbles.setOpen(true)
            }
        }

        // ↩ 回到地图（Electron v5.9.2：一颗圆泡泡，文字进提示）；⋯ 分裂开时让开
        RoundButton {
            id: backBtn
            objectName: "backBtn"
            x: dotsPill.width + Theme.px(8)
            visible: !root.onMap
            opacity: bubbles.open ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.motion ? 200 : 0 } }
            backdrop: root.backdrop
            icon: "uturn"
            iconColor: Theme.accentInk
            tip: qsTr("回到地图")
            onClicked: root.backClicked()
        }

        // 泡泡：从 ⋯ 的左边开始往右排（Electron .mf-chips left: 0），离 ⋯ 近的先出发
        Item {
            id: bubbles
            objectName: "pageBubbles"
            property bool open: false
            readonly property var model: root.pages
            // 竖列：subPages + 下次出发（nowKind 为 none 时不显示）；n = 在这一列里排第几，ownerIndex = 横条里的位置
            readonly property var subModel: {
                const m = root.subPages.slice()
                if (root.nowKind !== "none") m.push({ key: "next", name: root.nowLabel, owner: "plan" })
                const cnt = {}
                return m.map(x => { const n = cnt[x.owner] || 0; cnt[x.owner] = n + 1
                                    return { key: x.key, name: x.name, owner: x.owner, n: n, ownerIndex: root.pages.findIndex(p => p.key === x.owner) } })
            }
            function setOpen(on, picked) {
                if (on === open) return
                open = on
                if (!on) closeSub(picked)
                for (let i = 0; i < buds.count; ++i) {
                    const b = buds.itemAt(i)
                    if (!b) continue
                    if (on) b.go(true, i * 30)
                    else b.go(false, picked && b.key === picked ? 80 : 0)
                }
            }
            // 竖列：同一套分裂 / 回弹 / 收回参数，从所属那颗出发，逐颗往下
            function openSub(g) {
                root.subOpen = g
                for (let i = 0; i < subBuds.count; ++i) { const b = subBuds.itemAt(i); if (b) b.go(b.owner === g, b.owner === g ? b.n * 30 : 0) }
            }
            function closeSub(picked) {
                if (root.subOpen === "") return
                root.subOpen = ""
                for (let i = 0; i < subBuds.count; ++i) { const b = subBuds.itemAt(i); if (b) b.go(false, picked && b.key === picked ? 80 : 0) }
            }
            Repeater {
                id: buds
                model: bubbles.model
                delegate: SplitBud {
                    required property var modelData
                    required property int index
                    readonly property string key: modelData.key
                    objectName: "bud-" + modelData.key
                    backdrop: root.backdrop
                    text: modelData.name
                    on: root.activeKeys.indexOf(key) >= 0
                    caret: modelData.sub === true
                    onColor: Theme.accentInk
                    interactive: bubbles.open
                    // 打开后的位置 = 前一颗的右边 + 6 px（换组时再多 10 px；前一颗先建好；宽度变了跟着挪）；
                    // 超出右边胶囊左边时换到下一行（0.8.10）
                    readonly property int group: modelData.g || 0
                    readonly property var slot: {
                        const prev = index > 0 ? buds.itemAt(index - 1) : null
                        if (!prev) return { x: 0, row: 0 }
                        const x = prev.slot.x + prev.width + Theme.px(prev.group !== group ? 16 : 6)
                        return x + width > root.budsMaxW && prev.slot.x > 0 ? { x: 0, row: prev.slot.row + 1 } : { x: x, row: prev.slot.row }
                    }
                    homeX: slot.x
                    homeY: slot.row * (root.btnH + Theme.px(8))
                    centerX: dotsPill.width / 2
                    width: contentWidth
                    height: root.btnH
                    onClicked: {
                        if (modelData.sub === true) root.groupClicked(key)
                        else { root.pagePicked(key); bubbles.setOpen(false, key) }
                    }
                }
            }
            // 竖列（0.11.6）：排在所属那颗的正下方，homeX 对齐它，homeY 逐颗往下（间距照横条）；起点是那颗所在的行
            Repeater {
                id: subBuds
                model: bubbles.subModel
                delegate: SplitBud {
                    required property var modelData
                    required property int index
                    readonly property string key: modelData.key
                    readonly property string owner: modelData.owner
                    readonly property int n: modelData.n
                    readonly property var parentBud: { buds.count; return buds.itemAt(modelData.ownerIndex) }
                    readonly property var ps: parentBud ? parentBud.slot : ({ x: 0, row: 0 })
                    objectName: "bud-" + modelData.key
                    backdrop: root.backdrop
                    text: modelData.name
                    on: root.activeKeys.indexOf(key) >= 0
                    onColor: Theme.accentInk
                    interactive: root.subOpen === owner
                    homeX: ps.x
                    homeY: (ps.row + n + 1) * (root.btnH + Theme.px(8))
                    centerX: ps.x + (parentBud ? parentBud.width / 2 : 0)
                    centerY: ps.row * (root.btnH + Theme.px(8))
                    width: contentWidth
                    height: root.btnH
                    badges: key === "next" ? [root.nowBadge !== "" ? { text: root.nowBadge, kind: "days" } : null,
                                              root.todoOpen > 0 ? { text: String(root.todoOpen), kind: "count" } : null].filter(x => x) : []
                    onClicked: {
                        if (key === "next") { bubbles.setOpen(false, key); nextLater.restart() }
                        else { root.subPicked(key); bubbles.setOpen(false, key) }
                    }
                }
            }
            // Electron：setPages(false, chip); setTimeout(() => setNowOpen(true), 120)
            Timer { id: nextLater; interval: 120; onTriggered: root.openNow() }
        }
    }

    // ---------------- 右边：保存状态 + 胶囊（列表 | 头像） ----------------
    Row {
        id: right
        anchors.right: parent.right
        anchors.rightMargin: root.gapX
        y: (root.barH - root.btnH) / 2
        spacing: Theme.px(8)

        Item {
            id: savePill
            objectName: "saveState"
            visible: root.saveText !== ""
            width: saveLabel.implicitWidth + Theme.px(28)
            height: root.btnH
            GlassPanel {
                anchors.fill: parent
                backdrop: root.backdrop
                radius: height / 2
                tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.82 : 0.96)
            }
            Text {
                id: saveLabel
                anchors.centerIn: parent
                text: root.saveText
                font.family: serifFamily; font.pixelSize: Theme.px(13)
                color: root.saveWarn ? Theme.warn : Theme.muted
            }
        }

        Item {
            id: capsule
            objectName: "topCapsule"
            width: capRow.implicitWidth + Theme.px(8)
            height: root.btnH
            GlassPanel {
                anchors.fill: parent
                backdrop: root.backdrop
                radius: height / 2
                tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.82 : 0.96)
            }
            Row {
                id: capRow
                anchors.centerIn: parent
                spacing: Theme.px(4)
                Item {
                    id: listBtn
                    objectName: "listBtn"
                    width: Theme.px(36); height: Theme.px(36)
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("计划列表 (Ctrl B)")
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: root.listOpen ? Qt.rgba(1, 1, 1, 0.72) : (listMa.containsMouse ? Qt.rgba(1, 1, 1, 0.45) : "transparent")
                        border.width: root.listOpen ? 1 : 0
                        border.color: Qt.rgba(1, 1, 1, 0.9)
                    }
                    TpIcon { anchors.centerIn: parent; name: "list"; color: root.listOpen ? Theme.accent : Theme.text }
                    MouseArea { id: listMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.listClicked() }
                }
                Rectangle { width: 1; height: Theme.px(18); color: Theme.lineStrong; anchors.verticalCenter: parent.verticalCenter }
                // 新建计划（Electron .mf-new：＋ + 文字；≤ 900 只剩 ＋）
                Item {
                    id: newBtn
                    objectName: "newBtn"
                    visible: root.showNew
                    width: newRow.implicitWidth + Theme.px(root.compact ? 18 : 22)
                    height: Theme.px(32)
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("新建计划")
                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: newMa.pressed ? Theme.accentInk : Theme.accent
                        scale: newMa.pressed ? 0.95 : 1
                        Behavior on scale { NumberAnimation { duration: Theme.motion ? 120 : 0 } }
                    }
                    Row {
                        id: newRow
                        anchors.centerIn: parent
                        spacing: Theme.px(4)
                        TpIcon { name: "plus"; color: "#ffffff"; width: Theme.px(16); height: width; stroke: 2.2; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            visible: !root.compact
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("新建计划")
                            font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: "#ffffff"
                        }
                    }
                    MouseArea { id: newMa; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.newClicked() }
                }
                Item {
                    id: meBtn
                    objectName: "meBtn"
                    width: Theme.px(36); height: Theme.px(36)
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("菜单")
                    Rectangle {
                        anchors.centerIn: parent
                        width: Theme.px(32); height: width; radius: width / 2
                        color: Theme.accentInk
                        border.width: 2
                        border.color: root.menuOpen ? Theme.accentSoft : "#fafcfd"
                        scale: meMa.pressed ? 0.92 : 1
                        Behavior on scale { NumberAnimation { duration: Theme.motion ? 120 : 0 } }
                        TpIcon { anchors.centerIn: parent; name: "person"; color: "#ffffff"; width: Theme.px(17); height: Theme.px(17) }
                    }
                    MouseArea { id: meMa; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.menuClicked() }
                }
            }
        }
    }
}
