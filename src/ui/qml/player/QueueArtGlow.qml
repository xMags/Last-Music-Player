import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// The glow under the playing song's cover (.np-art-glow, motion.css 114-141):
// a soft radial wash of the song colour, solid to 45% of the way out, then
// fading, with the ellipse touching the sides of the box (closest-side). It
// breathes over Motion.nowPlayingBreathe while music plays and holds where it
// is while paused; under reduced motion it rests at a fixed size and opacity.
// The owner places it (inset -2% -14% -24% around the cover).
Item {
    id: glow

    required property bool playing
    // 0 to 1 through one breath, straight in time.
    readonly property real phase: breath.phase
    property color wash: Theme.mixSrgb(Theme.ambient, "transparent", 0.62 * Theme.ambientStrength)

    opacity: Motion.reduced ? Motion.glowRestOpacity : Motion.glowBreathOpacity(phase)
    scale: Motion.reduced ? Motion.glowRestScale : Motion.glowBreathScale(phase)

    Behavior on wash {
        enabled: !Motion.reduced
        ColorAnimation { duration: Motion.ambient; easing: Motion.easeInOut }
    }

    AmbientPhase {
        id: breath

        // Not while the window is minimised or in the tray.
        running: !Motion.reduced && glow.visible && glow.playing && Motion.windowShown
        held: Motion.ambientHeld
        period: Motion.nowPlayingBreathe
        stepInterval: Motion.ambientSlowStep
        // Paused, it holds where it is and goes on from there.
        resume: true
    }

    // A circle as wide as the box, squeezed to its height: an ellipse.
    Shape {
        width: glow.width
        height: glow.width
        preferredRendererType: Shape.CurveRenderer
        transform: Scale { yScale: glow.height / Math.max(1, glow.width) }

        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.width / 2
                centerY: glow.width / 2
                centerRadius: glow.width / 2
                focalX: glow.width / 2
                focalY: glow.width / 2
                GradientStop { position: 0.45; color: glow.wash }
                GradientStop { position: 1; color: Qt.rgba(glow.wash.r, glow.wash.g, glow.wash.b, 0) }
            }

            PathRectangle { width: glow.width; height: glow.width }
        }
    }
}
