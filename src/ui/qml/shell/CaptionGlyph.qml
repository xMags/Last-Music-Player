import QtQuick
import QtQuick.Shapes

// A caption button's glyph (minimise, maximise, restore or close), drawn as
// one-device-pixel lines on whole device pixels. The icon font's glyphs land
// between pixels at fractional display scaling (125%, 150%), which smears
// their thin strokes over two rows. The button must sit on whole device
// pixels itself (CaptionButton sizes it so); the glyph is centred inside it.
Item {
    id: glyph

    // "minimize", "maximize", "restore" or "close".
    required property string kind
    property color color: "black"

    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    // 10 px glyphs and 1 px strokes at 100%, as the system's caption buttons.
    readonly property int sidePx: Math.round(10 * dpr)
    readonly property int strokePx: Math.max(1, Math.floor(dpr))
    // Restore: the front square's offset from the back one.
    readonly property int offsetPx: Math.round(2 * dpr)
    readonly property int leftPx: Math.round((width * dpr - sidePx) / 2)
    readonly property int topPx: Math.round((height * dpr - sidePx) / 2)

    function px(devicePixels) {
        return devicePixels / dpr
    }

    // A square outline whose corner pixels are left out, which reads as the
    // slightly rounded square of the system glyph.
    component Outline: Item {
        id: outline

        property int sidePx
        property int strokePx: 1
        property real dpr: 1
        property color color

        readonly property real side: sidePx / dpr
        readonly property real stroke: strokePx / dpr

        width: side
        height: side

        Rectangle {
            x: outline.stroke; y: 0
            width: outline.side - 2 * outline.stroke; height: outline.stroke
            color: outline.color; antialiasing: false
        }
        Rectangle {
            x: outline.stroke; y: outline.side - outline.stroke
            width: outline.side - 2 * outline.stroke; height: outline.stroke
            color: outline.color; antialiasing: false
        }
        Rectangle {
            x: 0; y: outline.stroke
            width: outline.stroke; height: outline.side - 2 * outline.stroke
            color: outline.color; antialiasing: false
        }
        Rectangle {
            x: outline.side - outline.stroke; y: outline.stroke
            width: outline.stroke; height: outline.side - 2 * outline.stroke
            color: outline.color; antialiasing: false
        }
    }

    Item {
        x: glyph.px(glyph.leftPx)
        y: glyph.px(glyph.topPx)
        width: glyph.px(glyph.sidePx)
        height: width

        // Minimise: a bar across the middle.
        Rectangle {
            visible: glyph.kind === "minimize"
            y: glyph.px(Math.floor((glyph.sidePx - glyph.strokePx) / 2))
            width: parent.width
            height: glyph.px(glyph.strokePx)
            color: glyph.color
            antialiasing: false
        }

        Outline {
            visible: glyph.kind === "maximize"
            sidePx: glyph.sidePx
            strokePx: glyph.strokePx
            dpr: glyph.dpr
            color: glyph.color
        }

        // Restore: a square in front, with the top and right edges of the
        // one behind it showing.
        Item {
            visible: glyph.kind === "restore"
            anchors.fill: parent

            Rectangle {
                x: glyph.px(glyph.offsetPx + glyph.strokePx); y: 0
                width: glyph.px(glyph.sidePx - glyph.offsetPx - 2 * glyph.strokePx); height: glyph.px(glyph.strokePx)
                color: glyph.color; antialiasing: false
            }
            Rectangle {
                x: glyph.px(glyph.sidePx - glyph.strokePx); y: glyph.px(glyph.strokePx)
                width: glyph.px(glyph.strokePx); height: glyph.px(glyph.sidePx - glyph.offsetPx - 2 * glyph.strokePx)
                color: glyph.color; antialiasing: false
            }
            Outline {
                y: glyph.px(glyph.offsetPx)
                sidePx: glyph.sidePx - glyph.offsetPx
                strokePx: glyph.strokePx
                dpr: glyph.dpr
                color: glyph.color
            }
        }

        // Close: two diagonals, which need anti-aliasing at any scale.
        Shape {
            visible: glyph.kind === "close"
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: glyph.color
                strokeWidth: glyph.px(glyph.strokePx)
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap
                startX: 0; startY: 0
                PathLine { x: glyph.px(glyph.sidePx); y: glyph.px(glyph.sidePx) }
                PathMove { x: glyph.px(glyph.sidePx); y: 0 }
                PathLine { x: 0; y: glyph.px(glyph.sidePx) }
            }
        }
    }
}
