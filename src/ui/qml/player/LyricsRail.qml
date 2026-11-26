import QtQuick
import LastMusicPlayer

// The Lyrics tab of the queue rail (views/queue.ts 160-172): the playing
// song above its lyrics, scrolling away with them as one column, as the Up
// Next tab's header does with the queue. While nothing plays, the idle panel
// shows instead.
Item {
    id: rail

    LyricsPanel {
        anchors.fill: parent
        visible: App.queueRail.hasTrack
        header: Component {
            QueueNowPlayingHeader {}
        }
    }

    QueueIdlePanel {
        anchors.fill: parent
        anchors.bottomMargin: 16
        visible: !App.queueRail.hasTrack
    }
}
