// 输入框的柔和焦点（0.12.2）：框外一圈淡光，框不变粗、文字不跳。按 Tab 键进来时加深，键盘操作看得清。
// 用法：放进输入框的背景 Rectangle 里，FocusRing { target: edit }
import QtQuick
import TPApp

Rectangle {
    property Item target: null
    readonly property bool byKey: target && (target.focusReason === Qt.TabFocusReason || target.focusReason === Qt.BacktabFocusReason)
    anchors.fill: parent
    anchors.margins: -3
    radius: parent.radius + 3
    color: "transparent"
    border.width: 3
    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, byKey ? 0.5 : 0.2)
    opacity: target && target.activeFocus ? 1 : 0
    visible: opacity > 0.01
    Behavior on opacity { TpMove { ms: 160 } }
}
