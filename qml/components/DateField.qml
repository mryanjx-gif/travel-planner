// 日期框（0.8.9，站点编辑器用；Electron 用浏览器自带的 <input type="date">）。
// 可以直接打 2026-11-20（也认 2026/11/20、2026.11.20、20261120），打完按 Enter 或离开时检查；打错就恢复原来的并闪一下红边。
// 右边的小日历按钮：弹出一个月历（放在窗口最上层，点别处或 Esc 收起），点一天就填进去；"清除"留空。
// value = "YYYY-MM-DD" 或空；改了发 edited(新值)。
import QtQuick
import TPApp

Item {
    id: root
    property string value: ""
    property string placeholder: qsTr("年-月-日")
    property string minDate: ""          // 打开日历时没有值就从这一天所在的月开始（如离开日期从到达日期开始）
    property bool bad: false             // 外面标红（如结束早于开始）
    signal edited(string value)
    implicitHeight: Theme.px(32)
    implicitWidth: Theme.px(150)
    property bool flash: false

    function pad(n) { return (n < 10 ? "0" : "") + n }
    function parse(t) {
        t = String(t).trim()
        if (t === "") return ""
        let m = t.match(/^(\d{4})[-/.年](\d{1,2})[-/.月](\d{1,2})日?$/) || t.match(/^(\d{4})(\d{2})(\d{2})$/)
        if (!m) return null
        const y = +m[1], mo = +m[2], d = +m[3]
        const dt = new Date(y, mo - 1, d)
        if (dt.getFullYear() !== y || dt.getMonth() !== mo - 1 || dt.getDate() !== d) return null
        return y + "-" + pad(mo) + "-" + pad(d)
    }
    function commit() {
        const v = parse(input.text)
        if (v === null) { input.text = root.value; root.flash = true; flashTimer.restart(); return }
        input.text = v
        if (v !== root.value) root.edited(v)
    }
    onValueChanged: if (!input.activeFocus) input.text = value
    Timer { id: flashTimer; interval: 900; onTriggered: root.flash = false }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusControl
        color: "#ffffff"
        border.width: 1
        border.color: root.flash || root.bad ? Theme.danger : input.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.6) : Theme.lineStrong
        FocusRing { target: input }
    }
    TextInput {
        id: input
        x: Theme.px(10)
        width: parent.width - x - calBtn.width - Theme.px(4)
        anchors.verticalCenter: parent.verticalCenter
        text: root.value
        font.family: serifFamily; font.pixelSize: Theme.px(13.5)
        color: Theme.text
        selectByMouse: true
        clip: true
        onEditingFinished: root.commit()
        Keys.onEscapePressed: (e) => { text = root.value; focus = false; e.accepted = true }
        Text {
            visible: input.text === "" && !input.activeFocus
            anchors.verticalCenter: parent.verticalCenter
            text: root.placeholder
            font: input.font
            color: Theme.faint
        }
    }
    Item {
        id: calBtn
        anchors.right: parent.right
        width: Theme.px(30); height: parent.height
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("选日期")
        Canvas {   // 小日历图标
            anchors.centerIn: parent
            width: Theme.px(15); height: width
            onPaint: {
                const c = getContext("2d"), s = width / 24
                c.reset(); c.strokeStyle = calMa.containsMouse ? Theme.accent : Theme.muted; c.lineWidth = 2 * s; c.lineCap = "round"
                c.beginPath(); c.rect(3 * s, 5 * s, 18 * s, 16 * s); c.moveTo(3 * s, 10 * s); c.lineTo(21 * s, 10 * s)
                c.moveTo(8 * s, 3 * s); c.lineTo(8 * s, 7 * s); c.moveTo(16 * s, 3 * s); c.lineTo(16 * s, 7 * s); c.stroke()
            }
        }
        MouseArea { id: calMa; objectName: "dateCalBtn"; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openCal() }
    }

    // ---- 月历 ----
    property int viewY: 2026
    property int viewM: 0
    function openCal() {
        const host = root.Window.window ? root.Window.window.contentItem : null
        if (!host) return
        const base = root.value || root.minDate || ""
        const d = base ? new Date(+base.slice(0, 4), +base.slice(5, 7) - 1, 1) : new Date()
        viewY = d.getFullYear(); viewM = d.getMonth()
        shield.parent = host; pop.parent = host
        const p = root.mapToItem(host, 0, root.height + Theme.px(4))
        pop.x = Math.max(8, Math.min(p.x, host.width - pop.width - 8))
        pop.y = (p.y + pop.height > host.height - 8) ? Math.max(8, p.y - root.height - pop.height - Theme.px(8)) : p.y
        shield.visible = true; pop.visible = true
        pop.forceActiveFocus()
    }
    function closeCal() { pop.visible = false; shield.visible = false }
    function shiftMonth(n) { let m = viewM + n, y = viewY; while (m < 0) { m += 12; --y } while (m > 11) { m -= 12; ++y } viewM = m; viewY = y }
    readonly property var cells: {
        const first = new Date(viewY, viewM, 1)
        const lead = (first.getDay() - calendar.weekStart + 7) % 7   // 跟设置里的一周起始
        const n = new Date(viewY, viewM + 1, 0).getDate()
        const out = []
        for (let i = 0; i < lead; ++i) out.push(0)
        for (let d = 1; d <= n; ++d) out.push(d)
        return out
    }
    MouseArea { id: shield; visible: false; anchors.fill: parent; z: 900; onClicked: root.closeCal() }
    Rectangle {
        id: pop
        visible: false
        z: 901
        width: Theme.px(252)
        height: calCol.implicitHeight + Theme.px(20)
        radius: Theme.radiusSmall
        color: "#fbfcfd"
        border.color: Theme.line
        Keys.onEscapePressed: (e) => { root.closeCal(); e.accepted = true }
        MouseArea { anchors.fill: parent }   // 点在月历上不关
        Column {
            id: calCol
            x: Theme.px(10); y: Theme.px(10)
            width: parent.width - Theme.px(20)
            spacing: Theme.px(6)
            Item {
                width: parent.width; height: Theme.px(28)
                Text {
                    anchors.centerIn: parent
                    text: Fmt.en ? Qt.locale("en_US").standaloneMonthName(root.viewM) + " " + root.viewY : root.viewY + " 年 " + (root.viewM + 1) + " 月"
                    font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: Theme.text
                }
                Repeater {
                    model: [-1, 1]
                    delegate: Rectangle {
                        required property var modelData
                        x: modelData < 0 ? 0 : parent.width - width
                        width: Theme.px(28); height: width; radius: width / 2
                        color: nMa.containsMouse ? Theme.accentSoft : "transparent"
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData < 0 ? qsTr("上个月") : qsTr("下个月")
                        TpTip { text: modelData < 0 ? qsTr("上个月") : qsTr("下个月"); show: nMa.containsMouse }
                        Text { anchors.centerIn: parent; text: modelData < 0 ? "‹" : "›"; font.pixelSize: Theme.px(18); color: Theme.textSoft }
                        MouseArea { id: nMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.shiftMonth(modelData) }
                    }
                }
            }
            Grid {
                columns: 7
                width: parent.width
                Repeater {
                    model: [0, 1, 2, 3, 4, 5, 6].map(i => (Fmt.en ? ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"] : ["日", "一", "二", "三", "四", "五", "六"])[(i + calendar.weekStart) % 7])
                    delegate: Text {
                        required property var modelData
                        width: calCol.width / 7; horizontalAlignment: Text.AlignHCenter
                        text: modelData; font.family: serifFamily; font.pixelSize: Theme.px(11); color: Theme.muted
                    }
                }
                Repeater {
                    model: root.cells
                    delegate: Rectangle {
                        required property var modelData
                        readonly property string ds: modelData ? root.viewY + "-" + root.pad(root.viewM + 1) + "-" + root.pad(modelData) : ""
                        readonly property bool sel: ds !== "" && ds === root.value
                        objectName: ds ? "dateDay-" + ds : ""
                        width: calCol.width / 7; height: Theme.px(30)
                        radius: height / 2
                        color: sel ? Theme.accent : (dMa.containsMouse && modelData ? Theme.accentSoft : "transparent")
                        Text {
                            anchors.centerIn: parent
                            visible: parent.modelData > 0
                            text: parent.modelData
                            font.family: serifFamily; font.pixelSize: Theme.px(13)
                            color: parent.sel ? "#ffffff" : Theme.text
                        }
                        MouseArea {
                            id: dMa
                            anchors.fill: parent
                            enabled: parent.modelData > 0
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { root.closeCal(); input.text = parent.ds; if (parent.ds !== root.value) root.edited(parent.ds) }
                        }
                    }
                }
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.px(6)
                TpButton { compact: true; kind: "ghost"; text: qsTr("清除"); onClicked: { root.closeCal(); input.text = ""; if (root.value !== "") root.edited("") } }
                TpButton { compact: true; kind: "ghost"; text: qsTr("今天"); onClicked: { const d = new Date(); root.closeCal(); const s = d.getFullYear() + "-" + root.pad(d.getMonth() + 1) + "-" + root.pad(d.getDate()); input.text = s; if (s !== root.value) root.edited(s) } }
            }
        }
    }
}
