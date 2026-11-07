// 同行人的内容（0.12.8 从 PeopleDialog 拆出来）：列出人，加、改名、删。第一次加别人时自动带上"我"；加人时已有的账自动变成平摊（plans::addPerson），写明这次会变几笔。
// 付过钱或被单独写进某一笔的人不能删，点删除时说清是哪几笔。
// 两处用：计划页的同行人窗口（PeopleDialog）、单笔窗口里翻到这一页（ExpenseDialog，弹窗不叠弹窗）。
import QtQuick
import TPApp

Column {
    id: root
    property var money: ({})
    property string addName: ""
    property string note: ""
    property string doneText: qsTr("完成")
    property real maxListHeight: Theme.px(260)
    readonly property var people: money.people || []
    readonly property int shareCount: money.shareCount || 0
    readonly property int bookingCount: money.bookingCount || 0
    signal closed()
    width: parent ? parent.width : 0
    spacing: Theme.px(14)

    function show() {
        addName = ""
        note = ""
        addField.clear()
        addField.focusAll()
    }
    function close() {
        root.forceActiveFocus()   // 正在改的名字先存（失去焦点时存）
        root.closed()
    }
    function add() {
        const e = planPage.addPerson(addName, app.greetName || qsTr("我"))
        if (e === "emptyName") return
        note = e === "dupName" ? qsTr("已经有“%1”了").arg(addName.trim()) : ""
        if (e === "") { addName = ""; addField.clear() }
        addField.focusAll()
    }
    function usesText(uses) {
        const names = uses.slice(0, 5).map(u => u.title || Fmt.catLabel(u.cat)).join(Fmt.en ? ", " : "、")
        return uses.length > 5 ? qsTr("%1 等 %2 笔").arg(names).arg(uses.length) : names
    }
    function remove(p) {
        note = planPage.editPerson(p.id, "", true) === "used"
               ? qsTr("%1 付过钱或被单独写进了这几笔，先改了才能删：%2").arg(p.name).arg(usesText(p.uses)) : ""
    }
    // 这次会变成几人平摊多少笔（再加一个人的话）
    function hintText() {
        const n = Math.max(people.length, 1) + 1
        if (shareCount > 0 && bookingCount > 0) return qsTr("再加一个人，已有的 %1 笔和交通住宿的 %2 笔，会变成 %3 人平摊。").arg(shareCount).arg(bookingCount).arg(n)
        if (shareCount > 0) return qsTr("再加一个人，已有的 %1 笔会变成 %2 人平摊。").arg(shareCount).arg(n)
        if (bookingCount > 0) return qsTr("再加一个人，交通住宿的 %1 笔会变成 %2 人平摊。").arg(bookingCount).arg(n)
        return ""
    }

    Text { text: qsTr("同行人"); font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text }
    Flickable {
        visible: root.people.length > 0
        width: parent.width
        height: Math.min(listCol.implicitHeight, Math.max(Theme.px(110), root.maxListHeight))
        contentHeight: listCol.implicitHeight
        interactive: contentHeight > height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: listCol
            width: parent.width
            spacing: Theme.px(6)
            Repeater {
                model: root.people
                delegate: Item {
                    id: pRow
                    required property var modelData
                    property string typed: modelData.name
                    function commit() { if (typed.trim() !== "" && typed !== modelData.name) planPage.editPerson(modelData.id, typed, false) }
                    width: listCol.width
                    height: Theme.px(32)
                    TpField {
                        objectName: "peopleName-" + pRow.modelData.name
                        width: parent.width - delBtn.width - Theme.px(6)
                        placeholder: qsTr("名字")
                        value: pRow.modelData.name
                        onEdited: (t) => { pRow.typed = t }
                        onEditingChanged: if (!editing) pRow.commit()
                        onAccepted: addField.focusAll()
                        onEscaped: root.close()
                    }
                    Item {
                        id: delBtn
                        objectName: "peopleDel-" + pRow.modelData.name
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.px(26); height: width
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("删除")
                        Rectangle { anchors.fill: parent; radius: width / 2; color: delMa.containsMouse ? Theme.panel2 : "transparent" }
                        TpIcon { anchors.centerIn: parent; width: Theme.px(15); height: width; name: "trash"; color: Theme.muted }
                        TpTip { text: qsTr("删除"); show: delMa.containsMouse }
                        MouseArea { id: delMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.remove(pRow.modelData) }
                    }
                }
            }
        }
    }
    Row {
        width: parent.width
        spacing: Theme.px(8)
        TpField {
            id: addField
            objectName: "peopleAddName"
            width: parent.width - addBtn.width - parent.spacing
            placeholder: qsTr("添加同行人的名字，回车")
            value: root.addName
            onEdited: (t) => { root.addName = t; root.note = "" }
            onAccepted: root.add()
            onEscaped: root.close()
        }
        TpButton { id: addBtn; objectName: "peopleAdd"; compact: true; anchors.verticalCenter: parent.verticalCenter; text: qsTr("添加"); onClicked: root.add() }
    }
    Text {
        objectName: "peopleHint"
        visible: text !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: [root.hintText(), root.hintText() !== "" ? qsTr("单独改过分摊的不变。") : "",
               root.people.length === 0 ? qsTr("第一次加别人时会自动带上“我”。") : ""].filter(x => x).join(Fmt.en ? " " : "\n")   // 中文一句一行：连着排时结尾会断出单字
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.textSoft
    }
    Text {
        objectName: "peopleNote"
        visible: root.note !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: root.note
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.danger
    }
    Row {
        anchors.right: parent.right
        TpButton { objectName: "peopleDone"; kind: "primary"; text: root.doneText; onClicked: root.close() }
    }
}
