// 雾蓝主题 token（从 Electron v6.3.0 提取，见 claude/技术选型.md 附录）+ 全局缩放（W-09 字号四档）。
// scale、motion、glass 由 Main.qml 绑定到 app 的设置。
pragma Singleton
import QtQuick

QtObject {
    // 全局缩放：小 0.9 / 中 1 / 大 1.12 / 超大 1.25
    property real scale: 1.0
    // "动效"和"透明效果"两个设置（设置页在阶段 ⑥；现在默认都开）
    property bool motion: true
    property bool glass: true
    property Item glassSource: null   // 全窗口共用的玻璃背景（Main.qml 给）

    function px(n) { return Math.round(n * scale) }

    // 颜色
    function seasonColor(k) { return k === "best" ? "#3f9a5a" : k === "nice" ? "#5b93c9" : k === "meh" ? "#e0b23f" : k === "bad" ? "#b9b2a7" : line }   // 季节条（计划页、想去）
    readonly property color bg: "#ebf0f4"
    readonly property color panel: "#f8fafc"
    readonly property color panel2: "#eef2f5"
    readonly property color panelLight: "#fdfefe"
    readonly property color text: "#16202a"
    readonly property color textSoft: "#3a4a57"
    readonly property color muted: "#5d6d7b"
    readonly property color faint: "#a9b6c0"
    readonly property color line: "#dbe3e9"
    readonly property color lineStrong: "#c4d0d9"
    readonly property color accent: "#2d5f8b"
    readonly property color accentInk: "#1f4a70"
    readonly property color accentSoft: "#e1ebf4"
    readonly property color warn: "#b04c0c"
    readonly property color ok: "#3f7a52"          // 0.5.0：备份状态"最近备份过"的小圆点
    readonly property color danger: "#a8401c"
    readonly property color dangerInk: "#8f3415"
    readonly property color glassTint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, glass ? 0.84 : 0.98)
    readonly property color glassEdge: Qt.rgba(1, 1, 1, 0.75)
    readonly property color scrim: Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.18)
    readonly property color shadow: Qt.rgba(30 / 255, 55 / 255, 80 / 255, 0.16)

    // 圆角（默认 / 小 / 控件 / 大面板）
    readonly property int radius: 20
    readonly property int radiusSmall: 12
    readonly property int radiusControl: 10
    readonly property int radiusLarge: 30

    // 动效：面板进场 .22s cubic-bezier(.2,.8,.2,1)
    readonly property int durPanel: motion ? 220 : 0
    readonly property var curvePanel: [0.2, 0.8, 0.2, 1.0, 1.0, 1.0]
}
