// 点击反馈：带回弹（选中、开关、按下的泡泡）
import QtQuick
import TPApp

NumberAnimation {
    property int ms: 260
    duration: Theme.motion ? ms : 0
    easing.type: Easing.BezierSpline
    easing.bezierCurve: Theme.curveSpring
}
