// 底图切换（0.8.5，功能清单 M-02、EN-01）：右上角的图层按钮，点一下往左分裂出 4 颗玻璃泡泡
// （0.9.0 去掉浅灰；0.9.2 图层合并：地球 / 卫星图 / 街道图 / 已解锁，单独的地球按钮去掉）；再点按钮、点别处或 Esc 收回；点一颗泡泡换底图，0.16 秒后收回。
// 照 Electron v6.3.1：map-app.js setLyrOpen + mapfirst.js TPSplit（split）+ mapfirst.css .tp-split .tp-bud + glass.css .lyr-opt：
//   - 收起时泡泡缩到 .55、平移到按钮中心（--bx）、透明；打开时按离按钮的远近依次出发（每颗 30 ms），
//     transform 0.42 s cubic-bezier(.3, .9, .3, 1.15)（带一点回弹），opacity 0.3 s
//   - 收回时同时走；选中的那颗晚 80 ms（.tp-split:not(.open) .tp-bud.picked）
//   - 当前底图的泡泡里垫一颗更白的玻璃胶囊（.lyr-opt.on::before：rgba(255,255,255,.58) + 白色内描边），
//     出现时 0.5 s cubic-bezier(.34, 1.6, .5, 1) 从 .7 放大；文字加粗
//   - 卫星图上（html.base-dark）泡泡换烟色玻璃（白色遮盖降到 .3）、白字带轻阴影；选中的那颗仍是深色字
// 粘连（液体拉丝）要等阶段 ⑥ 的液态玻璃着色器（smooth-min），这里先只有分裂 / 回弹 / 收回（PO 2026-11-04）。
// "动效"关掉时一步到位。
import QtQuick
import TPApp

Item {
    id: root
    property var bases: []            // [{ key, name }]，顺序 = 离按钮由近到远（Electron：row-reverse 排列）
    property string current: ""
    property bool dark: false         // 卫星图上
    property bool open: false
    signal picked(string key)

    readonly property int btnSize: Theme.px(36)
    readonly property int budH: Theme.px(36)
    readonly property int gap: Theme.px(6)
    width: btnSize
    height: btnSize
    // 每颗泡泡的宽度：文字（按粗体量，选中变粗时不跳）+ 左右各 14 px
    FontMetrics { id: fm; font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true }
    readonly property var budWidths: bases.map(b => Math.ceil(fm.advanceWidth(b.name)) + Theme.px(28))
    // 第 i 颗打开后的 x（相对按钮）：右边贴着按钮左边留 8 px（Electron right: calc(100% + 8px)），往左依次排、间隔 6 px
    function homeXOf(i) {
        let x = -Theme.px(8)
        for (let k = 0; k <= i && k < budWidths.length; ++k) x -= budWidths[k] + (k > 0 ? gap : 0)
        return x
    }

    function setOpen(on) {
        if (on === open) return
        open = on
        for (let i = 0; i < buds.count; ++i) {
            const b = buds.itemAt(i)
            if (!b) continue
            // 离按钮近的先出发（--i）；收回时一起走
            b.go(on, on ? i * 30 : 0)
        }
    }
    function toggle() { setOpen(!open) }
    // 某颗泡泡在窗口里的范围（点别处收回时用：点在按钮和泡泡上不算"别处"）
    function containsGlobal(gx, gy) {
        const p = root.mapFromItem(null, gx, gy)
        if (p.x >= 0 && p.y >= 0 && p.x <= width && p.y <= height) return true
        if (!open) return false
        for (let i = 0; i < buds.count; ++i) {
            const b = buds.itemAt(i)
            const q = b.mapFromItem(null, gx, gy)
            if (q.x >= 0 && q.y >= 0 && q.x <= b.width && q.y <= b.height) return true
        }
        return false
    }

    // ---- 泡泡（从按钮往左排；第 0 颗离按钮最近）。动画和外观在 SplitBud（0.8.8 和顶栏 ⋯ 共用） ----
    Repeater {
        id: buds
        model: root.bases
        delegate: SplitBud {
            id: bud
            required property var modelData
            required property int index
            text: modelData.name
            on: root.current === modelData.key
            dark: root.dark
            interactive: root.open
            homeX: root.homeXOf(index)
            centerX: root.btnSize / 2
            y: (root.btnSize - height) / 2
            width: root.budWidths[index] || 0
            height: root.budH
            Accessible.role: Accessible.RadioButton
            Accessible.checked: on
            onClicked: {
                root.picked(modelData.key)
                pickedNow = true
                closeLater.restart()   // Electron：setBase(…); setTimeout(() => setLyrOpen(false), 160)
            }
        }
    }
    Timer {
        id: closeLater
        interval: 160
        onTriggered: {
            // 被点的那颗晚 80 ms 收回
            for (let i = 0; i < buds.count; ++i) {
                const b = buds.itemAt(i)
                if (!b) continue
                b.go(false, b.pickedNow ? 80 : 0)
            }
            root.open = false
        }
    }

    // ---- 图层按钮 ----
    GlassPanel {
        id: btn
        anchors.fill: parent
        radius: Theme.px(12)
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? (root.dark ? 0.25 : 0.69) : 0.96)
        scale: btnArea.pressed ? 0.9 : 1   // 按下时轻轻缩一下（glass.css :active scale(.9)）
        Behavior on scale { NumberAnimation { duration: Theme.motion ? 120 : 0 } }
        Canvas {
            id: icon
            anchors.centerIn: parent
            width: Theme.px(20)
            height: Theme.px(20)
            // 打开时图标用强调色（base.css .lyr-ctrl.open .lyr-btn { color: var(--accent) }）
            property color ink: root.open ? Theme.accent : (root.dark ? "#ffffff" : Theme.text)
            onInkChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                // Electron 的 SVG（24×24）：三层菱形，最下面那层 .55 透明
                const c = getContext("2d")
                c.reset()
                c.scale(width / 24, height / 24)
                c.lineWidth = 1.8
                c.lineJoin = "round"
                c.strokeStyle = ink
                c.beginPath(); c.moveTo(12, 3); c.lineTo(2, 8.5); c.lineTo(12, 14); c.lineTo(22, 8.5); c.closePath(); c.stroke()
                c.beginPath(); c.moveTo(2, 13); c.lineTo(12, 18.5); c.lineTo(22, 13); c.stroke()
                c.globalAlpha = 0.55
                c.beginPath(); c.moveTo(2, 17.5); c.lineTo(12, 23); c.lineTo(22, 17.5); c.stroke()
            }
        }
        MouseArea {
            id: btnArea
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("切换底图")
            onClicked: root.toggle()
        }
    }
}
