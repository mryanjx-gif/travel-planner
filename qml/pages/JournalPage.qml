// 游记整页（0.11.2，J-01–J-04；照 Electron pages-journal.js）。列表 → 阅读 → 编辑都在这一页，靠 nav 的参数换：{} / { id } / { id, edit, plan }。
// PO 第 7 题 A：卡片网格，首图加 110 字摘要，可以按旅程筛选。R1：写的时候，关联了旅程就在编辑区下面列出这趟的照片，点一下插入，可以收起。
// 新建的先不进数据（JournalController）：什么都没写就离开，数据完全没动；写了又全删掉的离开时删掉。
import QtQuick
import TPApp

Item {
    id: root
    property bool shown: false
    signal openPlan(string id)
    signal showPhotos(var list, int index)
    signal deleteRequested(string id, string title)
    signal addFromFile(string planId, string stopId)
    signal pickPhotoRequested(var rows, var allRows)
    signal linkRequested(string selected)
    signal notice(string text)

    property string curId: ""
    property bool curEdit: false
    property var cur: ({})                  // 阅读：journal.entry(curId)
    readonly property bool exists: cur.id !== undefined
    readonly property var readBlocks: exists ? planPage.guideBlocks(cur.body || "").blocks : []

    // 编辑中的字段
    property string edTitle: ""
    property string edDate: ""
    property string edPlan: ""
    property string edStop: ""
    property var stopRows: []
    property var stripRows: []
    property bool stripOpen: true
    property bool waitingImport: false
    property bool hadRecord: false
    readonly property bool narrow: width < Theme.px(620)

    visible: opacity > 0.01
    opacity: shown ? 1 : 0
    scale: shown || !Theme.motion ? 1 : 0.96
    transformOrigin: Item.TopLeft
    Behavior on scale { TpAnim { shown: root.shown; ms: 280 } }
    Behavior on opacity { enabled: Theme.motion; TpAnim { shown: root.shown } }
    onShownChanged: { journal.active = shown; if (shown) route(); else leave() }

    // ---- 换页 ----
    Connections { target: nav; function onChanged() { if (nav.page === "journal") root.route() } }
    Connections { target: journal; function onTouched() { root.reread() } }
    function route() {
        const p = nav.params
        const id = p.id || "", edit = p.edit === true
        if (id === curId && edit === curEdit && (edit || cur.id === id)) return
        flush()
        curId = id
        curEdit = edit
        journal.dropEmpty(edit ? id : "")
        cur = id !== "" ? journal.entry(id) : ({})
        hadRecord = exists
        if (edit) loadEditor(p.plan || "")
    }
    function leave() { flush(); journal.dropEmpty("") }
    function reread() {
        if (curId === "") return
        cur = journal.entry(curId)
        // 撤销之类从外面改的：不在打字的时候跟上。这一篇被撤销没了：编辑框也清空，别在离开时又存回去
        if (curEdit && !md.focused && !md.pending && !titleSave.running) {
            if (exists ? md.text !== (cur.body || "") || edTitle !== (cur.title || "") : hadRecord) loadEditor(edPlan)
        }
        hadRecord = exists
    }
    function loadEditor(newPlan) {
        const e = cur
        edTitle = e.title || ""
        edDate = e.date || plans.today
        edPlan = e.id !== undefined ? (e.plan || "") : newPlan
        edStop = e.stop || ""
        md.setText(e.body || "")
        md.pane = "write"
        stopRows = journal.stopOptions(edPlan)
        refreshStrip()
        titleEdit.text = edTitle
        if (e.id === undefined) Qt.callLater(() => titleEdit.forceActiveFocus())
    }
    function refreshStrip() { stripRows = edPlan !== "" ? photos.planPhotos(edPlan) : [] }
    Connections { target: photos; function onLibraryChanged() { if (root.curEdit) root.refreshStrip() } }
    function fields() { return { title: edTitle.trim(), date: edDate, plan: edPlan, stop: edStop, body: md.text } }
    function saveNow() { if (curEdit && curId !== "") journal.save(curId, fields()) }
    function flush() { if (curEdit) { md.flush(); saveNow() } }
    function done() {
        flush()
        app.noteChanged(true)
        // 什么都没写：回列表（没有这一篇可看）
        if (journal.entry(curId).id === undefined || (edTitle.trim() === "" && md.text.trim() === "")) nav.go("journal", {}, true)
        else nav.go("journal", { id: curId }, true)
    }
    function askDelete() {
        flush()
        if (journal.entry(curId).id === undefined) nav.go("journal", {}, true)   // 还没存过
        else root.deleteRequested(curId, edTitle.trim())
    }
    function removeCurrent(id) {
        curEdit = false   // 不再把编辑框里的内容存回去
        curId = ""
        journal.remove(id)
        nav.go("journal", {}, true)
    }
    function writeNew(planId) { nav.go("journal", { id: journal.newId(), edit: true, plan: planId || "" }) }
    // 外面（Main.qml 的对话框）回填
    function insertPicked(ph) { md.insertPhoto(ph) }
    function insertLink(t) { md.replaceSelection(t) }
    Connections {   // "从电脑添加照片"：导入完把这几张插进正文
        target: photos
        function onImportFinished(r) {
            if (!root.waitingImport) return
            root.waitingImport = false
            if (!r.ok) return
            for (const id of r.ids || []) root.insertPicked({ id: id, caption: "" })
        }
    }
    function viewBlocks(blocks, k) {
        const list = []
        let at = 0
        for (let i = 0; i < blocks.length; ++i) {
            const b = blocks[i]
            if (b.type !== "img" || !b.file) continue
            if (i === k) at = list.length
            list.push({ id: b.id, file: b.file, thumb: b.thumb, caption: b.caption || "" })
        }
        root.showPhotos(list, at)
    }

    GlassPanel {
        anchors.fill: parent
        radius: Theme.radius
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.985 : 0.99)
        liquid: false
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }   // 不穿透到地图

    // ==================== 列表 ====================
    Flickable {
        id: listFlick
        objectName: "journalList"
        visible: root.curId === ""
        anchors.fill: parent
        contentHeight: listCol.implicitHeight + Theme.px(44)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Column {
            id: listCol
            y: Theme.px(22)
            width: Math.min(listFlick.width - Theme.px(32), Theme.px(1180))
            x: (listFlick.width - width) / 2
            spacing: Theme.px(16)
            Item {
                width: parent.width
                height: Theme.px(52)
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Text { text: qsTr("游记"); font.family: serifFamily; font.pixelSize: Theme.px(26); font.bold: true; color: Theme.text }
                    Text {
                        objectName: "journalSummary"
                        visible: (journal.page.total || 0) > 0
                        text: qsTr("%1 篇").arg(journal.page.total || 0) + ((journal.page.plans || []).length > 0 ? " · " + Fmt.one(qsTr("%1 个旅程").arg(journal.page.plans.length)) : "")
                        font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                    }
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.px(10)
                    TpSelect {
                        objectName: "journalFilter"
                        visible: (journal.page.total || 0) > 0
                        width: root.narrow ? Theme.px(130) : Theme.px(220)
                        options: [{ key: "all", text: qsTr("全部旅程") }]
                                 .concat((journal.page.plans || []).map(p => ({ key: p.id, text: p.name || qsTr("未命名计划") })))
                                 .concat(journal.page.hasNone ? [{ key: "none", text: qsTr("没关联计划") }] : [])
                        current: journal.filter
                        onPicked: (k) => journal.filter = k
                    }
                    TpButton {
                        objectName: "journalNew"
                        kind: "primary"
                        compact: true
                        text: qsTr("写一篇")
                        // 预关联当前筛选（Electron newEntry）
                        onClicked: root.writeNew(journal.filter !== "all" && journal.filter !== "none" ? journal.filter : "")
                    }
                }
            }
            // ---- 空状态 ----
            Column {
                objectName: "journalEmptyPage"
                visible: (journal.page.total || 0) === 0
                width: parent.width
                topPadding: Theme.px(50)
                spacing: Theme.px(10)
                Canvas {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.px(160); height: Theme.px(96)
                    onPaint: {   // Electron .jn-empty-art：山、水、一本打开的书
                        const c = getContext("2d")
                        c.reset()
                        c.scale(width / 160, height / 96)
                        c.lineWidth = 1.6
                        c.lineCap = "round"; c.lineJoin = "round"
                        c.strokeStyle = Theme.lineStrong
                        c.path = "M4 70 30 48l14 10 22-26 20 20 16-12 22 22 14-8 14 16"; c.stroke(); c.beginPath()
                        c.strokeStyle = Theme.line
                        c.path = "M4 84c26-6 52 4 78-2s52-4 74 0"; c.stroke(); c.beginPath()
                        c.strokeStyle = Theme.accent
                        c.fillStyle = "#ffffff"
                        c.path = "M48 58c10-4 22-4 32 2v26c-10-6-22-6-32-2zM80 60c10-6 22-6 32-2v26c-10-4-22-4-32 2z"; c.fill(); c.stroke()
                    }
                }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("还没有游记"); font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width, Theme.px(460))
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("点“写一篇”，或者在计划页的“游记”标签里写。正文里可以插入那个计划的照片。")
                    font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
                }
            }
            // ---- 卡片网格 ----
            Grid {
                id: cards
                readonly property int cols: Math.max(1, Math.floor((width + spacing) / (Theme.px(260) + spacing)))
                readonly property real cardW: (width - spacing * (cols - 1)) / cols
                width: parent.width
                columns: cols
                spacing: Theme.px(16)
                Repeater {
                    model: journal.page.rows || []
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        objectName: "journalCard-" + index
                        width: cards.cardW
                        height: Math.round(width * 9 / 16) + Theme.px(118)   // 一样高，排起来整齐；没有图的摘要多占几行
                        radius: Theme.px(16)
                        color: "#ffffff"
                        border.width: 1; border.color: cardMa.containsMouse ? Theme.lineStrong : Theme.line
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.title || qsTr("无标题")
                        Image {
                            id: cover
                            visible: !!parent.modelData.thumb && status !== Image.Error
                            x: 1; y: 1
                            width: parent.width - 2
                            height: Math.round(parent.width * 9 / 16)
                            source: parent.modelData.thumb || ""
                            fillMode: Image.PreserveAspectCrop
                            clip: true
                            asynchronous: true
                        }
                        Column {
                            x: Theme.px(14)
                            y: (cover.visible ? cover.height : 0) + Theme.px(12)
                            width: parent.width - Theme.px(28)
                            height: parent.height - y - Theme.px(10)
                            spacing: Theme.px(3)
                            Text { width: parent.width; elide: Text.ElideRight; text: modelData.title || qsTr("无标题"); font.family: serifFamily; font.pixelSize: Theme.px(16); font.bold: true; color: Theme.text }
                            Text { width: parent.width; elide: Text.ElideRight; text: [modelData.date, modelData.planName].filter(x => x).join(" · "); font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted }
                            Text {
                                width: parent.width
                                height: parent.height - y
                                wrapMode: Text.Wrap
                                elide: Text.ElideRight
                                text: modelData.text
                                lineHeight: 1.3
                                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
                            }
                        }
                        MouseArea { id: cardMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: nav.go("journal", { id: parent.modelData.id }) }
                    }
                }
            }
        }
    }

    // ---- 面包屑 ----
    component Crumb: Row {
        property string tail: ""
        spacing: Theme.px(6)
        LinkText { objectName: "journalBack"; text: qsTr("游记"); onClicked: nav.go("journal") }
        Text { text: "/"; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.faint }
        Text { text: parent.tail; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted; elide: Text.ElideRight; width: Math.min(implicitWidth, Theme.px(420)) }
    }

    // ==================== 阅读 ====================
    Flickable {
        id: readFlick
        objectName: "journalRead"
        visible: root.curId !== "" && !root.curEdit
        anchors.fill: parent
        contentHeight: readCol.implicitHeight + Theme.px(60)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Column {
            id: readCol
            y: Theme.px(22)
            width: Math.min(readFlick.width - Theme.px(32), Theme.px(820))
            x: (readFlick.width - width) / 2
            spacing: Theme.px(10)
            Crumb { tail: root.exists ? (root.cur.title || qsTr("无标题")) : "" }
            // 找不到（已经删了，前进 / 后退回到这里）
            Column {
                objectName: "journalMissing"
                visible: !root.exists
                width: parent.width
                topPadding: Theme.px(60)
                spacing: Theme.px(10)
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("找不到这篇游记"); font.family: serifFamily; font.pixelSize: Theme.px(20); font.bold: true; color: Theme.text }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("可能已经被删除了。"); font.family: serifFamily; font.pixelSize: Theme.px(14); color: Theme.muted }
            }
            Text {
                objectName: "journalReadTitle"
                visible: root.exists
                width: parent.width
                wrapMode: Text.Wrap
                text: root.cur.title || qsTr("无标题")
                font.family: serifFamily; font.pixelSize: Theme.px(30); font.bold: true; color: Theme.text
            }
            Item {
                visible: root.exists
                width: parent.width
                height: Math.max(metaRow.height, editBtn.height)
                Flow {
                    id: metaRow
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - editBtn.width - Theme.px(10)
                    spacing: Theme.px(8)
                    Text { visible: !!root.cur.date; height: Theme.px(28); verticalAlignment: Text.AlignVCenter; text: root.cur.date || ""; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
                    TpChip { objectName: "journalPlanChip"; visible: !!root.cur.planName; text: root.cur.planName || ""; onClicked: root.openPlan(root.cur.plan) }
                    Rectangle {
                        visible: !!root.cur.stopName
                        width: stopChip.implicitWidth + Theme.px(20); height: Theme.px(28); radius: height / 2
                        color: Theme.panel2
                        Text { id: stopChip; anchors.centerIn: parent; text: root.cur.stopName || ""; font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft }
                    }
                }
                TpButton { id: editBtn; objectName: "journalEditBtn"; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; compact: true; text: qsTr("编辑"); onClicked: nav.go("journal", { id: root.curId, edit: true }, true) }
            }
            GuideBlocks {
                objectName: "journalBody"
                visible: root.exists
                width: parent.width
                blocks: root.readBlocks
                bodyFont: 16
                picMaxH: Theme.px(640)
                onPhotoClicked: (k) => root.viewBlocks(root.readBlocks, k)
            }
        }
    }

    // ==================== 编辑 ====================
    Flickable {
        id: editFlick
        objectName: "journalEditor"
        visible: root.curEdit
        anchors.fill: parent
        contentHeight: editCol.implicitHeight + Theme.px(32)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Column {
            id: editCol
            y: Theme.px(18)
            width: Math.min(editFlick.width - Theme.px(32), Theme.px(1400))
            x: (editFlick.width - width) / 2
            spacing: Theme.px(10)
            Crumb { tail: qsTr("编辑") }
            TextInput {
                id: titleEdit
                objectName: "journalTitle"
                width: parent.width
                font.family: serifFamily; font.pixelSize: Theme.px(26); font.bold: true
                color: Theme.text
                selectByMouse: true
                clip: true
                onTextEdited: { root.edTitle = text; titleSave.restart() }
                Timer { id: titleSave; interval: 500; onTriggered: root.saveNow() }
                Text { visible: titleEdit.text === "" && titleEdit.preeditText === ""; text: qsTr("标题"); font: titleEdit.font; color: Theme.faint }
            }
            Rectangle { width: parent.width; height: 1; color: titleEdit.activeFocus ? Theme.accent : Theme.line }
            Flow {
                width: parent.width
                spacing: Theme.px(10)
                Row {
                    height: Theme.px(32)
                    spacing: Theme.px(6)
                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("日期"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
                    DateField { objectName: "journalDate"; value: root.edDate; onEdited: (v) => { root.edDate = v; root.saveNow() } }
                }
                Row {
                    height: Theme.px(32)
                    spacing: Theme.px(6)
                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("旅程"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
                    TpSelect {
                        objectName: "journalPlan"
                        width: Theme.px(200)
                        options: root.curEdit ? [{ key: "", text: qsTr("（不关联）") }].concat(journal.planOptions().map(p => ({ key: p.key, text: p.text || qsTr("未命名计划") }))) : []
                        current: root.edPlan
                        onPicked: (k) => {
                            root.edPlan = k
                            root.edStop = ""
                            root.stopRows = journal.stopOptions(k)
                            root.refreshStrip()
                            root.saveNow()
                        }
                    }
                }
                Row {
                    height: Theme.px(32)
                    spacing: Theme.px(6)
                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("所在站点"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
                    TpSelect {
                        objectName: "journalStop"
                        width: Theme.px(170)
                        enabled: root.edPlan !== ""
                        options: [{ key: "", text: qsTr("（整个旅程）") }].concat(root.stopRows)
                        current: root.edStop
                        onPicked: (k) => { root.edStop = k; root.saveNow() }
                    }
                }
                Text {
                    objectName: "journalSaved"
                    height: Theme.px(32)
                    verticalAlignment: Text.AlignVCenter
                    text: md.pending || titleSave.running || app.dirty ? qsTr("未保存…") : journal.entry(root.curId).id !== undefined ? qsTr("已自动保存") : ""
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                }
                TpButton { objectName: "journalDelete"; compact: true; kind: "dangerOutline"; text: qsTr("删除"); onClicked: root.askDelete() }
                TpButton { objectName: "journalDone"; compact: true; kind: "primary"; text: qsTr("完成"); onClicked: root.done() }
            }
            MdEditor {
                id: md
                namePrefix: "journal"
                live: root.curEdit
                width: parent.width
                showDone: false
                delay: 900
                bodyFont: 15
                tools: ["h1", "h2", "b", "ul", "quote", "link", "pic", "file"]
                placeholder: qsTr("开始写……\n\n空一行 = 新段落。插入的照片单独占一行。")
                viewH: Math.max(Theme.px(240), editFlick.height - Theme.px(root.narrow ? 330 : 250) - (root.stripRows.length > 0 ? (root.stripOpen ? Theme.px(120) : Theme.px(30)) : 0))
                onEdited: root.saveNow()
                onLinkRequested: (s) => root.linkRequested(s)
                onPhotoRequested: root.pickPhotoRequested(root.stripRows, root.edPlan !== "" ? photos.library() : null)
                onFileRequested: { root.waitingImport = true; root.addFromFile(root.edPlan, root.edStop) }
                onPhotoViewRequested: (blocks, k) => root.viewBlocks(blocks, k)
            }
            // ---- R1 这趟的照片 ----
            Column {
                objectName: "journalStrip"
                visible: root.stripRows.length > 0
                width: parent.width
                spacing: Theme.px(6)
                Row {
                    spacing: Theme.px(6)
                    TpIcon { anchors.verticalCenter: parent.verticalCenter; width: Theme.px(14); height: Theme.px(14); name: root.stripOpen ? "down" : "up"; color: Theme.muted }
                    Text {
                        objectName: "journalStripToggle"
                        text: qsTr("这趟的照片") + " " + root.stripRows.length + (root.stripOpen ? " · " + qsTr("点一下插入") : "")
                        font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.textSoft
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.stripOpen = !root.stripOpen }
                    }
                }
                ListView {
                    objectName: "journalStripList"
                    visible: root.stripOpen
                    width: parent.width
                    height: Theme.px(84)
                    orientation: ListView.Horizontal
                    spacing: Theme.px(6)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.stripRows
                    delegate: Item {
                        required property var modelData
                        required property int index
                        objectName: "journalStripPhoto-" + index
                        width: Theme.px(112); height: Theme.px(84)
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.caption || modelData.stop || ""
                        Image {
                            anchors.fill: parent
                            source: parent.modelData.thumb || ""
                            fillMode: Image.PreserveAspectCrop
                            clip: true
                            asynchronous: true
                        }
                        Rectangle { anchors.fill: parent; color: "transparent"; border.width: stripMa.containsMouse ? 2 : 0; border.color: Theme.accent }
                        MouseArea { id: stripMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: md.insertAtCursor(parent.modelData) }
                    }
                }
            }
        }
    }
}
