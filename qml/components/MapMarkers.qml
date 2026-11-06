// 地图上的站点标记、合并圆圈、聚焦光晕（0.8.2）。数据和位置都在 C++（mapc.markers，src/map/markerlayer.*），
// 图片由 image://marker/… 画（src/map/markerimages.*，照 Electron v6.3.0 的 CSS / SVG）。
// 这里只摆放、转发悬停和点击。标记和圆圈放在同一个父项里，z 才能互相比较：普通标记 0、圆圈 1、选中的计划 1000、聚焦的站 3000。
import QtQuick
import TPApp

Item {
    id: overlay   // 不能叫 layer：Item 自带 layer 属性组，子项里写 layer.xxx 会指到它
    property var tip            // MapTip
    property bool moving: mapc.moving   // 0.9.5：地球上用地球的（在动时不显示提示框）
    // 0.9.6：不用的时候不建标记元素（地球上那一份在平面上时也跟着模型更新几百个元素，PO 电脑上悬停渐变变慢）
    property bool active: true
    readonly property int pad: 8     // MarkerImages::kPad（图片四周给阴影留的空白）

    Repeater {
        id: rep
        model: overlay.active ? mapc.markers.stops : null
        delegate: Item {
            id: mk
            required property int index
            required property var model      // 用 model.角色 读：模型 dataChanged 时会跟着变（每帧的位置、透明度）
            readonly property real mx: model.mx
            readonly property real my: model.my
            readonly property string img: model.img
            readonly property real mw: model.mw
            readonly property real mh: model.mh
            readonly property real ax: model.ax
            readonly property real ay: model.ay
            readonly property real op: model.op
            readonly property int mz: model.mz
            readonly property bool shown: model.shown
            readonly property bool focused: model.focused
            readonly property string glow: model.glow
            readonly property string tip: model.tip
            readonly property real sc: model.sc        // 0.9.5 地球上：靠近边缘变小（G3）
            readonly property bool hit: model.hit      // 0.9.5 地球上：快转到背面的不能点
            x: mx - ax
            y: my - ay
            width: mw
            height: mh
            z: mz
            visible: shown
            transform: Scale { origin.x: mk.ax; origin.y: mk.ay; xScale: mk.sc; yScale: mk.sc }

            // 聚焦站点：底下一团柔和的光晕，原地明暗呼吸（.mk-focus::after，1.8 秒；关掉"动效"时不动）
            Image {
                id: glowImg
                visible: mk.focused
                readonly property bool pin: mk.mh > mk.mw
                x: -16
                y: pin ? -12 : -16
                width: mk.mw + 32
                height: pin ? mk.mh + 16 : mk.mh + 32
                source: mk.focused && mk.glow !== "" ? "image://marker/" + mk.glow : ""
                smooth: true
                property real pulse: 1
                opacity: mk.op * pulse
                SequentialAnimation on pulse {
                    running: mk.focused && Theme.motion && shotPage === ""   // 截图时不呼吸：就绪判断靠"不再有新帧"（main.cpp）
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.35; to: 1; duration: 900; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 1; to: 0.35; duration: 900; easing.type: Easing.InOutSine }
                    onStopped: glowImg.pulse = 1
                }
            }
            Image {
                x: -overlay.pad
                y: -overlay.pad
                width: mk.mw + 2 * overlay.pad
                height: mk.mh + 2 * overlay.pad
                source: "image://marker/" + mk.img
                smooth: true
                opacity: mk.op
            }
            HoverHandler {
                enabled: mk.hit
                cursorShape: Qt.PointingHandCursor
                onHoveredChanged: {
                    if (hovered) {
                        mapc.markers.hoverStop(mk.index)
                        if (overlay.tip && !overlay.moving) overlay.tip.showAt(mk.tip, mk.x + mk.mw / 2, mk.y + mk.ay * (1 - mk.sc) - 4, "el", mk)
                    } else {
                        mapc.markers.unhoverStop(mk.index)
                        if (overlay.tip) overlay.tip.hideFor(mk)
                    }
                }
            }
            TapHandler {
                enabled: !editor.open && mk.hit   // 0.8.9：编辑时点别的计划不切换（Electron selectPlan：if (editor.open) return）
                onTapped: {
                    if (overlay.tip) overlay.tip.hide()
                    mapc.markers.clickStop(mk.index)
                }
            }
        }
    }

    Repeater {
        model: overlay.active ? mapc.markers.clusters : null
        delegate: Item {
            id: cl
            required property int index
            required property var model
            readonly property real mx: model.mx
            readonly property real my: model.my
            readonly property string img: model.img
            readonly property real mw: model.mw
            readonly property real op: model.op
            readonly property int mz: model.mz
            readonly property string tip: model.tip
            readonly property real sc: model.sc
            readonly property bool hit: model.hit
            readonly property bool shown: model.shown
            x: mx - mw / 2
            y: my - mw / 2
            width: mw
            height: mw
            z: mz
            visible: shown
            transform: Scale { origin.x: cl.mw / 2; origin.y: cl.mw / 2; xScale: cl.sc; yScale: cl.sc }
            Image {
                x: -overlay.pad
                y: -overlay.pad
                width: cl.mw + 2 * overlay.pad
                height: cl.mw + 2 * overlay.pad
                source: "image://marker/" + cl.img
                smooth: true
                opacity: cl.op
            }
            HoverHandler {
                enabled: cl.hit
                cursorShape: Qt.PointingHandCursor
                onHoveredChanged: {
                    if (hovered) {
                        mapc.markers.hoverCluster(cl.index)
                        if (overlay.tip && !overlay.moving) overlay.tip.showAt(cl.tip, cl.x + cl.mw / 2, cl.y + cl.mw / 2 * (1 - cl.sc) - 4, "el", cl)
                    } else {
                        mapc.markers.unhoverCluster(cl.index)
                        if (overlay.tip) overlay.tip.hideFor(cl)
                    }
                }
            }
            TapHandler {
                enabled: cl.hit
                onTapped: {
                    if (overlay.tip) overlay.tip.hide()
                    mapc.markers.clickCluster(cl.index)
                }
            }
        }
    }
}
