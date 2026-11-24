import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// A rounded rectangle filled with linear-gradient(135deg, start, end). On a
// square the CSS gradient line runs exactly from the top-left corner to the
// bottom-right one, which a corner-to-corner gradient reproduces; on other
// shapes it is the same direction, stretched to the box.
Shape {
    id: fill

    property color startColor: "black"
    property color endColor: "black"
    property real radius: 0

    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        strokeWidth: 0
        strokeColor: "transparent"
        fillGradient: LinearGradient {
            x1: 0
            y1: 0
            x2: fill.width
            y2: fill.height
            GradientStop { position: 0; color: fill.startColor }
            GradientStop { position: 1; color: fill.endColor }
        }

        PathRectangle {
            width: fill.width
            height: fill.height
            radius: fill.radius
        }
    }
}
