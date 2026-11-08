// 导入照片的确认表（0.7.0，独立组件，PO 2026-11-03；阶段 ④ 计划页、⑤ 相册 / 游记的"添加照片"直接用它）。
// 跟着 photos.importStage 走：reading（读拍摄信息，进度）→ review（每张一行，可以改计划和站点、勾掉不导入）→ copying（复制，进度）。
// 用法：photos.startImport(文件列表, 计划id, 站点id) 之后它自己出现；结果由 photos.importFinished 通知（Main.qml 里弹提示）。
import QtQuick
import TPApp

ModalLayer {
    id: root
    readonly property string stage: photos.importStage
    readonly property var items: photos.importItems
    property var plans: []
    open: stage !== ""
    cardWidth: stage === "review" ? 820 : 460
    dismissible: stage === "review"
    onDismissed: photos.cancelImport()
    onStageChanged: if (stage === "review") plans = photos.planOptions()

    readonly property var planChoices: {
        const o = [{ key: "", text: qsTr("未归类") }]
        for (let i = 0; i < plans.length; ++i) o.push({ key: plans[i].id, text: plans[i].name })
        return o
    }
    readonly property int nAuto: { let n = 0; for (let i = 0; i < items.length; ++i) if (items[i].plan !== "" && items[i].why !== "") n++; return n }
    readonly property int nDup: { let n = 0; for (let i = 0; i < items.length; ++i) if (items[i].dup) n++; return n }
    readonly property int nImport: { let n = 0; for (let i = 0; i < items.length; ++i) if (!items[i].skip) n++; return n }

    // 改一行时整张表会重建：记住滚动位置，改完再滚回去
    function keep(fn) {
        const y = listView.contentY
        fn()
        Qt.callLater(() => listView.contentY = Math.min(y, Math.max(0, listView.contentHeight - listView.height)))
    }
    function metaLine(it) {
        const p = []
        p.push(it.takenAt !== "" ? it.takenAt.replace("T", " ") + (it.tz !== "" ? " (UTC" + it.tz + ")" : "") : qsTr("没有拍摄时间"))
        if (it.hasGps) p.push(qsTr("有位置"))
        if (it.why === "date") p.push(qsTr("按拍摄日期"))
        else if (it.why === "gps") p.push(qsTr("按拍摄位置"))
        if (it.camera !== "") p.push(it.camera)
        return p.join(" · ")
    }

    // ---- 读取中 / 复制中 ----
    Text {
        visible: root.stage !== "review"
        width: parent.width
        text: root.stage === "copying" ? qsTr("正在导入照片…") : qsTr("正在读取照片的拍摄信息…")
        font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
    }
    Text {
        visible: root.stage !== "review"
        width: parent.width
        wrapMode: Text.Wrap
        text: (photos.importTotal > 0 ? qsTr("%1 / %2").arg(photos.importDone).arg(photos.importTotal) + "\n" : "")
              + (root.stage === "copying" ? qsTr("取消的话，已经复制的照片会删掉，数据不变。") : qsTr("只读拍摄时间、位置和相机信息，不改原文件。"))
        lineHeight: 1.3
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.textSoft
    }
    Rectangle {
        visible: root.stage !== "review"
        width: parent.width
        height: Theme.px(6)
        radius: height / 2
        color: Theme.panel2
        Rectangle {
            width: photos.importTotal > 0 ? parent.width * photos.importDone / photos.importTotal : 0
            height: parent.height
            radius: height / 2
            color: Theme.accent
            Behavior on width { TpMove { ms: 120 } }
        }
    }
    Item {
        visible: root.stage !== "review"
        width: parent.width
        height: Theme.px(40)
        TpButton { anchors.right: parent.right; text: qsTr("取消"); onClicked: photos.cancelImport() }
    }

    // ---- 确认表 ----
    Text {
        visible: root.stage === "review"
        width: parent.width
        text: qsTr("导入 %1 张照片").arg(root.items.length)
        font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
    }
    Text {
        visible: root.stage === "review"
        width: parent.width
        wrapMode: Text.Wrap
        lineHeight: 1.3
        text: qsTr("%1 张已自动归类，%2 张没认出来，先放“未归类”。")
                  .arg(root.nAuto).arg(root.items.length - root.nAuto)
              + (root.nDup > 0 ? " " + qsTr("<b>%1 张照片库里已经有一样的</b>，默认不导入。").arg(root.nDup) : "")
        textFormat: Text.StyledText
        font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
    }
    Row {
        visible: root.stage === "review"
        spacing: Theme.px(8)
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("全部改成：")
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
        }
        TpSelect {
            width: Theme.px(260)
            options: [{ key: "__keep", text: qsTr("（不改）") }].concat(root.planChoices)
            current: "__keep"
            onPicked: (k) => { if (k !== "__keep") photos.setAllPlan(k) }
        }
    }
    Rectangle {
        visible: root.stage === "review"
        width: parent.width
        height: Math.min(listView.contentHeight + 2, Math.max(Theme.px(200), (root.height || 600) - Theme.px(330)))
        radius: Theme.radiusSmall
        color: "transparent"
        border.width: 1
        border.color: Theme.line
        clip: true
        ListView {
            id: listView
            anchors.fill: parent
            anchors.margins: 1
            model: root.items
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width: listView.width
                height: Theme.px(68)
                color: index % 2 === 0 ? Qt.rgba(1, 1, 1, 0.6) : "transparent"
                opacity: modelData.skip ? 0.55 : 1
                Rectangle { visible: row.index > 0; width: parent.width; height: 1; color: "#e4eaef" }

                TpCheck {
                    id: chk
                    x: Theme.px(10)
                    anchors.verticalCenter: parent.verticalCenter
                    checked: !row.modelData.skip
                    Accessible.name: qsTr("导入这张")
                    onToggled: (c) => root.keep(() => photos.setItemSkip(row.index, !c))
                }
                Rectangle {
                    id: thumbBox
                    anchors.left: chk.right
                    anchors.leftMargin: Theme.px(10)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(52); height: width
                    radius: Theme.px(8)
                    color: Theme.panel2
                    clip: true
                    Image {
                        id: thumb
                        anchors.fill: parent
                        source: row.modelData.heic && !photos.heicReady() ? "" : row.modelData.thumb
                        sourceSize.width: 320
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: (row.modelData.heic && !photos.heicReady()) || thumb.status === Image.Error
                        text: row.modelData.heic ? "HEIC" : "?"
                        font.pixelSize: Theme.px(11); font.bold: true; color: Theme.muted
                    }
                }
                Column {
                    anchors.left: thumbBox.right
                    anchors.leftMargin: Theme.px(12)
                    anchors.right: planSel.left
                    anchors.rightMargin: Theme.px(10)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.px(3)
                    Text {
                        width: parent.width
                        text: row.modelData.name
                        elide: Text.ElideMiddle
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
                    }
                    Text {
                        width: parent.width
                        text: root.metaLine(row.modelData)
                        elide: Text.ElideRight
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                    Text {
                        visible: row.modelData.dup || (row.modelData.heic && !photos.heicReady())
                        width: parent.width
                        text: row.modelData.dup ? (row.modelData.dupInBatch ? qsTr("这一批里选了两次") : qsTr("照片库里已经有一样的"))
                                                : qsTr("HEIC 能导入，但要装 HEIF / HEVC 扩展才能显示")
                        elide: Text.ElideRight
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.warn
                    }
                }
                TpSelect {
                    id: planSel
                    anchors.right: stopSel.left
                    anchors.rightMargin: Theme.px(8)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(190)
                    options: root.planChoices
                    current: row.modelData.plan
                    onPicked: (k) => root.keep(() => photos.setItemPlan(row.index, k))
                }
                TpSelect {
                    id: stopSel
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.px(10)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(170)
                    enabled: row.modelData.plan !== ""
                    placeholder: "—"
                    options: {
                        const s = row.modelData.plan !== "" ? photos.stopOptions(row.modelData.plan) : []
                        const o = []
                        for (let i = 0; i < s.length; ++i) o.push({ key: s[i].id, text: s[i].name })
                        return o
                    }
                    current: row.modelData.stop
                    onPicked: (k) => root.keep(() => photos.setItemStop(row.index, k))
                }
            }
        }
    }
    Item {
        visible: root.stage === "review"
        width: parent.width
        height: Theme.px(46)
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: Theme.px(10)
            TpButton { text: qsTr("取消"); onClicked: photos.cancelImport() }
            TpButton {
                text: root.nImport > 0 ? qsTr("导入 %1 张").arg(root.nImport) : qsTr("导入")
                kind: "primary"
                enabled: root.nImport > 0
                onClicked: photos.confirmImport()
            }
        }
    }
}
