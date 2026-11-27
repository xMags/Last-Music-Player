pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// A song row's 40px number column (common.ts trackRow): the number, a play
// glyph (an alert for an unavailable song) on hover, the bars while it
// plays. The glyph is made on the first hover and kept, the bars only while
// the song is the playing one.
Item {
    id: cell

    required property int number
    required property bool hovered
    required property bool playing
    required property bool unavailable

    onHoveredChanged: {
        if (cell.hovered)
            glyph.active = true
    }

    AppText {
        anchors.centerIn: parent
        visible: !cell.playing && !(cell.hovered && !cell.unavailable)
        text: cell.number
        font: Theme.trackNumber
        color: Theme.fgTertiary
    }

    Loader {
        id: glyph

        anchors.centerIn: parent
        active: false
        sourceComponent: Icon {
            visible: cell.hovered && !cell.playing
            name: cell.unavailable ? "alert" : "play"
            size: 12
            color: cell.unavailable ? Theme.danger : Theme.accentColor
        }
    }

    Loader {
        anchors.centerIn: parent
        active: cell.playing
        sourceComponent: EqBars {
            running: App.playerBar.playing
        }
    }
}