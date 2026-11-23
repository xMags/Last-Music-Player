import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import LastMusicPlayer

// The light band that sweeps across a loading placeholder (styles.css 663-671,
// 786-790): a 105 degree gradient band crosses the whole region, then pauses.
// Per 2.8 s pass it waits 18%, moves for 44% with ease-in-out, waits 38%.
// Fill the region with it, above the blocks. Nothing moves under reduced motion.
Item {
    id: root

    clip: true

    Shape {
        id: band

        width: root.width
        height: root.height
        x: -1.15 * root.width
        preferredRendererType: Shape.CurveRenderer
        visible: !Motion.reduced

        // A 105 degree gradient line through the centre, long enough that the
        // corners sit at 0% and 100% (as CSS sizes it).
        readonly property real lineLength: 0.9659 * width + 0.2588 * height

        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: band.width / 2 - 0.9659 * band.lineLength / 2
                y1: band.height / 2 - 0.2588 * band.lineLength / 2
                x2: band.width / 2 + 0.9659 * band.lineLength / 2
                y2: band.height / 2 + 0.2588 * band.lineLength / 2
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.48; color: Theme.mixSrgb(Theme.surface, "transparent", 0.62) }
                GradientStop { position: 0.59; color: "transparent" }
            }
            PathRectangle { width: band.width; height: band.height }
        }

        SequentialAnimation on x {
            running: !Motion.reduced && Motion.windowShown && root.visible
            loops: Animation.Infinite

            PropertyAction { value: -1.15 * root.width }
            PauseAnimation { duration: Motion.skeletonSweep * 0.18 }
            NumberAnimation {
                from: -1.15 * root.width
                to: 1.15 * root.width
                duration: Motion.skeletonSweep * 0.44
                easing: Motion.easeInOut
            }
            PauseAnimation { duration: Motion.skeletonSweep * 0.38 }
        }
    }
}
