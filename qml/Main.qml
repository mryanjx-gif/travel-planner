// 主窗口。阶段 ⓪ 原型（平面地图 + 地球 + 液态玻璃面板）+ 0.4.0 第一批界面：
// 数据文件夹选择、冲突弹框、外部修改提示、只读 / 更新版本横幅、关闭时没能保存、设置面板（通用 + 数据）、快捷键。
// 0.5.1：出错小框（右下角，和外部修改卡片叠在一起）、"上次意外退出了"提示。
// 0.6.0：从 Electron 版导入（选方式、进度、结果）、别处开着时的只读横幅和"仍然编辑"、更新版本写的数据只读。
// 0.7.0：照片——导入确认表（PhotoImportReview）、清理没用的照片（PhotoCleanupDialog）。
// 正式的地图页从阶段 ② 开始做。
// 0.8.7：左下状态胶囊 + 筛选下拉、"此刻"卡片、右边的简易计划面板、换底图时截一帧盖住再淡出。
// 0.8.8：顶栏（⋯ 泡泡、↩、列表按钮、头像菜单）代替原型条；"我的旅程"列表（Ctrl B）、Ctrl K 命令面板、前进 / 后退、Esc 一层一层关。
// 0.8.10：页面结构（PO 2026-11-06）——地图分成 计划 / 想去 / 足迹 / 照片 四种模式（nav 的页面就是模式），日历 / 游记 / 账本是整页（先占位）。
import QtQuick
import QtQuick.Dialogs
import TPApp

