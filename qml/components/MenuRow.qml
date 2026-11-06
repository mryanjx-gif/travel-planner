// 菜单里的一行（0.10.0 从 PlanSheet 拆出来，计划页的 ⋯ 也用）
import QtQuick
import TPApp

Rectangle {
    id: mr
    property string text: ""
    property bool danger: false
    signal clicked()
    width: Math.max(Theme.px(150), mrText.implicitWidth + Theme.px(24))
    height: visible ? Theme.px(34) : 0
    radius: Theme.radiusControl
    color: mrMa.containsMouse ? Theme.accentSoft : "transparent"
    Accessible.role: Accessible.MenuItem
    Accessible.name: text
    Text { id: mrText; x: Theme.px(12); anchors.verticalCenter: parent.verticalCenter; text: mr.text; font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: mr.danger ? Theme.dangerInk : Theme.text }
    MouseArea { id: mrMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: mr.clicked() }
}
