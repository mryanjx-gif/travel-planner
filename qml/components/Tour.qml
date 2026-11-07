// 新手引导（0.12.8，S-17，照 Electron tour.js）：压暗挖洞 + 玻璃卡片 + 进度点，一步指一个按钮。
// PO 选：不自动开始，第一次进地图时先在底部放一张小卡片问"要不要看"（offering，不挡地图，Esc = 跳过）。
// 引导进行中算弹框（Main 的 modalOpen），快捷键和 Esc 都归这里。看完或跳过都记 app.tourDone。
import QtQuick
import TPApp

Item {
    id: root
    objectName: "tour"
    anchors.fill: parent
    z: 190
    visible: active || offering

    // 每步 { target: Item, text }；target 看不见就跳过（比如窄窗口藏起来的按钮）
    property var steps: []
    property bool active: false
    property bool offering: false
    property int index: 0
    property bool last: false
    signal startRequested()

    function offer() { offering = true }
    function skipOffer() { offering = false; app.tourDone = true }
    function start() {
        offering = false
        active = true
        placed = false
        focusScope.forceActiveFocus()
        go(0)
    }
    function finish() {
        active = false
        app.tourDone = true
    }
    function usable(i) {
        const t = steps[i].target
        return t && t.visible && t.width > 2 && t.height > 2 && t.opacity > 0.01
    }
    function go(i) {
        while (i < steps.length && !usable(i)) i++
        if (i >= steps.length) { finish(); return }
        index = i
        let more = false
        for (let k = i + 1; k < steps.length; k++) if (usable(k)) more = true
        last = !more
        place()
    }

    // ---- 挖洞的位置：窗口变大小、顶栏动画途中都要跟着，所以开着时隔一会儿量一次 ----
    property bool placed: false
    property rect holeRect: Qt.rect(0, 0, 0, 0)
    function place() {
        if (!active || !steps[index]) return
        const t = steps[index].target
        const pad = Theme.px(6)
        const p = t.mapToItem(root, 0, 0)
        holeRect = Qt.rect(p.x - pad, p.y - pad, t.width + pad * 2, t.height + pad * 2)
        Qt.callLater(() => { root.placed = true })
    }
    Timer { interval: 200; repeat: true; running: root.active; onTriggered: root.place() }

    // 一个超粗边框的圆角矩形就是"整屏压暗，中间挖个圆角洞"：内圆角 = 半径 - 边框
    Rectangle {
        id: hole
        objectName: "tourHole"
        visible: root.active
        readonly property real thick: Math.hypot(root.width, root.height) * 1.2
        readonly property real inner: Math.min(root.holeRect.height / 2, root.holeRect.width / 2)
        x: root.holeRect.x - thick
        y: root.holeRect.y - thick
        width: root.holeRect.width + thick * 2
        height: root.holeRect.height + thick * 2
        radius: thick + inner
        color: "transparent"
        border.width: thick
        border.color: Qt.rgba(16 / 255, 26 / 255, 36 / 255, 0.48)
        Behavior on x { enabled: root.placed; TpMove {} }
        Behavior on y { enabled: root.placed; TpMove {} }
        Behavior on width { enabled: root.placed; TpMove {} }
        Behavior on height { enabled: root.placed; TpMove {} }
    }
    // 0.12.11 洞口描一圈亮边：卫星图本来就暗，只靠压暗看不出指的是哪个按钮
    Rectangle {
        objectName: "tourRing"
        visible: root.active
        x: root.holeRect.x; y: root.holeRect.y
        width: root.holeRect.width; height: root.holeRect.height
        radius: hole.inner
        color: "transparent"
        border.width: Theme.px(2)
        border.color: Qt.rgba(1, 1, 1, 0.92)
        Behavior on x { enabled: root.placed; TpMove {} }
        Behavior on y { enabled: root.placed; TpMove {} }
        Behavior on width { enabled: root.placed; TpMove {} }
        Behavior on height { enabled: root.placed; TpMove {} }
    }
    // 点哪儿都是下一步（Electron 一样）
    MouseArea {
        visible: root.active
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.go(root.index + 1)
        onWheel: (wheel) => wheel.accepted = true
    }
    FocusScope {
        id: focusScope
        anchors.fill: parent
        visible: root.active
        Keys.onEscapePressed: root.finish()
        Keys.onReturnPressed: root.go(root.index + 1)
        Keys.onEnterPressed: root.go(root.index + 1)
        Keys.onSpacePressed: root.go(root.index + 1)
        Keys.onRightPressed: root.go(root.index + 1)

        GlassPanel {
            id: card
            objectName: "tourCard"
            liquid: false
            tint: Qt.rgba(Theme.glassTint.r, Theme.glassTint.g, Theme.glassTint.b, 0.98)
            width: Math.min(Theme.px(320), root.width - 24)
            height: cardCol.implicitHeight + Theme.px(32)
            // 放在目标下面，放不下放上面；水平贴着目标，不出窗口
            readonly property real wantY: root.holeRect.y + root.holeRect.height + Theme.px(10)
            x: Math.max(12, Math.min(root.width - width - 12, root.holeRect.x + root.holeRect.width / 2 - width / 2))
            y: Math.max(12, wantY + height > root.height - 12 ? root.holeRect.y - height - Theme.px(10) : wantY)
            Behavior on x { enabled: root.placed; TpMove {} }
            Behavior on y { enabled: root.placed; TpMove {} }
            MouseArea { anchors.fill: parent }   // 点卡片空白处不算下一步
            Column {
                id: cardCol
                x: Theme.px(18)
                y: Theme.px(16)
                width: card.width - 2 * x
                spacing: Theme.px(14)
                Text {
                    objectName: "tourText"
                    width: parent.width
                    wrapMode: Text.Wrap
                    lineHeight: 1.3
                    text: root.steps[root.index] ? root.steps[root.index].text : ""
                    font.family: serifFamily; font.pixelSize: Theme.px(15); color: Theme.text
                }
                Item {
                    width: parent.width
                    height: nextBtn.height
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.px(6)
                        Repeater {
                            model: root.steps.length
                            Rectangle {
                                required property int index
                                width: Theme.px(6); height: width; radius: width / 2
                                color: index === root.index ? Theme.accent : Theme.lineStrong
                                Behavior on color { ColorAnimation { duration: Theme.motion ? 160 : 0 } }
                            }
                        }
                    }
                    Row {
                        anchors.right: parent.right
                        spacing: Theme.px(6)
                        TpButton { objectName: "tourSkip"; visible: !root.last; compact: true; kind: "ghost"; text: qsTr("跳过"); onClicked: root.finish() }
                        TpButton { id: nextBtn; objectName: "tourNext"; compact: true; kind: "primary"; text: root.last ? qsTr("开始用吧") : qsTr("下一步"); onClicked: root.go(root.index + 1) }
                    }
                }
            }
        }
    }

    // ---- 先问一句 ----
    GlassPanel {
        id: offerCard
        objectName: "tourOffer"
        visible: root.offering
        liquid: false
        tint: Qt.rgba(Theme.glassTint.r, Theme.glassTint.g, Theme.glassTint.b, 0.96)
        width: Math.min(Theme.px(380), root.width - 24)
        height: offerCol.implicitHeight + Theme.px(32)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.px(28)
        MouseArea { anchors.fill: parent; onWheel: (wheel) => wheel.accepted = true }
        Column {
            id: offerCol
            x: Theme.px(18)
            y: Theme.px(16)
            width: offerCard.width - 2 * x
            spacing: Theme.px(12)
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("要不要看 4 步引导？把常用的按钮认一遍。")
                font.family: serifFamily; font.pixelSize: Theme.px(15); color: Theme.text
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.px(6)
                TpButton { objectName: "tourOfferSkip"; compact: true; kind: "ghost"; text: qsTr("跳过"); onClicked: root.skipOffer() }
                TpButton { objectName: "tourOfferGo"; compact: true; kind: "primary"; text: qsTr("看看"); onClicked: root.startRequested() }
            }
        }
    }
}
