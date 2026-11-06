// "此刻"卡片（0.8.7，S-09；PO 2026-11-05 选方案 B"今天优先"）。照 Electron mapfirst.js renderNow / bindNow 的内容，版式改成：
//   标题行（色点 + 在路上 / 下一次出发 / 刚回来）→ 大标题（今天所在的站 / 计划名）+ 右边"第 3 / 6 天"或倒计时
//   → 在路上时：天数进度条、今天的安排（带时间，最多 3 条）、下一站（交通方式 + 站名）
//   → 按钮："在地图上看"（主按钮）+ ⋯（取消指定）→ 这趟的待办（最多 3 条，可以勾）→ 提醒（日期已过的计划、60 天内到期的证件）。
// 0.10.0 起主按钮是"今天的行程"（在路上）/"打开计划"，"在地图上看"退到第二个。
// 指向还没做的页面的按钮（记一笔、行李与待办、整理照片、写游记、去计划库、看证件）先不显示，
// 页面做好时补上（PO 2026-11-05，记在待办需求记录.md）。
// info = plans.now（plans.cpp nowInfo）。勾待办后先显示勾上，0.45 秒后才真的改数据（Electron setTimeout(renderNow, 450)）。
import QtQuick
import TPApp

Item {
    id: root
    property Item backdrop: null
    property var info: ({})
    signal closeRequested()
    signal showOnMap(string planId)
    signal openTrip(string planId, int stop)    // 0.10.0：今天的行程（stop = 今天所在的站）/ 打开计划（-1）
    signal markDone(string planId, string name)
    signal unsetNext()
    readonly property string kind: info.kind || "none"
    readonly property bool live: kind === "live"
    readonly property real labelW: Theme.px(Fmt.en ? 74 : 44)   // 时间 / "明天"那一列（英文 Tomorrow 更宽）
    width: Theme.px(360)
    height: Math.min(col.implicitHeight + Theme.px(30), maxHeight)
    property real maxHeight: 600
    clip: true

    function legLabel(m) { return m === "train" ? qsTr("火车") : m === "ferry" ? qsTr("船") : m === "drive" ? qsTr("自驾") : m === "walk" ? qsTr("徒步") : qsTr("飞机") }
    function countdown(n) { return n < 0 ? "" : n === 0 ? qsTr("今天") : n === 1 ? qsTr("明天") : qsTr("%1 天后").arg(n) }
    function containsGlobal(gx, gy) {
        const p = root.mapFromItem(null, gx, gy)
        return visible && p.x >= 0 && p.y >= 0 && p.x <= width && p.y <= height
    }

    GlassPanel {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: Theme.radius
    }
    MouseArea { anchors.fill: parent; onClicked: moreMenu.open = false }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + Theme.px(30)
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: col
            x: Theme.px(18)
            y: Theme.px(16)
            width: root.width - Theme.px(36)
            spacing: 0

            // ---- 标题行 ----
            Row {
                width: parent.width - Theme.px(28)
                spacing: Theme.px(6)
                Rectangle {
                    width: Theme.px(9); height: width; radius: width / 2
                    color: root.info.color || Theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    width: parent.width - Theme.px(15)
                    elide: Text.ElideRight
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); font.bold: true; color: Theme.accentInk
                    text: root.live ? qsTr("在路上") + " · " + (root.info.planName || qsTr("未命名计划"))
                        : root.kind === "next" ? qsTr("下一次出发")
                        : qsTr("刚回来") + " · " + qsTr("%1 天前").arg(root.info.daysAgo)
                }
            }
            // ---- 大标题 + 右边的天数 ----
            Item {
                width: parent.width
                height: Math.max(title.implicitHeight, side.implicitHeight) + Theme.px(6)
                Text {
                    id: title
                    y: Theme.px(6)
                    width: parent.width - side.implicitWidth - Theme.px(10)
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.family: serifFamily; font.pixelSize: Theme.px(22); font.bold: true; color: Theme.text
                    text: root.live ? (root.info.here || "") : (root.info.planName || qsTr("未命名计划"))
                }
                Text {
                    id: side
                    anchors.right: parent.right
                    anchors.baseline: title.baseline
                    textFormat: Text.StyledText
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
                    text: root.live ? qsTr("第 <b>%1</b> / %2 天").arg(root.info.dayN).arg(root.info.dayTotal)
                        : root.kind === "next" ? ("<b>" + root.countdown(root.info.daysUntil) + "</b>") : ""
                }
            }
            Text {   // 日期
                visible: !root.live
                width: parent.width
                topPadding: Theme.px(4)
                font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
                text: root.kind === "next" ? (root.info.dated ? Fmt.mdw(root.info.startDate) + " — " + Fmt.mdw(root.info.endDate) : qsTr("还没定日期"))
                    : Fmt.range(root.info.startDate, root.info.endDate)
            }
            // ---- 天数进度条（在路上）：超过 21 天时画成一整条 ----
            Item {
                visible: root.live
                width: parent.width
                height: Theme.px(17)
                readonly property int total: Math.max(1, root.info.dayTotal || 1)
                readonly property int n: root.info.dayN || 0
                Row {
                    visible: parent.total <= 21
                    y: Theme.px(10)
                    spacing: Theme.px(4)
                    Repeater {
                        model: parent.parent.total <= 21 ? parent.parent.total : 0
                        delegate: Rectangle {
                            required property int index
                            width: (col.width - (parent.parent.total - 1) * Theme.px(4)) / parent.parent.total
                            height: Theme.px(5)
                            radius: height / 2
                            color: index < parent.parent.n ? (root.info.color || Theme.accent) : Theme.line
                            opacity: index < parent.parent.n - 1 ? 0.45 : 1
                        }
                    }
                }
                Rectangle {
                    visible: parent.total > 21
                    y: Theme.px(10)
                    width: parent.width; height: Theme.px(5); radius: height / 2
                    color: Theme.line
                    Rectangle { width: parent.width * Math.min(1, parent.parent.n / parent.parent.total); height: parent.height; radius: parent.radius; color: root.info.color || Theme.accent }
                }
            }
            // ---- 今天的安排 + 下一站（在路上） ----
            Column {
                visible: root.live
                width: parent.width
                topPadding: Theme.px(12)
                spacing: Theme.px(6)
                readonly property var items: root.info.todayItems || []
                Text {
                    visible: parent.items.length === 0
                    font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
                    text: qsTr("今天还没有安排")
                }
                Repeater {
                    model: parent.items.slice(0, 3)
                    delegate: Row {
                        required property var modelData
                        spacing: Theme.px(12)
                        width: col.width
                        Text {
                            width: root.labelW
                            font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
                            text: modelData.time || "·"
                        }
                        Text {
                            width: col.width - root.labelW - Theme.px(12)
                            elide: Text.ElideRight
                            font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.text
                            text: modelData.text || modelData.stop || ""
                        }
                    }
                }
                Text {
                    visible: parent.items.length > 3
                    leftPadding: root.labelW + Theme.px(12)
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                    text: qsTr("还有 %1 项").arg(parent.items.length - 3)
                }
                Row {
                    visible: root.info.next !== undefined
                    spacing: Theme.px(12)
                    width: col.width
                    Text {
                        width: root.labelW
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
                        text: root.info.next ? (root.info.next.tomorrow ? qsTr("明天") : Fmt.md(root.info.next.arriveDate)) : ""
                    }
                    Text {
                        width: col.width - root.labelW - Theme.px(12)
                        elide: Text.ElideRight
                        textFormat: Text.StyledText
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.text
                        text: root.info.next ? root.legLabel(root.info.next.leg ? root.info.next.leg.mode : "") + " → <b>" + root.info.next.name + "</b>" : ""
                    }
                }
            }

            // ---- 按钮 ----
            Item {
                width: parent.width
                height: Theme.px(34) + Theme.px(14)
                Rectangle {
                    id: openBtn
                    objectName: "nowOpenTrip"
                    y: Theme.px(14)
                    width: openLbl.implicitWidth + Theme.px(28)
                    height: Theme.px(34)
                    radius: height / 2
                    color: openMa.pressed ? Theme.accentInk : Theme.accent
                    Text { id: openLbl; anchors.centerIn: parent; text: root.live ? qsTr("今天的行程") : qsTr("打开计划"); font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: "#ffffff" }
                    MouseArea { id: openMa; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.openTrip(root.info.planId, root.live ? root.info.hereIndex : -1) }
                    Accessible.role: Accessible.Button; Accessible.name: openLbl.text
                }
                Rectangle {
                    id: mapBtn
                    objectName: "nowShowOnMap"
                    x: openBtn.width + Theme.px(8)
                    y: Theme.px(14)
                    width: mapLbl.implicitWidth + Theme.px(24)
                    height: Theme.px(34)
                    radius: height / 2
                    color: mapMa.containsMouse ? "#ffffff" : Theme.panel
                    border.width: 1; border.color: Theme.lineStrong
                    Text { id: mapLbl; anchors.centerIn: parent; text: qsTr("在地图上看"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text }
                    MouseArea { id: mapMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.showOnMap(root.info.planId) }
                    Accessible.role: Accessible.Button; Accessible.name: mapLbl.text
                }
                Rectangle {
                    id: moreBtn
                    y: Theme.px(14)
                    anchors.right: parent.right
                    width: Theme.px(34); height: width; radius: width / 2
                    color: moreMa.containsMouse || moreMenu.open ? "#ffffff" : Theme.panel
                    border.width: 1; border.color: Theme.lineStrong
                    Accessible.role: Accessible.Button; Accessible.name: qsTr("更多")
                    Row {
                        anchors.centerIn: parent
                        spacing: Theme.px(3)
                        Repeater { model: 3; delegate: Rectangle { width: Theme.px(3.2); height: width; radius: width / 2; color: Theme.text } }
                    }
                    MouseArea { id: moreMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: moreMenu.open = !moreMenu.open }
                }
            }
            Item {   // ⋯ 菜单：取消指定
                id: moreMenu
                property bool open: false
                width: parent.width
                height: open ? Theme.px(44) : 0
                clip: true
                Behavior on height { enabled: Theme.motion; NumberAnimation { duration: 160 } }
                Rectangle {
                    anchors.right: parent.right
                    y: Theme.px(6)
                    width: unsetLbl.implicitWidth + Theme.px(28)
                    height: Theme.px(34)
                    radius: Theme.radiusSmall
                    color: unsetMa.containsMouse ? "#ffffff" : Theme.panel
                    border.width: 1; border.color: Theme.line
                    Text { id: unsetLbl; anchors.centerIn: parent; text: qsTr("取消指定下次出发"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text }
                    MouseArea { id: unsetMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { moreMenu.open = false; root.unsetNext() } }
                }
            }

            // ---- 这趟的待办 ----
            Column {
                visible: todoRep.count > 0
                width: parent.width
                topPadding: Theme.px(12)
                spacing: Theme.px(2)
                Rectangle { width: parent.width; height: 1; color: Theme.line }
                Item { width: 1; height: Theme.px(6) }
                Repeater {
                    id: todoRep
                    model: root.info.todos || []
                    delegate: Item {
                        id: todo
                        required property var modelData
                        property bool ticked: false
                        width: col.width
                        height: Math.max(check.implicitHeight, Theme.px(26))
                        TpCheck {
                            id: check
                            width: parent.width - due.implicitWidth - Theme.px(8)
                            anchors.verticalCenter: parent.verticalCenter
                            text: todo.modelData.text
                            checked: todo.ticked
                            onToggled: (c) => { todo.ticked = c; tickTimer.restart() }
                        }
                        Text {
                            id: due
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: Fmt.ok(todo.modelData.due)
                            font.family: serifFamily; font.pixelSize: Theme.px(12)
                            font.bold: !!todo.modelData.late
                            color: todo.modelData.late ? "#b3261e" : todo.modelData.soon ? "#a0521a" : Theme.muted
                            text: Fmt.md(todo.modelData.due)
                        }
                        Timer { id: tickTimer; interval: 450; onTriggered: plans.setTodoDone(root.info.planId, todo.modelData.id, todo.ticked) }
                    }
                }
            }

            // ---- 提醒 ----
            Column {
                visible: noteRep.count > 0
                width: parent.width
                topPadding: Theme.px(8)
                spacing: Theme.px(6)
                Repeater {
                    id: noteRep
                    model: root.info.notices || []
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool doc: modelData.kind === "doc"
                        width: col.width
                        height: noteText.implicitHeight + Theme.px(14)
                        radius: Theme.radiusControl
                        color: doc ? "#f7ece3" : Theme.panel2
                        Text {
                            id: noteText
                            x: Theme.px(10)
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.px(20) - (link.visible ? link.implicitWidth + Theme.px(8) : 0)
                            wrapMode: Text.Wrap
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5)
                            color: parent.doc ? "#8a4210" : Theme.text
                            text: modelData.kind === "done" ? qsTr("「%1」日期已经过了").arg(modelData.name || qsTr("未命名计划"))
                                : modelData.kind === "more" ? qsTr("另有 %1 个计划日期已经过了").arg(modelData.n)
                                : (modelData.label || qsTr("证件")) + "  " + (modelData.n < 0 ? qsTr("已经过期")
                                   : modelData.n === 0 ? qsTr("今天到期") : qsTr("%1 到期（%2 天后）").arg(Fmt.md(modelData.expires)).arg(modelData.n))
                        }
                        LinkText {
                            id: link
                            visible: modelData.kind === "done"
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.px(10)
                            anchors.verticalCenter: parent.verticalCenter
                            small: true
                            text: qsTr("标为已完成")
                            onClicked: root.markDone(modelData.planId, modelData.name || "")
                        }
                    }
                }
            }
        }
    }

    // 收起
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.px(8)
        width: Theme.px(28); height: width
        radius: Theme.px(8)
        color: closeMa.containsMouse ? Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.06) : "transparent"
        Accessible.role: Accessible.Button; Accessible.name: qsTr("收起")
        Text { anchors.centerIn: parent; text: "×"; font.pixelSize: Theme.px(18); color: Theme.muted }
        MouseArea { id: closeMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
    }
}
