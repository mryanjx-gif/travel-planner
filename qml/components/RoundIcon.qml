// 面板头部的圆形小按钮（0.10.0 从 PlanSheet 拆出来，计划页也用）：kind = locate / more / edit / close / prev / next
import QtQuick
import TPApp

Rectangle {
    id: ri
    property string kind: ""
    property string label: ""
    property bool active: false
    property bool plain: false
    property bool small: false
    signal clicked()
    width: Theme.px(small ? 28 : 36)
    height: width
    radius: width / 2
    opacity: enabled ? 1 : 0.35
    color: plain ? (riMa.containsMouse ? Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.06) : "transparent")
                 : (active || riMa.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.7))
    border.width: plain ? 0 : 1
    border.color: Qt.rgba(196 / 255, 208 / 255, 217 / 255, 0.9)
    Accessible.role: Accessible.Button
    Accessible.name: label
    Canvas {
        anchors.centerIn: parent
        width: Theme.px(ri.small ? 14 : 18)
        height: width
        property string k: ri.kind
        onKChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"), s = width / 24
            c.reset()
            c.strokeStyle = Theme.text
            c.fillStyle = Theme.text
            c.lineWidth = 2 * s
            c.lineCap = "round"
            c.lineJoin = "round"
            c.beginPath()
            if (k === "locate") {
                c.arc(12 * s, 12 * s, 7 * s, 0, Math.PI * 2)
                c.moveTo(12 * s, 2 * s); c.lineTo(12 * s, 5 * s); c.moveTo(12 * s, 19 * s); c.lineTo(12 * s, 22 * s)
                c.moveTo(2 * s, 12 * s); c.lineTo(5 * s, 12 * s); c.moveTo(19 * s, 12 * s); c.lineTo(22 * s, 12 * s)
                c.stroke()
                c.beginPath(); c.arc(12 * s, 12 * s, 2 * s, 0, Math.PI * 2); c.fill()
            } else if (k === "more") {
                for (const x of [5, 12, 19]) { c.moveTo((x + 1.6) * s, 12 * s); c.arc(x * s, 12 * s, 1.6 * s, 0, Math.PI * 2) }
                c.fill()
            } else if (k === "edit") {   // 铅笔（0.8.9）
                c.moveTo(4 * s, 20 * s); c.lineTo(4 * s, 16 * s); c.lineTo(15 * s, 5 * s); c.lineTo(19 * s, 9 * s); c.lineTo(8 * s, 20 * s); c.closePath()
                c.moveTo(13 * s, 7 * s); c.lineTo(17 * s, 11 * s); c.stroke()
            } else if (k === "close") {
                c.moveTo(6 * s, 6 * s); c.lineTo(18 * s, 18 * s); c.moveTo(18 * s, 6 * s); c.lineTo(6 * s, 18 * s); c.stroke()
            } else if (k === "prev") {
                c.moveTo(15 * s, 5 * s); c.lineTo(8 * s, 12 * s); c.lineTo(15 * s, 19 * s); c.stroke()
            } else if (k === "next") {
                c.moveTo(9 * s, 5 * s); c.lineTo(16 * s, 12 * s); c.lineTo(9 * s, 19 * s); c.stroke()
            }
        }
    }
    MouseArea { id: riMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ri.clicked() }
}
