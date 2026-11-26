pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// The hero card's motif in small on a Home shortcut (.home-quick-field,
// styles.css 2554-2569; QUICK_TILE_FIELD in views/home.ts): a dot grid every
// 10px fading in from the right edge, and two field-line arcs at the right,
// drawn in a 200 by 64 box cropped from the right (xMaxYMid slice).
Item {
    id: field

    readonly property color dotColor: Qt.alpha(Theme.fg, 0.14)
    readonly property color lineColor: Qt.alpha(Theme.fg, 0.16)
    readonly property real boxScale: Math.max(width / 200, height / 64)

    // The outer arc runs past the tile, which cuts it (overflow: hidden).
    clip: true

    // A point of the 200 by 64 box, in this item's coordinates.
    function boxX(x) {
        return field.width - (200 - x) * field.boxScale
    }
    function boxY(y) {
        return field.height / 2 + (y - 32) * field.boxScale
    }

    // radial-gradient(circle, color 0.9px, transparent 1.2px) every 10px,
    // under a mask from opaque at the right edge to clear 70% of the way left.
    Canvas {
        id: dots

        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const context = dots.getContext("2d")
            context.reset()
            const dot = field.dotColor
            for (let y = 5; y < height; y += 10) {
                for (let x = 5; x < width; x += 10) {
                    const fade = Math.max(0, 1 - (width - x) / (width * 0.7))
                    if (fade <= 0)
                        continue
                    context.fillStyle = Qt.rgba(dot.r, dot.g, dot.b, dot.a * fade)
                    context.beginPath()
                    context.arc(x, y, 1.05, 0, 2 * Math.PI)
                    context.fill()
                }
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        Connections {
            target: Theme
            function onChanged() {
                dots.requestPaint()
            }
        }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: field.lineColor
            strokeWidth: field.boxScale
            fillColor: "transparent"

            startX: field.boxX(200); startY: field.boxY(4)
            PathCubic {
                control1X: field.boxX(150); control1Y: field.boxY(4)
                control2X: field.boxX(150); control2Y: field.boxY(60)
                x: field.boxX(200); y: field.boxY(60)
            }
            PathMove { x: field.boxX(200); y: field.boxY(-14) }
            PathCubic {
                control1X: field.boxX(110); control1Y: field.boxY(-14)
                control2X: field.boxX(110); control2Y: field.boxY(78)
                x: field.boxX(200); y: field.boxY(78)
            }
        }
    }
}
