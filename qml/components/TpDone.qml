// 完成时的对勾（0.12.2）：一笔画出来，约 300 ms。不要粒子。on 变成 true 时画一次。
import QtQuick
import TPApp

Canvas {
    id: root
    property bool on: false
    property color stroke: Theme.accent
    property real t: 0
    implicitWidth: Theme.px(14)
    implicitHeight: Theme.px(14)
    visible: on
    onOnChanged: { if (on && Theme.motion) { t = 0; anim.restart() } else { anim.stop(); t = on ? 1 : 0 } }
    onTChanged: requestPaint()
    onStrokeChanged: requestPaint()
    TpMove { id: anim; target: root; property: "t"; to: 1; ms: 300; easing.bezierCurve: Theme.curveEnter }
    onPaint: {
        const c = getContext("2d")
        c.reset()
        const k = width / 16
        c.scale(k, k)
        c.lineWidth = 2.2
        c.lineCap = "round"
        c.lineJoin = "round"
        c.strokeStyle = stroke
        // 两段：(3, 8.5) → (6.5, 12) → (13, 4.5)，按总长度走到 t
        const a = Math.hypot(3.5, 3.5), b = Math.hypot(6.5, 7.5), d = t * (a + b)
        c.beginPath()
        c.moveTo(3, 8.5)
        if (d <= a) c.lineTo(3 + 3.5 * d / a, 8.5 + 3.5 * d / a)
        else { c.lineTo(6.5, 12); c.lineTo(6.5 + 6.5 * (d - a) / b, 12 - 7.5 * (d - a) / b) }
        c.stroke()
    }
}
