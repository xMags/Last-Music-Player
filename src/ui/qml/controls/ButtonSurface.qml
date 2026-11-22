import QtQuick
import LastMusicPlayer

// The painted surface of a button: its fill and border, the press ripple and
// the keyboard focus ring. Colour changes fade over `fadeDuration`.
Rectangle {
    id: surface

    property bool rippleEnabled: true
    property bool focusShown: false
    property real focusOffset: 2
    property int fadeDuration: Motion.controlFade
    property var fadeEasing: Motion.ease

    Behavior on color {
        enabled: !Motion.reduced
        ColorAnimation { duration: surface.fadeDuration; easing: surface.fadeEasing }
    }
    Behavior on border.color {
        enabled: !Motion.reduced
        ColorAnimation { duration: surface.fadeDuration; easing: surface.fadeEasing }
    }

    Ripple {
        id: ripple

        radius: surface.radius
        surfaceColor: surface.color
    }

    // Passive: sees the press without taking it from the button.
    PointHandler {
        acceptedButtons: Qt.LeftButton
        enabled: surface.rippleEnabled
        onActiveChanged: {
            if (active)
                ripple.start(point.position.x, point.position.y)
        }
    }

    FocusRing {
        shown: surface.focusShown
        controlRadius: surface.radius
        offset: surface.focusOffset
    }
}
