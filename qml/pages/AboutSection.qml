// 设置 → 关于（0.12.5，T-13；Electron pages-more.js 的 about 和 creditsRow）。
// 授权核实见 claude/技术选型.md；授权全文放在程序目录的 LICENSES。
import QtQuick
import TPApp

Column {
    id: root
    objectName: "aboutSection"
    property var services: []               // 联网服务（SettingsPanel.netServices，开关那边也用）
    property var credits: []                // 照片署名
    signal logClicked()
    signal toastRequested(string text, bool error)
    spacing: Theme.px(12)

    function refresh() { credits = photos.creditRows() }
    function srcName(s) { return ({ wikimedia: "Wikimedia Commons", nasa: "NASA", own: qsTr("自己拍的"), google: qsTr("Google 地图"), other: qsTr("其他") })[s] || s }
    function isWeb(u) { return /^https?:\/\//i.test(u || "") }

    // 一条署名：名称 + 说明
    component Line: Column {
        property string name: ""
        property string detail: ""
        width: parent ? parent.width : 0
        spacing: Theme.px(2)
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: name
            font.family: serifFamily; font.pixelSize: Theme.px(13); font.bold: true; color: Theme.text
        }
        Text {
            width: parent.width
            visible: detail !== ""
            wrapMode: Text.Wrap
            text: detail
            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
            linkColor: Theme.accent
            onLinkActivated: (link) => Qt.openUrlExternally(link)   // 0.12.6 Exchange Rate API 要求署名带链接
            HoverHandler { cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
        }
    }

    SettingsCard {
        objectName: "setCard-about"
        width: parent.width
        title: qsTr("关于")
        Text {
            width: parent.width
            text: qsTr("旅行计划") + " v" + app.version
            font.family: serifFamily; font.pixelSize: Theme.px(13.5); color: Theme.text
        }
        SettingsRow {
            width: parent.width
            label: qsTr("更新日志")
            sub: qsTr("每个版本改了什么")
            TpButton { objectName: "aboutLog"; compact: true; text: qsTr("查看"); onClicked: root.logClicked() }
        }
        Line { name: qsTr("作者"); detail: qsTr("Jason Yan · 个人作品，免费分享，不用于商业") }
        Line { name: qsTr("系统要求"); detail: qsTr("Windows 10 或 Windows 11（64 位）。显卡优先用 Vulkan，不支持时用 OpenGL。") }
        SettingsRow {
            width: parent.width
            label: qsTr("新手引导")
            sub: qsTr("在地图上把几个常用按钮再指一遍")
            TpButton { objectName: "aboutTour"; compact: true; text: qsTr("再看一次"); onClicked: win.startTour() }
        }
    }

    SettingsCard {
        width: parent.width
        title: qsTr("地图数据来源")
        Line { name: qsTr("街道图"); detail: "Tiles © Esri — Esri, DeLorme, NAVTEQ, USGS, Intermap, iPC, NRCAN, Esri Japan, METI, Esri China (Hong Kong), Esri (Thailand), TomTom" }
        Line { name: qsTr("卫星图"); detail: "Tiles © Esri — Esri, Vantor, Earthstar Geographics, and the GIS User Community" }
        Line { name: qsTr("已解锁底图、省界和国界"); detail: qsTr("Natural Earth（公有领域）") }
        Line { name: qsTr("地球的夜晚灯光"); detail: "NASA Black Marble 2016 — NASA Earth Observatory images by Joshua Stevens, using Suomi NPP VIIRS data from Miguel Román, NASA's Goddard Space Flight Center" }
    }

    SettingsCard {
        width: parent.width
        title: qsTr("联网服务")
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("用途和署名。可以在“联网”里逐项关掉。")
            font.family: serifFamily; font.pixelSize: Theme.px(12); color: Theme.muted
        }
        Repeater {
            model: root.services
            delegate: Line {
                required property var modelData
                name: modelData.name
                detail: modelData.who
            }
        }
    }

    SettingsCard {
        objectName: "aboutCredits"
        width: parent.width
        title: qsTr("照片署名")
        Text {
            visible: root.credits.length === 0
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("现在没有需要署名的照片。")
            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
        }
        Repeater {
            model: root.credits
            delegate: Column {
                required property var modelData
                width: parent ? parent.width : 0
                spacing: Theme.px(2)
                Line {
                    name: modelData.plan || modelData.file
                    detail: [modelData.title, modelData.author, modelData.license, root.srcName(modelData.src)].filter(x => x).join(" · ")
                }
                Row {
                    spacing: Theme.px(12)
                    LinkText { visible: root.isWeb(modelData.url); small: true; text: qsTr("来源页面"); onClicked: Qt.openUrlExternally(modelData.url) }
                    LinkText { visible: root.isWeb(modelData.licenseUrl); small: true; text: qsTr("授权条款"); onClicked: Qt.openUrlExternally(modelData.licenseUrl) }
                }
            }
        }
    }

    SettingsCard {
        width: parent.width
        title: qsTr("开源组件和授权")
        Line { name: "Qt 6.12"; detail: qsTr("界面、网络、定位等。LGPL-3.0，动态链接，可以自行替换这些库。") }
        Line { name: "maplibre-native-qt（399311e）"; detail: qsTr("平面地图。BSD-2-Clause。") }
        Line { name: "MapLibre Native"; detail: qsTr("平面地图的渲染。BSD-2-Clause；内置的第三方组件见授权文件夹里的 maplibre-native-LICENSES.core.md。") }
        Line { name: "suncalc 2.0.2"; detail: qsTr("日出日落，移植成 C++。BSD-2-Clause，Copyright (c) 2026, Volodymyr Agafonkin。") }
        SettingsRow {
            objectName: "aboutLicenses"
            width: parent.width
            label: qsTr("授权全文")
            sub: app.licensesDir()
            TpButton {
                compact: true
                text: qsTr("打开文件夹")
                onClicked: if (!app.openLicensesDir()) root.toastRequested(qsTr("没找到授权文件夹"), true)
            }
        }
    }

    SettingsCard {
        width: parent.width
        title: qsTr("字体")
        Line { name: qsTr("思源宋体（Noto Serif CJK SC）"); detail: qsTr("Adobe / Google，SIL Open Font License 1.1（OFL），打包在程序里。") }
        Line { name: "Material Symbols"; detail: qsTr("Google，Apache 2.0，地图标记的图标，打包在程序里。") }
        Line { name: qsTr("微软雅黑、等线、楷体"); detail: qsTr("Windows 自带，不随程序分发。") }
    }

    SettingsCard {
        width: parent.width
        title: qsTr("界面素材")
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("除了上面列出的，图标、按钮和装饰都是程序绘制的原创素材，没有第三方授权问题。")
            font.family: serifFamily; font.pixelSize: Theme.px(12.5); color: Theme.muted
        }
    }
}
