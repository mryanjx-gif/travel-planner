// 数据文件（travel.json）被别处改过的提示（D-09；PO 2026-11-02 选方案 B：右下角卡片，"稍后 / 重新加载"）。
// 窗口里有没保存的改动时换一句文案（和 Electron showExtBar 一样）。
import QtQuick
import TPApp

GlassPanel {
    id: root
    property bool open: false
    property bool dirty: false
    signal reloadClicked()
    signal laterClicked()

    width: Math.min(Theme.px(340), parent ? parent.width - 28 : 340)
    height: col.implicitHeight + Theme.px(32)
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    transform: Translate { y: root.open || !Theme.motion ? 0 : Theme.px(12) }
    Behavior on opacity { NumberAnimation { duration: Theme.durPanel } }

    Column {
        id: col
        x: Theme.px(18)
        y: Theme.px(16)
        width: root.width - 2 * x
        spacing: Theme.px(10)
        Row {
            width: parent.width
            spacing: Theme.px(10)
            Text {   // ↻
                text: "↻"
                font.pixelSize: Theme.px(18)
                color: root.dirty ? Theme.warn : Theme.accent
                anchors.top: parent.top
            }
            Column {
                width: parent.width - Theme.px(28) - closeBtn.width - Theme.px(10)
                spacing: Theme.px(4)
                Text {
                    width: parent.width
                    text: qsTr("数据在别处更新了")
                    font.family: serifFamily
                    font.pixelSize: Theme.px(15)
                    font.bold: true
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
                Text {
                    width: parent.width
                    text: root.dirty ? qsTr("数据文件被别处改过。重新加载会丢掉当前窗口里没保存的改动。")
                                     : qsTr("数据文件被别的程序改过（比如另一个旅行计划窗口）。重新加载可以看到最新数据。")
                    font.family: serifFamily
                    font.pixelSize: Theme.px(13)
                    lineHeight: 1.25
                    color: Theme.muted
                    wrapMode: Text.Wrap
                }
            }
            TpButton {
                id: closeBtn
                text: "×"
                kind: "ghost"
                compact: true
                width: Theme.px(32)
                Accessible.name: qsTr("关闭提示")
                onClicked: root.laterClicked()
            }
        }
        Row {
            anchors.right: parent.right
            spacing: Theme.px(8)
            TpButton { text: qsTr("稍后"); compact: true; onClicked: root.laterClicked() }
            TpButton { text: qsTr("重新加载"); kind: "primary"; compact: true; onClicked: root.reloadClicked() }
        }
    }
}
