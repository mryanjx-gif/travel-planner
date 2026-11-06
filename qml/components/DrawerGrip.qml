// 底部抽屉的把手（0.8.9，窄窗口 ≤ 768 时简易面板用；PO 选方案 B）：上下拖改高矮（松手吸到一半 / 大半 / 只露标题），双击切换。
import QtQuick
import TPApp

Item {
    id: root
    height: Theme.px(18)
    signal dragged(real dy, bool done)
    signal toggled()
    Rectangle { anchors.centerIn: parent; width: Theme.px(40); height: Theme.px(4); radius: 2; color: Theme.faint }
    DragHandler {
        target: null
        xAxis.enabled: false
        property real startY: 0
        onActiveChanged: if (active) startY = centroid.scenePosition.y; else root.dragged(centroid.scenePosition.y - startY, true)
        onCentroidChanged: if (active) root.dragged(centroid.scenePosition.y - startY, false)
    }
    TapHandler { onDoubleTapped: root.toggled() }
    HoverHandler { cursorShape: Qt.SizeVerCursor }
}
