import QtQuick
import LastMusicPlayer

// The 1px line across a Settings card above a row, drawn by the row itself:
// shown unless the row is the card's first part (at the top, y 0). In the
// card of what cannot be undone it is faintly red.
Rectangle {
    required property Item row
    property bool danger: false

    visible: row.y > 0
    width: row.width
    height: 1
    color: danger ? SettingsStyle.dangerLine : SettingsStyle.line
}
