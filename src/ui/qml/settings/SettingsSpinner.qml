import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// The busy mark of Settings (.settings-spinner, views.css 1604-1614): a 16px
// ring in the strong stroke colour whose top quarter is the accent, turning
// once every 0.75s.
Item {
    id: spinner

    implicitWidth: 16
    implicitHeight: 16

    Shape {
        id: ring

        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        // The 2px border, drawn on its centre line.
        ShapePath {
            strokeWidth: 2
            strokeColor: Theme.strokeStrong
            fillColor: "transparent"

            PathAngleArc {
                centerX: spinner.width / 2
                centerY: spinner.height / 2
                radiusX: spinner.width / 2 - 1
                radiusY: spinner.height / 2 - 1
                startAngle: 0
                sweepAngle: 360
            }
        }

        // border-top-color: the quarter from 45 degrees left of the top to
        // 45 degrees right of it.
        ShapePath {
            strokeWidth: 2
            strokeColor: Theme.accentColor
            fillColor: "transparent"

            PathAngleArc {
                centerX: spinner.width / 2
                centerY: spinner.height / 2
                radiusX: spinner.width / 2 - 1
                radiusY: spinner.height / 2 - 1
                startAngle: -135
                sweepAngle: 90
            }
        }

        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 750
            loops: Animation.Infinite
            running: spinner.visible && !Motion.reduced
        }
    }
}
