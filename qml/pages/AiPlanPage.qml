// 让 AI 帮我规划（0.13.2，PO 2026-11-08 选"单独整页向导"，阶段7设计预案.md §〇 8）：整页，和日历 / 账本同一套样式和进出动效。
// 五步：① 出发地、目的地 ② 日期或天数 ③ 人数、预算（主币种）④ 节奏、兴趣 ⑤ 必去 / 不去。每步可跳过（清掉这一步再往下），上一步 / 下一步。
// 提交：先问 ai.blockReason()（离线 / 到上限等拦下就留在这页说明），再 ai.planWizard(form) 开一段新对话发出去，转到地图上的 AI 面板接着聊。
// 没钥匙时整页只显示引导卡（AiKeyGuide）。表单整理成哪句话见 src/ai/aiprompt.cpp wizardMessage。
import QtQuick
import TPApp

Item {
    id: root
    property bool shown: false
    signal settingsRequested()
    signal submitted()

    // ---- 表单 ----
    property int step: 0
    property string fFrom: ""
    property string fTo: ""
    property string dateMode: "dates"   // dates / days
    property string fStart: ""
    property string fEnd: ""
    property string fDays: ""
    property string fPeople: ""
    property string fBudget: ""
    property string fPace: ""           // slow / normal / fast；空 = 没选
    property var fInterests: []
    property string fMust: ""
    property string fAvoid: ""
    property string blockText: ""
    component StepLabel: Text { width: parent ? parent.width : 0; wrapMode: Text.Wrap; font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text }
    component StepHint: Text { width: parent ? parent.width : 0; wrapMode: Text.Wrap; font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted }
    readonly property var steps: [qsTr("去哪儿"), qsTr("日期"), qsTr("人数预算"), qsTr("节奏兴趣"), qsTr("必去不去")]
    readonly property var interestList: [qsTr("美食"), qsTr("自然风光"), qsTr("徒步登山"), qsTr("历史古迹"), qsTr("博物馆"), qsTr("购物"), qsTr("海滩"), qsTr("摄影"), qsTr("亲子"), qsTr("夜生活")]

    function form() {
        return { from: fFrom, to: fTo, start: dateMode === "dates" ? fStart : "", end: dateMode === "dates" ? fEnd : "",
                 days: dateMode === "days" ? fDays : "", people: fPeople, budget: fBudget, pace: fPace, interests: fInterests, must: fMust, avoid: fAvoid }
    }
    function reset() {
        step = 0; fFrom = ""; fTo = ""; dateMode = "dates"; fStart = ""; fEnd = ""; fDays = ""
        fPeople = ""; fBudget = ""; fPace = ""; fInterests = []; fMust = ""; fAvoid = ""; blockText = ""
    }
    function back() { if (step > 0) { step--; blockText = "" } }
    function next() { blockText = ""; if (step < steps.length - 1) step++; else submit() }
    // 跳过：这一步填了的清掉，再往下
    function skip() {
        if (step === 0) { fFrom = ""; fTo = "" }
        else if (step === 1) { fStart = ""; fEnd = ""; fDays = "" }
        else if (step === 2) { fPeople = ""; fBudget = "" }
        else if (step === 3) { fPace = ""; fInterests = [] }
        else { fMust = ""; fAvoid = "" }
        next()
    }
    // 截图用：填一份样子，停在第 n 步
    function shotFill(n) {
        fFrom = "上海"; fTo = "京都、奈良"; dateMode = "dates"; fStart = "2026-11-20"; fEnd = "2026-11-25"
        fPeople = "2"; fBudget = "12000"; fPace = "normal"; fInterests = [interestList[0], interestList[3], interestList[7]]
        fMust = "伏见稻荷、岚山小火车"; fAvoid = "红眼航班"; step = n
    }
    function toggleInterest(t) {
        const a = fInterests.slice(), i = a.indexOf(t)
        if (i >= 0) a.splice(i, 1); else a.push(t)
        fInterests = a
    }
    function submit() {
        const why = ai.blockReason()
        if (why !== "") { blockText = why; return }
        if (ai.busy) { blockText = qsTr("AI 正在回答上一句，等它写完再试。"); return }
        const ok = ai.planWizard(form())
        if (!ok && ai.pendingTell === "") { blockText = qsTr("没发出去，可以再试一次。"); return }
        reset()
        root.submitted()
    }

    visible: opacity > 0.01
    opacity: shown ? 1 : 0
    scale: shown || !Theme.motion ? 1 : 0.96
    transformOrigin: Item.TopLeft
    Behavior on scale { TpAnim { shown: root.shown; ms: 280 } }
    Behavior on opacity { enabled: Theme.motion; TpAnim { shown: root.shown } }
    onShownChanged: if (shown) blockText = ""

    GlassPanel {
        anchors.fill: parent
        anchors.topMargin: -Theme.radius   // 和账本一样贴着窗口顶
        radius: Theme.radius
        tint: Qt.rgba(250 / 255, 252 / 255, 253 / 255, Theme.glass ? 0.985 : 0.99)
        liquid: false
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }   // 不穿透到地图

    Flickable {
        id: flick
        objectName: "aiPlanFlick"
        anchors.fill: parent
        anchors.topMargin: topBar.barH
        contentHeight: col.implicitHeight + Theme.px(44)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Column {
            id: col
            y: Theme.px(8)
            width: Math.min(flick.width - Theme.px(32), Theme.px(560))
            x: (flick.width - width) / 2
            spacing: Theme.px(16)

            Column {
                width: parent.width
                spacing: Theme.px(4)
                Text { text: qsTr("让 AI 帮我规划"); font.family: serifFamily; font.pixelSize: Theme.px(24); font.bold: true; color: Theme.text }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("填几项就行，每一步都能跳过。AI 先出一份粗略方案，改动都先给你预览，之后在 AI 助手里接着细化。")
                    font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.textSoft; lineHeight: 1.25
                }
            }

            // 没钥匙：引导卡
            AiKeyGuide {
                objectName: "aiPlanGuide"
                prefix: "aiPlanGuide"
                visible: !ai.hasKey
                width: parent.width
                onSettingsRequested: root.settingsRequested()
            }

            // ---- 步数 ----
            // 窄窗口（最长的步名放不下）时只留圆点，下面一行写"当前步名 · 2 / 5"，不截断（0.13.3）
            FontMetrics { id: stepFm; font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: true }
            readonly property bool narrowSteps: {
                let m = 0
                for (const s of root.steps) m = Math.max(m, stepFm.advanceWidth(s))
                return m + Theme.px(6) > stepRow.width / root.steps.length
            }
            Row {
                id: stepRow
                objectName: "aiPlanSteps"
                visible: ai.hasKey
                width: parent.width
                Repeater {
                    model: root.steps
                    delegate: Item {
                        id: st
                        required property string modelData
                        required property int index
                        readonly property bool cur: index === root.step
                        readonly property bool done: index < root.step
                        objectName: "aiPlanStep-" + index
                        width: stepRow.width / root.steps.length
                        height: col.narrowSteps ? Theme.px(28) : Theme.px(52)
                        Rectangle {   // 连线
                            visible: st.index > 0
                            x: -width / 2; y: Theme.px(13)
                            width: st.width - Theme.px(30); height: 1.5
                            color: st.done || st.cur ? Theme.accent : Theme.line
                        }
                        Rectangle {
                            id: dot
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Theme.px(26); height: width; radius: width / 2
                            color: st.cur ? Theme.accent : st.done ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14) : "#ffffff"
                            border.width: st.cur ? 0 : 1
                            border.color: st.done ? Theme.accent : Theme.line
                            Text {
                                anchors.centerIn: parent
                                visible: !st.done
                                text: st.index + 1
                                font.family: serifFamily; font.pixelSize: Theme.px(12.5); font.bold: true
                                color: st.cur ? "#ffffff" : Theme.muted
                            }
                            TpIcon { anchors.centerIn: parent; visible: st.done; name: "check"; width: Theme.px(14); height: width; color: Theme.accentInk; stroke: 2.4 }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: dot.bottom; anchors.topMargin: Theme.px(5)
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            visible: !col.narrowSteps
                            text: st.modelData
                            font.family: serifFamily; font.pixelSize: Theme.px(12); font.bold: st.cur
                            color: st.cur ? Theme.text : Theme.muted
                        }
                    }
                }
            }
            Text {
                objectName: "aiPlanStepNarrow"
                visible: ai.hasKey && col.narrowSteps
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: root.steps[root.step] + "  ·  " + (root.step + 1) + " / " + root.steps.length
                font.family: serifFamily; font.pixelSize: Theme.px(12.5); font.bold: true; color: Theme.text
            }

            // ---- 这一步的内容 ----
            Rectangle {
                visible: ai.hasKey
                width: parent.width
                height: body.implicitHeight + Theme.px(36)
                radius: Theme.radiusSmall
                color: Qt.rgba(1, 1, 1, 0.72)
                border.width: 1; border.color: Theme.line
                Column {
                    id: body
                    x: Theme.px(18); y: Theme.px(18)
                    width: parent.width - Theme.px(36)
                    spacing: Theme.px(10)


                    // ① 出发地、目的地
                    Column {
                        objectName: "aiPlanPage-0"
                        visible: root.step === 0
                        width: parent.width
                        spacing: Theme.px(8)
                        StepLabel { text: qsTr("从哪里出发") }
                        TpField { objectName: "aiPlanFrom"; width: parent.width; placeholder: qsTr("城市，比如 上海"); value: root.fFrom; onEdited: (t) => root.fFrom = t }
                        StepLabel { text: qsTr("想去哪里") }
                        TpField { objectName: "aiPlanTo"; width: parent.width; placeholder: qsTr("城市、国家或一条路线，比如 京都、北海道环线"); value: root.fTo; onEdited: (t) => root.fTo = t }
                        StepHint { text: qsTr("还没想好可以跳过，AI 会先问你。") }
                    }
                    // ② 日期或天数
                    Column {
                        objectName: "aiPlanPage-1"
                        visible: root.step === 1
                        width: parent.width
                        spacing: Theme.px(8)
                        Segmented {
                            objectName: "aiPlanDateMode"
                            options: [{ key: "dates", text: qsTr("定了日期") }, { key: "days", text: qsTr("只定天数") }]
                            current: root.dateMode
                            onPicked: (k) => root.dateMode = k
                        }
                        Row {
                            visible: root.dateMode === "dates"
                            spacing: Theme.px(8)
                            DateField { objectName: "aiPlanStart"; width: Theme.px(150); placeholder: qsTr("开始日期"); value: root.fStart; onEdited: (v) => root.fStart = v }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "—"; color: Theme.muted }
                            DateField { objectName: "aiPlanEnd"; width: Theme.px(150); placeholder: qsTr("结束日期"); value: root.fEnd; minDate: root.fStart; bad: root.fStart !== "" && root.fEnd !== "" && root.fEnd < root.fStart; onEdited: (v) => root.fEnd = v }
                        }
                        Row {
                            visible: root.dateMode === "days"
                            spacing: Theme.px(8)
                            TpField { objectName: "aiPlanDays"; width: Theme.px(80); placeholder: "5"; value: root.fDays; onEdited: (t) => root.fDays = t.replace(/[^0-9]/g, "") }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("天"); font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.textSoft }
                        }
                    }
                    // ③ 人数、预算
                    Column {
                        objectName: "aiPlanPage-2"
                        visible: root.step === 2
                        width: parent.width
                        spacing: Theme.px(8)
                        StepLabel { text: qsTr("几个人") }
                        Row {
                            spacing: Theme.px(8)
                            TpField { objectName: "aiPlanPeople"; width: Theme.px(80); placeholder: "2"; value: root.fPeople; onEdited: (t) => root.fPeople = t.replace(/[^0-9]/g, "") }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("人"); font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.textSoft }
                        }
                        StepLabel { text: qsTr("预算（所有人合计）") }
                        Row {
                            spacing: Theme.px(8)
                            Text { anchors.verticalCenter: parent.verticalCenter; text: rates.main; font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.textSoft }
                            TpField { objectName: "aiPlanBudget"; width: Theme.px(140); placeholder: qsTr("不填就不考虑"); value: root.fBudget; onEdited: (t) => root.fBudget = t.replace(/[^0-9.]/g, "") }
                        }
                        StepHint { text: qsTr("人数和预算只用来定方案，不会写进计划。") }
                    }
                    // ④ 节奏、兴趣
                    Column {
                        objectName: "aiPlanPage-3"
                        visible: root.step === 3
                        width: parent.width
                        spacing: Theme.px(8)
                        StepLabel { text: qsTr("节奏") }
                        Segmented {
                            objectName: "aiPlanPace"
                            options: [{ key: "slow", text: qsTr("松") }, { key: "normal", text: qsTr("适中") }, { key: "fast", text: qsTr("紧") }]
                            current: root.fPace
                            onPicked: (k) => root.fPace = root.fPace === k ? "" : k
                        }
                        StepLabel { text: qsTr("兴趣（可以多选）") }
                        Flow {
                            width: parent.width
                            spacing: Theme.px(6)
                            Repeater {
                                model: root.interestList
                                delegate: TpChip {
                                    required property string modelData
                                    required property int index
                                    objectName: "aiPlanInterest-" + index
                                    text: modelData
                                    on: root.fInterests.indexOf(modelData) >= 0
                                    onClicked: root.toggleInterest(modelData)
                                }
                            }
                        }
                    }
                    // ⑤ 必去 / 不去
                    Column {
                        objectName: "aiPlanPage-4"
                        visible: root.step === 4
                        width: parent.width
                        spacing: Theme.px(8)
                        StepLabel { text: qsTr("一定要去的") }
                        TpField { objectName: "aiPlanMust"; width: parent.width; multiline: true; maxLines: 4; placeholder: qsTr("地方、店、活动都行，比如 伏见稻荷、岚山小火车"); value: root.fMust; onEdited: (t) => root.fMust = t }
                        StepLabel { text: qsTr("不想去的") }
                        TpField { objectName: "aiPlanAvoid"; width: parent.width; multiline: true; maxLines: 4; placeholder: qsTr("比如 人太多的景点、红眼航班"); value: root.fAvoid; onEdited: (t) => root.fAvoid = t }
                    }
                }
            }

            Text {
                objectName: "aiPlanBlock"
                visible: ai.hasKey && root.blockText !== ""
                width: parent.width
                wrapMode: Text.Wrap
                text: root.blockText
                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.danger
            }

            // ---- 上一步 / 跳过 / 下一步 ----
            Item {
                visible: ai.hasKey
                width: parent.width
                height: Theme.px(38)
                TpButton {
                    objectName: "aiPlanBack"
                    visible: root.step > 0
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("上一步")
                    onClicked: root.back()
                }
                Row {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.px(8)
                    TpButton { objectName: "aiPlanSkip"; text: qsTr("跳过"); onClicked: root.skip() }
                    TpButton {
                        objectName: "aiPlanNext"
                        kind: "primary"
                        text: root.step < root.steps.length - 1 ? qsTr("下一步") : qsTr("让 AI 规划")
                        onClicked: root.next()
                    }
                }
            }
            Text {
                visible: ai.hasKey
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("会发给 %1（%2）。生成后转到地图上的 AI 助手，新计划要你在预览里勾选、点“应用”才写入。").arg(ai.providerName).arg(ai.model || qsTr("没填模型"))
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
            }
        }
    }
}
