import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// A card's hover light (card-sheen.ts, motion.css 357-374): a glossy sheen
// on the cover that follows the pointer. The card makes it on its first
// hover and keeps it, so it fades out, and in again, like any later hover.
Shape {
    id: sheen

    // The pointer over the cover.
    required property point pointerAt
    // The card is hovered and has a square cover.
    required property bool shown
    // Set by the card once the sheen exists: an item made already shown
    // would start at full opacity instead of fading in.
    property bool lit: false

    readonly property real px: pointerAt.x
    readonly property real py: pointerAt.y
    // CSS's farthest-corner circle.
    readonly property real reach: Math.max(Math.hypot(px, py), Math.hypot(width - px, py),
                                           Math.hypot(px, height - py), Math.hypot(width - px, height - py))

    visible: !Motion.reduced && opacity > 0
    opacity: lit && shown ? 1 : 0

    Behavior on opacity {
        enabled: !Motion.reduced
        NumberAnimation { duration: 300; easing: Motion.ease }
    }

    ShapePath {
        strokeColor: "transparent"
        fillGradient: RadialGradient {
            centerX: sheen.px
            centerY: sheen.py
            focalX: sheen.px
            focalY: sheen.py
            centerRadius: Math.max(1, sheen.reach)
            GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.24) }
            GradientStop { position: 0.34; color: Qt.rgba(1, 1, 1, 0.07) }
            GradientStop { position: 0.6; color: "transparent" }
        }
        PathRectangle {
            width: sheen.width
            height: sheen.height
            radius: Theme.radiusXl
        }
    }
}
