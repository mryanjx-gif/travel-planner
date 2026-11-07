// 列表行的右键菜单（0.12.9）：整个窗口一个，放在 Main 里，win.showRowMenu(x, y, 列表) 打开。列表每项 { text, danger, fn }。
// 点别处或 Esc 收起（Esc 在 Main 的 escChain）。
import QtQuick
import TPApp

Item {
    id: root
    objectName: "rowMenu"
    property var items: []
    anchors.fill: parent
    visible: false
    z: 250
    function show(px, py, list) {
        items = list
        box.x = Math.max(8, Math.min(px, width - box.width - 8))
        box.y = Math.max(8, Math.min(py, height - box.height - 8))
        visible = true
    }
    function hide() { visible = false }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: root.hide()
        onWheel: (w) => root.hide()
    }
    TpCard {
        id: box
        width: col.implicitWidth + Theme.px(12)
        height: col.implicitHeight + Theme.px(12)
        radius: Theme.px(12)
        color: Theme.card
        Column {
            id: col
            x: Theme.px(6); y: Theme.px(6)
            Repeater {
                model: root.items
                delegate: MenuRow {
                    required property var modelData
                    objectName: "rowMenuItem"
                    text: modelData.text
                    danger: !!modelData.danger
                    onClicked: { root.hide(); modelData.fn() }
                }
            }
        }
    }
}
