// 整页占位（0.8.10，功能清单 S-01；PO 选 ① A：页名 + 一句话）：日历、游记、账本在阶段 ⑤ 做，现在先占住位置，
// 让 ⋯ 里 8 颗泡泡、Ctrl K、前进 / 后退、Esc 都按真的页面走。
// 整页的样子照 Electron mapfirst.js onPage + mapfirst.css：地图常驻最底层，整页从顶栏下沿铺满、盖住地图，淡入 .18 s。
// 阶段 ⑤ 做哪一页就把这一页换成真页面（容器的位置、淡入照用）。
import QtQuick
import TPApp

Item {
    id: root
    property Item backdrop: null
    property bool shown: false
    property string title: ""
    visible: opacity > 0.01
    opacity: shown ? 1 : 0
    Behavior on opacity { enabled: Theme.motion; NumberAnimation { duration: 180 } }

    GlassPanel {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: Theme.radius
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.985 : 0.99)
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }   // 不穿透到地图

    Column {
        anchors.centerIn: parent
        spacing: Theme.px(8)
        Text {
            objectName: "placeholderTitle"
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.title
            font.family: serifFamily; font.pixelSize: Theme.px(28); font.bold: true; color: Theme.text
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("这一页在阶段 ⑤ 做")
            font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.muted
        }
    }
}
