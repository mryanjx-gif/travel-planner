// 设置面板（右侧）。0.12.3 改版（PO 选 A，代替 Electron 的分组折叠 T-01）：宽约 760，左边侧边栏，快捷键和关于固定在底部；窄窗口改成顶部横向标签。
// 0.12.4：货币（T-07）、证件和紧急信息（T-08）。0.12.5：联网（T-09）、缓存用量、关于（T-13，AboutSection）、更新日志。AI 分组是占位，阶段 ⑦ 接功能。
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
    signal docRequested(var row)            // 0.12.4 加 / 改证件（row 空 = 新加）
    signal emergencyRequested()
    signal toastRequested(string text, bool error)

    radius: Theme.radiusLarge
    width: Math.min(Theme.px(430), parent ? parent.width - 28 : 430)
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    transform: Translate { x: root.open || !Theme.motion ? 0 : Theme.px(24) }
    Behavior on opacity { TpAnim { shown: root.open } }

    // 现在看哪一组。截图页直接打开对应分组
    property string section: ({ "ui-backup": "data", "ui-backup-running": "data", "ui-devtools": "data", "ui-settings-photos": "data", "ui-settings-data": "data",
                                "calendar-settings": "cal", "ui-settings-me": "me", "ui-settings-ai": "ai", "ui-settings-keys": "keys",
                                "ui-settings-money": "money", "ui-settings-docs": "docs", "ui-settings-doc-form": "docs",
                                "ui-settings-net": "net", "ui-settings-net-off": "net", "ui-settings-about": "about", "ui-settings-about-end": "about", "ui-settings-log": "log" })[shotPage] || "look"
    readonly property bool narrow: width < Theme.px(640)
    readonly property var mainSections: [
        { key: "look", text: qsTr("外观") }, { key: "me", text: qsTr("我") }, { key: "map", text: qsTr("地图") }, { key: "cal", text: qsTr("日历") }, { key: "win", text: qsTr("窗口") },
        { key: "money", text: qsTr("货币") }, { key: "docs", text: qsTr("证件") }, { key: "ai", text: qsTr("AI") }, { key: "net", text: qsTr("联网") }, { key: "data", text: qsTr("数据") }
    ]
    readonly property var footSections: [{ key: "keys", text: qsTr("快捷键") }, { key: "about", text: qsTr("关于") }]
    // 联网服务（Electron prefs.js NET_SERVICES，键名相同）：开关和关于都用这张表
    readonly property var netServices: [
        { key: "tiles", name: qsTr("地图瓦片"), use: qsTr("地图和地球的卫星图、街道图。关掉后只用“已解锁”底图，存下来的瓦片地球上照常显示。"), who: qsTr("Esri（Esri 及其数据提供方）") },
        { key: "geocode", name: qsTr("地名搜索"), use: qsTr("编辑器里搜地名、点地图取地名。关掉后可以在地图上点选或手动输入坐标。"), who: qsTr("Nominatim · © OpenStreetMap 贡献者（ODbL）") },
        { key: "routing", name: qsTr("道路路线"), use: qsTr("自驾、徒步段按道路画线。关掉后已经算好的路线还在，没算的画直线。"), who: qsTr("OSRM（FOSSGIS 服务器 routing.openstreetmap.de）· © OpenStreetMap 贡献者") },
        { key: "rates", name: qsTr("汇率"), use: qsTr("消费折算成主货币，一天最多更新一次。关掉后继续用上次的。"), who: qsTr("Frankfurter（欧洲央行数据）；失败或缺某种货币时用 open.er-api.com（<a href=\"https://www.exchangerate-api.com\">Rates By Exchange Rate API</a>）") },
        { key: "weather", name: qsTr("天气"), use: qsTr("16 天内出发或正在旅行的计划，显示各站点的预报。"), who: qsTr("Open-Meteo（CC BY 4.0）") },
        { key: "climate", name: qsTr("气候"), use: qsTr("各站点近 10 年同一个月的平均气温和降水。"), who: qsTr("Open-Meteo 历史天气（CC BY 4.0；ERA5 · Copernicus 气候变化服务）") },
        { key: "elev", name: qsTr("海拔和时区"), use: qsTr("自动补站点的海拔（高反提示用）和时区（日出日落时间用）。"), who: qsTr("Open-Meteo（CC BY 4.0；海拔：Copernicus DEM GLO-90）") },
        { key: "ai", name: qsTr("AI 助手"), use: qsTr("右下角的 AI 助手，用你自己的 API 钥匙；你问的话会发给你在设置 → AI 里选的那家。"), who: qsTr("你选的那家：Claude（Anthropic）、DeepSeek、通义千问（阿里云百炼）、Kimi（月之暗面）、豆包（火山方舟）或自定义") }
    ]
    onSectionChanged: { flick.contentY = 0; if (section === "me") mapc.needUnlockCount(); refreshSection() }
    Connections { target: netc; function onTileCacheCleared(ok) { root.toastRequested(ok ? qsTr("地图瓦片缓存已移进回收站，用到时会重新下载。") : qsTr("没能移进回收站，可以稍后再试。"), !ok) } }
    Connections { target: photos; function onThumbCacheCleared(ok) { root.toastRequested(ok ? qsTr("缩略图缓存已移进回收站，用到时会重新生成。") : qsTr("没能移进回收站，可以稍后再试。"), !ok) } }
    // 从别处跳过来（此刻卡的"看证件"、计划页的"管理证件"）：已经在这一组时也回到开头
    function showSection(key) { section = key; flick.contentY = 0 }

    // ---- 货币、证件的内容（0.12.4）----
    property var docRows: []
    property var emergency: ({})
    function refreshDocs() { docRows = planPage.documents(); emergency = planPage.emergencyInfo() }
    function refreshSection() {
        if (section === "money") rates.ensure()
        if (section === "docs") refreshDocs()
        if (section === "about") aboutSection.refresh()
    }
    function curName(c) {
        return ({ CNY: qsTr("人民币"), AUD: qsTr("澳元"), USD: qsTr("美元"), EUR: qsTr("欧元"), JPY: qsTr("日元"), HKD: qsTr("港币"), GBP: qsTr("英镑"), NZD: qsTr("新西兰元"), KRW: qsTr("韩元"),
                  SGD: qsTr("新加坡元"), THB: qsTr("泰铢"), MYR: qsTr("马来西亚林吉特"), IDR: qsTr("印尼盾"), VND: qsTr("越南盾"), CHF: qsTr("瑞士法郎"), TWD: qsTr("新台币"), RUB: qsTr("卢布"), TRY: qsTr("土耳其里拉"),
                  AED: qsTr("阿联酋迪拉姆"), EGP: qsTr("埃及镑"), ISK: qsTr("冰岛克朗"), NOK: qsTr("挪威克朗"), SEK: qsTr("瑞典克朗"), CAD: qsTr("加元") })[c] || c
    }
    readonly property string rateStatus: {
        if (rates.busy) return qsTr("正在取汇率…")
        if (rates.date === "") return rates.netOff ? qsTr("汇率的联网已关闭，现在没有汇率") : rates.failed ? qsTr("还没拿到汇率，联网后重新打开计划试试") : qsTr("还没取到")
        const s = qsTr("%1 的汇率（来源：%2，%3 取到）").arg(rates.date).arg(rates.src).arg(Qt.formatDateTime(new Date(rates.updatedAt), "yyyy-MM-dd hh:mm"))
        return s + (rates.failed ? " · " + qsTr("更新失败（可能没联网，或者汇率服务暂时不可用），用的是上次的") : "")
    }
    function docName(d) {
        const kind = d.kind === "passport" ? qsTr("护照") : d.kind === "visa" ? qsTr("签证") : qsTr("其他证件")
        return [d.owner, kind, d.label].filter(x => x).join(" · ")
    }
    readonly property string emergencySummary: {
        const e = emergency, n = (e.contacts || []).length
        const parts = [e.insurer, e.policyNo ? qsTr("保单 %1").arg(e.policyNo) : "", n === 1 ? qsTr("1 位紧急联系人") : n ? qsTr("%1 位紧急联系人").arg(n) : ""].filter(x => x)
        return parts.length ? parts.join(" · ") : qsTr("保险、紧急联系人，所有计划共用。当地紧急电话在每个计划的“行李与待办”里填。")
    }

    function refresh() { history = app.historyList() }
    // 开发版：故意制造一个 QML 里的 JS 错误（0.5.1）
    function devThrow() { const nothing = null; return nothing.boom }
    onOpenChanged: if (open) { refresh(); photos.refreshCacheSize(); forceActiveFocus(); if (section === "me") mapc.needUnlockCount(); refreshSection() }
    Connections { target: app; function onDataChanged() { if (root.open) root.refresh() } }
    Connections { target: planPage; function onContentChanged() { if (root.open) root.refreshSection() } }   // 证件、撤销、别处改了数据
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
            tip: qsTr("关闭设置")
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
                    on: root.section === modelData.key || (root.section === "log" && modelData.key === "about")
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
                    on: root.section === modelData.key || (root.section === "log" && modelData.key === "about")
                    onPicked: root.section = modelData.key
                }
            }
        }
    }
    // 窄：顶部横向标签，一行放不下时换行（0.12.10：以前要拖才看得到"关于"）
    Flow {
        id: tabs
        visible: root.narrow
        anchors.top: head.bottom
        x: Theme.px(12)
        width: parent.width - 2 * x
        height: visible ? implicitHeight + Theme.px(8) : 0
        spacing: Theme.px(2)
        Repeater {
            model: root.mainSections.concat(root.footSections)
            delegate: NavItem {
                required property var modelData
                objectName: "setTab-" + modelData.key
                label: modelData.text
                on: root.section === modelData.key || (root.section === "log" && modelData.key === "about")
                onPicked: root.section = modelData.key
            }
        }
    }

    Flickable {
        id: flick
        objectName: "settingsFlick"
        anchors.top: root.narrow ? tabs.bottom : head.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.left: root.narrow ? parent.left : side.right
        clip: true
        contentHeight: body.implicitHeight + Theme.px(16)
        boundsBehavior: Flickable.StopAtBounds
        // 截图 ui-devtools（0.5.2）：滚到最底下的"开发版测试"
        Timer {
            running: shotPage === "ui-devtools" || shotPage === "ui-settings-about-end"
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
                        options: [{ key: "auto", text: qsTr("跟随系统") }, { key: "on", text: qsTr("完整动效") }, { key: "off", text: qsTr("减少动效") }]
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

            // ---------- 货币（T-07；Electron moneyHtml）----------
            SettingsCard {
                objectName: "setCard-money"
                visible: root.section === "money"
                width: parent.width
                title: qsTr("货币")
                SettingsRow {
                    width: parent.width
                    label: qsTr("主货币")
                    sub: qsTr("消费统计、分账结算都折算成这种货币")
                    TpSelect {
                        objectName: "setMainCur"
                        width: Theme.px(200)
                        options: rates.currencies.map(c => ({ key: c, text: c + " · " + root.curName(c) }))
                        current: rates.main
                        onPicked: (k) => rates.main = k
                    }
                }
                SettingsRow {
                    objectName: "setRatesRow"
                    width: parent.width
                    label: qsTr("联网汇率")
                    sub: root.rateStatus + qsTr("。需要时自动更新，一天最多一次；没联网时继续用这一版。")
                    TpButton {
                        objectName: "setRatesNow"
                        compact: true
                        enabled: !rates.busy
                        text: qsTr("现在更新")
                        onClicked: if (!rates.refreshNow()) root.toastRequested(qsTr("汇率的联网已关闭"), true)
                    }
                }
            }
            // ---------- 证件和紧急信息（T-08；Electron travelkit.js settingsHtml）----------
            SettingsCard {
                objectName: "setCard-docs"
                visible: root.section === "docs"
                width: parent.width
                title: qsTr("证件")
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("填护照、签证的到期日期，计划页会在有效期不够时提醒。不要填证件号码。")
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                }
                Repeater {
                    model: root.docRows
                    delegate: Item {
                        id: docRow
                        required property var modelData
                        width: parent ? parent.width : 0
                        height: docCol.implicitHeight
                        Column {
                            id: docCol
                            width: parent.width - docEdit.width - Theme.px(10)
                            spacing: Theme.px(2)
                            Text {
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: root.docName(docRow.modelData)
                                font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
                            }
                            Flow {
                                width: parent.width
                                spacing: Theme.px(6)
                                Text {
                                    objectName: "docDate-" + docRow.modelData.id
                                    text: docRow.modelData.expires ? qsTr("到期 %1").arg(docRow.modelData.expires) + (docRow.modelData.expired ? " · " + qsTr("已过期") : "") : qsTr("没填到期日期")
                                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); font.bold: docRow.modelData.expired
                                    color: docRow.modelData.expired ? Theme.danger : Theme.textSoft
                                }
                                Text {
                                    visible: docRow.modelData.planId !== ""
                                    text: "·"
                                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                                }
                                Text {
                                    visible: docRow.modelData.planId !== ""
                                    text: qsTr("关联：%1").arg(docRow.modelData.planName || qsTr("（已删除的计划）"))
                                    font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                                }
                            }
                            Text {
                                visible: docRow.modelData.notes !== ""
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: docRow.modelData.notes
                                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                            }
                        }
                        TpButton {
                            id: docEdit
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            compact: true
                            text: qsTr("修改")
                            onClicked: root.docRequested(docRow.modelData)
                        }
                    }
                }
                TpButton {
                    objectName: "docAdd"
                    compact: true
                    text: qsTr("添加证件")
                    onClicked: root.docRequested(null)
                }
            }
            SettingsCard {
                objectName: "setCard-emergency"
                visible: root.section === "docs"
                width: parent.width
                title: qsTr("紧急信息")
                SettingsRow {
                    objectName: "setEmergencyRow"
                    width: parent.width
                    label: qsTr("所有计划共用")
                    sub: root.emergencySummary
                    TpButton {
                        objectName: "setEmergencyEdit"
                        compact: true
                        text: qsTr("编辑")
                        onClicked: root.emergencyRequested()
                    }
                }
            }

            // ---------- 联网（T-09；Electron prefs.js NET_SERVICES、pages-more.js）----------
            SettingsCard {
                objectName: "setCard-net"
                visible: root.section === "net"
                width: parent.width
                title: qsTr("联网服务")
                SettingsRow {
                    width: parent.width
                    label: qsTr("离线模式")
                    sub: netc.offline ? qsTr("开着：所有联网服务都不用，下面的开关先不起作用。已经存下来的地图、汇率、天气照常显示。")
                                      : qsTr("开了以后所有联网服务都不用。")
                    Segmented {
                        objectName: "netOffline"
                        options: [{ key: "off", text: qsTr("关") }, { key: "on", text: qsTr("开") }]
                        current: netc.offline ? "on" : "off"
                        onPicked: (k) => netc.offline = k === "on"
                    }
                }
                Repeater {
                    model: root.netServices
                    delegate: SettingsRow {
                        id: netRow
                        required property var modelData
                        width: parent ? parent.width : 0
                        label: modelData.name
                        sub: modelData.use
                        Segmented {
                            objectName: "netSw-" + netRow.modelData.key
                            enabled: !netc.offline
                            opacity: enabled ? 1 : 0.45
                            options: [{ key: "off", text: qsTr("关") }, { key: "on", text: qsTr("开") }]
                            current: netc.switches[netRow.modelData.key] ? "on" : "off"
                            onPicked: (k) => netc.setSwitch(netRow.modelData.key, k === "on")
                        }
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("每项服务的提供方和署名见“关于”。")
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                }
            }

            // ---------- AI 助手（0.13.0；PO 2026-11-08 选五家 + 自定义、三档、月上限，阶段7设计预案.md §〇）----------
            SettingsCard {
                id: aiCard
                objectName: "setCard-ai"
                visible: root.section === "ai"
                width: parent.width
                title: qsTr("AI 助手")
                property string keyErr: ""
                property bool adv: false
                readonly property var cur: ai.providers.find(p => p.id === ai.provider) || ({})

                SettingsRow {
                    width: parent.width
                    label: qsTr("服务商")
                    sub: qsTr("你问的话和对话内容会发给这家处理。") + (aiCard.cur.chinaBlocked ? qsTr("中国大陆用不了 Claude。") : "") + (aiCard.cur.untested ? qsTr("自定义服务没有测试过，能不能用要看它是否兼容 OpenAI 接口。") : "")
                    TpSelect {
                        objectName: "aiProvider"
                        width: Theme.px(170)
                        enabled: !ai.busy
                        options: ai.providers.map(p => ({ key: p.id, text: p.name + (p.untested ? qsTr("（未测试）") : "") }))
                        current: ai.provider
                        onPicked: (k) => { ai.provider = k; aiCard.keyErr = "" }
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("API 钥匙")
                    sub: ai.hasKey ? qsTr("已存在 Windows 凭据管理器（末 4 位 %1）").arg(ai.keyTails[ai.provider]) : qsTr("还没有填。只收开发者平台的 API 钥匙（按用量付费），网页版的会员订阅不能用。")
                    Row {
                        spacing: Theme.px(6)
                        TpButton { objectName: "aiKeyClear"; visible: ai.hasKey; compact: true; text: qsTr("清除"); onClicked: ai.clearKey() }
                        TpButton { objectName: "aiSignup"; visible: !!aiCard.cur.signup; compact: true; text: qsTr("申请钥匙 ↗"); onClicked: ai.openSignup(ai.provider) }
                    }
                }
                Item {
                    width: parent.width
                    height: Theme.px(34)
                    Rectangle {
                        width: parent.width - keySave.width - Theme.px(8)
                        height: parent.height
                        radius: Theme.radiusControl
                        color: "#ffffff"
                        border.width: 1
                        border.color: aiCard.keyErr !== "" ? Theme.danger : keyInput.activeFocus ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.6) : Theme.lineStrong
                        TextInput {
                            id: keyInput
                            objectName: "aiKeyInput"
                            x: Theme.px(10); width: parent.width - Theme.px(20); height: parent.height
                            verticalAlignment: TextInput.AlignVCenter
                            echoMode: TextInput.Password
                            clip: true
                            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text
                            onAccepted: keySave.clicked()
                            onTextEdited: aiCard.keyErr = ""
                            Text {
                                visible: keyInput.text === ""
                                anchors.verticalCenter: parent.verticalCenter
                                text: ai.hasKey ? qsTr("粘贴新钥匙替换") : (ai.provider === "claude" ? qsTr("粘贴 sk-ant- 开头的钥匙") : qsTr("粘贴钥匙"))
                                font: keyInput.font; color: "#7f8e9a"
                            }
                        }
                    }
                    TpButton {
                        id: keySave
                        objectName: "aiKeySave"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        compact: true
                        kind: "primary"
                        text: qsTr("保存")
                        onClicked: { aiCard.keyErr = ai.setKey(keyInput.text); if (aiCard.keyErr === "") { keyInput.text = ""; root.toastRequested(qsTr("钥匙已存好"), false) } }
                    }
                }
                Text {
                    objectName: "aiKeyErr"
                    visible: aiCard.keyErr !== ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: aiCard.keyErr
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.danger
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("回答方式")
                    sub: ai.manualModel !== "" && ai.provider !== "custom" ? qsTr("手填了模型，档位只管要不要思考。") : qsTr("一个档位同时定模型和要不要思考。")
                    Segmented {
                        objectName: "aiTierSet"
                        options: [{ key: "fast", text: qsTr("快") }, { key: "balanced", text: qsTr("均衡") }, { key: "careful", text: qsTr("细致") }]
                        current: ai.tier
                        onPicked: (k) => ai.tier = k
                    }
                }
                Column {
                    objectName: "aiTierInfo"
                    width: parent.width
                    spacing: Theme.px(2)
                    Repeater {
                        model: ai.tiers
                        delegate: Text {
                            required property var modelData
                            width: parent ? parent.width : 0
                            elide: Text.ElideRight
                            text: ({ fast: qsTr("快"), balanced: qsTr("均衡"), careful: qsTr("细致") })[modelData.key] + "：" + (modelData.model || qsTr("（先填模型名）"))
                                  + " · " + (modelData.think ? qsTr("思考") : qsTr("不思考"))
                                  + (modelData.est ? " · " + qsTr("每轮约 %1").arg(modelData.est) : "")
                            font.family: serifFamily; font.pixelSize: Theme.px(12); font.features: Theme.tnum
                            font.bold: modelData.key === ai.tier
                            color: modelData.key === ai.tier ? Theme.textSoft : Theme.muted
                        }
                    }
                }
                // 高级：从接口刷新模型列表 / 手填模型、地址、自定义的单价
                Text {
                    objectName: "aiAdvToggle"
                    text: (aiCard.adv || ai.provider === "custom" ? "▾ " : "▸ ") + qsTr("高级")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.accent
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: aiCard.adv = !aiCard.adv }
                }
                Column {
                    visible: aiCard.adv || ai.provider === "custom"
                    width: parent.width
                    spacing: Theme.px(8)
                    SettingsRow {
                        width: parent.width
                        label: qsTr("模型")
                        sub: ai.modelsState === "busy" ? qsTr("正在取列表…") : ai.modelsState === "ok" ? qsTr("点一个就用它；清空回到跟着档位。") : ai.modelsState
                        TpButton { objectName: "aiModelsRefresh"; compact: true; text: qsTr("从接口刷新列表"); enabled: ai.modelsState !== "busy"; onClicked: ai.refreshModels() }
                    }
                    TpField {
                        objectName: "aiModelInput"
                        width: parent.width
                        value: ai.manualModel
                        placeholder: ai.provider === "custom" ? qsTr("模型名（必填）") : qsTr("跟着档位（现在是 %1）").arg(ai.tiers.length ? ai.tiers[1].model : "")
                        onAccepted: ai.manualModel = text
                        onEditingChanged: if (!editing) ai.manualModel = text
                    }
                    Flow {
                        visible: ai.modelList.length > 0
                        width: parent.width
                        spacing: Theme.px(6)
                        Repeater {
                            model: ai.modelList.slice(0, 40)
                            delegate: TpChip { required property string modelData; text: modelData; on: modelData === ai.model; onClicked: ai.manualModel = modelData }
                        }
                    }
                    SettingsRow { width: parent.width; label: qsTr("接口地址"); sub: ai.provider === "custom" ? qsTr("兼容 OpenAI 的地址，一般到 /v1 为止（必填）") : qsTr("一般不用改；留空用默认") }
                    TpField {
                        objectName: "aiBaseInput"
                        width: parent.width
                        value: ai.base
                        placeholder: ai.defaultBase || "https://…/v1"
                        onAccepted: ai.base = text
                        onEditingChanged: if (!editing) ai.base = text
                    }
                    Row {
                        visible: ai.provider === "custom"
                        spacing: Theme.px(8)
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("每百万 token（%1）输入").arg(ai.custom.cur); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft }
                        TpField { objectName: "aiPriceIn"; width: Theme.px(70); value: ai.custom.pin || ""; onEditingChanged: if (!editing) ai.setCustom("pin", text) }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("输出"); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft }
                        TpField { objectName: "aiPriceOut"; width: Theme.px(70); value: ai.custom.pout || ""; onEditingChanged: if (!editing) ai.setCustom("pout", text) }
                    }
                    Text {
                        visible: ai.provider === "custom"
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("单价可以留空：留空只记 token，不算钱，也不算进月上限。")
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("联网搜索")
                    sub: qsTr("让 AI 查路线、开放时间等最新信息，每轮最多搜 3 次，每次另收一点费用（下个版本才用上）。")
                    Segmented {
                        objectName: "aiWeb"
                        options: [{ key: "off", text: qsTr("关") }, { key: "on", text: qsTr("开") }]
                        current: ai.web ? "on" : "off"
                        onPicked: (k) => ai.web = k === "on"
                    }
                }
                SettingsRow {
                    width: parent.width
                    label: qsTr("每月上限")
                    sub: qsTr("各家按自己的币种计价，用汇率折成%1。到 80% 提醒，到了先停下，可以当场提高。").arg(ai.mainCur)
                    Row {
                        spacing: Theme.px(6)
                        Text { anchors.verticalCenter: parent.verticalCenter; text: ai.mainCur; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft }
                        TpField {
                            objectName: "aiCapInput"
                            width: Theme.px(80)
                            value: String(Math.round(ai.cap * 100) / 100)
                            onAccepted: { const v = parseFloat(text); if (v >= 0) ai.cap = v }
                            onEditingChanged: if (!editing) { const v = parseFloat(text); if (v >= 0 && Math.abs(v - ai.cap) > 0.004) ai.cap = v }
                        }
                    }
                }
                SettingsRow {
                    objectName: "aiUsed"
                    width: parent.width
                    label: qsTr("本月已用")
                    sub: ai.usageRows.length ? "" : qsTr("这个月还没用过")
                    Text { text: qsTr("约 %1 / %2").arg(ai.money(ai.used, ai.mainCur)).arg(ai.money(ai.cap, ai.mainCur)); font.family: serifFamily; font.pixelSize: Theme.px(13); font.features: Theme.tnum; color: ai.capReached ? Theme.danger : Theme.textSoft }
                }
                Repeater {
                    model: ai.usageRows
                    delegate: Text {
                        required property var modelData
                        width: parent ? parent.width : 0
                        wrapMode: Text.Wrap
                        text: modelData.name + "：" + modelData.text
                        font.family: serifFamily; font.pixelSize: Theme.px(12); font.features: Theme.tnum; color: Theme.muted
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("钥匙存在 Windows 凭据管理器里，不放进数据文件夹，导出和备份都不带。")
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
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

            // ---------- 关于（T-13）和更新日志 ----------
            AboutSection {
                id: aboutSection
                visible: root.section === "about"
                width: parent.width
                services: root.netServices
                onLogClicked: root.section = "log"
                onToastRequested: (text, error) => root.toastRequested(text, error)
            }
            SettingsCard {
                objectName: "setCard-log"
                visible: root.section === "log"
                width: parent.width
                title: qsTr("更新日志")
                LinkText { objectName: "aboutLogBack"; text: qsTr("← 返回关于"); onClicked: root.section = "about" }
                Text {
                    visible: app.lang === "en"
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("更新日志只有中文。")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                }
                Text {
                    objectName: "logText"
                    width: parent.width
                    wrapMode: Text.Wrap
                    textFormat: Text.MarkdownText
                    text: root.section === "log" ? app.changelogText() : ""   // 约 100 KB，只在看的时候排版
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
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
                // 0.12.5：缓存用量和清空（只移进回收站）；0.12.6：硬盘上限
                SettingsRow {
                    objectName: "cacheTiles"
                    width: parent.width
                    label: qsTr("地图瓦片缓存")
                    sub: (netc.tileClearing ? qsTr("正在移进回收站…") : !netc.tileScanned ? qsTr("正在统计…") : netc.tileBytes === 0 ? "0 KB" : root.sizeText(netc.tileBytes)) + " · " + qsTr("看过的地图存在这里，离线也能看。")
                    TpButton { objectName: "cacheTilesClear"; text: qsTr("清空"); compact: true; enabled: netc.tileBytes > 0 && !netc.tileClearing; onClicked: netc.clearTileCache() }
                }
                Column {   // 四个选项太宽，放在右边会把说明挤成窄列：控件放第二行
                    objectName: "cacheTilesMax"
                    width: parent.width
                    spacing: Theme.px(6)
                    SettingsRow {
                        width: parent.width
                        label: qsTr("地图瓦片缓存上限")
                        sub: qsTr("超过时自动删掉最旧的。调低后马上按新上限删。")
                    }
                    Segmented {
                        objectName: "cacheTilesMaxPick"
                        options: [{ key: "512", text: "500 MB" }, { key: "1024", text: "1 GB" }, { key: "2048", text: "2 GB" }, { key: "5120", text: "5 GB" }]
                        current: String(netc.tileMaxMB)
                        onPicked: (k) => netc.tileMaxMB = Number(k)
                    }
                }
                SettingsRow {
                    objectName: "cacheThumbs"
                    width: parent.width
                    label: qsTr("缩略图缓存")
                    sub: (photos.cacheBytes < 0 ? qsTr("正在统计…") : photos.cacheBytes === 0 ? "0 KB" : root.sizeText(photos.cacheBytes)) + " · " + qsTr("超过 1 GB 时自动删掉最旧的。")
                    TpButton { text: qsTr("清空"); compact: true; enabled: photos.cacheBytes > 0; onClicked: photos.clearThumbCache() }
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
    // 0.12.11 放不下时右边一直有位置条（数据、关于这些长的分组，一眼看出能往下滚）
    Rectangle {
        objectName: "settingsScrollBar"
        visible: flick.contentHeight > flick.height + 1
        x: flick.x + flick.width - width - Theme.px(4)
        y: flick.y + Theme.px(4) + flick.visibleArea.yPosition * (flick.height - Theme.px(8))
        width: Theme.px(4)
        height: Math.max(Theme.px(24), flick.visibleArea.heightRatio * (flick.height - Theme.px(8)))
        radius: width / 2
        color: Theme.lineStrong
    }
}
