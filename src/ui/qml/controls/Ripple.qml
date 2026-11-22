pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import LastMusicPlayer

// The water-drop press ripple: a soft disc that swells and fades from the
// press point, and a thin ring that follows a beat later. Fills its parent and
// is clipped to `radius`. Call start() from the control's press handler.
Item {
    id: root

    // Corner radius of the pressed control, so the ripple keeps its shape.
    property real radius: 0
    // Colour of the surface under the ripple; light water is drawn on dark or
    // coloured surfaces and a faint shadow on light ones.
    property color surfaceColor: "transparent"
    readonly property color fillColor: Theme.rippleFillOn(surfaceColor)

    property real diameter: 0
    property point origin: Qt.point(0, 0)

    function start(x, y) {
        if (Motion.reduced || width < 4 || height < 4)
            return
        origin = Qt.point(x, y)
        diameter = Motion.rippleDiameter(width, height, x, y)
        drop.restart()
    }

    anchors.fill: parent
    visible: drop.running
    layer.enabled: drop.running
    layer.effect: MultiEffect {
        maskEnabled: true
        maskSource: mask
    }

    Rectangle {
        id: mask

        anchors.fill: parent
        radius: root.radius
        visible: false
        layer.enabled: true
    }

    Shape {
        id: wave

        x: root.origin.x - root.diameter / 2
        y: root.origin.y - root.diameter / 2
        width: root.diameter
        height: root.diameter
        scale: 0
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: wave.width / 2
                centerY: wave.height / 2
                centerRadius: wave.width / 2
                focalX: centerX
                focalY: centerY
                GradientStop { position: 0; color: root.fillColor }
                GradientStop { position: 0.45; color: root.fillColor }
                GradientStop { position: 0.7; color: "transparent" }
            }
            PathAngleArc {
                centerX: wave.width / 2
                centerY: wave.height / 2
                radiusX: wave.width / 2
                radiusY: wave.height / 2
                sweepAngle: 360
            }
        }
    }

    Rectangle {
        id: ring

        x: root.origin.x - root.diameter / 2
        y: root.origin.y - root.diameter / 2
        width: root.diameter
        height: root.diameter
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: Theme.rippleRingOn(root.surfaceColor)
        opacity: 0
        scale: 0.08
    }

    ParallelAnimation {
        id: drop

        NumberAnimation { target: wave; property: "scale"; from: 0; to: 1; duration: Motion.ripple; easing: Motion.water }
        NumberAnimation { target: wave; property: "opacity"; from: 1; to: 0; duration: Motion.ripple; easing: Motion.water }
        SequentialAnimation {
            PropertyAction { target: ring; property: "opacity"; value: 0 }
            PauseAnimation { duration: Motion.rippleRingDelay }
            ParallelAnimation {
                NumberAnimation { target: ring; property: "scale"; from: 0.08; to: 0.85; duration: Motion.rippleRing; easing: Motion.water }
                NumberAnimation { target: ring; property: "opacity"; from: 0.9; to: 0; duration: Motion.rippleRing; easing: Motion.water }
            }
        }
    }
}
