// 整页的页头（日历、游记、账本）：1.3.1 起整页是浮在地图上的圆角面板（顶栏下面），页头放在面板里第一行，不再和顶栏同一行。
// 左右两组同一行放不下时各占一行（右边那组 anchors.bottom 贴底，twoRows 时靠左）。
// 用法：放在整页最外层，leftW / rightW 给两组控件的宽度；内容从 contentTop 开始。
import QtQuick
import TPApp

Item {
    id: head
    property real leftW: 0
    property real rightW: 0
    readonly property real gutter: Theme.px(16)
    readonly property real need: leftW + rightW + Theme.px(12)
    readonly property real fullW: parent.width - 2 * gutter
    readonly property real contentTop: y + height + Theme.px(8)
    objectName: "barHead"
    z: 1
    x: gutter
    y: Theme.px(12)
    width: fullW
    readonly property bool twoRows: need > fullW
    height: Theme.px(twoRows ? 76 : 36)
}
