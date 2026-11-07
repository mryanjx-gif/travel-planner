// 日历的季节表（0.11.3，C-05、C-06、C-07；照 Electron pages-calendar.js seasonHtml）。数据是 calendar.info.season（plans::calSeason）。
// 按区域分组、可折叠（第一次全部展开，记住折叠）；同色相邻的月份连成一段；本月一条竖向光带；标了 ramadan 的行画这一年的斋月（一年可能两段，T1）；
// ✓ = 去过（后台算，算好才打勾）。ListView 只建屏幕上的行（约 100 行）。
import QtQuick
import TPApp

Item {
    id: root
    property var info: ({})
    property bool narrow: false
    signal openPlan(string id)
    signal hoverText(bool on, Item item, string text)

    readonly property var rows: info.rows || []
    readonly property bool ready: info.ready === true
    readonly property int curMonth: new Date().getMonth() + 1
    readonly property real nameW: narrow ? Theme.px(84) : Theme.px(Fmt.en ? 164 : 132)
    readonly property real plansW: narrow ? 0 : Theme.px(160)
    readonly property real lineW: width - nameW - plansW
    readonly property real rowH: Theme.px(32)
    function kindLabel(k) { return k === "best" ? qsTr("最适合") : k === "nice" ? qsTr("不错") : k === "meh" ? qsTr("有点热/人多") : k === "bad" ? qsTr("不推荐") : qsTr("表里没写") }
    function monthRange(a, b) { return a === b ? (Fmt.en ? Fmt.monEn[a - 1] : qsTr("%1 月").arg(a)) : Fmt.en ? Fmt.monEn[a - 1] + "–" + Fmt.monEn[b - 1] : qsTr("%1 月").arg(a + "–" + b) }
    function shortDate(s) { const d = Fmt.dt(s); return Fmt.en ? Fmt.monEn[d.getMonth()] + " " + d.getDate() : (d.getMonth() + 1) + "/" + d.getDate() }
    // 折叠、换数据后列表留在原处
    property real keepY: 0
    onRowsChanged: { keepY = list.contentY; Qt.callLater(() => { list.contentY = Math.max(0, Math.min(keepY, list.contentHeight - list.height)) }) }

    // ---- 图例 ----
    Flow {
        id: legend
        width: parent.width
        spacing: Theme.px(12)
        Repeater {
            model: ["best", "nice", "meh", "bad"]
            delegate: Row {
                required property string modelData
                spacing: Theme.px(5)
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: Theme.px(11); height: width; radius: Theme.px(3); color: Theme.seasonColor(parent.modelData) }
                Text { text: root.kindLabel(parent.modelData); font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.textSoft }
            }
        }
        Text {
            objectName: "calSeasonVisitedNote"
            text: root.ready ? qsTr("✓ 去过") : qsTr("✓ 去过（正在计算…）")
            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
        }
        Text {
            objectName: "calSeasonRamNote"
            text: qsTr("斋月是约数：按乌姆库拉历推算，各国看月可能差一两天")
            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
        }
        LinkText { objectName: "calSeasonOpenAll"; small: true; text: qsTr("全部展开"); onClicked: calendar.setAllSeasonGroups(false) }
        LinkText { objectName: "calSeasonCloseAll"; small: true; text: qsTr("全部收起"); onClicked: calendar.setAllSeasonGroups(true) }
    }

    // ---- 月份表头 ----
    Item {
        id: head
        y: legend.height + Theme.px(8)
        width: parent.width
        height: Theme.px(24)
        Repeater {
            model: 12
            delegate: Text {
                required property int index
                objectName: "calSeasonMonth-" + (index + 1)
                x: root.nameW + index / 12 * root.lineW
                width: root.lineW / 12
                horizontalAlignment: Text.AlignHCenter
                text: Fmt.en ? Fmt.monEn[index] : (index + 1) + (root.narrow ? "" : "月")
                font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: index + 1 === root.curMonth
                color: index + 1 === root.curMonth ? Theme.accent : Theme.textSoft
            }
        }
        Text {
            visible: !root.narrow
            x: root.nameW + root.lineW + Theme.px(8)
            text: qsTr("相关计划")
            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
        }
    }

    ListView {
        id: list
        objectName: "calSeasonList"
        y: head.y + head.height
        width: parent.width
        height: parent.height - y
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.rows
        delegate: Loader {
            required property var modelData
            required property int index
            width: list.width
            sourceComponent: modelData.head ? groupHead : seasonRow
        }
    }

    // 本月的竖向光带（盖在表上，不挡鼠标）
    Rectangle {
        objectName: "calSeasonCurBand"
        x: root.nameW + (root.curMonth - 1) / 12 * root.lineW
        y: head.y
        width: root.lineW / 12
        height: parent.height - y
        color: Qt.alpha(Theme.accent, 0.08)
    }

    Component {
        id: groupHead
        Rectangle {
            readonly property var g: parent ? parent.modelData : ({})
            objectName: "calSeasonGroup-" + (g.group || "")
            height: Theme.px(32)
            color: headMa.containsMouse ? Theme.panel2 : Theme.panelLight
            radius: Theme.px(6)
            Text {
                x: Theme.px(8)
                anchors.verticalCenter: parent.verticalCenter
                text: (parent.g.closed ? "▸ " : "▾ ") + qsTr(parent.g.group || "")
                font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
            }
            Text {
                objectName: "calSeasonGroupCount-" + (parent.g.group || "")
                anchors.right: parent.right; anchors.rightMargin: Theme.px(10)
                anchors.verticalCenter: parent.verticalCenter
                text: parent.g.count + (parent.g.visited ? " · " + qsTr("去过 %1").arg(parent.g.visited) : "")
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
            }
            MouseArea {
                id: headMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: calendar.setSeasonGroupClosed(parent.g.group, !parent.g.closed)
            }
        }
    }

    Component {
        id: seasonRow
        Item {
            id: rowItem
            readonly property var r: parent ? parent.modelData : ({})
            readonly property var plans: r.plans || []
            objectName: "calSeasonRow-" + (r.key || "")
            // 标了斋月的行多一条细轨道（字不压在说明上）；窄窗口：相关计划放在最下面
            readonly property real ramH: (r.ram || []).length ? Theme.px(15) : 0
            height: root.rowH + ramH + (root.narrow && plans.length ? Theme.px(26) : 0)
            Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.line }
            Text {
                id: nameText
                x: Theme.px(8)
                width: root.nameW - x - (tick.visible ? tick.width + Theme.px(2) : 0) - Theme.px(4)
                height: root.rowH
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: Fmt.en && rowItem.r.nameEn ? rowItem.r.nameEn : (rowItem.r.name || "")
                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text
                MouseArea { anchors.fill: parent; hoverEnabled: true; onContainsMouseChanged: root.hoverText(containsMouse, nameText, nameText.text) }
            }
            Text {
                id: tick
                objectName: "calSeasonVisited-" + (rowItem.r.key || "")
                visible: rowItem.r.visited === true
                x: nameText.x + Math.min(nameText.implicitWidth, nameText.width) + Theme.px(2)
                height: root.rowH
                verticalAlignment: Text.AlignVCenter
                text: "✓"
                font.pixelSize: Theme.px(13); font.bold: true; color: Theme.seasonColor("best")
            }
            Item {
                x: root.nameW
                width: root.lineW
                height: root.rowH
                Repeater {
                    model: rowItem.r.segs || []
                    delegate: Rectangle {
                        id: seg
                        required property var modelData
                        readonly property string label: Fmt.en ? modelData.textEn : modelData.text
                        x: (modelData.from - 1) / 12 * root.lineW + 1
                        y: Theme.px(3)
                        width: (modelData.to - modelData.from + 1) / 12 * root.lineW - 2
                        height: root.rowH - Theme.px(6)
                        radius: Theme.px(5)
                        color: modelData.kind === "none" ? "transparent" : Qt.alpha(Theme.seasonColor(modelData.kind), modelData.kind === "bad" ? 0.32 : 0.3)
                        Text {
                            visible: seg.modelData.kind !== "none" && !root.narrow
                            x: Theme.px(5)
                            width: parent.width - Theme.px(8)
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: seg.label || root.kindLabel(seg.modelData.kind)
                            font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.text
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onContainsMouseChanged: root.hoverText(containsMouse, seg, root.monthRange(seg.modelData.from, seg.modelData.to) + qsTr("：") + root.kindLabel(seg.modelData.kind) + (seg.label ? " · " + seg.label : ""))
                        }
                    }
                }
                Repeater {   // 斋月：一年可能两段；跨公历年的只画这一年里的部分，字写完整的日期
                    model: rowItem.r.ram || []
                    delegate: Item {
                        id: ram
                        required property var modelData
                        required property int index
                        objectName: "calSeasonRam-" + rowItem.r.key + "-" + index
                        readonly property string range: root.shortDate(modelData.a) + "–" + root.shortDate(modelData.b)
                        x: modelData.l * root.lineW
                        y: root.rowH + Theme.px(2)
                        width: (modelData.r - modelData.l) * root.lineW
                        height: Theme.px(6)
                        Rectangle { anchors.fill: parent; radius: height / 2; color: "#7a5bb5" }
                        Text {
                            objectName: "calSeasonRamText"
                            x: ram.modelData.labelLeft ? -width : ram.width
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: Theme.px(4); rightPadding: Theme.px(4)
                            text: qsTr("斋月约 %1").arg(ram.range)
                            font.family: serifFamily; font.pixelSize: Theme.px(11); color: "#5b3f94"
                        }
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -Theme.px(4)
                            hoverEnabled: true
                            onContainsMouseChanged: root.hoverText(containsMouse, ram, qsTr("斋月约 %1（按乌姆库拉历推算，各国看月可能差一两天）").arg(ram.range))
                        }
                    }
                }
            }
            // ---- 相关计划：最多放两个，多的写 +N，悬停看全部 ----
            Row {
                x: root.narrow ? Theme.px(8) : root.nameW + root.lineW + Theme.px(6)
                y: root.narrow ? root.rowH + rowItem.ramH : Theme.px(4)
                width: (root.narrow ? rowItem.width - Theme.px(16) : root.plansW - Theme.px(8))
                height: Theme.px(24)
                spacing: Theme.px(4)
                readonly property int shown: Math.min(rowItem.plans.length, 2)
                readonly property real more: rowItem.plans.length > 2 ? Theme.px(26) : 0
                Repeater {
                    model: rowItem.plans.slice(0, 2)
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        objectName: "calSeasonPlan-" + modelData.id
                        width: Math.min(chipText.implicitWidth + Theme.px(14), (parent.width - parent.more - parent.spacing * 2) / parent.shown)
                        height: Theme.px(24)
                        radius: height / 2
                        color: modelData.wish ? Theme.accentSoft : Theme.panel2
                        Text {
                            id: chipText
                            x: Theme.px(7)
                            width: parent.width - Theme.px(14)
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: chip.modelData.name || qsTr("未命名计划")
                            font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.textSoft
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.openPlan(chip.modelData.id)
                            onContainsMouseChanged: root.hoverText(containsMouse, chip, chipText.text)
                        }
                    }
                }
                Text {
                    id: moreText
                    visible: parent.more > 0
                    width: parent.more
                    height: Theme.px(24)
                    verticalAlignment: Text.AlignVCenter
                    text: "+" + (rowItem.plans.length - 2)
                    font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
                    MouseArea { anchors.fill: parent; hoverEnabled: true; onContainsMouseChanged: root.hoverText(containsMouse, moreText, rowItem.plans.map(p => p.name).join("\n")) }
                }
            }
        }
    }
}
