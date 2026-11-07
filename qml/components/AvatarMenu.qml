// 头像菜单（0.8.8，S-06、W-08）。Electron mapfirst.js #mfMenu：设置、更新日志（窄窗口时加页面入口）。
// 原生版没有系统菜单栏，PO 0.8.8 选了改进 b：把 Electron 系统菜单里的几项放进来——更换数据文件夹…、修改窗口标题…、退出（Ctrl Q）。
// 更新日志在阶段 ⑥ 做好后加；窄窗口的页面入口在 0.8.9（窄窗口）和阶段 ⑤（页面）。
// 点一项后收起；点别处、Esc 收起。
import QtQuick
import TPApp

Item {
    id: root
    property bool open: false
    property var items: []          // [{ key, text, kbd? }] 或 { sep: true }
    signal picked(string key)

    width: Math.max(Theme.px(220), col.implicitWidth + Theme.px(12))
    height: col.implicitHeight + Theme.px(12)
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    transform: Translate { y: root.open ? 0 : -Theme.px(6); Behavior on y { enabled: Theme.motion; TpAnim { shown: root.open } } }
    Behavior on opacity { enabled: Theme.motion; TpAnim { shown: root.open } }

    function containsGlobal(gx, gy) {
        if (!visible) return false
        const p = root.mapFromItem(null, gx, gy)
        return p.x >= 0 && p.y >= 0 && p.x <= width && p.y <= height
    }

    GlassPanel {
        anchors.fill: parent
        radius: Theme.px(14)
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.9 : 0.98)
    }
    MouseArea { anchors.fill: parent }

    Column {
        id: col
        x: Theme.px(6)
        y: Theme.px(6)
        width: root.width - Theme.px(12)
        Repeater {
            model: root.items
            delegate: Item {
                id: row
                required property var modelData
                width: col.width
                implicitWidth: modelData.sep ? 0 : label.implicitWidth + (kbd.visible ? kbd.width + Theme.px(24) : 0) + Theme.px(24)
                height: modelData.sep ? Theme.px(9) : Theme.px(38)
                objectName: modelData.sep ? "" : "menu-" + modelData.key
                Rectangle {
                    visible: row.modelData.sep === true
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.px(6); width: parent.width - Theme.px(12); height: 1
                    color: Theme.line
                }
                Rectangle {
                    visible: !row.modelData.sep
                    anchors.fill: parent
                    radius: Theme.px(9)
                    color: ma.containsMouse ? Qt.rgba(45 / 255, 95 / 255, 139 / 255, 0.08) : "transparent"
                }
                Text {
                    id: label
                    visible: !row.modelData.sep
                    x: Theme.px(12)
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.text || ""
                    font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.text
                }
                Rectangle {
                    id: kbd
                    visible: !row.modelData.sep && (row.modelData.kbd || "") !== ""
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.px(10)
                    anchors.verticalCenter: parent.verticalCenter
                    width: kt.implicitWidth + Theme.px(10); height: Theme.px(18); radius: Theme.px(5)
                    color: "transparent"
                    border.color: Theme.lineStrong
                    Text { id: kt; anchors.centerIn: parent; text: row.modelData.kbd || ""; font.pixelSize: Theme.px(11); color: Theme.muted }
                }
                MouseArea {
                    id: ma
                    anchors.fill: parent
                    enabled: !row.modelData.sep && root.open
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.MenuItem
                    Accessible.name: row.modelData.text || ""
                    onClicked: root.picked(row.modelData.key)
                }
            }
        }
    }
}
