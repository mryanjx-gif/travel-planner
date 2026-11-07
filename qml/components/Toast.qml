// 底部居中的小提示（"已保存"等），只放成功和不重要的结果。0.12.8：报错不走这里，show(msg, true) 发 errorRaised，Main 放进横幅。
// 0.8.7：可以带一个文字按钮（删除计划后的"撤销"），带按钮时停留 6 秒
import QtQuick
import TPApp

Rectangle {
    id: root
    objectName: "toast"
    readonly property string message: label.text
    property string actionText: ""
    property var action: null
    property bool showing: false
    property bool doneMark: false   // 文字前面画一笔对勾
    readonly property int stayMs: timer.interval
    signal errorRaised(string msg)
    function doAction() { const f = action; showing = false; opacity = 0; actionText = ""; action = null; if (f) f() }
    function show(msg, isError, actText, actFn, done, ms) {
        if (isError) { errorRaised(msg); return }
        doneMark = !!done
        label.text = msg
        actionText = actText || ""
        action = actFn || null
        opacity = 1
        showing = true
        timer.interval = ms || (actionText !== "" ? 6000 : 3500)
        timer.restart()
    }
    width: Math.min(label.implicitWidth + (root.doneMark ? Theme.px(20) : 0) + (actionText !== "" ? act.implicitWidth + Theme.px(16) : 0) + Theme.px(36), parent ? parent.width - 40 : 600)
    height: label.implicitHeight + Theme.px(20)
    radius: height / 2
    color: "#e8162a36"
    opacity: 0
    visible: opacity > 0.01
    z: 200
    Behavior on opacity { TpAnim { shown: root.showing } }
    TpDone {
        anchors.right: label.left
        anchors.rightMargin: Theme.px(6)
        anchors.verticalCenter: parent.verticalCenter
        on: root.doneMark && root.showing
        stroke: "#ffffff"
    }
    Text {
        id: label
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: (root.actionText !== "" ? -(act.implicitWidth + Theme.px(16)) / 2 : 0) + (root.doneMark ? Theme.px(10) : 0)
        width: Math.min(implicitWidth, root.parent ? root.parent.width - 76 - (root.actionText !== "" ? act.implicitWidth + Theme.px(16) : 0) : 560)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        font.family: serifFamily
        font.pixelSize: Theme.px(14)
        color: "#ffffff"
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
        color: "#bcd6ee"
        MouseArea {
            id: actMa
            anchors.fill: parent
            anchors.margins: -Theme.px(6)
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.doAction()
        }
    }
    Timer { id: timer; onTriggered: { root.showing = false; root.opacity = 0 } }
}
