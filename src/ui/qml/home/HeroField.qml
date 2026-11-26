pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// The "Jump back in" card's backdrop art (hero-field.ts): the field lines of
// a magnet lying along the x axis, a soft glow and a dot grid fading in from
// the right. Drawn in a 1000 by 260 box cropped from the right, never
// stretched (xMaxYMid slice), so the centre of the field stays in view on a
// narrow card. The lines swell and the glow brightens slowly, unless motion
// is reduced or the field is out of view (`running`).
Item {
    id: field

    property bool running: true

    readonly property real boxScale: Math.max(width / 1000, height / 260)
    readonly property point magnet: Qt.point(830, 130)
    readonly property color lineColor: Theme.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.6)
    readonly property color strongColor: Theme.dark ? Qt.alpha(Qt.tint(Theme.ambient, Qt.rgba(1, 1, 1, 0.2)), 0.45)
                                                    : Qt.alpha(Theme.ambient, 0.55)
    // 0 at rest, 1 at the top of a breath.
    readonly property real swell: (1 - Math.cos(2 * Math.PI * pulse.phase)) / 2

    // One closed field line r = k sin^2(theta) of a dipole, stretched 1.6 sideways.
    function fieldLine(reach) {
        const steps = 72
        let upper = ""
        let lower = ""
        for (let step = 0; step <= steps; ++step) {
            const theta = Math.PI * step / steps
            const r = reach * Math.pow(Math.sin(theta), 2)
            const x = (field.magnet.x + r * Math.cos(theta) * 1.6).toFixed(1)
            upper += (step === 0 ? "M" : " L") + x + " " + (field.magnet.y - r * Math.sin(theta)).toFixed(1)
            lower += (step === 0 ? "M" : " L") + x + " " + (field.magnet.y + r * Math.sin(theta)).toFixed(1)
        }
        return upper + " " + lower
    }

    clip: true

    // One breath: up over 3 s and down over 3 s, InOutSine both ways, which
    // over the whole loop is (1 - cos(2 pi phase)) / 2. Each start begins
    // at rest; a stop holds the lines and glow where they are.
    AmbientPhase {
        id: pulse

        running: field.running && field.visible && !Motion.reduced && Motion.windowShown
        period: 6000
    }

    Item {
        id: box

        width: 1000
        height: 260
        scale: field.boxScale
        transformOrigin: Item.TopLeft
        x: field.width - 1000 * field.boxScale
        y: (field.height - 260 * field.boxScale) / 2

        // The dot grid, every 14 units, fading out towards the left.
        Canvas {
            id: dots

            anchors.fill: parent
            renderStrategy: Canvas.Cooperative
            onPaint: {
                const context = dots.getContext("2d")
                context.reset()
                const dot = Theme.dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.alpha(Theme.ambient, 0.30)
                for (let x = 2; x < 1000; x += 14) {
                    const fade = Math.max(0, (x / 1000 - 0.4) / 0.6)
                    if (fade <= 0)
                        continue
                    context.fillStyle = Qt.rgba(dot.r, dot.g, dot.b, dot.a * fade)
                    for (let y = 2; y < 260; y += 14) {
                        context.beginPath()
                        context.arc(x, y, 1, 0, 2 * Math.PI)
                        context.fill()
                    }
                }
            }

            Connections {
                target: Theme

                function onChanged() {
                    dots.requestPaint()
                }

                function onAmbientChanged() {
                    dots.requestPaint()
                }
            }
        }

        // The glow: an ellipse whose radial gradient fades to nothing at its
        // own edge (SVG's bounding-box gradient). Drawn as a 440 circle and
        // squashed to 240 high, so the gradient is squashed with it rather
        // than cut off at the top and bottom. It brightens and swells with
        // the lines (hero-field-glow-pulse).
        Item {
            id: glow

            x: field.magnet.x - 220
            y: field.magnet.y - 120
            width: 440
            height: 240
            transformOrigin: Item.Center
            scale: Motion.reduced ? 1 : 1 + 0.12 * field.swell
            opacity: Motion.reduced ? 1 : 0.75 + 0.25 * field.swell

            Shape {
                width: 440
                height: 440
                preferredRendererType: Shape.CurveRenderer
                transform: Scale { yScale: 240 / 440 }

                ShapePath {
                    strokeColor: "transparent"
                    fillGradient: RadialGradient {
                        centerX: 220
                        centerY: 220
                        centerRadius: 220
                        focalX: 220
                        focalY: 220
                        GradientStop { position: 0; color: Theme.dark ? Qt.alpha(Theme.ambient, 0.32) : Qt.rgba(1, 1, 1, 0.7) }
                        GradientStop { position: 1; color: "transparent" }
                    }
                    PathAngleArc {
                        centerX: 220
                        centerY: 220
                        radiusX: 220
                        radiusY: 220
                        sweepAngle: 360
                    }
                }
            }
        }

        Item {
            id: lines

            anchors.fill: parent
            transform: Scale {
                id: breathe

                origin.x: field.magnet.x
                origin.y: field.magnet.y
                xScale: Motion.reduced ? 1 : 1 + 0.035 * field.swell
                yScale: Motion.reduced ? 1 : 1 + 0.035 * field.swell
            }

            Repeater {
                model: [30, 52, 80, 114, 154, 204, 266, 340, 430]

                Shape {
                    id: line

                    required property int index
                    required property int modelData

                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        fillColor: "transparent"
                        strokeColor: line.index % 3 === 1 ? field.strongColor : field.lineColor
                        strokeWidth: line.index % 3 === 1 ? 1.4 : 1
                        capStyle: ShapePath.RoundCap
                        PathSvg { path: field.fieldLine(line.modelData) }
                    }
                }
            }
        }
    }
}
