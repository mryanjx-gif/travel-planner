// 6 类色条和图例（0.10.1 消费，0.10.2 账本共用）。cats = plans::spendInfo / ledgerInfo 的 cats
import QtQuick
import TPApp

Column {
    id: root
    property var cats: []
    property bool whole: false          // 金额取整（账本）
    visible: cats.length > 0
    spacing: Theme.px(6)
    Row {
        id: catBar
        width: parent.width
        spacing: 1
        Repeater {
            model: root.cats
            delegate: Rectangle {
                required property var modelData
                width: (catBar.width - (root.cats.length - 1)) * modelData.share
                height: Theme.px(9)
                color: Fmt.catColors[modelData.k]
            }
        }
    }
    Flow {
        width: parent.width
        spacing: Theme.px(12)
        Repeater {
            model: root.cats
            delegate: Row {
                required property var modelData
                spacing: Theme.px(5)
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: Theme.px(9); height: width; radius: width / 2; color: Fmt.catColors[parent.modelData.k] }
                Text {
                    text: Fmt.catLabel(parent.modelData.k) + " " + Fmt.money(root.whole ? Math.round(parent.modelData.v) : parent.modelData.v) + " · " + parent.modelData.pct + "%"
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.textSoft
                }
            }
        }
    }
}
