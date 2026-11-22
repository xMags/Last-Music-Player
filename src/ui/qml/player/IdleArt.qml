import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import LastMusicPlayer

// What the cover shows while nothing is playing (.idle-art, styles.css
// 1155-1174): a record on the placeholder tint. The disc is 76% of the tile,
// with fine grooves, a soft highlight up and to the left, and a label in the
// accent's seed colour around a dark spindle hole.
Item {
    id: art

    property real radius: Theme.radiusMd

    readonly property real disc: Math.min(width, height) * 0.76

    // linear-gradient(135deg, art-grad-1, art-grad-2)
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0
                y1: 0
                x2: art.width
                y2: art.height
                GradientStop { position: 0; color: Theme.artGrad1 }
                GradientStop { position: 1; color: Theme.artGrad2 }
            }
            PathRectangle { width: art.width; height: art.height; radius: art.radius }
        }
    }

    Item {
        id: record

        anchors.centerIn: parent
        width: art.disc
        height: art.disc

        BoxShadow {
            anchors.fill: parent
            layers: Theme.shadowIdleDisc
            radius: width / 2
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            // repeating-radial-gradient(circle, #1A1A1C 0 1.5px, #26262A 1.5px 3px)
            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: record.width / 2
                    centerY: record.height / 2
                    centerRadius: 3
                    focalX: centerX
                    focalY: centerY
                    spread: ShapeGradient.RepeatSpread
                    GradientStop { position: 0; color: Theme.idleDisc }
                    GradientStop { position: 0.5; color: Theme.idleDisc }
                    GradientStop { position: 0.5; color: Theme.idleDiscGroove }
                    GradientStop { position: 1; color: Theme.idleDiscGroove }
                }
                PathAngleArc {
                    centerX: record.width / 2
                    centerY: record.height / 2
                    radiusX: record.width / 2
                    radiusY: record.height / 2
                    sweepAngle: 360
                }
            }

            // radial-gradient(circle at 34% 30%, white 16%, transparent 46%):
            // 46% of the distance to the farthest corner.
            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: record.width * 0.34
                    centerY: record.height * 0.30
                    centerRadius: record.width * 0.962 * 0.46
                    focalX: centerX
                    focalY: centerY
                    GradientStop { position: 0; color: Theme.idleDiscHighlight }
                    GradientStop { position: 1; color: "transparent" }
                }
                PathAngleArc {
                    centerX: record.width / 2
                    centerY: record.height / 2
                    radiusX: record.width / 2
                    radiusY: record.height / 2
                    sweepAngle: 360
                }
            }
        }

        // The label: the seed colour out to 27% and the hole within 4% of the
        // distance to the corner (the circle's radius times the square root of 2).
        Rectangle {
            anchors.centerIn: parent
            width: record.width * 1.4142 * 0.27
            height: width
            radius: width / 2
            color: Theme.accentSeed
        }

        Rectangle {
            anchors.centerIn: parent
            width: record.width * 1.4142 * 0.04
            height: width
            radius: width / 2
            color: Theme.idleDisc
        }

        // inset 0 0 0 1px: a faint light rim.
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: Theme.idleDiscRing
        }
    }
}
