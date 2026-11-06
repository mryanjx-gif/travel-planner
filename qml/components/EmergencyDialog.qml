// 改紧急信息（0.10.4，P-10；照 Electron travelkit.js editLocal、editEmergency）。mode = local：这个计划的当地电话；mode = global：保险和联系人，所有计划共用。
import QtQuick
import TPApp

ModalLayer {
    id: root
    property string mode: "local"
    property var v: ({})
    property string err: ""
    cardWidth: 520
    onDismissed: open = false

    readonly property var localKeys: ["报警", "急救", "消防", "使领馆 / 领事保护", "其他"]
    function localName(k) {
        return k === "报警" ? qsTr("报警") : k === "急救" ? qsTr("急救") : k === "消防" ? qsTr("消防") : k === "使领馆 / 领事保护" ? qsTr("使领馆 / 领事保护") : qsTr("其他")
    }
    function set(k, t) { v[k] = t; vChanged() }
    function showLocal(kit) {
        root.forceActiveFocus()
        const m = {}
        for (const x of kit.local || []) if (localKeys.indexOf(x.label) >= 0) m[x.label] = x.phone
        v = m; mode = "local"; err = ""; open = true
    }
    function showGlobal(kit) {
        root.forceActiveFocus()
        const e = kit.emergency || {}
        const m = { insurer: e.insurer || "", policyNo: e.policyNo || "", insurerPhone: e.insurerPhone || "", notes: e.notes || "" }
        const c = e.contacts || []
        for (let i = 0; i < 3; ++i) { const x = c[i] || {}; m["n" + i] = x.name || ""; m["r" + i] = x.relation || ""; m["p" + i] = x.phone || "" }
        v = m; mode = "global"; err = ""; open = true
    }
    function save() {
        const e = mode === "local" ? planPage.saveLocalEmergency(v) : planPage.saveEmergency(v)
        if (e) { err = qsTr("数据现在只能看、不能改"); return }
        open = false
    }

    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: root.mode === "local" ? qsTr("当地紧急电话") : qsTr("紧急信息")
        font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: root.mode === "local" ? qsTr("只对这个计划有效。") : qsTr("所有计划共用。")
        font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
    }
    Flickable {
        width: parent.width
        height: Math.min(form.implicitHeight, root.height - Theme.px(210))
        contentHeight: form.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: form
            width: parent.width
            spacing: Theme.px(10)
            // ---- 当地电话 ----
            Repeater {
                model: root.mode === "local" ? root.localKeys : []
                delegate: Column {
                    required property var modelData
                    width: form.width
                    spacing: Theme.px(3)
                    Text { text: root.localName(parent.modelData); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft }
                    TpField {
                        objectName: "emLocal-" + parent.modelData
                        width: parent.width
                        placeholder: parent.modelData === "使领馆 / 领事保护" ? qsTr("号码和名称，如 +81 3 xxxx（驻日使馆）") : ""
                        value: root.v[parent.modelData] || ""
                        onEdited: (t) => root.set(parent.modelData, t)
                        onAccepted: root.save()
                        onEscaped: root.open = false
                    }
                }
            }
            // ---- 保险和联系人 ----
            Row {
                visible: root.mode === "global"
                width: parent.width
                spacing: Theme.px(10)
                Labeled { width: (parent.width - parent.spacing) / 2; label: qsTr("保险公司"); key: "insurer" }
                Labeled { width: (parent.width - parent.spacing) / 2; label: qsTr("保单号"); key: "policyNo" }
            }
            Labeled { visible: root.mode === "global"; width: parent.width; label: qsTr("保险公司 24 小时救援电话"); key: "insurerPhone" }
            Repeater {
                model: root.mode === "global" ? 3 : 0
                delegate: Column {
                    required property int index
                    width: form.width
                    spacing: Theme.px(6)
                    Row {
                        width: parent.width
                        spacing: Theme.px(10)
                        Labeled { width: (parent.width - parent.spacing) / 2; label: qsTr("紧急联系人 %1").arg(index + 1); key: "n" + index }
                        Labeled { width: (parent.width - parent.spacing) / 2; label: qsTr("关系"); key: "r" + index }
                    }
                    Labeled { width: parent.width; label: qsTr("电话（带国家代码，如 +86 …）"); key: "p" + index }
                }
            }
            Column {
                visible: root.mode === "global"
                width: parent.width
                spacing: Theme.px(3)
                Text { width: parent.width; wrapMode: Text.Wrap; text: qsTr("其他（血型、过敏、常用药等，自己决定写不写）"); font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft }
                TpField {
                    width: parent.width
                    multiline: true
                    maxLines: 4
                    value: root.v.notes || ""
                    onEdited: (t) => root.set("notes", t)
                    onEscaped: root.open = false
                }
            }
        }
    }
    Text {
        visible: root.err !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: root.err
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
    }
    Row {
        anchors.right: parent.right
        spacing: Theme.px(8)
        TpButton { text: qsTr("取消"); onClicked: root.open = false }
        TpButton { objectName: "emOk"; kind: "primary"; text: qsTr("保存"); onClicked: root.save() }
    }

    component Labeled: Column {
        property string label: ""
        property string key: ""
        spacing: Theme.px(3)
        Text { width: parent.width; elide: Text.ElideRight; text: parent.label; font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft }
        TpField {
            objectName: "em-" + parent.key
            width: parent.width
            value: root.v[parent.key] || ""
            onEdited: (t) => root.set(parent.key, t)
            onAccepted: root.save()
            onEscaped: root.open = false
        }
    }
}
