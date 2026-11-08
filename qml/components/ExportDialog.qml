// 导出（0.14.0，E-01–E-04、E-10）：行程单（PDF / PNG 长图，可勾选订单号、消费、备注、紧急信息，右边是第一页的预览）、
// 紧急信息卡（PDF / PNG）、日历文件 .ics。点"导出…"：行程单先看有没有还没按道路算的段（E-10：先算好 / 直接导出 / 取消），
// 再用系统的保存对话框选位置。⋯ 菜单、简易面板的"导出…"、行李与待办的"导出信息卡"都打开这里。
import QtQuick
import QtQuick.Dialogs
import TPApp

Item {
    id: root
    objectName: "exportRoot"
    anchors.fill: parent
    z: 100
    property alias open: dlg.open
    property string planId: ""
    property string kind: "itinerary"    // itinerary / card / ics
    property string format: "pdf"        // pdf / png
    property var opts: ({ refs: false, money: false, notes: true, emergency: false })
    property var info: ({})
    property string previewUrl: ""
    property string lastFile: ""         // 自检用：上一次写出的文件
    property string autoPath: ""         // 自检用：有值时不弹保存对话框，直接写到这里
    readonly property bool wide: width >= Theme.px(700)

    function show(id, k, f) {
        planId = id
        info = exporter.info(id)
        kind = k || "itinerary"
        format = f || "pdf"
        open = true
        refresh()
    }
    function shotRoads(n) { roadAsk.count = n; roadAsk.open = true }   // 截图用
    function refresh() { if (open && kind !== "ics") previewTimer.restart() }
    function setOpt(k, v) { const o = Object.assign({}, opts); o[k] = v; opts = o; refresh() }
    Timer {
        id: previewTimer
        interval: 60
        onTriggered: root.previewUrl = root.open && root.kind !== "ics" ? exporter.preview(root.planId, root.kind, root.opts, Theme.px(440)) : ""
    }
    readonly property string problem: kind === "card" && !info.hasEmergency ? qsTr("还没有填紧急信息。在“行李与待办”里填当地电话，在“设置 → 证件与紧急信息”里填保险和联系人。")
                                      : kind === "ics" && !info.icsCount ? qsTr("这个计划还没有带日期的内容（计划日期、交通住宿、日程），没东西可以导出")
                                      : ""

    // 点"导出…"：行程单先看路线（E-10），再选位置
    function begin() {
        if (problem !== "") return
        const n = kind === "itinerary" ? roads.pendingFor(planId) : 0
        if (n > 0) { roadAsk.count = n; roadAsk.open = true; return }
        pick()
    }
    function pick() {
        if (autoPath !== "") { write(autoPath); return }
        saveDialog.selectedFile = exporter.defaultFolder() + "/" + exporter.defaultName(planId, kind, format)
        saveDialog.open()
    }
    function write(path) {
        const r = exporter.save(planId, kind, format, opts, path)
        if (!r.ok) { toast.show(qsTr("导出失败：") + r.error, true); return r }
        lastFile = r.file
        open = false
        const msg = kind === "ics" ? qsTr("已保存：%1（%2 项）。发到手机上打开，或在日历 app 里导入").arg(r.file).arg(r.count)
                  : r.truncated ? qsTr("已保存：%1。行程太长，长图只放下了前面一部分，完整的请导出 PDF").arg(r.file)
                  : qsTr("已保存：") + r.file
        toast.show(msg, false)
        return r
    }

    component Label: Text { font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft }

    ModalLayer {
        id: dlg
        objectName: "exportDialog"
        cardWidth: 660
        onDismissed: open = false
        Text {
            width: parent.width
            text: root.kind === "card" ? qsTr("导出紧急信息卡") : root.kind === "ics" ? qsTr("导出日历文件") : qsTr("导出行程单")
            font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: root.info.name || ""
            elide: Text.ElideRight
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
        }
        Grid {
            width: parent.width
            columns: root.wide ? 2 : 1
            columnSpacing: Theme.px(20)
            rowSpacing: Theme.px(14)
            Column {
                id: form
                width: root.wide ? parent.width - prev.width - parent.columnSpacing : parent.width
                spacing: Theme.px(12)
                Label { text: qsTr("导出成") }
                Segmented {
                    objectName: "exportKind"
                    width: parent.width
                    fill: true
                    options: [{ key: "itinerary", text: qsTr("行程单") }, { key: "card", text: qsTr("紧急信息卡") }, { key: "ics", text: qsTr("日历 .ics") }]
                    current: root.kind
                    onPicked: (k) => { root.kind = k; root.refresh() }
                }
                Label { visible: root.kind !== "ics"; text: qsTr("格式") }
                Segmented {
                    objectName: "exportFormat"
                    visible: root.kind !== "ics"
                    width: parent.width
                    fill: true
                    options: [{ key: "pdf", text: qsTr("PDF（A4，适合打印）") }, { key: "png", text: qsTr("长图（适合发手机）") }]
                    current: root.format
                    onPicked: (k) => root.format = k
                }
                Label { visible: root.kind === "itinerary"; text: qsTr("包含") }
                Column {
                    visible: root.kind === "itinerary"
                    width: parent.width
                    spacing: Theme.px(8)
                    Repeater {
                        model: [{ key: "refs", text: qsTr("订单号") }, { key: "money", text: qsTr("消费（费用和消费明细）") },
                                { key: "notes", text: qsTr("备注") }, { key: "emergency", text: qsTr("紧急信息（保险、紧急联系人、当地电话）") }]
                        delegate: TpCheck {
                            required property var modelData
                            objectName: "exportOpt-" + modelData.key
                            width: form.width
                            text: modelData.text
                            checked: !!root.opts[modelData.key]
                            onToggled: (c) => root.setOpt(modelData.key, c)
                        }
                    }
                }
                Text {
                    visible: root.kind === "ics" && root.problem === ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    lineHeight: 1.25
                    text: qsTr("导入手机日历：计划日期（全天）、交通住宿、每天的日程，共 %1 项。时间按当地时间写，不带时区。").arg(root.info.icsCount || 0)
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
                }
                Text {
                    objectName: "exportProblem"
                    visible: root.problem !== ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: root.problem
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.danger
                }
            }
            // 第一页的预览
            Rectangle {
                id: prev
                visible: root.kind !== "ics"
                width: Theme.px(root.wide ? 220 : 160)
                height: width * 1.414
                color: "#ffffff"
                border.width: 1; border.color: Theme.line
                radius: Theme.px(4)
                Image {
                    objectName: "exportPreview"
                    anchors.fill: parent
                    anchors.margins: 1
                    source: root.previewUrl
                    cache: false
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }
                Text {
                    anchors.top: parent.bottom
                    anchors.topMargin: Theme.px(4)
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("第一页预览")
                    font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
                }
            }
        }
        Item { width: 1; height: Theme.px(4) }
        Row {
            anchors.right: parent.right
            spacing: Theme.px(8)
            TpButton { text: qsTr("取消"); onClicked: root.open = false }
            TpButton { objectName: "exportGo"; kind: "primary"; text: qsTr("导出…"); enabled: root.problem === ""; opacity: enabled ? 1 : 0.5; onClicked: root.begin() }
        }

    }

    FileDialog {
        id: saveDialog
        title: qsTr("导出到")
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.kind === "ics" ? "ics" : root.format
        nameFilters: root.kind === "ics" ? [qsTr("日历文件") + " (*.ics)"] : root.format === "png" ? [qsTr("图片") + " (*.png)"] : ["PDF (*.pdf)"]
        onAccepted: root.write(selectedFile)
    }

    // E-10：还有没按道路算的段（Electron askRoads）
    ChoiceDialog {
        id: roadAsk
        objectName: "exportRoadAsk"
        property int count: 0
        readonly property bool canNet: !!netc.allowed.routing
        title: count === 1 ? qsTr("还有 1 段路线没按道路算") : qsTr("还有 %1 段路线没按道路算").arg(count)
        message: qsTr("这些自驾 / 徒步段现在画的是直线。直接导出的话，行程单的路线简图上也是直线。")
        cancelKey: "cancel"
        options: [
            { key: "calc", disabled: !canNet, title: qsTr("先算好再导出"),
              detail: canNet ? qsTr("约 %1 分钟，算完自动保存，然后接着导出").arg(Math.max(1, Math.ceil(count * 1.4 / 60)))
                             : qsTr("道路路线已关闭（设置 → 联网服务），现在没法算") },
            { key: "skip", title: qsTr("直接导出"), detail: qsTr("路线简图上先画直线") },
            { key: "cancel", title: qsTr("取消"), detail: "" }
        ]
        onChosen: (key) => {
            open = false
            if (key === "skip") root.pick()
            else if (key === "calc") {
                roads.startFor(root.planId)
                if (roads.running) roadWait.armed = true
                else root.pick()   // 被挡住（编辑器开着）时 roads 自己提示；已经没有要算的了就直接选位置
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
            if (root.open) { root.refresh(); root.pick() }
        }
    }
}
