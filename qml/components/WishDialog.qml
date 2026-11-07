// 想去的两个小对话框（0.11.0，H-02；Electron pages-wishlist.js start、ref）：
// showStart(id) 开始计划：定日期（可以不填），状态改成"计划中"；showSeason(id) 关联季节表里的一行。
import QtQuick
import TPApp

ModalLayer {
    id: root
    property string kind: "start"
    property string planId: ""
    property string planName: ""
    property string start: ""
    property string end: ""
    property string ref: ""
    property var seasonOptions: []
    readonly property bool badRange: start !== "" && end !== "" && end < start
    signal started(string name)
    cardWidth: 440
    onDismissed: open = false

    function load(id) {
        const b = plans.planBasics(id)
        planId = id
        planName = b.name || qsTr("未命名计划")
        return b
    }
    function showStart(id) {
        const b = load(id)
        start = b.startDate || ""
        end = b.endDate || ""
        kind = "start"
        open = true
    }
    function showSeason(id) {
        const b = load(id)
        ref = b.seasonRef || ""
        seasonOptions = [{ key: "", text: qsTr("（不关联）") }].concat(plans.seasonRows().map(r => ({
            key: r.key, text: Fmt.en && r.nameEn ? r.nameEn : (r.group ? r.group + " · " : "") + r.name })))
        kind = "season"
        open = true
    }
    function save() {
        if (kind === "start") {
            if (badRange) return
            if (plans.startPlan(planId, start, end)) { open = false; root.started(planName) }
            else win.showError(qsTr("没有改成：数据现在只能看、不能改，或者这个计划已经不在了。"))
        } else {
            if (plans.setSeasonRef(planId, ref)) open = false
            else win.showError(qsTr("没有改成：数据现在只能看、不能改，或者这个计划已经不在了。"))
        }
    }

    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: root.kind === "start" ? qsTr("开始计划「%1」").arg(root.planName) : qsTr("关联季节表")
        font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        visible: root.kind === "season"
        text: qsTr("「%1」对应季节表里的").arg(root.planName)
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.textSoft
    }
    Row {
        visible: root.kind === "start"
        spacing: Theme.px(12)
        Column {
            spacing: Theme.px(4)
            Text { text: qsTr("出发"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
            DateField { objectName: "wishStart"; value: root.start; onEdited: (v) => root.start = v }
        }
        Column {
            spacing: Theme.px(4)
            Text { text: qsTr("回来"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
            DateField { objectName: "wishEnd"; value: root.end; minDate: root.start; bad: root.badRange; onEdited: (v) => root.end = v }
        }
    }
    TpSelect {
        objectName: "wishSeason"
        visible: root.kind === "season"
        width: parent.width
        current: root.ref
        options: root.seasonOptions
        onPicked: (k) => root.ref = k
    }
    Item {
        width: parent.width
        height: Theme.px(40)
        Row {
            anchors.right: parent.right
            spacing: Theme.px(10)
            TpButton { text: qsTr("取消"); onClicked: root.open = false }
            TpButton {
                objectName: "wishSave"
                kind: "primary"
                enabled: !root.badRange
                text: root.kind === "start" ? qsTr("改成“计划中”") : qsTr("保存")
                onClicked: root.save()
            }
        }
    }
}
