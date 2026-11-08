// 没钥匙时的引导卡（0.13.0 在 AI 面板里；0.13.2 抽出来，向导首页也用）：各家申请页（外部浏览器打开）+ 去设置填。
// prefix：objectName 前缀（面板 aiGuide、向导 aiPlanGuide），自检按它找
import QtQuick
import TPApp

Column {
    id: root
    property string prefix: "aiGuide"
    signal settingsRequested()
    spacing: Theme.px(10)
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("用 AI 助手要先填一家的 API 钥匙。钥匙在服务商的开发者平台申请，按用量付费，和网页版的会员订阅分开。")
        font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.text; lineHeight: 1.25
    }
    Repeater {
        model: ai.providers.filter(p => p.id !== "custom")
        delegate: Rectangle {
            id: gp
            required property var modelData
            objectName: root.prefix + "-" + modelData.id
            width: root.width
            height: Theme.px(46)
            radius: Theme.radiusSmall
            color: gpMa.containsMouse ? "#ffffff" : Qt.rgba(1, 1, 1, 0.6)
            border.width: 1; border.color: Theme.line
            Column {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.px(12)
                width: parent.width - Theme.px(120)
                Text { text: gp.modelData.name + (gp.modelData.id === ai.provider ? qsTr("（当前）") : ""); font.family: serifFamily; font.pixelSize: Theme.px(13.5); font.bold: true; color: Theme.text }
                Text {
                    visible: gp.modelData.chinaBlocked
                    text: qsTr("中国大陆用不了")
                    font.family: serifFamily; font.pixelSize: Theme.px(11.5); color: Theme.muted
                }
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: Theme.px(12)
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("申请钥匙 ↗")
                font.family: serifFamily; font.pixelSize: Theme.px(13); color: Theme.accent
            }
            MouseArea { id: gpMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ai.openSignup(gp.modelData.id) }
        }
    }
    TpButton {
        objectName: root.prefix + "Settings"
        kind: "primary"
        text: qsTr("去设置里填钥匙")
        onClicked: root.settingsRequested()
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("你问的话会发给你选的那家服务商。钥匙存在 Windows 凭据管理器里，不放进数据文件夹。")
        font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
    }
}
