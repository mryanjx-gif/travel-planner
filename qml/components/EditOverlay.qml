// 地图上和编辑、定位有关的覆盖层（0.8.9），放在 FlatMap 里、站点标记之上：
//   - 正在编辑的计划的标记（草稿，虚线边，能拖动；松手 = 改坐标，Electron addDraftMarker 的 dragend）；
//   - 地名搜索结果的编号小圆点（PO 选方案 B：鼠标停在列表哪一行，这里那个点变大；点一下也能加）；
//   - 点选模式（改进 a）：十字光标 + 跟着鼠标的提示 + 从上一站到鼠标的虚线预览；点一下加一站，可以连续点，Esc 退出；
//   - 我的位置：蓝点 + 精度圈（改进 b）。
// 位置都用 mapc.pixelFor（和地图画面同一个相机），相机一动就重新算。
import QtQuick
import TPApp

Item {
    id: root
    property var tip            // MapTip
    readonly property int pad: 8
    // 相机变了就重算（pixelFor 不是属性，靠这个依赖）
    readonly property var cam: [mapc.lat, mapc.lng, mapc.zoom, width, height]
    function px(lat, lng) { root.cam; return mapc.pixelFor(lat, lng) }

    // ---- 我的位置 ----
    Item {
        id: me
        visible: locator.hasFix
        readonly property point p: root.px(locator.lat, locator.lng)
        readonly property real accPx: locator.accuracy > 0 ? Math.min(2000, locator.accuracy / mapc.metersPerPixel(locator.lat)) : 0
        x: p.x; y: p.y
        Rectangle {   // 精度圈（改进 b）：太小（< 12 px）就不画
            visible: me.accPx >= 12
            x: -me.accPx; y: -me.accPx
            width: me.accPx * 2; height: width; radius: width / 2
            color: Qt.rgba(26 / 255, 115 / 255, 232 / 255, 0.12)
            border.width: 1
            border.color: Qt.rgba(26 / 255, 115 / 255, 232 / 255, 0.45)
        }
        Rectangle {   // Electron .me-dot：16 px 蓝点、3 px 白边、6 px 浅蓝光圈
            x: -14; y: -14; width: 28; height: 28; radius: 14
            color: Qt.rgba(26 / 255, 115 / 255, 232 / 255, 0.22)
        }
        Rectangle {
            objectName: "meDot"
            x: -8; y: -8; width: 16; height: 16; radius: 8
            color: "#1a73e8"
            border.width: 3; border.color: "#ffffff"
        }
    }

    // ---- 搜索结果的编号小圆点 ----
    Repeater {
        model: editor.open && editor.searchState === "done" ? editor.results : []
        delegate: Rectangle {
            required property var modelData
            required property int index
            readonly property point p: root.px(modelData.lat, modelData.lng)
            readonly property bool hot: editor.hoverResult === index
            x: p.x - width / 2; y: p.y - height / 2
            width: hot ? 26 : 20; height: width; radius: width / 2
            z: hot ? 2 : 1
            color: hot ? Theme.warn : Theme.accent
            border.width: 2; border.color: "#ffffff"
            Behavior on width { TpMove { ms: 120 } }
            Text { anchors.centerIn: parent; text: index + 1; font.pixelSize: 11; font.bold: true; color: "#ffffff" }
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
                onHoveredChanged: {
                    editor.hoverResult = hovered ? index : (editor.hoverResult === index ? -1 : editor.hoverResult)
                    if (root.tip) { if (hovered) root.tip.showAt(modelData.name, x + width / 2, y - 4, "el", parent); else root.tip.hideFor(parent) }
                }
            }
            TapHandler { onTapped: editor.addResult(index) }
        }
    }

    // ---- 正在编辑的计划：草稿标记（能拖动） ----
    Repeater {
        id: draftRep
        model: mapc.draftMarkers
        delegate: Item {
            id: dm
            required property var modelData
            required property int index
            readonly property point p: root.px(modelData.lat, modelData.lng)
            readonly property bool active: editor.activeStop === modelData.stop
            property real dx: 0
            property real dy: 0
            x: p.x - modelData.ax + dx
            y: p.y - modelData.ay + dy
            width: modelData.w; height: modelData.h
            z: 2000 + (active ? 10 : 0) + (drag.active ? 20 : 0)
            scale: active ? 1.18 : 1
            Behavior on scale { TpSpring {} }
            Image {
                x: -root.pad; y: -root.pad
                width: dm.width + 2 * root.pad; height: dm.height + 2 * root.pad
                source: "image://marker/" + dm.modelData.img
                smooth: true
            }
            HoverHandler {
                cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                onHoveredChanged: if (root.tip) { if (hovered && !drag.active) root.tip.showAt(dm.modelData.name || qsTr("未命名地点"), dm.x + dm.width / 2, dm.y - 4, "el", dm); else root.tip.hideFor(dm) }
            }
            DragHandler {
                id: drag
                target: null
                property point start
                onActiveChanged: {
                    if (active) { start = centroid.scenePosition; if (root.tip) root.tip.hideFor(dm) }
                    else {
                        // 松手：标记锚点所在的位置就是新坐标（Electron dragend → setDirty + 自动保存）
                        const c = mapc.coordinateFor(dm.p.x + dm.dx, dm.p.y + dm.dy)
                        const stop = dm.modelData.stop
                        dm.dx = 0; dm.dy = 0
                        editor.setStopLatLng(stop, c[0], c[1])
                    }
                }
                onCentroidChanged: if (active) { dm.dx = centroid.scenePosition.x - start.x; dm.dy = centroid.scenePosition.y - start.y }
            }
            // 点一下 = 在面板里展开这一站（点选模式下点到已有站点不加新站，Electron 一样）
            TapHandler { onTapped: editor.activeStop = dm.modelData.stop }
        }
    }

    // ---- 点选模式（M-24 + 改进 a） ----
    Item {
        id: pick
        anchors.fill: parent
        visible: editor.open && editor.pickMode
        z: 1500   // 在草稿标记下面：点到已有站点时由站点处理
        property point mouse: Qt.point(-1, -1)
        readonly property var last: {
            const m = mapc.draftMarkers
            return m.length ? m[m.length - 1] : null
        }
        HoverHandler {
            id: pickHover
            cursorShape: Qt.CrossCursor
            onPointChanged: pick.mouse = point.position
            onHoveredChanged: if (!hovered) pick.mouse = Qt.point(-1, -1)
        }
        TapHandler {
            onTapped: (ev) => { const c = mapc.coordinateFor(ev.position.x, ev.position.y); editor.pickAt(c[0], c[1]) }
        }
        // 从上一站到鼠标的虚线（路线时才有意义；单一目的地也画，表示"加在这里"）
        Canvas {
            id: rubber
            anchors.fill: parent
            visible: pick.last !== null && pick.mouse.x >= 0
            property var deps: [pick.mouse, root.cam, pick.last]
            onDepsChanged: requestPaint()
            onPaint: {
                const c = getContext("2d")
                c.reset()
                if (!pick.last || pick.mouse.x < 0) return
                const a = root.px(pick.last.lat, pick.last.lng)
                c.lineCap = "round"
                c.strokeStyle = "rgba(255,255,255,0.9)"; c.lineWidth = 7
                c.beginPath(); c.moveTo(a.x, a.y); c.lineTo(pick.mouse.x, pick.mouse.y); c.stroke()
                c.strokeStyle = "#2d5f8b"; c.lineWidth = 3; c.setLineDash([2, 3])
                c.beginPath(); c.moveTo(a.x, a.y); c.lineTo(pick.mouse.x, pick.mouse.y); c.stroke()
            }
        }
        // 跟着鼠标的提示
        Rectangle {
            visible: pick.mouse.x >= 0
            x: Math.min(pick.mouse.x + 16, root.width - width - 6)
            y: Math.min(pick.mouse.y + 18, root.height - height - 6)
            width: pickTip.implicitWidth + 16; height: pickTip.implicitHeight + 8; radius: 8
            color: "#26323c"
            Text {
                id: pickTip
                anchors.centerIn: parent
                text: qsTr("点一下加站点 · Esc 退出")
                font.family: serifFamily; font.pixelSize: 12; color: "#ffffff"
            }
        }
    }
}
