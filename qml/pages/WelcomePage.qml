// 欢迎页（0.12.7，S-16）：只在第一次打开、还没有数据文件夹时出现（有 Electron 数据时先走"要不要导入"，不到这里）。
// 照 Electron welcome.html：插画、标题、数据位置（可更改）、开始（含一个示例计划）、打开已有文件夹、中英切换。点"开始"之前不写文件。
import QtQuick
import QtQuick.Dialogs
import TPApp

Item {
    id: root
    property bool forced: false                 // 截图和自检
    readonly property bool shown: forced || app.startupChoice === "welcome"
    property string dir: app.defaultDataDir
    property string error: ""
    signal openExisting()                       // 打开已有文件夹：Main.qml 的选文件夹和后面的对话框
    signal started()

    anchors.fill: parent
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { enabled: Theme.motion; TpAnim { shown: root.shown } }

    function start() {
        error = ""
        const r = app.welcomeStart(dir)
        if (r.ok) root.started()
        else error = r.error || qsTr("出错了")
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#f7fafc" }
            GradientStop { position: 0.55; color: "#e8eef3" }
            GradientStop { position: 1; color: "#dde6ed" }
        }
    }
    // 盖住下面的页面：鼠标和滚轮都不漏下去
    MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }

    Segmented {
        objectName: "welcomeLang"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.px(16)
        options: [{ key: "zh", text: "中文" }, { key: "en", text: "English" }]
        current: app.lang
        onPicked: (k) => app.lang = k
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.px(32), Theme.px(520))
        spacing: Theme.px(4)

        EmptyArt {
            name: "welcome"
            anchors.horizontalCenter: parent.horizontalCenter
            // 窗口矮时插画让路，按钮不能被挤出去
            width: Math.min(implicitWidth, Math.max(Theme.px(72), root.height - Theme.px(400)))
            height: width
        }
        Text {
            objectName: "welcomeTitle"
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("旅行计划")
            font.family: serifFamily; font.pixelSize: Theme.px(32); font.bold: true; font.letterSpacing: Theme.px(2); color: Theme.text
        }
        Text {
            width: parent.width
            bottomPadding: Theme.px(20)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("把每一趟旅行画在地球上")
            font.family: serifFamily; font.pixelSize: Theme.px(15); color: Theme.muted
        }
        TpCard {
            width: parent.width
            height: where.implicitHeight + Theme.px(28)
            color: Qt.rgba(1, 1, 1, 0.72)
            radius: Theme.radiusSmall + Theme.px(4)
            Row {
                id: where
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.px(16)
                width: parent.width - Theme.px(32)
                spacing: Theme.px(12)
                TpIcon { name: "folder"; color: Theme.accent; width: Theme.px(24); height: Theme.px(24); anchors.verticalCenter: parent.verticalCenter }
                Column {
                    width: parent.width - Theme.px(36) - change.width - Theme.px(12)
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("你的计划和照片存在这里")
                        font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.muted
                    }
                    Text {
                        objectName: "welcomeDir"
                        width: parent.width
                        wrapMode: Text.WrapAnywhere
                        text: root.dir
                        font.family: "Consolas"; font.pixelSize: Theme.px(13); color: Theme.text
                    }
                }
                TpButton {
                    id: change
                    objectName: "welcomeChange"
                    compact: true
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("更改")
                    onClicked: { root.error = ""; pick.open() }
                }
            }
        }
        Item { width: 1; height: Theme.px(14) }
        TpButton {
            objectName: "welcomeStart"
            width: parent.width
            height: Theme.px(52)
            kind: "primary"
            fontSize: 17
            text: qsTr("开始")
            onClicked: root.start()
        }
        Item { width: 1; height: Theme.px(8) }
        TpButton {
            objectName: "welcomeOpen"
            anchors.horizontalCenter: parent.horizontalCenter
            kind: "link"
            compact: true
            fontSize: 13.5
            text: qsTr("已经有数据？打开数据文件夹")
            onClicked: { root.error = ""; root.openExisting() }
        }
        Text {
            objectName: "welcomeError"
            width: parent.width
            topPadding: Theme.px(8)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: root.error !== ""
            text: root.error
            font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.danger
        }
    }

    FolderDialog {
        id: pick
        title: qsTr("选一个文件夹放数据")
        onAccepted: root.dir = app.welcomeFolder(selectedFolder)
    }
}
