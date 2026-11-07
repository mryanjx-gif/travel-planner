// 平面地图（0.8.0）。单独一个文件、由 Main.qml 用 Loader 加载：地图库没装好（或云端测试环境没有 maplibre）时
// 只是这一层加载失败，界面其他部分照常显示。
//
// 相机由 C++ 的 mapc（src/map/mapcontroller.h）管：拖动、滚轮、飞行都在那边算，这里只把中心和缩放交给地图。
// 样式：骨架（背景 + 当前底图）是一个小文件，每种底图一份，换底图就换样式（0.8.6：隐藏的底图在地图库里会一直留着瓦片，
// 见 mapcontroller.cpp skeletonStyleJson）；换样式时下面这些参数由地图库自己按顺序重新加一遍。
// 路线、站点、已解锁、昼夜阴影都用 SourceParameter / LayerParameter 从内存交给地图。图层顺序 = 下面 params 的顺序（后加的在上面），照 Electron：
// 底图 → 昼夜阴影 → 已解锁 → 路线（平时 → 鼠标碰到的高亮层 → 选中，0.8.4）→ 错开时真实位置的小点。
// 站点标记（0.8.2 起）不在 maplibre 里画，是上面的 QML 覆盖层（components/MapMarkers.qml）。
import QtQuick
import MapLibre 4.0
import TPApp

