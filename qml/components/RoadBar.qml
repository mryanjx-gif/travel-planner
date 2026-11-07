// 地图上的提示条"有 N 段还没算"（0.8.4，功能清单 M-19；Electron index.html #roadBar、base.css #roadBar、map-app.js refreshRoadBar）。
// 一行：说明文字 + 主按钮（现在计算 / 停止 / 保存 / 好）+ ×（先不算）。数据和状态都在 C++（roads，src/services/roadrouter.*）。
// 位置：顶部居中、底图切换条下面（Electron 在顶栏下面，和"数据在别处更新了"同一处）。
import QtQuick
import TPApp

GlassPanel {
    id: root
    property bool open: roads.shown
    radius: Theme.radiusSmall
    width: Math.min(row.implicitWidth + Theme.px(26), maxWidth)
    property real maxWidth: Theme.px(620)
    height: Math.max(label.implicitHeight, goBtn.implicitHeight) + Theme.px(16)
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    Behavior on opacity { TpAnim { shown: root.open } }

    Row {
        id: row
        x: Theme.px(16)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.px(12)
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, root.maxWidth - Theme.px(26) - btns.width - row.spacing)
            text: roads.text
            wrapMode: Text.Wrap
            font.family: serifFamily
            font.pixelSize: Theme.px(13)
            lineHeight: 1.2
            color: Theme.text
        }
        Row {
            id: btns
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(4)
            TpButton {
                id: goBtn
                objectName: "roadBarGo"
                text: roads.button
                kind: "primary"
                compact: true
                onClicked: roads.primary()
            }
            TpButton {
                text: "×"
                kind: "ghost"
                compact: true
                width: Theme.px(32)
                Accessible.name: qsTr("先不算")
                tip: qsTr("先不算")
                onClicked: roads.dismiss()
            }
        }
    }
}
