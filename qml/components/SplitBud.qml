// 分裂泡泡的一颗（0.8.8 从 LayerBubbles 抽出来；顶栏 ⋯ 和右上角图层按钮共用，照 Electron v6.3.1 的 TPSplit：两处同一套参数）：
//   - 收起时缩到 .55、平移到按钮中心、透明；打开时按 delay 依次出发，
//     transform 0.42 s cubic-bezier(.3, .9, .3, 1.15)（带一点回弹），opacity 0.3 s；收回时同时走，被点的那颗晚 80 ms；
//   - 选中（on）时里面垫一颗更白的玻璃胶囊（.on::before：rgba(255,255,255,.58) + 白色内描边），出现时 0.5 s 从 .7 放大；文字加粗；
//   - dark：卫星图上的烟色玻璃、白字带轻阴影（图层泡泡用）。
// 位置由外面给：homeX = 打开后的 x，centerX = 按钮中心的 x（同一个坐标系）。粘连（液体拉丝）在阶段 ⑥。
import QtQuick
import TPApp

Item {
    id: bud
    property string text: ""
    property bool on: false
    property bool dark: false
    property bool whiteText: false    // 1.1.0（PO）：顶栏 ⋯ 泡泡没选中时白字 + 很淡的深色描边（选中照旧）
    property bool interactive: true
    property bool solid: false        // 0.12.10：下面不是地图时（整页上）不折射、接近实底
    property real homeX: 0
    property real homeY: 0            // 0.8.10：顶栏 ⋯ 泡泡太多、一行放不下时排到第二行
    property real centerX: 0
    property real centerY: 0          // 0.11.6：竖列从上一颗（所在行）出发，不是第一行
    property bool caret: false        // 0.11.6：文字后面的小三角 ▾（计划、照片带竖列）
    property var badges: []           // [{ text, kind: "days" | "count" }]（顶栏"下次出发"的倒计时和待办数）
    property real baseAlpha: 0.42     // 小泡泡遮盖（glass.css .lyr-opt --lg-alpha .5）
    property color onColor: Theme.text
    property bool pickedNow: false
    signal clicked()

    readonly property real bx: centerX - (homeX + width / 2)   // 泡泡中心到按钮中心
    property real p: 0   // 平移 / 缩放的进度（0 收起，1 打开；回弹时会略超过 1）
    property real a: 0   // 透明度
    x: homeX + bx * (1 - p)
    y: centerY + (homeY - centerY) * p
    scale: 0.55 + 0.45 * p
    opacity: a
    visible: a > 0.002
    z: on ? 2 : 1
    readonly property real contentWidth: row.implicitWidth + Theme.px(badges.length ? 20 : 28)   // 带徽标的：徽标本身是圆头，边距可以小，整颗不至于太宽

    function go(open, delay) {
        anim.stop()
        pz.duration = delay
        pa.duration = delay
        np.to = open ? 1 : 0
        na.to = open ? 1 : 0
        pickedNow = false
        if (!Theme.motion) { p = np.to; a = na.to; return }
        anim.start()
    }
    ParallelAnimation {
        id: anim
        SequentialAnimation {
            PauseAnimation { id: pz; duration: 0 }
            NumberAnimation { id: np; target: bud; property: "p"; duration: 420; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.3, 0.9, 0.3, 1.15, 1, 1] }
        }
        SequentialAnimation {
            PauseAnimation { id: pa; duration: 0 }
            NumberAnimation { id: na; target: bud; property: "a"; duration: 300; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.25, 0.1, 0.25, 1, 1, 1] }
        }
    }

    GlassPanel {
        anchors.fill: parent
        radius: height / 2
        shadow: bud.p > 0.98
        liquid: !bud.solid
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass && !bud.solid ? (bud.dark ? 0.25 : bud.baseAlpha) : 0.96)
        edge: hover.hovered ? Theme.lineStrong : Theme.glassEdge
    }
    // 选中：里面垫一颗更白的玻璃胶囊
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Qt.rgba(1, 1, 1, 0.58)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.9)
        opacity: bud.on ? 1 : 0
        scale: bud.on ? 1 : 0.7
        Behavior on opacity { NumberAnimation { duration: Theme.motion ? (bud.on ? 180 : 160) : 0 } }
        Behavior on scale { NumberAnimation { duration: Theme.motion ? (bud.on ? 500 : 160) : 0; easing.type: Easing.BezierSpline
                                              easing.bezierCurve: bud.on ? [0.34, 1.6, 0.5, 1, 1, 1] : [0.42, 0, 1, 1, 1, 1] } }
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.px(bud.badges.length ? 8 : 6)
        Item {
            width: label.implicitWidth
            height: label.implicitHeight
            anchors.verticalCenter: parent.verticalCenter
            Text {
                visible: bud.dark && !bud.on
                x: 0; y: 1
                text: label.text
                font: label.font
                color: Qt.rgba(0, 0, 0, 0.45)
            }
            Text {
                id: label
                text: bud.text
                font.family: serifFamily
                font.pixelSize: Theme.px(13)
                font.bold: bud.on
                color: (bud.dark || bud.whiteText) && !bud.on ? "#ffffff" : (bud.on ? bud.onColor : Theme.text)
                style: bud.whiteText && !bud.on ? Text.Outline : Text.Normal
                styleColor: Qt.rgba(0, 0, 0, 0.22)
            }
        }
        TpIcon {
            visible: bud.caret
            name: "down"
            width: Theme.px(12); height: width
            color: bud.on ? bud.onColor : bud.whiteText ? "#ffffff" : Theme.muted
            anchors.verticalCenter: parent.verticalCenter
        }
        Repeater {
            model: bud.badges
            delegate: Rectangle {
                required property var modelData
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                width: Math.max(height, bt.implicitWidth + Theme.px(12))
                height: Theme.px(18)
                radius: height / 2
                color: modelData.kind === "days" ? "#f3e3d9" : Theme.accentSoft
                Text {
                    id: bt
                    anchors.centerIn: parent
                    text: parent.modelData.text
                    font.family: serifFamily; font.pixelSize: Theme.px(11); font.bold: true
                    color: parent.modelData.kind === "days" ? "#8a4210" : Theme.accentInk
                }
            }
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    MouseArea {
        anchors.fill: parent
        enabled: bud.interactive
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: bud.text
        onClicked: bud.clicked()
    }
}