Window {
    id: win
    width: 1400
    height: 900
    minimumWidth: 360
    minimumHeight: 500
    visible: false            // C++ 按记住的大小和位置摆好后再显示（WindowKeeper）
    color: Theme.bg
    title: qsTr("旅行计划") + " v" + app.version + (app.windowTitleExtra !== "" ? "  " + app.windowTitleExtra : "")

    Binding { target: Theme; property: "scale"; value: app.fontScale }
    Binding { target: Theme; property: "motion"; value: app.motion === "on" || (app.motion === "auto" && app.systemMotion) }
    Binding { target: Theme; property: "glass"; value: app.transparency !== "off" }
    Binding { target: Theme; property: "glassMode"; value: app.glassMode }
    // 带不动就换标准玻璃，设置页写明原因；改一次外观设置或下次启动再试
    GlassWatch { window: win; active: Theme.liquidOn; onSlow: Theme.glassSlow = true }
    Connections { target: app; function onAppearanceChanged() { Theme.glassSlow = false } }
    Binding { target: Fmt; property: "en"; value: app.lang === "en" }

    property bool globeMode: false
    onGlobeModeChanged: globeMode ? globeHold.stop() : globeHold.restart()
    Timer { id: globeHold; interval: 60000 }   // 离开地球后留多久（见 GlobeItem 的 active）
    // ---- 0.8.9 窄窗口（S-15，Electron mapfirst.css / shell.css / base.css 的断点）----
    // > 1100 完整；≤ 1100 ⋯ 不显示页名、头像菜单加页面入口；≤ 900 边距 10、"新建计划"只剩 ＋、设置面板占满宽度；
    // ≤ 768 简易面板 / 编辑器变成底部抽屉（PO 选方案 B，上半截留给地图）；≤ 640 ↩ 只剩图标（原生版本来就是图标）
    readonly property bool wide: width > 1100
    readonly property bool w900: width <= 900
    readonly property bool drawerMode: width <= 520   // 0.8.10：PO 选 ⑥ A，从 768 改成 520（右边面板还能露出一点地图的最窄宽度）
    readonly property int gap: w900 ? 10 : 14
    property real globeFade: globeMode ? 1 : 0
    // 0.9.7：变形播完交接时只淡 0.12 秒（两边已经逐像素对齐）；动效关掉时 0.2 秒；其余（截图、内存测试的直接切换）0.45 秒
    property int globeFadeMs: 450
    Behavior on globeFade { NumberAnimation { duration: win.globeFadeMs; easing.type: Easing.InOutCubic } }
    property string rhiName: ""
    property string rhiDevice: ""
    property int fps: 0
    // 当前页（0.8.8 起由 nav 记录，带前进 / 后退）。0.8.10：plan / wish / foot / photo（地图的四种模式）、
    // calendar / journal / ledger（整页）、settings。goPage("map") = 回到当前模式
    readonly property bool settingsOpen: nav.page === "settings"
    readonly property var modeKeys: ["plan", "wish", "foot", "photo"]
    readonly property var fullPageKeys: ["calendar", "journal", "ledger", "aiplan"]   // aiplan：0.13.2 让 AI 帮我规划
    readonly property bool onMapPage: modeKeys.indexOf(nav.page) >= 0
    readonly property bool fullPageOpen: fullPageKeys.indexOf(nav.page) >= 0
    // 0.10.0 完整计划页（S-02）：nav 的页面 "trip"，参数 { id }。地图还在后面露着、照常能拖；mapLive = 地图正显示着（模式页或计划页）
    readonly property bool tripOpen: nav.page === "trip"
    readonly property bool mapLive: win.onMapPage || win.tripOpen
    property string tripId: ""
    property int tripStop: -1
    property string tripTab: ""            // 打开时直接到哪个标签（quick = 消费标签的一行快记）
    property string tripTabPlan: ""        // 只对这个计划算（从游记退回计划页）；空 = 下一个打开的就是
    // 窗口太窄、计划页盖满时地图不让位，± 和图层也藏起来
    readonly property bool tripRoom: win.width - planPanel.width - 2 * win.gap >= Theme.px(260)
    function openTrip(id, stop) {
        if (editor.open) { win.tripTab = ""; toast.show(qsTr("请先完成或放弃当前编辑"), false); return }
        win.nowOpen = false
        win.tripStop = stop === undefined ? -1 : stop
        nav.go("trip", { id: id })
    }
    // Electron mapfirst.js onPage / follow：打开时地图只画这一条、框住它；关掉回到地图时打开它的简易面板
    function syncTrip() {
        const id = win.tripOpen ? (nav.params.id || "") : ""
        if (id === win.tripId) return
        const old = win.tripId
        win.tripId = id
        planPage.show(id)
        plans.setSolo(id)
        win.updateInsets()
        if (id !== "") {
            planPanel.resetView()
            if (!planPage.info.id) return
            plans.noteRecent(id)
            const stop = win.tripStop
            win.tripStop = -1
            mapc.markers.selectPlan(id, stop, false)
            mapc.flyToPlan(id)
            if (stop >= 0) planPanel.jumpTo(stop)
            if (win.tripTab !== "" && (win.tripTabPlan === "" || win.tripTabPlan === id)) planPanel.openTab(win.tripTab)
            win.tripTab = ""
            win.tripTabPlan = ""
        } else if (old !== "") {
            if (win.onMapPage && !editor.open && plans.mode !== "photo") win.revealToast(plans.openPlan(old, false))   // 照片模式从相册打开的：回相册
            else mapc.markers.clearSelection()
        }
    }
    function goPage(p) {
        if (p === "map") p = plans.mode
        // 编辑器开着时不换模式（换过去这条计划可能不在地图上）
        if (editor.open && win.modeKeys.indexOf(p) >= 0 && p !== plans.mode) { toast.show(qsTr("请先完成或放弃当前编辑"), false); return }
        nav.go(p)
        Qt.callLater(win.openListIfEmpty)
    }
    function modeName(k) { const it = win.navPages.find(x => x.key === k); return it ? it.name : k }
    property bool newerDismissed: false
    property bool pickFromStartup: false   // 第一次用时从"选择其他文件夹…"进来的（新建空数据后接着问要不要导入）
    readonly property var map: mapLoader.item
    // 图层泡泡（顺序 = 离按钮由近到远）。0.9.0 去掉浅灰；0.9.2 图层合并（PO 2026-11-06）：加"地球"，
    // 单独的地球 / 平面按钮去掉。地球只有卫星图；另外三种一直是平面
    readonly property var bases: [
        { key: "globe", name: qsTr("地球") },
        { key: "sat", name: qsTr("卫星图") },
        { key: "street", name: qsTr("街道图") },
        { key: "unlock", name: qsTr("已解锁") }
    ]
    // 图层里现在选的（地球上是 "globe"）
    readonly property string layerKey: win.globeMode ? "globe" : mapc.base

    function setRhiInfo(name, dev) { rhiName = name; rhiDevice = dev }
    // 自检"性能"（0.9.1）：临时关掉 / 打开透明效果，量玻璃占多少（设置页的开关在阶段 ⑥）
    function setGlassForTest(on) { Theme.glass = on }
    function setMotionForTest(on) { Theme.motion = on }
    function setGlassSlowForTest(on) { Theme.glassSlow = on }
    function glassReasonForTest() { return Theme.glassReason }
    // 图形后端退回原因（C++ 里是中文原文）：整句或"前半句：后半句"分别查词典，查不到的部分（如显卡报的错误）原样显示
    function trReason(r) {
        if (!r) return ""
        const t = qsTr(r)
        if (t !== r) return t
        const i = r.indexOf("：")
        return i < 0 ? r : qsTr(r.slice(0, i + 1)) + qsTr(r.slice(i + 1))
    }

    // 出错报告里的"页面"（0.5.1）：以后各页面做出来后在这里补
    Binding { target: errors; property: "page"; value: win.settingsOpen ? "settings" : win.tripOpen ? "trip" : win.fullPageOpen ? nav.page : (win.globeMode ? "globe" : "map-" + plans.mode) }

    // 开发版：故意制造一个 QML 里的 JS 错误（TypeError），实测出错小框
    function devThrow() {
        const nothing = null
        return nothing.boom
    }

    // 地图 ↔ 地球直接切换（淡入淡出）：地球中心对准地图中心，缩放按屏幕中心比例尺换算（0.9.0）。
    // 0.9.7 起只有启动、截图、内存测试用它；用户的操作走下面的变形过渡（flatten / goGlobe）
    function toggleGlobe() {
        win.globeFadeMs = 450
        globe.setMorph(0)
        if (!globeMode) {
            globe.centerLat = mapc.lat
            globe.centerLng = mapc.lng
            globe.setFromZoomLevel(mapc.zoom)
        } else {
            mapc.jumpTo(globe.centerLat, globe.centerLng, globe.zoomLevelEquivalent())
        }
        globeMode = !globeMode
    }

    // ---- 0.9.7 地球 ⇄ 平面的变形过渡（C1）、自动摊平 / 卷回（B3，PO 选 B：越过 5.5 级播一段摊平动画）----
    // 摊平：平面先悄悄跳到终点镜头（在地球下面开始要瓦片），地球一边飞过去一边把球面摊成那一级的墨卡托平面，
    // 播完和平面逐像素对齐，0.12 秒交给平面。卷回：地球（先是平的，和平面对齐）盖上来，等瓦片到齐再卷成球。
    // 平面在变形期间一直垫在下面（瓦片没到的地方露出来的是它）；星空、光晕、城市灯光、昼夜跟着进度淡出 / 淡入
    readonly property real flattenZoom: 5.5   // 进（和 GlobeItem.kFlattenZoom、MapController.kGlobeMaxZoom 一样）
    readonly property real rollZoom: 5.0      // 出（和 MapController.kGlobeRollZoom 一样）
    readonly property int morphMs: 600
    property var afterMorph: null
    property bool morphTest: false            // 自检：截图 / 自检时也自动摊平 / 卷回（平时 testRun 时不自动）
    property bool startDone: false            // 启动时进不进地球已经定了（之前不自动卷回）
    property var editFromGlobe: null          // 从地球打开编辑器：打开前的地球镜头 { lat, lng, zoom }（B4）
    property string lastEditId: ""
    property string finishedId: ""
    // 变形要平面地图在（交接给它）、动效开着；不然直接淡入淡出
    function morphAllowed() { return Theme.motion && win.mapLive && win.map !== null }
    function normLng(l) { return ((l + 540) % 360 + 360) % 360 - 180 }
    // 地球 → 平面：终点 (lat, lng, zoom)；then = 平面接手后接着做的事
    function flatten(lat, lng, zoom, then) {
        if (!win.globeMode || globe.morphing) { if (then) then(); return }
        zoom = Math.max(zoom, mapc.minZoom)
        lng = win.normLng(lng)
        mapc.jumpTo(lat, lng, zoom)   // jumpTo 不转给地球
        if (!win.morphAllowed()) {
            win.globeFadeMs = 200
            win.globeMode = false
            if (then) then()
            return
        }
        win.afterMorph = then || null
        globe.morphTo(1, win.morphMs, lat, lng, zoom, false)
    }
    // 平面 → 地球（平面现在 ≤ 5 级左右）：终点 (lat1, lng1, zoom1)
    function rollUp(lat1, lng1, zoom1, then) {
        if (win.globeMode || globe.morphing) { if (then) then(); return }
        mapc.stop()
        globe.stop()
        globe.setMorph(0)
        globe.centerLat = mapc.lat
        globe.centerLng = mapc.lng
        globe.setFromZoomLevel(mapc.zoom)
        if (!win.morphAllowed()) {
            globe.flyTo(lat1, win.normLng(lng1), zoom1, 0)
            win.globeFadeMs = 200
            win.globeMode = true
            if (then) then()
            return
        }
        globe.setMorph(1)
        win.globeFadeMs = 120
        win.globeMode = true
        win.afterMorph = then || null
        globe.morphTo(0, win.morphMs, lat1, win.normLng(lng1), zoom1, true)
    }
    // 平面 → 地球：缩放高于 5 级时先在原地缩到 5 级（地球拉不到那么近），再卷；before = 卷之前做的事（换成卫星图）
    function goGlobe(lat1, lng1, zoom1, then, before) {
        if (win.globeMode) { if (then) then(); return }
        if (mapc.zoom > win.rollZoom + 0.01 && win.morphAllowed()) {
            mapc.easeTo(mapc.lat, mapc.lng, win.rollZoom, 450)
            win.afterFlatMove(() => { if (before) before(); win.rollUp(lat1, lng1, Math.min(zoom1, win.rollZoom), then) })
            return
        }
        if (before) before()
        win.rollUp(lat1, lng1, Math.min(zoom1, win.rollZoom), then)
    }
    // 平面的动画停下来以后做 fn（最多等 3 秒）
    property var flatMoveCb: null
    function afterFlatMove(fn) { win.flatMoveCb = fn; flatMoveTimer.n = 0; flatMoveTimer.restart() }
    Timer {
        id: flatMoveTimer
        interval: 30
        repeat: true
        property int n: 0
        onTriggered: {
            if (mapc.moving && ++n < 100) return
            stop()
            const f = win.flatMoveCb
            win.flatMoveCb = null
            if (f) f()
        }
    }
    // 地球的飞行停下来以后做 fn
    property var globeFlyCb: null
    Connections {
        target: globe
        function onFlattenRequested(lat, lng, zoom) { win.flatten(lat, lng, zoom, null) }
        function onFlyFinished() { const f = win.globeFlyCb; win.globeFlyCb = null; if (f) f() }
        function onMorphFinished(m) {
            const w = globe.takeWheel()
            if (m >= 1) {
                win.globeFadeMs = 120
                win.globeMode = false
                if (w[0] !== 0) mapc.wheel(w[0], w[1], w[2])   // 变形期间滚的轮交给平面
            } else if (w[0] !== 0) {
                globe.wheel(w[0], w[1], w[2])
            }
            const f = win.afterMorph
            win.afterMorph = null
            if (f) f()
        }
    }
    Connections {
        target: mapc
        function onGlobeRollRequested(lat, lng, zoom) { win.rollUp(lat, lng, zoom, null) }
        // 平面的动画停在 5 级以下（框住一个计划、Ctrl K…），图层是地球：卷回地球
        function onMovingChanged() {
            if (!mapc.moving && mapc.autoGlobe && win.startDone && mapc.zoom <= win.rollZoom - 0.01 && !flatMoveTimer.running)
                Qt.callLater(() => { if (mapc.autoGlobe && !win.globeMode && !mapc.moving) win.rollUp(mapc.lat, mapc.lng, mapc.zoom, null) })
        }
    }
    Binding {
        target: mapc
        property: "autoGlobe"
        value: mapc.globeLayer && !win.globeMode && win.mapLive && !editor.open && !globe.morphing && globe.morph === 0
               && win.map !== null && (!testRun || win.morphTest)
    }
    // 编辑器关掉后回到地球（B4，PO 选 B）：卷回地球、对着刚编辑的计划（最近 5 级、让开面板）；没有这个计划（新建后放弃）时回到打开前的视角
    function returnAfterEdit() {
        const g = win.editFromGlobe
        win.editFromGlobe = null
        const id = win.finishedId || win.lastEditId
        win.finishedId = ""
        if (!g || !mapc.globeLayer || win.globeMode || !win.onMapPage || editor.open) return
        const cam = id !== "" ? mapc.fitPlanCamera(id, win.rollZoom) : []
        if (cam.length === 3) win.goGlobe(cam[0], cam[1], cam[2], null)
        else win.goGlobe(g.lat, g.lng, g.zoom, null)
    }
    Connections {
        target: editor
        function onPlanChanged() { if (editor.open && editor.planId !== "") win.lastEditId = editor.planId }
        function onOpenChanged() {
            if (editor.open) { win.lastEditId = editor.planId; return }
            if (win.editFromGlobe) Qt.callLater(win.returnAfterEdit)
        }
    }

    // 换底图（0.8.7）：先截一帧盖住，等两帧画完再真的换，新底图画好后淡出（不闪底色）。
    // 0.9.2（PO 选 X）：不再固定停 0.38 秒，按迹象判断"画好了"——地图库说样式加载完、瓦片服务没有在路上的、
    // 0.1 秒没有新的帧，三条都满足就淡出（0.3 秒）；最多等 1 秒。换模式不再用它（四种模式共用一个底图，PO 选 A）
    property var pendingSwap: null
    property int coverFrames: 0
    // 0.9.1 计时：盖帧过渡的每一段（等两帧、执行、停、淡出）写进日志（"[计时] 换模式 过渡全程 …"）
    property string swapLabel: ""
    property real swapT0: 0
    property real swapT1: 0
    property real swapT2: 0
    property real swapT3: 0
    function switchBase(key) {
        if (key === mapc.base) return
        win.coverSwap(() => { mapc.base = key }, true, "换底图")
    }
    property real lastFrameAt: 0
    Connections {
        target: win
        enabled: baseCover.active
        function onFrameSwapped() { win.lastFrameAt = perf.now() }
    }
    // 新底图画好了没有：地图库说样式加载完、瓦片服务没有在路上的，再加上下面任一条：
    // 0.1 秒没有新的帧（地图库画完就不再要帧），或者换了 0.3 秒以上、0.25 秒没有新的瓦片回来（别的动画一直在要帧时）
    function baseDrawn() {
        const m = mapLoader.item
        if (!m || !m.styleLoaded) return false
        if (mapc.tilesBusy() > 0) return false
        const now = perf.now()
        if (now - swapT2 < 100) return false
        return now - win.lastFrameAt > 100 || (now - swapT2 > 300 && mapc.tilesQuietMs() > 250)
    }
    function coverSwap(fn, slow, label) {
        if (!Theme.motion || win.globeMode || mapLoader.status !== Loader.Ready || baseCover.active || !win.mapWanted) {
            const t0 = perf.now()
            fn()
            perf.mark(label + " 直接换（不盖帧）", perf.now() - t0)
            return
        }
        swapLabel = label
        swapT0 = perf.now()
        pendingSwap = fn
        baseCover.fadeMs = 300
        baseCover.fading = false
        baseCover.active = true
        baseCover.opacity = 1
        baseCover.scheduleUpdate()
        coverFrames = 0
    }
    function coverFrame() {
        if (pendingSwap === null) return
        if (++coverFrames < 2) return
        const f = pendingSwap
        pendingSwap = null
        swapT1 = perf.now()
        f()
        swapT2 = perf.now()
        win.lastFrameAt = swapT2
        coverHold.restart()
    }
    Timer {
        id: coverHold
        interval: 50
        repeat: true
        onTriggered: {
            if (!win.baseDrawn() && perf.now() - win.swapT2 < 1000) return
            stop()
            win.swapT3 = perf.now()
            baseCover.fading = true
            baseCover.opacity = 0
        }
    }
    function coverDone() {
        const t4 = perf.now()
        perf.mark(swapLabel + " 过渡全程", t4 - swapT0,
                  "等两帧 " + (swapT1 - swapT0).toFixed(0) + "、执行 " + (swapT2 - swapT1).toFixed(0) + "、停 " + (swapT3 - swapT2).toFixed(0)
                  + "、淡出 " + (t4 - swapT3).toFixed(0))
    }

    // ---- 0.8.10 地图模式 ----
    // 0.9.2（PO 选 A）：四种模式共用一个底图，换模式不换底图，也不再截帧盖住（PO："同一张地图，直接把标记换一下"）：
    // 路线直接换，标记 0.15 秒淡入。足迹也不再自动换成已解锁（PO 07:21），要看已解锁就在图层里点
    function applyMode(m) {
        if (m === plans.mode) return
        const t0 = perf.now()
        plans.setMode(m)
        perf.mark("换模式（直接换）", perf.now() - t0)
        if (!win.globeMode && win.mapLive && win.map) win.map.fadeInMarkers()
    }
    // 图层泡泡、Ctrl K 选底图（0.9.2 图层合并）：地球 = 进地球（地球只有卫星图，平面底图也换成卫星图）；
    // 另外三种 = 平面上的这种底图（在地球上时回到平面）。选了什么记进设置，下次打开还是它
    // 0.9.7：平面上选地球 = 高于 5 级先缩到 5 级，再卷成球；地球上选另外三种 = 先放大到 5.5 级、摊平，再换底图
    function pickBase(b) {
        if (b === "globe") {
            mapc.globeLayer = true
            // 离线 / 在线底图关了时平面底图不换（地球照样能看缓存里的卫星瓦片，没有的地方用简图）
            if (!win.globeMode) win.goGlobe(mapc.lat, mapc.lng, Math.min(mapc.zoom, win.rollZoom), null, () => { if (!mapc.tilesOff) mapc.base = "sat" })
            return
        }
        mapc.globeLayer = false
        if (win.globeMode) win.flatten(globe.centerLat, globe.centerLng, Math.max(globe.zoom, win.flattenZoom), () => { if (b !== mapc.base) win.switchBase(b) })
        else win.switchBase(b)
    }
    // 启动、截图：直接进地球（不播变形）
    function enterGlobeNow() {
        mapc.globeLayer = true
        if (!win.globeMode) { if (!mapc.tilesOff) mapc.base = "sat"; win.toggleGlobe() }
    }
    Connections {
        target: nav
        function onChanged() {
            if (nav.page !== "trip" && nav.page !== "journal") { win.tripTab = ""; win.tripTabPlan = "" }   // 从游记没退回去、去了别处：不留着
            win.syncTrip()
            if (!win.onMapPage || nav.page === plans.mode) return
            if (editor.open) { toast.show(qsTr("请先完成或放弃当前编辑"), false); nav.go(plans.mode); return }
            win.applyMode(nav.page)
        }
    }

    // 退出前（0.8.0）：先在窗口还在的时候卸掉地图，让 maplibre 在渲染线程上释放（main.cpp 调用）
    function releaseMap() { mapLoader.active = false }

    // F11：由 C++ 做（不用 Qt 的全屏状态，最大化的窗口退出全屏时不会先变小，见 src/platform/fullscreen.h）
    function toggleFullscreen() {
        app.toggleFullscreen()
        if (app.fullscreen) toast.show(qsTr("已进入全屏，按 F11 退出"), false)
    }

    // 关窗口：按"关闭按钮"设置（直接退出 / 收到托盘 / 最小化），退出前先保存
    onClosing: (close) => { close.accepted = false; planPanel.flushGuide(); app.closeButton() }

    // ---- 被玻璃采样的那一层：地图 + 地球 ----
    property bool mapWanted: false
    Item {
        id: scene
        anchors.fill: parent
        onWidthChanged: if (width > 0 && height > 0) win.mapWanted = true
        onHeightChanged: if (width > 0 && height > 0) win.mapWanted = true

        Rectangle {   // 地图没加载出来时的底色
            anchors.fill: parent
            color: "#dfe7ee"
            visible: mapLoader.status !== Loader.Ready
            Text {
                anchors.centerIn: parent
                visible: mapLoader.status === Loader.Error
                text: qsTr("地图库没有加载（测试环境或安装不完整）")
                font.family: serifFamily; font.pixelSize: 14; color: Theme.muted
            }
        }
        Loader {
            id: mapLoader
            anchors.fill: parent
            // 窗口有了大小才加载地图（0.8.2）：maplibre 的地图项在加载完 0.25 秒后按自己当时的大小建地图，
            // 那时窗口还没显示出来（大小是 0）就抛异常 "failed to resize: size is empty"，地图一直是空的。
            // 启动时主线程忙（云端截图"已解锁"）就会碰上；以前也可能发生，只是少见
            // 只"打开"一次（mapWanted 不会再变回 false）：最小化等情况下大小变成 0 时不能卸掉地图（会在主线程上释放，见 releaseMap）
            active: win.mapWanted
            source: Qt.resolvedUrl("FlatMap.qml")
            // 0.9.7：变形期间（和摊平后地球还在淡出时）平面一直垫在下面
            opacity: globe.morph > 0 || globe.morphing ? 1 : 1 - win.globeFade
            visible: opacity > 0.001
            onLoaded: item.interactive = Qt.binding(() => !win.globeMode)
        }

        // 换底图时盖在地图上的一帧（0.8.7）：换样式时地图库会先清掉旧底图、露出底色再一块块画新的；
        // 先把换之前的样子截下来盖住，换完停一会儿再淡出。截图用 ShaderEffectSource（显卡上复制一帧，不读回内存），
        // 平时 sourceItem 为空、不占显存；"动效"关掉时不盖，直接换
        ShaderEffectSource {
            id: baseCover
            objectName: "baseCover"
            anchors.fill: parent
            property bool active: false
            sourceItem: active ? mapLoader : null
            live: false
            hideSource: false
            visible: active
            opacity: 0
            Behavior on opacity { enabled: baseCover.fading; NumberAnimation { duration: baseCover.fadeMs; easing.type: Easing.InOutQuad } }
            property bool fading: false
            property int fadeMs: 420
            onOpacityChanged: if (fading && opacity <= 0.001) { fading = false; active = false; win.coverDone() }
        }

        // 0.9.7 平面上的交通小图标（PO 14:44 定，规则和地球一样；飞机沿画出来的线走、顺着方向转）
        FlatVehicle {
            id: flatVehicle
            objectName: "flatVehicle"
            mapController: mapc
            dpr: win.screen ? win.screen.devicePixelRatio : 1
            allowed: Theme.motion && shotPage === "" && !win.globeMode && win.mapLive && win.map !== null && !win.map.unlock && win.visibility !== Window.Minimized   // 截图时不走：就绪判断靠"不再有新帧"
        }
        Image {
            id: flatVehicleImg
            objectName: "flatVehicleImg"
            visible: !win.globeMode && flatVehicle.shown && flatVehicle.img !== "" && mapc.markers.shown && win.map !== null && !win.map.unlock
            width: flatVehicle.size
            height: flatVehicle.size
            x: flatVehicle.x - width / 2
            y: flatVehicle.y - height / 2
            source: flatVehicle.img !== "" ? "image://marker/" + flatVehicle.img : ""
            smooth: true
            readonly property bool plane: flatVehicle.img.startsWith("v/flight")
            rotation: plane ? flatVehicle.angle : 0
            mirror: !plane && flatVehicle.flip
            opacity: 1 - win.globeFade
        }
        // 0.9.3：地球后面的天空（星空 + 大气光晕）单独一层：星星闪烁时只重画这一层
        SkyItem {
            id: sky
            objectName: "sky"
            anchors.fill: parent
            opacity: win.globeFade * (1 - globe.morph)   // 0.9.7：摊平时星空、光晕跟着淡掉
            visible: opacity > 0.001
            centerLat: globe.centerLat
            centerLng: globe.centerLng
            distance: globe.distance
            // 星星闪、流星（Electron noAnim：系统减少动态效果 / 设置里关了星空动画）；窗口最小化、不在地图页时停下
            animate: Theme.motion && app.spaceAnim && win.globeMode && win.mapLive && win.visibility !== Window.Minimized
        }
        GlobeItem {
            id: globe
            objectName: "globe"
            anchors.fill: parent
            opacity: win.globeFade
            // 0.8.6：离开地球后多留一帧（看不见），让它把显卡上的大纹理换掉；active 只看透明度，不看 visible
            visible: opacity > 0.001 || releasePending
            // 0.9.8：图层还是地球时（自动摊平、编辑器）留 60 秒不放显存，卷回不用重建（重建一次主线程卡约 0.5 秒）；换了别的图层马上放
            active: win.globeMode || opacity > 0.001 || (mapc.globeLayer && globeHold.running && (!testRun || win.morphTest))
            onOpacityChanged: if (opacity <= 0.001 && !win.globeMode && morph !== 0) setMorph(0)   // 留着时也回到球（平面的 autoGlobe 要看）
            mapController: mapc
            // 0.9.3 空闲自转（C7）：动效开着、在地图页、地球、没在编辑、窗口没最小化；截图 / 自检时不自己转（自检里单独测）
            // 0.9.5：选中了计划、鼠标停在标记上时也不转（免得把正在看的计划转走）
            spinAllowed: Theme.motion && win.globeMode && win.onMapPage && !editor.open && win.visibility !== Window.Minimized && !testRun
                         && mapc.markers.selectedPlan === "" && !mapc.markers.pointerOn && !globe.morphing && globe.morph === 0
            // 0.9.7 自动摊平：放大越过 5.5 级发 flattenRequested（截图 / 自检时不自动，除非自检要测）
            autoFlatten: win.globeMode && !editor.open && (!testRun || win.morphTest)
            // 0.9.5 交通小图标（G1）：动效开着、在地球上、在地图页
            vehicleAllowed: Theme.motion && win.globeMode && win.mapLive && win.visibility !== Window.Minimized

            // 拖动：抓住的地方跟着鼠标走，松手有惯性（和平面一样）
            DragHandler {
                target: null
                enabled: win.globeMode && !globe.morphing && globe.morph === 0
                onActiveChanged: active ? globe.dragStart() : globe.dragEnd()
                onTranslationChanged: (delta) => globe.dragBy(delta.x, delta.y)
            }
            // 滚轮：一格半级，0.2 秒缓动，以鼠标处为中心（和平面一样）
            WheelHandler {
                enabled: win.globeMode
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => globe.wheel(event.angleDelta.y, event.x, event.y)
            }
            // 0.9.5：碰到路线就高亮那个计划、提示框显示计划名；点线 = 选中（和平面 FlatMap.qml 一样，线的命中由 mapc 按地球上的线算）
            HoverHandler {
                id: globeHover
                enabled: win.globeMode && !testRun
                cursorShape: mapc.lineHot ? Qt.PointingHandCursor : Qt.ArrowCursor
                onPointChanged: {
                    const p = point.position
                    const t = mapc.lineHover(p.x, p.y)
                    if (t !== "") globeTip.showAt(t, p.x, p.y - 14, "line")
                    else if (globeTip.kind === "line") globeTip.hide()
                }
                onHoveredChanged: if (!hovered) { if (globeTip.kind === "line") globeTip.hide(); mapc.lineLeave() }
            }
            // 0.9.7（I1）：双击以双击处为中心放大一级（越过 5.5 级就摊平）；单击等 0.25 秒确认不是双击再选线
            TapHandler {
                id: globeTap
                enabled: win.globeMode && !testRun && !editor.open && !globe.morphing
                onTapped: (ev, button) => {
                    if (globeTap.tapCount >= 2) { globeTapTimer.stop(); globe.wheel(240, ev.position.x, ev.position.y); return }
                    globeTapTimer.px = ev.position.x
                    globeTapTimer.py = ev.position.y
                    globeTapTimer.restart()
                }
            }
            Timer {
                id: globeTapTimer
                interval: 250
                property real px: 0
                property real py: 0
                onTriggered: if (win.globeMode && mapc.lineClick(px, py) && globeTip.kind === "line") globeTip.hide()
            }
        }
        // 0.9.5：地球上的标记（和平面同一个模型，在地球上时 C++ 按地球的相机摆；背面渐隐、边缘变小）
        MapMarkers {
            id: globeMarkers
            objectName: "globeMarkers"
            anchors.fill: parent
            tip: globeTip
            moving: globe.moving
            opacity: win.globeFade
            visible: win.globeMode && mapc.markers.shown
            active: win.globeMode && !win.photoMode   // 0.9.6：只在地球上建元素；0.11.1 照片模式只有照片
        }
        PhotoMarkers {
            objectName: "globePhotoMarkers"
            anchors.fill: parent
            tip: globeTip
            moving: globe.moving
            opacity: win.globeFade
            visible: active
            active: win.globeMode && mapc.photoLayer.active
            onPhotosOpened: (ids) => win.showMapPhotos(ids)
        }
        // 0.9.5 交通小图标：选中计划时一个图标按站点顺序把整条行程走一遍（飞行是飞机、朝前进方向转；其他是圆牌，往左走时左右翻）
        Image {
            id: vehicle
            objectName: "vehicle"
            visible: win.globeMode && globe.vehicleShown && globe.vehicleImg !== ""
            width: globe.vehicleSize
            height: globe.vehicleSize
            x: globe.vehicleX - width / 2
            y: globe.vehicleY - height / 2
            z: 2
            source: globe.vehicleImg !== "" ? "image://marker/" + globe.vehicleImg : ""
            smooth: true
            readonly property bool plane: globe.vehicleImg.startsWith("v/flight")
            rotation: plane ? globe.vehicleAngle : 0
            mirror: !plane && globe.vehicleFlip
            opacity: win.globeFade
        }
        MapTip { id: globeTip; anchors.fill: parent; visible: win.globeMode && kind !== "" }
        Connections {
            target: globe
            function onMovingChanged() { if (globe.moving && (globeTip.kind === "el" || globeTip.kind === "line")) globeTip.hide() }
        }
        // "连续失败 8 次"在地球上按地球的底图算（0.9.0）
        Binding { target: mapc; property: "globeBase"; value: win.globeMode ? globe.base : "" }
        // 0.9.5：标记、碰线、定位在地球上时按地球算
        Binding { target: mapc; property: "globeOn"; value: win.globeMode }
        // 0.9.5：在地球上要飞到超过 5.5 级的地方（城市里的行程、我的位置、Ctrl K 选站点）。
        // 0.9.7：在地球上沿大圆转过去并拉近到 5.5 级 → 摊平 → 在平面上继续拉近，一段连着走完
        Connections {
            target: mapc
            function onGlobeExitRequested(lat, lng, zoom, ms) {
                if (!win.globeMode) return
                if (!win.morphAllowed()) { win.flatten(lat, lng, zoom, null); return }
                win.globeFlyCb = () => win.flatten(lat, lng, win.flattenZoom, () => mapc.easeTo(lat, lng, zoom, 700))
                globe.flyTo(lat, lng, win.flattenZoom, Math.max(500, Math.round(ms * 0.7)))
            }
        }
    }

    // ---- 顶栏（0.8.8，S-06 / S-07）：⋯ 页面胶囊 + 泡泡、↩ 回到地图、保存状态、列表按钮 | 头像 ----
    readonly property real topH: topBar.barH
    // ⋯ 里的页面（0.8.10 PO 定的页面结构）：地图的 4 种模式 + 3 个整页（日历、游记、账本在阶段 ⑤ 之前是占位页，PO 选 ① A）
    readonly property var navPages: [
        { key: "plan", name: qsTr("计划"), g: 0, alias: qsTr("计划库") + " " + qsTr("地图") },
        { key: "wish", name: qsTr("想去"), g: 0, alias: qsTr("愿望清单") },
        { key: "foot", name: qsTr("足迹"), g: 0, alias: qsTr("已解锁") + " " + qsTr("去过的") },
        { key: "photo", name: qsTr("照片"), g: 0, alias: qsTr("相册") },
        { key: "calendar", name: qsTr("日历"), g: 1 },
        { key: "journal", name: qsTr("游记"), g: 1 },
        { key: "ledger", name: qsTr("账本"), g: 1, alias: qsTr("消费") + " " + qsTr("花费") }
    ]
    // 0.11.6：⋯ 横条只放 5 颗（计划 ▾、照片 ▾ ｜ 日历、游记、账本），其余进竖列；navPages 仍是全部（头像菜单、Ctrl K 用）
    readonly property var barPages: navPages.filter(p => ["plan", "photo", "calendar", "journal", "ledger"].indexOf(p.key) >= 0)
                                            .map(p => p.g === 0 ? Object.assign({ sub: true }, p) : p)
    readonly property var subPages: [
        { key: "library", name: qsTr("计划库"), owner: "plan" },
        { key: "wish", name: qsTr("想去"), owner: "plan" },
        { key: "foot", name: qsTr("足迹"), owner: "plan" },
        { key: "album", name: qsTr("相册"), owner: "photo" }
    ]
    // 横条 / 竖列里高亮的：计划这一组 = 计划地图、计划库、想去、足迹；照片这一组 = 照片地图、相册
    readonly property var barActive: {
        const p = nav.page, a = []
        if (p === "plan") { a.push("plan"); if (plans.libraryOpen) a.push("library") }
        else if (p === "wish" || p === "foot") a.push("plan", p)
        else if (p === "photo") { a.push("photo"); if (win.albumOpen) a.push("album") }
        else a.push(p)
        if (win.nowOpen) a.push("next")
        return a
    }
    // 保存状态（PO 0.8.8 选改进 c）：只读时一直显示；有改动超过 1.5 秒还没存上才显示（自动保存很快，照 Electron 写法会一闪一闪）
    property bool dirtyLong: false
    // 全窗口共用的液态玻璃背景：地图 + 地球这一层，带 mipmap；分辨率最高 1.25 倍（Electron v6.2.2）
    ShaderEffectSource {
        id: glassSource
        objectName: "glassSource"
        sourceItem: scene
        visible: false
        live: Theme.glass   // 实色时不用背景
        mipmap: true
        smooth: true
        textureSize: {
            const d = Math.min(Screen.devicePixelRatio, 1.25)
            return Qt.size(Math.ceil(scene.width * d), Math.ceil(scene.height * d))
        }
    }
    Binding { target: Theme; property: "glassSource"; value: glassSource }

    Timer { id: dirtyTimer; interval: 1500; onTriggered: win.dirtyLong = app.dirty }
    Connections { target: app; function onDirtyChanged() { if (app.dirty) dirtyTimer.restart(); else { dirtyTimer.stop(); win.dirtyLong = false } } }
    readonly property string saveText: app.readOnly ? qsTr("只读") : win.dirtyLong ? qsTr("● 有未保存的改动") : ""

    TopBar {
        id: topBar
        objectName: "topBar"
        width: parent.width
        z: 25
        page: nav.page
        onMap: win.onMapPage
        // ⋯ 上的名字：当前模式 / 整页（"计划"是默认的，不显示）
        pageLabel: {
            if (nav.page === "plan") return plans.libraryOpen ? qsTr("计划库") : ""
            if (nav.page === "photo" && win.albumOpen) return qsTr("相册")
            if (nav.page === "aiplan") return qsTr("AI 规划")
            const it = win.navPages.find(x => x.key === nav.page); return it ? it.name : ""
        }
        pages: win.barPages
        subPages: win.subPages
        activeKeys: win.barActive
        nowKind: win.nowKind
        nowLabel: win.nowKind === "live" ? qsTr("在路上") : win.nowKind === "back" ? qsTr("刚回来") : qsTr("下次出发")
        nowBadge: win.nowBadge
        todoOpen: plans.now.todoOpen || 0
        nowOpen: win.nowOpen
        listOpen: win.listOpen
        menuOpen: avatarMenu.open
        saveText: win.saveText
        saveWarn: win.dirtyLong
        // Electron：点 ⋯ 里的一颗 = 去那一页；点当前页那一颗 = 回地图
        onPagePicked: (key) => win.pickPage(key)
        onGroupClicked: (key) => win.pickGroup(key)
        onSubPicked: (key) => win.pickSub(key)
        onOpenNow: { if (!win.onMapPage) win.goPage("map"); win.setNowOpen(true) }
        onBackClicked: win.goPage("map")
        onListClicked: win.toggleList()
        onMenuClicked: avatarMenu.open = !avatarMenu.open
        // 0.8.9
        compact: win.w900
        hidePageLabel: !win.wide && !win.fullPageOpen   // 0.12.10：整页不再自己写页名，名字一直留在 ⋯ 上
        overPage: win.fullPageOpen
        onNewClicked: win.newPlan()
        showAi: netc.allowed.ai
        onAiPlanClicked: win.goPage("aiplan")
    }

    // 点 ⋯ 里的一颗（Electron：点一颗 = 去那一页；点当前页那一颗 = 回地图）。0.8.10：点当前模式那一颗 = 回"计划"
    function pickPage(key) {
        if (key === "library" || key === "album") { win.pickSub(key); return }   // 头像菜单的入口
        if (key !== nav.page) win.goPage(key)
        else win.goPage(win.onMapPage ? "plan" : "map")
    }

    // 0.11.6 计划 ▾ / 照片 ▾ 的点法（PO 定）：不在这一组 → 去它的地图；已在这一组 → 展开竖列（地图不动）；竖列开着再点 → 回它的地图并收起
    function inGroup(g) { return g === "plan" ? (nav.page === "plan" || nav.page === "wish" || nav.page === "foot") : nav.page === "photo" }
    function pickGroup(g) {
        if (win.inGroup(g) && topBar.subOpen !== g) { topBar.openSub(g); return }
        if (g === "plan") plans.setLibraryOpen(false); else win.albumOpen = false
        win.goPage(g)
        topBar.setBubbles(false, g)
    }
    // 选竖列里一项：去那里（横条和竖列由 TopBar 一起收起）
    function pickSub(key) {
        if (key === "library") win.openLibrary()
        else if (key === "album") {
            win.goPage("photo"); win.listOpen = true
            Qt.callLater(() => { win.albumOpen = true; photos.album.plan = "" })
        } else win.goPage(key)
    }

    // 头像菜单（S-06 + PO 选的改进 b：Electron 系统菜单里的几项）
    AvatarMenu {
        id: avatarMenu
        objectName: "avatarMenu"
        z: 26
        x: win.width - width - win.gap
        y: win.topH - 4
        // ≤ 1100 时最上面加页面入口（S-15；和 ⋯ 泡泡同一份：只放做好的页面 + 下次出发）
        items: (win.wide ? [] : win.navPages.map(p => ({ key: "page:" + p.key, text: p.name }))
                                .concat([{ key: "page:library", text: qsTr("计划库") }, { key: "page:album", text: qsTr("相册") }])
                                .concat(win.nowKind !== "none" ? [{ key: "now", text: topBar.nowLabel }] : [])
                                .concat([{ sep: true }])).concat([
            { key: "settings", text: qsTr("设置") },
            { key: "importTrip", text: qsTr("导入 .trip") },
            { sep: true },
            { key: "folder", text: qsTr("更换数据文件夹…") },
            { key: "title", text: qsTr("修改窗口标题…") },
            { sep: true },
            { key: "quit", text: qsTr("退出"), kbd: "Ctrl Q" }
        ])
        onPicked: (key) => {
            open = false
            if (key.startsWith("page:")) win.pickPage(key.slice(5))
            else if (key === "now") { if (!win.onMapPage) win.goPage("map"); win.setNowOpen(true) }
            else if (key === "settings") win.goPage("settings")
            else if (key === "importTrip") win.notReady()
            else if (key === "folder") folderDialog.open()
            else if (key === "title") { titleDialog.text = app.windowTitleExtra; titleDialog.open = true }
            else if (key === "quit") app.requestQuit()
        }
    }

    // ---- 右上一列（Electron：顶栏下面 球形 / 平面 → 图层）：图层泡泡（0.8.5，M-02；0.9.2 起地球也在图层里，
    // 单独的地球 / 平面按钮去掉，PO 2026-11-06）、± ----
    // 设置面板、简易面板开着时往左让开
    readonly property real rightColX: win.width - win.gap - (win.settingsOpen ? settings.width + win.gap
                                                        : win.tripOpen ? planPanel.width + win.gap
                                                        : win.drawerMode ? 0
                                                        : win.editorShown ? planEditor.width + win.gap
                                                        : win.sheetOpen ? planSheet.targetW + win.gap : 0)
    LayerBubbles {
        id: layers
        objectName: "layers"
        x: win.rightColX - width
        y: win.topH + 2
        z: 15
        visible: (!win.tripOpen || win.tripRoom) && !win.albumOpen   // 0.11.1 相册铺满时不留地图按钮
        bases: win.bases
        current: win.layerKey
        dark: win.globeMode || mapc.base === "sat"   // 地球上背景是星空，用烟色玻璃
        onPicked: (key) => win.pickBase(key)
        Behavior on x { TpMove {} }
    }
    // ± 缩放（0.8.9，M-30；PO 选 A 照 Electron：右上一列 图层 → ±）。以地图中心缩放一级
    Item {
        id: zoomBtns
        objectName: "zoomBtns"
        width: layers.btnSize
        height: Theme.px(76)
        x: win.rightColX - width
        y: layers.y + layers.btnSize + 10
        z: 15
        visible: (win.mapShown && !win.albumOpen) || (win.tripOpen && win.tripRoom)   // 0.9.0：地球上也有 ±（Electron 球形时也有）；0.9.5 起 mapShown 含地球
        Behavior on x { TpMove {} }
        GlassPanel {
            anchors.fill: parent
            radius: width / 2
            tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.69 : 0.96)
        }
        Column {
            anchors.fill: parent
            Repeater {
                model: [{ k: "plus", d: 1, t: qsTr("放大") }, { k: "minus", d: -1, t: qsTr("缩小") }]
                delegate: Item {
                    required property var modelData
                    objectName: modelData.d > 0 ? "zoomIn" : "zoomOut"
                    width: zoomBtns.width; height: zoomBtns.height / 2
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.t
                    TpTip { text: parent.modelData.t; show: zMa.containsMouse }
                    TpIcon { anchors.centerIn: parent; name: parent.modelData.k; color: zMa.containsMouse ? Theme.accent : Theme.text }
                    MouseArea { id: zMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.globeMode ? globe.wheel(parent.modelData.d * 240, globe.width / 2, globe.height / 2) : mapc.zoomBy(parent.modelData.d, win.width / 2, win.height / 2, 300) }
                }
            }
        }
        Rectangle { anchors.centerIn: parent; width: parent.width * 0.5; height: 1; color: Theme.line }
    }

    // ---- Ctrl K 命令面板（0.8.8，S-12） ----
    CommandPalette {
        id: palette
        objectName: "palette"
        anchors.fill: parent
        z: 60
        recent: plans.recent
        source: win.paletteItems
        onRun: (item) => win.runCommand(item)
    }
    function openPalette(q) {
        topBar.setBubbles(false)
        avatarMenu.open = false
        layers.setOpen(false)
        filterMenu.open = false
        palette.show(q)
    }
    // 0.12.3 占位入口（AI、导入导出）统一走这里：外观和真按钮一样，点了提示还在做
    function notReady() { toast.show(qsTr("这个功能还在做"), false) }
    // 0.12.9 删除后撤销：fn 真的删了才提示（返回 false = 没删）。前后各存一步，撤销只退这一下；
    // 删完又改过，按钮不能再退（退的会是后面的改动），提示用 Ctrl+Z
    function deleteWithUndo(fn, msg) {
        app.commitStep()
        if (fn() === false) return false
        app.commitStep()
        const mark = app.undoMark()
        toast.show(msg || qsTr("已删除"), false, qsTr("撤销"), () => {
            if (app.undoMark() === mark) app.undo()
            else toast.show(qsTr("删除之后又改过了，请按 Ctrl+Z 一步步撤销"), false)
        })
        return true
    }
    function deleteExpenseRow(r) { return deleteWithUndo(() => planPage.deleteExpense(r.id)) }
    function deleteBookingRow(r) {
        let n = -1
        return deleteWithUndo(() => { n = planPage.deleteBooking(r.id); return n >= 0 },
                              r.linked ? qsTr("已删除，“消费”里对应的那一笔也删了") : "")
    }
    function showRowMenu(x, y, list) { rowMenu.show(x, y, list) }
    function md(d) { return d ? String(d).slice(5).replace("-", ".") : "" }   // palette.js md
    function statusLabel(s) { return s === "wish" ? qsTr("想去") : s === "completed" ? qsTr("已完成") : qsTr("计划中") }
    // 条目（palette.js source()）。页面只有做好的；操作补上原来系统菜单里的命令（PO 0.8.8 选方案 B），带快捷键提示
    function paletteItems() {
        const out = []
        const page = (key, label) => out.push({ kind: "page", key: key, label: label, sub: qsTr("页面") })
        // 0.8.10：地图的 4 种模式（带 Ctrl 1–4，PO 选改进 b）+ 3 个整页；搜旧名字（计划库、愿望清单、相册）也找得到
        win.navPages.forEach((p, i) => out.push({ kind: "page", key: p.key, label: p.name, alias: p.alias || "",
                                                   sub: p.g === 0 ? qsTr("地图模式") : qsTr("页面"), kbd: p.g === 0 ? "Ctrl " + (i + 1) : "" }))
        page("settings", qsTr("设置"))
        const act = (key, label, sub, kbd) => out.push({ kind: "act", key: key, label: label, sub: sub || qsTr("操作"), kbd: kbd || "" })
        act("new", qsTr("新建计划"))   // 0.8.9（Electron palette.js：新建计划）
        act("undo", qsTr("撤销"), "", "Ctrl Z")
        act("redo", qsTr("重做"), "", "Ctrl Y")
        for (const b of win.bases) if (b.key !== "globe") out.push({ kind: "act", key: "base:" + b.key, label: qsTr("底图：") + b.name, sub: qsTr("底图"), kbd: "" })
        act("globe", win.globeMode ? qsTr("切到平面地图") : qsTr("切到地球"))
        act("list", qsTr("我的旅程"), "", "Ctrl B")
        if (plans.hiddenCount > 0) act("unhide", qsTr("显示全部隐藏的计划"))
        if (plans.excludedCount > 0) act("filterAll", qsTr("清除筛选（全选）"))
        // 0.9.7：叠上其他旅程（左下胶囊去重复；Claude 加的入口，PO 可去掉）
        if (plans.overlayAvailable) act("overlay", plans.overlay ? qsTr("不叠其他旅程") : qsTr("叠上其他旅程"))
        if (netc.allowed.ai) act("ai", qsTr("AI 助手"))   // 0.13.0
        if (netc.allowed.ai) page("aiplan", qsTr("让 AI 帮我规划"))   // 0.13.2 整页向导
        for (const t of [qsTr("导入 .trip"), qsTr("导出行程单 PDF"), qsTr("导出行程单长图"), qsTr("导出日历文件 .ics"), qsTr("导出计划文件 .trip"), qsTr("导出分享版"), qsTr("导出信息卡"), qsTr("批量导出")])
            act("notReady", t)
        act("fullscreen", qsTr("全屏"), "", "F11")
        act("dataFolder", qsTr("打开数据文件夹"))
        act("logFolder", qsTr("打开日志文件夹"), "", "F12")
        act("quit", qsTr("退出"), "", "Ctrl Q")
        for (const x of plans.commandItems()) {
            if (x.kind === "plan") {
                const dated = x.startDate && x.endDate
                out.push({ kind: "plan", id: x.id, label: x.label || qsTr("未命名计划"), color: x.color,
                           sub: win.statusLabel(x.status) + (dated ? " · " + win.md(x.startDate) + " — " + win.md(x.endDate) : "") })
            } else {
                out.push({ kind: "stop", id: x.id, stop: x.stop, label: x.label, color: x.color,
                           sub: qsTr("第 %1 站 · %2").arg(x.stop + 1).arg(x.planName || "") })
            }
        }
        return out
    }
    function runCommand(x) {
        if (x.kind === "page") { win.goPage(x.key); return }
        if (x.kind === "plan") { win.showPlanOnMap(x.id); return }
        if (x.kind === "stop") {
            if (editor.open) { toast.show(qsTr("请先完成或放弃当前编辑"), false); return }
            win.nowOpen = false
            if (!win.onMapPage) win.goPage("map")
            win.revealToast(plans.openPlan(x.id, false, x.stop))   // 0.9.5：在地球上也行（地球转过去）   // Electron：selectPlan(id, false, i) + fitStops([s])（聚焦这一站并平移过去）
            return
        }
        const k = x.key
        if (k === "notReady") win.notReady()
        else if (k === "ai") { if (!win.onMapPage) win.goPage("map"); aiPanel.open = true }
        else if (k === "new") win.newPlan()
        else if (k === "undo") app.undo()
        else if (k === "redo") app.redo()
        else if (k.startsWith("base:")) { if (!win.onMapPage) win.goPage("map"); win.pickBase(k.slice(5)) }
        else if (k === "globe") { if (!win.onMapPage) win.goPage("map"); win.pickBase(win.globeMode ? mapc.base : "globe") }
        else if (k === "list") win.toggleList()
        else if (k === "unhide") plans.showAllHidden()
        else if (k === "filterAll") plans.filterAll()
        else if (k === "overlay") { if (!win.onMapPage) win.goPage("map"); plans.toggleOverlay() }
        else if (k === "fullscreen") win.toggleFullscreen()
        else if (k === "dataFolder") app.openDataFolder()
        else if (k === "logFolder") app.openLogFolder()
        else if (k === "quit") app.requestQuit()
    }

    // 点别处收回（Electron document click）：C++ 在窗口上装了事件过滤器，按下鼠标时通知、不拦截（地图照样能拖、能点）；
    // 点在按钮和泡泡上不算。（0.8.5 第一版用一个盖在最上面的 TapHandler，结果把泡泡自己的点击挡住了）
    Connections {
        target: app
        function onPointerPressed(x, y) {
            if (layers.open && !layers.containsGlobal(x, y)) layers.setOpen(false)
            // 0.8.7：筛选下拉、"此刻"卡片（Electron：点在卡片、入口、弹框外面就收起）
            if (filterMenu.open && !filterMenu.containsGlobal(x, y) && !statusPill.containsGlobal(x, y) && !planList.filterButtonContains(x, y)) filterMenu.open = false
            if (win.nowOpen && !nowCard.containsGlobal(x, y) && !topBar.nextBudContains(x, y) && !win.modalOpen && !palette.open) win.nowOpen = false
            // 0.8.8：⋯ 泡泡、头像菜单
            if (topBar.subIsOpen && !topBar.bubblesContain(x, y)) topBar.closeSub()   // 先收竖列，再点一次收横条
            else if (topBar.bubblesOpen && !topBar.bubblesContain(x, y)) topBar.setBubbles(false)
            if (avatarMenu.open && !avatarMenu.containsGlobal(x, y) && !topBar.menuButtonContains(x, y)) avatarMenu.open = false
        }
    }

    // ---- 0.8.7：此刻、筛选、简易计划面板 ----
    readonly property string nowKind: plans.now.kind || "none"
    property bool nowOpen: false
    onNowKindChanged: if (nowKind === "none") nowOpen = false
    // "此刻"卡片和"我的旅程"列表都在左边：一个打开时另一个收起（Electron syncList / setNowOpen）
    // 0.11.0 去计划库 = 计划模式 + 列表展开
    function openDocs() {   // 0.12.4 此刻卡"看证件"、计划页"管理证件"
        win.nowOpen = false
        settings.showSection("docs")
        win.goPage("settings")
    }
    function openLibrary() {
        win.goPage("plan")
        win.listOpen = true
        Qt.callLater(() => plans.setLibraryOpen(true))
    }
    function setNowOpen(v) { win.nowOpen = v; if (v) win.listOpen = false }
    property bool listOpen: false
    onListOpenChanged: { if (listOpen) win.nowOpen = false; else { plans.setLibraryOpen(false); win.albumOpen = false } }
    // 0.11.1 相册 = 照片模式的列表铺满（整页）。P1：窄列表点一趟，地图飞到这趟的范围；铺满时不飞
    property bool albumOpen: false
    readonly property bool photoMode: plans.mode === "photo"
    Binding { target: photos.album; property: "active"; value: win.photoMode }
    Binding { target: mapc.photoLayer; property: "active"; value: win.photoMode && !win.albumOpen }   // 铺满时地图被盖住，标记不用摆
    Connections {
        target: plans
        function onModeChanged() {
            if (plans.mode !== "photo") { win.albumOpen = false; return }
            win.fitPhotosOnce()
        }
    }
    Connections { target: mapc.photoLayer; function onInfoChanged() { win.fitPhotosOnce() } }
    property bool photosFramed: false
    function fitPhotosOnce() {   // Electron：第一次进来缩放到全部照片
        if (win.photosFramed || !win.photoMode) return
        const pts = mapc.photoLayer.pointsOf("")
        if (pts.length === 0) return
        win.photosFramed = true
        mapc.fitPoints(pts, 10, Theme.motion ? 800 : 0)
    }
    function flyToPhotos(planId) {
        if (win.albumOpen) return
        const pts = mapc.photoLayer.pointsOf(planId)
        if (pts.length > 0) mapc.fitPoints(pts, 12, Theme.motion ? 800 : 0)
    }
    function openTripPhotos(id) {   // 此刻卡"整理照片"：照片模式、展开这一趟、地图飞过去
        win.goPage("photo")
        win.listOpen = true
        photos.album.plan = id
        win.photosFramed = true
        Qt.callLater(() => win.flyToPhotos(id))
    }
    function showMapPhotos(ids) { const l = photos.album.itemsFor(ids); if (l.length > 0) lightbox.show(l, 0) }
    Connections { target: win.map; ignoreUnknownSignals: true; function onPhotoOpened(ids) { win.showMapPhotos(ids) } }
    // Ctrl B / 顶栏列表按钮：不在地图页时先回地图（Electron mfList.onclick）
    function toggleList() {
        if (!win.onMapPage) { win.goPage("map"); win.listOpen = true; return }
        win.listOpen = !win.listOpen
    }
    readonly property string nowBadge: {
        const n = plans.now
        if (n.kind === "live") return qsTr("第 %1 / %2 天").arg(n.dayN).arg(n.dayTotal)
        if (n.kind === "back") return qsTr("%1 天前回来").arg(n.daysAgo)
        if (n.kind === "next" && n.daysUntil >= 0) return n.daysUntil === 0 ? qsTr("今天") : n.daysUntil === 1 ? qsTr("明天") : qsTr("%1 天后").arg(n.daysUntil)
        return ""
    }
    function inItem(item, gx, gy) {
        if (!item || !item.visible) return false
        const p = item.mapFromItem(null, gx, gy)
        return p.x >= 0 && p.y >= 0 && p.x <= item.width && p.y <= item.height
    }
    // 0.9.5：地球上也算（我的旅程、状态胶囊、简易计划面板、此刻卡片、图例、比例尺、我的位置在地球上也显示，Electron 球形时都在）
    readonly property bool mapShown: win.onMapPage && (win.globeMode || win.map !== null)
    readonly property bool sheetOpen: win.mapShown && mapc.markers.selectedPlan !== "" && (plans.sheet.id || "") !== ""

    // 筛选提示：定位一个被筛掉的计划时自动打开了哪些选项（Electron revealPlan 的提示）
    function revealToast(r) {
        if (r && r.mode) {
            // 0.8.10：足迹模式里定位计划中 / 想去的计划：切到它的模式（C++ 已经换了模式，这里跟上 nav；0.9.2 起底图不跟着换）
            const m = r.mode
            if (nav.page !== m) nav.go(m)
            toast.show(qsTr("已切到「%1」").arg(win.modeName(m)), false)
            return
        }
        if (!r || (!(r.opened && r.opened.length) && !r.unhid)) return
        const names = (r.opened || []).map(k => k === "planning" ? qsTr("计划中") : k === "wish" ? qsTr("想去") : k === "completed" ? qsTr("已完成")
                                                : k === "single" ? qsTr("单一目的地") : k === "route" ? qsTr("路线") : qsTr(k))
        if (names.length) toast.show(qsTr("已打开「%1」筛选").arg(names.join(qsTr("、"))) + (r.unhid ? qsTr("，并取消隐藏这条路线") : ""), false)
        else toast.show(qsTr("已取消隐藏这条路线"), false)
    }
    function showPlanOnMap(id) {
        if (editor.open) { toast.show(qsTr("请先完成或放弃当前编辑"), false); return }
        win.nowOpen = false
        if (!win.onMapPage) win.goPage("map")
        win.revealToast(plans.openPlan(id, true))
    }

    Connections {
        target: weather
    }
    NowCard {
        id: nowCard
        objectName: "nowCard"
        visible: win.nowOpen && win.nowKind !== "none" && win.mapShown
        x: win.gap
        y: win.topH - 6
        z: 14
        maxHeight: win.height - y - (statusPill.visible ? win.height - statusPill.y + 12 : 20)
        width: Math.min(Theme.px(360), win.width - 28)
        info: plans.now
        onCloseRequested: win.nowOpen = false
        onShowOnMap: (id) => win.showPlanOnMap(id)
        onOpenTrip: (id, stop) => win.openTrip(id, stop)
        onQuickExpense: (id) => { win.tripTab = "quick"; win.openTrip(id, -1) }
        onOpenPacking: (id) => { win.tripTab = "packing"; win.openTrip(id, -1) }
        onUnsetNext: { win.nowOpen = false; plans.setNextTrip(plans.now.planId, false) }
        onOpenLibrary: win.openLibrary()
        onOpenDocs: win.openDocs()
        onOpenPhotos: (id) => win.openTripPhotos(id)
        onOpenJournal: (id) => { win.tripTab = "journal"; win.tripTabPlan = id; win.openTrip(id, -1) }
        onMarkDone: (id, name) => { planConfirm.ask("done", id, name) }
    }

    // ---- 0.8.8：我的旅程（S-08）。左边、顶栏下面；打开时左下的足迹框、状态胶囊挪到它右边（Electron v5.9.1） ----
    readonly property real leftX: planList.open ? planList.x + planList.width + win.gap : win.gap
    PlanListPanel {
        id: planList
        objectName: "planList"
        x: win.gap
        y: win.topH - 6
        z: 14
        // 0.11.0 展开 = 计划库：占窗口约 60%，右边留地图（PO 第 1 题）；窄窗口只给右边的图层和缩放按钮留一条
        // 0.11.1 相册铺满整个窗口（PO：照片模式展开成整页）
        width: targetWidth
        targetWidth: win.albumOpen ? win.width - 2 * win.gap : expanded ? (win.width < Theme.px(900) ? win.width - 28 - Theme.px(56) : Math.round(win.width * 0.6)) : Math.min(Theme.px(320), win.width - 28)
        albumOpen: win.albumOpen
        Behavior on width { enabled: Theme.motion; TpMove { ms: 220 } }
        height: win.height - y - 20
        open: win.listOpen && win.mapShown
        info: plans.list
        // 0.8.10：标题跟着模式（PO 选 ② B）
        title: plans.mode === "foot" ? qsTr("去过的") : plans.mode === "wish" ? qsTr("想去") : plans.mode === "photo" ? (win.albumOpen ? qsTr("相册") : qsTr("照片"))
             : plans.libraryOpen ? qsTr("计划库") : qsTr("计划")
        selectedId: mapc.markers.selectedPlan
        nextBadge: win.nowBadge
        filterOpen: filterMenu.open && win.filterFrom === "list"
        onCloseRequested: win.listOpen = false
        onFilterClicked: { const again = win.filterFrom === "list"; win.filterFrom = "list"; filterMenu.open = again ? !filterMenu.open : true }
        onPlanClicked: (id) => { if (editor.open) { toast.show(qsTr("请先完成或放弃当前编辑"), false); return } win.revealToast(plans.openPlan(id, true)) }
        onEyeClicked: (id, hidden) => plans.setHidden(id, hidden)
        onLibraryOpenPlan: (id) => win.openTrip(id, -1)
        onNewWishClicked: win.newWish()
        onNewPlanClicked: win.newPlan()
        onNewFootClicked: win.newFoot()
        onLibraryDelete: (ids) => {
            if (editor.open) { toast.show(qsTr("请先完成或放弃当前编辑"), false); return }
            bulkConfirm.ask(ids)
        }
        onWishStart: (id) => wishDialog.showStart(id)
        onWishSeason: (id) => wishDialog.showSeason(id)
        onExpandedChanged: { mapc.markers.hoverFromList(""); if (expanded) mapc.markers.clearSelection() }
        onAlbumExpand: (on) => { win.albumOpen = on; photos.album.plan = "" }
        onAlbumOpenPlan: (id) => { win.tripTab = "photos"; win.openTrip(id, -1) }
        onAlbumImport: photoFileDialog.open()
        onAlbumShow: (l, i) => lightbox.show(l, i)
        onAlbumAssign: (ids) => { if (!photoAssign.ask(ids)) toast.show(qsTr("还没有计划"), false) }
        onAlbumFly: (id) => win.flyToPhotos(id)
    }
    // 0.11.0 想去：底部月份条（H-03），在左下胶囊右边、放不下时在它上面；开始计划、关联季节
    WishMonthBar {
        id: monthBar
        objectName: "wishMonthBar"
        visible: win.mapShown && plans.mode === "wish" && !win.sheetOpen && !win.editorShown
        z: 12
        readonly property real leftEdge: statusPill.x + statusPill.width + win.gap
        readonly property bool below: win.width - leftEdge - win.gap >= 12 * Theme.px(30)
        x: below ? leftEdge : win.leftX
        width: Math.min(implicitWidth, win.width - x - win.gap - (locBtn.visible && below ? locBtn.width + 10 : 0))
        height: implicitHeight
        y: below ? win.height - height - 20 : statusPill.y - height - 10
    }
    WishDialog {
        id: wishDialog
        objectName: "wishDialog"
        onStarted: (name) => toast.show(qsTr("「%1」已改成计划中").arg(name), false, qsTr("撤销"), () => app.undo())
    }
    PhotoAssignDialog {
        id: photoAssign
        objectName: "photoAssign"
        onAssigned: (n, plan, stop) => toast.show(Fmt.one(qsTr("%1 张已归到「%2」· %3（照片文件留在原位置，不用挪）").arg(n).arg(plan).arg(stop)), false, qsTr("撤销"), () => app.undo())
    }
    // 0.11.0 批量删除（L-08：列前 8 个名字，照片不删）
    MessageDialog {
        id: bulkConfirm
        objectName: "bulkConfirm"
        property var ids: []
        function ask(list) {
            ids = list
            const names = []
            for (const id of list) {
                const b = plans.planBasics(id)
                names.push("· " + (b.name || qsTr("未命名计划")).replace(/&/g, "&amp;").replace(/</g, "&lt;"))
            }
            title = qsTr("删除 %1 个计划").arg(list.length)
            paragraphs = [names.slice(0, 8).join("<br>") + (names.length > 8 ? "<br>" + qsTr("…… 共 %1 个计划").arg(names.length) : ""),
                          qsTr("删除后可以按 Ctrl+Z 撤销。计划里的照片文件不会删除，还在照片库里。")]
            open = true
        }
        buttons: [{ key: "cancel", text: qsTr("取消") }, { key: "ok", text: qsTr("删除"), kind: "danger" }]
        onChosen: (key) => {
            open = false
            if (key !== "ok") return
            const n = ids.length
            if (plans.deletePlans(ids)) {
                planList.leaveLibrarySelection()
                toast.show(qsTr("已删除 %1 个计划").arg(n), false, qsTr("撤销"), () => app.undo())
            }
        }
    }

    StatusFilterPill {
        id: statusPill
        objectName: "statusPill"
        visible: win.mapShown && !win.sheetOpen && !win.editorShown && !plans.libraryOpen && !win.photoMode   // 0.11.0 计划库有自己的筛选；0.11.1 照片模式没有可筛的
        x: win.leftX
        Behavior on x { enabled: Theme.motion; TpMove {} }

        y: win.height - height - 20
        z: 13
        menuOpen: filterMenu.open && win.filterFrom === "pill"
        onMenuClicked: { const again = win.filterFrom === "pill"; win.filterFrom = "pill"; filterMenu.open = again ? !filterMenu.open : true }
        function containsGlobal(gx, gy) { return win.inItem(statusPill, gx, gy) }
    }
    // 筛选下拉：从左下状态胶囊打开时在胶囊上面；从"我的旅程"列表头打开时在筛选按钮下面（同一个下拉、同一份状态）
    property string filterFrom: "pill"
    FilterMenu {
        id: filterMenu
        objectName: "filterMenu"
        readonly property point listAnchor: {
            planList.x; planList.y; planList.width; win.width; open
            const b = planList.filterButton
            return b ? b.mapToItem(null, b.width, b.height) : Qt.point(0, 0)
        }
        x: win.filterFrom === "list" ? Math.min(win.width - width - 14, Math.max(14, listAnchor.x - width + Theme.px(40))) : win.leftX
        y: win.filterFrom === "list" ? listAnchor.y + 8 : statusPill.y - height - 10
        z: 16
    }
    Connections { target: statusPill; function onVisibleChanged() { if (!statusPill.visible && win.filterFrom === "pill") filterMenu.open = false } }
    Connections { target: planList; function onOpenChanged() { if (!planList.open && win.filterFrom === "list") filterMenu.open = false } }


    PlanSheet {
        id: planSheet
        objectName: "planSheet"
        readonly property real targetW: win.drawerMode ? win.width : Math.min(Theme.px(400), win.width - 2 * win.gap)
        width: targetW
        // 窄窗口（≤ 768）变成底部抽屉（PO 0.8.9 选方案 B）
        y: win.drawerMode ? (win.sheetOpen ? win.height - win.drawerH : win.height + 20) : win.topH - 6
        height: win.drawerMode ? win.drawerH + 20 : win.height - y - win.gap
        x: win.drawerMode ? 0 : (win.sheetOpen ? win.width - width - win.gap : win.width + 20)
        visible: win.drawerMode ? y < win.height : x < win.width
        Behavior on y { enabled: Theme.motion && !win.drawerDragging; TpAnim { shown: win.sheetOpen } }
        Behavior on x { enabled: Theme.motion; TpAnim { shown: win.sheetOpen } }
        z: 14
        source: plans.sheet
        focusIndex: mapc.markers.focusStop
        onCloseRequested: mapc.markers.clearSelection()
        // ◎ 和"全部"：回到整条路线——取消聚焦（面板回到概览）、框住整个计划（让开面板）
        onLocate: (id) => { mapc.markers.selectPlan(id, -1, false); mapc.flyToPlan(id) }
        onOverview: (id) => { mapc.markers.selectPlan(id, -1, false); mapc.flyToPlan(id) }
        onPickStop: (i) => mapc.markers.selectPlan(plans.sheet.id, i, true)
        onTogglePin: (id) => { if (plans.togglePin(id)) toast.show(plans.sheet.pinned ? qsTr("已置顶") : qsTr("已取消置顶"), false) }
        onToggleNext: (id, on) => { if (plans.setNextTrip(id, on)) toast.show(on ? qsTr("已设为下次出发") : qsTr("已取消下次出发"), false) }
        onDeleteRequested: (id, name) => planConfirm.ask("delete", id, name)
        onEditRequested: (id) => win.editPlan(id)
        onFullRequested: (id) => win.openTrip(id, mapc.markers.focusStop)
        onPhotoViewRequested: (l, i) => lightbox.show(l, i)
    }
    // 抽屉的把手（简易面板；编辑器自己带）：拖动改高矮，双击在一半 / 大半之间切换
    DrawerGrip {
        visible: win.drawerMode && win.sheetOpen
        x: 0; y: planSheet.y; width: win.width
        z: 15
        onDragged: (dy, done) => win.drawerDrag(dy, done)
        onToggled: win.drawerToggle()
    }

    // ---- 0.8.9 站点编辑器（M-23；PO 选方案 B）：右边（宽窗口）或底部抽屉（≤ 768） ----
    readonly property bool editorShown: editor.open && win.mapShown
    PlanEditor {
        id: planEditor
        objectName: "planEditor"
        drawer: win.drawerMode
        z: 14
        width: win.drawerMode ? win.width : Math.min(Theme.px(470), win.width - 2 * win.gap)   // Electron --editor-w: 470px
        x: win.drawerMode ? 0 : (win.editorShown ? win.width - width - win.gap : win.width + 20)
        y: win.drawerMode ? (win.editorShown ? win.height - win.drawerH : win.height + 20) : win.topH - 6
        height: win.drawerMode ? win.drawerH : win.height - y - win.gap
        visible: win.drawerMode ? y < win.height : x < win.width
        Behavior on x { enabled: Theme.motion; TpAnim { shown: win.editorShown } }
        Behavior on y { enabled: Theme.motion && !win.drawerDragging; TpAnim { shown: win.editorShown } }
        onFinishRequested: win.finishEditor()
        onDiscardRequested: editorDialog.ask("discard", -1, "")
        onDeletePlanRequested: editorDialog.ask("deletePlan", -1, editor.plan.name || "")
        onDeleteStopRequested: (i, name) => editorDialog.ask("deleteStop", i, name)
        onStyleRequested: (anchor, opts) => stylePop.openAt(anchor, opts)
        onDrawerDrag: (dy, done) => win.drawerDrag(dy, done)
        onDrawerToggle: win.drawerToggle()
        onCoverPickRequested: (rows) => { if (rows.length > 0) coverPickDialog.show(rows); else win.addPhotos(editor.planId, "", true) }
        onPhotoAddRequested: (stopId) => win.addPhotos(editor.planId, stopId, false)
        onPhotoCaptionRequested: (ph) => photoCaptionDialog.show(ph)
        onPhotoRemoveRequested: (ph) => photoRemoveDialog.ask(editor.planId, ph)
        onPhotoViewRequested: (l, i) => lightbox.show(l, i)
    }
    StylePopover { id: stylePop; objectName: "stylePop" }
    RowMenu { id: rowMenu }

    // ---- 抽屉的高度：一半 / 大半 / 只露标题三档，拖动松手时吸到最近的一档 ----
    property real drawerFrac: 0.5
    property real drawerDy: 0
    property bool drawerDragging: false
    readonly property real drawerAvail: win.height - win.topH
    readonly property real drawerMin: Theme.px(76)
    readonly property real drawerH: Math.max(drawerMin, Math.min(drawerAvail - 6, drawerAvail * drawerFrac - drawerDy))
    function drawerDrag(dy, done) {
        if (!done) { win.drawerDragging = true; win.drawerDy = dy; return }
        const h = Math.max(drawerMin, Math.min(drawerAvail - 6, drawerAvail * drawerFrac - dy))
        const snaps = [drawerMin / drawerAvail, 0.5, 0.88]
        let best = snaps[0]
        for (const f of snaps) if (Math.abs(f * drawerAvail - h) < Math.abs(best * drawerAvail - h)) best = f
        win.drawerFrac = best
        win.drawerDy = 0
        win.drawerDragging = false
        win.updateInsets()
    }
    function drawerToggle() { win.drawerFrac = win.drawerFrac >= 0.8 ? 0.5 : 0.88; win.updateInsets() }

    // ---- 打开 / 关闭编辑器 ----
    function newPlan() {
        if (editor.open) { toast.show(qsTr("请先完成或放弃当前编辑"), false); return }
        if (!win.onMapPage) win.goPage("map")
        win.nowOpen = false
        const r = editor.openNew()
        if (r !== "") { toast.show(r, true); return }
        win.leaveGlobeForEditor(null)
    }
    // 0.11.0 H-04 新建想去（编辑器开着时 newPlan 只提示，不能改开着的那个）
    function newWish() {
        const was = editor.open
        win.newPlan()
        if (!was && editor.open) editor.setField("status", "wish")
    }
    // 0.12.7 这个模式一个计划都没有：把列表打开，空状态才看得见（有计划时不动，保留用户收起的状态）
    function openListIfEmpty() {
        if (win.modeKeys.indexOf(nav.page) >= 0 && !win.photoMode && (planList.info.rows || []).length === 0 && planList.modeCount === 0) win.listOpen = true
    }
    // 0.12.7 足迹的空状态：记一趟走过的旅程（状态直接是已完成）
    function newFoot() {
        const was = editor.open
        win.newPlan()
        if (!was && editor.open) editor.setField("status", "completed")
    }
    // 0.9.7（B4）：从地球打开编辑器：记下地球的镜头，原地摊平（编辑在平面上做），关掉后卷回地球
    function leaveGlobeForEditor(then) {
        if (!win.globeMode) { if (then) then(); return }
        win.editFromGlobe = { lat: globe.centerLat, lng: globe.centerLng, zoom: globe.zoom }
        win.flatten(globe.centerLat, globe.centerLng, globe.zoom, then)
    }
    function editPlan(id) {
        if (!win.onMapPage) win.goPage("map")
        win.nowOpen = false
        const r = editor.openPlan(id)
        if (r !== "") { toast.show(r, true); return }
        if (win.globeMode) win.leaveGlobeForEditor(() => { win.updateInsets(); editor.fitPlan() })
        else Qt.callLater(() => { win.updateInsets(); editor.fitPlan() })
    }
    function finishEditor() {
        const r = editor.finish()
        if (r.ok) return
        if (r.ask === "single") editorDialog.ask("single", -1, "")
        else editorDialog.ask("invalid", -1, r.reason || "")
    }
    Connections {
        target: editor
        function onToast(text, warn) { toast.show(text, warn) }
        // Electron 保存后：选中、打开详情、飞过去。0.9.7：从地球打开的编辑器不在平面上飞，卷回地球时对着它（returnAfterEdit）
        function onFinished(id) {
            if (win.editFromGlobe) { win.finishedId = id; win.revealToast(plans.openPlan(id, false)) }
            else win.revealToast(plans.openPlan(id, true))
        }
        function onOpenChanged() { win.updateInsets(); if (!editor.open) stylePop.close() }
    }
    Connections {
        target: locator
        function onToast(text, warn, ms) { toast.show(text, warn) }
    }
    // 编辑器的几个确认（Electron askConfirm）
    MessageDialog {
        id: editorDialog
        objectName: "editorDialog"
        property string kind: ""
        property int index: -1
        property string name: ""
        function ask(k, i, n) { kind = k; index = i; name = n; open = true }
        title: kind === "deleteStop" ? qsTr("删除站点") : kind === "deletePlan" ? qsTr("删除计划") : kind === "single" ? qsTr("类型确认")
             : kind === "discard" ? qsTr("放弃这次的修改") : qsTr("还不能保存")
        paragraphs: kind === "deleteStop" ? [qsTr("确定删除「%1」吗？").arg(name || qsTr("未命名站点"))]
                  : kind === "deletePlan" ? [qsTr("确定删除「%1」吗？").arg(name || qsTr("未命名计划")), qsTr("删除后可以按 Ctrl+Z 撤销。计划里的照片文件不会删除，还在照片库里。")]
                  : kind === "single" ? [qsTr("这个计划有多个站点，但类型是「单一目的地」，地图上会画成多个独立标记而不是路线。要改成「路线」吗？")]
                  : kind === "discard" ? [qsTr("恢复成打开编辑器时的样子吗？这次改的都会丢掉（新建的计划会删掉）。")]
                  : [name, qsTr("现在关闭会放弃这次的修改。")]
        buttons: kind === "deleteStop" || kind === "deletePlan" ? [{ key: "cancel", text: qsTr("取消") }, { key: "ok", text: qsTr("删除"), kind: "danger" }]
               : kind === "single" ? [{ key: "cancel", text: qsTr("保持单一目的地") }, { key: "ok", text: qsTr("改成路线"), kind: "primary" }]
               : kind === "discard" ? [{ key: "cancel", text: qsTr("继续编辑") }, { key: "ok", text: qsTr("放弃修改"), kind: "danger" }]
               : [{ key: "cancel", text: qsTr("继续编辑") }, { key: "ok", text: qsTr("放弃修改"), kind: "danger" }]
        onChosen: (key) => {
            open = false
            if (kind === "single") {
                if (key === "ok") editor.setField("type", "route"); else editor.keepSingle()
                win.finishEditor()
                return
            }
            if (key !== "ok") return
            if (kind === "deleteStop") editor.removeStop(index)
            else if (kind === "deletePlan") { const n = editor.deletePlan(); toast.show(qsTr("已删除「%1」").arg(n || qsTr("未命名计划")), false, qsTr("撤销"), () => app.undo()) }
            else editor.discard()
        }
    }

    // 浮在地图上的东西挡住的边距（S-05）：飞过去、框住、平移站点时让开
    function updateInsets() {
        if (win.tripOpen) {
            mapc.setInsets(0, win.topH, win.tripRoom ? planPanel.width + win.gap : 0, 0)
            return
        }
        const side = !win.drawerMode && (win.sheetOpen || win.editorShown)
        const drawer = win.drawerMode && (win.sheetOpen || win.editorShown)
        mapc.setInsets(Math.max(nowCard.visible && win.width > 1100 ? nowCard.x + nowCard.width : 0,
                                planList.open ? planList.x + planList.width : 0),
                       win.topH,
                       side ? (win.editorShown ? planEditor.width : planSheet.targetW) + win.gap : 0,
                       drawer ? win.drawerH : statusPill.visible ? win.height - statusPill.y : 0)
    }
    onEditorShownChanged: updateInsets()
    onDrawerModeChanged: updateInsets()
    onSheetOpenChanged: updateInsets()
    onWidthChanged: updateInsets()
    onHeightChanged: updateInsets()
    Connections { target: nowCard; function onVisibleChanged() { win.updateInsets() } function onWidthChanged() { win.updateInsets() } }
    Connections { target: statusPill; function onVisibleChanged() { win.updateInsets() } }
    Connections { target: planList; function onOpenChanged() { win.updateInsets() } function onWidthChanged() { win.updateInsets() } }

    // 启动时框住正在进行或下一次的旅行（Electron mapfirst.js start：400 ms 后，maxZoom 5.5）
    Timer {
        id: frameNextTimer
        interval: 400
        onTriggered: {
            win.updateInsets()
            const n = plans.now
            const framed = (n.kind === "live" || n.kind === "next") && win.mapShown && !mapc.markers.selectedPlan && mapc.frameTrip(n.planId)
            // 0.9.2：上次图层选的是地球：框好以后进地球（地球中心对准平面的中心）。
            // 0.9.5：没有要框的行程时，进地球后照 Electron fitAll 的球形公式框住所有站点（分布广时整个地球放进可见区域）
            // 0.9.7：框好的行程超过 5.5 级时留在平面上（图层仍是地球 = 已摊平，缩小到 5 级以下卷回地球）
            if (!testRun && mapc.globeLayer && !win.globeMode && !(framed && mapc.zoom > win.flattenZoom)) {
                win.enterGlobeNow()
                if (!framed) mapc.fitAll(false)
            }
            win.startDone = true
        }
    }
    Connections {
        target: mapLoader
        function onLoaded() { if (!testRun || shotPage === "now") frameNextTimer.start() }
    }

    // 确认：标记为已完成 / 删除计划
    MessageDialog {
        id: planConfirm
        property string kind: ""
        property string planId: ""
        property string planName: ""
        function ask(k, id, name) { kind = k; planId = id; planName = name || qsTr("未命名计划"); open = true }
        title: kind === "delete" ? qsTr("删除计划") : qsTr("标记为已完成")
        paragraphs: kind === "delete"
            ? [qsTr("确定删除「%1」吗？").arg(planName), qsTr("删除后可以按 Ctrl+Z 撤销。计划里的照片文件不会删除，还在照片库里。")]
            : [qsTr("「%1」的日期已经过了。标记为“已完成”吗？").arg(planName)]
        buttons: kind === "delete" ? [{ key: "cancel", text: qsTr("取消") }, { key: "ok", text: qsTr("删除"), kind: "danger" }]
                                   : [{ key: "cancel", text: qsTr("取消") }, { key: "ok", text: qsTr("标记为已完成"), kind: "primary" }]
        onChosen: (key) => {
            open = false
            if (key !== "ok") return
            if (kind === "delete") {
                if (plans.deletePlan(planId)) toast.show(qsTr("已删除「%1」").arg(planName), false, qsTr("撤销"), () => app.undo())
            } else if (plans.markCompleted(planId)) toast.show(qsTr("已标记为已完成"), false, "", null, true)
        }
    }

    // ---- "已解锁"的足迹框 / 手动标记（0.8.5，M-08）：左下角；手动标记时变成左边一列 ----
    UnlockPanel {
        id: unlockPanel
        objectName: "unlockPanel"
        visible: mapc.base === "unlock" && !win.globeMode && win.map !== null
        x: win.leftX
        Behavior on x { enabled: Theme.motion; TpMove {} }
        readonly property real bottomGap: win.height - win.leftStackTop + 10   // 0.8.9：在比例尺 / 图例上面（Electron 左下：足迹 → 比例尺 → 图例）
        y: win.height - height - bottomGap
        z: 12
        maxWidth: win.width - 14 - x
        maxHeight: Math.max(Theme.px(240), win.height - (win.topH + 4) - bottomGap)
        onVisibleChanged: if (!visible) mapc.unlockEdit = false
    }

    // ---- 0.8.9 左下：比例尺、图例（M-30，PO 选 A 照 Electron：左下从上到下 足迹框 → 比例尺 → 图例 → 状态胶囊）；0.9.7 起没有图例 ----
    readonly property real leftStackBottom: statusPill.visible ? statusPill.y - 10 : win.height - 20
    readonly property real leftStackTop: scaleBar.visible ? scaleBar.y : leftStackBottom + 10
    readonly property bool drawerPanelOpen: win.drawerMode && (win.sheetOpen || win.editorShown)
    // 0.9.7：图例去掉（左下胶囊去重复，PO 15:15 选方案 A：模式本身就说明了线型对应的状态）
    // 比例尺（Electron v5.10.0 的样子：一条白线段 + 两头小竖线 + 白字，带轻阴影，什么底图上都看得清；公制、最长 100 px）
    Item {
        id: scaleBar
        objectName: "scaleBar"
        visible: win.mapShown && !win.drawerPanelOpen && mapc.scaleWidth > 0 && !win.albumOpen
        x: win.leftX + 2
        y: win.leftStackBottom - height
        z: 12
        width: mapc.scaleWidth
        height: Theme.px(16)
        Behavior on x { enabled: Theme.motion; TpMove {} }
        Rectangle { y: parent.height - 2; width: parent.width; height: 2; color: "#ffffff"; layer.enabled: true }
        Rectangle { y: parent.height - 7; width: 2; height: 7; color: "#ffffff" }
        Rectangle { x: parent.width - 2; y: parent.height - 7; width: 2; height: 7; color: "#ffffff" }
        Text {
            x: 3; y: parent.height - 3 - height
            text: mapc.scaleLabel
            font.family: serifFamily; font.pixelSize: Theme.px(11); font.bold: true
            color: "#ffffff"
            style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.35)
        }
    }

    Timer { running: shotPage === "ui-settings-narrow"; interval: 400; onTriggered: win.width = 620 }   // 截图：设置页的窄窗口
    // ---- AI 助手泡泡（0.12.3 放好，0.13.0 接功能）：我的位置上面一颗；面板开着时玻璃 + 主题色细边（I-11）----
    RoundButton {
        id: aiBtn
        objectName: "aiBtn"
        visible: locBtn.visible && netc.allowed.ai   // Electron：AI 那项关了（或离线）就不显示
        width: locBtn.width; height: width
        x: locBtn.x
        y: locBtn.y - height - Theme.px(10)
        z: 13
        label: "AI"
        iconColor: Theme.accentInk
        tip: qsTr("AI 助手")
        on: aiPanel.open
        onClicked: aiPanel.open = !aiPanel.open
    }
    AiPanel {
        id: aiPanel
        width: Math.min(Theme.px(400), win.width - 2 * win.gap)
        height: Math.max(Theme.px(320), Math.min(Theme.px(620), aiBtn.y - Theme.px(210)))   // 顶上让开右边的缩放按钮
        x: win.width - width - win.gap
        y: aiBtn.y - height - Theme.px(10)
        z: 14
        onCloseRequested: open = false
        onSettingsRequested: { open = false; goPage("settings"); settings.showSection("ai") }
        onToastRequested: (t, e) => toast.show(t, e)
        Connections { target: aiBtn; function onVisibleChanged() { if (!aiBtn.visible) aiPanel.open = false } }   // 离线、关了 AI、打开右侧面板时跟着收起
    }
    // ---- 0.8.9 我的位置（M-26，PO 选 A：右下角单独一颗）----
    Item {
        id: locBtn
        objectName: "locBtn"
        visible: win.mapShown && !win.sheetOpen && !win.editorShown && !win.albumOpen   // Electron：右边面板开着时藏起来
        width: Theme.px(46); height: width
        x: win.width - width - win.gap
        y: win.height - height - win.gap
        z: 13
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("我的位置")
        GlassPanel {
            anchors.fill: parent
            radius: width / 2
            tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.69 : 0.96)
            edge: locMa.containsMouse ? Theme.lineStrong : Theme.glassEdge
            scale: locMa.pressed ? 0.92 : 1
            Behavior on scale { TpSpring {} }
            TpIcon {
                anchors.centerIn: parent
                width: Theme.px(22); height: width
                id: locIcon
                name: "locate"
                color: Theme.accentInk
                RotationAnimator on rotation { running: locator.busy && Theme.motion; from: 0; to: 360; duration: 1200; loops: Animation.Infinite; onStopped: locIcon.rotation = 0 }
            }
        }
        TpTip { text: qsTr("我的位置"); show: locMa.containsMouse }
        MouseArea { id: locMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: locator.locate() }
    }

    // ---- 横幅：只读模式 / 更新版本的 app 写的 ----
    // 0.8.8：横幅放在顶栏下面、左边的列表和右上一列按钮之间（不盖住它们）
    readonly property real bannerLeft: win.leftX
    readonly property real bannerRight: win.rightColX - layers.btnSize - 14
    Column {
        id: banners
        x: Math.round(win.bannerLeft + (win.bannerRight - win.bannerLeft - width) / 2)
        y: win.topH + 2
        width: Math.max(Theme.px(240), Math.min(Theme.px(860), win.bannerRight - win.bannerLeft))
        spacing: 12
        z: 10
        Banner {
            width: parent.width
            visible: app.readOnly && !app.newerVersion && !app.lockedByOther || shotPage === "ui-unreadable"
            warning: true
            closable: false
            title: qsTr("数据文件读不出来，已暂停保存")
            detail: qsTr("为了不覆盖原文件，现在只能看、不能改。可以从历史版本或 travel.backup.json 恢复。") + "\n" + app.loadError
            TpButton { text: qsTr("打开数据文件夹"); compact: true; onClicked: app.openDataFolder() }
            TpButton { text: qsTr("历史版本…"); compact: true; kind: "primary"; onClicked: win.goPage("settings") }
        }
        Banner {
            width: parent.width
            visible: app.newerVersion || shotPage === "ui-newer"
            warning: true
            closable: false
            title: qsTr("这份数据是更新版本的旅行计划写的")
            detail: qsTr("数据格式版本 %1，这个版本只认识到 %2。为了不弄乱数据，现在只能看、不能改。请先更新软件。").arg(app.newerVersion ? app.fileVersion : 2).arg(1)
        }
        Banner {
            width: parent.width
            visible: app.lockedByOther && !app.newerVersion || shotPage === "ui-locked"
            closable: false
            title: qsTr("这份数据正在别处打开")
            detail: qsTr("%1 正开着这份数据，现在只能看、不能改。那边关掉以后，这里会自动恢复编辑。").arg(app.lockOwner !== "" ? app.lockOwner : qsTr("%1 上的旅行计划 %2").arg("LAPTOP-JASON").arg("0.6.0"))
            TpButton { text: qsTr("仍然编辑…"); compact: true; onClicked: takeOverDialog.open = true }
        }
        // 底图瓦片连续 8 次没取到（0.8.5，M-04；Electron map.on('error') → showBanner(…, '切到已解锁')）
        Banner {
            id: tileBanner
            objectName: "tileBanner"
            width: parent.width
            visible: mapc.tileBanner && (win.globeMode ? globe.base !== "unlock" : mapc.base !== "unlock")
            warning: true
            title: qsTr("当前底图加载失败（可能没联网，或服务方限流、改了政策）。可以换成不用联网的「已解锁」底图。")
            onCloseClicked: mapc.dismissTileBanner()
            TpButton { objectName: "tileRetry"; text: qsTr("重试"); compact: true; onClicked: mapc.retryTiles() }
            TpButton { text: qsTr("切到已解锁"); compact: true; kind: "primary"; onClicked: win.pickBase("unlock") }
        }
        // "有 N 段还没算"（0.8.4，Electron #roadBar：顶栏下面居中）
        RoadBar {
            id: roadBar
            anchors.horizontalCenter: parent.horizontalCenter
            maxWidth: Math.min(Theme.px(620), parent.width)
        }
    }

    // ---- 左下角：测试信息（图形后端、退回原因、数据、刷新率）；只在开发版显示（0.8.0） ----
    Rectangle {
        id: devInfo
        visible: app.devTools && !win.fullPageOpen && !win.drawerPanelOpen && !plans.libraryOpen && !win.settingsOpen && !win.tripOpen
                 && !tour.offering && !tour.active && !toast.visible   // 0.12.11 引导邀请、升级提示等底部提示出现时让开；计划库、设置、计划页开着时地图窄，放不下；0.8.9：窄窗口抽屉开着时不显示（会透到抽屉底下）
        anchors.right: parent.right   // 0.8.7：左下角给状态胶囊，测试信息挪到右下角（简易面板开着时让到它左边）
        anchors.bottom: parent.bottom
        anchors.margins: 14
        anchors.bottomMargin: monthBar.visible && monthBar.below ? 28 + monthBar.height : 14   // 0.11.0 让开想去的月份条
        anchors.rightMargin: 14 + (win.drawerMode ? 0 : win.editorShown ? planEditor.width + 14 : win.sheetOpen ? planSheet.targetW + 14 : 0) + (locBtn.visible ? locBtn.width + 10 : 0)
        // 0.8.9：左边有图例 / 比例尺，宽度不能盖过去；放不下就截断（只有开发版有这个框）
        readonly property real leftLimit: Math.max(14, statusPill.visible ? statusPill.x + statusPill.width + 14 : 14)
        width: Math.max(Theme.px(160), Math.min(info.natW + 28, win.width - anchors.rightMargin - leftLimit))
        height: info.implicitHeight + 20
        radius: 12
        color: "#ecf8fafc"
        border.color: "#dbe3e9"
        Column {
            id: info
            x: 14; y: 10
            spacing: 3
            width: devInfo.width - 28
            readonly property real natW: { let w = 0; for (let k = 0; k < children.length; ++k) if (children[k].visible) w = Math.max(w, children[k].implicitWidth); return w }
            Text {
                width: info.width; elide: Text.ElideRight
                font.family: serifFamily; font.pixelSize: 14; font.bold: true; color: "#16202a"
                text: qsTr("图形后端：") + backendApi + (win.rhiName ? "（RHI " + win.rhiName + "）" : "")
                      + (backendDevice ? " · " + backendDevice : (win.rhiDevice ? " · " + win.rhiDevice : ""))
            }
            Text {
                width: info.width; elide: Text.ElideRight
                visible: backendReason !== ""
                font.family: serifFamily; font.pixelSize: 13; color: "#c2560f"
                text: qsTr("没用 Vulkan 的原因：") + win.trReason(backendReason)
            }
            Text {
                width: info.width; elide: Text.ElideRight
                font.family: serifFamily; font.pixelSize: 13; color: "#5d6d7b"
                text: mapc.error ? qsTr("数据：") + mapc.error : mapc.dataInfo + (mapc.unlockInfo ? " · " + mapc.unlockInfo : "")
            }
            Text {
                width: info.width; elide: Text.ElideRight
                font.family: serifFamily; font.pixelSize: 12; color: "#a9b6c0"
                text: (app.dataDir !== "" ? app.dataDir : qsTr("（没有数据）")) + "  ·  " + qsTr("刷新 ") + win.fps + qsTr(" 帧/秒（静止时不刷新）")
                      + (win.globeMode ? "  ·  " + globe.tileStatus : "")
            }
        }
    }

    // ---- 右下角卡片：出错小框（0.5.1，新的在上面）+ 外部修改提示 ----
    ListModel { id: errModel }   // { code, message, report }
    Column {
        id: bottomCards
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: win.settingsOpen ? settings.width + 28 : 14   // 设置面板开着时往左让开
        anchors.bottomMargin: 20
        spacing: 10
        z: 20
        Repeater {
            model: errModel
            delegate: ErrorCard {
                required property int index
                required property var model
                code: model.code
                message: model.message
                report: model.report
                width: Math.min(Theme.px(340), win.width - 28)
                onCloseClicked: errModel.remove(index)
                onCopyClicked: { app.copyText(report); toast.show(qsTr("已复制，可以直接粘贴发给 Jason"), false) }
            }
        }
        ExtChangedCard {
            id: extCard
            property bool forced: false
            width: Math.min(Theme.px(340), win.width - 28)
            open: (app.extChanged || forced) && !win.settingsOpen
            dirty: app.dirty || forced && shotPage === "ui-ext-dirty"
            onReloadClicked: { forced = false; app.reloadData() }
            onLaterClicked: { forced = false; app.dismissExtChanged() }
        }
    }
    Connections {
        target: errors
        function onPopup(code, message, report) { errModel.insert(0, { code: code, message: message, report: report }) }
    }

    // ---- 0.10.0 完整计划页（右侧，宽 ≤ 820）----
    // 面板开着时地图压暗一点，分清前后（S-03；Electron 亮度 / 饱和度 .9，这里用一层半透明，便宜）
    Rectangle {
        anchors.fill: parent
        z: 13
        color: Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.1)
        opacity: win.tripOpen || win.settingsOpen ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { enabled: Theme.motion; TpAnim { shown: win.tripOpen || win.settingsOpen } }
    }
    PlanPage {
        id: planPanel
        objectName: "planPage"
        z: 14
        width: Math.min(Theme.px(820), win.width - 2 * win.gap)
        y: win.topH + 4
        height: win.height - y - win.gap
        x: win.tripOpen ? win.width - width - win.gap : win.width + 20
        visible: x < win.width
        Behavior on x { enabled: Theme.motion; TpAnim { shown: win.tripOpen } }
        shown: win.tripOpen
        source: planPage.info
        focusIndex: mapc.markers.focusStop
        onCloseRequested: { if (nav.canBack) nav.back(); else win.goPage("map") }
        onSimpleRequested: win.goPage("map")
        // 0.11.0：想去的计划（"愿望清单"按钮）去想去模式；其余打开计划库
        onListRequested: { if (planPage.info.status === "wish") { win.goPage("wish"); win.listOpen = true } else win.openLibrary() }
        onEditRequested: (id) => win.editPlan(id)
        onDaysRequested: (cur) => { daysDialog.text = cur; daysDialog.open = true }
        onStartRequested: { startDialog.start = planPage.info.startDate || ""; startDialog.end = planPage.info.endDate || ""; startDialog.open = true }
        onTogglePin: (id) => { if (plans.togglePin(id)) toast.show(planPage.info.pinned ? qsTr("已置顶") : qsTr("已取消置顶"), false) }
        onToggleNext: (id, on) => { if (plans.setNextTrip(id, on)) toast.show(on ? qsTr("已设为下次出发") : qsTr("已取消下次出发"), false) }
        onPickStop: (i) => mapc.markers.selectPlan(win.tripId, i, true)
        onAddBooking: (kind) => bookingDialog.show(null, kind)
        onEditBooking: (row) => bookingDialog.show(row, "")
        onDeleteBooking: (row) => win.deleteBookingRow(row)
        onAddExpense: (draft) => expenseDialog.show(null, draft)
        onEditExpense: (row) => expenseDialog.show(row, null)
        onDeleteExpense: (row) => win.deleteExpenseRow(row)
        onPeopleRequested: peopleDialog.show()
        onNotice: (t) => toast.show(t, false)
        onSaveTemplateRequested: {
            tplDialog.paragraphs = [qsTr("用这份清单（%1 项）替换原来的模板，以后别的计划可以“从模板添加”。").arg(planPage.info.kit.packing.length)]
            tplDialog.open = true
        }
        onEditLocalEmergency: emergencyDialog.showLocal(planPage.info.kit)
        onEditEmergency: emergencyDialog.showGlobal(planPage.info.kit)
        onDocsRequested: win.openDocs()
        onGuideLinkRequested: (s) => guideLinkDialog.show(s)
        onGuidePhotoRequested: {
            const list = planPage.planPhotos()
            if (list.length === 0) toast.show(qsTr("这个计划还没有照片"), false)
            else photoPickDialog.show(list)
        }
        onPhotoAddRequested: (stopId) => win.addPhotos(planPage.planId, stopId, false)
        onPhotoCaptionRequested: (ph) => photoCaptionDialog.show(ph)
        onPhotoRemoveRequested: (ph) => photoRemoveDialog.ask(planPage.planId, ph)
        onPhotoViewRequested: (l, i) => lightbox.show(l, i)
        // 退回来还在游记标签
        onJournalWriteRequested: { win.tripTab = "journal"; win.tripTabPlan = planPage.planId; journalPage.writeNew(planPage.planId) }
        onJournalOpenRequested: (id) => { win.tripTab = "journal"; win.tripTabPlan = planPage.planId; nav.go("journal", { id: id }) }
        onCalendarRequested: win.showInCalendar(planPage.planId, planPage.info.startDate || planPage.info.endDate || "")
        onBudgetRequested: {
            budgetDialog.budget = planPage.info.money.budget > 0 ? String(planPage.info.money.budget) : ""
            budgetDialog.cur = planPage.info.money.currency
            budgetDialog.bad = false
            budgetDialog.open = true
        }
    }
    EmergencyDialog { id: emergencyDialog; objectName: "emergencyDialog" }
    DocDialog { id: docDialog; objectName: "docDialog" }
    GuideLinkDialog { id: guideLinkDialog; objectName: "guideLinkDialog"; onAccepted: (md) => planPanel.guideInsertLink(md) }
    PhotoPickDialog { id: photoPickDialog; objectName: "photoPickDialog"; onPicked: (ph) => planPanel.guideInsertPhoto(ph) }
    // ---- 0.10.5 照片标签和编辑器共用：写说明、移除、加照片、选封面 ----
    TextInputDialog {
        id: photoCaptionDialog
        objectName: "photoCaptionDialog"
        maxLength: 200
        title: qsTr("照片说明")
        property string photoId: ""
        function show(ph) { photoId = ph.id; text = ph.caption || ""; open = true }
        onAccepted: (t) => { open = false; photos.setCaption(photoId, t) }
        onCanceled: open = false
    }
    MessageDialog {
        id: photoRemoveDialog
        objectName: "photoRemoveDialog"
        title: qsTr("从计划里移除这张照片？")
        paragraphs: [qsTr("只是从计划里去掉，照片文件还留在数据文件夹的 photos 里（不会删文件）。")]
        buttons: [{ key: "cancel", text: qsTr("取消") }, { key: "ok", text: qsTr("移除"), kind: "danger" }]
        property string planId: ""
        property string photoId: ""
        // 0.12.9 能撤销就不问；编辑器开着时不能撤销，还是问一句
        function ask(plan, ph) {
            planId = plan; photoId = ph.id
            if (editor.open) open = true
            else win.deleteWithUndo(() => photos.removeFromPlan(plan, ph.id), qsTr("已移除"))
        }
        onChosen: (key) => {
            open = false
            if (key !== "ok") return
            // 编辑器开着时计划在它手里：移除的是封面就让它也去掉，否则下次保存又写回去
            if (editor.open && editor.plan.cover === photoId) editor.setField("cover", "")
            photos.removeFromPlan(planId, photoId)
        }
    }
    PhotoPickDialog {
        id: coverPickDialog
        objectName: "coverPickDialog"
        title: qsTr("选择封面照片")
        fromFile: true
        onPicked: (ph) => editor.setField("cover", ph.id)
        onFileRequested: win.addPhotos(editor.planId, "", true)
    }
    FileDialog {
        id: addPhotosDialog
        title: qsTr("选择要导入的照片")
        fileMode: asCover ? FileDialog.OpenFile : FileDialog.OpenFiles
        nameFilters: photoFileDialog.nameFilters
        property string planId: ""
        property string stopId: ""
        property bool asCover: false
        onAccepted: {
            win.coverImport = asCover
            const r = photos.startImport(selectedFiles, planId, stopId)
            if (!r.ok) { win.coverImport = false; toast.show(r.error, true) }
        }
    }
    property bool coverImport: false        // 编辑器"从电脑选一张"：导入完把它设成封面
    function addPhotos(planId, stopId, asCover) {
        addPhotosDialog.planId = planId
        addPhotosDialog.stopId = stopId
        addPhotosDialog.asCover = asCover
        addPhotosDialog.open()
    }
    MessageDialog {
        id: tplDialog
        objectName: "tplDialog"
        title: qsTr("存成行李模板？")
        buttons: [{ key: "cancel", text: qsTr("取消") }, { key: "ok", text: qsTr("存成模板"), kind: "primary" }]
        onChosen: (key) => {
            open = false
            if (key === "ok" && planPage.saveTemplate() > 0) toast.show(qsTr("模板已更新"), false)
        }
    }
    BookingDialog { id: bookingDialog; objectName: "bookingDialog"; currency: (planPage.info.money || {}).currency || "" }
    ExpenseDialog { id: expenseDialog; objectName: "expenseDialog"; planCurrency: (planPage.info.money || {}).currency || ""; today: plans.today; people: (planPage.info.money || {}).people || [] }
    // 同行人（0.11.5）：放在记一笔后面，从那里打开时盖在上面
    PeopleDialog {
        id: peopleDialog
        objectName: "peopleDialog"
        money: planPage.info.money || ({})
    }
    ModalLayer {
        id: budgetDialog
        objectName: "budgetDialog"
        cardWidth: 400
        property string budget: ""
        property string cur: ""
        property bool bad: false
        function save() {
            const err = planPage.setBudget(budget, cur)
            bad = err === "badBudget"
            if (!err) open = false
        }
        onDismissed: open = false
        Text { width: parent.width; text: qsTr("预算"); font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text }
        Row {
            width: parent.width
            spacing: Theme.px(10)
            TpField {
                width: parent.width - parent.spacing - Theme.px(110)
                placeholder: qsTr("总预算")
                value: budgetDialog.budget
                bad: budgetDialog.bad
                onEdited: (t) => { budgetDialog.budget = t }
                onAccepted: budgetDialog.save()
                onEscaped: budgetDialog.open = false
            }
            TpSelect {
                width: Theme.px(110)
                options: (rates.currencies.indexOf(budgetDialog.cur) < 0 && budgetDialog.cur !== "" ? [budgetDialog.cur] : []).concat(rates.currencies).map(c => ({ key: c, text: c }))
                current: budgetDialog.cur
                onPicked: (k) => { budgetDialog.cur = k }
            }
        }
        Text { visible: budgetDialog.bad; width: parent.width; text: qsTr("预算不能是负数"); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger }
        Row {
            anchors.right: parent.right
            spacing: Theme.px(8)
            TpButton { text: qsTr("取消"); onClicked: budgetDialog.open = false }
            TpButton { objectName: "budgetOk"; kind: "primary"; text: qsTr("保存"); onClicked: budgetDialog.save() }
        }
    }
    TextInputDialog {
        id: daysDialog
        maxLength: 6
        title: qsTr("预计天数")
        hint: qsTr("大约几天（不确定可以留空）")
        onAccepted: (t) => {
            if (planPage.setDays(t) === "badDays") error = qsTr("天数要在 1 到 999 之间")   // 窗口留着，打的字不丢
            else open = false
        }
        onCanceled: open = false
    }
    ModalLayer {
        id: startDialog
        cardWidth: 420
        property string start: ""
        property string end: ""
        onDismissed: open = false
        Text { width: parent.width; text: qsTr("开始计划"); font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text }
        Row {
            width: parent.width
            spacing: Theme.px(10)
            DateField { width: (parent.width - parent.spacing) / 2; placeholder: qsTr("出发"); value: startDialog.start; onEdited: (v) => { startDialog.start = v } }
            DateField { width: (parent.width - parent.spacing) / 2; placeholder: qsTr("回来"); value: startDialog.end; minDate: startDialog.start; onEdited: (v) => { startDialog.end = v } }
        }
        Row {
            anchors.right: parent.right
            spacing: Theme.px(8)
            TpButton { text: qsTr("取消"); onClicked: startDialog.open = false }
            TpButton {
                objectName: "startOk"
                kind: "primary"
                text: qsTr("改成“计划中”")
                onClicked: { startDialog.open = false; planPage.startPlanning(startDialog.start, startDialog.end) }
            }
        }
    }

    // ---- 设置面板（右侧） ----
    SettingsPanel {
        id: settings
        objectName: "settingsPanel"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: win.gap
        width: win.w900 ? win.width - 2 * win.gap : Math.min(Theme.px(760), win.width - 28)   // ≤ 900 占满宽度（S-15）；0.12.3 放宽，放侧边栏
        anchors.topMargin: win.topH + 4   // 0.8.8：停在顶栏下面（Electron 右边面板 top: --mf-top + 4），顶栏的 ↩ 一直看得到
        z: 30
        open: win.settingsOpen
        onCloseClicked: win.goPage("map")
        onChooseFolderClicked: folderDialog.open()
        onEditTitleClicked: { titleDialog.text = app.windowTitleExtra; titleDialog.open = true }
        onRestoreClicked: (entry) => {
            restoreDialog.entry = entry
            restoreDialog.open = true
        }
        onPickBackupFolder: (then) => { backupFolderDialog.then = then; backupFolderDialog.open() }
        onImportClicked: { importDialog.src = app.electronDataDir; importDialog.open = true }
        onToastRequested: (text, error) => toast.show(text, error)
        onCleanupClicked: win.startCleanup()
        onDocRequested: (row) => docDialog.show(row)
        onEmergencyRequested: emergencyDialog.showGlobal({ emergency: planPage.emergencyInfo() })
    }

    // ---- 0.8.10 整页：账本 0.10.2，日历、游记 0.11.2 ----
    LedgerPage {
        objectName: "ledgerPage"
        anchors.fill: parent
        anchors.margins: win.gap
        anchors.topMargin: 0   // 0.12.10：整页从窗口顶部开始，页头和顶栏同一行（BarHead）
        z: 20
        shown: nav.page === "ledger"
        onOpenPlan: (id) => { win.tripTab = "budget"; win.openTrip(id, -1) }
        onOpenLibrary: win.openLibrary()
    }
    // ---- 0.13.2 让 AI 帮我规划（整页向导）：提交后回地图、打开 AI 面板接着聊 ----
    AiPlanPage {
        id: aiPlanPage
        objectName: "aiPlanPage"
        anchors.fill: parent
        anchors.margins: win.gap
        anchors.topMargin: 0
        z: 20
        shown: nav.page === "aiplan"
        onSettingsRequested: { win.goPage("settings"); settings.showSection("ai") }
        onSubmitted: { win.goPage("map"); aiPanel.open = true }
    }
    // ---- 0.11.2 日历、游记 ----
    CalendarPage {
        id: calendarPage
        objectName: "calendarPage"
        anchors.fill: parent
        anchors.margins: win.gap
        anchors.topMargin: 0
        z: 20
        shown: nav.page === "calendar"
        onOpenPlan: (id, tab) => { win.tripTab = tab; win.openTrip(id, -1) }
        onToast: (t, warn) => toast.show(t, warn)
        onNewPlan: win.newPlan()
    }
    JournalPage {
        id: journalPage
        objectName: "journalPage"
        anchors.fill: parent
        anchors.margins: win.gap
        anchors.topMargin: 0
        z: 20
        shown: nav.page === "journal"
        onOpenPlan: (id) => { win.tripTab = "journal"; win.openTrip(id, -1) }
        onShowPhotos: (l, i) => lightbox.show(l, i)
        onDeleteRequested: (id, title) => {
            journalDeleteDialog.entry = id
            journalDeleteDialog.paragraphs = [qsTr("「%1」\n正文里插入的照片不会被删除。").arg(title || qsTr("无标题"))]
            journalDeleteDialog.open = true
        }
        onAddFromFile: (planId, stopId) => win.addPhotos(planId, stopId, false)
        onPickPhotoRequested: (rows, all) => {
            if (rows.length === 0 && all === null) journalPhotoPick.show(photos.library())
            else journalPhotoPick.show(rows, all)
        }
        onLinkRequested: (s) => journalLinkDialog.show(s)
    }
    MessageDialog {
        id: journalDeleteDialog
        objectName: "journalDeleteDialog"
        property string entry: ""
        title: qsTr("删除这篇游记？")
        buttons: [{ key: "cancel", text: qsTr("取消") }, { key: "ok", text: qsTr("删除"), kind: "danger" }]
        onChosen: (key) => {
            open = false
            if (key !== "ok") return
            journalPage.removeCurrent(entry)
            toast.show(qsTr("已删除"), false, qsTr("撤销"), () => app.undo())
        }
    }
    PhotoPickDialog { id: journalPhotoPick; objectName: "journalPhotoPick"; onPicked: (ph) => journalPage.insertPicked(ph) }
    GuideLinkDialog { id: journalLinkDialog; objectName: "journalLinkDialog"; onAccepted: (md) => journalPage.insertLink(md) }
    // 计划页 ⋯"在日历里看"（Electron：有日期跳到那个月，没有就在右边定位）
    function showInCalendar(id, date) {
        win.goPage("calendar")
        if (date) calendar.showDate(date)
        else calendarPage.focusUndated(id)
    }

    // ---- 照片（0.7.0） ----
    function startCleanup() {
        const r = photos.scanCleanup()
        if (!r.ok) toast.show(r.error, true)
        else cleanupDialog.show(r)
    }
    FileDialog {
        id: photoFileDialog
        title: qsTr("选择要导入的照片")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("照片") + " (*.jpg *.jpeg *.png *.webp *.heic *.heif *.gif *.bmp)", qsTr("所有文件") + " (*)"]
        onAccepted: {
            const r = photos.startImport(selectedFiles)
            if (!r.ok) toast.show(r.error, true)
        }
    }
    PhotoImportReview {
        id: photoReview
    }
    PhotoCleanupDialog {
        id: cleanupDialog
        onToastRequested: (text, error) => toast.show(text, error)
    }
    Connections {
        target: photos
        function onImportFinished(r) {
            if (win.coverImport && r.ok && editor.open && (r.ids || []).length > 0) editor.setField("cover", r.ids[0])
            win.coverImport = false
            if (r.ok) toast.show(r.count === 0 && r.skipped > 0 ? qsTr("没有导入新照片，跳过 %1 张").arg(r.skipped)
                                 : qsTr("已导入 %1 张：%2 张归到计划").arg(r.count).arg(r.toPlan)
                                 + (r.inbox > 0 ? qsTr("，%1 张在“未归类”").arg(r.inbox) : "")
                                 + (r.skipped > 0 ? qsTr("，跳过 %1 张").arg(r.skipped) : ""), false)
            else if (r.cancelled) toast.show(qsTr("导入已取消，已经复制的照片删掉了。"), false)
            else toast.show(qsTr("照片没有导入：") + (r.error || ""), true)
        }
    }

    // ---- 对话框 ----
    FolderDialog {
        id: folderDialog
        title: qsTr("选择数据文件夹")
        currentFolder: app.dataDir !== "" ? "file:///" + app.dataDir.replace(/\\/g, "/") : ""
        onAccepted: {
            const r = app.switchDataFolder(app.localPath(selectedFolder), false)
            if (r.electron) { electronFolderDialog.dir = r.dir; electronFolderDialog.open = true }
            else if (r.needCreate) { noDataDialog.dir = r.dir; noDataDialog.open = true }
            else if (!r.ok && r.error) toast.show(r.error, true)
            else if (r.ok) dataChoice.forced = ""
        }
    }

    // 选备份文件夹（0.5.0）：then = "now" 选完马上备份 / "auto" 选完打开自动备份
    FolderDialog {
        id: backupFolderDialog
        property string then: ""
        title: qsTr("选择备份文件夹")
        currentFolder: app.backupDir !== "" ? "file:///" + app.backupDir.replace(/\\/g, "/") : ""
        onAccepted: {
            const r = app.setBackupFolder(app.localPath(selectedFolder))
            if (!r.ok) { toast.show(r.error, true); return }
            if (then === "now") app.backupNow()
            else if (then === "auto") app.setAutoBackup(true)
        }
        onRejected: if (then === "auto") toast.show(qsTr("没选备份文件夹，自动备份没有打开"), false)
    }

    // 启动时选数据文件夹：第一次用（电脑上有 Electron 版的数据：问要不要导入）/ 记住的找不到了
    ChoiceDialog {
        id: dataChoice
        property string forced: ""
        readonly property string mode: forced !== "" ? forced : app.startupChoice
        open: mode !== "" && mode !== "welcome"
        title: mode === "missing" ? qsTr("找不到数据文件夹") : qsTr("数据放在哪？")
        message: mode === "missing"
                 ? (app.missingDir !== "" ? qsTr("上次用的数据文件夹不见了：") + app.missingDir : qsTr("还没有可用的数据文件夹。"))
                 : qsTr("这台电脑上有 Electron 版旅行计划的数据。原生版用自己的数据文件夹，可以把 Electron 版的计划和照片导入进来（Electron 版的数据不会被改动）。")
        options: {
            const o = []
            if (app.electronDataDir !== "")
                o.push({ key: "import", title: qsTr("从 Electron 版导入"), detail: app.electronDataDir + "\n" + qsTr("导入到：") + app.defaultDataDir })
            o.push({ key: "default", title: mode === "missing" ? qsTr("在软件文件夹里新建") : qsTr("新建空数据"), detail: app.defaultDataDir + "\n" + qsTr("从空数据开始；卸载、重装都不会删除。") })
            o.push({ key: "pick", title: qsTr("选择其他文件夹…"), detail: "" })
            return o
        }
        onChosen: (key) => {
            let r = { ok: true }
            if (key === "import") r = app.firstRunImport()
            else if (key === "default") r = app.useDefaultData()
            else if (key === "pick") { win.pickFromStartup = true; folderDialog.open(); return }
            if (!r.ok) toast.show(r.error, true)
            else forced = ""
        }
    }

    // 第一次用（0.12.7，S-16）。盖住整个窗口；下面的弹框（选了没数据的文件夹等）要在它上面，所以放在它们前面、z 由声明顺序定
    WelcomePage {
        id: welcomePage
        objectName: "welcomePage"
        z: 900
        onOpenExisting: { win.pickFromStartup = true; folderDialog.open() }
        onStarted: rates.main = app.guessCurrency()
    }

    // 选的文件夹里还没有旅行计划的数据（travel.json）
    ChoiceDialog {
        id: noDataDialog
        property string dir: ""
        title: qsTr("这个文件夹里还没有旅行计划的数据")
        message: dir
        cancelKey: "cancel"
        options: [
            { key: "create", title: qsTr("在这里新建空数据"), detail: qsTr("建一个空的 travel.json 和 photos 文件夹。") },
            { key: "again", title: qsTr("重新选择"), detail: "" }
        ]
        onChosen: (key) => {
            open = false
            if (key === "create") {
                const r = app.switchDataFolder(dir, true)
                if (!r.ok) toast.show(r.error, true)
                else {
                    dataChoice.forced = ""
                    // 第一次用时在别处新建了空数据：电脑上有 Electron 版就接着问要不要导入
                    if (win.pickFromStartup && app.electronDataDir !== "") { importDialog.src = app.electronDataDir; importDialog.open = true }
                }
                win.pickFromStartup = false
            } else if (key === "again") folderDialog.open()
        }
    }

    // 冲突（PO 2026-11-02 选方案 B）
    ChoiceDialog {
        id: conflictDialog
        title: qsTr("数据文件被别处改过")
        message: qsTr("打开之后，数据文件夹里的 travel.json 被别的程序改过（比如另一个旅行计划窗口）。这次保存要怎么办？")
        cancelKey: "keep"
        options: [
            { key: "keep", title: qsTr("先不保存"), detail: qsTr("改动留在窗口里。之后可以重新加载看最新数据（没保存的改动会丢）。") },
            { key: "force", title: qsTr("仍然保存，覆盖别处的改动"), detail: qsTr("用窗口里的数据覆盖。被覆盖的那一版存成 travel.backup.json，可以找回。"), danger: true }
        ]
        onChosen: (key) => {
            open = false
            if (key === "force") app.saveNow(true)
            else app.conflictKept()   // 改动留在窗口里；下次改动时再问（Electron 一样）
        }
    }

    // 关闭时没能保存
    MessageDialog {
        id: closeFailDialog
        property bool conflict: true
        property string error: ""
        cardWidth: 540
        title: qsTr("关闭前没能保存")
        paragraphs: conflict
            ? [qsTr("travel.json 在打开之后被别处改过，窗口里的改动还没存进去。"),
               qsTr("<b>另存一份后退出</b>：travel.json 不动，窗口里的数据存成 travel.unsaved-日期-时间.json，放在数据文件夹里，以后可以手动合并。"),
               qsTr("<b>仍然保存并退出</b>：覆盖别处的改动（被覆盖的那一版存成 travel.backup.json）。")]
            : [qsTr("保存失败：") + error,
               qsTr("<b>另存一份后退出</b>：travel.json 不动，窗口里的数据存成 travel.unsaved-日期-时间.json，放在数据文件夹里。")]
        buttons: conflict
            ? [{ key: "cancel", text: qsTr("取消") }, { key: "force", text: qsTr("仍然保存并退出"), kind: "dangerOutline" }, { key: "copy", text: qsTr("另存一份后退出"), kind: "primary" }]
            : [{ key: "cancel", text: qsTr("取消") }, { key: "copy", text: qsTr("另存一份后退出"), kind: "primary" }]
        onChosen: (key) => {
            open = false
            if (key === "force") app.quitSaveAnyway()
            else if (key === "copy") app.quitWithCopy()
        }
    }

    // 恢复历史版本的确认
    MessageDialog {
        id: restoreDialog
        property var entry: null
        title: qsTr("恢复到这一天的版本？")
        paragraphs: entry ? [settings.dateText(entry.date) + " · " + qsTr("%1 个计划").arg(entry.plans),
                             qsTr("现在的 travel.json 会先存成 travel.backup.json，然后换成这一版。")] : []
        buttons: [{ key: "cancel", text: qsTr("取消") }, { key: "restore", text: qsTr("恢复"), kind: "primary" }]
        onChosen: (key) => {
            open = false
            if (key === "restore" && entry) {
                const r = app.restoreHistory(entry.name)
                if (!r.ok) toast.show(qsTr("恢复失败：") + r.error, true)
            }
        }
    }

    // 上次意外退出了（0.5.1）：崩溃那一刻写的记号，这次启动时提示一次
    MessageDialog {
        id: crashDialog
        property var crash: ({})   // 不能叫 info：Main 里已有 id info（左下角测试信息），id 优先
        cardWidth: 500
        open: crash.code !== undefined && crash.code !== ""
        cancelKey: "ok"
        title: qsTr("上次旅行计划意外退出了")
        paragraphs: {
            const p = [qsTr("错误码：%1").arg("<b>" + (crash.code || "") + "</b>"),
                       crash.dump ? qsTr("请把错误码发给 Jason；方便的话，把日志文件夹里的 %1 也一起发过去，能帮忙找到原因。").arg(crash.dump)
                                 : qsTr("请把错误码发给 Jason。"),
                       qsTr("没保存的改动可能丢了。")]
            if (crash.gl) p.push(qsTr("这次已自动改用 OpenGL 显示（上次用 Vulkan 启动时出的问题）。"))
            return p
        }
        buttons: [{ key: "copy", text: qsTr("复制错误信息") }, { key: "logs", text: qsTr("打开日志文件夹") }, { key: "ok", text: qsTr("知道了"), kind: "primary" }]
        onChosen: (key) => {
            if (key === "copy") { app.copyText(crash.report || crash.code); toast.show(qsTr("已复制，可以直接粘贴发给 Jason"), false) }
            else if (key === "logs") app.openLogFolder()
            crash = ({})
            app.clearLastCrash()
        }
        Connections { target: app; function onLastCrashChanged() { if (app.lastCrash.code) crashDialog.crash = app.lastCrash } }
        Component.onCompleted: if (app.lastCrash.code) crash = app.lastCrash
    }

    // 选的是 Electron 版的数据文件夹（0.6.0：原生版不在那里写东西，改为导入）
    ChoiceDialog {
        id: electronFolderDialog
        property string dir: ""
        title: qsTr("这是 Electron 版的数据文件夹")
        message: dir + "\n" + qsTr("原生版用自己的数据文件夹，不在这里写东西。可以把它导入到现在的数据里（Electron 版的数据不会被改动）。")
        cancelKey: "cancel"
        options: {
            const o = []
            if (app.dataDir !== "" && !app.readOnly) o.push({ key: "import", title: qsTr("导入到现在的数据…"), detail: app.dataDir })
            o.push({ key: "again", title: qsTr("重新选择"), detail: "" })
            return o
        }
        onChosen: (key) => {
            open = false
            if (key === "import") { importDialog.src = dir; importDialog.open = true }
            else if (key === "again") folderDialog.open()
        }
    }

    // 从 Electron 版导入（0.6.0）：选方式
    ChoiceDialog {
        id: importDialog
        property string src: ""
        cardWidth: 480
        title: qsTr("从 Electron 版导入")
        message: (src !== "" ? qsTr("来源：") + src + "\n" : qsTr("没有找到 Electron 版的数据文件夹，请先选一个。") + "\n")
                 + qsTr("Electron 版的数据不会被改动；照片会复制一份到现在的数据文件夹。")
        cancelKey: "cancel"
        options: {
            const o = []
            if (src !== "") {
                o.push({ key: "add", title: qsTr("只加新的（推荐）"), detail: qsTr("只加入这里还没有的计划；已经有的跳过。") })
                o.push({ key: "replace", title: qsTr("替换全部"), detail: qsTr("用 Electron 版的数据换掉这里现有的数据。现在的数据先备份成 travel.before-import-日期-时间.json。"), danger: true })
            }
            o.push({ key: "pick", title: qsTr("选择 Electron 版的数据文件夹…"), detail: "" })
            return o
        }
        onChosen: (key) => {
            open = false
            if (key === "pick") { electronPick.open(); return }
            if (key !== "add" && key !== "replace") return
            const r = app.importElectron(src, key)
            if (!r.ok) toast.show(r.error, true)
        }
    }
    FolderDialog {
        id: electronPick
        title: qsTr("选择 Electron 版的数据文件夹（里面有 data.js）")
        currentFolder: importDialog.src !== "" ? "file:///" + importDialog.src.replace(/\\/g, "/") : ""
        onAccepted: {
            const dir = app.localPath(selectedFolder)
            if (!app.isElectronFolder(dir)) { toast.show(qsTr("这个文件夹里没有 Electron 版的 data.js"), true); return }
            importDialog.src = dir
            importDialog.open = true
        }
    }

    // 导入进度
    MessageDialog {
        id: importProgressDialog
        property bool forced: false
        open: app.importRunning || forced
        dismissible: false
        cancelKey: "cancel"
        title: qsTr("正在导入…")
        paragraphs: {
            const done = forced ? 31 : app.importDone, total = forced ? 85 : app.importTotal
            return [total > 0 ? qsTr("正在复制照片：%1 / %2").arg(done).arg(total) : qsTr("正在读取 Electron 版的数据…"),
                    qsTr("取消的话，已经复制的照片会删掉，数据不变。")]
        }
        buttons: [{ key: "cancel", text: qsTr("取消") }]
        onChosen: (key) => { if (key === "cancel") { forced = false; app.cancelImport() } }
    }

    // 导入结果
    MessageDialog {
        id: importReportDialog
        property var report: ({})
        cardWidth: 520
        cancelKey: "ok"
        function show(r) { report = r; open = true }
        title: report.ok ? qsTr("导入完成") : (report.cancelled ? qsTr("导入已取消") : qsTr("导入没有完成"))
        paragraphs: {
            const r = report, p = []
            if (!r.ok) {
                if (r.cancelled) p.push(qsTr("已经复制的照片删掉了，数据没有变。"))
                else { p.push(qsTr("出错了：") + (r.error || "")); p.push(qsTr("数据没有变。")) }
                return p
            }
            p.push(qsTr("导入了 %1 个计划、%2 个站点、%3 张照片、%4 篇游记。").arg(r.plans).arg(r.stops).arg(r.photos).arg(r.journal))
            if (r.plansSkipped > 0) p.push(qsTr("已经有的 %1 个计划跳过了。").arg(r.plansSkipped))
            if (r.backupName) p.push(qsTr("原来的数据备份成了 %1。").arg(r.backupName))
            if (r.missingCount > 0) p.push(qsTr("<b>%1 张照片找不到文件</b>（记录保留了，显示为缺图）：").arg(r.missingCount) + r.missing.join("、") + (r.missingCount > r.missing.length ? qsTr(" 等") : ""))
            if (r.unusedCount > 0) p.push(qsTr("Electron 版的照片文件夹里有 %1 个文件没被任何计划用到，没有导入。").arg(r.unusedCount))
            if (r.unknownCount > 0) p.push(qsTr("有 %1 个这个版本不认识的字段，已经原样保留：").arg(r.unknownCount) + r.unknown.join("、"))
            p.push(qsTr("详细情况记在日志里（F12 打开日志文件夹）。"))
            return p
        }
        buttons: [{ key: "logs", text: qsTr("打开日志文件夹") }, { key: "ok", text: qsTr("知道了"), kind: "primary" }]
        onChosen: (key) => { open = false; if (key === "logs") app.openLogFolder() }
        Connections { target: app; function onImportFinished(r) { importProgressDialog.forced = false; importReportDialog.show(r) } }
    }
    function sampleReport() {
        return { ok: true, plans: 52, stops: 456, photos: 131, journal: 0, plansSkipped: 0, backupName: "",
                 missingCount: 2, missing: ["photos/p_dali_lake/cover.jpg", "photos/p_yunnan/01.jpg"],
                 unusedCount: 3, unknownCount: 1, unknown: [qsTr("计划") + ".mood"] }
    }

    // 别的旅行计划开着这份数据时"仍然编辑"
    MessageDialog {
        id: takeOverDialog
        cardWidth: 480
        title: qsTr("仍然在这里编辑？")
        paragraphs: [qsTr("%1 正开着这份数据。两边都改的话，后保存的会覆盖先保存的（被覆盖的那一版存成 travel.backup.json，可以找回）。").arg(app.lockOwner !== "" ? app.lockOwner : qsTr("%1 上的旅行计划 %2").arg("LAPTOP-JASON").arg("0.6.0")),
                     qsTr("建议先把那边关掉。")]
        buttons: [{ key: "cancel", text: qsTr("取消") }, { key: "take", text: qsTr("仍然编辑"), kind: "dangerOutline" }]
        onChosen: (key) => { open = false; if (key === "take") app.takeOverLock() }
    }

    TextInputDialog {
        id: titleDialog
        title: qsTr("修改窗口标题")
        hint: qsTr("显示在“旅行计划 v版本号”后面，留空就不显示。")
        onAccepted: (t) => { app.windowTitleExtra = t; open = false }
        onCanceled: open = false
    }

    Timer { id: tourShot; interval: 900; onTriggered: { win.startTour(); if (shotPage === "ui-tour-3") tour.go(2) } }
    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        onErrorRaised: (msg) => win.showError(msg)
    }

    // ---- 0.12.8 新手引导（S-17）、升级提示（S-18）----
    Tour {
        id: tour
        steps: [{ target: topBar.pillItem, text: qsTr("计划、日历、相册、游记都在这里") },
                { target: topBar.newItem, text: qsTr("点这里新建一个旅行计划") },
                { target: layers, text: qsTr("换底图：卫星图、街道图、浅灰、已解锁") },
                { target: locBtn, text: qsTr("一键回到你现在的位置") }]
        onStartRequested: win.startTour()
    }
    function startTour() {
        if (editor.open) { toast.show(qsTr("请先完成或放弃当前编辑"), false); return }
        win.goPage("plan")
        win.nowOpen = false
        win.albumOpen = false
        plans.setLibraryOpen(false)
        mapc.markers.clearSelection()
        tour.start()
    }
    function openChangelog() { settings.section = "log"; win.goPage("settings") }
    function showUpdated() { toast.show(qsTr("已更新到 v%1").arg(app.version), false, qsTr("看看改了什么"), () => win.openChangelog(), false, 12000) }
    // 第一次回到地图（欢迎页、启动时的对话框都关了）才问，照 Electron：没看过引导就问引导，看过了才提示升级
    property bool onboardDone: false
    Timer {
        interval: 1200
        repeat: true
        running: !testRun && !win.onboardDone
        onTriggered: {
            if (!win.onMapPage || win.modalOpen || palette.open || editor.open) return
            win.onboardDone = true
            const last = app.noteVersion()
            if (!app.tourDone) tour.offer()
            else if (last !== "" && last !== app.version) win.showUpdated()
        }
    }

    // ---- 0.12.8 报错和"东西不见了"：不自己消失，盖在弹窗上面（用弹窗时出的错也看得见） ----
    property string errText: ""
    property string errAction: ""
    property var errFn: null
    property bool errIsSave: false
    function showError(text, actText, actFn) {
        errText = text
        errAction = actText || ""
        errFn = actFn || null
        errIsSave = false
    }
    function clearError() { errText = ""; errAction = ""; errFn = null; errIsSave = false }
    Connections { target: app; function onDirtyChanged() { if (!app.dirty && win.errIsSave) win.clearError() } }
    Column {
        id: alerts
        x: Math.round((win.width - width) / 2)
        y: win.topH + 2
        width: Math.min(Theme.px(620), win.width - 32)
        spacing: 12
        z: 210
        Banner {
            objectName: "dataGoneBanner"
            liquid: false
            width: parent.width
            visible: app.dataGone || shotPage === "ui-gone"
            warning: true
            closable: false
            title: qsTr("数据文件夹不见了")
            detail: qsTr("%1 里的 travel.json 找不到了，可能是文件夹被移动、改名，或者移动硬盘没插。窗口里的内容还在；放回原位置会自动恢复，也可以换一个文件夹。").arg(app.dataDir)
            TpButton { objectName: "dataGoneSwitch"; text: qsTr("换文件夹…"); compact: true; onClicked: folderDialog.open() }
        }
        Banner {
            objectName: "errorBanner"
            liquid: false
            width: parent.width
            visible: win.errText !== ""
            warning: true
            title: win.errText
            onCloseClicked: win.clearError()
            TpButton {
                objectName: "errorRetry"
                visible: win.errAction !== ""
                text: win.errAction
                compact: true
                kind: "primary"
                onClicked: { const f = win.errFn; win.clearError(); if (f) f() }
            }
        }
    }

    Connections {
        target: app
        function onToast(text, error) { toast.show(text, error) }
        function onSaveFailed(text) { win.showError(text, qsTr("再试一次"), () => app.saveNow(false)); win.errIsSave = true }
        function onSaveConflict() { conflictDialog.open = true }
        function onCloseSaveFailed(conflict, error) {
            closeFailDialog.conflict = conflict
            closeFailDialog.error = error
            closeFailDialog.open = true
        }
    }

    // 0.10.5 看大图：盖住整个窗口（在 modalOpen 里，所以 Esc、← → 都归它）
    Lightbox { id: lightbox; z: 1000 }

    // ---- 快捷键（和 Electron 一样；F12 改成打开日志文件夹，F5 / Ctrl+R 不做任何事） ----
    Shortcut { sequence: "F11"; onActivated: win.toggleFullscreen() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: app.requestQuit() }
    Shortcut { sequence: "F12"; onActivated: app.openLogFolder() }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: app.zoom(1) }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: app.zoom(-1) }
    Shortcut { sequence: "Ctrl+0"; onActivated: app.zoom(0) }
    Shortcut { sequences: ["F5", "Ctrl+R"]; onActivated: {} }
    // ---- Esc 一层一层关（0.8.8，S-14；PO 选了改进 a：只用一个处理器，按顺序问每一层开着没有） ----
    // 顺序照 Electron：命令面板 →（引导）→ ⋯ 泡泡 → 图层泡泡 → 筛选下拉 →（大图、更新日志、编辑器）→ 简易面板 → 回地图；
    // 括号里的还没做，做好时插进这里（大图 0.10.5 算弹框，自己处理 Esc）。原生版加了：头像菜单（⋯ 之后）、"此刻"卡片（简易面板之前）、我的旅程列表（简易面板之后）。
    // 弹框开着时 Esc 交给弹框自己。
    readonly property var escChain: [
        { name: "palette", on: () => palette.open, close: () => palette.hide() },
        // 0.12.8 引导：进行中算弹框（modalOpen，Esc 归 Tour 自己）；这里是开始前问的那张小卡片
        { name: "tourOffer", on: () => tour.offering, close: () => tour.skipOffer() },
        { name: "sub", on: () => topBar.subIsOpen, close: () => topBar.closeSub() },
        { name: "bubbles", on: () => topBar.bubblesOpen, close: () => topBar.setBubbles(false) },
        { name: "menu", on: () => avatarMenu.open, close: () => { avatarMenu.open = false } },
        { name: "layers", on: () => layers.open, close: () => layers.setOpen(false) },
        { name: "filter", on: () => filterMenu.open, close: () => { filterMenu.open = false } },
        { name: "now", on: () => nowCard.visible, close: () => { win.nowOpen = false } },
        // 0.10.0：就地改日程 → 计划页的 ⋯（计划页本身由最后的 page 关）
        { name: "sched", on: () => (win.tripOpen && planPanel.scheduleEditing) || (editor.open && planEditor.scheduleEditing), close: () => { if (win.tripOpen) planPanel.cancelSchedule(); else planEditor.cancelSchedule() } },
        { name: "tripMenu", on: () => win.tripOpen && planPanel.menuOpen, close: () => { planPanel.menuOpen = false } },
        // 0.8.9：样式选择器 → 点选模式 → 搜索结果 → 编辑器（Electron：编辑器在简易面板前面，按 Esc = 关闭编辑器）
        { name: "style", on: () => stylePop.visible, close: () => stylePop.close() },
        { name: "pick", on: () => editor.open && editor.pickMode, close: () => { editor.pickMode = false } },
        { name: "search", on: () => editor.open && editor.searchState !== "idle", close: () => editor.clearSearch() },
        { name: "rowMenu", on: () => rowMenu.visible, close: () => rowMenu.hide() },
        { name: "editor", on: () => editor.open, close: () => win.finishEditor() },
        { name: "sheet", on: () => win.mapShown && mapc.markers.selectedPlan !== "", close: () => mapc.markers.clearSelection() },
        // 0.11.2 日历：拖动中 → 弹出卡、选年月（页面本身由最后的 page 关）
        { name: "calDrag", on: () => nav.page === "calendar" && calendarPage.dragging, close: () => calendarPage.cancelDrag() },
        { name: "calCard", on: () => nav.page === "calendar" && calendarPage.popupOpen, close: () => calendarPage.closePopups() },
        { name: "library", on: () => planList.open && plans.libraryOpen, close: () => plans.setLibraryOpen(false) },
        { name: "album", on: () => planList.open && win.albumOpen, close: () => { win.albumOpen = false } },
        { name: "list", on: () => planList.open, close: () => { win.listOpen = false } },
        // 0.8.10：整页 / 设置 → 回到进来之前的模式；想去 / 足迹 / 照片 → 回"计划"
        { name: "page", on: () => nav.page !== "plan", close: () => win.goPage(win.onMapPage ? "plan" : "map") }
    ]
    // 返回关掉的是哪一层（自检用）；什么都没关返回空
    function escOnce() {
        if (win.modalOpen) return ""
        for (const l of escChain) if (l.on()) { l.close(); return l.name }
        return ""
    }
    Shortcut {
        sequence: "Esc"
        enabled: !win.modalOpen
        onActivated: win.escOnce()
    }
    // 地图上选中的计划（0.8.2 过渡做法，0.8.6 接上简易计划面板）：← → 在路线里切站
    // （Electron：输入框里打字、弹框 / 大图打开、编辑中、不在地图页时不触发）
    readonly property bool mapKeys: !win.globeMode && win.onMapPage && !win.modalOpen && !win.textFocused && !palette.open
    // Ctrl K：打开 / 关闭命令面板（S-12）；Ctrl B：我的旅程（S-08）；Alt ← / →、鼠标侧键：后退 / 前进（S-13）
    Shortcut {
        sequence: "Ctrl+K"
        enabled: !win.modalOpen
        onActivated: palette.open ? palette.hide() : win.openPalette("")
    }
    Shortcut {
        sequence: "Ctrl+B"
        enabled: !win.modalOpen && !palette.open
        onActivated: win.toggleList()
    }
    // Ctrl 1–4：切地图的四种模式（0.8.10，PO 选改进 b；Electron 没有）
    Shortcut { sequence: "Ctrl+1"; enabled: !win.modalOpen && !palette.open; onActivated: win.goPage("plan") }
    Shortcut { sequence: "Ctrl+2"; enabled: !win.modalOpen && !palette.open; onActivated: win.goPage("wish") }
    Shortcut { sequence: "Ctrl+3"; enabled: !win.modalOpen && !palette.open; onActivated: win.goPage("foot") }
    Shortcut { sequence: "Ctrl+4"; enabled: !win.modalOpen && !palette.open; onActivated: win.goPage("photo") }
    Shortcut {
        sequence: "Alt+Left"
        enabled: !win.modalOpen && !palette.open
        onActivated: nav.back()
    }
    Shortcut {
        sequence: "Alt+Right"
        enabled: !win.modalOpen && !palette.open
        onActivated: nav.forward()
    }
    Connections {
        target: app
        function onNavButton(dir) { if (win.modalOpen || palette.open) return; if (dir < 0) nav.back(); else nav.forward() }
    }
    Shortcut {
        sequence: "Left"
        enabled: win.mapKeys && mapc.markers.canStep
        onActivated: mapc.markers.stepStop(-1)
    }
    Shortcut {
        sequence: "Right"
        enabled: win.mapKeys && mapc.markers.canStep
        onActivated: mapc.markers.stepStop(1)
    }
    // 0.10.0：计划页里 ← → 切站（Electron：输入框里打字、弹框开着时不触发）
    readonly property bool tripKeys: win.tripOpen && planPanel.stepping && planPanel.tab === "itinerary" && !win.modalOpen && !win.textFocused && !palette.open
    Shortcut { sequence: "Left"; enabled: win.tripKeys; onActivated: planPanel.step(-1) }
    Shortcut { sequence: "Right"; enabled: win.tripKeys; onActivated: planPanel.step(1) }
    Connections {
        target: mapc
        function onLeftUnlock(base) {
            const b = win.bases.find(x => x.key === base)
            toast.show(qsTr("已切回「%1」底图").arg(b ? b.name : base), false)
        }
        function onToast(text, warn) { toast.show(text, warn) }
    }
    // 撤销 / 重做（0.5.0，D-13）：光标在输入框里时不拦截（交给输入框自己的撤销），有弹框时也不拦截
    readonly property bool modalOpen: welcomePage.visible || tour.active || dataChoice.open || noDataDialog.open || conflictDialog.open || closeFailDialog.open
                                      || restoreDialog.open || titleDialog.open || crashDialog.open || electronFolderDialog.open
                                      || importDialog.open || importProgressDialog.open || importReportDialog.open || takeOverDialog.open
                                      || photoReview.open || cleanupDialog.open || planConfirm.open || bulkConfirm.open || wishDialog.open || editorDialog.open
                                      || daysDialog.open || startDialog.open
                                      || bookingDialog.open || expenseDialog.open || budgetDialog.open || peopleDialog.open
                                      || emergencyDialog.open || docDialog.open || guideLinkDialog.open || photoPickDialog.open || tplDialog.open
                                      || journalDeleteDialog.open || journalPhotoPick.open || journalLinkDialog.open
                                      || lightbox.open || photoCaptionDialog.open || photoRemoveDialog.open || coverPickDialog.open
    readonly property bool textFocused: activeFocusItem !== null && activeFocusItem.cursorPosition !== undefined
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: !win.modalOpen && !win.textFocused
        onActivated: app.undo()
    }
    Shortcut {
        sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]
        enabled: !win.modalOpen && !win.textFocused
        onActivated: app.redo()
    }
    // 开发版：原样保存一次（只改注释头里的"最后保存："时间），用来实测保存流程和冲突弹框。打包版没有
    Shortcut {
        sequence: "Ctrl+Alt+Shift+S"
        enabled: app.devTools
        onActivated: app.saveNow(false)
    }

    // 截图测试 ui-devtest（只在 --screenshot 时）：连点两下测试改动 → 自动保存一次 → 再改一下 → 撤销 → 重做 → 撤销 → 备份
    Timer {
        running: shotPage === "ui-devtest"
        interval: 1200; repeat: true
        property int step: 0
        onTriggered: {
            const s = step++
            if (s === 0) { app.devTestChange(); app.devTestChange() }
            else if (s === 1) app.devTestChange()
            else if (s === 2) app.undo()
            else if (s === 3) app.redo()
            else if (s === 4) app.undo()
            else if (s === 5) { app.backupNow(); stop() }
        }
    }

    Timer {
        id: importE2E
        interval: 800
        onTriggered: { const r = app.importElectron(app.electronDataDir, "replace"); if (!r.ok) toast.show(r.error, true) }
    }
    // 截图（0.7.0）：ui-photo-import 把 tests/photos 里的照片导入（停在确认表）
    Timer {
        id: photoImportShot
        interval: 500
        onTriggered: { const r = photos.startImport(photoShotFiles); if (!r.ok) toast.show(r.error, true) }
    }
    // 云端端到端 ui-photo-e2e：导入（确认表出来后直接导入）→ 撤销 → 重做 → 转第一张能转的 → 扫描清理 → 移到回收站；看 app.log 和数据文件夹
    Connections {
        target: photos
        enabled: shotPage === "ui-photo-e2e"
        function onImportChanged() { if (photos.importStage === "review") Qt.callLater(photos.confirmImport) }
        function onImportFinished(r) { console.log("e2e import", JSON.stringify(r)); photoE2E.start() }
        function onTrashFinished(r) { console.log("e2e trash", JSON.stringify(r)) }
    }
    Timer {
        id: photoE2E
        interval: 300; repeat: true
        property int step: 0
        onTriggered: {
            const s = step++
            if (s === 0) app.undo()
            else if (s === 1) { console.log("e2e after undo", photos.count); app.redo() }
            else if (s === 2) {
                console.log("e2e after redo", photos.count)
                const l = photos.library()
                for (let i = 0; i < l.length; ++i) if (l[i].canRotate && l[i].file.endsWith(".png")) { console.log("e2e rotate", l[i].file, JSON.stringify(photos.rotate(l[i].id, 1))); break }
            } else if (s === 3) {
                const r = photos.scanCleanup()
                console.log("e2e scan", JSON.stringify(r))
                const rels = []
                for (let i = 0; i < (r.unused || []).length; ++i) rels.push(r.unused[i].rel)
                for (let j = 0; j < (r.orphans || []).length; ++j) rels.push(r.orphans[j].rel)
                if (rels.length) console.log("e2e trash start", JSON.stringify(photos.trashFiles(rels)))
                stop()
            }
        }
    }
    Timer {
        id: photoCleanShot
        interval: 800
        onTriggered: win.startCleanup()
    }
    Timer {   // 截图 morph-ref（0.9.7）：第一次有数据时平面会自动框住所有站点，之后再跳到和 morph-flat 一样的镜头
        id: morphRefShot
        interval: 2000
        onTriggered: mapc.jumpTo(31.2, 121.5, 5.5)
    }
    Timer {   // 截图 globe-markers / globe-select（0.9.5）：数据和地球都好了以后再框住 / 选中
        id: globeShot
        interval: 700
        onTriggered: {
            mapc.dismissTileBanner()
            if (shotPage === "globe-markers") { mapc.fitAll(false); return }
            const id = mapc.globeDemoPlan()
            if (id === "") return
            mapc.markers.selectPlan(id, -1, false)
            plans.openPlan(id, false)
            mapc.flyToPlan(id)
            vehicleShot.start()
        }
    }
    Timer {   // 地球转好以后把小图标放在第一段飞行的中间
        id: vehicleShot
        interval: 1400
        onTriggered: globe.vehicleAtMs(1200)
    }
    Timer {   // 截图 ui-error：把最上面那张展开
        id: expandFirstError
        interval: 300
        onTriggered: { const c = bottomCards.children[0]; if (c && c.expanded !== undefined) c.expanded = true }
    }

    // 刷新率：每秒统计一次 frameSwapped（拖动、动画时才有意义）
    property int frames: 0
    onFrameSwapped: { frames++; coverFrame() }
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: { win.fps = win.frames; win.frames = 0 }
    }

    // ---- 截图模式：按页面设好状态 ----
    Component.onCompleted: {
        if (shotPage === "globe") { mapc.base = "sat"; globe.centerLat = 15; globe.centerLng = globe.sunLng() + 80; globeMode = true }   // 对准黄昏一侧，能看到晨昏线
        // 0.9.0：拉近的卫星图（看按视野加载的细一级瓦片）。0.9.2 起地球上没有已解锁，globe-unlock 这一页去掉了
        // 0.9.3：此刻天最黑的大城市附近（看夜里的城市灯光）
        // 0.9.5：城市灯光只在缩到最远时显示（PO 11:05），这一页拉到最远
        else if (shotPage === "globe-night") { const c = globe.nightCity(); mapc.base = "sat"; globe.centerLat = c[0]; globe.centerLng = c[1]; globeMode = true; globe.setFromZoomLevel(0) }   // 0 级 = 夹到最远
        // 0.9.5：地球上的标记和路线（框住所有站点）/ 选中一条有长途飞行的路线（弧线、交通小图标）
        else if (shotPage === "globe-markers" || shotPage === "globe-select") { mapc.base = "sat"; globeMode = true; globeShot.start() }
        else if (shotPage === "globe-near") { mapc.base = "sat"; globe.centerLat = 31.2; globe.centerLng = 121.5; globeMode = true; globe.setFromZoomLevel(6) }
        // 0.9.7 变形：上海附近 5.5 级，摊到一半 / 摊平（和平面对照用 morph-ref：同一镜头的平面卫星图）
        else if (shotPage === "morph-half" || shotPage === "morph-flat") {
            mapc.base = "sat"; globe.centerLat = 31.2; globe.centerLng = 121.5; globeMode = true; globe.setFromZoomLevel(5.5)
            globe.setMorph(shotPage === "morph-half" ? 0.5 : 1)
        }
        else if (shotPage === "morph-ref") { mapc.base = "sat"; morphRefShot.start() }
        else if (shotPage === "ui-conflict") conflictDialog.open = true
        else if (shotPage === "ui-ext" || shotPage === "ui-ext-dirty") extCard.forced = true
        else if (shotPage === "ui-closefail") closeFailDialog.open = true
        else if (shotPage === "ui-closefail-error") { closeFailDialog.conflict = false; closeFailDialog.error = qsTr("拒绝访问。"); closeFailDialog.open = true }
        else if (shotPage === "ui-settings-docs" || shotPage === "ui-settings-doc-form") {
            const opt = planPage.documentPlanOptions("")
            const addDay = (n) => { const x = new Date(); x.setDate(x.getDate() + n); return Qt.formatDate(x, "yyyy-MM-dd") }
            if (planPage.documents().length === 0) {   // 截图的数据副本会留到下一张，只加一次
                planPage.saveDocument(-1, "", { kind: "passport", owner: "我", label: "中国护照", expires: addDay(-30) })
                planPage.saveDocument(-1, "", { kind: "visa", owner: "妈妈", label: "日本单次签证", expires: addDay(90), planId: opt.length ? opt[0].key : "", notes: "在领馆办的，出发前再看一眼" })
                planPage.saveDocument(-1, "", { kind: "other", label: "国际驾照" })
            }
            planPage.saveEmergency({ insurer: "Allianz", policyNo: "AZ-2041-77", n0: "李明", r0: "父亲", p0: "+86 138 0000 0000" })
            goPage("settings")
            if (shotPage === "ui-settings-doc-form") docDialog.show(planPage.documents()[1])
        }
        else if (shotPage === "ui-settings-net-off") { netc.offlineForShot(); goPage("settings") }
        else if (shotPage.startsWith("ui-ai-")) { ai.shotSetup(shotPage); aiPanel.open = true; if (shotPage === "ui-ai-order") aiPanel.orderMode = true }
        else if (shotPage.startsWith("ui-aiplan-")) { ai.shotSetup(shotPage); goPage("aiplan"); if (shotPage !== "ui-aiplan-guide") aiPlanPage.shotFill(Number(shotPage.slice(10))) }   // 0.13.2 向导每一步   // 0.13.0：AI 面板（对话 / 引导卡 / 第一次发送的提示）
        else if (shotPage === "ui-settings-ai") { ai.shotSetup(shotPage); goPage("settings") }
        else if (shotPage.startsWith("ui-settings-") && shotPage !== "ui-settings-photos") goPage("settings")
        else if (shotPage === "ui-settings" || shotPage === "ui-backup" || shotPage === "ui-backup-running" || shotPage === "ui-devtools") goPage("settings")
        else if (shotPage === "ui-firstrun") dataChoice.forced = "firstRun"
        else if (shotPage === "ui-welcome") welcomePage.forced = true
        else if (shotPage === "ui-tour" || shotPage === "ui-tour-3") tourShot.start()
        else if (shotPage === "ui-tour-offer") tour.offer()
        else if (shotPage === "ui-update") win.showUpdated()
        else if (shotPage === "ui-error") win.showError(qsTr("保存失败：") + "D:\\旅行计划数据\\travel.json" + "\n" + qsTr("改动还在窗口里没丢，可以再试一次。"), qsTr("再试一次"), () => {})
        else if (shotPage.startsWith("empty-")) emptyShot.start()
        else if (shotPage === "ui-missing") dataChoice.forced = "missing"
        else if (shotPage === "ui-nodata") { noDataDialog.dir = "D:\\新文件夹"; noDataDialog.open = true }
        else if (shotPage === "ui-restore") { restoreDialog.entry = { name: "travel-2026-10-30.json", date: "2026-10-30", plans: 51, size: 1300000 }; restoreDialog.open = true }
        else if (shotPage === "ui-import") { importDialog.src = "D:\\旅行计划数据"; importDialog.open = true }
        else if (shotPage === "ui-import-running") importProgressDialog.forced = true
        else if (shotPage === "ui-import-report") importReportDialog.show(win.sampleReport())
        else if (shotPage === "ui-import-failed") importReportDialog.show({ ok: false, error: qsTr("data.js 读不出来：") + "第 12 行第 5 列：缺少逗号" })
        else if (shotPage === "ui-electron-folder") { electronFolderDialog.dir = "D:\\旅行计划数据"; electronFolderDialog.open = true }
        else if (shotPage === "ui-takeover") takeOverDialog.open = true
        else if (shotPage === "ui-import-e2e") importE2E.start()   // 云端端到端：用 TP_ELECTRON_SETTINGS 指的数据真的导入一次
        else if (shotPage === "ui-title") { titleDialog.text = "我的旅行"; titleDialog.open = true }
        else if (shotPage === "ui-banner") {}
        else if (shotPage === "ui-error") {   // 出错小框：两张叠在一起，上面那张展开
            errModel.append({ code: "TP-Q-3F9A", message: "TypeError: Cannot read property 'boom' of null", report: "" })
            errModel.append({ code: "TP-M-07KD", message: "开发版测试：故意抛出的 C++ 异常", report: "" })
            extCard.forced = true
            expandFirstError.start()
        }
        else if (shotPage === "ui-photo-import" || shotPage === "ui-photo-e2e") photoImportShot.start()
        else if (shotPage === "ui-photo-clean") photoCleanShot.start()
        else if (shotPage === "ui-settings-photos") goPage("settings")
        else if (shotPage === "ui-crash") crashDialog.crash = { code: "TP-CRASH-K2W7", dump: "crash-20261103-021530.dmp", report: "", gl: true }
        else if (shotPage === "markers" || shotPage === "spread" || shotPage === "pins") markerShot.start()
        else if (shotPage === "lines") lineShot.start()
        else if (shotPage === "roadbar") { mapc.base = "street"; roads.demoForShot(7) }
        else if (shotPage === "layers" || shotPage === "layers-sat") { mapc.base = shotPage === "layers" ? "street" : "sat"; layersShot.start() }
        else if (shotPage === "tilefail") { mapc.base = "street"; mapc.demoTileBanner() }
        else if (shotPage.startsWith("newbtn-grey")) mapc.base = shotPage.endsWith("-sat") ? "sat" : "street"   // 新建计划三档灰（TopBar newBtn.grey），PO 选
        else if (shotPage === "unlock-edit" || shotPage === "unlock-search") { mapc.base = "unlock"; unlockShot.start() }
        else if (shotPage === "now" || shotPage === "sheet" || shotPage === "filter" || shotPage.startsWith("filter-overlay")) planShot.start()
        else if (shotPage === "bubbles" || shotPage === "list" || shotPage === "palette" || shotPage === "menu") shellShot.start()
        else if (shotPage.startsWith("trip")) tripShot.start()
        else if (shotPage.startsWith("editor") || shotPage.startsWith("narrow") || shotPage === "locate" || shotPage === "controls") editShot.start()
        else if (shotPage.startsWith("calendar") || shotPage.startsWith("journal")) pagesShot.start()
        else if (shotPage.startsWith("mode") || shotPage.startsWith("ledger") || shotPage.startsWith("library") || shotPage.startsWith("wish-") || shotPage.startsWith("album") || shotPage.startsWith("photo-") || shotPage.startsWith("heic-")) modeShot.start()
        else if (shotPage !== "" && !shotPage.startsWith("ui-") && shotPage !== "selftest") mapc.base = shotPage
    }

    // 截图的步骤走完没有（0.10.5，main.cpp 的就绪判断调用）：步骤定时器、相机飞行、地图样式
    function shotBusy() {
        for (const t of [emptyShot, frameNextTimer, planShot, tripShot, tripShot2, shellShot, editShot, modeShot, pagesShot, pagesShot2, libShot, searchShot, styleShot, markerShot, markerTip, lineShot, lineTip, layersShot, unlockShot, unlockTip])
            if (t.running) return true
        if (shotPage.startsWith("calendar-season") && !(calendar.info.season || {}).ready) return true   // 等"去过"算好
        return mapc.moving || win.mapLive && mapLoader.status !== Loader.Error && !(win.map && win.map.styleLoaded)
    }

    // 截图（0.12.7）：empty-plan / wish / foot / album / calendar / ledger = 换到一份全新的空数据（副本旁边），打开那一页
    Timer {
        id: emptyShot
        interval: 1500
        onTriggered: {
            app.switchDataFolder(app.dataDir + "-empty", true)
            mapc.base = "street"
            const p = shotPage.substring(6)
            if (p === "plan") win.openLibrary()
            else if (p === "album") { win.goPage("photo"); win.listOpen = true; win.albumOpen = true }
            else { win.goPage(p); if (p === "wish" || p === "foot") win.listOpen = true }
        }
    }

    // 截图（0.8.7）：now = 一条带日期的路线设为下次出发、假装今天是第 3 天、打开"此刻"卡片；
    // sheet = 选中一条 ≥ 4 站的路线、聚焦第 2 站（简易计划面板，方案 C）；filter = 打开筛选下拉（0.9.7 起见下面）
    Timer {
        id: planShot
        interval: 2000
        onTriggered: {
            mapc.base = shotPage === "filter-overlay-sat" ? "sat" : "street"
            if (shotPage === "now") {
                const d = plans.demo()
                if (d.planId === undefined) { console.warn("截图 now：数据里没有带日期的路线"); return }
                plans.setNextTrip(d.planId, true)
                plans.setTodayOverride(d.today)
                win.nowOpen = true
                weather.fillDemoForShot()   // 0.10.3 L2
                frameNextTimer.start()
            } else if (shotPage === "sheet") {
                const r = mapc.markers.demo("route")
                if (r.planId === undefined) { console.warn("截图 sheet：数据里找不到 ≥ 4 站的路线"); return }
                plans.openPlan(r.planId, true, 1)
            } else {
                // 0.9.7：filter = 计划模式只画计划中的、打开筛选下拉（最下面是"叠上其他旅程"）；filter-overlay = 再把它打开
                plans.setTestAllStatuses(false)
                if (shotPage.startsWith("filter-overlay")) plans.setOverlay(true)
                filterMenu.open = true
            }
        }
    }

    // 截图（0.10.1）：trip-bookings = 交通住宿（含没订住宿的提醒）；trip-budget = 消费；trip-booking-form / trip-expense-form = 弹出表单；
    // trip-stopbooks = 站点卡片上当天的交通住宿。在数据副本上加几条示例
    // 0.10.2：trip-settle = 有同行人的消费页和结算；trip-split-form = 分账表单（按金额分）；trip-split-narrow = 500 宽的分账表单
    readonly property bool moneyShot: ["trip-expense-people", "trip-bookings", "trip-budget", "trip-booking-form", "trip-expense-form", "trip-stopbooks", "trip-settle", "trip-split-form", "trip-split-narrow", "trip-share-people", "trip-share-rows", "trip-share-on", "trip-share-off", "trip-share-edited"].indexOf(shotPage) >= 0
    // 0.10.5：trip-photos = 照片标签（有封面）；trip-lightbox = 看大图；trip-lightbox-zoom = 放大（原图）；*-narrow = 500 宽；editor-photos、editor-cover = 编辑器的照片、封面
    readonly property bool photosShot: ["trip-photos", "trip-photos-narrow", "trip-lightbox", "trip-lightbox-zoom"].indexOf(shotPage) >= 0
    function runPhotosShot() {
        planPage.seedPhotosForShot("")
        const l = planPage.info.photos || []
        if (l.length === 0) { console.warn("截图 " + shotPage + "：数据里没有照片"); return }
        photos.setCover(planPage.planId, l[1 % l.length].id)
        planPanel.openTab("photos")
        if (!shotPage.includes("lightbox")) return
        lightbox.show(planPage.info.photos, 2 % l.length)
        if (shotPage === "trip-lightbox-zoom") lightbox.zoomAt(2.5, 0, 0)
    }
    readonly property bool kitShot: ["trip-packing", "trip-packing-narrow", "trip-packing-paste", "trip-emergency-form", "trip-journal", "trip-guide", "trip-guide-narrow", "trip-guide-edit", "trip-guide-edit-narrow", "trip-guide-new"].indexOf(shotPage) >= 0
    function runKitShot() {
        planPage.seedKitForShot()
        const a = plans.today
        if (shotPage.startsWith("trip-packing") || shotPage === "trip-emergency-form") {
            planPanel.openTab("packing")
            const kit = planPage.info.kit
            if (kit.todos.length < 3) planPage.addChecks("todos", "办签证\n换汇\n订租车\n核实学期安排，确认 7–8 月能空出来")
            if (kit.packing.length === 0) planPage.addFromTemplate()
            const t = planPage.info.kit.todos, p = planPage.info.kit.packing
            const soon = new Date(a + "T12:00:00")
            soon.setDate(soon.getDate() + 2)
            planPage.setCheckDue(t[0].id, Qt.formatDate(soon, "yyyy-MM-dd"))
            if (t.length > 1) planPage.setCheckDue(t[1].id, "2020-01-01")
            if (p.length > 1) planPage.setCheckDone("packing", p[0].id, true)
            if (shotPage === "trip-packing-paste") planPanel.pasteForShot("徒步杖\n三季徒步鞋\n头灯")
            if (shotPage === "trip-emergency-form") emergencyDialog.showGlobal(planPage.info.kit)
            return
        }
        if (shotPage === "trip-journal") { planPanel.openTab("journal"); return }
        if (shotPage === "trip-guide-new") planPage.setGuide(planPage.info.id, "")
        else planPage.setGuide(planPage.info.id, win.shotGuide())
        planPanel.openTab("guide")
        if (shotPage.startsWith("trip-guide-edit")) planPanel.guideBeginEdit()
    }
    function shotGuide() {
        const ph = planPage.planPhotos()
        return "# 交通\n机场到市区约 40 分钟，**提前买票**。详情看 https://example.com/a?x=1&y=2\n\n- 地铁\n- 机场巴士\n\n## 换乘时要注意的几件事，这个标题很长很长很长很长很长很长很长很长很长很长很长很长\n> 末班车 23:30\n\n"
             + (ph.length ? "![海边](photo:" + ph[0].id + ")\n\n" : "")
             + "# 准备\n- [ ] 买转换插头\n- [x] 订第一晚的酒店\n- [ ] 下载离线地图\n\n## 吃的\n[市场](https://example.com/market) 早上去。\n\n## 住的\n老城区走路方便。"
    }
    function seedSplit() {
        if (((planPage.info.money || {}).people || []).length > 0) return   // 前一张截图已经加过（几张截图用同一份副本）
        const a = plans.today
        planPage.addPerson("小王", qsTr("我"))
        planPage.addPerson("小李", qsTr("我"))
        const ids = planPage.info.money.people.map(p => p.id)
        planPage.saveExpense("", { title: "Dinner", amount: "240", cat: "food", date: a, paidBy: ids[0], split: [] })
        planPage.saveExpense("", { title: "Tickets", amount: "90", cat: "ticket", date: a, paidBy: ids[1], split: [{ who: ids[0] }, { who: ids[1] }] })
        planPage.saveExpense("", { title: "Taxi", amount: "100", currency: "USD", cat: "transport", date: a, paidBy: ids[2], split: [{ who: ids[0], amount: "60" }, { who: ids[2], amount: "40" }] })
        planPage.saveExpense("", { title: "Coffee", amount: "12", cat: "food", date: a, paidBy: ids[1], split: [{ who: ids[1] }] })
    }
    // 0.11.5：trip-share-people = 同行人窗口；-rows = 消费行的每人金额；-on / -off = 单笔窗口开和关；-edited = 改了金额
    function runShareShot() {
        if (shotPage === "trip-share-people") { peopleDialog.show(); return }
        if (shotPage === "trip-share-rows") { planPanel.bottomForShot(); return }
        const rows = planPage.info.money.rows
        const row = rows.find(x => x.title === (shotPage === "trip-share-off" ? "Coffee" : "Dinner"))
        expenseDialog.show(row, null)
        if (shotPage === "trip-share-edited") expenseDialog.setAmount(row.among[1], "100")
    }
    function seedMoney() {
        if ((planPage.info.money || {}).budget === 3000) return   // 前一张截图已经加过（几张截图用同一份副本）
        const stops = planPage.info.stops || []
        const a = (stops[0] || {}).arriveDate || plans.today
        const b = (stops[0] || {}).departDate || a
        planPage.setBudget("3000", "")
        planPage.saveBooking("", { kind: "hotel", title: "Hilton Darwin", startDate: a, startTime: "15:00", endDate: b > a ? b : "", ref: "HX-2041", cost: "620", notes: "含早餐" })
        planPage.saveBooking("", { kind: "flight", title: "QF 840", from: "Melbourne", to: "Darwin", startDate: a, startTime: "07:30", endDate: a, endTime: "11:05", cost: "410", toExp: true })
        planPage.saveExpense("", { title: "Lunch", amount: "28", cat: "food", date: a })
        planPage.saveExpense("", { title: "Museum", amount: "35", currency: "USD", cat: "ticket", date: a })
        planPage.saveExpense("", { title: "Tour refund", amount: "-40", cat: "ticket", date: b })
    }
    // 截图（0.10.0）：trip = 计划页（今天在这趟里、有季节条的话用有季节条的）；trip-today = 此刻卡进来（滚到今天）；trip-scroll = 往下滚（页头收起）；
    // trip-edit = 就地改一条日程；trip-narrow = 500 宽；trip-single = 单一目的地；trip-sketch = 没有封面
    Timer {
        id: tripShot
        interval: 2000
        onTriggered: {
            mapc.base = "street"
            plans.setTestAllStatuses(false)
            const d = plans.demo()
            if (d.planId === undefined) { console.warn("截图 trip：数据里没有带日期的路线"); return }
            plans.setTodayOverride(d.today)
            if (shotPage === "trip-today") plans.setNextTrip(d.planId, true)
            if (shotPage === "trip-narrow" || shotPage === "trip-split-narrow" || shotPage === "trip-alt-narrow" || shotPage.endsWith("-narrow") && (win.kitShot || win.photosShot)) win.width = 500
            let id = d.planId
            if (shotPage === "trip-sketch") id = mapc.markers.demo("route").planId || id   // 没有照片、没有日期：路线简图、预计天数
            if (shotPage === "trip-single") {
                plans.setTestAllStatuses(true)
                for (const x of plans.list.rows || []) if (x.type === "single" && x.stops > 0) { id = x.id; break }
            }
            win.openTrip(id, shotPage === "trip-today" ? plans.now.hereIndex || 0 : -1)
            if (shotPage === "trip-scroll") tripShot2.start()
            if (shotPage === "trip-edit") tripShot2.start()
            // 0.10.3：trip-weather = 高反提示 + 页头；trip-elev = 站点卡片（天气、气候、海拔就地改）；trip-alt-narrow = 500 宽。开发数据不在预报范围里，填示例
            if (shotPage === "trip-weather" || shotPage === "trip-elev" || shotPage === "trip-alt-narrow") {
                const elev = [1000, 3000, 5000, 7000, 9000]
                let n = 0
                const list = planPage.info.stops || []
                for (let k = 0; k < list.length && n < elev.length; ++k)
                    if (Fmt.nights(list[k].arriveDate, list[k].departDate) > 0) planPage.setElev(k, String(elev[n++]))
                weather.fillDemoForShot()
                if (shotPage === "trip-elev") tripShot2.start()
            }
            // 0.10.4：trip-packing = 行李与待办（M1、M2、紧急信息）；trip-packing-paste = M4；trip-emergency-form；trip-journal；trip-guide = 读；trip-guide-edit = 写和预览；
            // trip-guide-new = 空攻略；*-narrow = 500 宽。数据是副本，改了不要紧
            if (win.kitShot || win.photosShot) tripShot2.start()
            if (win.moneyShot) {
                win.seedMoney()
                if (shotPage === "trip-settle" || shotPage.startsWith("trip-split") || shotPage.startsWith("trip-share")) win.seedSplit()
                tripShot2.start()
            }
        }
    }
    Timer {
        id: tripShot2
        interval: 600
        onTriggered: {
            if (shotPage === "trip-scroll") { planPanel.pickStop(2); planPanel.reveal(2, true); return }
            if (shotPage === "trip-elev") { planPanel.reveal(1, true); planPanel.beginElev(1); return }
            if (win.kitShot) { win.runKitShot(); return }
            if (win.photosShot) { win.runPhotosShot(); return }
            if (win.moneyShot) {
                planPanel.openTab(shotPage === "trip-bookings" || shotPage === "trip-booking-form" ? "bookings" : shotPage === "trip-stopbooks" ? "itinerary" : "budget")
                if (shotPage === "trip-booking-form") bookingDialog.show(null, "hotel")
                if (shotPage === "trip-expense-form" || shotPage === "trip-expense-people") expenseDialog.show(null, { title: "", cat: "food" })
                if (shotPage === "trip-expense-people") expenseDialog.peoplePage = true
                if (shotPage.startsWith("trip-share")) win.runShareShot()
                if (shotPage.startsWith("trip-split")) {
                    for (const x of planPage.info.money.rows) if (x.custom) { expenseDialog.show(x, null); break }
                }
                return
            }
            planPanel.beginFirstEdit()
        }
    }
    // 截图（0.8.8）：bubbles = 指定下次出发后打开 ⋯ 泡泡；list = 打开我的旅程、选中一条路线、隐藏一个计划；
    // palette = 两个计划算"最近打开过"，打开 Ctrl K（没输入）；menu = 打开头像菜单
    Timer {
        id: shellShot
        interval: 2000
        onTriggered: {
            mapc.base = "street"
            const d = plans.demo()
            if (d.planId !== undefined) { plans.setNextTrip(d.planId, true); plans.setTodayOverride(d.today) }
            if (shotPage === "bubbles") {
                topBar.setBubbles(true)
            } else if (shotPage === "list") {
                win.listOpen = true
                const r = mapc.markers.demo("route")
                if (r.planId !== undefined) plans.openPlan(r.planId, true)
                const rows = plans.list.rows || []
                for (const x of rows) if (x.status === "completed" && x.id !== r.planId) { plans.setHidden(x.id, true); break }
            } else if (shotPage === "palette") {
                const r = mapc.markers.demo("route")
                if (r.planId !== undefined) plans.noteRecent(r.planId)
                if (d.planId !== undefined) plans.noteRecent(d.planId)
                win.openPalette("")
            } else if (shotPage === "menu") {
                avatarMenu.open = true
            }
        }
    }

    // 截图（0.8.9）：editor = 编辑一条 ≥ 4 站的路线、展开第 2 站；editor-new = 新建（信息卡片展开）；
    // editor-search = 编辑 + 一组示例搜索结果（不联网，地图上也标出来）；editor-style = 打开计划的样式浮层；
    // locate = 假装拿到了位置（精度 3 公里）；controls = 街道图上的 ± / 比例尺 / 图例 / 我的位置；
    // narrow = 窗口 500 宽 + 简易面板变抽屉；narrow-editor = 500 宽 + 编辑器抽屉；narrow-menu = 1000 宽 + 头像菜单（多了页面入口）
    // （0.8.10 抽屉断点从 768 改成 520，截图从 700 宽改成 500 宽）
    Timer {
        id: editShot
        interval: 2000
        onTriggered: {
            mapc.base = "street"
            if (shotPage.startsWith("narrow")) win.width = shotPage === "narrow-menu" ? 1000 : 500
            const r = mapc.markers.demo("route")
            if (shotPage === "editor-new") { win.newPlan(); return }
            if (shotPage === "locate") { locator.simulateFix(r.lat !== undefined ? r.lat : 35.0, r.lng !== undefined ? r.lng : 135.7, 3000); return }
            if (shotPage === "controls") { mapc.jumpTo(35.0, 135.7, 6); return }
            if (shotPage === "narrow-menu") { avatarMenu.open = true; return }
            if (r.planId === undefined) { console.warn("截图 " + shotPage + "：数据里找不到 ≥ 4 站的路线"); return }
            if (shotPage === "narrow") { plans.openPlan(r.planId, true, 1); return }
            const edPhotos = shotPage === "editor-photos" || shotPage === "editor-cover"   // 0.10.5 编辑器的照片；封面在计划信息卡片上
            if (edPhotos) planPage.seedPhotosForShot(r.planId)
            win.editPlan(r.planId)
            editor.activeStop = shotPage === "editor-photos" ? 0 : shotPage === "editor-cover" ? -1 : 1
            if (edPhotos) { editor.setField("cover", photos.planPhotos(r.planId)[0].id); planEditor.infoOpen = true }
            if (shotPage === "editor-search") searchShot.start()   // 等 editPlan 的 fitPlan 先走完
            if (shotPage === "editor-style") styleShot.start()
        }
    }
    // 截图（0.8.10）：modes = 打开 ⋯（8 颗泡泡）；modes-narrow = 640 宽时 ⋯ 排两行；mode-foot / mode-wish / mode-photo = 切到那种模式、
    // 打开列表；ledger / ledger-narrow = 账本。会改数据副本（设为下次出发）
    Timer {
        id: modeShot
        interval: 2000
        onTriggered: {
            mapc.base = "street"
            const d = plans.demo()
            // ledger-now（0.10.5）= 真实的今天：看"已花"几乎是 0 时的样子
            if (d.planId !== undefined && shotPage !== "ledger-now") { plans.setNextTrip(d.planId, true); plans.setTodayOverride(d.today) }
            if (shotPage.startsWith("album") || shotPage.startsWith("photo-") || shotPage.startsWith("heic-") || shotPage === "mode-photo") {
                // 0.11.1：mode-photo = 地图 + 窄列表；photo-trip = P1 点了一趟；photo-lightbox = 看大图（位置来自站点）；
                // album / album-year / album-fav / album-inbox / album-narrow = 相册整页；
                // heic-album / heic-lightbox = 真的解 HEIC；heic-off-album / heic-off-lightbox = 没装扩展（假装）
                plans.setTestAllStatuses(false)
                if (shotPage.startsWith("heic-off")) photos.setHeicTestOff(true)   // 几张图共用一份副本：进相册之前就假装没装，不然先读出缩略图
                win.goPage("photo")
                win.listOpen = true
                photoModeShot.start()
            } else if (shotPage.startsWith("library")) {
                // 0.11.0：library（画廊）/ library-table / library-region / library-select / library-narrow / library-hover（鼠标在第一张卡片上）；
                // wish-start = 开始计划的对话框；mode-wish / mode-foot 走下面的 else
                plans.setTestAllStatuses(false)
                if (shotPage === "library-narrow") win.width = 640
                win.listOpen = true
                plans.setLibraryOpen(true)
                if (shotPage === "library-table") plans.setLibraryPref("view", "table")
                if (shotPage === "library-region") plans.setLibraryPref("view", "region")
                if (shotPage === "library-select") { plans.setLibraryPref("view", "table"); libShot.start() }
                if (shotPage === "library-hover") libShot.start()
            } else if (shotPage === "wish-start" || shotPage === "wish-season") {
                plans.setTestAllStatuses(false)
                win.goPage("wish")
                win.listOpen = true
                const w = (plans.list.rows || [])[0]
                if (w) { if (shotPage === "wish-start") wishDialog.showStart(w.id); else wishDialog.showSeason(w.id) }
            } else if (shotPage === "modes" || shotPage === "modes-narrow" || shotPage === "modes-sub" || shotPage === "modes-sub-narrow") {
                if (shotPage.endsWith("narrow")) win.width = 640
                // 0.11.6：modes-sub = 横条展开后再展开计划的竖列（计划库、想去、足迹、下次出发）
                Qt.callLater(() => { topBar.setBubbles(true); if (shotPage.startsWith("modes-sub")) topBar.openSub("plan") })
            } else if (shotPage.startsWith("ledger")) {
                if (shotPage === "ledger-narrow") win.width = 500
                win.goPage("ledger")
            } else {
                plans.setTestAllStatuses(false)
                win.goPage(shotPage.slice(5))
                win.listOpen = true
            }
        }
    }
    // 0.11.2 截图。calendar = 年视图；calendar-month；calendar-card = 弹出卡；calendar-drag = 拖到一半（有撞期）；calendar-notice = 拖完的提示；
    // calendar-picker = 选年月；calendar-narrow；journal = 列表；journal-empty；journal-read；journal-edit；journal-edit-narrow。都在副本上
    Timer {
        id: pagesShot
        interval: 400
        onTriggered: {
            if (shotPage.endsWith("-narrow")) win.width = 620
            if (shotPage === "calendar-settings") {   // 设置页：一周从哪天开始
                win.goPage("settings")
                pagesShot2.start()
                return
            }
            if (shotPage.startsWith("calendar")) {
                plans.setTestAllStatuses(false)
                win.goPage("calendar")
                if (shotPage === "calendar-month" || shotPage === "calendar-narrow" || shotPage === "calendar-month-sun") calendar.showDate("2026-12-05")
                if (shotPage.endsWith("-sun")) calendar.weekStart = 0
                if (shotPage.startsWith("calendar-week")) {   // 周视图：路线的第 3 天，假装那天是今天（有当前时间线）
                    const wd = plans.demo()
                    if (wd.today) { plans.setTodayOverride(wd.today); calendar.showWeek(wd.today) }
                    else calendar.view = "week"
                }
                if (shotPage.startsWith("calendar-season-20")) plans.setTodayOverride(shotPage.slice(-4) + "-06-15")   // 一年两段斋月（T1）
                if (shotPage.startsWith("calendar-season")) calendar.view = "season"
                if (shotPage.startsWith("calendar-season-20")) {   // 只留有斋月的两组，一屏看得到
                    calendar.setAllSeasonGroups(true)
                    calendar.setSeasonGroupClosed("东南亚", false)
                    calendar.setSeasonGroupClosed("西亚/中亚", false)
                }
            } else {
                win.goPage("journal")
                if (shotPage === "journal-empty") for (const id of (journal.page.rows || []).map(r => r.id)) journal.remove(id)   // 几张截图用同一份副本
                else win.seedJournalShot()
            }
            pagesShot2.start()
        }
    }
    Timer {
        id: pagesShot2
        interval: 500
        onTriggered: {
            const bars = (calendar.info.year || {}).bars || []
            const busy = bars.filter(b => !b.done && !b.narrow)
            const any = bars.filter(b => !b.done)
            const pick = busy.length ? busy[0] : any.length ? any[0] : null
            if (shotPage === "calendar-week-events") calendarPage.scrollWeekForShot()
            const wkBars = ((calendar.info.week || {}).bars || []).filter(b => b.kind === "plan")
            if (shotPage === "calendar-week-drag" && wkBars.length) calendarPage.dragForShot(wkBars[0].id, Fmt.addDays(wkBars[0].start > calendar.weekFirst ? wkBars[0].start : calendar.weekFirst, 2), wkBars[0].start > calendar.weekFirst ? wkBars[0].start : calendar.weekFirst, 620, 300)
            if (shotPage === "calendar-card" && pick) calendarPage.showCardAt(pick.id, 320, 170)
            if (shotPage === "calendar-picker") calendarPage.showPickerAt(320, 60)
            if (shotPage === "calendar-drag" && pick) calendarPage.dragForShot(pick.id, Fmt.addDays(pick.start, 1), pick.start, 480, 260)
            if (shotPage === "calendar-notice" && pick) {
                const r = calendar.drop(pick.id, Fmt.addDays(pick.start, 7), pick.start)
                if (r.ok) calendarPage.showNotice(r)
            }
            if (shotPage === "journal-read") { const rows = journal.page.rows || []; if (rows.length) nav.go("journal", { id: rows[0].id }) }
            if (shotPage.startsWith("journal-edit")) { const rows = journal.page.rows || []; if (rows.length) nav.go("journal", { id: rows[0].id, edit: true }) }
        }
    }
    function seedJournalShot() {
        const d = plans.demo()
        const planId = d.planId || ""
        if (planId) planPage.seedPhotosForShot(planId)
        const ph = planId ? photos.planPhotos(planId) : []
        const pic = i => ph.length ? "![海边的傍晚](photo:" + ph[i % ph.length].id + ")\n\n" : ""
        journal.save("jn_shot1", { title: "第一天：到了", date: "2026-12-05", plan: planId, body: pic(0) + "**中午到**，先去市场吃了东西，下午沿着海边走了一圈。\n\n## 晚上\n风很大，第二天的船不知道开不开。\n\n> 这里的日落差不多七点半，拍了很多照片，回来慢慢挑。\n\n" + pic(1) + "- 市场的牡蛎\n- 海边步道\n\n路上遇到一对从霍巴特来的老夫妇，聊了很久。" })
        journal.save("jn_shot2", { title: "第二天：等船", date: "2026-12-06", plan: planId, body: pic(2) + "早上船取消了，在码头边的咖啡馆坐了一上午。下午天晴了，改成开车去灯塔，路上全是羊。回来的时候太阳刚好落到海里，这一天比原来的安排还好。" })
        journal.save("jn_shot3", { title: "", date: "2026-10-02", plan: "", body: "还没想好标题。随手记几句：下次出门要带转换插头，别忘了下载离线地图。" })
    }
    Timer {
        id: photoModeShot
        interval: 900
        onTriggered: {
            const lib = photos.library()
            for (let i = 0; i < lib.length && i < 40; i += 13) photos.setFav(lib[i].id, true)   // 副本上：几张精选
            const album = photos.album
            if (shotPage.startsWith("album")) {
                if (shotPage === "album-narrow") win.width = 560
                win.albumOpen = true
                if (shotPage === "album-year") album.view = "year"
                if (shotPage === "album-fav") album.view = "fav"
                if (shotPage === "album-inbox") {
                    const ids = []
                    for (let i = 3; i < lib.length && ids.length < 9; i += 7) ids.push(lib[i].id)
                    photos.assign(ids, "", "")
                    album.view = "inbox"
                }
            } else if (shotPage === "photo-trip" || shotPage === "photo-lightbox") {
                const t = album.trips
                let best = null
                for (const x of t) if (mapc.photoLayer.pointsOf(x.id).length > (best ? mapc.photoLayer.pointsOf(best.id).length : 0)) best = x
                if (best) {
                    // view 会记在副本里，单截时可能还是上一张留下的未归类
                    if (shotPage === "photo-trip") { album.view = "trip"; album.plan = best.id; win.flyToPhotos(best.id) }
                    else {
                        const l = []
                        for (const p of lib) if (p.plan === best.id && !p.lat) l.push(p.id)
                        win.showMapPhotos(l.slice(0, 12))
                    }
                }
            } else if (shotPage.startsWith("heic-")) {
                if (shotPage.startsWith("heic-off")) photos.setHeicTestOff(true)
                if (shotPage === "heic-off-album") photos.clearThumbCache()   // 缓存里有缩略图时看不到"要装扩展"的格子
                photos.startImport(photoShotFiles.filter(f => String(f).endsWith(".heic")))
            }
        }
    }
    Connections {   // 截图 heic-*：确认表出来后直接导入，再看未归类
        target: photos
        enabled: shotPage.startsWith("heic-")
        function onImportChanged() { if (photos.importStage === "review") photos.confirmImport() }
        function onImportFinished(r) {
            // 测试 HEIC 按日期会被猜到某个计划：挪回未归类（副本），才看得到"要装扩展"的格子
            photos.assign(photos.library().filter(p => p.heic).map(p => p.id), "", "")
            win.albumOpen = true
            photos.album.view = "inbox"
            if (shotPage.endsWith("lightbox")) heicShot.start()
        }
    }
    Timer {
        id: heicShot
        interval: 500
        onTriggered: { const h = photos.library().filter(p => p.heic); if (h.length) win.showMapPhotos([h[h.length - 1].id]) }
    }
    Timer {
        id: libShot
        interval: 700
        onTriggered: {
            const v = win.findByName(planList, "libraryView")
            if (!v) return
            const rows = plans.library.rows || []
            if (shotPage === "library-select") { v.selMode = true; v.selIds = rows.slice(1, 4).map(x => x.id) }
            else {
                // 飞到站点最多的路线，让它高亮
                const d = mapc.markers.demo("line")
                if (d.planId !== undefined) { mapc.flyToPlan(d.planId); mapc.markers.hoverFromList(d.planId) }
            }
        }
    }
    Timer {
        id: searchShot; interval: 900
        onTriggered: {
            // 结果都不在眼前 → editor 自己把镜头移过去（showResults）
            editor.setResultsForTest([
                { name: "Springfield", address: "Sangamon County, Illinois, United States", lat: 39.7990, lng: -89.6440 },
                { name: "Springfield", address: "Greene County, Missouri, United States", lat: 37.2090, lng: -93.2923 },
                { name: "Springfield", address: "Hampden County, Massachusetts, United States", lat: 42.1015, lng: -72.5898 }])
            editor.hoverResult = 0
        }
    }
    Timer { id: styleShot; interval: 900; onTriggered: { planEditor.infoOpen = true; Qt.callLater(() => { const bb = win.findByName(planEditor, "edPlanStyle"); if (bb) bb.clicked() }) } }
    function findByName(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) { const f = win.findByName(kids[i], name); if (f) return f }
        return null
    }

    // 截图（0.8.2）：markers = 选中一条路线、聚焦第 3 站、显示它的提示框（7 级）；spread = 重叠的站点错开（15 级）；
    // pins = "想去"的单一目的地（空心水滴，8 级）
    Timer {
        id: markerShot
        interval: 2500
        onTriggered: {
            mapc.base = "street"
            const kind = shotPage === "markers" ? "route" : shotPage === "spread" ? "spread" : "pin"
            const d = mapc.markers.demo(kind)
            if (d.planId === undefined) { console.warn("截图 " + shotPage + "：数据里找不到合适的地方"); return }
            if (shotPage === "markers") mapc.markers.selectPlan(d.planId, d.stop, false)
            mapc.jumpTo(d.lat, d.lng, shotPage === "markers" ? 7 : shotPage === "spread" ? 15 : 8)
            if (shotPage === "markers") markerTip.start()
        }
    }
    // 截图（0.8.4）：lines = 鼠标碰到一条路线（这条加粗、其余的线和标记变淡、提示框是计划名），5 级
    Timer {
        id: lineShot
        interval: 2500
        onTriggered: {
            mapc.base = "street"
            const d = mapc.markers.demo("line")
            if (d.planId === undefined) { console.warn("截图 lines：数据里找不到合适的路线"); return }
            mapc.jumpTo(d.lat, d.lng, 5)
            lineTip.planId = d.planId
            lineTip.seg = Math.max(0, d.stop - 1)
            lineTip.start()
        }
    }
    Timer {
        id: lineTip
        property string planId: ""
        property int seg: 0
        interval: 600
        onTriggered: if (win.map) win.map.showLineTip(planId, seg)
    }
    // 截图（0.8.5）：layers = 图层泡泡打开（街道图）；layers-sat = 卫星图上（烟色玻璃白字）
    Timer { id: layersShot; interval: 1500; onTriggered: layers.setOpen(true) }
    // unlock-edit = 手动标记面板（列出已经标记的；鼠标"碰到"一个省：描边 + 提示框）；unlock-search = 搜"四川"
    Timer {
        id: unlockShot
        interval: 2000
        onTriggered: {
            mapc.unlockEdit = true
            if (shotPage === "unlock-search") {
                const s = win.findChild(unlockPanel, "unlockSearch")
                if (s) s.text = "四川"
            }
            const d = mapc.unlockDemo()
            if (d.id === undefined) return
            mapc.jumpTo(d.lat, d.lng, 4)
            unlockTip.start()
        }
    }
    Timer {
        id: unlockTip
        interval: 600
        onTriggered: {
            const d = mapc.unlockDemo()
            if (d.id === undefined || !win.map) return
            const p = mapc.pixelFor(d.lat, d.lng)
            const t = mapc.unlockEditHover(p.x, p.y)
            if (t !== "") win.map.showTipAt(t, p.x, p.y - 14, "unlock")
        }
    }
    function findChild(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) { const r = findChild(item.children[i], name); if (r) return r }
        return null
    }
    Timer {
        id: markerTip
        interval: 800
        onTriggered: { const d = mapc.markers.demo("route"); if (win.map) win.map.showStopTip(d.planId, d.stop) }
    }

    // ---- 内存测试：每 6 秒做一件事，循环 ----
    // 0.8.6：每一步开始前记一行内存（= 上一步做完 6 秒后的状态，memlog.mark）；memSkip 里的步骤跳过（globe / unlock / sat）
    Timer {
        id: memLoop
        interval: 6000; running: memtest; repeat: true
        property int step: 0
        property string last: "启动"
        // 0.9.0：没有浅灰了，第 3、4 步改成街道图上缩小、再飞一次；进地球单数轮是卫星图、双数轮是已解锁
        readonly property var names: ["街道图 + 飞行", "拖动", "缩小", "再飞一次", "卫星图", "卫星图上飞行", "已解锁", "进地球", "转地球", "地球缩放", "回平面", "开关面板"]
        function skipped(s) {
            return (s === 4 && memSkip.indexOf("sat") >= 0) || (s === 6 && memSkip.indexOf("unlock") >= 0)
                || (s >= 7 && s <= 10 && memSkip.indexOf("globe") >= 0)
        }
        onTriggered: {
            if (memlog) memlog.mark(last, Math.floor(step / 12))
            const s = step++ % 12
            last = names[s] + (skipped(s) ? "（跳过）" : "")
            if (skipped(s)) return
            if (!win.map && s < 7) { last = names[s] + "（没有地图）"; return }
            if (s === 0) { mapc.base = "street"; const c = mapc.randomStop(); mapc.flyTo(c[0], c[1], 5 + Math.random() * 5, 2500) }
            else if (s === 1) { mapc.dragStart(); mapc.dragBy(200 - Math.random() * 400, 200 - Math.random() * 400); mapc.dragEnd() }
            else if (s === 2) mapc.zoomBy(-1, win.width / 2, win.height / 2)
            else if (s === 3) { const c = mapc.randomStop(); mapc.flyTo(c[0], c[1], 3 + Math.random() * 8, 2500) }
            else if (s === 4) mapc.base = "sat"
            else if (s === 5) { const c = mapc.randomStop(); mapc.flyTo(c[0], c[1], 2 + Math.random() * 3, 2500) }
            else if (s === 6) mapc.base = "unlock"
            else if (s === 7) { if (Math.floor(step / 12) % 2 === 0) mapc.base = "sat"; win.toggleGlobe() }
            else if (s === 8) spin.restart()
            else if (s === 9) globe.zoomBy(Math.random() < 0.5 ? 2.2 : 0.45)
            else if (s === 10) win.toggleGlobe()
            else if (s === 11) bar.visible = !bar.visible
        }
    }
    Timer {   // 地球连续转 4 秒
        id: spin
        interval: 16; repeat: true
        property int n: 0
        onTriggered: { globe.rotateBy(6, 1); if (++n > 250) { n = 0; stop() } }
    }
}