MapLibre {
    id: map
    objectName: "map"
    property bool interactive: true
    signal photoOpened(var ids)   // 0.11.1 点了照片标记（看大图在 Main）
    readonly property bool unlock: mapc.base === "unlock"
    readonly property bool sat: mapc.base === "sat"
    // 线宽 3.5 px；DASH_PX 像素 ÷ 线宽（Electron LINE_STYLE.idle / DASH_PX）
    readonly property real lineW: 3.5
    function vis(on) { return on ? "visible" : "none" }

    style: mapc.styleUrl
    // 0.9.1 计时：换样式到地图库说"样式加载完"要多久（第一次是启动的一步）
    property real styleT0: 0
    property bool styleOnce: false
    onStyleChanged: styleT0 = perf.now()
    onStyleLoadedChanged: if (styleLoaded) {
        if (!styleOnce) { styleOnce = true; perf.startup("地图样式加载完") }
        perf.mark("地图库加载样式", perf.now() - styleT0, mapc.base)
    }
    coordinate: mapc.coordinate
    zoomLevel: mapc.zoom
    onWidthChanged: mapc.setViewport(width, height)
    onHeightChanged: mapc.setViewport(width, height)

    // ---- 骨架里的图层：只改显示 / 颜色 ----
    LayerParameter {
        id: pBg; styleId: "bg"; type: "background"
        // 背景：卫星图 #0b1a2e，其他 #e8e2d6；已解锁时是海的颜色 #d9e4ea（map-app.js setBase / applyUnlock）
        paint: ({ "background-color": map.unlock ? "#d9e4ea" : map.sat ? "#0b1a2e" : "#e8e2d6" })
    }
    // 和背景同色的一整块多边形（0.8.2，在骨架样式里：背景之上、三个底图之下）：maplibre-native 在视野里既没有加载成功的瓦片、
    // 也没有任何 GeoJSON 要素时整帧不画（出来是白的）。有这一块，离线 / 瓦片失败时也照常显示底色。这里只改颜色。
    // （0.8.2 第一版是用 SourceParameter 后加的，结果叠在底图上面、把瓦片全盖住了——后加的图层总在最上面）
    LayerParameter {
        id: lBgFill; styleId: "bg-fill"; type: "fill"
        paint: ({ "fill-color": map.unlock ? "#d9e4ea" : map.sat ? "#0b1a2e" : "#e8e2d6", "fill-antialias": false })
    }
    // 底图的栅格图层在骨架样式里（只有当前这一种），这里不用管
    // 屏幕缩放比例：卫星图瓦片按它选大小（0.8.6）
    readonly property real screenDpr: Screen.devicePixelRatio
    onScreenDprChanged: mapc.setDpr(screenDpr)

    // ---- 昼夜阴影：几层半透明多边形叠出暮光过渡；缩放 ≤3 完全显示，到 4.5 淡出（NIGHT.fullZoom / hideZoom） ----
    SourceParameter { id: sNight; styleId: "night"; type: "geojson"; property string data: mapc.nightJson }
    LayerParameter {
        id: lNight; styleId: "night"; type: "fill"
        property string source: "night"
        layout: ({ "visibility": map.vis(map.sat) })
        paint: ({ "fill-color": "#040a20", "fill-antialias": false,
                  "fill-opacity": ["interpolate", ["linear"], ["zoom"], 3, ["get", "a"], 4.5, 0] })
    }

    // ---- 已解锁：白色陆地 + 去过的省上色 + 省界 / 国界 + 小单位的圆点 ----
    // 省界 u-adm1 / u-adm0 在骨架样式里（0.9.2：已解锁时是文件地址，地图库在后台读；见 mapcontroller.cpp skeletonStyleJson）
    SourceParameter { id: sVisit; styleId: "u-visit"; type: "geojson"; property string data: mapc.visitJson }
    SourceParameter { id: sDot; styleId: "u-dot"; type: "geojson"; property string data: mapc.dotsJson }
    LayerParameter {
        id: lLand; styleId: "u-land"; type: "fill"
        property string source: "u-adm1"
        layout: ({ "visibility": map.vis(map.unlock) })
        paint: ({ "fill-color": "#ffffff" })
    }
    LayerParameter {
        id: lVisit; styleId: "u-visit"; type: "fill"
        property string source: "u-visit"
        layout: ({ "visibility": map.vis(map.unlock) })
        paint: ({ "fill-color": ["get", "c"], "fill-opacity": 0.9 })
    }
    LayerParameter {
        id: lAdm1; styleId: "u-adm1"; type: "line"
        property string source: "u-adm1"
        layout: ({ "visibility": map.vis(map.unlock) })
        paint: ({ "line-color": "#c9c2b8", "line-width": ["interpolate", ["linear"], ["zoom"], 1, 0.3, 5, 0.8] })
    }
    LayerParameter {
        id: lAdm0; styleId: "u-adm0"; type: "line"
        property string source: "u-adm0"
        layout: ({ "visibility": map.vis(map.unlock) })
        paint: ({ "line-color": "#7d746a", "line-width": ["interpolate", ["linear"], ["zoom"], 1, 0.6, 5, 1.4] })
    }
    LayerParameter {
        id: lDot; styleId: "u-dot"; type: "circle"
        property string source: "u-dot"
        layout: ({ "visibility": map.vis(map.unlock) })
        paint: ({ "circle-color": ["get", "c"], "circle-radius": 5, "circle-stroke-color": "#ffffff", "circle-stroke-width": 1.5,
                  "circle-opacity": ["interpolate", ["linear"], ["zoom"], 5, 1, 6.5, 0],
                  "circle-stroke-opacity": ["interpolate", ["linear"], ["zoom"], 5, 1, 6.5, 0] })
    }

    // 手动标记时鼠标所在的省（或同一组的几块）描边（0.8.5，Electron u-hover：单独一个小数据源，#2b2620、2.2 px）
    SourceParameter { id: sUHover; styleId: "u-hover-src"; type: "geojson"; property string data: mapc.unlockHoverJson }
    LayerParameter {
        id: lUHover; styleId: "u-hover"; type: "line"
        property string source: "u-hover-src"
        layout: ({ "visibility": map.vis(map.unlock) })
        paint: ({ "line-color": "#2b2620", "line-width": 2.2 })
    }

    // ---- 路线（0.8.4 起照 Electron 分三套，ROUTE_LAYERS 的顺序）：平时（rt-*）→ 高亮层（rh-*，鼠标碰到的那条）→ 选中（rt-*-sel）。
    // 每套都是整条白边（k='c'）在下、彩线按段（实线 / 虚线 / 点线）在上；"已解锁"时都不显示。
    // 透明度由 C++ 每帧算好（mapc.routeOpacity 等，和标记的渐变同一个定时器）；骨架样式里 transition 是 0，maplibre 不再自己过渡。
    // 线宽、虚线：平时 3.5 px / 白边 6.5 px、虚线 4 8；碰到和选中 5 px / 白边 9 px，选中的虚线 4 10；想去的点线 0.5 7（像素 ÷ 线宽）
    function routeLayout() { return { "visibility": map.vis(!map.unlock), "line-join": "round", "line-cap": "round" } }
    function dashFor(dash, sel, w) { return dash === 2 ? [0.5 / w, 7 / w] : sel ? [4 / w, 10 / w] : [4 / w, 8 / w] }
    SourceParameter { id: sRoutes; styleId: "routes"; type: "geojson"; property string data: mapc.routesJson }
    // 0.9.7：叠上来的其他旅程（左下胶囊去重复，PO 选 A）：淡灰细线、实线，垫在所有路线下面
    SourceParameter { id: sRoutesOv; styleId: "routes-ov"; type: "geojson"; property string data: mapc.routesOverlayJson }
    LayerParameter {
        id: lOverlay; styleId: "rt-overlay"; type: "line"
        property string source: "routes-ov"
        property var filter: ["==", ["get", "k"], "s"]
        layout: map.routeLayout()
        paint: ({ "line-color": "#6f7d89", "line-width": 1.8, "line-opacity": 0.8 })
    }
    SourceParameter { id: sRoutesHv; styleId: "routes-hv"; type: "geojson"; property string data: mapc.routesHvJson }
    SourceParameter { id: sRoutesSel; styleId: "routes-sel"; type: "geojson"; property string data: mapc.routesSelJson }
    // 平时
    LayerParameter {
        id: lEdge; styleId: "rt-edge"; type: "line"
        property string source: "routes"
        property var filter: ["==", ["get", "k"], "c"]
        layout: map.routeLayout()
        paint: ({ "line-color": "#ffffff", "line-width": 6.5, "line-opacity": mapc.edgeOpacity })
    }
    LayerParameter {
        id: lSolid; styleId: "rt-solid"; type: "line"
        property string source: "routes"
        property var filter: ["all", ["==", ["get", "k"], "s"], ["==", ["get", "dash"], 0]]
        layout: map.routeLayout()
        paint: ({ "line-color": ["get", "color"], "line-width": map.lineW, "line-opacity": mapc.routeOpacity })
    }
    LayerParameter {
        id: lDash; styleId: "rt-dash"; type: "line"
        property string source: "routes"
        property var filter: ["all", ["==", ["get", "k"], "s"], ["==", ["get", "dash"], 1]]
        layout: map.routeLayout()
        paint: ({ "line-color": ["get", "color"], "line-width": map.lineW, "line-opacity": mapc.routeOpacity, "line-dasharray": map.dashFor(1, false, map.lineW) })
    }
    LayerParameter {
        id: lDot2; styleId: "rt-dot"; type: "line"
        property string source: "routes"
        property var filter: ["all", ["==", ["get", "k"], "s"], ["==", ["get", "dash"], 2]]
        layout: map.routeLayout()
        paint: ({ "line-color": ["get", "color"], "line-width": map.lineW, "line-opacity": mapc.routeOpacity, "line-dasharray": map.dashFor(2, false, map.lineW) })
    }
    // 高亮层：鼠标碰到的那条路线（透明度 = 它的高亮程度，进 200 ms、出 350 ms）
    LayerParameter {
        id: lHvEdge; styleId: "rh-edge"; type: "line"
        property string source: "routes-hv"
        property var filter: ["==", ["get", "k"], "c"]
        layout: map.routeLayout()
        paint: ({ "line-color": "#ffffff", "line-width": 9, "line-opacity": mapc.hvOpacity })
    }
    LayerParameter {
        id: lHvSolid; styleId: "rh-body-solid"; type: "line"
        property string source: "routes-hv"
        property var filter: ["all", ["==", ["get", "k"], "s"], ["==", ["get", "dash"], 0]]
        layout: map.routeLayout()
        paint: ({ "line-color": ["get", "color"], "line-width": 5, "line-opacity": mapc.hvOpacity })
    }
    LayerParameter {
        id: lHvDash; styleId: "rh-body-dash"; type: "line"
        property string source: "routes-hv"
        property var filter: ["all", ["==", ["get", "k"], "s"], ["==", ["get", "dash"], 1]]
        layout: map.routeLayout()
        paint: ({ "line-color": ["get", "color"], "line-width": 5, "line-opacity": mapc.hvOpacity, "line-dasharray": map.dashFor(1, false, 5) })
    }
    LayerParameter {
        id: lHvDot; styleId: "rh-body-dot"; type: "line"
        property string source: "routes-hv"
        property var filter: ["all", ["==", ["get", "k"], "s"], ["==", ["get", "dash"], 2]]
        layout: map.routeLayout()
        paint: ({ "line-color": ["get", "color"], "line-width": 5, "line-opacity": mapc.hvOpacity, "line-dasharray": map.dashFor(2, false, 5) })
    }
    // 选中的计划：加粗、不透明、画在最上面
    LayerParameter {
        id: lSelEdge; styleId: "rt-edge-sel"; type: "line"
        property string source: "routes-sel"
        property var filter: ["==", ["get", "k"], "c"]
        layout: map.routeLayout()
        paint: ({ "line-color": "#ffffff", "line-width": 9, "line-opacity": 1 })
    }
    LayerParameter {
        id: lSelSolid; styleId: "rt-solid-sel"; type: "line"
        property string source: "routes-sel"
        property var filter: ["all", ["==", ["get", "k"], "s"], ["==", ["get", "dash"], 0]]
        layout: map.routeLayout()
        paint: ({ "line-color": ["get", "color"], "line-width": 5, "line-opacity": 1 })
    }
    LayerParameter {
        id: lSelDash; styleId: "rt-dash-sel"; type: "line"
        property string source: "routes-sel"
        property var filter: ["all", ["==", ["get", "k"], "s"], ["==", ["get", "dash"], 1]]
        layout: map.routeLayout()
        paint: ({ "line-color": ["get", "color"], "line-width": 5, "line-opacity": 1, "line-dasharray": map.dashFor(1, true, 5) })
    }
    LayerParameter {
        id: lSelDot; styleId: "rt-dot-sel"; type: "line"
        property string source: "routes-sel"
        property var filter: ["all", ["==", ["get", "k"], "s"], ["==", ["get", "dash"], 2]]
        layout: map.routeLayout()
        paint: ({ "line-color": ["get", "color"], "line-width": 5, "line-opacity": 1, "line-dasharray": map.dashFor(2, true, 5) })
    }
    // 重叠标记错开时（≥14 级），真实位置留一个小实心点（Electron spread-dot）；"已解锁"时和路线一起藏起来
    SourceParameter { id: sSpread; styleId: "spread"; type: "geojson"; property string data: mapc.markers.spreadJson }
    LayerParameter {
        id: lSpread; styleId: "spread-dot"; type: "circle"
        property string source: "spread"
        layout: ({ "visibility": map.vis(!map.unlock) })
        paint: ({ "circle-radius": 3.5, "circle-color": "#3d3630", "circle-stroke-color": "#ffffff", "circle-stroke-width": 1.5 })
    }

    // ---- 编辑中的计划（0.8.9，Electron draft-case / draft-line）：白边 8 px、彩色虚线 4 px（4 8），不可点 ----
    SourceParameter { id: sDraft; styleId: "draft"; type: "geojson"; property string data: mapc.draftJson }
    LayerParameter {
        id: lDraftCase; styleId: "draft-case"; type: "line"
        property string source: "draft"
        property var filter: ["==", ["get", "k"], "c"]
        layout: map.routeLayout()
        paint: ({ "line-color": "#ffffff", "line-width": 8, "line-opacity": 0.9 })
    }
    LayerParameter {
        id: lDraftLine; styleId: "draft-line"; type: "line"
        property string source: "draft"
        property var filter: ["==", ["get", "k"], "s"]
        layout: map.routeLayout()
        paint: ({ "line-color": ["get", "color"], "line-width": 4, "line-opacity": 0.95, "line-dasharray": [4 / 4, 8 / 4] })
    }

    // ---- 自检探针：一个纯品红圆点（#ff00ff），平时没有数据 ----
    SourceParameter { id: sProbe; styleId: "tp-probe"; type: "geojson"; property string data: mapc.probeJson }
    LayerParameter {
        id: lProbe; styleId: "tp-probe"; type: "circle"
        property string source: "tp-probe"
        paint: ({ "circle-radius": 7, "circle-color": "#ff00ff", "circle-opacity": 1 })
    }

    readonly property var params: [pBg, lBgFill, sNight, lNight, sVisit, sDot, lLand, lVisit, lAdm1, lAdm0, lDot, sUHover, lUHover,
                                   sRoutesOv, lOverlay, sRoutes, sRoutesHv, sRoutesSel, lEdge, lSolid, lDash, lDot2, lHvEdge, lHvSolid, lHvDash, lHvDot,
                                   lSelEdge, lSelSolid, lSelDash, lSelDot, sSpread, lSpread, sDraft, lDraftCase, lDraftLine, sProbe, lProbe]
    Component.onCompleted: {
        for (let i = 0; i < params.length; ++i) map.addStyleParameter(params[i])
        mapc.setViewport(width, height)
        mapc.setDpr(screenDpr)
    }

    // ---- 交互：拖动（松手有惯性）、滚轮缩放（以鼠标为中心） ----
    DragHandler {
        id: drag
        target: null
        enabled: map.interactive
        property point last
        onActiveChanged: {
            if (active) { last = centroid.position; mapc.dragStart() }
            else mapc.dragEnd()
        }
        onCentroidChanged: {
            if (!active) return
            const p = centroid.position
            mapc.dragBy(p.x - last.x, p.y - last.y)
            last = p
        }
    }
    WheelHandler {
        id: wheel
        enabled: map.interactive
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => mapc.wheel(event.angleDelta.y, wheel.point.position.x, wheel.point.position.y)
    }

    // ---- 站点标记（0.8.2）：QML 覆盖层，位置由 C++ 每次相机变化时算好 ----
    Binding { target: mapc.markers; property: "dpr"; value: Screen.devicePixelRatio }
    Binding { target: mapc.markers; property: "motion"; value: Theme.motion }   // "动效"关掉时高亮 / 变淡直接到位
    // 0.9.2：换模式时标记 0.15 秒淡入（PO："同一张地图，直接把标记换一下"）；"动效"关掉时直接出现
    function fadeInMarkers() {
        if (!Theme.motion) return
        markerFade.stop()
        markers.opacity = 0
        markerFade.start()
    }
    NumberAnimation { id: markerFade; target: markers; property: "opacity"; to: 1; duration: 150; easing.type: Easing.OutQuad }
    MapMarkers {
        id: markers
        objectName: "mapMarkers"   // 0.9.1：自检"性能"里临时藏起来，量标记占多少
        anchors.fill: parent
        tip: mapTip
        // 地球模式时不显示（地球上的标记在阶段 ③）；"已解锁"时藏起来（Electron pages.css：#map.unlock-mode .maplibregl-marker）
        visible: map.interactive && !map.unlock && mapc.markers.shown
    }
    // 0.11.1 照片模式：只有照片标记，站点标记和路线不画（筛选在 plancontroller）
    PhotoMarkers {
        objectName: "photoMarkers"
        anchors.fill: parent
        tip: mapTip
        active: mapc.photoLayer.active && map.interactive
        visible: active && !map.unlock
        onPhotosOpened: (ids) => map.photoOpened(ids)
    }
    // 0.8.9：编辑中的草稿标记（可拖动）、搜索结果的点、点选模式、我的位置（components/EditOverlay.qml）
    EditOverlay {
        id: editOverlay
        objectName: "editOverlay"
        anchors.fill: parent
        tip: mapTip
        visible: map.interactive
        z: 5
    }
    // 鼠标在地图上移动（0.8.4 起一个 HoverHandler 管两种）：
    // - "已解锁"里悬停去过的省 / 小单位圆点：提示框跟着鼠标（Electron showTipAt(…, e.point.x, e.point.y - 14, 'unlock')）
    // - 其他底图：碰到路线的线就高亮那个计划、提示框显示计划名（Electron onMapMove → lineAt / hoverPlan，'line'）
    HoverHandler {
        id: hover
        enabled: map.interactive && !testRun
        cursorShape: mapc.lineHot || (map.unlock && mapc.unlockEdit && map.overLand) ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPointChanged: map.pointerMoved()
        onHoveredChanged: {
            if (hovered) return
            if (mapTip.kind === "unlock" || mapTip.kind === "line") mapTip.hide()
            mapc.lineLeave()   // Electron：地图容器 mouseleave → unhoverPlan()
            if (mapc.unlockEdit) { mapc.setUnlockHover(""); map.overLand = false }   // Electron：mouseleave u-land
        }
    }
    function pointerMoved() {
        if (map.unlock) { unlockTip(); return }
        const p = hover.point.position
        const t = mapc.lineHover(p.x, p.y)
        if (t !== "") mapTip.showAt(t, p.x, p.y - 14, "line")
        else if (mapTip.kind === "line") mapTip.hide()
    }
    property bool overLand: false   // 手动标记时鼠标在陆地上（手形光标）
    function unlockTip() {
        if (mapTip.kind === "el") return
        const p = hover.point.position
        if (mapc.unlockEdit) {
            // 手动标记（Electron mousemove 'u-land'）：描边鼠标所在的省、提示"点击标记为去过 / 手动标记 · 点击取消 / 已由计划点亮"
            const e = mapc.unlockEditHover(p.x, p.y)
            map.overLand = e !== ""
            if (e !== "") mapTip.showAt(e, p.x, p.y - 14, "unlock")
            else if (mapTip.kind === "unlock") mapTip.hide()
            return
        }
        const t = mapc.unlockTipAt(p.x, p.y)
        if (t !== "") mapTip.showAt(t, p.x, p.y - 14, "unlock")
        else if (mapTip.kind === "unlock") mapTip.hide()
    }
    // 点线 = 选中那个计划（Electron map.on('click') → selectPlan(pid, false)；点在标记上时由标记处理）
    // 0.9.7（I1）：双击以双击处为中心放大一级（和地球上一样；Electron 的 GL JS 默认也是）；单击等 0.25 秒确认不是双击再选线。
    // 编辑时双击只放大（选线本来就不响应）
    TapHandler {
        id: flatTap
        enabled: map.interactive && !map.unlock && !testRun
        onTapped: (ev, button) => {
            if (flatTap.tapCount >= 2) { flatTapTimer.stop(); mapc.zoomBy(1, ev.position.x, ev.position.y, 300); return }
            if (editor.open) return   // 编辑时不切换计划（Electron selectPlan：if (editor.open) return）
            flatTapTimer.px = ev.position.x
            flatTapTimer.py = ev.position.y
            flatTapTimer.restart()
        }
    }
    Timer {
        id: flatTapTimer
        interval: 250
        property real px: 0
        property real py: 0
        onTriggered: if (map.interactive && mapc.lineClick(px, py) && mapTip.kind === "line") mapTip.hide()
    }
    // 手动标记时点省份：去过 ↔ 没去过（Electron map.on('click', 'u-land') → toggleRegion）
    TapHandler {
        enabled: map.interactive && map.unlock && mapc.unlockEdit && !testRun
        onTapped: (ev, button) => { if (mapc.unlockEditClick(ev.position.x, ev.position.y)) map.unlockTip() }
    }
    onUnlockChanged: if (!unlock && mapTip.kind === "unlock") mapTip.hide()
    Connections {
        target: mapc
        function onMovingChanged() { if (mapc.moving && (mapTip.kind === "el" || mapTip.kind === "line")) mapTip.hide() }   // Electron movestart
        function onDataChanged() { if (!mapc.unlockEdit) mapTip.hide() }                    // renderMap → hideTip（手动标记时点了省份也会重算，提示框留着）
        function onUnlockEditChanged() { if (!mapc.unlockEdit) { map.overLand = false; if (mapTip.kind === "unlock") mapTip.hide() } }
    }
    MapTip { id: mapTip; anchors.fill: parent }
    // 截图用（lines 页）：鼠标"碰到"某条路线第 seg 段的中点（高亮、提示框和真的悬停一样）
    function showLineTip(planId, seg) {
        const p = mapc.linePoint(planId, seg)
        if (p.x < 0) return
        const t = mapc.lineHover(p.x, p.y)
        if (t !== "") mapTip.showAt(t, p.x, p.y - 14, "line")
    }
    // 截图用：在 (x, y) 显示一个提示框
    function showTipAt(text, x, y, kind) { mapTip.showAt(text, x, y, kind) }
    // 截图用：显示某个站点标记的提示框（和鼠标悬停时一样的位置）
    function showStopTip(planId, stop) {
        const i = mapc.markers.indexOf(planId, stop)
        if (i < 0) return
        const s = mapc.markers.stopInfo(i)
        mapTip.showAt(s.tip, s.x - s.ax + s.w / 2, s.y - s.ay - 4, "el")
    }
}
