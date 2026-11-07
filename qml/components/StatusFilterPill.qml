// 左下角的状态筛选胶囊（0.8.7，S-11）+ 末尾的"筛选"按钮（M-20；PO 2026-11-05：筛选下拉先挂在这里，
// 0.8.8 的"我的旅程"列表头再放一个入口打开同一个下拉）。
// 照 Electron mapfirst.js renderLayers / bindBottom + mapfirst.css .mf-layers：三颗按钮各管一个状态（和筛选里的状态勾选是同一份），
// 选中的垫一颗半透明玻璃胶囊——出现时从后面冒出来放大、到位回弹，取消时淡出；"动效"关掉时直接切。
// 筛选按钮上的角标 = 被排除掉的选项数（Electron #filterCount）。
// 0.8.10：跟着地图模式变（PO 选 ② B）——计划 / 想去模式是"也显示"+ 另外两个状态（固定显示的那个不列出来）；
// 足迹模式是一句"只看已完成的旅程"，照片模式是一句说明；后面都是"筛选"。状态 = 模式的"也显示"（plans.toggleFilter("status") 改的就是它）。
// 0.9.7（PO 15:15 选方案 A，左上模式 / 左下胶囊去重复）：四种模式都只留"筛选"；"叠上其他旅程"挪进筛选弹窗最下面，
// 开着时按钮上加一个小圆点（不算进角标数字）。
import QtQuick
import TPApp

Item {
    id: root
    property Item backdrop: null
    property bool menuOpen: false
    signal menuClicked()
    implicitHeight: Theme.px(44)
    implicitWidth: row.implicitWidth + Theme.px(12)

    GlassPanel {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: height / 2
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.42 : 0.96)
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.px(2)
        Item {
            id: fbtn
            objectName: "filterButton"
            width: frow.implicitWidth + Theme.px(24)
            height: Theme.px(32)
            anchors.verticalCenter: parent.verticalCenter
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("筛选")
            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: root.menuOpen ? Qt.rgba(1, 1, 1, 0.95) : (fa.containsMouse ? Qt.rgba(1, 1, 1, 0.45) : "transparent")
                border.width: root.menuOpen ? 1 : 0
                border.color: Qt.rgba(196 / 255, 208 / 255, 217 / 255, 0.9)
            }
            Row {
                id: frow
                anchors.centerIn: parent
                spacing: Theme.px(6)
                Canvas {   // 漏斗
                    width: Theme.px(15)
                    height: width
                    anchors.verticalCenter: parent.verticalCenter
                    onPaint: {
                        const c = getContext("2d"), k = width / 24
                        c.reset()
                        c.strokeStyle = Theme.text
                        c.lineWidth = 2 * k
                        c.lineJoin = "round"
                        c.beginPath()
                        c.moveTo(4 * k, 5 * k); c.lineTo(20 * k, 5 * k); c.lineTo(14 * k, 13 * k); c.lineTo(14 * k, 18 * k)
                        c.lineTo(10 * k, 20 * k); c.lineTo(10 * k, 13 * k); c.closePath()
                        c.stroke()
                    }
                }
                Text {
                    text: qsTr("筛选")
                    font.family: serifFamily
                    font.pixelSize: Theme.px(13)
                    color: Theme.text
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {   // 0.9.7：叠上了其他旅程
                    objectName: "overlayDot"
                    visible: plans.overlayLit
                    width: Theme.px(7)
                    height: width
                    radius: width / 2
                    color: Theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    visible: plans.excludedCount > 0
                    width: Math.max(height, cnt.implicitWidth + Theme.px(12))
                    height: Theme.px(18)
                    radius: height / 2
                    color: Theme.accentSoft
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        id: cnt
                        anchors.centerIn: parent
                        text: plans.excludedCount
                        font.family: serifFamily
                        font.pixelSize: Theme.px(11.5)
                        font.bold: true
                        color: Theme.accentInk
                    }
                }
            }
            MouseArea {
                id: fa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.menuClicked()
            }
        }
    }
}
