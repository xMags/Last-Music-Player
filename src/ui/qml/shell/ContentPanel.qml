pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// The content panel's background (styles.css 95-120 and 407-430; motion.css
// 249-290): a rounded-top panel with a diagonal gradient, two soft glows in
// the ambient colour at its top corners, and, while music plays, a third
// glow that drifts slowly along the top edge. All of them are fills of the
// panel's own outline, so they stay inside its rounded corners. The page sits
// on top as a child.
Item {
    id: panel

    property bool playing: false

    // The ambient colour, faded to each new value (--ambient's transition).
    property color ambient: Theme.ambient
    readonly property real strength: Theme.ambientStrength
    readonly property real radius: Theme.radius2xl

    // The drift glow (.content::before): 46% + 560px wide, 560px high,
    // starting at 18% - 280px, then moved by the keyframes below. `driftX`
    // is a share of the glow's width, `driftY` pixels, as in the keyframes.
    readonly property real glowWidth: width * 0.46 + 560
    readonly property real glowLeft: width * 0.18 - 280
    property real driftX: -0.14
    property real driftY: -270
    property real driftScale: 1
    // Without motion the glow keeps no transform, as the reduced-motion rule does.
    readonly property real glowCenterX: glowLeft + glowWidth / 2 + (Motion.reduced ? 0 : driftX) * glowWidth
    readonly property real glowCenterY: 280 + (Motion.reduced ? 0 : driftY)
    readonly property real glowScale: Motion.reduced ? 1 : driftScale
    // It fades out while nothing plays.
    property real glowOpacity: playing ? 1 : 0

    Behavior on ambient {
        enabled: !Motion.reduced
        ColorAnimation { duration: Motion.ambient; easing: Motion.ease }
    }

    Behavior on glowOpacity {
        enabled: !Motion.reduced
        NumberAnimation { duration: Motion.ambientFade; easing: Motion.ease }
    }

    // ambient-drift: 26s, ease-in-out between keyframes, alternating.
    SequentialAnimation {
        running: panel.playing && !Motion.reduced && panel.visible
        loops: Animation.Infinite

        ParallelAnimation {
            NumberAnimation { target: panel; property: "driftX"; to: 0.11; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftY"; to: -210; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftScale"; to: 1.12; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
        }
        ParallelAnimation {
            NumberAnimation { target: panel; property: "driftX"; to: 0.32; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftY"; to: -260; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftScale"; to: 0.94; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
        }
        ParallelAnimation {
            NumberAnimation { target: panel; property: "driftX"; to: 0.05; duration: Motion.ambientDrift * 0.30; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftY"; to: -190; duration: Motion.ambientDrift * 0.30; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftScale"; to: 1.06; duration: Motion.ambientDrift * 0.30; easing: Motion.easeInOut }
        }
        // ...and back again (animation-direction: alternate).
        ParallelAnimation {
            NumberAnimation { target: panel; property: "driftX"; to: 0.32; duration: Motion.ambientDrift * 0.30; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftY"; to: -260; duration: Motion.ambientDrift * 0.30; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftScale"; to: 0.94; duration: Motion.ambientDrift * 0.30; easing: Motion.easeInOut }
        }
        ParallelAnimation {
            NumberAnimation { target: panel; property: "driftX"; to: 0.11; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftY"; to: -210; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftScale"; to: 1.12; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
        }
        ParallelAnimation {
            NumberAnimation { target: panel; property: "driftX"; to: -0.14; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftY"; to: -270; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
            NumberAnimation { target: panel; property: "driftScale"; to: 1; duration: Motion.ambientDrift * 0.35; easing: Motion.easeInOut }
        }
    }

    // An elliptical radial fill of the panel outline: a circular gradient of
    // radius `rx` squeezed vertically to `ry` by the fill transform.
    component EllipseGlow: ShapePath {
        id: glow

        property real cx
        property real cy
        property real rx: 1
        property real ry: 1
        property color inner
        property real solidUntil: 0
        property real fadeEnd: 1

        strokeColor: "transparent"
        fillTransform: Qt.matrix4x4(1, 0, 0, 0, 0, glow.ry / glow.rx, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        fillGradient: RadialGradient {
            centerX: glow.cx
            centerY: glow.cy * glow.rx / glow.ry
            centerRadius: glow.rx
            focalX: centerX
            focalY: centerY
            GradientStop { position: 0; color: glow.inner }
            GradientStop { position: glow.solidUntil; color: glow.inner }
            GradientStop { position: glow.fadeEnd; color: Qt.rgba(glow.inner.r, glow.inner.g, glow.inner.b, 0) }
        }
        PathRectangle {
            width: panel.width
            height: panel.height
            topLeftRadius: panel.radius
            topRightRadius: panel.radius
            bottomLeftRadius: 0
            bottomRightRadius: 0
        }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        // linear-gradient(135deg, panel-grad-1, panel-grad-2): the gradient
        // line runs corner to corner through the centre, (w + h) / sqrt(2) long.
        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: panel.width / 2 - (panel.width + panel.height) / 4
                y1: panel.height / 2 - (panel.width + panel.height) / 4
                x2: panel.width / 2 + (panel.width + panel.height) / 4
                y2: panel.height / 2 + (panel.width + panel.height) / 4
                GradientStop { position: 0; color: Theme.panelGrad1 }
                GradientStop { position: 1; color: Theme.panelGrad2 }
            }
            PathRectangle {
                width: panel.width
                height: panel.height
                topLeftRadius: panel.radius
                topRightRadius: panel.radius
                bottomLeftRadius: 0
                bottomRightRadius: 0
            }
        }

        // radial-gradient(110% 75% at 8% -12%, ambient 20%, transparent 62%)
        EllipseGlow {
            cx: panel.width * 0.08
            cy: panel.height * -0.12
            rx: Math.max(1, panel.width * 1.1)
            ry: Math.max(1, panel.height * 0.75)
            inner: Qt.rgba(panel.ambient.r, panel.ambient.g, panel.ambient.b, 0.20 * panel.strength)
            fadeEnd: 0.62
        }

        // radial-gradient(70% 55% at 100% -8%, ambient 11%, transparent 60%)
        EllipseGlow {
            cx: panel.width
            cy: panel.height * -0.08
            rx: Math.max(1, panel.width * 0.7)
            ry: Math.max(1, panel.height * 0.55)
            inner: Qt.rgba(panel.ambient.r, panel.ambient.g, panel.ambient.b, 0.11 * panel.strength)
            fadeEnd: 0.60
        }

        // The drift glow: radial-gradient(closest-side, ambient 20% 30%, transparent).
        EllipseGlow {
            cx: panel.glowCenterX
            cy: panel.glowCenterY
            rx: Math.max(1, panel.glowWidth / 2 * panel.glowScale)
            ry: Math.max(1, 280 * panel.glowScale)
            inner: Qt.rgba(panel.ambient.r, panel.ambient.g, panel.ambient.b, 0.20 * panel.strength * panel.glowOpacity)
            solidUntil: 0.30
        }
    }
}
