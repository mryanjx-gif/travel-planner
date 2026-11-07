// 面板出现 / 消失：进来缓出，离开缓入且更快（阶段6设计预案 §一）。用法：Behavior on opacity { TpAnim { shown: root.open } }
import QtQuick
import TPApp

NumberAnimation {
    property bool shown: true
    property int ms: 220
    duration: !Theme.motion ? 0 : shown ? ms : 140
    easing.type: Easing.BezierSpline
    easing.bezierCurve: shown ? Theme.curveEnter : Theme.curveExit
}
