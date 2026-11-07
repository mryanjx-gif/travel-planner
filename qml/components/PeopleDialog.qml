// 同行人窗口（0.11.5，P-13）：计划页打开。内容在 PeoplePane，单笔窗口里是同一窗口内翻过去（0.12.8）。
import QtQuick
import TPApp

ModalLayer {
    id: root
    property alias money: pane.money
    cardWidth: 420
    onDismissed: pane.close()

    function show() {
        open = true
        pane.show()
    }
    PeoplePane {
        id: pane
        maxListHeight: Math.max(Theme.px(110), root.height - Theme.px(340))
        onClosed: root.open = false
    }
}
