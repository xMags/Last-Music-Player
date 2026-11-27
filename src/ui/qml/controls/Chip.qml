import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A filter chip (.chip, views.css 77-101): a 32px pill with a thin border.
// The selected chip is filled with the accent colour. A quiet chip (the
// Library's playlist filter, views.css 304-317) is 28px high, padded 12px,
// borderless, 12.5px, and its selected state is the soft accent tint.
T.Button {
    id: control

    property bool selected: false
    property bool quiet: false

    implicitHeight: quiet ? 28 : 32
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: quiet ? 12 : 14
    rightPadding: quiet ? 12 : 14
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    font: quiet ? Theme.chipQuietLabel : Theme.chipLabel

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: AppText {
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

        implicitHeight: control.implicitHeight
        radius: height / 2
        color: "transparent"
        border.width: 1
        border.color: control.quiet ? "transparent" : Theme.stroke
        fadeDuration: Motion.chipFade
        fadeEasing: Motion.standard
        rippleEnabled: control.enabled
        focusShown: control.visualFocus
    }

    states: [
        State {
            name: "quietSelected"
            when: control.selected && control.quiet
            PropertyChanges {
                surface.color: Theme.accentSoft
                label.color: Theme.accentColor
            }
        },
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
