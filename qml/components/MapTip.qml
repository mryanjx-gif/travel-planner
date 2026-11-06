// 地图上的悬停提示框（0.8.2，Electron v6.3.0 map-app.js showTipAt / showTipFor / hideTip，样式 base.css .map-tip）：
// 同一时间只显示一个；(px, py) 是提示框底边中点；标记 / 圆圈的提示框在地图开始移动时藏起来（Electron movestart）。
// 文字是 StyledText（C++ 已经转义，名字加粗等用 <b>、换行用 <br>）。
import QtQuick
import TPApp

Item {
    id: tip
    property string kind: ""      // "el" 标记 / 圆圈、"unlock" 已解锁、"line" 路线（0.8.3）
    property string html: ""
    property real px: 0
    property real py: 0
    visible: kind !== ""
    z: 4000
    enabled: false                // 不挡鼠标（pointer-events: none）

    property var owner: null      // 谁显示的（鼠标从一个标记直接移到另一个时，旧的那个"离开"会晚到，不能把新的藏掉）
    function showAt(text, x, y, k, who) { html = text; px = x; py = y; kind = k || "el"; owner = who || null }
    function hide() { kind = ""; html = ""; owner = null }
    function hideFor(who) { if (owner === who) hide() }

    Rectangle {
        id: box
        x: Math.round(tip.px - width / 2)
        y: Math.round(tip.py - height)
        width: label.implicitWidth + 18
        height: label.implicitHeight + 10
        color: Theme.panel
        border.color: Theme.line
        border.width: 1
        radius: 8
        // --shadow-sm：一层很淡的投影
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 1
            anchors.bottomMargin: -2
            radius: parent.radius + 1
            color: Theme.shadow
            opacity: 0.6
        }
        Text {
            id: label
            x: 9; y: 5
            text: tip.html
            textFormat: Text.StyledText
            color: Theme.text
            font.family: serifFamily
            font.pixelSize: 12
            lineHeight: 1.5
            wrapMode: Text.NoWrap
        }
    }
}
