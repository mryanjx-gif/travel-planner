// 地图上的照片标记（0.11.1，A-04；PO 第 5 题 A：缩略图 + 张数，代表图优先精选）。数据和位置都在 C++（mapc.photoLayer）：
// 只有看得见的组占一行，移出视野的行留给新进来的，不增删元素。圆角只给白边，图片四角盖在白边里，不用遮罩（这里可能有几百个）。
import QtQuick
import TPApp

Item {
    id: overlay
    property var tip                     // MapTip
    property bool moving: mapc.moving
    property bool active: false
    signal photosOpened(var ids)

    Repeater {
        model: overlay.active ? mapc.photoLayer : null
        delegate: Item {
            id: mk
            required property int index
            required property var model
            readonly property real size: Theme.px(52)
            readonly property bool on: model.shown
            readonly property string file: model.file
            readonly property int n: model.count
            readonly property bool hot: hover.hovered
            x: model.mx - size / 2
            y: model.my - size / 2
            width: size
            height: size
            z: hot ? 2 : model.fav ? 1 : 0
            visible: on
            opacity: model.op
            scale: (hot ? 1.14 : 1) * model.sc
            Behavior on scale { enabled: Theme.motion && overlay.active && !overlay.moving; TpMove { ms: 120 } }
            objectName: "photoMk-" + index

            Rectangle {
                id: frame
                anchors.fill: parent
                radius: Theme.px(9)
                color: Theme.panel2
                border.width: 0
                Rectangle { anchors.fill: parent; anchors.margins: -1; z: -1; radius: parent.radius + 1; color: Qt.rgba(0, 0, 0, 0.28) }   // 一圈淡影（不用 layer）
                Rectangle { anchors.fill: parent; radius: parent.radius; color: "#ffffff" }
                Image {
                    id: pic
                    objectName: "photoMkImg"
                    anchors.fill: parent
                    anchors.margins: Theme.px(2.5)
                    // revision：转过方向后地址变，换新图
                    source: mk.on && mk.file !== "" && photos.revision >= 0 ? photos.thumbUrl(mk.file, 160) : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    clip: true
                }
                Text {
                    anchors.centerIn: parent
                    visible: pic.status === Image.Error
                    text: photos.isHeic(mk.file) ? "HEIC" : "?"
                    font.family: serifFamily; font.pixelSize: Theme.px(11); font.bold: true; color: Theme.muted
                }
            }
            // 张数（Electron：右上角，超过 999 写 999+）
            Rectangle {
                visible: mk.n > 1
                objectName: "photoMkCount"
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: -Theme.px(7)
                anchors.topMargin: -Theme.px(7)
                width: Math.max(height, cnt.implicitWidth + Theme.px(10))
                height: Theme.px(19)
                radius: height / 2
                color: Theme.accent
                border.width: 1.5
                border.color: "#ffffff"
                Text {
                    id: cnt
                    anchors.centerIn: parent
                    text: mk.n > 999 ? "999+" : mk.n
                    font.pixelSize: Theme.px(11); font.bold: true; color: "#ffffff"
                }
            }
            // 代表图是精选
            Text {
                visible: mk.model.fav
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.px(5)
                anchors.bottomMargin: Theme.px(3)
                text: "★"
                font.pixelSize: Theme.px(13); color: "#ffd24a"
                style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.45)
            }
            HoverHandler {
                id: hover
                enabled: mk.on && mk.model.hit
                cursorShape: Qt.PointingHandCursor
                onHoveredChanged: {
                    if (!overlay.tip) return
                    if (hovered && !overlay.moving) overlay.tip.showAt(mapc.photoLayer.tipOf(mk.index), mk.x + mk.width / 2, mk.y - 4, "el", mk)
                    else if (!hovered) overlay.tip.hideFor(mk)
                }
            }
            TapHandler {
                enabled: mk.on && mk.model.hit
                onTapped: {
                    if (overlay.tip) overlay.tip.hide()
                    const r = mapc.photoLayer.click(mk.index, Theme.motion)
                    if (r.ids) overlay.photosOpened(r.ids)
                }
            }
        }
    }
}
