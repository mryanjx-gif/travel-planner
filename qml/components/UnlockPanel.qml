// "已解锁"的足迹框和手动标记（0.8.5，功能清单 M-08；Electron v6.3.0 map-app.js renderUnlockPanel / renderUnlockList，
// 样式 pages.css .unlock-panel + mapfirst.css .layout-map .unlock-panel）。只在"已解锁"底图时显示。
//   - 平时：左下角一颗胶囊"已解锁  N 个国家 / 地区 · M 个省 / 州  [手动标记]"
//   - 手动标记：变成左边一列面板——标题 + 完成、说明、搜索框、列表（打勾 = 去过；被计划点亮的打着勾、不能改，标"计划"；
//     手动标记的可以填年份）。没搜索时列出已经手动标记的。在地图上点省份也能切换（FlatMap.qml）。改了 0.7 秒后自动保存。
// 数据和规则都在 C++（mapc.unlockRows / setManual / setManualYear / setUnlockHover）。
import QtQuick
import TPApp

GlassPanel {
    id: root
    property bool editing: mapc.unlockEdit
    property real maxHeight: 600
    radius: editing ? Theme.px(16) : height / 2
    width: editing ? Math.min(Theme.px(340), maxWidth) : Math.min(compactRow.implicitWidth + Theme.px(30), maxWidth)
    property real maxWidth: Theme.px(360)
    height: editing ? maxHeight : compactRow.implicitHeight + Theme.px(16)

    // 列表内容：搜索词、数据（重新上色）变了就重算
    property string query: ""
    property var rows: []
    function refresh() { rows = editing ? mapc.unlockRows(query) : [] }
    onEditingChanged: { if (!editing) { query = ""; search.text = "" } refresh() }
    onQueryChanged: refresh()
    Connections { target: mapc; function onUnlockChanged() { root.refresh() } }

    // ---- 平时 ----
    Row {
        id: compactRow
        visible: !root.editing
        x: Theme.px(14)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.px(10)
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("已解锁")
            font.family: serifFamily
            font.pixelSize: Theme.px(12)
            font.letterSpacing: Theme.px(12) * 0.14
            color: Theme.muted
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            visible: mapc.unlockReady
            spacing: Theme.px(3)
            // Electron：<em>N</em> 个国家 / 地区 · <em>M</em> 个省 / 州（em：19 px、粗体、强调色）
            Text { text: mapc.unlockCountries; font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.accent; id: n1 }
            Text { id: c1; anchors.baseline: n1.baseline; text: qsTr("个国家 / 地区 ·"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text }
            Text { anchors.baseline: n1.baseline; text: mapc.unlockProvinces; font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.accent; leftPadding: Theme.px(3) }
            Text { anchors.baseline: n1.baseline; text: qsTr("个省 / 州"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: !mapc.unlockReady
            text: mapc.error !== "" ? mapc.error : qsTr("正在载入省界数据…")
            font.family: serifFamily
            font.pixelSize: Theme.px(13)
            color: Theme.muted
        }
        TpButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: mapc.unlockReady
            objectName: "unlockEditBtn"
            text: qsTr("手动标记")
            compact: true
            implicitHeight: Theme.px(28)
            onClicked: mapc.unlockEdit = true
        }
    }

    // ---- 手动标记 ----
    Column {
        id: editCol
        visible: root.editing
        x: Theme.px(16)
        y: Theme.px(14)
        width: root.width - Theme.px(32)
        height: root.height - Theme.px(28)
        spacing: Theme.px(8)
        Item {
            width: parent.width
            height: Math.max(doneBtn.height, headText.implicitHeight)
            Text {
                id: headText
                anchors.left: parent.left
                anchors.right: doneBtn.left
                anchors.rightMargin: Theme.px(10)
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("手动标记去过的省 / 州")
                wrapMode: Text.Wrap
                font.family: serifFamily
                font.pixelSize: Theme.px(15)
                font.bold: true
                font.letterSpacing: Theme.px(15) * 0.04
                color: Theme.text
            }
            TpButton {
                id: doneBtn
                anchors.right: parent.right
                text: qsTr("完成")
                kind: "primary"
                compact: true
                onClicked: mapc.unlockEdit = false
            }
        }
        Text {
            width: parent.width
            text: qsTr("在地图上点省份：去过 ↔ 没去过。也可以在这里搜索后打勾。改了自动保存。")
            wrapMode: Text.Wrap
            font.family: serifFamily
            font.pixelSize: Theme.px(12)
            color: Theme.muted
        }
        Rectangle {
            width: parent.width
            height: Theme.px(34)
            radius: Theme.px(8)
            color: Theme.bg
            border.width: search.activeFocus ? 2 : 1
            border.color: search.activeFocus ? Theme.accent : Theme.line
            TextInput {
                id: search
                objectName: "unlockSearch"
                anchors.fill: parent
                anchors.leftMargin: Theme.px(10)
                anchors.rightMargin: Theme.px(10)
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                selectByMouse: true
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.text
                font.family: serifFamily
                font.pixelSize: Theme.px(13.5)
                color: Theme.text
                onTextChanged: root.query = text
                Keys.onEscapePressed: { if (text !== "") text = ""; else { focus = false; mapc.unlockEdit = false } }
            }
            Text {
                anchors.fill: search
                verticalAlignment: Text.AlignVCenter
                visible: search.text === "" && !search.activeFocus
                text: qsTr("搜索省 / 州或国家，如：四川、中国")
                elide: Text.ElideRight
                font: search.font
                color: Theme.faint
            }
        }
        ListView {
            id: list
            width: parent.width + Theme.px(12)
            x: -Theme.px(6)
            height: parent.height - y
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds
            spacing: 0
            delegate: Item {
                id: row
                required property var modelData
                width: list.width
                height: Theme.px(32)
                Rectangle {   // .u-row:hover：强调色 8%
                    anchors.fill: parent
                    radius: Theme.px(8)
                    color: rowHover.hovered ? Qt.rgba(45 / 255, 95 / 255, 139 / 255, 0.08) : "transparent"
                }
                HoverHandler {
                    id: rowHover
                    onHoveredChanged: mapc.setUnlockHover(hovered ? row.modelData.id : "")
                }
                TpCheck {
                    id: chk
                    x: Theme.px(6)
                    anchors.verticalCenter: parent.verticalCenter
                    checked: row.modelData.byPlan || row.modelData.manual
                    enabled: !row.modelData.byPlan
                    onToggled: (on) => mapc.setManual(row.modelData.id, on)
                }
                Text {
                    id: nameText
                    anchors.left: chk.right
                    anchors.leftMargin: Theme.px(6)
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.name
                    font.family: serifFamily
                    font.pixelSize: Theme.px(13.5)
                    color: Theme.text
                    MouseArea { anchors.fill: parent; cursorShape: row.modelData.byPlan ? Qt.ArrowCursor : Qt.PointingHandCursor
                                onClicked: if (!row.modelData.byPlan) mapc.setManual(row.modelData.id, !chk.checked) }
                }
                Text {
                    anchors.left: nameText.right
                    anchors.leftMargin: Theme.px(6)
                    anchors.right: tail.left
                    anchors.rightMargin: Theme.px(6)
                    anchors.baseline: nameText.baseline
                    text: row.modelData.sub
                    elide: Text.ElideRight
                    font.family: serifFamily
                    font.pixelSize: Theme.px(11.5)
                    color: Theme.muted
                }
                Item {
                    id: tail
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.px(6)
                    anchors.verticalCenter: parent.verticalCenter
                    width: row.modelData.byPlan ? tag.width : row.modelData.manual ? Theme.px(56) : 0
                    height: Theme.px(24)
                    // 被计划点亮的：小标签"计划"（悬停看是哪几个计划由提示框不做，Electron 是 title）
                    Rectangle {
                        id: tag
                        visible: row.modelData.byPlan
                        anchors.verticalCenter: parent.verticalCenter
                        width: tagText.implicitWidth + Theme.px(16)
                        height: Theme.px(20)
                        radius: height / 2
                        color: "transparent"
                        border.color: Theme.line
                        Text { id: tagText; anchors.centerIn: parent; text: qsTr("计划"); font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted }
                    }
                    // 手动标记的：年份（四位数字才记）
                    Rectangle {
                        visible: !row.modelData.byPlan && row.modelData.manual
                        anchors.fill: parent
                        radius: Theme.px(6)
                        color: Theme.bg
                        border.width: year.activeFocus ? 2 : 1
                        border.color: year.activeFocus ? Theme.accent : Theme.line
                        TextInput {
                            id: year
                            anchors.fill: parent
                            horizontalAlignment: TextInput.AlignHCenter
                            verticalAlignment: TextInput.AlignVCenter
                            maximumLength: 4
                            inputMethodHints: Qt.ImhDigitsOnly
                            selectByMouse: true
                            text: row.modelData.year
                            font.family: serifFamily
                            font.pixelSize: Theme.px(12.5)
                            color: Theme.text
                            onEditingFinished: { const v = mapc.setManualYear(row.modelData.id, text); if (text !== v) text = v }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: year.text === "" && !year.activeFocus
                            text: qsTr("年份")
                            font: year.font
                            color: Theme.faint
                        }
                    }
                }
            }
            Text {
                visible: list.count === 0
                x: Theme.px(6)
                y: Theme.px(8)
                width: list.width - Theme.px(12)
                wrapMode: Text.Wrap
                text: root.query.trim() !== "" ? qsTr("没有找到。") : qsTr("还没有手动标记。搜索省份或国家，或者直接在地图上点。")
                font.family: serifFamily
                font.pixelSize: Theme.px(13)
                color: Theme.muted
            }
        }
    }
}
