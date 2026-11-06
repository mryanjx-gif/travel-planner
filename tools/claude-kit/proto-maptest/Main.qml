import QtQuick
import MapLibre 4.0
import MapTestApp

Window {
    id: win
    width: 1200; height: 760; visible: true; color: "#ebf0f4"
    title: "maplibre + QQuickRhiItem 测试"
    property alias map: map

    MapLibre {
        id: map
        objectName: "map"
        anchors.fill: parent
        style: styleUrl
        zoomLevel: 1.6
        coordinate: [30, 100]
    }
    // 验证：地图能否作为纹理被采样（液态玻璃的前提）
    ShaderEffectSource {
        id: mapTex
        objectName: "mapTex"
        sourceItem: map
        live: true
        width: 300; height: 190
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 16
        Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#2d5f8b"; border.width: 2 }
    }
    TriangleItem {
        width: 220; height: 150
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 16
    }
    Text {
        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 12
        text: "styleLoaded=" + map.styleLoaded
        font.pixelSize: 14
    }
}
