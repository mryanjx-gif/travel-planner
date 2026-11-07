// 设置面板（右侧）。0.12.3 改版（PO 选 A，代替 Electron 的分组折叠 T-01）：宽约 760，左边侧边栏，快捷键和关于固定在底部；窄窗口改成顶部横向标签。
// 货币、证件、联网、关于的内容在 0.12.4、0.12.5；AI 分组是占位，阶段 ⑦ 接功能。
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
    Behavior on opacity { TpAnim { shown: root.open } }

    // 现在看哪一组。截图页直接打开对应分组
    property string section: ({ "ui-backup": "data", "ui-backup-running": "data", "ui-devtools": "data", "ui-settings-photos": "data", "ui-settings-data": "data",
                                "calendar-settings": "cal", "ui-settings-me": "me", "ui-settings-ai": "ai", "ui-settings-keys": "keys" })[shotPage] || "look"
    readonly property bool narrow: width < Theme.px(640)
    readonly property var mainSections: [
        { key: "look", text: qsTr("外观") }, { key: "me", text: qsTr("我") }, { key: "map", text: qsTr("地图") }, { key: "cal", text: qsTr("日历") }, { key: "win", text: qsTr("窗口") },
        { key: "money", text: qsTr("货币") }, { key: "docs", text: qsTr("证件") }, { key: "ai", text: qsTr("AI") }, { key: "net", text: qsTr("联网") }, { key: "data", text: qsTr("数据") }
    ]
    readonly property var footSections: [{ key: "keys", text: qsTr("快捷键") }, { key: "about", text: qsTr("关于") }]
    readonly property string sectionName: { const x = mainSections.concat(footSections).find(i => i.key === section); return x ? x.text : "" }
    onSectionChanged: { flick.contentY = 0; if (section === "me") mapc.needUnlockCount() }

    function refresh() { history = app.historyList() }
    // 开发版：故意制造一个 QML 里的 JS 错误（0.5.1）
    function devThrow() { const nothing = null; return nothing.boom }
    onOpenChanged: if (open) { refresh(); photos.refreshCacheSize(); forceActiveFocus(); if (section === "me") mapc.needUnlockCount() }
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
            objectName: "settingsClose"
            kind: "ghost"
            text: "×"
            Accessible.name: qsTr("关闭设置")
            onClicked: root.closeClicked()
        }
    }

    // 侧边栏或顶部标签的一项
    component NavItem: Rectangle {
        id: navRoot
        property string label: ""
        property bool on: false
        signal picked()
        implicitWidth: navText.implicitWidth + Theme.px(24)
        implicitHeight: Theme.px(34)
        radius: Theme.radiusControl
        color: on ? Theme.accentSoft : navMa.containsMouse ? Qt.rgba(45 / 255, 95 / 255, 139 / 255, 0.06) : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.motion ? 120 : 0 } }
        activeFocusOnTab: true
        Accessible.role: Accessible.PageTab
        Accessible.name: label
        Keys.onSpacePressed: picked()
        Keys.onReturnPressed: picked()
        FocusRing { target: navRoot }
        Text {
            id: navText
            x: Theme.px(12)
            anchors.verticalCenter: parent.verticalCenter
            text: navRoot.label
            font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: navRoot.on
            color: navRoot.on ? Theme.accentInk : Theme.textSoft
        }
        MouseArea { id: navMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: navRoot.picked() }
    }

    // 宽：左边侧边栏，快捷键和关于固定在底部
    Item {
        id: side
        visible: !root.narrow
        anchors.top: head.bottom
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.px(14)
        x: Theme.px(14)
        width: Theme.px(112)
        Column {
            width: parent.width
            spacing: Theme.px(2)
            Repeater {
                model: root.mainSections
                delegate: NavItem {
                    required property var modelData
                    objectName: "setNav-" + modelData.key
                    width: side.width
                    label: modelData.text
                    on: root.section === modelData.key
                    onPicked: root.section = modelData.key
                }
            }
        }
        Column {
            anchors.bottom: parent.bottom
            width: parent.width
            spacing: Theme.px(2)
            Repeater {
                model: root.footSections
                delegate: NavItem {
                    required property var modelData
                    objectName: "setNav-" + modelData.key
                    width: side.width
                    label: modelData.text
                    on: root.section === modelData.key
                    onPicked: root.section = modelData.key
                }
            }
        }
    }
    // 窄：顶部横向标签，放不下时左右拖
    Flickable {
        id: tabs
        visible: root.narrow
        anchors.top: head.bottom
        x: Theme.px(12)
        width: parent.width - 2 * x
        height: visible ? Theme.px(42) : 0
        contentWidth: tabRow.width
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Row {
            id: tabRow
            spacing: Theme.px(2)
            Repeater {
                model: root.mainSections.concat(root.footSections)
                delegate: NavItem {
                    required property var modelData
                    objectName: "setTab-" + modelData.key
                    label: modelData.text
                    on: root.section === modelData.key
                    onPicked: root.section = modelData.key
                }
            }
        }
    }

    Flickable {
        id: flick
        anchors.top: root.narrow ? tabs.bottom : head.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.left: root.narrow ? parent.left : side.right
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

            // ---------- 外观（T-02）----------
            SettingsCard {
                objectName: "setCard-look"
                visible: root.section === "look"
                width: parent.width
                title: qsTr("外观")

                SettingsRow {
                    width: parent.width
                    label: qsTr("语言")
                    Segmented {
                        options: [{ key: "zh", text: "中文" }, { key: "en", text: "English" }]
                        current: app.lang
                        onPicked: (k) => app.lang = k
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("字体")
                    sub: qsTr("全部界面统一用这一种。现在显示的是：%1").arg(serifFamily)
                    TpSelect {
                        objectName: "uiFontSel"
                        width: Theme.px(150)
                        options: [{ key: "serif", text: qsTr("思源宋体") }, { key: "yahei", text: qsTr("微软雅黑") }, { key: "dengxian", text: qsTr("等线") }, { key: "kaiti", text: qsTr("楷体") }]
                        current: app.uiFont
                        onPicked: (k) => app.uiFont = k
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
                Column {
                    width: parent.width
                    spacing: Theme.px(7)
                    Text {
                        text: qsTr("动效")
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
                    }
                    Segmented {
                        width: parent.width
                        fill: true
                        options: [{ key: "auto", text: qsTr("跟随系统") }, { key: "on", text: qsTr("开") }, { key: "off", text: qsTr("减少动效") }]
                        current: app.motion
                        onPicked: (k) => app.motion = k
                    }
                }
                Column {
                    width: parent.width
                    spacing: Theme.px(7)
                    Text {
                        text: qsTr("透明效果")
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
                    }
                    Segmented {
                        options: [{ key: "on", text: qsTr("开") }, { key: "off", text: qsTr("关（全部实色）") }]
                        current: app.transparency
                        onPicked: (k) => app.transparency = k
                    }
                }
                Column {
                    width: parent.width
                    spacing: Theme.px(7)
                    Text {
                        text: qsTr("玻璃材质")
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
                    }
                    Segmented {
                        options: [{ key: "liquid", text: qsTr("液态") }, { key: "css", text: qsTr("标准") }]
                        current: app.glassMode
                        onPicked: (k) => app.glassMode = k
                    }
                    Text {
                        objectName: "glassStatus"
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: Theme.glassReason === "" ? qsTr("当前：液态")
                            : Theme.glassReason === "pref" ? qsTr("当前：标准 · 设置里选了“标准”")
                            : Theme.glassReason === "motion" ? qsTr("当前：标准 · 动效设成了减少动效（或系统关了动画）")
                            : Theme.glassReason === "slow" ? qsTr("当前：标准 · 显卡带不动，已自动换成标准玻璃，改一次这里的设置或下次启动时重试")
                            : qsTr("当前：实色 · 透明效果关了")
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                }
                Text {
                    objectName: "backendStatus"
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("图形后端：") + backendApi + (backendReason !== "" ? " · " + qsTr("没用 Vulkan 的原因：") + win.trReason(backendReason) : "")
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                }
            }

            // ---------- 我（T-03）----------
            SettingsCard {
                objectName: "setCard-me"
                visible: root.section === "me"
                width: parent.width
                title: qsTr("我")

                SettingsRow {
                    width: parent.width
                    label: qsTr("名字")
                    sub: qsTr("右上角头像显示第一个字；分账里的“我”用这个名字。空着也可以")
                    TpField {
                        objectName: "greetField"
                        width: Theme.px(200)
                        maxLength: 20
                        value: app.greetName
                        onEdited: (t) => app.greetName = t
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("所在地国家")
                    sub: qsTr("地图上“我的位置”拿不到系统定位、又不在旅行中时，先显示这个国家")
                    TpSelect {
                        id: countrySel
                        objectName: "homeCountrySel"
                        width: Theme.px(200)
                        current: app.homeCountry || locator.homeCountry()
                        options: mapc.unlockReady ? mapc.countryOptions() : [{ key: current, text: current }]
                        onPicked: (k) => app.homeCountry = k
                    }
                }
            }

            // ---------- 地图（T-04）----------
            SettingsCard {
                objectName: "settingsMap"
                visible: root.section === "map"
                width: parent.width
                title: qsTr("地图")

                SettingsRow {
                    width: parent.width
                    label: qsTr("打开时的底图")
                    sub: qsTr("“上次用的”= 记住上次在地图上选的")
                    TpSelect {
                        objectName: "startBaseSel"
                        width: Theme.px(150)
                        options: [{ key: "", text: qsTr("上次用的") }, { key: "卫星图", text: qsTr("卫星图") }, { key: "街道图", text: qsTr("街道图") }, { key: "已解锁", text: qsTr("已解锁") }]
                        current: app.startBase
                        onPicked: (k) => app.startBase = k
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("打开时的形状")
                    TpSelect {
                        objectName: "startProjSel"
                        width: Theme.px(150)
                        options: [{ key: "", text: qsTr("上次用的") }, { key: "globe", text: qsTr("地球") }, { key: "mercator", text: qsTr("平面") }]
                        current: app.startProj
                        onPicked: (k) => app.startProj = k
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("这两项下次打开时生效。现在就想换，用地图右上角的图层按钮。")
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                }
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

            // ---------- 日历（T-05）----------
            SettingsCard {
                objectName: "setCard-cal"
                visible: root.section === "cal"
                width: parent.width
                title: qsTr("日历")

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
            }

            // ---------- 窗口（T-06）----------
            SettingsCard {
                objectName: "setCard-win"
                visible: root.section === "win"
                width: parent.width
                title: qsTr("窗口")

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
                    label: qsTr("窗口标题")
                    sub: app.windowTitleExtra === "" ? qsTr("旅行计划") + " v" + app.version + qsTr("（没有自定义文字）")
                                                     : qsTr("旅行计划") + " v" + app.version + "  " + app.windowTitleExtra
                    TpButton { text: qsTr("修改…"); compact: true; onClicked: root.editTitleClicked() }
                }
            }

            // ---------- 货币、证件、联网：内容在 0.12.4、0.12.5 ----------
            SettingsCard {
                objectName: "setCard-later"
                visible: ["money", "docs", "net"].indexOf(root.section) >= 0
                width: parent.width
                title: root.sectionName
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("这一组下一版做。")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                }
            }

            // ---------- AI（占位，阶段 ⑦ 接功能）：显示但不能改，点了提示还在做 ----------
            SettingsCard {
                objectName: "setCard-ai"
                visible: root.section === "ai"
                width: parent.width
                title: qsTr("AI 助手")

                SettingsRow {
                    width: parent.width
                    label: qsTr("API 密钥")
                    sub: qsTr("还没有设置")
                    TpButton { objectName: "aiKey"; text: qsTr("设置…"); compact: true; onClicked: win.notReady() }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("默认模型")
                    TpSelect {
                        objectName: "aiModel"
                        width: Theme.px(180)
                        options: [{ key: "sonnet", text: "Claude Sonnet 5.5" }]
                        current: "sonnet"
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.notReady() }
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("每月上限")
                    TpSelect {
                        objectName: "aiCap"
                        width: Theme.px(180)
                        options: [{ key: "5", text: "$5" }]
                        current: "5"
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.notReady() }
                    }
                }
            }

            // ---------- 快捷键（T-12，只读）----------
            SettingsCard {
                objectName: "setCard-keys"
                visible: root.section === "keys"
                width: parent.width
                title: qsTr("快捷键")

                Repeater {
                    model: [
                        { t: qsTr("搜索或跳转（命令面板）"), k: "Ctrl + K" },
                        { t: qsTr("打开 / 关闭我的旅程"), k: "Ctrl + B" },
                        { t: qsTr("切换计划、想去、足迹、照片"), k: "Ctrl + 1–4" },
                        { t: qsTr("撤销 / 重做"), k: "Ctrl + Z / Y" },
                        { t: qsTr("回到地图"), k: "Esc" },
                        { t: qsTr("后退 / 前进"), k: "Alt + ← / →" },
                        { t: qsTr("切换路线站点"), k: "← / →" },
                        { t: qsTr("放大 / 缩小 / 恢复字号"), k: "Ctrl + + / − / 0" },
                        { t: qsTr("全屏"), k: "F11" },
                        { t: qsTr("打开日志文件夹"), k: "F12" },
                        { t: qsTr("退出"), k: "Ctrl + Q" }
                    ]
                    delegate: SettingsRow {
                        id: keyRow
                        required property var modelData
                        width: parent ? parent.width : 0
                        label: modelData.t
                        Text { text: keyRow.modelData.k; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft }
                    }
                }
            }

            // ---------- 关于：内容在 0.12.5 ----------
            SettingsCard {
                objectName: "setCard-about"
                visible: root.section === "about"
                width: parent.width
                title: qsTr("关于")
                Text {
                    width: parent.width
                    text: qsTr("旅行计划") + " v" + app.version
                    font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.text
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("更新日志、数据来源和授权下一版做。")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                }
            }

            // ---------- 数据 ----------
            SettingsCard {
                objectName: "setCard-data"
                visible: root.section === "data"
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
                visible: app.devTools && root.section === "data"
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
        }
    }
}
