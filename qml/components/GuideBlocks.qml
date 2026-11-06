// 攻略的显示（0.10.4，P-17）：plans::guideBlocks 排好的一块块。不嵌 WebView，每块一个 Text.StyledText（html 已转义）或一张图。
// 模型只是块数：内容变了（打了一个勾）不重建，图也不重新加载。预览（tasks = false）时勾选框不能点。
import QtQuick
import TPApp

Column {
    id: root
    property var blocks: []
    property bool tasks: false
    signal taskToggled(int line, bool on)
    signal photoClicked(int block)          // 0.10.5 点正文里的照片看大图
    function blockY(i) { const it = rep.itemAt(i); return it ? it.y + it.gap : 0 }

    Text {
        visible: root.blocks.length === 0
        text: qsTr("（还没有内容）")
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
    }
    Repeater {
        id: rep
        model: root.blocks.length
        delegate: Item {
            id: blk
            required property int index
            readonly property var b: root.blocks[index] || ({})
            readonly property string type: b.type || ""
            readonly property string prev: index > 0 ? (root.blocks[index - 1] || {}).type || "" : ""
            readonly property bool listy: type === "li" || type === "task"
            readonly property real gap: index === 0 ? 0 : type === "h1" ? Theme.px(18) : type === "h2" ? Theme.px(14)
                                      : listy && (prev === "li" || prev === "task") ? Theme.px(3) : Theme.px(8)
            readonly property real indent: type === "li" ? Theme.px(14) : type === "task" ? Theme.px(24) : type === "quote" ? Theme.px(12) : 0
            readonly property real picRatio: (b.width > 0 && b.height > 0) ? b.height / b.width
                                           : (pic.status === Image.Ready && pic.implicitWidth > 0) ? pic.implicitHeight / pic.implicitWidth : 0.6
            width: root.width
            height: gap + (type === "img" ? picCol.height : txt.contentHeight)

            // ---- 文字 ----
            Text {
                id: txt
                visible: blk.type !== "img"
                x: blk.indent
                y: blk.gap
                width: parent.width - x
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                text: blk.type === "img" ? "" : blk.b.html || ""
                lineHeight: 1.32
                linkColor: Theme.accent
                font.family: serifFamily
                font.pixelSize: Theme.px(blk.type === "h1" ? 18 : blk.type === "h2" ? 15.5 : 14)
                font.bold: blk.type === "h1" || blk.type === "h2"
                font.strikeout: blk.type === "task" && blk.b.done === true
                color: blk.type === "quote" || (blk.type === "task" && blk.b.done) ? Theme.muted : Theme.text
                onLinkActivated: (u) => Qt.openUrlExternally(u.replace(/&amp;/g, "&"))
                HoverHandler { cursorShape: txt.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
            }
            Text {   // 列表的点
                visible: blk.type === "li"
                y: blk.gap
                text: "•"
                font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.muted
            }
            Rectangle {   // 引用的竖线
                visible: blk.type === "quote"
                y: blk.gap
                width: Theme.px(3); height: txt.contentHeight; radius: width / 2
                color: Theme.lineStrong
            }
            TpCheck {
                objectName: "guideTask-" + blk.index
                visible: blk.type === "task"
                y: blk.gap - Theme.px(1)
                width: Theme.px(18)
                enabled: root.tasks
                checked: blk.b.done === true
                Accessible.name: txt.text
                onToggled: (c) => root.taskToggled(blk.b.line, c)
            }
            // ---- 照片 ----
            Column {
                id: picCol
                visible: blk.type === "img"
                y: blk.gap
                width: parent.width
                spacing: Theme.px(4)
                Image {
                    id: pic
                    visible: !!blk.b.thumb
                    width: parent.width
                    height: visible ? width * blk.picRatio : 0
                    source: blk.b.thumb || ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    MouseArea {
                        objectName: "guidePhoto-" + blk.index
                        anchors.fill: parent
                        enabled: parent.visible
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.photoClicked(blk.index)
                    }
                }
                Rectangle {
                    visible: blk.type === "img" && !blk.b.thumb
                    width: parent.width; height: visible ? Theme.px(44) : 0
                    radius: Theme.radiusControl
                    color: Theme.panel2
                    Text { anchors.centerIn: parent; text: qsTr("这张照片找不到了"); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted }
                }
                Text {
                    visible: !!blk.b.caption
                    width: parent.width
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    text: blk.b.caption || ""
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                }
            }
        }
    }
}
