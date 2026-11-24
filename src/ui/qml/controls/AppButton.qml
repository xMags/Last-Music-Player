import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A text button in one of the design's four styles. Standard, Accent and
// Subtle are the `.btn` family (views.css 33-67); Danger is the destructive
// settings button (views.css 1174-1206) and keeps that button's metrics.
T.Button {
    id: control

    enum Variant { Standard, Accent, Subtle, Danger }

    property int variant: AppButton.Standard
    // Optional icon before the text, by Icons name.
    property string iconName: ""
    property real iconSize: 16
    // A chevron turned to point back, say.
    property real iconRotation: 0

    readonly property bool danger: variant === AppButton.Danger
    readonly property bool lit: enabled && (hovered || down)

    // .btn: height 32px, padding 0 14px, gap 8px; the settings button pads 16px.
    implicitHeight: 32
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: danger ? 16 : 14
    rightPadding: danger ? 16 : 14
    spacing: 8
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    font: danger ? Theme.settingsButtonLabel : Theme.buttonLabel

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Row {
        spacing: control.spacing

        Icon {
            id: glyph

            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: control.iconSize
            rotation: control.iconRotation
            color: label.color
            visible: control.iconName !== ""
        }

        Text {
            id: label

            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: Theme.fg
            elide: Text.ElideRight
        }
    }

    background: ButtonSurface {
        id: surface

        implicitHeight: 32
        radius: Theme.radiusSm
        border.width: 1
        color: Theme.surface
        border.color: Theme.strokeStrong
        rippleEnabled: control.enabled
        focusShown: control.visualFocus
    }

    // The first state whose condition holds wins, so pressed comes before hover.
    states: [
        State {
            name: "standardPressed"
            when: control.variant === AppButton.Standard && control.enabled && control.down
            PropertyChanges { surface.color: Theme.neutral; surface.border.color: Theme.fgDisabled }
        },
        State {
            name: "standardHover"
            when: control.variant === AppButton.Standard && control.lit
            PropertyChanges { surface.color: Theme.surfaceAlt; surface.border.color: Theme.fgDisabled }
        },
        State {
            name: "accentPressed"
            when: control.variant === AppButton.Accent && control.enabled && control.down
            PropertyChanges {
                surface.color: Theme.mixOklab(Theme.accentColor, Theme.fg, 0.78)
                surface.border.color: "transparent"
                label.color: Theme.accentInk
            }
        },
        State {
            name: "accentHover"
            when: control.variant === AppButton.Accent && control.lit
            PropertyChanges {
                surface.color: Theme.mixOklab(Theme.accentColor, Theme.fg, 0.88)
                surface.border.color: "transparent"
                label.color: Theme.accentInk
            }
        },
        State {
            name: "accent"
            when: control.variant === AppButton.Accent
            PropertyChanges {
                surface.color: Theme.accentColor
                surface.border.color: "transparent"
                label.color: Theme.accentInk
            }
        },
        State {
            name: "subtleHover"
            when: control.variant === AppButton.Subtle && control.lit
            PropertyChanges { surface.color: Theme.hover; surface.border.color: "transparent" }
        },
        State {
            name: "subtle"
            when: control.variant === AppButton.Subtle
            PropertyChanges { surface.color: "transparent"; surface.border.color: "transparent" }
        },
        State {
            name: "dangerHover"
            when: control.danger && control.lit
            PropertyChanges {
                surface.color: Theme.mixSrgb(Theme.danger, Theme.surface, 0.08)
                surface.border.color: Theme.mixSrgb(Theme.danger, "transparent", 0.32)
                label.color: Theme.danger
            }
        },
        State {
            name: "danger"
            when: control.danger
            PropertyChanges {
                surface.border.color: Theme.mixSrgb(Theme.danger, "transparent", 0.32)
                label.color: Theme.danger
            }
        }
    ]
}
