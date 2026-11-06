// 开发版"照片测试"页（0.7.0）：看照片库、导入、旋转、清理，实测阶段 ① 的照片底层。
// 正式的相册（A-01…A-04）、计划页照片（P-14）、灯箱（A-07）在阶段 ④⑤ 做；这里的样子只是测试用。
import QtQuick
import TPApp

GlassPanel {
    id: root
    property bool open: false
    property var list: []
    property string selectedId: ""
    readonly property var selected: {
        for (let i = 0; i < list.length; ++i) if (list[i].id === selectedId) return list[i]
        return null
    }
    signal closeClicked()
    signal importClicked()
    signal cleanupClicked()
    signal toastRequested(string text, bool error)

    radius: Theme.radiusLarge
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.durPanel; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curvePanel } }
    function refresh() { list = photos.library() }
    onOpenChanged: if (open) { refresh(); forceActiveFocus() }
    Connections { target: photos; function onLibraryChanged() { if (root.open) root.refresh() } }
    Keys.onEscapePressed: closeClicked()
    MouseArea { anchors.fill: parent; onWheel: (w) => w.accepted = true }

    function sizeText(b) { return b >= 1048576 ? (b / 1048576).toFixed(1) + " MB" : Math.max(0, Math.round(b / 1024)) + " KB" }
    function placeText(p) {
        if (!p) return ""
        if (p.plan === "") return qsTr("未归类")
        return (p.planName || p.plan) + (p.stopName ? " · " + p.stopName : "")
    }

    // ---- 标题栏 ----
    Item {
        id: head
        width: parent.width
        height: Theme.px(64)
        Text {
            id: title
            x: Theme.px(24)
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("照片测试")
            font.family: serifFamily; font.pixelSize: Theme.px(20); font.bold: true; color: Theme.text
        }
        Text {
            anchors.left: title.right
            anchors.leftMargin: Theme.px(10)
            anchors.baseline: title.baseline
            text: qsTr("%1 张照片").arg(root.list.length) + " · " + qsTr("开发版")
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(16)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(8)
            TpButton { compact: true; text: qsTr("导入照片…"); enabled: !app.readOnly && photos.importStage === ""; onClicked: root.importClicked() }
            TpButton { compact: true; text: qsTr("清理没用的照片…"); enabled: !app.readOnly; onClicked: root.cleanupClicked() }
            TpButton { width: Theme.px(36); compact: true; kind: "ghost"; text: "×"; Accessible.name: qsTr("关闭"); onClicked: root.closeClicked() }
        }
    }

    // ---- 照片格子 ----
    GridView {
        id: grid
        anchors.top: head.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: detail.visible ? detail.left : parent.right
        anchors.leftMargin: Theme.px(16)
        anchors.bottomMargin: Theme.px(12)
        clip: true
        model: root.list
        cellWidth: Math.floor(width / Math.max(1, Math.floor(width / Theme.px(170))))
        cellHeight: cellWidth * 0.75 + Theme.px(46)
        boundsBehavior: Flickable.StopAtBounds
        delegate: Item {
            id: cell
            required property var modelData
            width: grid.cellWidth
            height: grid.cellHeight
            Rectangle {
                id: pic
                x: Theme.px(4); y: Theme.px(4)
                width: parent.width - Theme.px(8)
                height: width * 0.75
                radius: Theme.px(10)
                color: Theme.panel2
                clip: true
                border.width: root.selectedId === cell.modelData.id ? 3 : 0
                border.color: Theme.accent
                Image {
                    id: img
                    anchors.fill: parent
                    anchors.margins: parent.border.width
                    source: cell.modelData.missing || cell.modelData.heic ? "" : cell.modelData.thumb
                    sourceSize.width: 320
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width - Theme.px(16)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: cell.modelData.missing || cell.modelData.heic || img.status === Image.Error
                    text: cell.modelData.missing ? qsTr("照片找不到") : cell.modelData.heic ? qsTr("HEIC（还没法显示）") : qsTr("读不出来")
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                }
            }
            Text {
                anchors.top: pic.bottom
                anchors.topMargin: Theme.px(5)
                x: Theme.px(6)
                width: parent.width - Theme.px(12)
                text: cell.modelData.caption !== "" ? cell.modelData.caption : cell.modelData.file.replace(/^.*\//, "")
                elide: Text.ElideRight
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.text
            }
            Text {
                anchors.top: pic.bottom
                anchors.topMargin: Theme.px(22)
                x: Theme.px(6)
                width: parent.width - Theme.px(12)
                text: root.placeText(cell.modelData)
                elide: Text.ElideRight
                font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.selectedId = root.selectedId === cell.modelData.id ? "" : cell.modelData.id
            }
        }
        Text {
            anchors.centerIn: parent
            visible: root.list.length === 0
            text: qsTr("照片库里还没有照片。点右上角“导入照片…”试试。")
            font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.muted
        }
    }

    // ---- 右边：选中照片的信息 ----
    Rectangle {
        id: detail
        visible: root.selected !== null
        anchors.top: head.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: Theme.px(12)
        anchors.topMargin: 0
        width: Math.min(Theme.px(360), root.width * 0.42)
        radius: Theme.radius
        color: Qt.rgba(1, 1, 1, 0.55)
        border.width: 1
        border.color: Theme.line
        Flickable {
            anchors.fill: parent
            anchors.margins: Theme.px(14)
            clip: true
            contentHeight: info.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: info
                width: parent.width
                spacing: Theme.px(8)
                readonly property var p: root.selected || ({})
                Rectangle {
                    width: parent.width
                    height: width * 0.75
                    radius: Theme.px(10)
                    color: Theme.panel2
                    clip: true
                    Image {
                        id: big
                        anchors.fill: parent
                        source: info.p.missing || info.p.heic ? "" : (info.p.thumbLarge || "")
                        sourceSize.width: 1280
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: big.status !== Image.Ready
                        text: info.p.missing ? qsTr("照片找不到") : info.p.heic ? qsTr("HEIC（还没法显示）") : big.status === Image.Error ? qsTr("读不出来") : qsTr("加载中…")
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                }
                Row {
                    spacing: Theme.px(8)
                    TpButton {
                        compact: true
                        text: qsTr("向右转 90°")
                        enabled: !app.readOnly && !info.p.missing
                        onClicked: {
                            if (!info.p.canRotate) { root.toastRequested(qsTr("这种格式不能旋转"), true); return }
                            const r = photos.rotate(info.p.id, 1)
                            if (!r.ok) root.toastRequested(qsTr("没能旋转：") + r.error, true)
                        }
                    }
                    TpButton {
                        compact: true
                        text: qsTr("向左转 90°")
                        enabled: !app.readOnly && !info.p.missing
                        onClicked: {
                            if (!info.p.canRotate) { root.toastRequested(qsTr("这种格式不能旋转"), true); return }
                            const r = photos.rotate(info.p.id, 3)
                            if (!r.ok) root.toastRequested(qsTr("没能旋转：") + r.error, true)
                        }
                    }
                }
                Repeater {
                    model: {
                        const p = info.p, rows = []
                        if (!p.id) return rows
                        rows.push([qsTr("文件"), p.file])
                        rows.push([qsTr("归属"), root.placeText(p)])
                        if (p.takenAt) rows.push([qsTr("拍摄时间"), p.takenAt.replace("T", " ") + (p.tz ? " (UTC" + p.tz + ")" : "")])
                        if (p.lat !== undefined) rows.push([qsTr("位置"), p.lat.toFixed(5) + ", " + p.lng.toFixed(5) + (p.alt !== undefined ? qsTr("，海拔 %1 米").arg(p.alt) : "")])
                        if (p.dir !== undefined) rows.push([qsTr("拍摄方向"), p.dir + "°"])
                        if (p.width) rows.push([qsTr("尺寸"), p.width + " × " + p.height])
                        if (p.camera) rows.push([qsTr("相机"), p.camera])
                        if (p.lens) rows.push([qsTr("镜头"), p.lens])
                        const ex = photos.exposureText(p)
                        if (ex) rows.push([qsTr("拍摄参数"), ex])
                        return rows
                    }
                    delegate: Column {
                        required property var modelData
                        width: info.width
                        spacing: Theme.px(1)
                        Text { text: modelData[0]; font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted }
                        Text { width: parent.width; text: modelData[1]; wrapMode: Text.WrapAnywhere; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text }
                    }
                }
            }
        }
    }
}
