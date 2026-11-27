pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// The application logo (index.html 72; motion.css 181-212): a rounded tile
// with five white bars, drawn in a 256 x 256 box and scaled to `size`. While
// music plays the bars ripple outward from the middle; paused or idle, and
// under reduced motion, it is the still logo.
Item {
    id: mark

    property real size: 26
    property bool playing: false

    readonly property bool rippling: playing && !Motion.reduced

    implicitWidth: size
    implicitHeight: size

    component LogoBar: Rectangle {
        id: bar

        // How long after the middle bar this one starts.
        property int delay: 0

        width: 20
        radius: 10
        color: Theme.brandLogoBar
        // Full height until its delay has passed and whenever the ripple stops.
        transform: Scale {
            origin.x: bar.width / 2
            origin.y: bar.height / 2
            yScale: squeeze.holding ? 1 : squeeze.value
        }

        // 1 to 0.7 and back, half a ripple each way.
        AmbientBounce {
            id: squeeze

            clock: ripple
            delay: bar.delay
            halfPeriod: Motion.logoRipple / 2
            from: 1
            to: 0.7
            easing: Motion.easeInOut
        }
    }

    // The bars share one clock, so each keeps its delay after the middle one.
    AmbientPhase {
        id: ripple

        running: mark.rippling && Motion.windowShown
        held: Motion.windowMoving
        period: Motion.logoRipple
    }

    Item {
        width: 256
        height: 256
        scale: mark.size / 256
        transformOrigin: Item.TopLeft

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "transparent"
                fillGradient: LinearGradient {
                    x1: 0
                    y1: 0
                    x2: 256
                    y2: 256
                    GradientStop { position: 0; color: Theme.brandLogoStart }
                    GradientStop { position: 1; color: Theme.brandLogoEnd }
                }
                PathRectangle { width: 256; height: 256; radius: 52 }
            }
        }

        LogoBar { x: 57; y: 96; height: 64; delay: 2 * Motion.logoRippleStep }
        LogoBar { x: 87.5; y: 79; height: 98; delay: Motion.logoRippleStep }
        LogoBar { x: 117.5; y: 57; height: 142 }
        LogoBar { x: 147.5; y: 79; height: 98; delay: Motion.logoRippleStep }
        LogoBar { x: 178; y: 96; height: 64; delay: 2 * Motion.logoRippleStep }
    }
}
