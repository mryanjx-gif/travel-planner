// 看大图（0.10.5，A-07；PO 选 6A、N1、N2）：盖住整个窗口，暗底；← → 切换，Esc 关闭，点空白处关闭；显示序号、说明、站点和拍摄时间；关掉回到原处。
// 独立组件：show(列表, 第几张)，阶段 ⑤ 的相册和照片模式也用它。列表每项 { file（数据文件夹里的相对路径）, thumb, caption, stop, stopNo, route, takenAt }，有哪项给哪项。
// N1：滚轮放大（以鼠标为中心）、拖动、双击还原。平时按屏幕大小解码（省内存）；放大时才加载原图，还原或换一张就放掉（占多少内存见 v0.10.5记录.md）。
// ★ 精选和旋转按钮按功能清单在阶段 ⑤ 加（A-07、A-08）。
import QtQuick
import TPApp

Item {
    id: root
    objectName: "lightbox"
    property bool open: false
    property var items: []
    property int index: 0
    property real zoom: 1                  // 相对于"放得下"
    property real panX: 0
    property real panY: 0
    property Item returnFocus: null
    readonly property var cur: items[index] || ({})
    readonly property url src: open && cur.file ? photos.fileUrl(cur.file) : ""
    readonly property real dpr: Screen.devicePixelRatio
    readonly property real stageW: Math.max(1, width - 2 * Theme.px(width < Theme.px(620) ? 8 : 64))
    readonly property real stageH: Math.max(1, height - Theme.px(56) - Theme.px(cur.caption ? 84 : 58))
    // 原图的长宽（按 EXIF 方向摆正）从文件头读：从 Electron 导入的照片库里没记，缩放解码后的 implicitWidth 又不是原图的
    readonly property size realSize: open && cur.file ? photos.imageSize(cur.file) : Qt.size(0, 0)
    readonly property real natW: realSize.width > 0 ? realSize.width : fit.implicitWidth
    readonly property real natH: realSize.height > 0 ? realSize.height : fit.implicitHeight
    readonly property real fitK: natW > 0 && natH > 0 ? Math.min(stageW / natW, stageH / natH) : 1
    readonly property real maxZoom: Math.max(4, natW > 0 ? 2 * natW / (fitW * dpr) : 4)   // 至少能看到原图 200%
    readonly property real fitW: natW > 0 ? natW * fitK : stageW
    readonly property real fitH: natH > 0 ? natH * fitK : stageH
    readonly property int percent: natW > 0 ? Math.round(fitW * zoom * dpr / natW * 100) : Math.round(zoom * 100)
    readonly property bool zoomed: zoom > 1.001
    readonly property string metaText: {
        const a = []
        if (cur.stop) a.push(cur.route && cur.stopNo > 0 ? qsTr("第 %1 站").arg(cur.stopNo) + " · " + cur.stop : cur.stop)
        if (cur.takenAt) a.push(String(cur.takenAt).slice(0, 16).replace("T", " "))
        return a.join(" · ")
    }

    function show(list, i) {
        returnFocus = Window.activeFocusItem
        items = list
        index = Math.max(0, Math.min(i, list.length - 1))
        resetZoom()
        open = true
        keys.forceActiveFocus()
    }
    function close() {
        open = false
        resetZoom()
        if (returnFocus) returnFocus.forceActiveFocus()
        returnFocus = null
    }
    function step(d) {
        if (items.length < 2) return
        resetZoom()
        index = (index + d + items.length) % items.length
    }
    function resetZoom() { zoom = 1; panX = 0; panY = 0 }
    // 把图片上放在 (x, y)（相对 stage 中心）的点留在原地
    function zoomAt(k, x, y) {
        const z = Math.max(1, Math.min(maxZoom, zoom * k))
        const r = z / zoom
        panX = (panX - x) * r + x
        panY = (panY - y) * r + y
        zoom = z
        clampPan()
    }
    function clampPan() {
        const mx = Math.max(0, (fitW * zoom - stage.width) / 2), my = Math.max(0, (fitH * zoom - stage.height) / 2)
        panX = Math.max(-mx, Math.min(mx, panX))
        panY = Math.max(-my, Math.min(my, panY))
    }

    anchors.fill: parent
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    Behavior on opacity { enabled: Theme.motion; NumberAnimation { duration: 160 } }
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("看大图")

    Rectangle { anchors.fill: parent; color: Qt.rgba(8 / 255, 11 / 255, 16 / 255, 0.97) }

    FocusScope {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Escape) root.close()
            else if (e.key === Qt.Key_Left) root.step(-1)
            else if (e.key === Qt.Key_Right) root.step(1)
            else return
            e.accepted = true
        }

        // ---- 图片 ----
        Item {
            id: stage
            objectName: "lightboxStage"
            x: (parent.width - width) / 2
            y: Theme.px(56)
            width: root.stageW
            height: root.stageH
            clip: root.zoomed

            Item {
                id: pic
                objectName: "lightboxPic"
                width: root.fitW * root.zoom
                height: root.fitH * root.zoom
                x: (stage.width - width) / 2 + root.panX
                y: (stage.height - height) / 2 + root.panY
                Image {   // 缩略图先垫着，马上有东西看
                    anchors.fill: parent
                    source: root.open ? root.cur.thumb || "" : ""
                    fillMode: Image.PreserveAspectFit
                    visible: fit.status !== Image.Ready && full.status !== Image.Ready
                    asynchronous: true
                }
                Image {
                    id: fit
                    objectName: "lightboxFit"
                    anchors.fill: parent
                    source: root.src
                    // 不超过原图：比屏幕小的照片不放大解码
                    sourceSize: root.realSize.width > 0 ? Qt.size(Math.min(root.realSize.width, Math.ceil(root.stageW * root.dpr)), Math.min(root.realSize.height, Math.ceil(root.stageH * root.dpr)))
                                                        : Qt.size(Math.ceil(root.stageW * root.dpr), Math.ceil(root.stageH * root.dpr))
                    fillMode: Image.PreserveAspectFit
                    autoTransform: true
                    asynchronous: true
                    cache: false
                    smooth: true
                    visible: full.status !== Image.Ready
                }
                Image {
                    id: full
                    objectName: "lightboxFull"
                    anchors.fill: parent
                    source: root.zoomed ? root.src : ""
                    fillMode: Image.PreserveAspectFit
                    autoTransform: true
                    asynchronous: true
                    cache: false
                    smooth: true
                    mipmap: true
                    visible: status === Image.Ready
                }
            }
            Text {
                objectName: "lightboxError"
                anchors.centerIn: parent
                visible: root.open && root.cur.file !== undefined && (root.src.toString() === "" || fit.status === Image.Error)
                text: qsTr("这张照片显示不了")
                font.family: serifFamily; font.pixelSize: Theme.px(15); color: "#c8d0d8"
            }
        }

        // 滚轮、拖动、双击、点空白处。放在按钮下面
        MouseArea {
            id: area
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton   // 右键不漏到下面的地图
            hoverEnabled: true
            cursorShape: root.zoomed ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor
            property real lastX: 0
            property real lastY: 0
            property bool moved: false
            onPressed: (m) => { lastX = m.x; lastY = m.y; moved = false }
            onPositionChanged: (m) => {
                if (!pressed || !root.zoomed) return
                root.panX += m.x - lastX
                root.panY += m.y - lastY
                lastX = m.x
                lastY = m.y
                moved = true
                root.clampPan()
            }
            onClicked: (m) => {
                if (m.button !== Qt.LeftButton || moved || root.zoomed) return
                const p = mapToItem(pic, m.x, m.y)
                if (p.x < 0 || p.y < 0 || p.x > pic.width || p.y > pic.height) root.close()
            }
            onDoubleClicked: root.resetZoom()
            onWheel: (w) => {
                const c = mapToItem(stage, w.x, w.y)
                root.zoomAt(Math.pow(1.2, w.angleDelta.y / 120), c.x - stage.width / 2, c.y - stage.height / 2)
            }
        }

        // ---- 上面：序号、百分比、关闭 ----
        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(14)
            y: Theme.px(12)
            spacing: Theme.px(14)
            Text {
                objectName: "lightboxPercent"
                anchors.verticalCenter: parent.verticalCenter
                text: root.percent + "%"
                font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: "#c8d0d8"
            }
            Text {
                objectName: "lightboxCount"
                anchors.verticalCenter: parent.verticalCenter
                visible: root.items.length > 1
                text: (root.index + 1) + " / " + root.items.length
                font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: "#c8d0d8"
            }
            LightCtl { objectName: "lightboxClose"; name: "close"; label: qsTr("关闭"); onClicked: root.close() }
        }
        // ---- 左右 ----
        LightCtl {
            objectName: "lightboxPrev"
            visible: root.items.length > 1
            x: Theme.px(10)
            anchors.verticalCenter: parent.verticalCenter
            glyph: "‹"
            label: qsTr("上一张")
            onClicked: root.step(-1)
        }
        LightCtl {
            objectName: "lightboxNext"
            visible: root.items.length > 1
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(10)
            anchors.verticalCenter: parent.verticalCenter
            glyph: "›"
            label: qsTr("下一张")
            onClicked: root.step(1)
        }
        // ---- 下面：说明、站点和时间 ----
        Column {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.px(12)
            x: Theme.px(24)
            width: parent.width - 2 * x
            spacing: Theme.px(3)
            Text {
                objectName: "lightboxCaption"
                visible: !!root.cur.caption
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                maximumLineCount: 2
                elide: Text.ElideRight
                wrapMode: Text.Wrap
                text: root.cur.caption || ""
                font.family: serifFamily; font.pixelSize: Theme.px(15); color: "#f2f4f6"
            }
            Text {
                objectName: "lightboxMeta"
                visible: text !== ""
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: root.metaText
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: "#a9b4bf"
            }
        }
    }

    component LightCtl: Rectangle {
        id: ctl
        property string name: ""
        property string glyph: ""
        property string label: ""
        signal clicked()
        width: Theme.px(38); height: width
        radius: width / 2
        color: ctlMa.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(1, 1, 1, 0.1)
        Accessible.role: Accessible.Button
        Accessible.name: label
        TpIcon { visible: ctl.name !== ""; anchors.centerIn: parent; width: Theme.px(18); height: width; name: ctl.name; color: "#ffffff" }
        Text { visible: ctl.glyph !== ""; anchors.centerIn: parent; anchors.verticalCenterOffset: -Theme.px(2); text: ctl.glyph; font.pixelSize: Theme.px(26); color: "#ffffff" }
        MouseArea { id: ctlMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ctl.clicked() }
    }
}
