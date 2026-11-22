import QtQuick
import LastMusicPlayer

// The small "nothing here" box (.empty-state.compact, views.css 1800-1815):
// tertiary 13px text, 12px of padding, on the surface with a hairline border
// and the large radius. `boxed: false` drops the surface and border, for the
// always-dark full-screen player.
Rectangle {
    id: box

    required property string text
    property color textColor: Theme.fgTertiary
    property bool boxed: true

    implicitHeight: message.implicitHeight + 2 * 12
    radius: Theme.radiusLg
    color: boxed ? Theme.surface : "transparent"
    border.width: boxed ? 1 : 0
    border.color: Theme.stroke

    Text {
        id: message

        x: 12
        y: 12
        width: box.width - 2 * 12
        text: box.text
        font: Theme.emptyStateCompact
        color: box.textColor
        wrapMode: Text.Wrap
    }
}
