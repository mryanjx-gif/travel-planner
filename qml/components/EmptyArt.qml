// 欢迎页和空状态的插画（0.12.7，PO 选每页一张原创）：Canvas 画，只用色块和少量点线，不用外部素材。
// 统一的画法：场景站在一条地平线上（200×120 坐标），雾蓝三档 + 一点暖色当"目的地"，一串小圆点当"路线"。
// name：plan、wish、foot、album、calendar、ledger（欢迎页 1.3.2 起改用程序图标）
import QtQuick
import TPApp

Canvas {
    id: root
    property string name: ""
    readonly property color deep: Theme.accent
    readonly property color mid: "#7ea3c1"
    readonly property color pale: "#c9d9e6"
    readonly property color paper: "#f4f7fa"
    readonly property color warm: "#e6a95c"
    implicitWidth: Theme.px(200)
    implicitHeight: Theme.px(120)
    onNameChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function fill(c, color, d) { c.fillStyle = color; c.beginPath(); c.path = d; c.fill() }
    function line(c, color, w, d) { c.strokeStyle = color; c.lineWidth = w; c.beginPath(); c.path = d; c.stroke() }
    function dot(c, color, x, y, r) { c.fillStyle = color; c.beginPath(); c.arc(x, y, r, 0, Math.PI * 2); c.fill() }
    // 沿二次曲线放 n 个小圆点（路线）
    function trail(c, color, x0, y0, cx, cy, x1, y1, n, r) {
        for (let i = 0; i < n; ++i) {
            const t = i / (n - 1), u = 1 - t
            dot(c, color, u * u * x0 + 2 * u * t * cx + t * t * x1, u * u * y0 + 2 * u * t * cy + t * t * y1, r)
        }
    }
    // 交替左右的脚印
    function steps(c, color, x0, y0, cx, cy, x1, y1, n) {
        for (let i = 0; i < n; ++i) {
            const t = i / (n - 1), u = 1 - t, side = i % 2 ? 2.2 : -2.2
            dot(c, color, u * u * x0 + 2 * u * t * cx + t * t * x1 + side, u * u * y0 + 2 * u * t * cy + t * t * y1, 1.5)
        }
    }
    function card(c, color, x, y, w, h, r) {
        c.fillStyle = color
        c.beginPath(); c.roundedRect(x, y, w, h, r, r); c.fill()
    }
    function ground(c) { line(c, pale, 2, "M14 104H186") }

    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.scale(width / 200, height / 120)
        c.lineCap = "round"
        c.lineJoin = "round"
        switch (name) {
        case "wish":   // 远山和太阳：路线走到一半，还没到
            dot(c, warm, 150, 32, 10)
            fill(c, pale, "M26 104 60 62 80 82 104 48 150 104Z")
            fill(c, mid, "M76 104 124 50 172 104Z")
            fill(c, paper, "M124 50 136 63 129 60 123 67 116 60 112 64Z")
            ground(c)
            trail(c, paper, 98, 102, 116, 94, 123, 72, 7, 1.4)
            break
        case "plan":   // 摊开的折页地图：一条路线走到暖色的目的地
            fill(c, mid, "M46 46 86 34V92L46 104Z")
            fill(c, pale, "M86 34 126 46V104L86 92Z")
            fill(c, mid, "M126 46 166 34V92L126 104Z")
            ground(c)
            trail(c, deep, 62, 86, 96, 48, 141, 64, 11, 1.5)
            dot(c, paper, 62, 86, 4)
            dot(c, deep, 62, 86, 2.2)
            dot(c, paper, 143, 62, 8)
            dot(c, warm, 143, 62, 6)
            break
        case "foot":   // 脚印上了山顶，插了旗
            fill(c, pale, "M104 104C124 104 134 66 154 66C170 66 176 104 188 104Z")
            fill(c, mid, "M14 104C44 104 66 52 102 52C136 52 150 104 176 104Z")
            ground(c)
            steps(c, paper, 62, 100, 100, 94, 101, 58, 9)
            line(c, deep, 2, "M102 52V24")
            fill(c, warm, "M103 24 122 30 103 37Z")
            break
        case "album":   // 两张相片叠在一起
            c.save(); c.translate(84, 60); c.rotate(-0.17)
            card(c, pale, -34, -30, 68, 60, 3)
            c.restore()
            c.save(); c.translate(110, 60); c.rotate(0.08)
            card(c, deep, -36, -36, 72, 72, 3)
            card(c, "#ffffff", -34.500, -34.500, 69, 69, 2.5)
            card(c, "#dbe7f0", -29, -29, 58, 44, 1.5)
            dot(c, warm, 16, -17, 5.5)
            fill(c, mid, "M-29 15 -9 -11 4 3 12 -5 29 15Z")
            c.restore()
            ground(c)
            break
        case "calendar":   // 一页日历，三天被一趟旅程占了
            card(c, pale, 54, 22, 92, 82, 7)
            card(c, "#ffffff", 55.500, 23.500, 89, 79, 6)
            fill(c, deep, "M54 29A7 7 0 0 1 61 22H139A7 7 0 0 1 146 29V40H54Z")
            card(c, paper, 74, 16, 4, 12, 2)
            card(c, paper, 122, 16, 4, 12, 2)
            for (let row = 0; row < 4; ++row)
                for (let col = 0; col < 6; ++col)
                    card(c, "#e1ebf4", 62 + col * 13.300, 47 + row * 13.500, 9.500, 9.500, 2)
            card(c, warm, 75.300, 60.500, 36.100, 9.500, 3)
            card(c, mid, 115, 74, 22.800, 9.500, 3)
            ground(c)
            break
        case "ledger":   // 一张小票和两枚硬币
            fill(c, pale, "M64 24A5 5 0 0 1 69 19H121A5 5 0 0 1 126 24V102L116 96 106 102 95 96 84 102 74 96 64 102Z")
            fill(c, "#ffffff", "M65.500 24A3.500 3.500 0 0 1 69 20.500H121A3.500 3.500 0 0 1 124.500 24V99.500L116 94.500 106 100 95 94.500 84 100 74 94.500 65.500 99.500Z")
            card(c, deep, 74, 31, 26, 5, 2.5)
            card(c, "#e1ebf4", 74, 46, 42, 4.500, 2)
            card(c, "#e1ebf4", 74, 56, 42, 4.500, 2)
            card(c, "#e1ebf4", 74, 66, 30, 4.500, 2)
            card(c, mid, 96, 78, 20, 5, 2.5)
            dot(c, mid, 150, 92, 12)
            dot(c, warm, 138, 88, 16)
            dot(c, "#efc48c", 138, 88, 11)
            ground(c)
            break
        }
    }
}
