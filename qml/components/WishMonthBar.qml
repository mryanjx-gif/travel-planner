// 想去模式底部的月份条（0.11.0，H-03）：12 个月，选一个月，列表跟着分组、地图上这个月合适的标记变绿（O2）；
// 月份下面的小数字 = 这个月合适的有几个，当前月份下面一道线。数据是 plans.list（plans::wishInfo）。
import QtQuick
import TPApp

Item {
    id: root
    readonly property int month: plans.wishMonth
    readonly property int nowMonth: +plans.today.slice(5, 7)
    implicitHeight: Theme.px(46)
    implicitWidth: 12 * Theme.px(42) + Theme.px(12)

    GlassPanel { anchors.fill: parent; radius: Theme.px(16) }
    MouseArea { anchors.fill: parent; onWheel: (w) => w.accepted = true }
    Row {
        x: Theme.px(6)
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            model: 12
            delegate: Item {
                id: cell
                required property int index
                readonly property int m: index + 1
                readonly property int fit: (plans.list.months || [])[index] || 0
                objectName: "month-" + m
                width: (root.width - Theme.px(12)) / 12
                height: Theme.px(38)
                Accessible.role: Accessible.RadioButton
                Accessible.name: Fmt.en ? Fmt.monEn[index] : m + " 月"
                Accessible.checked: m === root.month
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Theme.px(1)
                    radius: Theme.px(10)
                    color: cell.m === root.month ? Qt.rgba(1, 1, 1, 0.92) : cMa.containsMouse ? Qt.rgba(1, 1, 1, 0.45) : "transparent"
                    border.width: cell.m === root.month ? 1 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.9)
                }
                Column {
                    anchors.centerIn: parent
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Fmt.en ? Fmt.monEn[cell.index] : cell.m
                        font.family: serifFamily; font.pixelSize: Theme.px(Fmt.en ? 12 : 14); font.bold: cell.m === root.month
                        color: cell.m === root.month ? Theme.text : Theme.textSoft
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cell.fit > 0 ? cell.fit : " "
                        font.family: serifFamily; font.pixelSize: Theme.px(10); font.bold: true; color: "#3f8a5c"
                    }
                }
                Rectangle {
                    visible: cell.m === root.nowMonth
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: Theme.px(14); height: 2; radius: 1
                    color: Theme.accent
                }
                MouseArea { id: cMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: plans.setWishMonth(cell.m) }
            }
        }
    }
}
