// 计划页的"行李与待办"标签（0.10.4，P-09、P-10）。PO 选 1A 左右两列（窄窗口一列）、2A 紧急信息卡放在底下、改的时候弹出表单；"导出信息卡"是占位，阶段 ⑧ 接功能。
// kit = planPage.info.kit（plans::kitInfo）。
import QtQuick
import TPApp

Column {
    id: root
    property var kit: ({})
    property bool readOnly: false
    signal notice(string text)
    signal saveTemplateRequested()
    signal editLocal()
    signal editEmergency()
    spacing: Theme.px(12)

    readonly property bool wide: width >= Theme.px(600)
    readonly property real cardW: wide ? (width - Theme.px(12)) / 2 : width
    readonly property var em: kit.emergency || ({})
    readonly property var contacts: em.contacts || []
    readonly property var local: kit.local || []
    readonly property bool hasGlobal: !!(em.insurer || em.policyNo || em.insurerPhone || em.notes || contacts.length > 0)

    function dueNotice() {
        const bits = []
        if ((kit.dueOver || 0) > 0) bits.push(qsTr("%1 条待办已过期").arg(kit.dueOver))
        if ((kit.dueSoon || 0) > 0) bits.push(qsTr("%1 条待办 3 天内到期").arg(kit.dueSoon))
        return bits.join(Fmt.en ? ", " : "，")
    }
    function localLabel(l) {
        return l === "报警" ? qsTr("报警") : l === "急救" ? qsTr("急救") : l === "消防" ? qsTr("消防")
             : l === "使领馆 / 领事保护" ? qsTr("使领馆 / 领事保护") : l === "其他" ? qsTr("其他") : l
    }
    function paste(t) { todoCard.draft = t }
    function addTemplate() {
        const n = planPage.addFromTemplate()
        if (n >= 0) root.notice(n > 0 ? qsTr("加了 %1 项").arg(n) : qsTr("模板里的物品都已经在清单里了"))
    }

    Flow {
        width: parent.width
        spacing: Theme.px(12)
        CheckCard {
            id: packCard
            listKey: "packing"
            hasTools: !root.readOnly
            width: root.cardW
            title: qsTr("行李清单")
            placeholder: qsTr("添加一件物品，回车")
            rows: root.kit.packing || []
            doneCount: root.kit.packDone || 0
            readOnly: root.readOnly
            Row {
                visible: !root.readOnly
                spacing: Theme.px(14)
                LinkText { objectName: "kitTplAdd"; small: true; text: qsTr("从模板添加"); onClicked: root.addTemplate() }
                LinkText {
                    objectName: "kitTplSave"
                    small: true
                    text: qsTr("存成模板")
                    onClicked: (root.kit.packing || []).length > 0 ? root.saveTemplateRequested() : root.notice(qsTr("清单是空的"))
                }
            }
        }
        CheckCard {
            id: todoCard
            listKey: "todos"
            hasTools: (root.kit.dueOver || 0) + (root.kit.dueSoon || 0) > 0
            width: root.cardW
            title: qsTr("待办")
            placeholder: qsTr("比如：办签证、换汇、订租车，回车")
            rows: root.kit.todos || []
            doneCount: root.kit.todoDone || 0
            readOnly: root.readOnly
            Rectangle {   // M2
                objectName: "kitDueNotice"
                visible: (root.kit.dueOver || 0) + (root.kit.dueSoon || 0) > 0
                width: dueNoticeText.implicitWidth + Theme.px(14)
                height: dueNoticeText.implicitHeight + Theme.px(4)
                radius: Theme.px(7)
                color: "#fbf1dc"
                Text {
                    id: dueNoticeText
                    anchors.centerIn: parent
                    text: root.dueNotice()
                    font.family: serifFamily; font.pixelSize: Theme.px(12); color: "#7a5a1c"
                }
            }
        }
    }
    Text {
        visible: !root.readOnly
        width: parent.width
        wrapMode: Text.Wrap
        text: ((root.kit.tplCount || 0) > 0 ? qsTr("模板里有 %1 项。").arg(root.kit.tplCount) : qsTr("模板还没存过，“从模板添加”会用一份默认清单。")) + qsTr("打勾、增删都会自动保存。")
        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
    }

    // ---- 紧急信息 ----
    TpCard {
        objectName: "kitEmergency"
        width: parent.width
        height: emCol.implicitHeight + Theme.px(24)
        radius: Theme.px(16)
        color: Theme.card
        Column {
            id: emCol
            x: Theme.px(14); y: Theme.px(12)
            width: parent.width - Theme.px(28)
            spacing: Theme.px(10)
            Item {
                width: parent.width
                height: emTitle.implicitHeight
                Text { id: emTitle; text: qsTr("紧急信息"); font.family: serifFamily; font.pixelSize: Theme.px(15); font.bold: true; color: Theme.text }
                LinkText { objectName: "kitExportCard"; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; small: true; text: qsTr("导出信息卡"); onClicked: win.notReady() }
            }
            Flow {
                width: parent.width
                spacing: Theme.px(16)
                readonly property real colW: root.wide ? (width - spacing) / 2 : width
                Column {
                    width: parent.colW
                    spacing: Theme.px(5)
                    Item {
                        width: parent.width
                        height: Math.max(localTitle.implicitHeight, localEdit.implicitHeight)
                        Text { id: localTitle; text: qsTr("当地紧急电话"); font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.textSoft }
                        LinkText { id: localEdit; objectName: "kitEditLocal"; visible: !root.readOnly; anchors.right: parent.right; small: true; text: qsTr("改当地电话"); onClicked: root.editLocal() }
                    }
                    Repeater {
                        model: root.local
                        delegate: PhoneRow { required property var modelData; label: root.localLabel(modelData.label); phone: modelData.phone }
                    }
                    Text {
                        visible: root.local.length === 0
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("还没填。出发前查一下当地的报警、急救电话和中国使领馆电话（外交部“中国领事”、当地政府网站）。")
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                    }
                }
                Column {
                    width: parent.colW
                    spacing: Theme.px(5)
                    Item {
                        width: parent.width
                        height: Math.max(globalTitle.implicitHeight, globalEdit.implicitHeight)
                        Text { id: globalTitle; text: qsTr("保险和联系人"); font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.textSoft }
                        LinkText { id: globalEdit; objectName: "kitEditGlobal"; visible: !root.readOnly; anchors.right: parent.right; small: true; text: qsTr("改保险和联系人"); onClicked: root.editEmergency() }
                    }
                    PhoneRow { visible: !!(root.em.insurer || root.em.policyNo); label: root.em.insurer || qsTr("保险"); phone: root.em.policyNo || "" }
                    PhoneRow { visible: !!root.em.insurerPhone; label: qsTr("救援电话"); phone: root.em.insurerPhone || "" }
                    Repeater {
                        model: root.contacts
                        delegate: PhoneRow {
                            required property var modelData
                            label: modelData.name + (modelData.relation ? (Fmt.en ? " (" + modelData.relation + ")" : "（" + modelData.relation + "）") : "")
                            phone: modelData.phone
                        }
                    }
                    Text {
                        visible: !!root.em.notes
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.em.notes || ""
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
                    }
                    Text {
                        visible: !root.hasGlobal
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("填一次，所有计划共用。")
                        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
                    }
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("这些只存在你自己的电脑上。")
                font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
            }
        }
    }

    component PhoneRow: Item {
        property string label: ""
        property string phone: ""
        width: parent ? parent.width : 0
        height: Math.max(labelText.contentHeight, phoneText.contentHeight)
        Text {
            id: labelText
            width: parent.phone !== "" ? parent.width * 0.5 : parent.width
            wrapMode: Text.Wrap
            text: parent.label
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
        }
        Text {
            id: phoneText
            anchors.right: parent.right
            width: parent.width * 0.5 - Theme.px(6)
            horizontalAlignment: Text.AlignRight
            wrapMode: Text.Wrap
            text: parent.phone
            font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text
        }
    }
}
