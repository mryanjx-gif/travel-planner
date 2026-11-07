// 整页的页头（0.12.10，PO 选"并进顶栏"）：日历、游记、账本的切换控件和 ⋯ 胶囊在同一行，夹在顶栏左右两组中间。
// 中间放不下时换到顶栏下面（窄窗口）；再放不下，左右两组各占一行（右边那组 anchors.bottom 贴底）。
// 用法：放在整页最外层，leftW / rightW 给两组控件的宽度；内容从 contentTop 开始。⋯ 分裂开时泡泡要占这一行，这里淡出让开。
import QtQuick
import TPApp

Item {
    id: head
    property real leftW: 0
    property real rightW: 0
    readonly property real gutter: Theme.px(16)
    readonly property real barL: topBar.leftEnd - win.gap + Theme.px(10)
    readonly property real barR: topBar.rightStart - win.gap - Theme.px(10)
    readonly property real need: leftW + rightW + Theme.px(12)
    readonly property bool inBar: need <= barR - barL
    readonly property real fullW: parent.width - 2 * gutter
    readonly property real contentTop: inBar ? topBar.barH : y + height + Theme.px(8)
    objectName: "barHead"
    z: 1
    x: inBar ? barL : gutter
    y: inBar ? (topBar.barH - Theme.px(36)) / 2 : topBar.barH
    width: inBar ? barR - barL : fullW
    height: Theme.px(!inBar && need > fullW ? 76 : 36)
    enabled: !hidden
    opacity: hidden ? 0 : 1
    readonly property bool hidden: inBar && topBar.bubblesOpen
    Behavior on opacity { NumberAnimation { duration: Theme.motion ? 160 : 0 } }
}
