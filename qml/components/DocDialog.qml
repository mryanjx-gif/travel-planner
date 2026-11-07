// 加 / 改一个证件（0.12.4，T-08；照 Electron travelkit.js editDoc）。row 是 planPage.documents() 的一行，null = 新加。
// 关联计划只列没完成的；原来关联的已完成或已删时也留在选项里，改别的字段不会把关联丢掉。
import QtQuick
import TPApp

ModalLayer {
    id: root
    property int index: -1
    property string docId: ""
    property string kind: "passport"
    property string owner: ""
    property string label: ""
    property string expires: ""
    property string planId: ""
    property string notes: ""
    property var planOptions: []
    property string err: ""
    property bool confirmDel: false
    readonly property bool isNew: index < 0
    cardWidth: 520
    onDismissed: open = false

    function show(row) {
        root.forceActiveFocus()   // 上次的输入框可能还留着焦点，那样新内容写不进去（TpField）
        const d = row || {}
        index = row ? d.index : -1
        docId = d.id || ""
        kind = row ? (d.kind || "") : "passport"
        owner = d.owner || ""
        label = d.label || ""
        expires = d.expires || ""
        planId = d.planId || ""
        notes = d.notes || ""
        err = ""
        confirmDel = false
        planOptions = [{ key: "", text: qsTr("（不关联）") }].concat(planPage.documentPlanOptions(planId).map(o => ({ key: o.key, text: o.text || qsTr("（已删除的计划）") })))
        open = true
    }
    function errText(e) { return e === "readOnly" ? qsTr("数据现在只能看、不能改") : qsTr("没有保存：这个证件可能已经被改过了") }
    function save() {
        dateField.commit()   // 日期框还有焦点时，打的字还没交出来
        const e = planPage.saveDocument(index, docId, { kind: kind, owner: owner, label: label, expires: expires, planId: planId, notes: notes })
        if (e) { err = errText(e); return }
        open = false
    }
    function remove() {
        if (!confirmDel) { confirmDel = true; return }
        const e = planPage.deleteDocument(index, docId)
        if (e) { err = errText(e); return }
        open = false
    }

    Text {
        width: parent.width
        text: root.isNew ? qsTr("添加证件") : qsTr("修改证件")
        font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
    }
    Row {
        width: parent.width
        spacing: Theme.px(10)
        Labeled {
            width: (parent.width - parent.spacing) / 2
            text: qsTr("类型")
            TpSelect {
                objectName: "docKind"
                width: parent.width
                placeholder: qsTr("其他证件")   // Electron 把认不出的类型当"其他"显示；不选就不改
                options: [{ key: "passport", text: qsTr("护照") }, { key: "visa", text: qsTr("签证") }, { key: "other", text: qsTr("其他证件") }]
                current: root.kind
                onPicked: (k) => root.kind = k
            }
        }
        Labeled {
            width: (parent.width - parent.spacing) / 2
            text: qsTr("谁的（可不填）")
            TpField {
                objectName: "docOwner"
                width: parent.width
                placeholder: qsTr("如 我、妈妈")
                value: root.owner
                onEdited: (t) => root.owner = t
                onAccepted: root.save()
                onEscaped: root.open = false
            }
        }
    }
    Row {
        width: parent.width
        spacing: Theme.px(10)
        Labeled {
            width: (parent.width - parent.spacing) / 2
            text: qsTr("名称（可不填）")
            TpField {
                objectName: "docLabel"
                width: parent.width
                placeholder: qsTr("如 中国护照、日本单次签证")
                value: root.label
                onEdited: (t) => root.label = t
                onAccepted: root.save()
                onEscaped: root.open = false
            }
        }
        Labeled {
            width: (parent.width - parent.spacing) / 2
            text: qsTr("到期日期")
            DateField {
                id: dateField
                objectName: "docExpires"
                width: parent.width
                value: root.expires
                onEdited: (v) => root.expires = v
            }
        }
    }
    Labeled {
        width: parent.width
        text: qsTr("关联的计划（签证用）")
        TpSelect {
            objectName: "docPlan"
            width: parent.width
            options: root.planOptions
            current: root.planId
            onPicked: (k) => root.planId = k
        }
    }
    Labeled {
        width: parent.width
        text: qsTr("备注（不要写证件号码）")
        TpField {
            objectName: "docNotes"
            width: parent.width
            multiline: true
            maxLines: 4
            value: root.notes
            onEdited: (t) => root.notes = t
            onEscaped: root.open = false
        }
    }
    Text {
        visible: root.err !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: root.err
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
    }
    Item {
        width: parent.width
        height: Theme.px(36)
        TpButton {
            objectName: "docDelete"
            visible: !root.isNew
            kind: "dangerOutline"
            text: root.confirmDel ? qsTr("再点一次确认删除") : qsTr("删除这个证件")
            onClicked: root.remove()
        }
        Row {
            anchors.right: parent.right
            spacing: Theme.px(8)
            TpButton { text: qsTr("取消"); onClicked: root.open = false }
            TpButton { objectName: "docOk"; kind: "primary"; text: qsTr("保存"); onClicked: root.save() }
        }
    }

    component Labeled: Column {
        property string text: ""
        default property alias body: holder.data
        spacing: Theme.px(3)
        Text { width: parent.width; elide: Text.ElideRight; text: parent.text; font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft }
        Item { id: holder; width: parent.width; height: childrenRect.height }
    }
}
