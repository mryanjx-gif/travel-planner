// .trip 计划文件、分享版、批量导出（0.14.1，E-05–E-09；照 Electron export.js tripExport / tripImport / askConflict / bulk / clean / dropTrip）。
// 入口：⋯ 菜单、我的旅程（导入 .trip、批量导出）、头像菜单、Ctrl K、设置 → 数据；把 .trip 拖进窗口任何地方也能导入。
// 导出前先看有没有还没按道路算的段（E-10，和行程单一样）。撞了已有计划时逐个问：覆盖 / 另存一份 / 只合并消费和照片；
// 从 Electron 再导入（Main.qml importDialog）也用这里的冲突框，选项是 覆盖 / 另存一份 / 跳过。
import QtQuick
import QtQuick.Dialogs
import TPApp

Item {
    id: root
    objectName: "tripFlow"
    anchors.fill: parent
    z: 100

    property string autoPath: ""   // 自检用：有值时不弹系统对话框，直接用这个文件 / 文件夹
    property string lastResult: "" // 自检用：上一次的结果文字

    // ---------- 导出 ----------
    property var ids: []
    property string next: ""       // 算完路之后接着做什么：trip / each / share
    property bool shareEach: false
    function exportTrip(list) { shareEach = false; start(list, "trip") }
    function exportShare(list) { shareEach = false; start(list || [], "share") }
    function bulk(list) {
        if (!list || list.length === 0) return
        ids = list
        bulkDlg.fmt = "trip"; bulkDlg.how = "one"
        bulkDlg.open = true
    }
    function start(list, what) {
        ids = list
        next = what
        const n = list.length ? roads.pendingForIds(list) : roads.pendingFor("")   // 空 = 全部计划（分享版）
        if (n > 0) { roadAsk.count = n; roadAsk.open = true; return }
        proceed()
    }
    function proceed() {
        if (next === "share") { shareDlg.open = true; return }
        if (next === "each") { pickFolder("each"); return }
        // 合并成一个 / 单个计划
        if (autoPath !== "") { writeTrip(autoPath); return }
        saveDialog.selectedFile = exporter.defaultFolder() + "/" + trips.defaultName(ids) + ".trip"
        saveDialog.open()
    }
    function pickFolder(what) {
        folderDialog.what = what
        if (autoPath !== "") { folderPicked(autoPath); return }
        folderDialog.currentFolder = exporter.defaultFolder()
        folderDialog.open()
    }
    function folderPicked(dir) {
        if (folderDialog.what === "each") {
            const r = trips.exportEach(ids, dir)
            if (!r.ok) { fail(r.error); return }
            done(qsTr("已导出 %1 个计划文件到 %2（照片 %3 张%4）").arg(r.count).arg(r.dir).arg(r.photos)
                 .arg(r.missing ? qsTr("，%1 张找不到文件没带").arg(r.missing) : ""))
        } else {
            const r = trips.exportShare(ids, shareDlg.opts, dir, shareEach)
            if (!r.ok) { fail(r.error); return }
            shareDone.show(r)
        }
    }
    function writeTrip(path) {
        const r = trips.exportTrip(ids, path)
        if (!r.ok) { fail(qsTr("导出失败：") + r.error); return }
        done(qsTr("已导出：%1（%2照片 %3 张%4）").arg(r.file).arg(r.plans > 1 ? qsTr("%1 个计划，").arg(r.plans) : "").arg(r.photos)
             .arg(r.missing ? qsTr("，%1 张找不到文件没带").arg(r.missing) : ""))
    }
    function done(msg) { lastResult = msg; toast.show(msg, false) }
    function fail(msg) { lastResult = msg; toast.show(msg, true) }

    // ---------- 导入 ----------
    property var queue: []        // 还要问的冲突
    property var modes: ({})
    property string source: ""    // trip / electron
    property var onDecided: null  // electron：问完后调用 (modes)
    function importTrip(path) {
        if (app.readOnly) { fail(qsTr("现在是只读模式，不能导入。")); return }
        if (editor.open) { fail(qsTr("先保存或取消地图编辑器里的改动，再导入")); return }
        if (!path) {
            if (autoPath !== "") path = autoPath
            else { openDialog.currentFolder = exporter.defaultFolder(); openDialog.open(); return }
        }
        const r = trips.openImport(path)
        if (!r.ok) { fail(qsTr("导入失败：") + r.error); return }
        source = "trip"
        onDecided = null
        ask(r.plans.filter(p => p.conflict), r.file)
    }
    // Electron 再导入：plans = app.electronConflicts(...).plans
    function askElectron(list, file, then) {
        source = "electron"
        onDecided = then
        ask(list, file)
    }
    function ask(list, file) {
        modes = ({})
        queue = list
        conflict.file = file
        conflict.total = list.length
        nextConflict()
    }
    function nextConflict() {
        if (queue.length === 0) { finish(); return }
        conflict.item = queue[0]
        conflict.applyAll = false
        conflict.open = true
    }
    function decide(key) {
        conflict.open = false
        if (key === "abort") { cancelAll(); return }
        const m = Object.assign({}, modes)
        const rest = conflict.applyAll ? queue : [queue[0]]
        for (const p of rest) m[p.id] = key
        modes = m
        queue = queue.slice(rest.length)
        nextConflict()
    }
    function cancelAll() {
        queue = []
        if (source === "trip") trips.cancelImport()
        lastResult = "cancelled"
    }
    function finish() {
        if (source === "electron") { if (onDecided) onDecided(modes); return }
        const r = trips.commitImport(modes)
        if (r.cancelled) { lastResult = "cancelled"; return }
        if (!r.ok) { fail(r.error); return }
        done(r.message)
        if (r.openId) win.openTrip(r.openId)
        else if (!win.onMapPage || !win.listOpen) win.toggleList()
    }

    // 截图用（verify.ps1 的 ui-tripfile-*）：假数据，不碰真数据
    function shot(kind) {
        const day = 86400000
        const a = { name: qsTr("北海道 7 日"), stops: 9, expenses: 14, photos: 23, updatedAt: Date.now() - 3 * day }
        const b = { name: a.name, stops: 11, expenses: 18, photos: 31, updatedAt: Date.now() - day }
        if (kind.startsWith("conflict")) {
            source = kind === "conflict-electron" ? "electron" : "trip"
            onDecided = null
            modes = ({})
            queue = [{ id: "x1", name: a.name, local: a, incoming: b }, { id: "x2", name: "", local: a, incoming: b }, { id: "x3", name: "", local: a, incoming: b }]
            conflict.file = source === "trip" ? a.name + ".trip" : "D:\\旅行计划数据"
            nextConflict()
        } else if (kind === "bulk") { ids = ["a", "b", "c"]; bulkDlg.fmt = "trip"; bulkDlg.how = "each"; bulkDlg.open = true }
        else if (kind === "share") { ids = ["a", "b"]; shareDlg.open = true }
        else if (kind === "drop" || kind === "drop-bad") { hintShot = true; hintShotBad = kind === "drop-bad" }
        else if (kind === "sharedone") shareDone.show({ dir: (app.lang === "en" ? "D:\\Shared\\" : "D:\\分享\\") + qsTr("旅行计划-分享版") + "-20261108-2130", count: 1, photos: 12, skipped: 5, missing: 0 })
    }

    // ---------- 拖进窗口（E-09）----------
    DropArea {
        id: drop
        objectName: "tripDrop"
        anchors.fill: parent
        property bool tripInside: false
        keys: ["text/uri-list"]
        onEntered: (d) => { tripInside = d.urls.some(u => trips.isTripFile(String(u))); d.accept(Qt.CopyAction) }
        onDropped: (d) => {
            const f = d.urls.map(u => String(u)).find(u => trips.isTripFile(u))
            if (f) root.importTrip(f)
            else root.fail(qsTr("这里只能拖入 .trip 文件"))
        }
    }
    property bool hintShot: false   // 截图用
    property bool hintShotBad: false
    Rectangle {
        objectName: "tripDropHint"
        anchors.fill: parent
        visible: drop.containsDrag || root.hintShot
        color: Qt.rgba(Theme.scrim.r, Theme.scrim.g, Theme.scrim.b, 0.55)
        GlassPanel {
            anchors.centerIn: parent
            liquid: false
            width: Math.min(Theme.px(360), parent.width - 32)
            height: hintCol.implicitHeight + Theme.px(44)
            Rectangle {
                anchors.fill: parent
                anchors.margins: Theme.px(8)
                radius: Theme.px(14)
                color: "transparent"
                border.width: 2
                border.color: bad ? Theme.danger : Theme.accent
                readonly property bool bad: root.hintShot ? root.hintShotBad : !drop.tripInside
                Column {
                    id: hintCol
                    anchors.centerIn: parent
                    width: parent.width - Theme.px(28)
                    spacing: Theme.px(6)
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: parent.parent.bad ? qsTr("这里只能拖入 .trip 计划文件") : qsTr("松开导入计划")
                        font.family: serifFamily; font.pixelSize: Theme.px(18); font.bold: true
                        color: parent.parent.bad ? Theme.danger : Theme.text
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: parent.parent.bad ? qsTr("照片请在相册里点“导入照片”") : qsTr(".trip 计划文件")
                        font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                    }
                }
            }
        }
    }

    // ---------- 对话框 ----------
    component Label: Text { font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft }

    // 撞了同一个计划（Electron askConflict）
    ModalLayer {
        id: conflict
        objectName: "tripConflict"
        cardWidth: 520
        property var item: ({})
        property string file: ""
        property int total: 0
        property bool applyAll: false
        readonly property int rest: root.queue.length - 1
        readonly property bool electron: root.source === "electron"
        onDismissed: root.decide(electron ? "abort" : "skip")
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: conflict.electron ? qsTr("这个计划这里已经有了") : qsTr("这个计划已经有了")
            font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
        }
        Text {
            width: parent.width
            elide: Text.ElideMiddle
            text: (conflict.item.name || "") + " · " + conflict.file
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
        }
        Row {
            width: parent.width
            spacing: Theme.px(12)
            Repeater {
                model: [{ head: qsTr("你现在的"), d: conflict.item.local || {} },
                        { head: conflict.electron ? qsTr("Electron 版的") : qsTr("导入的"), d: conflict.item.incoming || {} }]
                delegate: Rectangle {
                    required property var modelData
                    width: (parent.width - Theme.px(12)) / 2
                    height: cmpCol.implicitHeight + Theme.px(20)
                    radius: Theme.px(10)
                    color: Theme.panelLight
                    Column {
                        id: cmpCol
                        x: Theme.px(12); y: Theme.px(10)
                        width: parent.width - Theme.px(24)
                        spacing: Theme.px(3)
                        Text { text: modelData.head; font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.text }
                        Repeater {
                            model: [[qsTr("最后修改"), modelData.d.updatedAt ? Qt.formatDateTime(new Date(modelData.d.updatedAt), "yyyy-MM-dd HH:mm") : qsTr("不知道")],
                                    [qsTr("站点"), String(modelData.d.stops || 0)],
                                    [qsTr("消费"), qsTr("%1 笔").arg(modelData.d.expenses || 0)],
                                    [qsTr("照片"), qsTr("%1 张").arg(modelData.d.photos || 0)]]
                            delegate: Row {
                                required property var modelData
                                spacing: Theme.px(8)
                                Text { width: Theme.px(app.lang === "en" ? 92 : 64); text: modelData[0]; font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted }
                                Text { text: modelData[1]; font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.text }
                            }
                        }
                    }
                }
            }
        }
        Repeater {
            model: conflict.electron
                   ? [{ key: "replace", title: qsTr("覆盖"), detail: qsTr("用 Electron 版替换这里的（含游记和照片记录）") },
                      { key: "copy", title: qsTr("另存一份"), detail: qsTr("两个都留，导入的名字后面加“（导入）”") },
                      { key: "skip", title: qsTr("跳过"), detail: qsTr("留着这里的，这一个不导入") }]
                   : [{ key: "replace", title: qsTr("覆盖"), detail: qsTr("用导入的替换现在的（现在的游记也换成导入的）") },
                      { key: "copy", title: qsTr("另存一份"), detail: qsTr("两个都留，导入的名字后面加“（导入）”") },
                      { key: "merge", title: qsTr("只合并消费和照片"), detail: qsTr("合并对方的消费、同行人、照片和游记") }]
            delegate: ChoiceCard {
                required property var modelData
                objectName: "tripConflict-" + modelData.key
                width: parent ? parent.width : 0
                title: modelData.title
                detail: modelData.detail
                onClicked: root.decide(modelData.key)
            }
        }
        TpCheck {
            objectName: "tripConflictAll"
            visible: conflict.rest > 0
            width: parent.width
            text: conflict.rest === 1 ? qsTr("后面还有 1 个已经有的计划，都这样处理") : qsTr("后面还有 %1 个已经有的计划，都这样处理").arg(conflict.rest)
            checked: conflict.applyAll
            onToggled: (c) => conflict.applyAll = c
        }
        Row {
            anchors.right: parent.right
            TpButton {
                text: conflict.electron ? qsTr("取消导入") : qsTr("取消")
                onClicked: root.decide(conflict.electron ? "abort" : "skip")
            }
        }
    }

    // 批量导出（Electron bulk）：格式 × 方式
    ModalLayer {
        id: bulkDlg
        objectName: "tripBulk"
        cardWidth: 480
        property string fmt: "trip"
        property string how: "one"
        onDismissed: open = false
        Text {
            width: parent.width
            text: qsTr("导出 %1 个计划").arg(root.ids.length)
            font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
        }
        Label { text: qsTr("格式") }
        Column {
            width: parent.width
            spacing: Theme.px(6)
            Repeater {
                model: [{ key: "trip", title: qsTr("计划文件 .trip"), detail: qsTr("对方用“导入 .trip”或拖进窗口打开") },
                        { key: "share", title: qsTr("分享版文件夹"), detail: qsTr("去掉私人内容的数据副本") }]
                delegate: ChoiceCard {
                    required property var modelData
                    objectName: "tripBulkFmt-" + modelData.key
                    width: parent.width
                    title: (bulkDlg.fmt === modelData.key ? "● " : "○ ") + modelData.title
                    detail: modelData.detail
                    onClicked: bulkDlg.fmt = modelData.key
                }
            }
        }
        Label { visible: root.ids.length > 1; text: qsTr("方式") }
        Segmented {
            objectName: "tripBulkHow"
            visible: root.ids.length > 1
            width: parent.width
            fill: true
            options: [{ key: "one", text: qsTr("合并成一个") }, { key: "each", text: qsTr("每个计划单独一个") }]
            current: bulkDlg.how
            onPicked: (k) => bulkDlg.how = k
        }
        Row {
            anchors.right: parent.right
            spacing: Theme.px(8)
            TpButton { text: qsTr("取消"); onClicked: bulkDlg.open = false }
            TpButton {
                objectName: "tripBulkGo"
                kind: "primary"
                text: qsTr("下一步")
                onClicked: {
                    bulkDlg.open = false
                    const each = root.ids.length > 1 && bulkDlg.how === "each"
                    root.shareEach = each
                    root.start(root.ids, bulkDlg.fmt === "share" ? "share" : each ? "each" : "trip")
                }
            }
        }
    }

    // 分享版的选项（Electron clean 的表单）
    ModalLayer {
        id: shareDlg
        objectName: "tripShare"
        cardWidth: 520
        property var opts: ({ noMoney: true, noBook: true, noTodo: false, noJournal: false, noVisited: false, allPhotos: false, unknownCovers: false, unknownStops: false })
        function setOpt(k, v) { const o = Object.assign({}, opts); o[k] = v; opts = o }
        onDismissed: open = false
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: root.ids.length ? qsTr("导出分享版（%1 个计划）").arg(root.ids.length) : qsTr("导出分享版")
            font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            lineHeight: 1.25
            text: qsTr("复制一份数据，去掉私人内容、紧急信息和证件。")
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
        }
        Column {
            width: parent.width
            spacing: Theme.px(8)
            Repeater {
                model: [{ key: "noMoney", text: qsTr("去掉消费和预算") }, { key: "noBook", text: qsTr("去掉交通住宿里的订单号、价格和备注") },
                        { key: "noTodo", text: qsTr("去掉待办和行李清单") }, { key: "noJournal", text: qsTr("去掉游记") },
                        { key: "noVisited", text: qsTr("去掉“已解锁”里手动标记的省 / 州") }]
                delegate: TpCheck {
                    required property var modelData
                    objectName: "tripShare-" + modelData.key
                    width: parent.width
                    text: modelData.text
                    checked: !!shareDlg.opts[modelData.key]
                    onToggled: (c) => shareDlg.setOpt(modelData.key, c)
                }
            }
        }
        Label { text: qsTr("照片") }
        Segmented {
            objectName: "tripSharePhotos"
            width: parent.width
            fill: true
            options: [{ key: "cover", text: qsTr("只带封面") }, { key: "all", text: qsTr("全部可分发的") }]
            current: shareDlg.opts.allPhotos ? "all" : "cover"
            onPicked: (k) => shareDlg.setOpt("allPhotos", k === "all")
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            lineHeight: 1.25
            text: qsTr("只带来源是 Wikimedia、NASA 或自己拍的照片。")
            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
        }
        TpCheck {
            width: parent.width
            text: qsTr("没有来源记录的封面也带上（只在确定是自己拍的时候勾）")
            checked: !!shareDlg.opts.unknownCovers
            onToggled: (c) => shareDlg.setOpt("unknownCovers", c)
        }
        TpCheck {
            width: parent.width
            enabled: !!shareDlg.opts.allPhotos
            opacity: enabled ? 1 : 0.5
            text: qsTr("没有来源记录的站点照片也带上（确定是自己拍的才勾）")
            checked: !!shareDlg.opts.unknownStops
            onToggled: (c) => shareDlg.setOpt("unknownStops", c)
        }
        Row {
            anchors.right: parent.right
            spacing: Theme.px(8)
            TpButton { text: qsTr("取消"); onClicked: shareDlg.open = false }
            TpButton {
                objectName: "tripShareGo"
                kind: "primary"
                text: qsTr("选文件夹并导出")
                onClicked: { shareDlg.open = false; root.pickFolder("share") }
            }
        }
    }

    // 分享版导出完
    MessageDialog {
        id: shareDone
        objectName: "tripShareDone"
        property string dir: ""
        title: qsTr("分享版导出好了")
        buttons: [{ key: "open", text: qsTr("打开文件夹") }, { key: "ok", text: qsTr("好"), kind: "primary" }]
        function show(r) {
            dir = r.dir
            const p = [r.count > 1 ? qsTr("已导出 %1 个分享版文件夹到：").arg(r.count) + r.dir : qsTr("已导出到：") + r.dir,
                       qsTr("照片 %1 张").arg(r.photos) + (r.skipped ? qsTr("，%1 张不能分发的没带").arg(r.skipped) : "")
                       + (r.missing ? qsTr("，%1 张找不到文件").arg(r.missing) : ""),
                       qsTr("转发时带上“照片署名.txt”。对方在设置里选这个文件夹就能看。")]
            paragraphs = p
            root.lastResult = p[0]
            open = true
        }
        onChosen: (key) => { open = false; if (key === "open") app.openUrl("file:///" + dir.replace(/\\/g, "/")) }
    }

    // E-10（多个计划一起数）
    ChoiceDialog {
        id: roadAsk
        objectName: "tripRoadAsk"
        property int count: 0
        readonly property bool canNet: !!netc.allowed.routing
        title: count === 1 ? qsTr("还有 1 段路线没按道路算") : qsTr("还有 %1 段路线没按道路算").arg(count)
        message: qsTr("这些自驾 / 徒步段还是直线，对方打开也是直线。")
        cancelKey: "cancel"
        options: [
            { key: "calc", disabled: !canNet, title: qsTr("先算好再导出"),
              detail: canNet ? qsTr("约 %1 分钟，算完自动保存").arg(Math.max(1, Math.ceil(count * 1.4 / 60)))
                             : qsTr("道路路线已关闭（设置 → 联网服务），现在没法算") },
            { key: "skip", title: qsTr("直接导出"), detail: qsTr("对方那边先显示直线") },
            { key: "cancel", title: qsTr("取消"), detail: "" }
        ]
        onChosen: (key) => {
            open = false
            if (key === "skip") root.proceed()
            else if (key === "calc") {
                if (root.ids.length) roads.startForIds(root.ids); else roads.startFor("")
                if (roads.running) roadWait.armed = true
                else root.proceed()
            }
        }
    }
    Connections {
        id: roadWait
        property bool armed: false
        target: roads
        function onFinished() {
            if (!roadWait.armed) return
            roadWait.armed = false
            if (roads.failCount > 0) toast.show(qsTr("%1 段没算出来，导出后按直线显示").arg(roads.failCount), false)
            root.proceed()
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("导出计划文件")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "trip"
        nameFilters: [qsTr("旅行计划文件") + " (*.trip)"]
        onAccepted: root.writeTrip(selectedFile)
    }
    FileDialog {
        id: openDialog
        title: qsTr("导入计划文件")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("旅行计划文件") + " (*.trip)"]
        onAccepted: root.importTrip(String(selectedFile))
    }
    FolderDialog {
        id: folderDialog
        property string what: ""
        title: what === "share" ? qsTr("选择导出到哪个文件夹") : qsTr("选择把计划文件存到哪个文件夹")
        onAccepted: root.folderPicked(app.localPath(selectedFolder))
    }
}
