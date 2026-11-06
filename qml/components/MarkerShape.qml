// 标记外形的小预览（0.8.9，样式选择器、编辑器里的样式行用）：照 Electron markerPreviewHtml 的样子——
// 圆 / 方（圆角 5）/ 菱形（转 45° 缩小）/ 水滴，计划色实底 + 白边；有图标时中间画白色图标（Material Symbols），否则 text（数字）。
import QtQuick
import TPApp

Item {
    id: root
    property color color: "#e8590c"
    property string shape: "circle"
    property string icon: ""
    property string text: ""
    property real size: Theme.px(22)
    property bool dashed: false
    readonly property string iconText: icon !== "" ? editor.iconChar(icon) : ""
    width: size
    height: shape === "pin" ? size * 32 / 28 : size
    Canvas {
        id: cv
        anchors.fill: parent
        property var deps: [root.color, root.shape, root.dashed, root.size]
        onDepsChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const w = width, h = height
            c.fillStyle = root.color
            c.strokeStyle = "#ffffff"
            c.lineWidth = Math.max(1.5, w / 11)
            if (root.dashed) c.setLineDash([3, 2])
            c.beginPath()
            if (root.shape === "pin") {
                const s = w / 28
                c.moveTo(14 * s, 30 * s)
                c.bezierCurveTo(14 * s, 30 * s, 3 * s, 19 * s, 3 * s, 12 * s)
                c.bezierCurveTo(3 * s, 5.9 * s, 7.9 * s, 1 * s, 14 * s, 1 * s)
                c.bezierCurveTo(20.1 * s, 1 * s, 25 * s, 5.9 * s, 25 * s, 12 * s)
                c.bezierCurveTo(25 * s, 19 * s, 14 * s, 30 * s, 14 * s, 30 * s)
                c.closePath()
            } else if (root.shape === "diamond") {
                const m = w / 2, r = w * 0.42
                c.moveTo(m, m - r); c.lineTo(m + r, m); c.lineTo(m, m + r); c.lineTo(m - r, m); c.closePath()
            } else if (root.shape === "square") {
                const p = c.lineWidth / 2, r = Math.min(5, w / 4)
                c.moveTo(p + r, p); c.lineTo(w - p - r, p); c.arcTo(w - p, p, w - p, p + r, r)
                c.lineTo(w - p, w - p - r); c.arcTo(w - p, w - p, w - p - r, w - p, r)
                c.lineTo(p + r, w - p); c.arcTo(p, w - p, p, w - p - r, r)
                c.lineTo(p, p + r); c.arcTo(p, p, p + r, p, r); c.closePath()
            } else {
                c.arc(w / 2, w / 2, w / 2 - c.lineWidth / 2, 0, Math.PI * 2)
            }
            c.fill(); c.stroke()
        }
    }
    Text {
        x: 0; width: root.width
        y: root.shape === "pin" ? root.size * 0.08 : 0
        height: root.size * (root.shape === "pin" ? 0.75 : 1)
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        visible: text !== ""
        text: root.iconText !== "" ? root.iconText : root.text
        font.family: root.iconText !== "" ? iconFamily : serifFamily
        font.pixelSize: root.iconText !== "" ? root.size * 0.62 : root.size * 0.5
        font.bold: root.iconText === ""
        color: "#ffffff"
    }
}
