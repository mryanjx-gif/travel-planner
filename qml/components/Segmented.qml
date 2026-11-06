// 分段选择（关闭按钮三选一、字号四档、语言）。options: [{ key, text }]
import QtQuick
import TPApp

Rectangle {
    id: root
    property var options: []
    property string current: ""
    property bool fill: false           // true = 平均铺满宽度
    signal picked(string key)
    implicitWidth: row.implicitWidth + Theme.px(6)
    implicitHeight: Theme.px(36)
    radius: Theme.radiusSmall
    color: Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.06)
    Accessible.role: Accessible.PageTabList

    Row {
        id: row
        x: Theme.px(3)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.px(4)
        Repeater {
            model: root.options
            delegate: Item {
                id: seg
                required property var modelData
                readonly property bool on: root.current === modelData.key
                width: root.fill ? (root.width - Theme.px(6) - Theme.px(4) * (root.options.length - 1)) / root.options.length
                                 : segLabel.implicitWidth + Theme.px(22)
                height: root.height - Theme.px(6)
                activeFocusOnTab: true
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.text
                Accessible.checked: on
                Keys.onSpacePressed: root.picked(modelData.key)
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.px(9)
                    color: seg.on ? Qt.rgba(1, 1, 1, 0.92) : (segArea.containsMouse ? Qt.rgba(1, 1, 1, 0.45) : "transparent")
                    border.width: seg.activeFocus ? 2 : (seg.on ? 1 : 0)
                    border.color: seg.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.9)
                }
                Text {
                    id: segLabel
                    anchors.centerIn: parent
                    text: seg.modelData.text
                    font.family: serifFamily
                    font.pixelSize: Theme.px(13)
                    font.bold: seg.on
                    color: seg.on ? Theme.text : Theme.muted
                }
                MouseArea {
                    id: segArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.picked(seg.modelData.key)
                }
            }
        }
    }
}
