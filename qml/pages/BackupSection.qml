// 设置 → 数据 → 备份（0.5.0，PO 2026-11-03 选方案 B：一行状态 + 展开）。
// 收起时：状态（几天前备份过 · 自动备份几天一次）、备份文件夹、"设置"和"现在备份"；备份时显示进度和"取消"。
// 展开后：选备份文件夹、自动备份（每 N 天，1–90）。规则照搬 Electron（prefs.js、pages-more.js、main.js data:backup）。
import QtQuick
import TPApp

Column {
    id: root
    property bool expanded: false
    signal pickFolder(string then)     // then：""（只选文件夹）/ "now"（选完马上备份）/ "auto"（选完打开自动备份）

    // 截图模式下演示"正在备份"（只给 --screenshot ui-backup-running 用）
    readonly property bool demo: shotPage === "ui-backup-running"
    readonly property bool running: app.backupRunning || demo
    readonly property int done: demo ? 312 : app.backupDone
    readonly property int total: demo ? 840 : app.backupTotal
    readonly property bool hasDir: app.backupDir !== ""
    // 离上次备份几天（按本地日期算：今天 0、昨天 1）
    readonly property int daysAgo: {
        if (app.lastBackup <= 0) return -1
        const a = new Date(app.lastBackup), b = new Date(clock.now)
        const da = new Date(a.getFullYear(), a.getMonth(), a.getDate()), db = new Date(b.getFullYear(), b.getMonth(), b.getDate())
        return Math.max(0, Math.round((db - da) / 86400000))
    }
    readonly property bool overdue: hasDir && (daysAgo < 0 || (app.autoBackup ? daysAgo > app.backupEveryDays : daysAgo > 30))

    function statusText() {
        if (running) return total > 0 ? qsTr("正在备份 %1 / %2 个文件…").arg(done).arg(total) : qsTr("正在准备备份…")
        if (!hasDir) return qsTr("还没选备份文件夹")
        const when = daysAgo < 0 ? qsTr("还没备份过") : daysAgo === 0 ? qsTr("今天备份过") : daysAgo === 1 ? qsTr("昨天备份过")
                                 : qsTr("%1 天前备份过").arg(daysAgo)
        return when + " · " + (app.autoBackup ? qsTr("每 %1 天自动备份一次").arg(app.backupEveryDays) : qsTr("自动备份没开"))
    }
    function lastText() {
        if (app.lastBackup <= 0) return ""
        const d = new Date(app.lastBackup)
        const p2 = (n) => (n < 10 ? "0" : "") + n
        return qsTr("上次：%1").arg(d.getFullYear() + "/" + (d.getMonth() + 1) + "/" + d.getDate() + " " + p2(d.getHours()) + ":" + p2(d.getMinutes()))
    }

    QtObject { id: clock; property double now: Date.now() }
    Timer { interval: 60000; running: true; repeat: true; onTriggered: clock.now = Date.now() }
    Connections { target: app; function onBackupChanged() { clock.now = Date.now() } }

    spacing: Theme.px(8)

    // ---- 收起时的一行 ----
    Item {
        width: parent.width
        height: Math.max(info.implicitHeight, buttons.height)
        Column {
            id: info
            width: parent.width - buttons.width - Theme.px(10)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(3)
            Text {
                text: qsTr("备份")
                font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
            }
            Row {
                width: parent.width
                spacing: Theme.px(6)
                Item {   // 小圆点对齐第一行（英文时状态可能折成两三行）
                    id: dot
                    width: Theme.px(8)
                    height: statusLabel.font.pixelSize * 1.35
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width; height: width; radius: width / 2
                        color: root.running ? Theme.accent : (!root.hasDir || root.overdue) ? Theme.warn : Theme.ok
                    }
                }
                Text {
                    id: statusLabel
                    width: parent.width - dot.width - parent.spacing
                    text: root.statusText()
                    wrapMode: Text.Wrap
                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                }
            }
            Text {
                width: parent.width
                visible: root.hasDir
                text: app.backupDir
                elide: Text.ElideMiddle
                font.family: "Consolas"; font.pixelSize: Theme.px(12); color: Theme.muted
            }
        }
        Row {
            id: buttons
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(6)
            TpButton {
                compact: true
                text: root.expanded ? qsTr("收起") : qsTr("设置")
                Accessible.name: qsTr("备份设置")
                onClicked: root.expanded = !root.expanded
            }
            TpButton {
                compact: true
                kind: root.running ? "secondary" : "primary"
                text: root.running ? qsTr("取消") : qsTr("现在备份")
                onClicked: {
                    if (root.running) app.cancelBackup()
                    else if (!root.hasDir) root.pickFolder("now")
                    else app.backupNow()
                }
            }
        }
    }

    // ---- 进度条 ----
    Rectangle {
        visible: root.running
        width: parent.width
        height: Theme.px(4)
        radius: height / 2
        color: Theme.line
        Rectangle {
            width: parent.width * (root.total > 0 ? Math.min(1, root.done / root.total) : 0)
            height: parent.height
            radius: parent.radius
            color: Theme.accent
            Behavior on width { enabled: Theme.motion; NumberAnimation { duration: 120 } }
        }
    }

    // ---- 展开 ----
    Column {
        visible: root.expanded
        width: parent.width
        spacing: Theme.px(12)
        topPadding: Theme.px(4)

        Rectangle { width: parent.width; height: 1; color: Theme.line }

        SettingsRow {
            width: parent.width
            label: qsTr("备份文件夹")
            sub: root.hasDir ? app.backupDir : qsTr("还没选备份文件夹（建议选另一块硬盘或网盘同步文件夹）")
            TpButton { compact: true; text: qsTr("选文件夹"); onClicked: root.pickFolder("") }
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("每次在里面新建“旅行计划备份-日期-时间”，复制数据文件和全部照片。") + (root.lastText() !== "" ? " " + root.lastText() : "")
            font.family: serifFamily; font.pixelSize: Theme.px(12); lineHeight: 1.2; color: Theme.muted
        }
        Column {
            width: parent.width
            spacing: Theme.px(7)
            Text {
                text: qsTr("自动备份")
                font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
            }
            Row {
                spacing: Theme.px(8)
                Segmented {
                    anchors.verticalCenter: parent.verticalCenter
                    options: [{ key: "off", text: qsTr("关") }, { key: "on", text: qsTr("开") }]
                    current: app.autoBackup ? "on" : "off"
                    onPicked: (k) => {
                        if (k === "on" && !root.hasDir) root.pickFolder("auto")   // 还没选文件夹：先选（Electron 一样）
                        else app.setAutoBackup(k === "on")
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("每")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text
                }
                TpButton {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true; width: Theme.px(32); text: "−"
                    Accessible.name: qsTr("少一天")
                    enabled: app.backupEveryDays > 1
                    onClicked: app.setBackupEveryDays(app.backupEveryDays - 1)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(24)
                    horizontalAlignment: Text.AlignHCenter
                    text: app.backupEveryDays
                    font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: Theme.text
                }
                TpButton {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true; width: Theme.px(32); text: "+"
                    Accessible.name: qsTr("多一天")
                    enabled: app.backupEveryDays < 90
                    onClicked: app.setBackupEveryDays(app.backupEveryDays + 1)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("天一次")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("打开软件时，如果离上次备份超过设定的天数，就自动备份一次。")
                font.family: serifFamily; font.pixelSize: Theme.px(12); lineHeight: 1.2; color: Theme.muted
            }
        }
    }
}
