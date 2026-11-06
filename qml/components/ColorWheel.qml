// 自定义颜色（0.8.9，样式选择器里；Electron 用系统取色框 <input type="color">，Qt 的取色框在 Windows 上不是系统样式）：
// 上面一块"深浅 × 鲜艳"、下面一条色相，再加一个色值框（#rrggbb）。松手发 picked("#rrggbb")。
import QtQuick
import TPApp

Column {
    id: root
    property color color: "#2a78d6"
    signal picked(string hex)
    spacing: Theme.px(6)
    property real hue: 0.6
    property real sat: 0.7
    property real val: 0.8
    property bool internal: false
    onColorChanged: if (!internal) { const c = Qt.color(color); hue = c.hsvHue < 0 ? hue : c.hsvHue; sat = c.hsvSaturation; val = c.hsvValue }
    function hex() {
        const c = Qt.hsva(hue, sat, val, 1)
        const h = (x) => { const s = Math.round(x * 255).toString(16); return s.length < 2 ? "0" + s : s }
        return "#" + h(c.r) + h(c.g) + h(c.b)
    }
    function emitPick() { internal = true; root.picked(hex()); internal = false }

    Rectangle {
        width: parent.width; height: Theme.px(90); radius: Theme.px(6)
        color: Qt.hsva(root.hue, 1, 1, 1)
        Rectangle { anchors.fill: parent; radius: parent.radius; gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: "#ffffffff" } GradientStop { position: 1; color: "#00ffffff" } } }
        Rectangle { anchors.fill: parent; radius: parent.radius; gradient: Gradient { GradientStop { position: 0; color: "#00000000" } GradientStop { position: 1; color: "#ff000000" } } }
        Rectangle {
            x: root.sat * parent.width - width / 2; y: (1 - root.val) * parent.height - height / 2
            width: Theme.px(12); height: width; radius: width / 2; color: "transparent"; border.width: 2; border.color: "#ffffff"
        }
        MouseArea {
            anchors.fill: parent
            function at(m) { root.sat = Math.max(0, Math.min(1, m.x / width)); root.val = Math.max(0, Math.min(1, 1 - m.y / height)) }
            onPressed: (m) => at(m)
            onPositionChanged: (m) => at(m)
            onReleased: root.emitPick()
        }
    }
    Rectangle {
        width: parent.width; height: Theme.px(12); radius: height / 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#ff0000" } GradientStop { position: 0.17; color: "#ffff00" } GradientStop { position: 0.33; color: "#00ff00" }
            GradientStop { position: 0.5; color: "#00ffff" } GradientStop { position: 0.67; color: "#0000ff" } GradientStop { position: 0.83; color: "#ff00ff" }
            GradientStop { position: 1.0; color: "#ff0000" }
        }
        Rectangle { x: root.hue * parent.width - width / 2; y: -2; width: Theme.px(8); height: parent.height + 4; radius: 3; color: "transparent"; border.width: 2; border.color: "#ffffff" }
        MouseArea {
            anchors.fill: parent; anchors.margins: -4
            function at(m) { root.hue = Math.max(0, Math.min(0.999, (m.x - 4) / (width - 8))) }
            onPressed: (m) => at(m)
            onPositionChanged: (m) => at(m)
            onReleased: root.emitPick()
        }
    }
    Row {
        spacing: Theme.px(6)
        Rectangle { width: Theme.px(28); height: Theme.px(28); radius: Theme.px(6); color: root.hex(); border.color: Theme.line; anchors.verticalCenter: parent.verticalCenter }
        TpField {
            width: Theme.px(110)
            value: root.hex()
            placeholder: "#rrggbb"
            onAccepted: {
                const t = text.trim()
                if (/^#?[0-9a-fA-F]{6}$/.test(t)) { root.internal = true; root.color = t[0] === "#" ? t : "#" + t; root.internal = false; const c = Qt.color(root.color); root.hue = c.hsvHue < 0 ? root.hue : c.hsvHue; root.sat = c.hsvSaturation; root.val = c.hsvValue; root.emitPick() }
            }
        }
    }
}
