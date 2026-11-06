// 下拉选择（0.7.0，导入确认表的计划 / 站点用）。options: [{ key, text }]
// 列表放到窗口最上层显示（不会被对话框里的滚动区域裁掉），点别处或按 Esc 收起；下面放不下时往上展开。
import QtQuick
import TPApp

Item {
    id: root
    property var options: []
    property string current: ""
    property string placeholder: ""
    signal picked(string key)
    implicitHeight: Theme.px(32)
    implicitWidth: Theme.px(160)
    opacity: enabled ? 1 : 0.5
    activeFocusOnTab: true
    Accessible.role: Accessible.ComboBox
    Accessible.name: label.text
    Keys.onSpacePressed: openList()
    Keys.onReturnPressed: openList()

    readonly property string currentText: {
        for (let i = 0; i < options.length; ++i) if (options[i].key === current) return options[i].text
        return placeholder
    }

    function openList() {
        const host = root.Window.window ? root.Window.window.contentItem : null
        if (!host || !root.enabled) return
        shield.parent = host
        pop.parent = host
        const p = root.mapToItem(host, 0, root.height + Theme.px(4))
        const h = Math.min(list.contentHeight + Theme.px(8), Theme.px(300))
        pop.width = Math.max(root.width, Theme.px(180))
        pop.height = h
        pop.x = Math.min(p.x, host.width - pop.width - 8)
        pop.y = (p.y + h > host.height - 8) ? Math.max(8, p.y - root.height - h - Theme.px(8)) : p.y
        let idx = 0
        for (let i = 0; i < options.length; ++i) if (options[i].key === current) idx = i
        list.positionViewAtIndex(idx, ListView.Contain)
        shield.visible = true
        pop.visible = true
        pop.forceActiveFocus()
    }
    function closeList() { pop.visible = false; shield.visible = false; root.forceActiveFocus() }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusControl
        color: area.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.72)
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.accent : Theme.lineStrong
    }
    Text {
        id: label
        x: Theme.px(10)
        width: parent.width - x - Theme.px(24)
        anchors.verticalCenter: parent.verticalCenter
        text: root.currentText
        elide: Text.ElideRight
        font.family: serifFamily
        font.pixelSize: Theme.px(13)
        color: root.current === "" && root.placeholder !== "" ? Theme.muted : Theme.text
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(9)
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        font.pixelSize: Theme.px(12)
        color: Theme.muted
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.openList()
    }

    // 点列表外面收起
    MouseArea {
        id: shield
        visible: false
        anchors.fill: parent
        z: 999
        onClicked: root.closeList()
        onWheel: (w) => w.accepted = true
    }
    Rectangle {
        id: pop
        visible: false
        z: 1000
        radius: Theme.radiusSmall
        color: Theme.panelLight
        border.width: 1
        border.color: Theme.lineStrong
        Keys.onEscapePressed: root.closeList()
        Keys.onUpPressed: list.decrementCurrentIndex()
        Keys.onDownPressed: list.incrementCurrentIndex()
        Keys.onReturnPressed: if (list.currentIndex >= 0) { root.picked(root.options[list.currentIndex].key); root.closeList() }
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: Theme.px(4)
            clip: true
            model: root.options
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                id: opt
                required property var modelData
                required property int index
                width: list.width
                height: Theme.px(30)
                radius: Theme.px(7)
                color: optArea.containsMouse || list.currentIndex === index ? Theme.accentSoft : "transparent"
                Text {
                    x: Theme.px(9)
                    width: parent.width - 2 * x
                    anchors.verticalCenter: parent.verticalCenter
                    text: opt.modelData.text
                    elide: Text.ElideRight
                    font.family: serifFamily
                    font.pixelSize: Theme.px(13)
                    font.bold: opt.modelData.key === root.current
                    color: Theme.text
                }
                MouseArea {
                    id: optArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { root.picked(opt.modelData.key); root.closeList() }
                }
            }
        }
    }
}
