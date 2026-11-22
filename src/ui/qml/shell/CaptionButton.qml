import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// One window caption button (.caption-button, styles.css 196-214): 46px wide,
// the title strip's height, a Segoe Fluent Icons glyph. Close turns red under
// the pointer; every glyph dims while the window is inactive. Over the
// full-screen player the glyphs are light (styles.css 2316-2318).
T.AbstractButton {
    id: control

    // The glyph's character in the caption icon font.
    required property string glyph
    property bool closeButton: false
    // The system answers the pointer over the maximise button, so its hover
    // and press come from the window chrome instead of this item.
    property bool externalHover: false
    property bool externalPress: false
    property bool windowActive: true
    // Over the full-screen player: light glyphs and a faint white hover.
    property bool overPlayer: false

    readonly property bool lit: hovered || externalHover

    implicitWidth: 46
    implicitHeight: Theme.titleBarHeight
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    contentItem: Text {
        id: label

        text: control.glyph
        font: Theme.captionGlyph
        color: control.overPlayer ? Theme.fullscreenCaptionGlyph : control.windowActive ? Theme.fg : Theme.fgTertiary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        id: surface

        color: "transparent"
    }

    states: [
        State {
            name: "closeHover"
            when: control.closeButton && control.lit
            PropertyChanges { surface.color: Theme.captionCloseHover; label.color: Theme.captionCloseGlyph }
        },
        State {
            name: "playerHover"
            when: control.overPlayer && (control.lit || control.down || control.externalPress)
            PropertyChanges { surface.color: Theme.fullscreenButtonHover }
        },
        State {
            name: "hover"
            when: control.lit || control.down || control.externalPress
            PropertyChanges { surface.color: Theme.hover; label.color: Theme.fg }
        }
    ]
}
