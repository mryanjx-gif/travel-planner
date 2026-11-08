// 计划页的"照片"标签（0.10.5，P-14；照 Electron pages-plan.js photosHtml）：张数和精选数、添加照片（当前站点优先）、封面区、按站分组。
// 每张：点开看大图；鼠标放上去出现 精选 / 设封面 / 写说明 / 移除（移除只是从计划里拿掉，不删文件）。写说明和移除的对话框在 Main.qml。
import QtQuick
import TPApp

Column {
    id: root
    property var rows: []                   // planPage.info.photos
    property string coverId: ""
    property string coverThumb: ""          // 真正设过封面才有（没有时不显示封面区）
    property bool readOnly: false
    signal addRequested()
    signal captionRequested(var photo)
    signal removeRequested(var photo)
    signal viewRequested(var list, int index)
    signal notice(string text)
    spacing: Theme.px(10)

    readonly property int favCount: rows.filter(p => p.fav).length
    readonly property int columns: Math.max(2, Math.floor(width / Theme.px(170)))
    readonly property var groups: {
        const out = []
        for (let i = 0; i < rows.length; ++i) {
            const last = out[out.length - 1]
            if (last && last.stopId === rows[i].stopId) last.rows.push({ p: rows[i], i: i })
            else out.push({ stopId: rows[i].stopId, stop: rows[i].stop, stopNo: rows[i].stopNo, route: rows[i].route, rows: [{ p: rows[i], i: i }] })
        }
        return out
    }

    Item {
        width: parent.width
        height: addBtn.height
        Text {
            objectName: "photoCount"
            anchors.verticalCenter: parent.verticalCenter
            text: Fmt.one(qsTr("%1 张").arg(root.rows.length)) + (root.favCount > 0 ? " · " + qsTr("%1 张精选").arg(root.favCount) : "")
            font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.muted
        }
        TpButton {
            id: addBtn
            objectName: "photoAdd"
            visible: !root.readOnly
            anchors.right: parent.right
            compact: true; kind: "primary"; text: qsTr("添加照片")
            onClicked: root.addRequested()
        }
    }

    // ---- 封面 ----
    Column {
        objectName: "photoCover"
        visible: root.coverThumb !== ""
        width: parent.width
        spacing: Theme.px(6)
        Item {
            width: parent.width
            height: Theme.px(26)
            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("封面"); font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: Theme.text }
            TpButton {
                objectName: "photoCoverClear"
                visible: !root.readOnly
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                compact: true; kind: "ghost"; text: qsTr("去掉封面")
                onClicked: photos.setCover(planPage.planId, "")
            }
        }
        Rectangle {
            width: Math.min(parent.width, Theme.px(320))
            height: width * 0.56
            radius: Theme.px(12)
            color: Theme.panel2
            clip: true
            Image { anchors.fill: parent; source: root.coverThumb; fillMode: Image.PreserveAspectCrop; asynchronous: true }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    const i = root.rows.findIndex(p => p.id === root.coverId)
                    root.viewRequested(i >= 0 ? root.rows : [{ thumb: root.coverThumb }], Math.max(i, 0))
                }
            }
        }
    }

    Text {
        objectName: "photoEmpty"
        visible: root.rows.length === 0
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("还没有照片。")
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.muted
    }

    // ---- 按站分组 ----
    Repeater {
        model: root.groups
        delegate: Column {
            id: group
            required property var modelData
            width: root.width
            spacing: Theme.px(6)
            Text {
                objectName: "photoGroupTitle"
                width: parent.width
                elide: Text.ElideRight
                text: (group.modelData.route ? qsTr("第 %1 站").arg(group.modelData.stopNo) + " · " : "") + group.modelData.stop + "  ·  " + Fmt.one(qsTr("%1 张").arg(group.modelData.rows.length))
                font.family: serifFamily; font.pixelSize: Theme.px(14); font.bold: true; color: Theme.text
            }
            Grid {
                width: parent.width
                columns: root.columns
                spacing: Theme.px(8)
                Repeater {
                    model: group.modelData.rows
                    delegate: Column {
                        id: tile
                        required property var modelData
                        readonly property var ph: modelData.p
                        objectName: "photoTile-" + modelData.i
                        width: (root.width - (root.columns - 1) * Theme.px(8)) / root.columns
                        spacing: Theme.px(3)
                        Rectangle {
                            width: parent.width
                            height: width * 0.75
                            radius: Theme.px(10)
                            color: Theme.panel2
                            clip: true
                            border.width: tile.ph.id === root.coverId ? 2 : 0
                            border.color: Theme.accent
                            Image { anchors.fill: parent; source: tile.ph.thumb || ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 320 }
                            MouseArea {
                                objectName: "photoOpen"
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                Accessible.role: Accessible.Button
                                Accessible.name: tile.ph.caption || tile.ph.stop
                                onClicked: root.viewRequested(root.rows, tile.modelData.i)
                            }
                            HoverHandler { id: tileHover }
                            // 精选的星一直显示；其余鼠标放上去才出现
                            Row {
                                visible: !root.readOnly
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: Theme.px(5)
                                spacing: Theme.px(4)
                                TileAct {
                                    objectName: "photoFav"; icon: "star"; solid: tile.ph.fav === true
                                    shown: tileHover.hovered || tile.ph.fav === true
                                    label: tile.ph.fav ? qsTr("取消精选") : qsTr("设为精选")
                                    onClicked: photos.setFav(tile.ph.id, !tile.ph.fav)
                                }
                                TileAct {
                                    objectName: "photoSetCover"; icon: "image"; shown: tileHover.hovered
                                    label: qsTr("设为封面")
                                    onClicked: { if (photos.setCover(planPage.planId, tile.ph.id)) root.notice(qsTr("已设为封面")) }
                                }
                                TileAct {
                                    objectName: "photoCaption"; icon: "edit"; shown: tileHover.hovered
                                    label: qsTr("写说明")
                                    onClicked: root.captionRequested(tile.ph)
                                }
                                TileAct {
                                    objectName: "photoRemove"; icon: "trash"; shown: tileHover.hovered
                                    label: qsTr("从计划里移除")
                                    onClicked: root.removeRequested(tile.ph)
                                }
                            }
                        }
                        Text {
                            visible: !!tile.ph.caption
                            width: parent.width
                            elide: Text.ElideRight
                            text: tile.ph.caption || ""
                            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.textSoft
                        }
                    }
                }
            }
        }
    }

    component TileAct: Rectangle {
        id: act
        property string icon: ""
        property string label: ""
        property bool shown: false
        property bool solid: false
        signal clicked()
        width: Theme.px(26); height: width
        radius: width / 2
        color: actMa.containsMouse ? Qt.rgba(0, 0, 0, 0.72) : Qt.rgba(0, 0, 0, 0.5)
        opacity: shown ? 1 : 0
        Accessible.role: Accessible.Button
        Accessible.name: label
        TpIcon { anchors.centerIn: parent; width: Theme.px(15); height: width; name: act.solid ? "starOn" : act.icon; color: act.solid ? "#ffd166" : "#ffffff" }
        TpTip { text: act.label; show: actMa.containsMouse }
        MouseArea { id: actMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: act.clicked() }
    }
}
