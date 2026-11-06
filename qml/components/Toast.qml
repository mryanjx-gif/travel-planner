// 底部居中的小提示（"已保存"等）；出错时红色、停留更久（Electron showToast：3.5 秒 / 8 秒）。
// 0.8.7：可以带一个文字按钮（删除计划后的"撤销"），带按钮时停留 6 秒
import QtQuick
import TPApp

Rectangle {
    id: root
    property bool error: false
    property string actionText: ""
    property var action: null
    function show(msg, isError, actText, actFn) {
        label.text = msg
        error = !!isError
        actionText = actText || ""
        action = actFn || null
        opacity = 1
        timer.interval = isError ? 8000 : (actionText !== "" ? 6000 : 3500)
        timer.restart()
    }
    width: Math.min(label.implicitWidth + (actionText !== "" ? act.implicitWidth + Theme.px(16) : 0) + Theme.px(36), parent ? parent.width - 40 : 600)
    height: label.implicitHeight + Theme.px(20)
    radius: height / 2
    color: error ? "#f6e3dc" : "#e8162a36"
    border.width: error ? 1 : 0
    border.color: Qt.rgba(168 / 255, 64 / 255, 28 / 255, 0.35)
    opacity: 0
    visible: opacity > 0.01
    z: 200
    Behavior on opacity { NumberAnimation { duration: Theme.durPanel } }
    Text {
        id: label
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: root.actionText !== "" ? -(act.implicitWidth + Theme.px(16)) / 2 : 0
        width: Math.min(implicitWidth, root.parent ? root.parent.width - 76 - (root.actionText !== "" ? act.implicitWidth + Theme.px(16) : 0) : 560)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        font.family: serifFamily
        font.pixelSize: Theme.px(14)
        color: root.error ? Theme.dangerInk : "#ffffff"
    }
    Text {
        id: act
        objectName: "toastAction"
        visible: root.actionText !== ""
        anchors.left: label.right
        anchors.leftMargin: Theme.px(16)
        anchors.verticalCenter: parent.verticalCenter
        text: root.actionText
        font.family: serifFamily
        font.pixelSize: Theme.px(14)
        font.bold: true
        font.underline: actMa.containsMouse
        color: root.error ? Theme.dangerInk : "#bcd6ee"
        MouseArea {
            id: actMa
            anchors.fill: parent
            anchors.margins: -Theme.px(6)
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { const f = root.action; root.opacity = 0; root.actionText = ""; root.action = null; if (f) f() }
        }
    }
    Timer { id: timer; onTriggered: root.opacity = 0 }
}
