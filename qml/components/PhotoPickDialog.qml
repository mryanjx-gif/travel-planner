// 攻略：从这个计划的照片里挑一张（0.10.4，P-16；Electron pickPlanPhoto）。rows = planPage.planPhotos()。
import QtQuick
import TPApp

ModalLayer {
    id: root
    property var rows: []
    signal picked(var photo)
    cardWidth: 580
    onDismissed: open = false

    function show(list) { rows = list; open = true }

    Text { text: qsTr("插入照片"); font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text }
    Flickable {
        width: parent.width
        height: Math.min(grid.implicitHeight, root.height - Theme.px(190))
        contentHeight: grid.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Grid {
            id: grid
            width: parent.width
            columns: 4
            spacing: Theme.px(6)
            Repeater {
                model: root.open ? root.rows : []
                delegate: Item {
                    required property var modelData
                    required property int index
                    objectName: "guidePick-" + index
                    width: (grid.width - 3 * grid.spacing) / 4
                    height: width * 0.75
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.caption || modelData.stop
                    Image {
                        anchors.fill: parent
                        source: parent.modelData.thumb || ""
                        fillMode: Image.PreserveAspectCrop
                        clip: true
                        asynchronous: true
                    }
                    Rectangle { anchors.fill: parent; color: "transparent"; border.width: pickMa.containsMouse ? 2 : 0; border.color: Theme.accent }
                    MouseArea {
                        id: pickMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root.picked(parent.modelData); root.open = false }   // 先发：关上时模型清空，这一格马上销毁
                    }
                }
            }
        }
    }
    Row {
        anchors.right: parent.right
        TpButton { text: qsTr("取消"); onClicked: root.open = false }
    }
}
