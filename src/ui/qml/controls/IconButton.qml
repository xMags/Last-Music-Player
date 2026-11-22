import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A button that shows only an icon. Three forms from the design:
//   Round   - player bar buttons (.pb-btn, styles.css 1403-1415): 32px circle.
//   Square  - row actions (.tr-like, views.css 241-251): 28px, small radius.
//   Overlay - the menu button over artwork (.card-menu-button,
//             styles.css 848-868): 30px circle, white on a dark glass disc.
// `active` shows the on state (liked, repeat on) in the accent colour.
T.Button {
    id: control

    enum Shape { Round, Square, Overlay }

    required property string iconName
    property int shape: IconButton.Round
    property bool active: false
    property real iconSize: 16

    readonly property bool lit: enabled && (hovered || down)
    readonly property bool square: shape === IconButton.Square
    readonly property bool overlay: shape === IconButton.Overlay

    implicitWidth: square ? 28 : overlay ? 30 : 32
    implicitHeight: implicitWidth
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        Icon {
            id: glyph

            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: Theme.fgSecondary
        }
    }

    background: ButtonSurface {
        id: surface

        implicitWidth: control.implicitWidth
        implicitHeight: control.implicitHeight
        radius: control.square ? Theme.radiusSm : width / 2
        color: "transparent"
        border.width: 0
        // The square form has no transition in the design: it changes at once.
        fadeDuration: control.square ? 0 : Motion.controlFade
        rippleEnabled: control.enabled
        focusShown: control.visualFocus
    }

    // The first state whose condition holds wins.
    states: [
        State {
            name: "overlayHover"
            when: control.overlay && control.lit
            PropertyChanges {
                surface.color: Theme.overlayButtonHover
                surface.fadeDuration: Motion.chipFade
                glyph.color: Theme.onOverlay
            }
        },
        State {
            name: "overlay"
            when: control.overlay
            PropertyChanges {
                surface.color: Theme.overlayButton
                surface.fadeDuration: Motion.chipFade
                glyph.color: Theme.onOverlay
            }
        },
        State {
            name: "activeHover"
            when: control.active && control.lit
            PropertyChanges { surface.color: Theme.hover; glyph.color: Theme.accentColor }
        },
        State {
            name: "active"
            when: control.active
            PropertyChanges { glyph.color: Theme.accentColor }
        },
        State {
            name: "hover"
            when: control.lit
            PropertyChanges { surface.color: Theme.hover; glyph.color: Theme.fg }
        },
        State {
            name: "squareRest"
            when: control.square
            PropertyChanges { glyph.color: Theme.fgTertiary }
        }
    ]
}
