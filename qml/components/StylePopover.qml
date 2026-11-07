// 标记样式选择器（0.8.9，M-11；PO 选方案 B：从"样式"按钮旁边弹出的小浮层，点一下立刻生效，点外面或 Esc 收起）。
// 内容照 Electron map-app.js openStylePicker / renderStylePicker：颜色（8 种 + 自定义）、外形（圆 / 方 / 水滴 / 菱形）、
// 图标（分类 + 搜索，Material Symbols），每一项都可以"不单独设置"（计划：恢复默认；站点：跟随计划）。
// 用法：openAt(按钮, { title, value: {color, shape, icon}, defaults: {color, shape, icon}, inheritLabel, onApply(v) })
import QtQuick
import TPApp

Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 80
    property var opts: null
    property var v: ({ color: "", shape: "", icon: "" })
    property string cat: ""
    property string q: ""
    property var groups: []
    property bool customOpen: false
    readonly property var eff: opts ? { color: v.color || opts.defaults.color, shape: v.shape || opts.defaults.shape, icon: v.icon || opts.defaults.icon || "" } : ({})

    property real anchorY: 0
    function openAt(anchor, o) {
        opts = o
        v = { color: o.value.color || "", shape: o.value.shape || "", icon: o.value.icon || "" }
        cat = ""; q = ""; customOpen = false
        if (!groups.length) groups = editor.iconGroups()
        search.value = ""
        const p = anchor.mapToItem(root, 0, 0)
        // 放在按钮左边（编辑器在右边）；放不下就放下面
        box.x = Math.max(8, p.x - box.width - Theme.px(10))
        anchorY = p.y   // y 是绑定：图标网格填好后高度会变，要跟着重新放（放不下往上挪）
        visible = true
        box.forceActiveFocus()
    }
    function close() { visible = false; opts = null }
    function apply() { if (opts && opts.onApply) opts.onApply({ color: v.color, shape: v.shape, icon: v.icon }) }
    function set(k, val) { const n = Object.assign({}, v); n[k] = val; v = n; apply() }

    MouseArea { anchors.fill: parent; onPressed: root.close() }   // 点外面收起

    Rectangle {
        id: box
        width: Theme.px(300)
        height: Math.min(col.implicitHeight + Theme.px(24), root.height - 16)
        y: Math.max(8, Math.min(root.anchorY - Theme.px(20), root.height - height - 8))
        clip: true
        radius: Theme.radius
        color: "#fbfcfd"
        border.color: Theme.line
        Keys.onEscapePressed: (e) => { root.close(); e.accepted = true }
        MouseArea { anchors.fill: parent; onWheel: (w) => w.accepted = true }
        Column {
            id: col
            x: Theme.px(14); y: Theme.px(12)
            width: parent.width - Theme.px(28)
            spacing: Theme.px(10)
            Row {
                width: parent.width
                spacing: Theme.px(8)
                MarkerShape { color: root.eff.color || "#e8590c"; shape: root.eff.shape || "circle"; icon: root.eff.icon || ""; size: Theme.px(26); anchors.verticalCenter: parent.verticalCenter }
                Text {
                    width: parent.width - Theme.px(70)
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.opts ? root.opts.title : ""
                    elide: Text.ElideRight
                    font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: Theme.text
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "✕"; font.pixelSize: Theme.px(14); color: Theme.muted
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("关闭")
                    TpTip { text: qsTr("关闭"); show: closeMa.containsMouse }
                    MouseArea { id: closeMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.close() }
                }
            }
            // 颜色
            Flow {
                width: parent.width
                spacing: Theme.px(6)
                Repeater {
                    model: editor.presetColors()
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool on: String(root.eff.color).toLowerCase() === String(modelData).toLowerCase()
                        width: Theme.px(22); height: width; radius: width / 2
                        color: modelData
                        border.width: on ? 2 : 0
                        border.color: "#ffffff"
                        Rectangle { anchors.centerIn: parent; width: parent.width + Theme.px(6); height: width; radius: width / 2; color: "transparent"; border.width: parent.on ? 2 : 0; border.color: Theme.accent }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.set("color", parent.modelData) }
                    }
                }
                Chip { text: qsTr("自定义颜色"); on: root.customOpen; onClicked: root.customOpen = !root.customOpen }
                Chip { visible: root.v.color !== ""; text: root.opts ? root.opts.inheritLabel : ""; onClicked: root.set("color", "") }
            }
            ColorWheel {
                visible: root.customOpen
                width: parent.width
                color: root.eff.color || "#2a78d6"
                onPicked: (c) => root.set("color", c)
            }
            // 外形
            Row {
                spacing: Theme.px(6)
                Repeater {
                    model: [{ k: "circle", t: qsTr("圆形") }, { k: "square", t: qsTr("方形") }, { k: "pin", t: qsTr("水滴") }, { k: "diamond", t: qsTr("菱形") }]
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool on: root.eff.shape === modelData.k
                        width: Theme.px(40); height: Theme.px(40); radius: Theme.radiusControl
                        color: on ? Theme.accentSoft : (sMa.containsMouse ? "#ffffff" : "transparent")
                        border.width: on ? 1 : 0; border.color: Theme.accent
                        MarkerShape { anchors.centerIn: parent; color: root.eff.color || "#e8590c"; shape: parent.modelData.k; icon: root.eff.icon || ""; size: Theme.px(22) }
                        MouseArea { id: sMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.set("shape", parent.modelData.k) }
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.t
                    }
                }
                Chip { visible: root.v.shape !== ""; text: root.opts ? root.opts.inheritLabel : ""; anchors.verticalCenter: parent.verticalCenter; onClicked: root.set("shape", "") }
            }
            // 图标：搜索 + 分类 + 格子
            TpField { id: search; width: parent.width; placeholder: qsTr("搜索图标"); onEdited: (t) => { root.q = t; root.cat = "" } }
            Flow {
                width: parent.width
                spacing: Theme.px(4)
                Chip { text: qsTr("全部"); on: root.cat === ""; onClicked: root.cat = "" }
                Repeater {
                    model: root.groups
                    delegate: Chip { required property var modelData; text: modelData.group; on: root.cat === modelData.group; onClicked: root.cat = modelData.group }
                }
            }
            Flickable {
                id: iconFlick
                width: parent.width
                height: Math.min(Theme.px(190), grid.implicitHeight)
                contentHeight: grid.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Grid {
                    id: grid
                    columns: 8
                    spacing: Theme.px(3)
                    readonly property real cell: (iconFlick.width - 7 * spacing) / 8
                    readonly property var items: {
                        const out = [{ name: "", label: root.opts && root.opts.inheritLabel === qsTr("跟随计划") ? qsTr("跟随") : qsTr("无"), cp: "" }]
                        const qq = root.q.trim().toLowerCase()
                        for (const g of root.groups) {
                            if (root.cat !== "" && g.group !== root.cat) continue
                            for (const i of g.icons) if (!qq || i.name.indexOf(qq) >= 0 || i.label.toLowerCase().indexOf(qq) >= 0) out.push(i)
                        }
                        return out
                    }
                    Repeater {
                        model: grid.items
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool on: (root.v.icon || "") === modelData.name
                            width: grid.cell; height: grid.cell; radius: Theme.px(6)
                            color: on ? Theme.accentSoft : (iMa.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.5))
                            border.width: on ? 2 : 1
                            border.color: on ? Theme.accent : Theme.line
                            Text {
                                anchors.centerIn: parent
                                text: parent.modelData.cp !== "" ? parent.modelData.cp : parent.modelData.label
                                font.family: parent.modelData.cp !== "" ? iconFamily : serifFamily
                                font.pixelSize: parent.modelData.cp !== "" ? Theme.px(18) : Theme.px(10)
                                color: parent.on ? Theme.accentInk : Theme.textSoft
                            }
                            MouseArea {
                                id: iMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.set("icon", parent.modelData.name)
                            }
                            ToolTipLite { text: parent.modelData.label; show: iMa.containsMouse && parent.modelData.cp !== "" }
                        }
                    }
                }
            }
            Text {
                visible: grid.items.length <= 1
                text: qsTr("没有匹配的图标。")
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
            }
        }
    }

    component Chip: Rectangle {
        property string text: ""
        property bool on: false
        signal clicked()
        width: cl.implicitWidth + Theme.px(16); height: Theme.px(24); radius: height / 2
        color: on ? Theme.accent : (cMa.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.7))
        border.width: on ? 0 : 1; border.color: Theme.line
        Text { id: cl; anchors.centerIn: parent; text: parent.text; font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: parent.on ? "#ffffff" : Theme.textSoft }
        MouseArea { id: cMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }
    component ToolTipLite: Rectangle {
        property string text: ""
        property bool show: false
        visible: show
        z: 10
        y: -height - 4
        x: (parent.width - width) / 2
        width: tl.implicitWidth + 12; height: tl.implicitHeight + 6; radius: 6
        color: "#26323c"
        Text { id: tl; anchors.centerIn: parent; text: parent.text; font.family: serifFamily; font.pixelSize: Theme.px(11); color: "#ffffff" }
    }
}
