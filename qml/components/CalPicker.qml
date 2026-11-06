// 日历标题点开：选年份和月份（0.11.2，C-01；照 Electron pages-calendar.js openPicker）。
// 上面年份 + ‹ ›，一排有计划的年份，下面 12 个月，格子里的彩色小点是这个月的计划。
import QtQuick
import TPApp

Item {
    id: root
    property bool open: false
    property real anchorX: 0
    property real anchorY: 0
    property int year: 2026
    property var info: ({})
    signal picked(int year, int month)
    signal todayClicked()
    anchors.fill: parent
    visible: open

    function show(ax, ay) {
        anchorX = ax; anchorY = ay
        year = calendar.year
        info = calendar.picker()
        open = true
    }

    MouseArea { anchors.fill: parent; onPressed: root.open = false; onWheel: (w) => w.accepted = true }
    Rectangle {
        objectName: "calPicker"
        x: Math.max(Theme.px(10), Math.min(root.width - width - Theme.px(10), root.anchorX - width / 2))
        y: root.anchorY + Theme.px(8)
        width: Theme.px(320)
        height: col.implicitHeight + Theme.px(28)
        radius: Theme.px(14)
        color: "#fbfcfd"
        border.width: 1; border.color: Theme.lineStrong
        MouseArea { anchors.fill: parent }
        Column {
            id: col
            x: Theme.px(14); y: Theme.px(14)
            width: parent.width - Theme.px(28)
            spacing: Theme.px(10)
            Item {
                width: parent.width
                height: Theme.px(34)
                Text { anchors.centerIn: parent; objectName: "calPickerYear"; text: String(root.year); font.family: serifFamily; font.pixelSize: Theme.px(22); font.bold: true; color: Theme.text }
                StepBtn { anchors.left: parent.left; text: "‹"; onClicked: root.year-- }
                StepBtn { anchors.right: parent.right; text: "›"; onClicked: root.year++ }
            }
            Flow {
                visible: (root.info.years || []).length > 0
                width: parent.width
                spacing: Theme.px(4)
                Repeater {
                    model: root.info.years || []
                    delegate: TpChip {
                        required property int modelData
                        on: root.year === modelData
                        text: String(modelData)
                        onClicked: root.year = modelData
                    }
                }
            }
            Grid {
                width: parent.width
                columns: 4
                spacing: Theme.px(6)
                Repeater {
                    model: 12
                    delegate: Rectangle {
                        required property int index
                        readonly property var dots: (root.info.counts || {})[root.year + "-" + Fmt.pad(index + 1)] || []
                        readonly property bool here: root.year === calendar.year && index + 1 === calendar.month
                        readonly property bool thisMonth: plans.today.slice(0, 7) === root.year + "-" + Fmt.pad(index + 1)
                        objectName: "calPickerMonth-" + index
                        width: (parent.width - parent.spacing * 3) / 4
                        height: Theme.px(46)
                        radius: Theme.px(8)
                        color: here ? Theme.accent : monthMa.containsMouse ? Theme.panel2 : "transparent"
                        border.width: thisMonth && !here ? 1 : 0
                        border.color: Theme.accent
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: Theme.px(6)
                            text: Fmt.en ? Fmt.monEn[parent.index] : (parent.index + 1) + " 月"
                            font.family: serifFamily; font.pixelSize: Theme.px(13); color: parent.here ? "#ffffff" : Theme.text
                        }
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: Theme.px(30)
                            spacing: Theme.px(2)
                            Repeater {
                                model: parent.parent.dots
                                delegate: Rectangle {
                                    required property string modelData
                                    width: Theme.px(6); height: width; radius: width / 2
                                    color: modelData
                                    border.width: 1; border.color: "#ffffff"
                                }
                            }
                        }
                        MouseArea { id: monthMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { root.open = false; root.picked(root.year, parent.index + 1) } }
                    }
                }
            }
            LinkText { text: qsTr("回到今天"); onClicked: { root.open = false; root.todayClicked() } }
        }
    }

    component StepBtn: Rectangle {
        id: step
        property string text: ""
        signal clicked()
        width: Theme.px(32); height: width; radius: Theme.px(8)
        color: stepMa.containsMouse ? Theme.panel2 : "transparent"
        Text { anchors.centerIn: parent; text: step.text; font.family: serifFamily; font.pixelSize: Theme.px(20); color: Theme.textSoft }
        MouseArea { id: stepMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: step.clicked() }
    }
}
