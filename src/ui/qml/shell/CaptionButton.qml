import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// One window caption button (.caption-button, styles.css 196-214): 46px wide,
// the title strip's height, with a CaptionGlyph. Close turns red under
// the pointer; every glyph dims while the window is inactive. Over the
// full-screen player the glyphs are light (styles.css 2316-2318).
T.AbstractButton {
    id: control

    // The glyph: "minimize", "maximize", "restore" or "close".
    required property string kind
    property bool closeButton: false
    // The system answers the pointer over the maximise button, so its hover
    // and press come from the window chrome instead of this item.
    property bool externalHover: false
    property bool externalPress: false
    property bool windowActive: true
    // Over the full-screen player: light glyphs and a faint white hover.
    property bool overPlayer: false

    readonly property bool lit: hovered || externalHover

    // Whole device pixels, so buttons lined up from the window's edge each
    // start on a pixel and their glyphs' one-pixel lines stay sharp.
    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1

    implicitWidth: Math.round(46 * dpr) / dpr
    implicitHeight: Math.round(Theme.titleBarHeight * dpr) / dpr
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    contentItem: CaptionGlyph {
        id: label

        kind: control.kind
        color: control.overPlayer ? Theme.fullscreenCaptionGlyph : control.windowActive ? Theme.fg : Theme.fgTertiary
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
