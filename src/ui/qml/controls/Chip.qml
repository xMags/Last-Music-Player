import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A filter chip (.chip, views.css 77-101): a 32px pill with a thin border.
// The selected chip is filled with the accent colour.
T.Button {
    id: control

    property bool selected: false

    implicitHeight: 32
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: 14
    rightPadding: 14
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    font: Theme.chipLabel

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Text {
        id: label

        text: control.text
        font: control.font
        color: Theme.fgSecondary
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight

        Behavior on color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.chipFade; easing: Motion.standard }
        }
    }

    background: ButtonSurface {
        id: surface

        implicitHeight: 32
        radius: height / 2
        color: "transparent"
        border.width: 1
        border.color: Theme.stroke
        fadeDuration: Motion.chipFade
        fadeEasing: Motion.standard
        rippleEnabled: control.enabled
        focusShown: control.visualFocus
    }

    states: [
        State {
            name: "selected"
            when: control.selected
            PropertyChanges {
                surface.color: Theme.accentColor
                surface.border.color: "transparent"
                label.color: Theme.accentInk
            }
        },
        State {
            name: "hover"
            when: control.enabled && control.hovered
            PropertyChanges { surface.color: Theme.hover }
        }
    ]
}
