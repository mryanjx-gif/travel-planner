// AI 的修改预览（0.13.1，I-04 / I-05；Electron assistant.js proposalHtml / changeHtml / gapsHtml）。
// p = ai.messages[i].props[k]（aitools.cpp view()）：按计划分组逐条勾选、默认全勾；找不到的目标标红、不能勾；
// add_stop 显示坐标核对（对不上时选一个）；一键应用 / 不应用；应用过 / 已撤销 / 已过期只留一行。下面是"还缺 / 没查"。
import QtQuick
import TPApp

Column {
    id: root
    property var p: ({})
    readonly property string st: p.state || ""
    readonly property bool open: st === "open"
    spacing: Theme.px(6)

    // ---- 预览卡片 ----
    Rectangle {
        objectName: "aiProp"
        visible: root.st !== "gapsonly" && root.st !== "empty" && root.st !== ""
        width: root.width
        height: card.height + Theme.px(20)
        radius: Theme.radiusSmall
        color: root.open ? "#ffffff" : Qt.rgba(1, 1, 1, 0.55)
        border.width: 1
        border.color: root.open ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45) : Theme.line
        Column {
            id: card
            x: Theme.px(12); y: Theme.px(10)
            width: parent.width - Theme.px(24)
            spacing: Theme.px(6)
            // 已应用 / 已撤销 / 没应用 / 已过期 / 截断：一行
            Text {
                objectName: "aiPropState"
                visible: !root.open
                width: parent.width
                wrapMode: Text.Wrap
                text: root.st === "applied" ? qsTr("✓ 已应用 %1 项。按 Ctrl+Z 可以撤销（光标不在输入框里时）。").arg(root.p.applied)
                    : root.st === "undone" ? qsTr("应用过 %1 项，已经撤销（Ctrl+Y 可以重做）。").arg(root.p.applied)
                    : root.st === "skipped" ? qsTr("没有应用。")
                    : root.st === "expired" ? qsTr("之前对话里的修改预览（%1 项），已经过期。要用的话请让它重新提一次。").arg(root.p.total)
                    : root.st === "truncated" ? qsTr("这份修改太长，写到一半被截断了，没法用。可以说“一次只改一个计划，攻略写短一点”再试。") : ""
                font.family: serifFamily; font.pixelSize: Theme.px(12.5)
                color: root.st === "applied" ? Theme.ok : root.st === "truncated" ? Theme.danger : Theme.muted
            }
            Item {
                visible: root.open
                width: parent.width
                height: Theme.px(20)
                Text { text: qsTr("修改预览"); font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1 项").arg(root.p.total || 0) + (root.p.nDel ? " · " + qsTr("其中删除 %1 项").arg(root.p.nDel) : "")
                    font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: root.p.nDel ? Theme.warn : Theme.muted
                }
            }
            Text {
                visible: root.open && (root.p.summary || "") !== ""
                width: parent.width
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: root.p.summary || ""
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
            }
            Repeater {
                model: root.open ? root.p.groups : []
                delegate: Column {
                    id: grp
                    required property var modelData
                    width: card.width
                    spacing: Theme.px(4)
                    Text {
                        visible: grp.modelData.head
                        width: parent.width
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: grp.modelData.name
                        font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.muted
                        topPadding: Theme.px(2)
                    }
                    Repeater {
                        model: grp.modelData.rows
                        delegate: Item {
                            id: row
                            required property var modelData
                            objectName: "aiChange-" + modelData.k
                            readonly property bool bad: (modelData.bad || "") !== ""
                            readonly property bool checking: !!modelData.geo && modelData.geo.state === "checking"
                            width: grp.width
                            height: Math.max(Theme.px(22), rowCol.height)
                            TpCheck {
                                id: chk
                                y: Theme.px(1)
                                checked: row.modelData.on
                                enabled: !row.bad && !row.checking
                                onToggled: ai.toggleChange(root.p.mi, root.p.pi, row.modelData.k)
                            }
                            Column {
                                id: rowCol
                                x: chk.width + Theme.px(8)
                                width: row.width - x
                                spacing: Theme.px(3)
                                Text {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    textFormat: Text.StyledText
                                    text: "<b>" + row.modelData.label + "</b>  " + String(row.modelData.desc).replace(/&/g, "&amp;").replace(/</g, "&lt;")
                                    font.family: serifFamily; font.pixelSize: Theme.px(12.5)
                                    color: row.bad ? Theme.danger : row.modelData.del ? Theme.dangerInk : Theme.text
                                    font.strikeout: row.bad
                                }
                                Text {
                                    objectName: "aiBad"
                                    visible: row.bad
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    text: row.modelData.bad || ""
                                    font.family: serifFamily; font.pixelSize: Theme.px(11.5); font.bold: true; color: Theme.danger
                                }
                                // 坐标核对（add_stop）
                                Text {
                                    visible: !!row.modelData.geo
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    text: row.modelData.geo ? row.modelData.geo.text : ""
                                    font.family: serifFamily; font.pixelSize: Theme.px(11.5)
                                    color: row.modelData.geo && row.modelData.geo.state === "ok" ? Theme.ok : Theme.muted
                                }
                                Repeater {
                                    model: row.modelData.geo && row.modelData.geo.state !== "ok" && row.modelData.geo.state !== "checking"
                                           ? row.modelData.geo.cands.concat(row.modelData.geo.ai !== "" ? [qsTr("用 AI 给的坐标（%1），备注里标“坐标待核实”").arg(row.modelData.geo.ai)] : []) : []
                                    delegate: Item {
                                        id: cand
                                        required property var modelData
                                        required property int index
                                        readonly property int j: index < row.modelData.geo.cands.length ? index : -1
                                        width: rowCol.width
                                        height: candText.height + Theme.px(4)
                                        Rectangle {
                                            id: dot
                                            y: Theme.px(3)
                                            width: Theme.px(12); height: width; radius: width / 2
                                            color: "transparent"
                                            border.width: 1; border.color: Theme.lineStrong
                                            Rectangle { anchors.centerIn: parent; width: Theme.px(6); height: width; radius: width / 2; color: Theme.accent; visible: row.modelData.geo.pick === cand.j }
                                        }
                                        Text {
                                            id: candText
                                            x: dot.width + Theme.px(6)
                                            width: parent.width - x
                                            wrapMode: Text.Wrap
                                            textFormat: Text.PlainText
                                            text: cand.modelData
                                            font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.textSoft
                                        }
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ai.pickGeo(root.p.mi, root.p.pi, row.modelData.k, cand.j) }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // 备份失败：问还要不要应用
            Text {
                visible: root.open && (root.p.warn || "") !== ""
                width: parent.width
                wrapMode: Text.Wrap
                text: root.p.warn || ""
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.warn
            }
            Item {
                visible: root.open
                width: parent.width
                height: Theme.px(30)
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.p.nOn === root.p.nOk ? qsTr("全不选") : qsTr("全选")
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.accent
                    MouseArea { anchors.fill: parent; anchors.margins: -Theme.px(4); cursorShape: Qt.PointingHandCursor; onClicked: ai.toggleAll(root.p.mi, root.p.pi) }
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.px(6)
                    TpButton { objectName: "aiSkip"; compact: true; text: qsTr("不应用"); onClicked: ai.skipProposal(root.p.mi, root.p.pi) }
                    TpButton {
                        objectName: "aiApply"
                        compact: true
                        kind: "primary"
                        enabled: root.p.nOn > 0 && !root.p.busyGeo
                        text: (root.p.warn || "") !== "" ? qsTr("仍然应用") : qsTr("应用 %1 项").arg(root.p.nOn || 0)
                        onClicked: ai.applyProposal(root.p.mi, root.p.pi, (root.p.warn || "") !== "")
                    }
                }
            }
        }
    }

    // ---- 还缺 / 没查 ----
    Rectangle {
        objectName: "aiGaps"
        visible: (root.p.gaps || []).length > 0
        width: root.width
        height: gapCol.height + Theme.px(18)
        radius: Theme.radiusSmall
        color: Qt.rgba(1, 1, 1, 0.55)
        border.width: 1; border.color: Theme.line
        readonly property bool gapOpen: root.p.gapState === "open" && !ai.busy
        Column {
            id: gapCol
            x: Theme.px(12); y: Theme.px(9)
            width: parent.width - Theme.px(24)
            spacing: Theme.px(4)
            Item {
                width: parent.width
                height: Theme.px(20)
                Text { text: qsTr("还缺 / 没查"); font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: root.p.gapState === "sent" ? qsTr("已请它补充") : root.p.gapState === "closed" ? qsTr("先这样") : ""
                    font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
                }
            }
            Repeater {
                model: root.p.gaps || []
                delegate: Item {
                    id: gap
                    required property var modelData
                    required property int index
                    width: gapCol.width
                    height: Math.max(Theme.px(22), gapText.height + Theme.px(2))
                    TpCheck { id: gchk; y: Theme.px(1); checked: gap.modelData.on; enabled: root.p.gapState === "open" && !ai.busy; onToggled: ai.toggleGap(root.p.mi, root.p.pi, gap.index) }
                    Text {
                        id: gapText
                        x: gchk.width + Theme.px(8)
                        width: parent.width - x
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        text: gap.modelData.text + (gap.modelData.search ? "  · " + qsTr("要联网查") : "")
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                    }
                }
            }
            Row {
                visible: root.p.gapState === "open" && !ai.busy
                anchors.right: parent.right
                spacing: Theme.px(6)
                TpButton { compact: true; text: qsTr("先这样"); onClicked: ai.gapsSkip(root.p.mi, root.p.pi) }
                TpButton { objectName: "aiGapFill"; compact: true; kind: "primary"; text: qsTr("补充选中的"); onClicked: ai.gapsFill(root.p.mi, root.p.pi) }
            }
        }
    }
}
