// 列表行的共同行为（0.12.9，PO 选）：放在一行里面（铺满，放在最前面，别的元素盖在它上面）。
// 鼠标移上去或 Tab 键选中时 active 为真（删除按钮看它才出现）；Enter、空格 = activated；Delete 键 = deleteRequested；右键菜单放这两个。
// 读 active 的元素自己决定怎么显示；只读时把 enabled 关掉。
import QtQuick
import TPApp

Item {
    id: root
    property bool canDelete: true
    property string editText: ""            // 右键菜单第一项（修改等）；空 = 没有
    readonly property bool active: hover.hovered || activeFocus
    signal activated()
    signal deleteRequested()
    anchors.fill: parent
    activeFocusOnTab: enabled

    function showMenu(x, y) {
        const list = []
        if (editText !== "") list.push({ text: editText, fn: () => root.activated() })
        if (canDelete) list.push({ text: qsTr("删除"), danger: true, fn: () => root.deleteRequested() })
        const p = root.mapToItem(null, x, y)
        if (list.length > 0) win.showRowMenu(p.x, p.y, list)
    }
    Keys.onDeletePressed: if (canDelete) deleteRequested()
    Keys.onReturnPressed: activated()
    Keys.onEnterPressed: activated()
    Keys.onSpacePressed: activated()
    Keys.onMenuPressed: showMenu(width / 2, height / 2)
    // Tab 到看不见的行：滚到看得见
    onActiveFocusChanged: {
        if (!activeFocus) return
        for (let f = parent; f; f = f.parent) {
            if (f.contentY === undefined || f.contentHeight === undefined) continue
            const p = root.mapToItem(f.contentItem, 0, 0)
            if (p.y < f.contentY) f.contentY = Math.max(0, p.y - Theme.px(8))
            else if (p.y + height > f.contentY + f.height) f.contentY = Math.min(f.contentHeight - f.height, p.y + height - f.height + Theme.px(8))
            return
        }
    }
    HoverHandler { id: hover; enabled: root.enabled }
    TapHandler {
        enabled: root.enabled
        acceptedButtons: Qt.RightButton
        onTapped: (ev) => root.showMenu(ev.position.x, ev.position.y)
    }
    Rectangle {   // 键盘选中的这一行
        anchors.fill: parent
        anchors.margins: -Theme.px(2)
        radius: Theme.px(10)
        color: "transparent"
        border.width: 2
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.5)
        visible: root.activeFocus
    }
}
