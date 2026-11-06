// 清理没用的照片（0.7.0，A-10）：照片文件夹里没被照片库记录的文件 → 回收站。
// 三道保护在 C++ 里（photolib.h）：最近 10 分钟加进来的先不动、移动前再核对数据文件、撤销记录里还用得到的不动。
// 旋转前的原图（.originals）里原照片已经不在的，单独一项，勾了才清理。
// 用法：show(photos.scanCleanup() 的结果)
import QtQuick
import TPApp

ModalLayer {
    id: root
    property var scan: ({})
    property bool withOrphans: false
    signal toastRequested(string text, bool error)
    cardWidth: 520
    dismissible: !photos.trashRunning
    onDismissed: open = false

    function show(r) { scan = r; withOrphans = false; open = true }
    function sizeText(b) { return b >= 1048576 ? (b / 1048576).toFixed(1) + " MB" : Math.max(1, Math.round(b / 1024)) + " KB" }
    function names(list) {
        const n = []
        for (let i = 0; i < Math.min(list.length, 6); ++i) n.push(list[i].rel.replace(/^photos\//, ""))
        return n.join("、") + (list.length > 6 ? qsTr(" 等") : "")
    }
    readonly property var unused: scan.unused || []
    readonly property var orphans: scan.orphans || []
    readonly property int total: unused.length + (withOrphans ? orphans.length : 0)

    Text {
        width: parent.width
        text: photos.trashRunning ? qsTr("正在移到回收站…") : qsTr("清理没用的照片")
        font.family: serifFamily; font.pixelSize: Theme.px(19); font.bold: true; color: Theme.text
    }
    Text {
        visible: photos.trashRunning
        width: parent.width
        text: qsTr("%1 / %2").arg(photos.trashDone).arg(photos.trashTotal)
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.textSoft
    }
    Text {
        visible: !photos.trashRunning
        width: parent.width
        wrapMode: Text.Wrap
        lineHeight: 1.3
        textFormat: Text.StyledText
        text: root.unused.length > 0
              ? qsTr("照片文件夹里有 <b>%1 个文件</b>没被任何照片用到（共 %2）：").arg(root.unused.length).arg(root.sizeText(root.scan.unusedBytes || 0))
                + root.names(root.unused)
              : qsTr("照片文件夹里没有多余的文件。")
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.textSoft
    }
    TpCheck {
        visible: !photos.trashRunning && root.orphans.length > 0
        width: parent.width
        checked: root.withOrphans
        text: qsTr("另外 %1 张旋转前的原图，原照片已经不在了（共 %2），也一起清理").arg(root.orphans.length).arg(root.sizeText(root.scan.orphanBytes || 0))
        onToggled: (c) => root.withOrphans = c
    }
    Text {
        visible: !photos.trashRunning && ((root.scan.keptRecent || 0) > 0 || (root.scan.keptUndo || 0) > 0)
        width: parent.width
        wrapMode: Text.Wrap
        lineHeight: 1.3
        text: {
            const p = []
            if (root.scan.keptRecent > 0) p.push(qsTr("最近 10 分钟加进来的 %1 个").arg(root.scan.keptRecent))
            if (root.scan.keptUndo > 0) p.push(qsTr("撤销记录里还用得到的 %1 个").arg(root.scan.keptUndo))
            return p.join(qsTr("、")) + qsTr("这次先不动。")
        }
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
    }
    Text {
        visible: !photos.trashRunning && root.total > 0
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("移到回收站以后，需要时还可以从回收站找回来。")
        font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
    }
    Item {
        width: parent.width
        height: Theme.px(46)
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: Theme.px(10)
            TpButton {
                visible: !photos.trashRunning
                text: root.total > 0 ? qsTr("取消") : qsTr("知道了")
                kind: root.total > 0 ? "secondary" : "primary"
                onClicked: root.open = false
            }
            TpButton {
                visible: !photos.trashRunning && root.total > 0
                text: qsTr("移到回收站（%1 个）").arg(root.total)
                kind: "dangerOutline"
                onClicked: {
                    const rels = []
                    for (let i = 0; i < root.unused.length; ++i) rels.push(root.unused[i].rel)
                    if (root.withOrphans) for (let j = 0; j < root.orphans.length; ++j) rels.push(root.orphans[j].rel)
                    const r = photos.trashFiles(rels)
                    if (!r.ok) { root.toastRequested(r.error, true); root.open = false }
                }
            }
        }
    }
    Connections {
        target: photos
        function onTrashFinished(r) {
            root.open = false
            if (r.failed.length === 0) root.toastRequested(qsTr("已移到回收站：%1 个文件").arg(r.moved), false)
            else root.toastRequested(qsTr("移到回收站 %1 个，%2 个没移动（详情在日志里）").arg(r.moved).arg(r.failed.length), true)
        }
    }
}
