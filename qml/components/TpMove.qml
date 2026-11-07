// A 到 B：缓入缓出，到位就停，不回弹（拖动吸附、布局让位、进度）
import QtQuick
import TPApp

NumberAnimation {
    property int ms: 240
    duration: Theme.motion ? ms : 0
    easing.type: Easing.BezierSpline
    easing.bezierCurve: Theme.curveMove
}
