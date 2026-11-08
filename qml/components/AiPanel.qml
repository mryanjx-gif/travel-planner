// AI 助手面板（0.13.0，I-11；Electron assistant.js build / draw）：液态玻璃，右下 AI 泡泡上方。
// 0.13.1 能改计划：回答里的修改预览（AiProposal）、查找进度和"继续查 / 先到这里"、做了什么（读了哪些计划、查了什么）、"没有联网核实"。
// 0.13.0：Enter 发送 / Shift+Enter 换行 / 输入法组字时不误发（TpField）、■ 停止、
// 快捷动作、麦克风（替用户按 Win+H）、每条回复下小字写 token 和花费；没钥匙时显示引导卡；第一次发给某家前提示一次。
import QtQuick
import TPApp

GlassPanel {
    id: root
    objectName: "aiPanel"
    property bool open: false
    property bool histOpen: false
    property bool orderMode: false   // 0.13.2 从订单生成（I-12）：下一句是粘贴的订单文字
    property bool backEdit: false    // 1.1.0 窄窗口盖在编辑器上：顶部"回到编辑"
    signal backRequested()
    signal settingsRequested()
    signal closeRequested()
    signal toastRequested(string text, bool error)

    radius: Theme.radiusLarge
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    Behavior on opacity { TpAnim { shown: root.open } }
    transform: Translate { y: root.open || !Theme.motion ? 0 : Theme.px(16) }

    onOpenChanged: if (open) { histOpen = false; focusTimer.start() } else orderMode = false
    Timer { id: focusTimer; interval: 60; onTriggered: if (ai.hasKey) input.focusEnd() }
    function send(text) {
        const t = (text === undefined ? input.text : text).trim()
        if (t === "" || ai.busy) return
        if (root.orderMode) { root.orderMode = false; if (ai.sendOrder(t) || ai.pendingTell !== "") input.clear(); return }
        if (ai.send(t) || ai.pendingTell !== "") input.clear()
    }
    // 快捷动作（Electron quickActs 没选中计划时的三个；0.13.1 有了计划上下文才答得上来）
    readonly property var quick: [
        { k: "season", t: qsTr("最近适合去哪"), q: qsTr("接下来两三个月适合去哪？各说一句理由。") },
        { k: "visa", t: qsTr("出国要准备什么"), q: qsTr("第一次出国自由行，出发前要准备哪些东西？只列要点。") },
        { k: "pack", t: qsTr("行李清单"), q: qsTr("给我一份一周海岛旅行的行李清单，按类别分。") }
    ]

    Keys.onEscapePressed: root.closeRequested()

    // ---- 头部 ----
    Item {
        id: head
        x: Theme.px(18); y: Theme.px(12)
        width: parent.width - Theme.px(30)
        height: Theme.px(38)
        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - btns.width - Theme.px(8)
            Text {
                objectName: "aiBackEdit"
                visible: root.backEdit
                text: "‹ " + qsTr("回到编辑")
                font.family: serifFamily; font.pixelSize: Theme.px(16); font.bold: true; color: Theme.accent
                MouseArea { anchors.fill: parent; anchors.margins: -Theme.px(4); cursorShape: Qt.PointingHandCursor; onClicked: root.backRequested() }
            }
            Text { visible: !root.backEdit; text: qsTr("AI 助手"); font.family: serifFamily; font.pixelSize: Theme.px(16); font.bold: true; color: Theme.text }
            Text {
                objectName: "aiHeadModel"
                width: parent.width
                elide: Text.ElideRight
                text: ai.providerName + (ai.model ? " · " + ai.model : "")
                font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
            }
        }
        Row {
            id: btns
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(2)
            Repeater {
                model: [{ k: "clock", t: qsTr("最近的对话") }, { k: "plus", t: qsTr("新对话") }, { k: "gear", t: qsTr("AI 设置") }, { k: "close", t: qsTr("关闭（Esc）") }]
                delegate: Rectangle {
                    id: hb
                    required property var modelData
                    objectName: "aiHead-" + modelData.k
                    width: Theme.px(32); height: width; radius: width / 2
                    color: hbMa.containsMouse || (modelData.k === "clock" && root.histOpen) ? Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.07) : "transparent"
                    opacity: (modelData.k === "plus" || modelData.k === "clock") && ai.busy ? 0.35 : 1
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.t
                    TpIcon { anchors.centerIn: parent; width: Theme.px(17); height: width; name: hb.modelData.k; color: Theme.textSoft }
                    MouseArea {
                        id: hbMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const k = hb.modelData.k
                            if (k === "close") root.closeRequested()
                            else if (k === "gear") root.settingsRequested()
                            else if (ai.busy) return
                            else if (k === "plus") { ai.newChat(); root.histOpen = false; input.focusEnd() }
                            else root.histOpen = !root.histOpen
                        }
                    }
                    TpTip { text: hb.modelData.t; show: hbMa.containsMouse }
                }
            }
        }
    }

    // ---- 正文：引导卡 / 对话 ----
    Item {
        id: body
        x: Theme.px(14)
        y: head.y + head.height + Theme.px(8)
        width: parent.width - Theme.px(28)
        height: foot.y - y - Theme.px(6)
        clip: true

        // 没钥匙：各家申请页（外部浏览器打开），去设置填
        Flickable {
            id: guide
            objectName: "aiGuide"
            anchors.fill: parent
            visible: !ai.hasKey
            contentHeight: guideCol.height
            boundsBehavior: Flickable.StopAtBounds
            AiKeyGuide {
                id: guideCol
                width: parent.width
                onSettingsRequested: root.settingsRequested()
            }
        }

        ListView {
            id: list
            objectName: "aiList"
            anchors.fill: parent
            visible: ai.hasKey
            model: ai.messages
            spacing: Theme.px(10)
            boundsBehavior: Flickable.StopAtBounds
            onCountChanged: Qt.callLater(() => list.positionViewAtEnd())
            Connections { target: ai; function onLiveChanged() { if (ai.busy && list.atYEnd === false && list.contentHeight - list.contentY - list.height < Theme.px(80)) list.positionViewAtEnd() } }
            header: Item { width: list.width; height: list.count === 0 ? emptyCol.height + Theme.px(8) : 0
                Column {
                    id: emptyCol
                    visible: list.count === 0
                    width: parent.width
                    spacing: Theme.px(10)
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("问行程、交通，或让我改计划。改动先预览。") + (ai.noMoney ? (app.lang === "en" ? " " : "") + qsTr("发给 AI 的内容不含消费和订单号。") : "")
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.text
                    }
                    Flow {
                        width: parent.width
                        spacing: Theme.px(6)
                        Repeater {
                            model: root.quick
                            delegate: TpChip {
                                required property var modelData
                                objectName: "aiQuick-" + modelData.k
                                text: modelData.t
                                onClicked: root.send(modelData.q)
                            }
                        }
                        // 0.13.2 I-12（Electron quickActs 的"从订单生成交通住宿"）：切到订单模式，下一句粘贴订单文字
                        TpChip {
                            objectName: "aiQuick-order"
                            text: qsTr("从订单生成交通住宿")
                            on: root.orderMode
                            onClicked: { root.orderMode = !root.orderMode; input.focusEnd() }
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("本月约 %1 / 上限 %2").arg(ai.money(ai.used, ai.mainCur)).arg(ai.money(ai.cap, ai.mainCur))
                        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                    }
                }
            }
            delegate: Item {
                id: msg
                required property var modelData
                required property int index
                readonly property string role: modelData.role
                width: list.width
                visible: !modelData.hidden
                height: modelData.hidden ? 0 : role === "user" ? bubble.height : role === "note" ? noteBox.height : botCol.height
                // 用户的话：右边的浅色气泡
                Rectangle {
                    id: bubble
                    visible: msg.role === "user"
                    anchors.right: parent.right
                    width: Math.min(parent.width * 0.86, userText.implicitWidth + Theme.px(24))
                    height: userText.height + Theme.px(16)
                    radius: Theme.radiusSmall
                    color: Theme.accentSoft
                    Text {
                        id: userText
                        x: Theme.px(12); y: Theme.px(8)
                        width: Math.min(implicitWidth, msg.width * 0.86 - Theme.px(24))
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        text: msg.role === "user" ? msg.modelData.text : ""
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.text
                    }
                }
                // 说明（没钥匙、到上限、截断）
                Rectangle {
                    id: noteBox
                    objectName: "aiNote"
                    visible: msg.role === "note"
                    width: parent.width
                    height: noteCol.height + Theme.px(16)
                    radius: Theme.radiusSmall
                    color: Qt.rgba(1, 1, 1, 0.55)
                    border.width: 1; border.color: Theme.line
                    Column {
                        id: noteCol
                        x: Theme.px(12); y: Theme.px(8)
                        width: parent.width - Theme.px(24)
                        spacing: Theme.px(6)
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: msg.role === "note" ? msg.modelData.text : ""
                            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                        }
                        TpButton {
                            objectName: "aiRaiseCap"
                            visible: msg.modelData.kind === "cap" && ai.capReached
                            compact: true
                            text: qsTr("提高本月上限")
                            onClicked: ai.raiseCap()
                        }
                        TpButton {
                            visible: msg.modelData.kind === "nokey"
                            compact: true
                            text: qsTr("去设置")
                            onClicked: root.settingsRequested()
                        }
                    }
                }
                // AI 的回答：思考（折叠的一行）、正文、来源、错误、用量
                Column {
                    id: botCol
                    visible: msg.role === "assistant"
                    width: parent.width
                    spacing: Theme.px(4)
                    readonly property bool live: msg.modelData.live === true
                    readonly property string thinking: live ? ai.liveThinking : (msg.modelData.thinking || "")
                    Text {
                        objectName: "aiAct"
                        visible: (msg.modelData.act || []).length > 0
                        width: parent.width
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        text: (msg.modelData.act || []).join(" · ")
                        font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
                    }
                    Text {
                        visible: botCol.thinking !== ""
                        width: parent.width
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                        text: qsTr("思考：") + botCol.thinking.replace(/\s+/g, " ").slice(-160)
                        font.family: serifFamily; font.pixelSize: Theme.px(11.5); font.italic: true; color: Theme.muted
                    }
                    Text {
                        objectName: "aiAnswer"
                        width: parent.width
                        wrapMode: Text.Wrap
                        textFormat: Text.StyledText
                        visible: text !== ""
                        text: botCol.live ? ai.liveText : (msg.modelData.html || "")
                        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.text
                        lineHeight: 1.2
                    }
                    Text {
                        visible: botCol.live && ai.liveText === "" && botCol.thinking === ""
                        text: "…"
                        font.pixelSize: Theme.px(18); color: Theme.muted
                    }
                    Text {
                        objectName: "aiNoWeb"
                        visible: !botCol.live && msg.modelData.noWeb === true
                        text: "⊘ " + qsTr("没有联网核实")
                        font.family: serifFamily; font.pixelSize: Theme.px(11); color: Theme.warn
                    }
                    // 查找进度（research_status）：到预计时间停下来问
                    Rectangle {
                        objectName: "aiResearch"
                        visible: !!msg.modelData.research
                        width: parent.width
                        height: visible ? rsCol.height + Theme.px(16) : 0
                        radius: Theme.radiusSmall
                        readonly property var r: msg.modelData.research || ({})
                        color: r.paused ? "#ffffff" : Qt.rgba(1, 1, 1, 0.55)
                        border.width: 1; border.color: r.paused ? Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.5) : Theme.line
                        Column {
                            id: rsCol
                            x: Theme.px(12); y: Theme.px(8)
                            width: parent.width - Theme.px(24)
                            spacing: Theme.px(4)
                            readonly property var r: parent.r
                            Item {
                                width: parent.width; height: Theme.px(18)
                                Text { text: rsCol.r.finished ? qsTr("查完了") : rsCol.r.paused ? qsTr("到预计时间了") : qsTr("查找进度"); font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.text }
                                Text { anchors.right: parent.right; text: qsTr("已用 %1").arg(rsCol.r.usedText || ""); font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted; font.features: Theme.tnum }
                            }
                            Text { visible: (rsCol.r.note || "") !== ""; width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: rsCol.r.note || ""; font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft }
                            Text { visible: (rsCol.r.done || []).length > 0; width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: qsTr("已查：") + (rsCol.r.done || []).join("、"); font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted }
                            Text {
                                visible: (rsCol.r.todo || []).length > 0 && !rsCol.r.finished
                                width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText
                                text: qsTr("还要查：") + (rsCol.r.todo || []).join("、") + (rsCol.r.paused ? qsTr("，%1").arg(rsCol.r.secsText) : "")
                                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
                            }
                            Row {
                                visible: rsCol.r.paused === true && rsCol.r.answered !== true && !ai.busy
                                anchors.right: parent.right
                                spacing: Theme.px(6)
                                TpButton { objectName: "aiRsStop"; compact: true; text: qsTr("先到这里"); onClicked: ai.researchAnswer(rsCol.r.mi, false) }
                                TpButton { objectName: "aiRsGo"; compact: true; kind: "primary"; text: qsTr("继续查（%1）").arg(rsCol.r.secsText || ""); onClicked: ai.researchAnswer(rsCol.r.mi, true) }
                            }
                        }
                    }
                    // 1.1.0 Claude 的来源：默认收起的灰色小字，点开列网址（Anthropic 要求，技术选型.md §11）
                    Column {
                        id: srcCol
                        objectName: "aiSources"
                        visible: !botCol.live && (msg.modelData.sources || []).length > 0
                        width: parent.width
                        property bool open: false
                        Text {
                            text: qsTr("来源") + (srcCol.open ? " ▴" : " ▾")
                            font.family: serifFamily; font.pixelSize: Theme.px(11); color: Theme.muted
                            MouseArea { anchors.fill: parent; anchors.margins: -Theme.px(4); cursorShape: Qt.PointingHandCursor; onClicked: srcCol.open = !srcCol.open }
                        }
                        Repeater {
                            model: srcCol.open ? (msg.modelData.sources || []) : []
                            delegate: Text {
                                required property var modelData
                                width: srcCol.width
                                elide: Text.ElideRight
                                text: modelData
                                font.family: serifFamily; font.pixelSize: Theme.px(11); color: Theme.muted
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally(parent.modelData) }
                            }
                        }
                    }
                    Repeater {
                        model: msg.modelData.props || []
                        delegate: AiProposal {
                            required property var modelData
                            width: botCol.width
                            p: modelData
                        }
                    }
                    Text {
                        visible: !!msg.modelData.error
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: msg.modelData.error || ""
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5)
                        color: msg.modelData.error === qsTr("已停止") ? Theme.muted : Theme.danger
                    }
                    Text {
                        objectName: "aiMeta"
                        visible: !botCol.live && !!msg.modelData.meta
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: msg.modelData.meta || ""
                        font.family: serifFamily; font.pixelSize: Theme.px(11); color: Theme.muted
                        font.features: Theme.tnum
                    }
                }
            }
        }

        // 最近的对话
        Rectangle {
            objectName: "aiHist"
            visible: root.histOpen
            width: parent.width
            height: Math.min(histCol.height + Theme.px(12), parent.height)
            radius: Theme.radiusSmall
            color: Theme.panelLight
            border.width: 1; border.color: Theme.line
            Column {
                id: histCol
                x: Theme.px(6); y: Theme.px(6)
                width: parent.width - Theme.px(12)
                Text {
                    visible: ai.chats.length === 0
                    padding: Theme.px(8)
                    text: qsTr("还没有保存的对话")
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
                }
                Repeater {
                    model: ai.chats
                    delegate: Rectangle {
                        id: hr
                        required property var modelData
                        width: histCol.width
                        height: Theme.px(34)
                        radius: Theme.radiusControl
                        color: hrMa.containsMouse || modelData.id === ai.chatId ? Theme.accentSoft : "transparent"
                        Text {
                            x: Theme.px(8); anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.px(90)
                            elide: Text.ElideRight
                            text: hr.modelData.title || qsTr("（没有标题）")
                            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text
                        }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: Theme.px(8); anchors.verticalCenter: parent.verticalCenter
                            text: hr.modelData.at
                            font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
                        }
                        MouseArea { id: hrMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { ai.openChat(hr.modelData.id); root.histOpen = false } }
                    }
                }
            }
        }
    }

    // ---- 底部：第一次发给这家的提示、进度、输入框、档位 / 麦克风 / 花费 ----
    Column {
        id: foot
        x: Theme.px(14)
        width: parent.width - Theme.px(28)
        y: parent.height - height - Theme.px(12)
        spacing: Theme.px(6)
        visible: ai.hasKey

        Rectangle {
            objectName: "aiTell"
            visible: ai.pendingTell !== ""
            width: parent.width
            height: tellCol.height + Theme.px(20)
            radius: Theme.radiusSmall
            color: "#ffffff"
            border.width: 1; border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.5)
            Column {
                id: tellCol
                x: Theme.px(12); y: Theme.px(10)
                width: parent.width - Theme.px(24)
                spacing: Theme.px(8)
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("第一次用 %1：对话会发给 %1，按它的条款保存。").arg(ai.pendingTell)
                    font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.text
                }
                Row {
                    spacing: Theme.px(8)
                    TpButton { objectName: "aiTellOk"; kind: "primary"; compact: true; text: qsTr("知道了，发送"); onClicked: ai.confirmTell() }
                    TpButton { objectName: "aiTellCancel"; compact: true; text: qsTr("先不发"); onClicked: ai.cancelTell() }
                }
            }
        }
        Item {
            objectName: "aiOrderBar"
            visible: root.orderMode && !ai.busy
            width: parent.width
            height: Theme.px(22)
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - orderCancel.width - Theme.px(8)
                elide: Text.ElideRight
                text: qsTr("从订单生成交通住宿 · 用“快”档模型，只出修改预览")
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.accentInk
            }
            Text {
                id: orderCancel
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("取消")
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.accent
                MouseArea { anchors.fill: parent; anchors.margins: -Theme.px(4); cursorShape: Qt.PointingHandCursor; onClicked: root.orderMode = false }
            }
        }
        Text {
            objectName: "aiRun"
            visible: ai.runText !== ""
            text: ai.runText
            width: parent.width; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
            font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
            font.features: Theme.tnum
        }
        Item {
            width: parent.width
            height: input.height
            TpField {
                id: input
                objectName: "aiInput"
                width: parent.width - sendBtn.width - Theme.px(8)
                multiline: true
                maxLines: 6
                submitOnEnter: true
                placeholder: root.orderMode ? qsTr("把订单确认邮件或 PDF 里的文字粘贴到这里") : qsTr("问点什么")
                onAccepted: root.send()
                onEscaped: root.closeRequested()
            }
            Rectangle {
                id: sendBtn
                objectName: ai.busy ? "aiStop" : "aiSend"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: Theme.px(34); height: width; radius: width / 2
                color: ai.busy ? Theme.textSoft : input.text.trim() !== "" ? Theme.accent : Theme.faint   // 1.1.0 没输入时灰
                Accessible.role: Accessible.Button
                Accessible.name: ai.busy ? qsTr("停止") : qsTr("发送")
                TpIcon { anchors.centerIn: parent; width: Theme.px(17); height: width; name: ai.busy ? "square" : "send"; color: "#ffffff"; stroke: 2.2 }
                MouseArea { id: sendMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ai.busy ? ai.stop() : root.send() }
                TpTip { text: ai.busy ? qsTr("停止") : qsTr("发送"); show: sendMa.containsMouse }
            }
        }
        Item {
            width: parent.width
            height: Theme.px(28)
            Segmented {
                id: tierSeg
                objectName: "aiTier"
                anchors.verticalCenter: parent.verticalCenter
                enabled: !ai.busy
                options: [{ key: "fast", text: qsTr("快") }, { key: "balanced", text: qsTr("均衡") }, { key: "careful", text: qsTr("细致") }]
                current: ai.tier
                onPicked: (k) => ai.tier = k
            }
            Rectangle {
                id: micBtn
                objectName: "aiMic"
                anchors.left: tierSeg.right; anchors.leftMargin: Theme.px(6)
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.px(28); height: width; radius: width / 2
                color: micMa.containsMouse ? Qt.rgba(22 / 255, 32 / 255, 42 / 255, 0.07) : "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("语音输入")
                TpIcon { anchors.centerIn: parent; width: Theme.px(16); height: width; name: "mic"; color: Theme.textSoft }
                MouseArea {
                    id: micMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        input.focusEnd()
                        if (app.listPref("aiVoiceHint") === "") { app.setListPref("aiVoiceHint", "1"); root.toastRequested(qsTr("用的是 Windows 语音输入；中文要先装中文语言包。"), false) }
                        if (!ai.voice()) root.toastRequested(qsTr("没能自动打开语音输入，直接按 Win+H 也可以"), true)
                    }
                }
                TpTip { text: qsTr("语音输入（Win+H）"); show: micMa.containsMouse }
            }
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - micBtn.x - micBtn.width - Theme.px(8)
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                text: ai.convCost
                font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
                font.features: Theme.tnum
            }
        }
    }
}
