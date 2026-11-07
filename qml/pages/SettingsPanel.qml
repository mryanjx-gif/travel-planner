// 设置面板（右侧；PO 2026-11-02 选方案 A：分组卡片，一页往下排，和 Electron 一样）。
// 0.4.0 只有"通用"和"数据"两节；外观、联网服务、AI、关于等在各自的阶段补，最终视觉在阶段 ⑥。
// 0.5.0：数据里加"备份"（BackupSection，方案 B）和开发版的"测试改动"。
import QtQuick
import TPApp

GlassPanel {
    id: root
    tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.95 : 0.99)   // 设置要长时间读字
    property bool open: false
    property var history: []
    signal closeClicked()
    signal chooseFolderClicked()
    signal editTitleClicked()
    signal restoreClicked(var entry)
    signal pickBackupFolder(string then)
    signal importClicked()
    signal cleanupClicked()
    signal toastRequested(string text, bool error)

    radius: Theme.radiusLarge
    width: Math.min(Theme.px(430), parent ? parent.width - 28 : 430)
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    transform: Translate { x: root.open || !Theme.motion ? 0 : Theme.px(24) }
    Behavior on opacity { NumberAnimation { duration: Theme.durPanel; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.curvePanel } }

    function refresh() { history = app.historyList() }
    // 开发版：故意制造一个 QML 里的 JS 错误（0.5.1）
    function devThrow() { const nothing = null; return nothing.boom }
    onOpenChanged: if (open) { refresh(); photos.refreshCacheSize(); forceActiveFocus() }
    Connections { target: app; function onDataChanged() { if (root.open) root.refresh() } }
    Keys.onEscapePressed: closeClicked()
    MouseArea { anchors.fill: parent; onWheel: (w) => w.accepted = false }

    function dateText(iso) {
        const p = iso.split("-")
        if (p.length !== 3) return iso
        if (app.lang === "en") return new Date(+p[0], +p[1] - 1, +p[2]).toLocaleDateString(Qt.locale("en_US"), "MMM d")
        return (+p[1]) + " 月 " + (+p[2]) + " 日"
    }
    function sizeText(b) { return b >= 1048576 ? (b / 1048576).toFixed(1) + " MB" : Math.max(1, Math.round(b / 1024)) + " KB" }

    // 标题栏
    Item {
        id: head
        width: parent.width
        height: Theme.px(64)
        Text {
            x: Theme.px(24)
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("设置")
            font.family: serifFamily
            font.pixelSize: Theme.px(20)
            font.bold: true
            color: Theme.text
        }
        TpButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(16)
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.px(36)
            compact: true
            kind: "ghost"
            text: "×"
            Accessible.name: qsTr("关闭设置")
            onClicked: root.closeClicked()
        }
    }

    Flickable {
        id: flick
        anchors.top: head.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        contentHeight: body.implicitHeight + Theme.px(16)
        boundsBehavior: Flickable.StopAtBounds
        // 截图 ui-devtools（0.5.2）：滚到最底下的"开发版测试"
        Timer {
            running: shotPage === "ui-devtools"
            interval: 1500
            onTriggered: flick.contentY = Math.max(0, flick.contentHeight - flick.height)
        }

        Column {
            id: body
            x: Theme.px(16)
            width: flick.width - 2 * x
            spacing: Theme.px(12)

            // ---------- 通用 ----------
            SettingsCard {
                width: parent.width
                title: qsTr("通用")

                SettingsRow {
                    width: parent.width
                    label: qsTr("语言")
                    Segmented {
                        options: [{ key: "zh", text: "中文" }, { key: "en", text: "English" }]
                        current: app.lang
                        onPicked: (k) => app.lang = k
                    }
                }
                Column {
                    width: parent.width
                    spacing: Theme.px(7)
                    Text {
                        text: qsTr("关闭按钮")
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
                    }
                    Segmented {
                        width: parent.width
                        fill: true
                        options: [{ key: "quit", text: qsTr("直接退出") }, { key: "tray", text: qsTr("收到托盘") }, { key: "taskbar", text: qsTr("最小化") }]
                        current: app.closeAction
                        onPicked: (k) => app.closeAction = k
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("Ctrl+Q 和托盘里的“退出”总是真的退出。")
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("字号")
                    Segmented {
                        options: [{ key: "s", text: qsTr("小") }, { key: "m", text: qsTr("中") }, { key: "l", text: qsTr("大") }, { key: "xl", text: qsTr("超大") }]
                        current: app.fontSize
                        onPicked: (k) => app.fontSize = k
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("一周从哪天开始")
                    sub: qsTr("日历和选日期的小日历")
                    Segmented {
                        objectName: "weekStartSeg"
                        options: [{ key: "1", text: qsTr("周一") }, { key: "0", text: qsTr("周日") }]
                        current: String(calendar.weekStart)
                        onPicked: (k) => calendar.weekStart = +k
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("窗口标题")
                    sub: app.windowTitleExtra === "" ? qsTr("旅行计划") + " v" + app.version + qsTr("（没有自定义文字）")
                                                     : qsTr("旅行计划") + " v" + app.version + "  " + app.windowTitleExtra
                    TpButton { text: qsTr("修改…"); compact: true; onClicked: root.editTitleClicked() }
                }
            }

            // ---------- 地图（0.9.3：先只有星空动画；打开时的底图 / 形状等在阶段 ⑥）----------
            SettingsCard {
                objectName: "settingsMap"
                width: parent.width
                title: qsTr("地图")

                SettingsRow {
                    width: parent.width
                    label: qsTr("星空动画")
                    sub: qsTr("地球后面的星星闪烁、偶尔有流星。电脑比较慢时可以关掉")
                    Segmented {
                        objectName: "spaceAnimSeg"
                        options: [{ key: "off", text: qsTr("关") }, { key: "on", text: qsTr("开") }]
                        current: app.spaceAnim ? "on" : "off"
                        onPicked: (k) => app.spaceAnim = k === "on"
                    }
                }
            }

            // ---------- 数据 ----------
            SettingsCard {
                width: parent.width
                title: qsTr("数据")

                Column {
                    width: parent.width
                    spacing: Theme.px(6)
                    Text {
                        text: qsTr("数据文件夹")
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
                    }
                    Text {
                        width: parent.width
                        text: app.dataDir === "" ? qsTr("还没有选") : app.dataDir
                        wrapMode: Text.WrapAnywhere
                        font.family: "Consolas"
                        font.pixelSize: Theme.px(12.5)
                        color: Theme.textSoft
                    }
                    Text {
                        width: parent.width
                        visible: text !== ""
                        wrapMode: Text.Wrap
                        text: app.dataDirTemporary ? qsTr("命令行临时指定，不会记进设置")
                            : app.dataDirIsDefault ? qsTr("软件文件夹里的默认位置；卸载、重装都不会删除")
                            : ""
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                    Row {
                        spacing: Theme.px(6)
                        TpButton { text: qsTr("打开文件夹"); compact: true; enabled: app.dataDir !== ""; onClicked: app.openDataFolder() }
                        TpButton { text: qsTr("选择…"); compact: true; onClicked: root.chooseFolderClicked() }
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("从 Electron 版导入")
                    sub: app.electronDataDir !== "" ? app.electronDataDir
                                                    : qsTr("没有找到 Electron 版；导入时可以手动选它的数据文件夹。")
                    TpButton { text: qsTr("导入…"); compact: true; enabled: app.dataDir !== "" && !app.readOnly && !app.importRunning; onClicked: root.importClicked() }
                }
                BackupSection {
                    width: parent.width
                    expanded: shotPage === "ui-backup" || shotPage === "ui-backup-running"
                    onPickFolder: (then) => root.pickBackupFolder(then)
                }
                // 0.7.0：照片（A-10 清理、A-06 缩略图缓存）
                SettingsRow {
                    width: parent.width
                    label: qsTr("清理照片")
                    sub: qsTr("照片文件夹里没被任何照片用到的文件，移到回收站。")
                    TpButton { text: qsTr("检查…"); compact: true; enabled: app.dataDir !== "" && !app.readOnly && !photos.trashRunning; onClicked: root.cleanupClicked() }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("缩略图缓存")
                    sub: (photos.cacheBytes < 0 ? qsTr("正在统计…") : photos.cacheBytes === 0 ? "0 KB" : root.sizeText(photos.cacheBytes)) + " · " + qsTr("超过 1 GB 时自动删掉最旧的。")
                    TpButton { text: qsTr("清空"); compact: true; enabled: photos.cacheBytes > 0; onClicked: { photos.clearThumbCache(); root.toastRequested(qsTr("缩略图缓存已清空，用到时会重新生成。"), false) } }
                }
                Column {
                    width: parent.width
                    spacing: Theme.px(6)
                    Text {
                        text: qsTr("历史版本")
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("每天第一次保存前自动存一份，保留最近 14 天。改坏了可以从这里恢复。")
                        font.family: serifFamily; font.pixelSize: Theme.px(12); lineHeight: 1.2; color: Theme.muted
                    }
                    Text {
                        visible: root.history.length === 0
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("还没有历史版本。")
                        font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                    }
                    Rectangle {
                        visible: root.history.length > 0
                        width: parent.width
                        height: list.implicitHeight + 2
                        radius: Theme.radiusSmall
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.line
                        clip: true
                        Column {
                            id: list
                            x: 1; y: 1
                            width: parent.width - 2
                            Repeater {
                                model: root.history
                                delegate: Rectangle {
                                    id: hrow
                                    required property var modelData
                                    required property int index
                                    width: list.width
                                    height: Theme.px(42)
                                    color: index % 2 === 0 ? Qt.rgba(1, 1, 1, 0.6) : "transparent"
                                    Rectangle { visible: hrow.index > 0; width: parent.width; height: 1; color: "#e4eaef" }
                                    Text {
                                        x: Theme.px(12)
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: root.dateText(hrow.modelData.date)
                                        font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text
                                    }
                                    Text {
                                        anchors.right: restoreBtn.left
                                        anchors.rightMargin: Theme.px(10)
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("%1 个计划").arg(hrow.modelData.plans) + " · " + root.sizeText(hrow.modelData.size)
                                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                                    }
                                    TpButton {
                                        id: restoreBtn
                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.px(8)
                                        anchors.verticalCenter: parent.verticalCenter
                                        compact: true
                                        text: qsTr("恢复")
                                        onClicked: root.restoreClicked(hrow.modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // ---------- 开发版：测试改动（打包版没有） ----------
            SettingsCard {
                visible: app.devTools
                width: parent.width
                title: qsTr("开发版测试")
                SettingsRow {
                    width: parent.width
                    label: qsTr("测试改动")
                    sub: app.devTestAllowed
                         ? qsTr("给第一个计划的名字加一个 ★（满三个时去掉），停手 0.8 秒后自动保存；Ctrl+Z 撤销、Ctrl+Y 重做。")
                         : qsTr("只在文件夹名里带“测试”或 test 的数据副本上能用。")
                    TpButton {
                        compact: true
                        text: qsTr("改一下")
                        enabled: app.devTestAllowed
                        onClicked: {
                            const r = app.devTestChange()
                            if (!r.ok) root.toastRequested(r.error, true)
                        }
                    }
                }
                // 0.5.1：故意制造错误，实测出错小框 / 崩溃 / 卡住
                SettingsRow {
                    width: parent.width
                    label: qsTr("制造错误")
                    sub: qsTr("JS 错误、C++ 异常弹右下角小框；两种崩溃都会直接退出，下次打开时提示；卡住 12 秒，10 秒后弹系统对话框。")
                    Grid {
                        columns: 2
                        spacing: Theme.px(6)
                        TpButton { compact: true; text: qsTr("JS 错误"); onClicked: root.devThrow() }
                        TpButton { compact: true; text: qsTr("C++ 异常"); onClicked: app.devTrigger("m") }
                        TpButton { compact: true; text: qsTr("卡住"); onClicked: app.devTrigger("hang") }
                        TpButton { compact: true; text: qsTr("崩溃（主线程）"); kind: "dangerOutline"; onClicked: app.devTrigger("crash") }
                        TpButton { compact: true; text: qsTr("崩溃（后台线程）"); kind: "dangerOutline"; onClicked: app.devTrigger("crash-thread") }
                    }
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("外观、联网服务、AI 助手、关于等设置会在之后的版本加上。")
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.faint
            }
        }
    }
}
