import QtQuick
import LastMusicPlayer

// The small "nothing here" box (.empty-state.compact, views.css 1800-1815):
// tertiary 13px text, 12px of padding, on the surface with a hairline border
// and the large radius. `boxed: false` drops the surface and border, for the
// always-dark full-screen player. `compact: false` is the plain .empty-state:
// 24px of padding, the extra-large radius and the card shadow.
Rectangle {
    id: box

    required property string text
    property color textColor: Theme.fgTertiary
    property bool boxed: true
    property bool compact: true

    readonly property real pad: compact ? 12 : 24

    implicitHeight: message.implicitHeight + 2 * pad
    radius: compact ? Theme.radiusLg : Theme.radiusXl
    color: boxed ? Theme.surface : "transparent"
    border.width: boxed ? 1 : 0
    border.color: Theme.stroke

    BoxShadow {
        z: -1
        anchors.fill: parent
        visible: box.boxed && !box.compact
        layers: Theme.shadowCard
        radius: box.radius
    }

    Text {
        id: message

        x: box.pad
        y: box.pad
        width: box.width - 2 * box.pad
        text: box.text
        font: Theme.emptyStateCompact
        color: box.textColor
        wrapMode: Text.Wrap
    }
}
