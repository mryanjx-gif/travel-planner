// 没有封面时的路线简图（0.10.0，P-01；照 Electron store.js routeSketch）：站点连线 + 圆点，起点大一点；单一目的地画一个图钉。
// 只用经纬度直接投影（经度按平均纬度压缩），不是地图，只是示意。
import QtQuick

Canvas {
    id: root
    property var stops: []       // [{ lat, lng }]
    property bool route: false
    property color tone: "#2d5f8b"
    onStopsChanged: requestPaint()
    onRouteChanged: requestPaint()
    onToneChanged: requestPaint()
    onPaint: {
        const c = getContext("2d"), W = width, H = height, pad = W * 0.18
        c.reset()
        c.fillStyle = Qt.rgba(tone.r, tone.g, tone.b, 0.13)
        c.fillRect(0, 0, W, H)
        c.fillStyle = tone
        const st = stops || []
        if (!route || st.length < 2) {
            const r = W * 0.14, cy = H * 0.44
            c.beginPath()
            c.arc(W / 2, cy, r, Math.PI * 0.85, Math.PI * 0.15)
            c.lineTo(W / 2, cy + r * 2)
            c.closePath()
            c.fill()
            c.fillStyle = "#ffffff"
            c.beginPath(); c.arc(W / 2, cy, r * 0.42, 0, Math.PI * 2); c.fill()
            return
        }
        // 跨 180° 经线：以前一站为参照把经度展开
        const pts = []
        for (let i = 0; i < st.length; ++i) {
            let x = st[i].lng
            if (i) { const prev = pts[i - 1][0]; while (x - prev > 180) x -= 360; while (prev - x > 180) x += 360 }
            pts.push([x, st[i].lat])
        }
        const k = Math.cos(pts.reduce((s, p) => s + p[1], 0) / pts.length * Math.PI / 180) || 1
        const xs = pts.map(p => p[0] * k), ys = pts.map(p => -p[1])
        const x0 = Math.min(...xs), x1 = Math.max(...xs), y0 = Math.min(...ys), y1 = Math.max(...ys)
        const s = Math.min((W - pad * 2) / Math.max(x1 - x0, 1e-6), (H - pad * 2) / Math.max(y1 - y0, 1e-6))
        const ox = (W - (x1 - x0) * s) / 2, oy = (H - (y1 - y0) * s) / 2
        const P = xs.map((x, i) => [ox + (x - x0) * s, oy + (ys[i] - y0) * s])
        c.lineCap = "round"; c.lineJoin = "round"
        for (const [color, lw] of [["#ffffff", W * 0.07], [tone, W * 0.035]]) {
            c.strokeStyle = color; c.lineWidth = lw
            c.beginPath()
            P.forEach((p, i) => i ? c.lineTo(p[0], p[1]) : c.moveTo(p[0], p[1]))
            c.stroke()
        }
        P.forEach((p, i) => { c.beginPath(); c.arc(p[0], p[1], W * (i ? 0.035 : 0.055), 0, Math.PI * 2); c.fill() })
    }
}
