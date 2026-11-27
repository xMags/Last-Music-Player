import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// The download glyph while songs are on their way (download-badge.ts
// downloadProgressGlyph): the download arrow inside a faint ring (r 9 in a
// 24 box, 1.6 stroke, round caps) that fills clockwise from the top with
// the progress, at least 4% so a start shows. With no progress known
// (`percent` < 0) a quarter arc turns instead, still under reduced motion.
Item {
    id: ring

    property int percent: -1
    property real size: 24
    property color color: Theme.accentColor

    readonly property bool spinning: percent < 0

    implicitWidth: size
    implicitHeight: size

    Shape {
        width: 24
        height: 24
        preferredRendererType: Shape.CurveRenderer
        transform: Scale { xScale: ring.size / 24; yScale: ring.size / 24 }

        // .dl-ring-track: opacity .25.
        ShapePath {
            strokeColor: Qt.alpha(ring.color, 0.25)
            strokeWidth: 1.6
            fillColor: "transparent"
            PathAngleArc { centerX: 12; centerY: 12; radiusX: 9; radiusY: 9; startAngle: 0; sweepAngle: 360 }
        }

        ShapePath {
            strokeColor: ring.color
            strokeWidth: 1.6
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M12 7.5v8M8.5 12.5L12 16L15.5 12.5" }
        }
    }

    // The fill on its own layer, so only it turns while spinning.
    Shape {
        id: fill

        width: 24
        height: 24
        preferredRendererType: Shape.CurveRenderer
        transform: [
            Rotation { id: turn; origin.x: 12; origin.y: 12; angle: 0 },
            Scale { xScale: ring.size / 24; yScale: ring.size / 24 }
        ]

        ShapePath {
            strokeColor: ring.color
            strokeWidth: 1.6
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: 12
                centerY: 12
                radiusX: 9
                radiusY: 9
                startAngle: -90
                sweepAngle: ring.spinning ? 90 : 360 * Math.max(4, Math.min(100, ring.percent)) / 100

                Behavior on sweepAngle {
                    enabled: !Motion.reduced && !ring.spinning
                    NumberAnimation { duration: 240 }
                }
            }
        }

        // dl-spin: 1.1s a turn, linear.
        NumberAnimation {
            target: turn
            property: "angle"
            from: 0
            to: 360
            duration: 1100
            loops: Animation.Infinite
            running: ring.spinning && !Motion.reduced && ring.visible && !Motion.ambientHeld
            onStopped: turn.angle = 0
        }
    }
}
