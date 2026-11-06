// 线条图标（0.8.8）：照 Electron v6.3.0 mapfirst.js / palette.js / map-app.js 里的 SVG（24×24，线宽 1.8，圆头）用 Canvas 画。
// 不用图标字体：打包的 Material Symbols 只是地图标记用的子集。
// name：dots、list、uturn、person、close、search、eye、eyeOff、pin（实心书签）、globe、flatmap、stop、plan、page、act、down、filter；
// 0.8.9：plus、minus、locate、edit、grip、up、trash、pick、coord、check
import QtQuick
import TPApp

Canvas {
    id: root
    property string name: ""
    property color color: Theme.text
    property real stroke: 1.8
    implicitWidth: Theme.px(18)
    implicitHeight: Theme.px(18)
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function circle(c, x, y, r, fill) {
        c.beginPath(); c.arc(x, y, r, 0, Math.PI * 2)
        if (fill) c.fill(); else c.stroke()
    }
    function poly(c, pts, close) {
        c.beginPath(); c.moveTo(pts[0], pts[1])
        for (let i = 2; i < pts.length; i += 2) c.lineTo(pts[i], pts[i + 1])
        if (close) c.closePath()
        c.stroke()
    }
    function rrect(c, x, y, w, h, r) {
        c.beginPath()
        c.moveTo(x + r, y); c.lineTo(x + w - r, y); c.arcTo(x + w, y, x + w, y + r, r)
        c.lineTo(x + w, y + h - r); c.arcTo(x + w, y + h, x + w - r, y + h, r)
        c.lineTo(x + r, y + h); c.arcTo(x, y + h, x, y + h - r, r)
        c.lineTo(x, y + r); c.arcTo(x, y, x + r, y, r)
        c.closePath(); c.stroke()
    }

    function cloud(c, dy) {
        c.beginPath(); c.moveTo(7, 18 + dy)
        c.bezierCurveTo(2.5, 18 + dy, 2.5, 12 + dy, 7, 12 + dy)
        c.bezierCurveTo(7, 6.5 + dy, 15.5, 5.5 + dy, 16.5, 11 + dy)
        c.bezierCurveTo(21.5, 10.5 + dy, 22, 18 + dy, 17, 18 + dy)
        c.closePath(); c.stroke()
    }

    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.scale(width / 24, height / 24)
        c.lineWidth = stroke
        c.lineCap = "round"
        c.lineJoin = "round"
        c.strokeStyle = color
        c.fillStyle = color
        switch (name) {
        case "dots":
            circle(c, 5, 12, 1.6, true); circle(c, 12, 12, 1.6, true); circle(c, 19, 12, 1.6, true); break
        case "list":
            for (const y of [7, 12, 17]) { poly(c, [8, y, 20, y]); circle(c, 4, y, 0.9, true) }
            break
        case "uturn":   // M9 14 4 9l5-5  M4 9h11a5 5 0 0 1 0 10h-4
            poly(c, [9, 14, 4, 9, 9, 4])
            c.beginPath(); c.moveTo(4, 9); c.lineTo(15, 9); c.arc(15, 14, 5, -Math.PI / 2, Math.PI / 2, false); c.lineTo(11, 19); c.stroke()
            break
        case "person":
            circle(c, 12, 8.5, 3.6)
            c.beginPath(); c.moveTo(5, 20); c.bezierCurveTo(5.8, 16.4, 8.6, 14.4, 12, 14.4); c.bezierCurveTo(15.4, 14.4, 18.2, 16.4, 19, 20); c.stroke()
            break
        case "sun":
            circle(c, 12, 12, 4)
            for (const r of [[12, 2, 12, 5], [12, 19, 12, 22], [2, 12, 5, 12], [19, 12, 22, 12], [5, 5, 7, 7], [17, 17, 19, 19], [5, 19, 7, 17], [17, 7, 19, 5]]) poly(c, r)
            break
        case "moon":
            c.beginPath(); c.arc(12, 12, 8, Math.PI * 0.35, Math.PI * 1.65, false); c.bezierCurveTo(13, 7, 13, 17, 17.4, 17.9); c.stroke()
            break
        case "mountain":
            poly(c, [2, 20, 9, 7, 13, 14, 16, 10, 22, 20], true); break
        case "cloud":
            cloud(c, 0); break
        case "rain":
            cloud(c, -3); poly(c, [8, 18, 7, 21]); poly(c, [12.5, 18, 11.5, 21]); poly(c, [17, 18, 16, 21]); break
        case "snow":
            cloud(c, -3); circle(c, 8, 19.5, 1, true); circle(c, 12, 21, 1, true); circle(c, 16, 19.5, 1, true); break
        case "storm":
            cloud(c, -3); poly(c, [13, 15, 10, 19.5, 13.5, 19.5, 11, 23]); break
        case "close":
            poly(c, [6, 6, 18, 18]); poly(c, [18, 6, 6, 18]); break
        case "search":
            circle(c, 11, 11, 6.5); poly(c, [16, 16, 20.5, 20.5]); break
        case "eye":
        case "eyeOff":
            c.beginPath(); c.moveTo(1, 12); c.bezierCurveTo(1, 12, 5, 5, 12, 5); c.bezierCurveTo(19, 5, 23, 12, 23, 12)
            c.bezierCurveTo(23, 12, 19, 19, 12, 19); c.bezierCurveTo(5, 19, 1, 12, 1, 12); c.closePath(); c.stroke()
            circle(c, 12, 12, 3)
            if (name === "eyeOff") poly(c, [2, 2, 22, 22])
            break
        case "pin":     // 实心书签（map-app.js PIN_SVG）
            c.beginPath(); c.moveTo(6.5, 3); c.lineTo(17.5, 3); c.lineTo(18.5, 4); c.lineTo(18.5, 20.2); c.lineTo(17.2, 20.9)
            c.lineTo(12, 17.2); c.lineTo(6.8, 20.9); c.lineTo(5.5, 20.2); c.lineTo(5.5, 4); c.closePath(); c.fill()
            break
        case "globe":
            circle(c, 12, 12, 8.5); poly(c, [3.5, 12, 20.5, 12])
            c.beginPath(); c.moveTo(12, 3.5); c.bezierCurveTo(15.5, 7, 15.5, 17, 12, 20.5); c.stroke()
            c.beginPath(); c.moveTo(12, 3.5); c.bezierCurveTo(8.5, 7, 8.5, 17, 12, 20.5); c.stroke()
            break
        case "flatmap":
            poly(c, [9, 4, 3, 6, 3, 20, 9, 18, 15, 20, 21, 18, 21, 4, 15, 6], true); poly(c, [9, 4, 9, 18]); poly(c, [15, 6, 15, 20]); break
        case "stop":    // palette.js I.pin
            c.beginPath(); c.moveTo(12, 21); c.bezierCurveTo(12, 21, 5.5, 15, 5.5, 10); c.arc(12, 10, 6.5, Math.PI, 0, false)
            c.bezierCurveTo(18.5, 15, 12, 21, 12, 21); c.stroke(); circle(c, 12, 10, 2.3)
            break
        case "plan":
            rrect(c, 4, 7, 16, 13, 2); poly(c, [9, 7, 9, 5, 10, 4, 14, 4, 15, 5, 15, 7]); poly(c, [4, 12, 20, 12]); break
        case "page":
            rrect(c, 4, 4, 16, 16, 3); poly(c, [8, 9, 16, 9]); poly(c, [8, 13, 13, 13]); break
        case "act":
            poly(c, [13, 3, 5, 14, 11, 14, 10, 21, 18, 10, 12, 10], true); break
        case "down":
            poly(c, [6, 9, 12, 15, 18, 9]); break
        case "filter":
            poly(c, [4, 6, 20, 6]); poly(c, [7, 12, 17, 12]); poly(c, [10, 18, 14, 18]); break
        // ---- 0.8.9：新建、缩放、我的位置、编辑器 ----
        case "plus":    // mapfirst.js I.plus
            poly(c, [12, 5, 12, 19]); poly(c, [5, 12, 19, 12]); break
        case "minus":
            poly(c, [5, 12, 19, 12]); break
        case "locate":  // map-app.js LOC / .mf-loc
            circle(c, 12, 12, 7); circle(c, 12, 12, 2, true)
            poly(c, [12, 2, 12, 5]); poly(c, [12, 19, 12, 22]); poly(c, [2, 12, 5, 12]); poly(c, [19, 12, 22, 12]); break
        case "edit":    // 铅笔
            poly(c, [4, 20, 4, 16, 15, 5, 19, 9, 8, 20], true); poly(c, [13, 7, 17, 11]); break
        case "grip":    // 拖动把手 ⋮⋮
            for (const x of [9, 15]) for (const y of [6, 12, 18]) circle(c, x, y, 1.5, true)
            break
        case "up":
            poly(c, [6, 15, 12, 9, 18, 15]); break
        case "trash":
            poly(c, [4, 7, 20, 7]); poly(c, [10, 4, 14, 4]); poly(c, [6, 7, 7, 20, 17, 20, 18, 7]); break
        case "pick":    // 在地图上点选：十字准星
            circle(c, 12, 12, 6); poly(c, [12, 2, 12, 7]); poly(c, [12, 17, 12, 22]); poly(c, [2, 12, 7, 12]); poly(c, [17, 12, 22, 12]); break
        case "coord":   // 手动输入坐标：#
            poly(c, [9, 4, 7, 20]); poly(c, [17, 4, 15, 20]); poly(c, [4, 9, 20, 9]); poly(c, [4, 15, 20, 15]); break
        case "star":
        case "starOn":
            c.beginPath()
            for (let i = 0; i < 10; ++i) {
                const r = i % 2 ? 4 : 9.5, a = -Math.PI / 2 + i * Math.PI / 5
                c.lineTo(12 + r * Math.cos(a), 12.5 + r * Math.sin(a))
            }
            c.closePath()
            if (name === "starOn") c.fill(); else c.stroke()
            break
        case "image":   // 设为封面
            rrect(c, 3, 5, 18, 14, 2); circle(c, 8.5, 10, 1.4, true); poly(c, [4, 17, 10, 12, 14, 15.5, 17, 13, 20, 16]); break
        case "check":
            poly(c, [5, 12, 10, 17, 19, 7]); break
        }
    }
}
