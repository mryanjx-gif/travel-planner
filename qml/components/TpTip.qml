// 图标按钮的鼠标提示（0.12.8）：放在按钮里，show 接 containsMouse；停 500 ms 才出现。
// 画在窗口最上层，不会被列表裁掉；窗口边上放不下时往里挪。文字放不下的按钮才用它，有字的按钮不用。
import QtQuick
import TPApp

Item {
    id: root
    property string text: ""
    property bool show: false
    readonly property Item host: parent
    Timer { id: wait; interval: 500; running: root.show && root.text !== ""; onTriggered: tip.place() }
    onShowChanged: if (!show) tip.visible = false

    Rectangle {
        id: tip
        objectName: "tpTip"
        parent: root.Window.window ? root.Window.window.contentItem : root
        visible: false
        z: 300
        width: tipText.implicitWidth + Theme.px(14)
        height: tipText.implicitHeight + Theme.px(8)
        radius: Theme.px(6)
        color: "#26323c"
        function place() {
            const w = parent.width, h = parent.height
            const p = root.host.mapToItem(parent, root.host.width / 2, 0)
            x = Math.round(Math.max(6, Math.min(w - width - 6, p.x - width / 2)))
            const below = p.y + root.host.height + Theme.px(6)
            y = Math.round(below + height > h - 6 ? p.y - height - Theme.px(6) : below)
            visible = true
        }
        Text {
            id: tipText
            anchors.centerIn: parent
            text: root.text
            font.family: serifFamily
            font.pixelSize: Theme.px(12)
            color: "#ffffff"
        }
    }
}
