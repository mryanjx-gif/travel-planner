// 把未归类的照片归到计划（0.11.1，A-03；Electron pages-album.js assign 是两步，这里放在一个框里）。文件不移动。
import QtQuick
import TPApp

ModalLayer {
    id: root
    property var ids: []
    property string planId: ""
    property string stopId: ""
    readonly property var planChoices: open ? photos.planOptions().map(p => ({ key: p.id, text: p.name })) : []
    readonly property var stopChoices: open && planId ? photos.stopOptions(planId).map(s => ({ key: s.id, text: s.name })) : []
    signal assigned(int count, string planName, string stopName)
    cardWidth: 440
    onDismissed: open = false

    function ask(list) {
        ids = list
        const opts = photos.planOptions()
        if (opts.length === 0) return false
        planId = opts[0].id
        const st = photos.stopOptions(planId)
        stopId = st.length ? st[0].id : ""
        open = true
        return true
    }
    function choose(p) {
        planId = p
        const st = photos.stopOptions(p)
        stopId = st.length ? st[0].id : ""
    }
    function save() {
        const pn = planChoices.find(x => x.key === planId), sn = stopChoices.find(x => x.key === stopId)
        const n = ids.length
        open = false
        if (photos.assign(ids, planId, stopId)) root.assigned(n, pn ? pn.text : "", sn ? sn.text.replace(/^\d+\.\s*/, "") : "")
    }

    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("把 %1 张照片归到").arg(root.ids.length)
        font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
    }
    Text { text: qsTr("计划"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
    TpSelect {
        objectName: "assignPlan"
        width: parent.width
        current: root.planId
        options: root.planChoices
        onPicked: (k) => root.choose(k)
    }
    Text { text: qsTr("放到哪一站"); font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted }
    TpSelect {
        objectName: "assignStop"
        width: parent.width
        current: root.stopId
        options: root.stopChoices
        onPicked: (k) => root.stopId = k
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("照片文件留在原位置，不用挪。")
        font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.textSoft
    }
    Item {
        width: parent.width
        height: Theme.px(40)
        Row {
            anchors.right: parent.right
            spacing: Theme.px(10)
            TpButton { text: qsTr("取消"); onClicked: root.open = false }
            TpButton { objectName: "assignSave"; kind: "primary"; text: qsTr("归类"); onClicked: root.save() }
        }
    }
}
