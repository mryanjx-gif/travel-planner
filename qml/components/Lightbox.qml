// 看大图（0.10.5，A-07；PO 选 6A、N1、N2）：盖住整个窗口，暗底；← → 切换，Esc 关闭，点空白处关闭；显示序号、说明、站点和拍摄时间；关掉回到原处。
// 独立组件：show(列表, 第几张)，阶段 ⑤ 的相册和照片模式也用它。列表每项 { file（数据文件夹里的相对路径）, thumb, caption, stop, stopNo, route, takenAt }，有哪项给哪项。
// N1：滚轮放大（以鼠标为中心）、拖动、双击还原。平时按屏幕大小解码（省内存）；放大时才加载原图，还原或换一张就放掉（占多少内存见 v0.10.5记录.md）。
// 0.11.1：★ 精选、F 键、旋转（A-07、A-08，只对库里的照片，也就是有 id 的）；地图上点开的标明"位置来自站点"；
// HEIC 走 Windows 自带的解码，没装扩展时说去哪装；旋转在后台做。
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
    // revision：转过方向后地址带 ?r=，才重新读
    readonly property url src: open && cur.file && photos.revision >= 0 ? photos.fileUrl(cur.file) : ""
    property var favOver: ({})             // 这次看大图里改过的精选（items 是点开时的一份）
    readonly property bool isFav: cur.id !== undefined && (favOver[cur.id] !== undefined ? favOver[cur.id] : cur.fav === true)
    readonly property bool heicMissing: open && !!cur.file && photos.revision >= 0 && photos.isHeic(cur.file) && !photos.heicReady()
    property string note: ""               // 旋转的结果、不能旋转的原因（几秒后消失）
    readonly property real dpr: Screen.devicePixelRatio
    readonly property real stageW: Math.max(1, width - 2 * Theme.px(width < Theme.px(620) ? 8 : 64))
    readonly property real stageH: Math.max(1, height - Theme.px(56) - Theme.px(cur.caption ? 84 : 58))
    // 原图的长宽（按 EXIF 方向摆正）从文件头读：从 Electron 导入的照片库里没记，缩放解码后的 implicitWidth 又不是原图的
    readonly property size realSize: open && cur.file && photos.revision >= 0 ? photos.imageSize(cur.file) : Qt.size(0, 0)
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
        if (cur.fromStop) a.push(qsTr("位置来自站点"))
        return a.join(" · ")
    }

    function show(list, i) {
        returnFocus = Window.activeFocusItem
        items = list
        index = Math.max(0, Math.min(i, list.length - 1))
        resetZoom()
        favOver = ({})
        note = ""
        open = true
        keys.forceActiveFocus()
    }
    function toggleFav() {
        if (cur.id === undefined) return
        const v = !isFav
        if (!photos.setFav(cur.id, v)) return
        const o = Object.assign({}, favOver)
        o[cur.id] = v
        favOver = o
    }
    function rotate() {
        if (cur.id === undefined || photos.rotating) return
        if (!photos.canRotate(cur.file)) { say(qsTr("这种格式不能旋转（只支持 JPG、PNG、WebP）")); return }
        photos.rotateLater(cur.id, 1)
    }
    function say(t) { note = t; noteTimer.restart() }
    Timer { id: noteTimer; interval: 4000; onTriggered: root.note = "" }
    Connections {
        target: photos
        function onRotateFinished(id, ok, error) {
            if (!root.open || id !== root.cur.id) return
            root.resetZoom()
            root.say(ok ? qsTr("已旋转并保存，原图已备份。") : qsTr("旋转失败：") + error)
        }
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
        note = ""
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
    Behavior on opacity { enabled: Theme.motion; TpAnim { shown: root.open } }
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
            else if (e.key === Qt.Key_F && !(e.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) root.toggleFav()
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
                    visible: !root.heicMissing && fit.opacity < 1 && full.status !== Image.Ready   // 没装扩展时下面压着提示文字
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
                    // 0.12.9 从糊到清：解好了淡入，盖住垫着的缩略图
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { TpMove { ms: 180 } }
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
                visible: root.open && root.cur.file !== undefined && !root.heicMissing && (root.src.toString() === "" || fit.status === Image.Error)
                width: Math.min(parent.width - Theme.px(40), Theme.px(460))
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                lineHeight: 1.35
                text: qsTr("这张照片显示不了") + (root.cur.missing ? "\n" + qsTr("文件找不到：%1。放回原处就能显示。").arg(root.cur.file || "") : "")
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
                visible: !root.heicMissing
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
            LightPill {
                objectName: "lightboxFav"
                visible: root.cur.id !== undefined
                text: root.isFav ? qsTr("★ 已精选") : qsTr("☆ 精选")
                on: root.isFav
                tip: root.isFav ? qsTr("取消精选（F）") : qsTr("设为精选（F）")
                onClicked: root.toggleFav()
            }
            LightPill {
                objectName: "lightboxRotate"
                visible: root.cur.id !== undefined
                dim: !!root.cur.file && !photos.canRotate(root.cur.file)
                text: photos.rotating ? qsTr("旋转中…") : qsTr("↻ 旋转")
                tip: qsTr("顺时针转 90°，保存到本地文件")
                onClicked: root.rotate()
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
        // HEIC 没装扩展（PO 第 6 题 A）：说去哪装。放在 MouseArea 上面，能点
        Column {
            objectName: "lightboxHeicHint"
            visible: root.heicMissing
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.px(48), Theme.px(460))
            spacing: Theme.px(10)
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("HEIC 照片显示不了，要装 HEIF / HEVC 扩展。原文件没动。")
                font.family: serifFamily; font.pixelSize: Theme.px(15); color: "#f2f4f6"
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.px(10)
                LightPill { objectName: "lightboxStoreHeif"; text: qsTr("HEIF 图像扩展"); onClicked: photos.openStore("heif") }
                LightPill { objectName: "lightboxStoreHevc"; text: qsTr("HEVC 视频扩展"); onClicked: photos.openStore("hevc") }
            }
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
            Text {
                objectName: "lightboxNote"
                visible: root.note !== ""
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: root.note
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: "#ffd24a"
            }
        }
    }

    component LightPill: Rectangle {
        id: pill
        property string text: ""
        property string tip: ""
        property bool on: false
        property bool dim: false
        signal clicked()
        width: pillT.implicitWidth + Theme.px(22)
        height: Theme.px(34)
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        radius: height / 2
        color: pillMa.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(1, 1, 1, 0.1)
        opacity: dim ? 0.45 : 1
        Accessible.role: Accessible.Button
        Accessible.name: tip !== "" ? tip : text
        Text { id: pillT; anchors.centerIn: parent; text: pill.text; font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: pill.on ? "#ffd24a" : "#ffffff" }
        MouseArea { id: pillMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pill.clicked() }
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
