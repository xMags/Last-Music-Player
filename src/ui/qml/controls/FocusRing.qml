import QtQuick
import LastMusicPlayer

// The keyboard focus outline: a ring in the accent colour drawn `offset`
// pixels outside the control (inside for a negative offset), following its
// corner radius. Shown only while `shown` is true; controls bind it to their
// `visualFocus`, which is set for keyboard focus and not for a click.
Rectangle {
    required property bool shown
    // Corner radius of the control the ring surrounds.
    property real controlRadius: 0
    property real offset: 2

    anchors.fill: parent
    anchors.margins: -(offset + border.width)
    radius: Math.max(0, controlRadius + offset + border.width)
    color: "transparent"
    border.width: Theme.focusRingWidth
    border.color: Theme.accentColor
    visible: shown
}
