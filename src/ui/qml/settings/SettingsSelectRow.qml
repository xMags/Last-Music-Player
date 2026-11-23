import QtQuick
import LastMusicPlayer

// A row whose control is a dropdown (.settings-select, 180px or wider).
// `activated` reports the chosen index; the owner writes it and binds
// `currentIndex` back.
SettingsRow {
    id: row

    property var choices: []
    property int currentIndex: 0

    signal activated(int index)

    AppSelect {
        implicitWidth: Math.max(180, implicitContentWidth + leftPadding + rightPadding)
        model: row.choices
        currentIndex: row.currentIndex
        onActivated: index => row.activated(index)
    }
}
