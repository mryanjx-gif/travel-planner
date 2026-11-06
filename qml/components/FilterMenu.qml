// 筛选下拉（0.8.7，M-20）：挂在左下状态胶囊的"筛选"按钮上方。照 Electron index.html #filterMenu + map-app.js：
// 全选 / 清空（作用于状态、类型、区域全部选项；眼睛隐藏的不动）、类型、区域（每一行"只看"= 区域只留这一项，状态和类型不动）。
// 状态在胶囊上切（同一份状态）。下面一行：眼睛隐藏了几个计划 + "全部显示"（隐藏按钮在 0.8.8 的"我的旅程"列表里）。
// 点别处、Esc 收起。
// 0.9.7：最下面加"叠上其他旅程"开关（左下胶囊去重复，PO 选方案 A）。
import QtQuick
import TPApp

Item {
    id: root
    property Item backdrop: null
    property bool open: false
    width: Theme.px(Fmt.en ? 360 : 300)
    height: col.implicitHeight + Theme.px(26)
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    transform: Translate { y: root.open ? 0 : Theme.px(8) ; Behavior on y { enabled: Theme.motion; NumberAnimation { duration: Theme.durPanel; easing.type: Easing.OutCubic } } }
    Behavior on opacity { enabled: Theme.motion; NumberAnimation { duration: 160 } }

    function regionLabel(r) { return qsTr(r) }
    function containsGlobal(gx, gy) {
        const p = root.mapFromItem(null, gx, gy)
        return p.x >= 0 && p.y >= 0 && p.x <= width && p.y <= height
    }

    GlassPanel {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: Theme.radius
    }
    MouseArea { anchors.fill: parent }   // 面板里的空白处不穿透到地图

    Column {
        id: col
        x: Theme.px(16)
        y: Theme.px(12)
        width: parent.width - Theme.px(32)
        spacing: Theme.px(4)

        Row {
            width: parent.width
            spacing: Theme.px(12)
            Text {
                text: qsTr("筛选")
                width: parent.width - allBtn.width - noneBtn.width - 2 * parent.spacing
                font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: Theme.text
            }
            LinkText { id: allBtn; text: qsTr("全选"); onClicked: plans.filterAll() }
            LinkText { id: noneBtn; text: qsTr("清空"); muted: true; onClicked: plans.filterNone() }
        }

        Text {
            text: qsTr("类型")
            topPadding: Theme.px(8)
            font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.muted
        }
        Repeater {
            model: [{ key: "single", text: qsTr("单一目的地") }, { key: "route", text: qsTr("路线") }]
            delegate: TpCheck {
                required property var modelData
                width: col.width
                text: modelData.text
                checked: plans.filterType.indexOf(modelData.key) >= 0
                onToggled: plans.toggleFilter("type", modelData.key)
            }
        }

        Text {
            text: qsTr("区域")
            topPadding: Theme.px(8)
            font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true; color: Theme.muted
        }
        Grid {
            columns: 2
            columnSpacing: Theme.px(12)
            rowSpacing: Theme.px(4)
            width: parent.width
            Repeater {
                model: plans.regions
                delegate: Item {
                    required property string modelData
                    width: (col.width - Theme.px(12)) / 2
                    height: Math.max(Theme.px(26), chk.implicitHeight)
                    TpCheck {
                        id: chk
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - only.width - Theme.px(4)
                        text: root.regionLabel(modelData)
                        checked: plans.filterRegion.indexOf(modelData) >= 0
                        onToggled: plans.toggleFilter("region", modelData)
                    }
                    LinkText {
                        id: only
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("只看")
                        small: true
                        onClicked: plans.onlyRegion(modelData)
                    }
                }
            }
        }

        Row {
            visible: plans.hiddenCount > 0
            topPadding: Theme.px(8)
            spacing: Theme.px(8)
            Text {
                text: qsTr("隐藏了 %1 个计划").arg(plans.hiddenCount)
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                anchors.verticalCenter: parent.verticalCenter
            }
            LinkText { text: qsTr("全部显示"); small: true; onClicked: plans.showAllHidden() }
        }

        // 0.9.7（PO 15:15 选方案 A）：叠上其他旅程（计划 / 想去 / 足迹有，照片没有；每种模式各自记住）
        Item { width: 1; height: Theme.px(4); visible: plans.overlayAvailable }
        Rectangle { width: parent.width; height: 1; color: Theme.line; visible: plans.overlayAvailable }
        TpCheck {
            objectName: "overlayCheck"
            visible: plans.overlayAvailable
            width: col.width
            text: qsTr("叠上其他旅程")
            checked: plans.overlay
            onToggled: plans.toggleOverlay()
        }
        // 0.10.5 叠哪些：另外两种模式，总开关开着才显示
        Repeater {
            model: ["plan", "wish", "foot"]
            delegate: TpCheck {
                required property string modelData
                objectName: "overlayPart-" + modelData
                visible: plans.overlay && modelData !== plans.mode
                x: Theme.px(24)
                width: col.width - x
                text: modelData === "plan" ? qsTr("叠加计划") : modelData === "wish" ? qsTr("叠加想去") : qsTr("叠加足迹")
                checked: plans.overlayParts[modelData] === true
                onToggled: (on) => plans.setOverlayPart(modelData, on)
            }
        }
        Text {
            visible: plans.overlayAvailable
            width: col.width
            wrapMode: Text.Wrap
            text: qsTr("其他状态的旅程画成淡灰细线，垫在下面")
            font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
        }
    }
}
